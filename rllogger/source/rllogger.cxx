/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <rllogger/rllogger.hxx>

#include <OutcomeSnapshot.hxx>
#include <Persist.hxx>
#include <RawCapture.hxx>
#include <SemanticEmitter.hxx>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace rllogger {

namespace {

// Module-local state. Set by initialize() when the env var is present.
// Subsequent steps (raw capture, semantic interceptor, etc.) read this
// to know where to write.
bool g_active = false;
std::filesystem::path g_sessionDir;
std::string g_sessionId;
uint64_t g_sessionStartWallMs = 0;

uint64_t wallTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Minimal JSON escape for the few session fields we emit (session id,
// directory path). The raw / semantic emitters have their own escapers;
// duplicating a small one here avoids pulling in a shared module just
// for two callsites.
std::string escapeAscii(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (const unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (c < 0x20) { out += ' '; }
                else { out += static_cast<char>(c); }
        }
    }
    return out;
}

// Resolve the base directory where session subdirectories live.
//   LO_RL_LOG_DIR set       -> that path verbatim
//   otherwise               -> platform default below
// The default keeps the logger always-on without forcing every
// downstream tooling user to remember the env var. Opt-out is
// LO_RL_LOG_DISABLE=1.
std::filesystem::path resolveBaseDir()
{
    if (const char* explicit_dir = std::getenv("LO_RL_LOG_DIR");
        explicit_dir != nullptr && explicit_dir[0] != '\0')
    {
        return std::filesystem::path(explicit_dir);
    }
#if defined(_WIN32)
    if (const char* appdata = std::getenv("LOCALAPPDATA"); appdata != nullptr)
        return std::filesystem::path(appdata) / "lo-rl-logs";
    if (const char* userprofile = std::getenv("USERPROFILE"); userprofile != nullptr)
        return std::filesystem::path(userprofile) / ".lo-rl-logs";
    return std::filesystem::temp_directory_path() / "lo-rl-logs";
#else
    if (const char* home = std::getenv("HOME"); home != nullptr)
        return std::filesystem::path(home) / ".lo-rl-logs";
    return std::filesystem::temp_directory_path() / "lo-rl-logs";
#endif
}

// Keep the most recent N session dirs under baseDir; remove older
// ones so an always-on logger doesn't grow unboundedly. Errors are
// swallowed — cleanup is best-effort and never blocks a session.
void cleanupOldSessions(const std::filesystem::path& baseDir, size_t keep)
{
    namespace fs = std::filesystem;
    try
    {
        std::vector<fs::path> entries;
        for (const auto& e : fs::directory_iterator(baseDir))
            if (e.is_directory()) entries.push_back(e.path());

        if (entries.size() <= keep) return;

        std::sort(entries.begin(), entries.end(),
                  [](const fs::path& a, const fs::path& b) {
                      std::error_code ec1, ec2;
                      const auto ta = fs::last_write_time(a, ec1);
                      const auto tb = fs::last_write_time(b, ec2);
                      return ta > tb; // newest first
                  });

        std::error_code ec;
        for (size_t i = keep; i < entries.size(); ++i)
            fs::remove_all(entries[i], ec);
    }
    catch (const std::exception&)
    {
    }
}

std::string makeSessionId()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d-%H%M%S", &tm);

#if defined(__unix__) || defined(__APPLE__)
    const long pid = static_cast<long>(::getpid());
#else
    const long pid = 0;
#endif

    char pidBuf[32];
    std::snprintf(pidBuf, sizeof(pidBuf), "-pid%ld", pid);

    return std::string(timeBuf) + pidBuf;
}

void touchEmptyFile(const std::filesystem::path& p)
{
    std::ofstream ofs(p, std::ios::app);
    // Just opening for append is enough to create an empty file; close on scope exit.
}

void emitSessionStart()
{
    std::ostringstream os;
    os << '{'
       << R"("schemaVersion":1,)"
       << R"("eventId":"sem-session-start",)"
       << R"("kind":"lifecycle",)"
       << R"("name":"session_start",)"
       << R"("timestamp":)" << g_sessionStartWallMs << ','
       << R"("sessionId":")" << escapeAscii(g_sessionId) << R"(",)"
       << R"("sessionDir":")" << escapeAscii(g_sessionDir.generic_string()) << R"(")"
       << '}';
    persist::enqueueSemantic(os.str());
}

void emitSessionEnd()
{
    const uint64_t nowMs = wallTimeMs();
    std::ostringstream os;
    os << '{'
       << R"("schemaVersion":1,)"
       << R"("eventId":"sem-session-end",)"
       << R"("kind":"lifecycle",)"
       << R"("name":"session_end",)"
       << R"("timestamp":)" << nowMs << ','
       << R"("durationMs":)" << (nowMs - g_sessionStartWallMs) << ','
       << R"("sessionId":")" << escapeAscii(g_sessionId) << R"(")"
       << '}';
    persist::enqueueSemantic(os.str());
}

void onAtexit()
{
    if (!g_active) return;
    emitSessionEnd();
    // outcome::flushFinal() used to run here, but std::atexit fires
    // after VCL / UNO have begun their own teardown. The UNO calls
    // inside buildAndWrite() then SIGSEGV — try/catch can't trap a
    // segfault. The periodic 250 ms timer guarantees outcome.jsonl
    // is at most one tick stale, which is good enough for V1.
    persist::shutdown();
    g_active = false;
}

} // namespace

SAL_DLLPUBLIC_EXPORT void initialize()
{
    // Opt-out: LO_RL_LOG_DISABLE=1 short-circuits to a true no-op so
    // packaging users / CI / debug builds can suppress the logger
    // without touching the binary.
    if (const char* off = std::getenv("LO_RL_LOG_DISABLE");
        off != nullptr && off[0] != '\0' && off[0] != '0')
    {
        return;
    }

    const std::filesystem::path baseDir = resolveBaseDir();

    std::error_code ec;
    std::filesystem::create_directories(baseDir, ec);
    if (ec)
    {
        std::fprintf(stderr,
                     "rllogger: cannot create base log directory %s: %s\n",
                     baseDir.string().c_str(), ec.message().c_str());
        return;
    }

    // Trim to the last 50 sessions so an always-on default doesn't
    // grow unboundedly. Runs before the new session is created, so
    // the cap is the cap on *previous* sessions.
    cleanupOldSessions(baseDir, 50);

    g_sessionId = makeSessionId();
    g_sessionDir = baseDir / g_sessionId;

    std::filesystem::create_directories(g_sessionDir, ec);
    if (ec)
    {
        std::fprintf(stderr,
                     "rllogger: cannot create session directory %s: %s\n",
                     g_sessionDir.string().c_str(), ec.message().c_str());
        return;
    }

    // Pre-create the three JSONL streams. Subsequent steps append events.
    touchEmptyFile(g_sessionDir / "raw.jsonl");
    touchEmptyFile(g_sessionDir / "semantic.jsonl");
    touchEmptyFile(g_sessionDir / "outcome.jsonl");

    g_active = true;
    g_sessionStartWallMs = wallTimeMs();

    // Start the background writer thread that drains raw.jsonl and
    // semantic.jsonl. Producers (raw/semantic) only push to its
    // queues; the main thread never opens or flushes those files
    // again.
    persist::install(g_sessionDir);

    // Install the raw VCL event listener; key/mouse/focus events start
    // appending to raw.jsonl from here on.
    raw::install(g_sessionDir);

    // Install the semantic dispatch interceptor. The actual UNO
    // subscription is deferred to a VCL idle so it runs after the
    // service manager is fully bootstrapped.
    semantic::install(g_sessionDir);

    // Remember the outcome.jsonl path; the periodic snapshot timer
    // is started lazily from raw::rawEventHandler once the VCL
    // scheduler is alive.
    outcome::install(g_sessionDir);

    // Bracket the logs with start/end lifecycle events and arrange a
    // clean shutdown — final outcome snapshot + writer thread join.
    emitSessionStart();
    std::atexit(onAtexit);

    std::fprintf(stderr,
                 "rllogger: session %s active at %s\n",
                 g_sessionId.c_str(), g_sessionDir.string().c_str());
}

} // namespace rllogger

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

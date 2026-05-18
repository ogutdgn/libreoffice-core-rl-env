/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <rllogger/rllogger.hxx>

#include <RawCapture.hxx>
#include <SemanticEmitter.hxx>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

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

} // namespace

SAL_DLLPUBLIC_EXPORT void initialize()
{
    const char* env = std::getenv("LO_RL_LOG_DIR");
    if (env == nullptr || env[0] == '\0')
    {
        // Logger disabled. Zero overhead path.
        return;
    }

    std::filesystem::path baseDir(env);

    std::error_code ec;
    std::filesystem::create_directories(baseDir, ec);
    if (ec)
    {
        std::fprintf(stderr,
                     "rllogger: cannot create base log directory %s: %s\n",
                     baseDir.string().c_str(), ec.message().c_str());
        return;
    }

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

    // Install the raw VCL event listener; key/mouse/focus events start
    // appending to raw.jsonl from here on.
    raw::install(g_sessionDir);

    // Install the semantic dispatch interceptor. The actual UNO
    // subscription is deferred to a VCL idle so it runs after the
    // service manager is fully bootstrapped.
    semantic::install(g_sessionDir);

    std::fprintf(stderr,
                 "rllogger: session %s active at %s\n",
                 g_sessionId.c_str(), g_sessionDir.string().c_str());
}

} // namespace rllogger

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <Persist.hxx>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

namespace rllogger::persist {

namespace {

// --- Module state -----------------------------------------------------

std::atomic<bool> g_running{false};
std::atomic<bool> g_stop{false};

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<std::string> g_rawQueue;
std::deque<std::string> g_semQueue;

std::ofstream g_rawStream;
std::ofstream g_semStream;

std::thread g_thread;

// --- Writer thread ----------------------------------------------------
//
// Owns g_rawStream / g_semStream. Drains the queues every 250 ms (or
// sooner if notify_one fires). On shutdown the loop drains one last
// time and exits. The main thread never opens, writes, or flushes
// these streams — that's the whole point of this module.

void writerLoop()
{
    using namespace std::chrono;

    while (true)
    {
        std::deque<std::string> rawBatch;
        std::deque<std::string> semBatch;
        bool stopping = false;

        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait_for(lk, milliseconds(250), [] {
                return g_stop.load(std::memory_order_acquire)
                    || !g_rawQueue.empty()
                    || !g_semQueue.empty();
            });
            rawBatch.swap(g_rawQueue);
            semBatch.swap(g_semQueue);
            stopping = g_stop.load(std::memory_order_acquire);
        }

        if (!rawBatch.empty())
        {
            for (const auto& line : rawBatch)
                g_rawStream << line << '\n';
            g_rawStream.flush();
        }
        if (!semBatch.empty())
        {
            for (const auto& line : semBatch)
                g_semStream << line << '\n';
            g_semStream.flush();
        }

        if (stopping)
        {
            // Final drain in case enqueue raced with the stop signal.
            std::deque<std::string> finalRaw;
            std::deque<std::string> finalSem;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                finalRaw.swap(g_rawQueue);
                finalSem.swap(g_semQueue);
            }
            for (const auto& line : finalRaw)
                g_rawStream << line << '\n';
            for (const auto& line : finalSem)
                g_semStream << line << '\n';
            g_rawStream.flush();
            g_semStream.flush();
            break;
        }
    }
}

} // namespace

void install(const std::filesystem::path& sessionDir)
{
    if (g_running.exchange(true)) return;

    g_rawStream.open(sessionDir / "raw.jsonl", std::ios::app);
    g_semStream.open(sessionDir / "semantic.jsonl", std::ios::app);
    if (!g_rawStream.is_open() || !g_semStream.is_open())
    {
        std::fprintf(stderr, "rllogger.persist: cannot open jsonl streams\n");
        g_running.store(false);
        return;
    }

    g_thread = std::thread(writerLoop);
    // Note: no std::atexit registration here — the orchestration is
    // owned by rllogger::initialize() which sequences session_end +
    // final outcome flush + persist::shutdown() in one place.
}

void enqueueRaw(std::string line)
{
    if (!g_running.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_rawQueue.push_back(std::move(line));
    }
    g_cv.notify_one();
}

void enqueueSemantic(std::string line)
{
    if (!g_running.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_semQueue.push_back(std::move(line));
    }
    g_cv.notify_one();
}

void shutdown()
{
    if (!g_running.exchange(false)) return;
    g_stop.store(true, std::memory_order_release);
    g_cv.notify_one();
    if (g_thread.joinable())
        g_thread.join();
    g_rawStream.close();
    g_semStream.close();
}

} // namespace rllogger::persist

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

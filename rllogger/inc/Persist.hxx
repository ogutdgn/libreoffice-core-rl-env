/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <filesystem>
#include <string>

namespace rllogger::persist {

// Open raw.jsonl + semantic.jsonl for append and spawn the writer
// thread that drains them every 250 ms. Idempotent. A std::atexit
// handler is registered the first time to flush + join on shutdown,
// so even hard exits from VCL teardown leave the log consistent.
void install(const std::filesystem::path& sessionDir);

// Enqueue one JSON line (no trailing newline) for the raw / semantic
// stream. Producers hold the queue mutex for a few microseconds and
// return — no file I/O on the calling thread.
void enqueueRaw(std::string line);
void enqueueSemantic(std::string line);

// Signal the writer thread to drain any remaining queues and exit.
// Safe to call multiple times. Normally driven by std::atexit.
void shutdown();

} // namespace rllogger::persist

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

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

namespace rllogger::outcome {

// Remember the session output path for outcome.jsonl. Idempotent.
// The actual periodic snapshotting starts only after retryStart()
// succeeds, which the raw VCL handler attempts on every event until
// the VCL scheduler is up.
void install(const std::filesystem::path& sessionDir);

// Start the 250 ms AutoTimer that builds and overwrites outcome.jsonl
// with a snapshot of the active document. Called repeatedly from the
// raw event handler; the first successful call sets a latch and the
// rest become atomic-load no-ops.
void retryStart();

// Build and write one final snapshot, ignoring the timer. Called by
// the session shutdown handler so outcome.jsonl reflects the actual
// closing document state rather than the last periodic tick.
void flushFinal();

} // namespace rllogger::outcome

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

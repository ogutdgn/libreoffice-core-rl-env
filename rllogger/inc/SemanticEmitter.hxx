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

namespace rllogger::semantic {

// Open the per-session semantic.jsonl writer, install a global
// document-event listener, and arrange for an always-on
// XDispatchRecorder to be attached to every loaded Frame. Idempotent.
void install(const std::filesystem::path& sessionDir);

// Retry the UNO subscription if it failed during install() (the UNO
// service manager isn't fully wired when sofficemain calls
// rllogger::initialize). Called from the raw VCL event handler — by
// the time the first VCL event fires, Application::Execute() is
// running and the UNO context is ready. Cheap atomic-read no-op after
// the first successful attempt.
void retrySubscription();

} // namespace rllogger::semantic

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

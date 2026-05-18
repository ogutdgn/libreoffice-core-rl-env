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

} // namespace rllogger::semantic

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

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

namespace rllogger::raw {

// Open the per-session raw.jsonl writer and install a global VCL event
// listener. Idempotent — repeat calls are no-ops.
void install(const std::filesystem::path& sessionDir);

} // namespace rllogger::raw

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

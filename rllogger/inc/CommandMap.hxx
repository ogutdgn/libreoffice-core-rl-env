/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <string_view>

namespace rllogger::semantic {

// Map a `.uno:Foo` dispatch URL to a stable, RL-friendly semantic
// event name (e.g. `.uno:Bold` → `format_bold`). Returns an empty
// view when no mapping is defined; callers should fall back to the
// raw URL in that case.
std::string_view mapCommand(std::string_view unoUrl);

} // namespace rllogger::semantic

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

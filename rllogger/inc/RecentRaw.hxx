/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <cstdint>

namespace rllogger::raw {

enum class LastRawType : uint8_t
{
    None,
    KeyDown,
    KeyUp,
    MouseDown,
    MouseUp,
    MouseMove,
    FocusIn,
    FocusOut,
    Command,
    Gesture,
};

enum class TargetWidget : uint8_t
{
    Unknown,
    Toolbar,
    MenuBar,
    FloatingMenu, // popup or context-menu floating window
    Document,
};

struct RecentRawSnapshot
{
    LastRawType type = LastRawType::None;
    TargetWidget widget = TargetWidget::Unknown;
    uint64_t timestampMs = 0;
    bool hasModifier = false;
};

// Returns a copy of the most recent raw-event summary. Updated on the
// main thread inside the VCL listener; read on the main thread by the
// semantic emitter at recordDispatch time. No synchronization needed
// because both producer and consumer hold SolarMutex.
RecentRawSnapshot getLastRaw();

} // namespace rllogger::raw

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

namespace rllogger {

// Entry point invoked once at soffice startup. Probes LO_RL_LOG_DIR;
// if unset, returns immediately and installs no hooks. If set, brings
// up the session directory, raw capture, semantic interceptor, and
// background writer thread.
void initialize();

} // namespace rllogger

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

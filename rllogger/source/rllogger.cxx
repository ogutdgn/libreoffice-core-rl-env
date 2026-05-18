/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <rllogger/rllogger.hxx>

namespace rllogger {

void initialize()
{
    // Step 1 scaffold: empty entry point. Subsequent commits add env-var
    // probing, raw capture, semantic interception, outcome snapshots, and
    // the background writer thread.
}

} // namespace rllogger

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

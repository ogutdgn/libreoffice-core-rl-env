# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Library_Library,rllogger))

$(eval $(call gb_Library_set_include,rllogger,\
    -I$(SRCDIR)/rllogger/inc \
    $$(INCLUDE) \
))

$(eval $(call gb_Library_use_sdk_api,rllogger))

$(eval $(call gb_Library_use_libraries,rllogger,\
    comphelper \
    cppu \
    cppuhelper \
    sal \
    tl \
    vcl \
))

$(eval $(call gb_Library_add_exception_objects,rllogger,\
    rllogger/source/rllogger \
    rllogger/source/CommandMap \
    rllogger/source/OutcomeSnapshot \
    rllogger/source/Persist \
    rllogger/source/RawCapture \
    rllogger/source/SemanticEmitter \
))

# vim: set noet sw=4 ts=4:

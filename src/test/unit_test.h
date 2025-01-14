//--------------------------------------------------------------------------
// Copyright (C) 2014-2015 Cisco and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------
// unit_test.h author Russ Combs <rucombs@cisco.com>

#ifndef UNIT_TEST_H
#define UNIT_TEST_H

// Unit test interface

// These are the available arguments to unit_test_mode()
#define UNIT_TEST_MODE_SILENT "silent"
#define UNIT_TEST_MODE_MINIMAL "minimal"
#define UNIT_TEST_MODE_NORMAL "normal"
#define UNIT_TEST_MODE_VERBOSE "verbose"
#define UNIT_TEST_MODE_ENV "env"
#define UNIT_TEST_MODE_OFF "off"

// Use "env" test mode to allow setting verbosity via this environment variable
#define UNIT_TEST_MODE_ENVVAR "CK_VERBOSITY"

void unit_test_mode(const char* = nullptr);
bool unit_test_enabled();
int unit_test();

#endif


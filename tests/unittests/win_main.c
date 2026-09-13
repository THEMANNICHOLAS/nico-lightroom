/*
    This file is part of Ansel,
    Copyright (C) 2026 Aurélien PIERRE.

    Ansel is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Ansel is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Ansel.  If not, see <http://www.gnu.org/licenses/>.
*/

/* The Windows entry point for a unit test binary.
 *
 * Every executable here inherits -municode from ansel_deps, which selects the wide CRT
 * startup: it calls wmain(), never main(), so a test whose entry point is a plain main()
 * does not link at all. This wires up the same wrapper every app in src/apps uses --
 * it forwards UTF-8 argv to the test's own main(). See the BUILD_TESTING comment in the
 * top-level CMakeLists.txt. Compiled into the test targets on WIN32 only. */

#include "win/main_wrapper.h"

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on

/*
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#define LEAN_AND_MEAN
#include <Windows.h>

using callback_functionPtr = void (*) ();

bool trayicon_init(HICON icon, const TCHAR* tooltip);
void trayicon_change_icon(HICON newicon);
void trayicon_remove();

void trayicon_add_item(const TCHAR *text, callback_functionPtr functionPtr);

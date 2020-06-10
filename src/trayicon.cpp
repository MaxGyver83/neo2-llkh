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

#include "trayicon.h"
#include "resources.h"

#include <tchar.h>

#define WM_ICON_CLICK (WM_USER+1)
#define LOG_ERROR(text) MessageBox(NULL, TEXT(text), TEXT("Error"), MB_ICONERROR | MB_OK);
#define WND_CLASS L" toolbar"

enum {
    ID_LAST
};

NOTIFYICONDATA trayicon_data = { };

HMENU popup_menu;

callback_functionPtr *functionptr_array = nullptr;
unsigned item_count = 1;

LRESULT CALLBACK trayicon_messageloop(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool trayicon_init(HICON icon, const TCHAR* tooltip) {
    // build new class name
    TCHAR class_name[256];
    _tcscpy(class_name, tooltip);
    _tcscat(class_name, WND_CLASS);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.style = 0;
    wc.lpfnWndProc = &trayicon_messageloop;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = class_name;
    if (!RegisterClassEx(&wc)) {
        LOG_ERROR("Error registering window class.");
        return false;
    }

    auto hidden_window =
        CreateWindowEx(0, class_name, class_name,
                       0, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, HWND_MESSAGE, NULL,
                       GetModuleHandle(NULL), NULL);

    if (!hidden_window) {
        LOG_ERROR("Error creating hidden window.");
        return false;
    }

    trayicon_data.cbSize = sizeof(trayicon_data);
    trayicon_data.hIcon = icon;
    trayicon_data.hWnd = hidden_window;
    trayicon_data.uID = 1;
    trayicon_data.uCallbackMessage = WM_ICON_CLICK;
    trayicon_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;

    _tcsncpy(trayicon_data.szTip, tooltip, 64);
    trayicon_data.szTip[64] = '\0';
    Shell_NotifyIcon(NIM_ADD, &trayicon_data);

    popup_menu = CreatePopupMenu();

    functionptr_array = (callback_functionPtr*)malloc(sizeof(callback_functionPtr));
    functionptr_array[0] = nullptr;
    return true;
}

void trayicon_change_icon(HICON newicon) {
    trayicon_data.hIcon = newicon;
    Shell_NotifyIcon(NIM_MODIFY, &trayicon_data);
}

void trayicon_remove() {
    Shell_NotifyIcon(NIM_DELETE, &trayicon_data);
    free(functionptr_array);
}

void trayicon_add_item(const TCHAR *text, callback_functionPtr functionPtr) {
    if (!text) {
        functionptr_array[0] = functionPtr;
    } else {
        functionptr_array = (callback_functionPtr*)realloc(functionptr_array, sizeof(callback_functionPtr) * (item_count+1));
        AppendMenu(popup_menu, MF_STRING, ID_LAST + item_count, text);
        functionptr_array[item_count] = functionPtr;
        item_count++;
    }
}

LRESULT CALLBACK trayicon_messageloop(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: break;
    case WM_ICON_CLICK:
        switch (lParam) {
        case WM_LBUTTONUP:
            if (functionptr_array[0]) (*functionptr_array[0]) ();
            break;
        case WM_RBUTTONUP: {
            POINT cursor_position;
            GetCursorPos(&cursor_position);
            TrackPopupMenu(popup_menu, 0, cursor_position.x, cursor_position.y, 0, hWnd, 0);
            break;
            }
        }
        break;
    case WM_COMMAND:
        //find out if a menu item was clicked
        if (lParam >= ID_LAST && lParam < ID_LAST + item_count)
                (*functionptr_array[lParam - ID_LAST + 1]) ();
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

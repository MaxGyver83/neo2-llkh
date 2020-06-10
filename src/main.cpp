/**
 * Alternative Windows driver for the Neo2 based keyboard layouts:
 * Neo2, (www.neo-layout.org)
 * AdNW, AdNWzjßf, KOY (www.adnw.de)
 * bone (https://web.archive.org/web/20180721192908/http://wiki.neo-layout.org/wiki/Bone)
 */
#include "trayicon.h"
#include "resources.h"

#include <tchar.h>
#include <stdio.h>
#include <wchar.h>
#include <stdbool.h>
#include <io.h>
#include <fcntl.h>
#include <type_traits>

#define APPNAME L"neo2-llkh"

enum {
    MAPPING_LEN = 103,
    LEVEL_COUNT = 6,
    SCANCODE_TAB_KEY = 15,
    SCANCODE_CAPSLOCK_KEY = 58,
    SCANCODE_LOWER_THAN_KEY = 86, // <
    SCANCODE_QUOTE_KEY = 40,      // Ä
    SCANCODE_HASH_KEY = 43,       // #
    SCANCODE_RETURN_KEY = 28,
    // SCANCODE_ANY_ALT_KEY = 56,        // Alt or AltGr
};

// Mapping tables for one level
struct KeyMapping {
    TCHAR mapping[MAPPING_LEN] = {};

    template<size_t O, size_t N>
    constexpr auto fillAtLiteral(const TCHAR (&p)[N]) {
        for (auto i = 0u; i < N-1; i++) mapping[O + i] = p[i];
    }
};
struct CharMapping {
    CHAR mapping[MAPPING_LEN] = {};
};

enum Level {
    LEVEL_1 = 0,
    LEVEL_2 = 1,
    LEVEL_3 = 2,
    LEVEL_4 = 3,
    LEVEL_5 = 4,
    LEVEL_6 = 5,
};

struct Layout {
    // mappings for all levels
    KeyMapping levels[LEVEL_COUNT];
    bool is_kou_or_vue{};
    CharMapping level4_specials;

    constexpr auto operator[](const Level level) -> KeyMapping& { return levels[level]; }
};

// Some global settings.
// These values can be set in a configuration file (settings.ini)
struct Settings {
    TCHAR layout[100];                    // keyboard layout (default: neo)
    bool debugWindow = false;            // show debug output in a separate console window
    bool quoteAsMod3R = false;           // use quote/ä as right level 3 modifier
    bool returnAsMod3R = false;          // use return as right level 3 modifier
    bool tabAsMod4L = false;             // use tab as left level 4 modifier
    bool capsLockEnabled = false;        // enable (allow) caps lock
    bool shiftLockEnabled = false;       // enable (allow) shift lock (disabled if capsLockEnabled is true)
    bool level4LockEnabled = false;      // enable (allow) level 4 lock (toggle by pressing both Mod4 keys at the same time)
    bool qwertzForShortcuts = false;     // use QWERTZ when Ctrl, Alt or Win is involved
    bool swapLeftCtrlAndLeftAlt = false; // swap left Ctrl and left Alt key
    bool swapLeftCtrlLeftAltAndLeftWin = false;  // swap left Ctrl, left Alt key and left Win key. Resulting order: Win, Alt, Ctrl (on a standard Windows keyboard)
    bool supportLevels5and6 = false;     // support levels five and six (greek letters and mathematical symbols)
    bool capsLockAsEscape = false;       // if true, hitting CapsLock alone sends Esc
    bool mod3RAsReturn = false;          // if true, hitting Mod3R alone sends Return
    bool mod4LAsTab = false;             // if true, hitting Mod4L alone sends Tab

    DWORD scanCodeMod3L = SCANCODE_CAPSLOCK_KEY;
    DWORD scanCodeMod3R = SCANCODE_HASH_KEY;       // depends on quoteAsMod3R and returnAsMod3R
    DWORD scanCodeMod4L = SCANCODE_LOWER_THAN_KEY; // depends on tabAsMod4L
    // DWORD scanCodeMod4R = SCANCODE_ANY_ALT_KEY;

    void sanitize() {
        if (capsLockEnabled) shiftLockEnabled = false;
        if (swapLeftCtrlLeftAltAndLeftWin) swapLeftCtrlAndLeftAlt = false;

        if (quoteAsMod3R)
            // use ä/quote key instead of #/backslash key as right level 3 modifier
            scanCodeMod3R = SCANCODE_QUOTE_KEY;
        else if (returnAsMod3R)
            // use return key instead of #/backslash as right level 3 modifier
            // (might be useful for US keyboards because the # key is missing there)
            scanCodeMod3R = SCANCODE_RETURN_KEY;

        if (tabAsMod4L)
            // use tab key instead of < key as left level 4 modifier
            // (might be useful for US keyboards because the < key is missing there)
            scanCodeMod4L = SCANCODE_TAB_KEY;
    }
};

struct State {
    // True if no mapping should be done
    bool bypassMode = false;

    bool shiftPressed = false;
    bool mod3Pressed = false;
    bool mod4Pressed = false;

    // States of some keys and shift lock.
    bool shiftLeftPressed = false;
    bool shiftRightPressed = false;
    bool shiftLockActive = false;
    bool capsLockActive = false;
    bool level3modLeftPressed = false;
    bool level3modRightPressed = false;
    bool level3modLeftAndNoOtherKeyPressed = false;
    bool level3modRightAndNoOtherKeyPressed = false;
    bool level4modLeftAndNoOtherKeyPressed = false;

    bool level4modLeftPressed = false;
    bool level4modRightPressed = false;
    bool level4LockActive = false;

    bool ctrlLeftPressed = false;
    bool ctrlRightPressed = false;
    bool altLeftPressed = false;
    bool winLeftPressed = false;
    bool winRightPressed = false;
};

extern void toggleBypassMode();

Settings g_settings;
Layout g_layout;
State g_state;
FILE* g_console = stdout;

void SetStdOutToNewConsole() {
    // allocate a console for this app
    auto success = AllocConsole();
    if (!success) return; // probably has a console already

    // redirect unbuffered STDOUT to the console
    freopen_s(&g_console, "CONOUT$", "w", stdout);

    auto outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    setvbuf(g_console, nullptr, _IONBF, 0);

    // give the console window a nicer title
    SetConsoleTitle(L"neo-llkh Debug Output");

    // give the console window a bigger buffer size
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(outputHandle, &csbi)) {
        COORD bufferSize;
        bufferSize.X = csbi.dwSize.X;
        bufferSize.Y = 9999;
        SetConsoleScreenBufferSize(outputHandle, bufferSize);
    }
}

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
    // Handle the Ctrl-c signal.
    case CTRL_C_EVENT:
        fwprintf(g_console, L"\nCtrl-c detected!\n");
        fwprintf(g_console, L"Please quit by using the tray icon!\n\n");
        return TRUE;

    default:
        return FALSE;
    }
}

template<class T, size_t N>
struct ArrayView {
    T* m;

    constexpr auto operator[](size_t i) const -> T { return m[i]; }
    constexpr auto begin() const -> T* { return m; }
    constexpr auto end() const -> T* { return m+N; }

    constexpr auto findIndex(T f) const -> int {
        for (auto i = 0u; i < N; i++) if (m[i] == f) return i;
        return -1;
    }
};

template <class T, size_t N>
constexpr auto fromString(T (&p)[N]) -> ArrayView<T, N-1> { return {p}; }

constexpr auto lowerLetters = fromString(L"abcdefghijklmnopqrstuvwxyzäöüß.,");
using LetterArray = std::remove_const_t<decltype (lowerLetters)>;

void mapLevelsLetters(const KeyMapping& base, KeyMapping& mappingTableOutput, const LetterArray newChars) {
    for (int i = 0; i < MAPPING_LEN; i++) {
        auto c = lowerLetters.findIndex(base.mapping[i]);
        if (c != -1) {
            //fwprintf(console, L"i = %d: mappingTableLevel1[i] = %c; ptr = %d; ptr = %s; index = %d\n", i, mappingTableLevel1[i], ptr, ptr, ptr-l1_lowercase+1);
            mappingTableOutput.mapping[i] = newChars[c];
        }
    }
}

auto buildLayout(const Settings& settings) -> Layout {
    auto layout = Layout{};

    // same for all layouts
    layout[LEVEL_1].fillAtLiteral<2>(L"1234567890-`");
    layout[LEVEL_1].fillAtLiteral<27>(L"´");

    layout[LEVEL_2].fillAtLiteral<41>(L"̌");  // key to the left of the "1" key
    layout[LEVEL_2].fillAtLiteral<2>(L"°§ℓ»«$€„“”—̧");
    layout[LEVEL_2].fillAtLiteral<27>(L"~");

    layout[LEVEL_3].fillAtLiteral<41>(L"^");
    layout[LEVEL_3].fillAtLiteral<2>(L"¹²³›‹¢¥‚‘’—̊");
    layout[LEVEL_3].fillAtLiteral<16>(L"…_[]^!<>=&ſ̷");
    layout[LEVEL_3].fillAtLiteral<30>(L"\\/{}*?()-:@");
    layout[LEVEL_3].fillAtLiteral<44>(L"#$|~`+%\"';");

    layout[LEVEL_4].fillAtLiteral<41>(L"̇");
    layout[LEVEL_4].fillAtLiteral< 2>(L"ªº№⋮·£¤0/*-¨");
    layout[LEVEL_4].fillAtLiteral<21>(L"¡789+−˝");
    layout[LEVEL_4].fillAtLiteral<35>(L"¿456,.");
    layout[LEVEL_4].fillAtLiteral<49>(L":123;");

    // layout dependent
    if (wcscmp(settings.layout, L"adnw") == 0) {
        layout[LEVEL_1].fillAtLiteral<16>(L"kuü.ävgcljf´");
        layout[LEVEL_1].fillAtLiteral<30>(L"hieaodtrnsß");
        layout[LEVEL_1].fillAtLiteral<44>(L"xyö,qbpwmz");

    } else if (wcscmp(settings.layout, L"adnwzjf") == 0) {
        layout[LEVEL_1].fillAtLiteral<16>(L"kuü.ävgclßz´");
        layout[LEVEL_1].fillAtLiteral<30>(L"hieaodtrnsf");
        layout[LEVEL_1].fillAtLiteral<44>(L"xyö,qbpwmj");

    } else if (wcscmp(settings.layout, L"bone") == 0) {
        layout[LEVEL_1].fillAtLiteral<16>(L"jduaxphlmwß´");
        layout[LEVEL_1].fillAtLiteral<30>(L"ctieobnrsgq");
        layout[LEVEL_1].fillAtLiteral<44>(L"fvüäöyz,.k");

    } else if (wcscmp(settings.layout, L"koy") == 0) {
        layout[LEVEL_1].fillAtLiteral<16>(L"k.o,yvgclßz´");
        layout[LEVEL_1].fillAtLiteral<30>(L"haeiudtrnsf");
        layout[LEVEL_1].fillAtLiteral<44>(L"xqäüöbpwmj");

    } else if (wcscmp(settings.layout, L"kou") == 0
             || wcscmp(settings.layout, L"vou") == 0) {
        layout.is_kou_or_vue = true;
        if (wcscmp(settings.layout, L"kou") == 0) {
            layout[LEVEL_1].fillAtLiteral<16>(L"k.ouäqgclfj´");
            layout[LEVEL_1].fillAtLiteral<30>(L"haeiybtrnsß");
            layout[LEVEL_1].fillAtLiteral<44>(L"zx,üöpdwmv");
        } else {  // vou
            layout[LEVEL_1].fillAtLiteral<16>(L"v.ouäqglhfj´");
            layout[LEVEL_1].fillAtLiteral<30>(L"caeiybtrnsß");
            layout[LEVEL_1].fillAtLiteral<44>(L"zx,üöpdwmk");
        }

        layout[LEVEL_3].fillAtLiteral<16>(L"@%{}^!<>=&€̷");
        layout[LEVEL_3].fillAtLiteral<30>(L"|`()*?/:-_→");
        layout[LEVEL_3].fillAtLiteral<44>(L"#[]~$+\"'\\;");

        layout[LEVEL_4].fillAtLiteral< 4>(L"✔✘·£¤0/*-¨");
        layout[LEVEL_4].fillAtLiteral<21>(L":789+−˝");
        layout[LEVEL_4].fillAtLiteral<35>(L"-456,;");
        layout[LEVEL_4].fillAtLiteral<49>(L"_123.");

    } else { // neo
        layout[LEVEL_1].fillAtLiteral<16>(L"xvlcwkhgfqß´");
        layout[LEVEL_1].fillAtLiteral<30>(L"uiaeosnrtdy");
        layout[LEVEL_1].fillAtLiteral<44>(L"üöäpzbm,.j");
    }

    // map letters of level 2
    mapLevelsLetters(layout[LEVEL_1], layout[LEVEL_2], fromString(L"ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜẞ•–"));

    // map main block on levels 5 and 6
    mapLevelsLetters(layout[LEVEL_1], layout[LEVEL_5], fromString(L"αβχδεφγψιθκλμνοπϕρστuvωξυζηϵüςϑϱ"));  // a-zäöüß.,
    mapLevelsLetters(layout[LEVEL_1], layout[LEVEL_6], fromString(L"∀⇐ℂΔ∃ΦΓΨ∫Θ⨯Λ⇔ℕ∈ΠℚℝΣ∂⊂√ΩΞ∇ℤℵ∩∪∘↦⇒"));

    // add number row and dead key in upper letter row
    layout[LEVEL_5].fillAtLiteral<41>(L"̉");
    layout[LEVEL_5].fillAtLiteral< 2>(L"₁₂₃♂♀⚥ϰ⟨⟩₀?῾");
    layout[LEVEL_5].fillAtLiteral<27>(L"᾿");
    layout[LEVEL_5].mapping[57] = 0x00a0;  // space = no-break space

    layout[LEVEL_6].fillAtLiteral<41>(L"̣");
    layout[LEVEL_6].fillAtLiteral< 2>(L"¬∨∧⊥∡∥→∞∝⌀?̄");
    layout[LEVEL_6].fillAtLiteral<27>(L"˘");
    layout[LEVEL_6].mapping[57] = 0x202f;  // space = narrow no-break space

    // if quote/ä is the right level 3 modifier, copy symbol of quote/ä key to backslash/# key
    if (settings.quoteAsMod3R) {
        layout[LEVEL_1].mapping[43] = layout[LEVEL_1].mapping[40];
        layout[LEVEL_2].mapping[43] = layout[LEVEL_2].mapping[40];
        layout[LEVEL_3].mapping[43] = layout[LEVEL_3].mapping[40];
        layout[LEVEL_4].mapping[43] = layout[LEVEL_4].mapping[40];
        layout[LEVEL_5].mapping[43] = layout[LEVEL_5].mapping[40];
        layout[LEVEL_6].mapping[43] = layout[LEVEL_6].mapping[40];
    }

    layout[LEVEL_2].mapping[8] = 0x20AC;  // €

    layout.level4_specials.mapping[16] = VK_PRIOR;
    if (layout.is_kou_or_vue) {
        layout.level4_specials.mapping[17] = VK_NEXT;
        layout.level4_specials.mapping[18] = VK_UP;
        layout.level4_specials.mapping[19] = VK_BACK;
        layout.level4_specials.mapping[20] = VK_DELETE;
    } else {
        layout.level4_specials.mapping[17] = VK_BACK;
        layout.level4_specials.mapping[18] = VK_UP;
        layout.level4_specials.mapping[19] = VK_DELETE;
        layout.level4_specials.mapping[20] = VK_NEXT;
    }
    layout.level4_specials.mapping[30] = VK_HOME;
    layout.level4_specials.mapping[31] = VK_LEFT;
    layout.level4_specials.mapping[32] = VK_DOWN;
    layout.level4_specials.mapping[33] = VK_RIGHT;
    layout.level4_specials.mapping[34] = VK_END;
    if (layout.is_kou_or_vue) {
        layout.level4_specials.mapping[44] = VK_INSERT;
        layout.level4_specials.mapping[45] = VK_TAB;
        layout.level4_specials.mapping[46] = VK_RETURN;
        layout.level4_specials.mapping[47] = VK_ESCAPE;
    } else {
        layout.level4_specials.mapping[44] = VK_ESCAPE;
        layout.level4_specials.mapping[45] = VK_TAB;
        layout.level4_specials.mapping[46] = VK_INSERT;
        layout.level4_specials.mapping[47] = VK_RETURN;
    }
    layout.level4_specials.mapping[57] = '0';

    return layout;
}

/**
 * Map a key scancode to the char that should be displayed after typing
 **/
auto mapScanCodeToChar(Level level, size_t in) -> TCHAR {
    return g_layout[level].mapping[in];
}

void sendUnicodeChar(TCHAR key) {
    auto input = INPUT{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = key;
    input.ki.dwFlags = KEYEVENTF_UNICODE;
    SendInput(1, &input, sizeof(input));
}

/**
 * Sends a char using emulated keyboard input
 *
 * This works for most cases, but not for dead keys etc
 **/
void sendChar(TCHAR key, KBDLLHOOKSTRUCT keyInfo)
{
    SHORT keyScanResult = VkKeyScanEx(key, GetKeyboardLayout(0));

    if (keyScanResult == -1 || g_state.shiftLockActive || g_state.capsLockActive || g_state.level4LockActive
        || (keyInfo.vkCode >= 0x30 && keyInfo.vkCode <= 0x39)) {
        // key not found in the current keyboard layout or shift lock is active
        //
        // If shiftLockActive is true, a unicode letter will be sent. This implies
        // that shortcuts don't work in shift lock mode. That's good, because
        // people might not be aware that they would send Ctrl-S instead of
        // Ctrl-s. Sending a unicode letter makes it possible to undo shift
        // lock temporarily by holding one shift key because that way the
        // shift key won't be sent.
        //
        // Furthermore, use unicode for number keys.
        sendUnicodeChar(key);
    } else {
        keyInfo.vkCode = keyScanResult;
        char modifiers = keyScanResult >> 8;
        bool shift = ((modifiers & 1) != 0);
        bool alt = ((modifiers & 2) != 0);
        bool ctrl = ((modifiers & 4) != 0);
        bool altgr = alt && ctrl;
        if (altgr) {
            ctrl = false;
            alt = false;
        }

        if (altgr) keybd_event(VK_RMENU, 0, 0, 0);
        if (ctrl) keybd_event(VK_CONTROL, 0, 0, 0);
        if (alt) keybd_event(VK_MENU, 0, 0, 0); // ALT
        if (shift) keybd_event(VK_SHIFT, 0, 0, 0);

        keyInfo.vkCode = keyScanResult;
        keybd_event(keyInfo.vkCode, keyInfo.scanCode, keyInfo.flags, keyInfo.dwExtraInfo);

        if (altgr) keybd_event(VK_RMENU, 0, KEYEVENTF_KEYUP, 0);
        if (ctrl) keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        if (alt) keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0); // ALT
        if (shift) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    }
}

bool handleLayer2SpecialCases(KBDLLHOOKSTRUCT keyInfo) {
    switch(keyInfo.scanCode) {
    case 27:
        sendChar(L'̃', keyInfo);  // perispomene (Tilde)
        return true;
    case 41:
        sendChar(L'̌', keyInfo);  // caron, wedge, háček (Hatschek)
        return true;
    default:
        return false;
    }

}

bool handleLayer3SpecialCases(KBDLLHOOKSTRUCT keyInfo) {
    switch(keyInfo.scanCode) {
    case 13:
        sendChar(L'̊', keyInfo);  // overring
        return true;
    case 20:
        sendChar(L'^', keyInfo);
        keybd_event(VK_SPACE, 0, 0, 0);
        return true;
    case 27:
        sendChar(L'̷', keyInfo);  // bar (diakritischer Schrägstrich)
        return true;
    case 31:
        if (g_layout.is_kou_or_vue) {
            sendChar(L'`', keyInfo);
            keybd_event(VK_SPACE, 0, 0, 0);
            return true;
        }
        return false;
    case 48:
        if (g_layout.is_kou_or_vue) {
            sendChar(L'`', keyInfo);
            keybd_event(VK_SPACE, 0, 0, 0);
            return true;
        }
        return false;
    default:
        return false;
    }

}

bool handleLayer4SpecialCases(KBDLLHOOKSTRUCT keyInfo)
{
    switch(keyInfo.scanCode) {
    case 13:
        sendChar(L'¨', keyInfo);  // diaeresis, umlaut
        return true;
    case 27:
        sendChar(L'˝', keyInfo);  // double acute (doppelter Akut)
        return true;
    case 41:
        sendChar(L'̇', keyInfo);  // dot above (Punkt, darüber)
        return true;
    }

    // A second level 4 mapping table for special (non-unicode) keys.
    // Maybe this could be included in the global TCHAR mapping table or level 4!?
    // byte bScan = 0;
    CHAR mapped = g_layout.level4_specials.mapping[keyInfo.scanCode];

    if (mapped != 0) {
        //		if (mapped == VK_RETURN)
        //			bScan = 0x1c;
        //		else if (mapped == VK_INSERT)
        //			bScan = 0x52;  // or 0x52e0?
        // If arrow key, page up/down, home or end,
        // send flag 0x01 (bit 0 = extended).
        // This in necessary for selecting text with shift + arrow.
        //		if (mapped==VK_LEFT
        //			|| mapped==VK_RIGHT
        //			|| mapped==VK_UP
        //			|| mapped==VK_DOWN
        //			|| mapped==VK_PRIOR
        //			|| mapped==VK_NEXT
        //			|| mapped==VK_HOME
        //			|| mapped==VK_END
        //			|| mapped==VK_INSERT
        //			|| mapped==VK_RETURN)
        // always send extended flag (maybe this fixes mousepad issues)
        keybd_event(mapped, 0, 0x01, 0);
        //		else
        //			keybd_event(mappingTable[keyInfo.scanCode], bScan, 0, 0);
        return true;
    }
    return false;
}

constexpr bool isShift(KBDLLHOOKSTRUCT keyInfo) {
    return keyInfo.vkCode == VK_SHIFT || keyInfo.vkCode == VK_LSHIFT || keyInfo.vkCode == VK_RSHIFT;
}

bool isMod3(KBDLLHOOKSTRUCT keyInfo) {
    return keyInfo.scanCode == g_settings.scanCodeMod3L
        || keyInfo.scanCode == g_settings.scanCodeMod3R;
}

bool isMod4(KBDLLHOOKSTRUCT keyInfo) {
    return keyInfo.scanCode == g_settings.scanCodeMod4L
        || keyInfo.vkCode == VK_RMENU;
}

bool isSystemKeyPressed() {
    return g_state.ctrlLeftPressed || g_state.ctrlRightPressed
        || g_state.altLeftPressed
        || g_state.winLeftPressed || g_state.winRightPressed;
}

constexpr bool isLetter(TCHAR key) {
    return ((key >= L'A' && key <= L'Z')  // A-Z
            || (key >= L'a' && key <= L'z')  // a-z
            || key == L'ä' || key == L'ö'
            || key == L'ü' || key == L'ß'
            || key == L'Ä' || key == L'Ö'
            || key == L'Ü' || key == L'ẞ');
}

auto keyNameFor(DWORD vkCode) -> const wchar_t* {
    switch (vkCode) {
    case VK_LSHIFT: return L"(Shift left)";
    case VK_RSHIFT: return L"(Shift right)";
    case VK_SHIFT: return L"(Shift)";
    case VK_CAPITAL: return L"(M3 left)";
    case 0xde: return g_settings.quoteAsMod3R ? L"(M3 right)" : L"";  // ä
    case 0xbf: return g_settings.quoteAsMod3R ? L"" : L"(M3 right)"; // #
    case VK_OEM_102: return L"(M4 left [<])";
    case VK_CONTROL: return L"(Ctrl)";
    case VK_LCONTROL: return L"(Ctrl left)";
    case VK_RCONTROL: return L"(Ctrl right)";
    case VK_MENU: return L"(Alt)";
    case VK_LMENU: return L"(Alt left)";
    case VK_RMENU: return L"(Alt right)";
    case VK_LWIN: return L"(Win left)";
    case VK_RWIN: return L"(Win right)";
    case VK_BACK: return L"(Backspace)";
    case VK_RETURN: return L"(Return)";
    default:
        if (vkCode >= 0x41 && vkCode <= 0x5a) return L"(A-Z)";
        return L"";
        //keyName = MapVirtualKeyA(keyInfo.vkCode, MAPVK_VK_TO_CHAR);
    }
}

void logKeyEvent(const wchar_t *desc, KBDLLHOOKSTRUCT keyInfo) {
    auto keyName = keyNameFor(keyInfo.vkCode);
    auto shiftLockCapsLockInfo = g_state.shiftLockActive
        ? L" [shift lock active]"
        : (g_state.capsLockActive ? L" [caps lock active]" : L"");
    auto level4LockInfo = g_state.level4LockActive ? L" [level4 lock active]" : L"";
    fwprintf(g_console, L"%-10s sc %lu vk 0x%lx 0x%lx %llu %s%s%s\n",
           desc, keyInfo.scanCode, keyInfo.vkCode,
           keyInfo.flags, keyInfo.dwExtraInfo, keyName, shiftLockCapsLockInfo, level4LockInfo);
}

void logActivation(const wchar_t *name, bool isActive) {
    fwprintf(g_console, L"%s %s!\n", name, isActive ? L"activated" : L"deactivated");
}

__declspec(dllexport)
LRESULT CALLBACK keyevent(int code, WPARAM wparam, LPARAM lparam) {

    if (code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    bool keyUp = wparam == WM_SYSKEYUP || wparam == WM_KEYUP;
    bool keyDown = wparam == WM_SYSKEYDOWN || wparam == WM_KEYDOWN;
    if (!keyUp && !keyDown) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    auto& keyInfo = *((KBDLLHOOKSTRUCT *) lparam);
    if (keyInfo.flags & LLKHF_INJECTED) {
        // process injected events like normal, because most probably we are injecting them
        logKeyEvent(L"injected", keyInfo);
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    if (keyDown && g_state.shiftPressed && keyInfo.scanCode == 69) {
        toggleBypassMode(); // Shift + Pause
        return -1;
    }
    if (g_state.bypassMode) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    if (keyUp) {
        logKeyEvent(L"key up", keyInfo);

        if (isShift(keyInfo)) {
            if (keyInfo.vkCode == VK_RSHIFT) {
                g_state.shiftRightPressed = false;
                if (g_state.shiftLeftPressed) {
                    if (g_settings.shiftLockEnabled) {
                        g_state.shiftLockActive = !g_state.shiftLockActive;
                        logActivation(L"Shift lock", g_state.shiftLockActive);
                    } else if (g_settings.capsLockEnabled) {
                        g_state.capsLockActive = !g_state.capsLockActive;
                        logActivation(L"Caps lock", g_state.capsLockActive);
                    }
                }
                keybd_event(VK_RSHIFT, 54, KEYEVENTF_KEYUP, 0);
            } else {
                g_state.shiftLeftPressed = false;
                if (g_state.shiftRightPressed) {
                    if (g_settings.shiftLockEnabled) {
                        g_state.shiftLockActive = !g_state.shiftLockActive;
                        logActivation(L"Shift lock", g_state.shiftLockActive);
                    } else if (g_settings.capsLockEnabled) {
                        g_state.capsLockActive = !g_state.capsLockActive;
                        logActivation(L"Caps lock", g_state.capsLockActive);
                    }
                }
                keybd_event(VK_LSHIFT, 42, KEYEVENTF_KEYUP, 0);
            }
            g_state.shiftPressed = g_state.shiftLeftPressed || g_state.shiftRightPressed;
            return -1;
        }
        else if (isMod3(keyInfo)) {
            if (keyInfo.scanCode == g_settings.scanCodeMod3R) {
                g_state.level3modRightPressed = false;
                if (g_settings.mod3RAsReturn && g_state.level3modRightAndNoOtherKeyPressed) {
                    // release Mod3_R
                    keybd_event(keyInfo.vkCode, 0, KEYEVENTF_KEYUP, 0);
                    // send Return
                    keybd_event(VK_RETURN, 0, 0x01, 0);
                    g_state.level3modRightAndNoOtherKeyPressed = false;
                    return -1;
                }
            } else {  // scanCodeMod3L (CapsLock)
                g_state.level3modLeftPressed = false;
                if (g_settings.capsLockAsEscape && g_state.level3modLeftAndNoOtherKeyPressed) {
                    // release CapsLock/Mod3_L
                    keybd_event(VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0);
                    // send Escape
                    keybd_event(VK_ESCAPE, 0, 0x01, 0);
                    g_state.level3modLeftAndNoOtherKeyPressed = false;
                    return -1;
                }
            }
            g_state.mod3Pressed = g_state.level3modLeftPressed || g_state.level3modRightPressed;
            return -1;
        }
        else if (isMod4(keyInfo)) {
            if (keyInfo.scanCode == g_settings.scanCodeMod4L) {
                g_state.level4modLeftPressed = false;
                if (g_state.level4modRightPressed && g_settings.level4LockEnabled) {
                    g_state.level4LockActive = !g_state.level4LockActive;
                    logActivation(L"Level4 lock", g_state.level4LockActive);
                } else if (g_settings.mod4LAsTab && g_state.level4modLeftAndNoOtherKeyPressed) {
                    // release Mod4_L
                    keybd_event(keyInfo.vkCode, 0, KEYEVENTF_KEYUP, 0);
                    // send Tab
                    keybd_event(VK_TAB, 0, 0x01, 0);
                    g_state.level4modLeftAndNoOtherKeyPressed = false;
                    return -1;
                }
            } else {  // scanCodeMod4R
                g_state.level4modRightPressed = false;
                if (g_state.level4modLeftPressed && g_settings.level4LockEnabled) {
                    g_state.level4LockActive = !g_state.level4LockActive;
                    logActivation(L"Level4 lock", g_state.level4LockActive);
                }
            }
            g_state.mod4Pressed = g_state.level4modLeftPressed || g_state.level4modRightPressed;
            return -1;
        }

        // Check also the scan code because AltGr sends VK_LCONTROL with scanCode 541
        if (keyInfo.vkCode == VK_LCONTROL && keyInfo.scanCode == 29) {
            if (g_settings.swapLeftCtrlAndLeftAlt) {
                g_state.altLeftPressed = false;
                keybd_event(VK_LMENU, 0, KEYEVENTF_KEYUP, 0);
            } else if (g_settings.swapLeftCtrlLeftAltAndLeftWin) {
                g_state.winLeftPressed = false;
                keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
            } else {
                g_state.ctrlLeftPressed = false;
                keybd_event(VK_LCONTROL, 0, KEYEVENTF_KEYUP, 0);
            }
            return -1;
        } else if (keyInfo.vkCode == VK_RCONTROL) {
            g_state.ctrlRightPressed = false;
        } else if (keyInfo.vkCode == VK_LMENU) {
            if (g_settings.swapLeftCtrlAndLeftAlt || g_settings.swapLeftCtrlLeftAltAndLeftWin) {
                g_state.ctrlLeftPressed = false;
                keybd_event(VK_LCONTROL, 0, KEYEVENTF_KEYUP, 0);
            } else {
                g_state.altLeftPressed = false;
                keybd_event(VK_LMENU, 0, KEYEVENTF_KEYUP, 0);
            }
            return -1;
        } else if (keyInfo.vkCode == VK_LWIN) {
            if (g_settings.swapLeftCtrlLeftAltAndLeftWin) {
                g_state.altLeftPressed = false;
                keybd_event(VK_LMENU, 0, KEYEVENTF_KEYUP, 0);
            } else {
                g_state.winLeftPressed = false;
                keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
            }
            return -1;
        } else if (keyInfo.vkCode == VK_RWIN) {
            g_state.winRightPressed = false;
        }
    }

    else if (keyDown) {
        logKeyEvent(L"\nkey down", keyInfo);

        g_state.level3modLeftAndNoOtherKeyPressed = false;
        g_state.level3modRightAndNoOtherKeyPressed = false;
        g_state.level4modLeftAndNoOtherKeyPressed = false;

        // Check also the scan code because AltGr sends VK_LCONTROL with scanCode 541
        if (keyInfo.vkCode == VK_LCONTROL && keyInfo.scanCode == 29) {
            if (g_settings.swapLeftCtrlAndLeftAlt) {
                g_state.altLeftPressed = true;
                keybd_event(VK_LMENU, 0, 0, 0);
            } else if (g_settings.swapLeftCtrlLeftAltAndLeftWin) {
                g_state.winLeftPressed = true;
                keybd_event(VK_LWIN, 0, 0, 0);
            } else {
                g_state.ctrlLeftPressed = true;
                keybd_event(VK_LCONTROL, 0, 0, 0);
            }
            return -1;
        } else if (keyInfo.vkCode == VK_RCONTROL) {
            g_state.ctrlRightPressed = true;
        } else if (keyInfo.vkCode == VK_LMENU) {
            if (g_settings.swapLeftCtrlAndLeftAlt || g_settings.swapLeftCtrlLeftAltAndLeftWin) {
                g_state.ctrlLeftPressed = true;
                keybd_event(VK_LCONTROL, 0, 0, 0);
            } else {
                g_state.altLeftPressed = true;
                keybd_event(VK_LMENU, 0, 0, 0);
            }
            return -1;
        } else if (keyInfo.vkCode == VK_LWIN) {
            if (g_settings.swapLeftCtrlLeftAltAndLeftWin) {
                g_state.altLeftPressed = true;
                keybd_event(VK_LMENU, 0, 0, 0);
            } else {
                g_state.winLeftPressed = true;
                keybd_event(VK_LWIN, 0, 0, 0);
            }
            return -1;
        } else if (keyInfo.vkCode == VK_RWIN) {
            g_state.winRightPressed = true;
        }

        auto level = [] {
            if (g_settings.supportLevels5and6) {
                if (g_state.shiftPressed != g_state.shiftLockActive && g_state.mod3Pressed) return LEVEL_5;
                if (g_state.mod3Pressed && g_state.mod4Pressed != g_state.level4LockActive) return LEVEL_6;
            }
            if (g_state.mod4Pressed != g_state.level4LockActive) return LEVEL_4;
            if (g_state.mod3Pressed) return LEVEL_3;
            if (g_state.shiftPressed != g_state.shiftLockActive) return LEVEL_2;
            return LEVEL_1;
        }();

        if (isShift(keyInfo)) {
            g_state.shiftPressed = true;
            if (keyInfo.vkCode == VK_RSHIFT) {
                g_state.shiftRightPressed = true;
                keybd_event(VK_RSHIFT, 0, 0, 0);
            } else {
                g_state.shiftLeftPressed = true;
                keybd_event(VK_LSHIFT, 0, 0, 0);
            }
            return -1;
        }
        else if (isMod3(keyInfo)) {
            g_state.mod3Pressed = true;
            if (keyInfo.scanCode == g_settings.scanCodeMod3R) {
                g_state.level3modRightPressed = true;
                g_state.level3modRightAndNoOtherKeyPressed = true;
            }
            else {  // VK_CAPITAL (CapsLock)
                g_state.level3modLeftPressed = true;
                g_state.level3modLeftAndNoOtherKeyPressed = true;
            }
            return -1;
        } else if (isMod4(keyInfo)) {
            g_state.mod4Pressed = true;
            if (keyInfo.scanCode == g_settings.scanCodeMod4L) {
                g_state.level4modLeftPressed = true;
                g_state.level4modLeftAndNoOtherKeyPressed = true;
            } else { // scanCodeMod4R
                g_state.level4modRightPressed = true;
                /* ALTGR triggers two keys: LCONTROL and RMENU
                                   we don't want to have any of those two here effective but return -1 seems
                                   to change nothing, so we simply send keyup here.  */
                keybd_event(VK_RMENU, 0, KEYEVENTF_KEYUP, 0);
            }
            return -1;
        }
        else if (level == LEVEL_2 && handleLayer2SpecialCases(keyInfo)) {
            return -1;
        }
        else if (level == LEVEL_3 && handleLayer3SpecialCases(keyInfo)) {
            return -1;
        }
        else if (level == LEVEL_4 && handleLayer4SpecialCases(keyInfo)) {
            return -1;
        }
        else if (keyInfo.vkCode >= 0x60 && keyInfo.vkCode <= 0x6F) {
            // Numeric keypad -> don't remap
        }
        else if (!(g_settings.qwertzForShortcuts && isSystemKeyPressed())) {
            TCHAR key = mapScanCodeToChar(level, keyInfo.scanCode);
            if (g_state.capsLockActive && (level == LEVEL_1 || level == LEVEL_2) && isLetter(key))
                key = mapScanCodeToChar(level == LEVEL_1 ? LEVEL_2 : LEVEL_1, keyInfo.scanCode);
            if (key != 0 && (keyInfo.flags & LLKHF_INJECTED) == 0) {
                // if key must be mapped
                int character = MapVirtualKeyA(keyInfo.vkCode, MAPVK_VK_TO_CHAR);
                fwprintf(g_console, L"Mapped %lu %c->%c [0x%04X] (level %u)\n", keyInfo.scanCode, character, key, key, level);
                //BYTE state[256];
                //GetKeyboardState(state);
                sendChar(key, keyInfo);
                //SetKeyboardState(state);
                return -1;
            }
        }
    }
    /* Passes the hook information to the next hook procedure in the current hook chain.
         * 1st Parameter hhk - Optional
         * 2nd Parameter nCode - The next hook procedure uses this code to determine how to process the hook information.
         * 3rd Parameter wParam - The wParam value passed to the current hook procedure.
         * 4th Parameter lParam - The lParam value passed to the current hook procedure
         */
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

DWORD WINAPI hookThreadMain(void *user) {
    auto base = GetModuleHandle(nullptr);
    auto msg = MSG{};

    if (!base) {
        base = LoadLibrary((wchar_t *) user);
        if (!base) return 1;
    }

    // Installs an application-defined hook procedure into a hook chain
    // * 1st Parameter idHook: WH_KEYBOARD_LL - The type of hook procedure to be installed.
    // * Installs a hook procedure that monitors low-level keyboard input events.
    // * 2nd Parameter lpfn: LowLevelKeyboardProc - A pointer to the hook procedure.
    // * 3rd Parameter hMod: hExe - A handle to the DLL containing the hook procedure pointed to by the lpfn parameter.
    // * 4th Parameter dwThreadId: 0 - the hook procedure is associated with all existing threads running.
    // * If the function succeeds, the return value is the handle to the hook procedure.
    // * If the function fails, the return value is NULL.
    auto keyhook = SetWindowsHookEx(WH_KEYBOARD_LL, keyevent, base, 0);

    // Message loop retrieves messages from the thread's message queue and dispatches them to the appropriate window procedures.
    // For more info http://msdn.microsoft.com/en-us/library/ms644928%28v=VS.85%29.aspx#creating_loop
    // Retrieves a message from the calling thread's message queue.
    while (GetMessage(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // To free system resources associated with the hook and removes a hook procedure installed in a hook chain
    // Parameter hhk: hKeyHook - A handle to the hook to be removed.
    UnhookWindowsHookEx(keyhook);

    return 0;
}

void exitApplication() {
    trayicon_remove();
    PostQuitMessage(0);
}

void toggleBypassMode() {
    g_state.bypassMode = !g_state.bypassMode;

    auto hInstance = GetModuleHandle(nullptr);
    auto icon = LoadIcon(hInstance, MAKEINTRESOURCE(g_state.bypassMode ? IDI_APPICON_DISABLED : IDI_APPICON));
    trayicon_change_icon(icon);

    fwprintf(g_console, L"%i bypass mode \n", g_state.bypassMode);
}

bool fileExists(LPWSTR szPath) {
    DWORD dwAttrib = GetFileAttributesW(szPath);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void readSettingsIni() {
    // find settings.ini (in same folder as executable)

    // get path of executable
    TCHAR ini[256];
    GetModuleFileNameW(nullptr, ini, sizeof(ini));

    auto pch = wcsrchr(ini, L'\\'); // find last \ in path
    wcscpy(pch+1, L"settings.ini"); // replace neo-llkh.exe by settings.ini

    if (!fileExists(ini)) {
        fwprintf(g_console, L"\nNo settings.ini found: %s\n\n", ini);
        return;
    }

    GetPrivateProfileStringW(L"Settings", L"layout", L"neo", g_settings.layout, sizeof(g_settings.layout), ini);

    TCHAR returnValue[100];
    auto readBool = [&](const wchar_t* ident) {
        GetPrivateProfileStringW(L"Settings", ident, L"0", returnValue, sizeof(returnValue), ini);
        return (wcscmp(returnValue, L"1") == 0);
    };

    g_settings.quoteAsMod3R = readBool(L"symmetricalLevel3Modifiers");
    g_settings.returnAsMod3R = readBool(L"returnKeyAsMod3R");
    g_settings.tabAsMod4L = readBool(L"tabKeyAsMod4L");
    g_settings.capsLockEnabled = readBool(L"capsLockEnabled");
    g_settings.shiftLockEnabled = readBool(L"shiftLockEnabled");
    g_settings.level4LockEnabled = readBool(L"level4LockEnabled");
    g_settings.qwertzForShortcuts = readBool(L"qwertzForShortcuts");
    g_settings.swapLeftCtrlAndLeftAlt = readBool(L"swapLeftCtrlAndLeftAlt");

    g_settings.swapLeftCtrlLeftAltAndLeftWin = readBool(L"swapLeftCtrlLeftAltAndLeftWin");
    g_settings.supportLevels5and6 = readBool(L"supportLevels5and6");
    g_settings.capsLockAsEscape = readBool(L"capsLockAsEscape");
    g_settings.mod3RAsReturn = readBool(L"mod3RAsReturn");
    g_settings.mod4LAsTab = readBool(L"mod4LAsTab");
    g_settings.debugWindow = readBool(L"debugWindow");

    g_settings.sanitize();

    if (g_settings.debugWindow) {
        SetStdOutToNewConsole(); // Open Console Window to see printf output
    }

    fwprintf(g_console, L"\nSetting read from %s:\n", ini);
    fwprintf(g_console, L" Layout: %s\n", g_settings.layout);
    fwprintf(g_console, L" symmetricalLevel3Modifiers: %d\n", g_settings.quoteAsMod3R);
    fwprintf(g_console, L" returnKeyAsMod3R: %d\n", g_settings.returnAsMod3R);
    fwprintf(g_console, L" tabKeyAsMod4L: %d\n", g_settings.tabAsMod4L);
    fwprintf(g_console, L" capsLockEnabled: %d\n", g_settings.capsLockEnabled);
    fwprintf(g_console, L" shiftLockEnabled: %d\n", g_settings.shiftLockEnabled);
    fwprintf(g_console, L" level4LockEnabled: %d\n", g_settings.level4LockEnabled);
    fwprintf(g_console, L" qwertzForShortcuts: %d\n", g_settings.qwertzForShortcuts);
    fwprintf(g_console, L" swapLeftCtrlAndLeftAlt: %d\n", g_settings.swapLeftCtrlAndLeftAlt);
    fwprintf(g_console, L" swapLeftCtrlLeftAltAndLeftWin: %d\n", g_settings.swapLeftCtrlLeftAltAndLeftWin);
    fwprintf(g_console, L" supportLevels5and6: %d\n", g_settings.supportLevels5and6);
    fwprintf(g_console, L" capsLockAsEscape: %d\n", g_settings.capsLockAsEscape);
    fwprintf(g_console, L" mod3RAsReturn: %d\n", g_settings.mod3RAsReturn);
    fwprintf(g_console, L" mod4LAsTab: %d\n", g_settings.mod4LAsTab);
    fwprintf(g_console, L" debugWindow: %d\n\n", g_settings.debugWindow);
}

void readArguments(int argc, LPWSTR * argv) {
    if (argc < 2) return;

    fwprintf(g_console, L"Commandline arguments:");
    for (int i=1; i< argc; i++) {
        auto param = argv[i];
        if (wcscmp(param, L"neo") == 0
            || wcscmp(param, L"adnw") == 0
            || wcscmp(param, L"adnwzjf") == 0
            || wcscmp(param, L"bone") == 0
            || wcscmp(param, L"koy") == 0
            || wcscmp(param, L"kou") == 0
            || wcscmp(param, L"vou") == 0) {
            wcsncpy(g_settings.layout, param, sizeof(g_settings.layout));
            fwprintf(g_console, L"\n Layout: %s", g_settings.layout);
            continue;
        }
        auto equals = wcsstr(param, L"=");
        if (equals == nullptr) {
            fwprintf(g_console, L"\ninvalid arg: %s", param);
            continue;
        }
        equals[0] = 0;
        auto value = equals+1;
        // fwprintf(g_console, L"\n%s = %s", param, value);

        auto handled = false;
        auto boolArg = [&](bool& result, const wchar_t* name) {
            if (handled || wcscmp(param, name) != 0) return false;
            handled = true;
            bool val = value==nullptr ? false : (wcscmp(value, L"1") == 0);
            bool changed = val != result;
            result = val;
            wprintf(L"\n %s: %d", name, val);
            return changed;
        };

        auto debugChanged = boolArg(g_settings.debugWindow, L"debugWindow");
        if (debugChanged && g_settings.debugWindow) {
            // Open Console Window to see printf output
            SetStdOutToNewConsole();
        }
        if (wcscmp(param, L"layout") == 0 && value != nullptr) {
            wcsncpy_s(g_settings.layout, value, sizeof(g_settings.layout));
            fwprintf(g_console, L"\n Layout: %s", g_settings.layout);
        }

        boolArg(g_settings.quoteAsMod3R, L"symmetricalLevel3Modifiers");
        boolArg(g_settings.returnAsMod3R, L"returnKeyAsMod3R");
        boolArg(g_settings.tabAsMod4L, L"tabKeyAsMod4L");
        boolArg(g_settings.capsLockEnabled, L"capsLockEnabled");
        boolArg(g_settings.shiftLockEnabled, L"shiftLockEnabled");
        boolArg(g_settings.level4LockEnabled, L"level4LockEnabled");

        boolArg(g_settings.qwertzForShortcuts, L"qwertzForShortcuts");
        boolArg(g_settings.swapLeftCtrlAndLeftAlt, L"swapLeftCtrlAndLeftAlt");
        boolArg(g_settings.swapLeftCtrlLeftAltAndLeftWin, L"swapLeftCtrlLeftAltAndLeftWin");
        boolArg(g_settings.supportLevels5and6, L"supportLevels5and6");
        boolArg(g_settings.capsLockAsEscape, L"capsLockAsEscape");
        boolArg(g_settings.mod3RAsReturn, L"mod3RAsReturn");
        boolArg(g_settings.mod4LAsTab, L"mod4LAsTab");

        if (!handled) fwprintf(g_console, L"\nUnknown Argument: %s", param);
    }
}

int WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    auto nArgs = int{};
    auto szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);

    readSettingsIni();
    readArguments(nArgs, szArglist);
    g_settings.sanitize();

    fwprintf(g_console, L"\n\n");

    if (g_settings.swapLeftCtrlAndLeftAlt || g_settings.swapLeftCtrlLeftAltAndLeftWin) {
        // catch ctrl-c because it will send keydown for ctrl
        // but then keyup for alt. Then ctrl would be locked.
        SetConsoleCtrlHandler(CtrlHandler, TRUE);
    }

    g_layout = buildLayout(g_settings);

    setbuf(g_console, nullptr);

    // Retrieves a module handle for the specified module.
    // * parameter is NULL, GetModuleHandle returns a handle to the file used to create the calling process (.exe file).
    // * If the function succeeds, the return value is a handle to the specified module.
    // * If the function fails, the return value is NULL.
    trayicon_init(LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON)), APPNAME);
    trayicon_add_item(nullptr, &toggleBypassMode);
    trayicon_add_item(L"Exit", &exitApplication);

    // CreateThread function Creates a thread to execute within the virtual address space of the calling process.
    // * 1st Parameter lpThreadAttributes:  NULL - Thread gets a default security descriptor.
    // * 2nd Parameter dwStackSize:  0  - The new thread uses the default size for the executable.
    // * 3rd Parameter lpStartAddress:  KeyLogger - A pointer to the application-defined function to be executed by the thread
    // * 4th Parameter lpParameter:  argv[0] -  A pointer to a variable to be passed to the thread
    // * 5th Parameter dwCreationFlags: 0 - The thread runs immediately after creation.
    // * 6th Parameter pThreadId(out parameter): NULL - the thread identifier is not returned
    // * If the function succeeds, the return value is a handle to the new thread.
    auto tid = DWORD{};
    auto thread = CreateThread(0, 0, hookThreadMain, szArglist[0], 0, &tid);
    (void)thread;

    auto msg = MSG{};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg); // Translates virtual-key messages into character messages.
        DispatchMessage(&msg); // Dispatches a message to a window procedure.
    }
    return 0;
}

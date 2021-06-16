#define UNICODE
/**
 * Alternative Windows driver for the Neo2 based keyboard layouts:
 * Neo2 (https://neo-layout.org)
 * Bone (https://neo-layout.org/Layouts/bone/)
 * AdNW, AdNWzjßf, KOY (www.adnw.de)
 * VOU (https://maximilian-schillinger.de/vou-layout.html)
 * NeoQwertz (https://neo-layout.org/Layouts/neoqwertz/)
 */

#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <wchar.h>
#include <stdbool.h>
#include "trayicon.h"
#include "resources.h"
#include <io.h>

typedef struct ModState {
	bool shift, mod3, mod4;
} ModState;

HHOOK keyhook = NULL;
HANDLE hConsole;
#define APPNAME "neo-llkh"
#define LEN 103
#define SCANCODE_TAB_KEY 15
#define SCANCODE_CAPSLOCK_KEY 58
#define SCANCODE_LOWER_THAN_KEY 86 // <
#define SCANCODE_QUOTE_KEY 40      // Ä
#define SCANCODE_HASH_KEY 43       // #
#define SCANCODE_RETURN_KEY 28
// #define SCANCODE_ANY_ALT_KEY 56        // Alt or AltGr

enum modTapModifier {
	MT_NONE,
	MT_CTRL,
	MT_SHIFT,
	MT_MOD3,
	MT_MOD4,
	MT_ALT,
	MT_WIN
};

#define FG_WHITE 15
#define FG_YELLOW 14
#define FG_CYAN 11
#define FG_GRAY 8

/**
 * Some global settings.
 * These values can be set in a configuration file (settings.ini)
 */
char layout[100];                    // keyboard layout by name (default: neo)
char customLayout[65];               // custom keyboard layout (32 symbols but probably more than 32 bytes)
TCHAR customLayoutWcs[33];           // custom keyboard layout in UTF-16 (32 symbols)
bool debugWindow = false;            // show debug output in a separate console window
bool quoteAsMod3R = false;           // use quote/ä as right level 3 modifier
bool returnAsMod3R = false;          // use return as right level 3 modifier
bool tabAsMod4L = false;             // use tab as left level 4 modifier
DWORD scanCodeMod3L = SCANCODE_CAPSLOCK_KEY;
DWORD scanCodeMod3R = SCANCODE_HASH_KEY;       // depends on quoteAsMod3R and returnAsMod3R
DWORD scanCodeMod4L = SCANCODE_LOWER_THAN_KEY; // depends on tabAsMod4L
// DWORD scanCodeMod4R = SCANCODE_ANY_ALT_KEY;
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

/**
 * True if no mapping should be done
 */
bool bypassMode = false;

/**
 * States of some keys and shift lock.
 */
bool shiftLeftPressed = false;
bool shiftRightPressed = false;
bool shiftLockActive = false;
bool capsLockActive = false;

bool level3modLeftPressed = false;
bool level3modRightPressed = false;
bool level3modLeftAndNoOtherKeyPressed = false;
bool level3modRightAndNoOtherKeyPressed = false;
bool level4modLeftAndNoOtherKeyPressed = false;
clock_t level3modLeftPressedInstant = 0;
clock_t level3modRightPressedInstant = 0;
clock_t level4modLeftPressedInstant = 0;

bool level4modLeftPressed = false;
bool level4modRightPressed = false;
bool level4LockActive = false;

bool ctrlLeftPressed = false;
bool ctrlRightPressed = false;
bool altLeftPressed = false;
bool winLeftPressed = false;
bool winRightPressed = false;

ModState modState = { false, false, false };

int mapCharacterToScanCode[256] = {0};
/**
 * Mapping tables for four levels.
 * They will be defined in initLayout().
 */
TCHAR mappingTableLevel1[LEN] = {0};
TCHAR mappingTableLevel2[LEN] = {0};
TCHAR mappingTableLevel3[LEN] = {0};
TCHAR mappingTableLevel4[LEN] = {0};
TCHAR mappingTableLevel5[LEN] = {0};
TCHAR mappingTableLevel6[LEN] = {0};
CHAR mappingTableLevel4Special[LEN] = {0};
TCHAR mappingTapNextRelease[LEN] = {0};
TCHAR numpadSlashKey[7];

/**
 * When a key with TapNextRelease function is pressed, the key to emit depends
 * on the order this and succesive keys are being released. Thus all keys
 * are stored in the keyQueue in the first place.
 */
#define QUEUE_SIZE 50
KBDLLHOOKSTRUCT keyQueue[QUEUE_SIZE];
int keyQueueLength;
int keyQueueFirst;
int keyQueueLast;
int keyQueueStatus[QUEUE_SIZE]; // 0=empty/handled, 1=regular key pressed, 2=TapNextRelease key not activated, 3=TapNextRelease key activated
char *MT_MODIFIER_STRING[7] = {"", "CTRL", "SHIFT", "MOD3", "MOD4", "ALT", "WIN"};

typedef struct ModTap {
	int modifier;
	int keycode;
} ModTap;
#define MOD_TAP_LEN 12
ModTap modTap[MOD_TAP_LEN];
int modTapKeyCount = 0;  // how many ModTap keys are defined

bool handleSystemKey(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp);
void handleShiftKey(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp);
void handleMod3Key(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp);
void handleMod4Key(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp);
bool updateStatesAndWriteKey(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp);


void convertToUTF8(TCHAR *wide, char *utf8) {
	int sizeRequired = WideCharToMultiByte(
			CP_UTF8, 0, &wide[0], -1, NULL,
			0, NULL, NULL);
	int bytesWritten = WideCharToMultiByte(
			CP_UTF8, 0, &wide[0], -1, &utf8[0],
			sizeRequired, NULL, NULL);
}

void resetKeyQueue() {
	keyQueueLength = 0;
	keyQueueFirst = 0;
	keyQueueLast = -1;
	memset(keyQueueStatus, 0, sizeof keyQueueStatus);
}

void cleanupKeyQueue() {
	printf("\nkeyQueueStatus:");
	for (int i = keyQueueFirst; i <= keyQueueLast; i++) {
		printf(" %i", keyQueueStatus[i]);
	}
	printf("\n");

	if (keyQueueFirst == 0) {
		int firstEmptyPosition = 0;
		while (keyQueueStatus[firstEmptyPosition])
			firstEmptyPosition++;
		int nextNonEmptyPosition = firstEmptyPosition;
		while (!keyQueueStatus[nextNonEmptyPosition])
			nextNonEmptyPosition++;
		int delta = nextNonEmptyPosition - firstEmptyPosition;
		SetConsoleTextAttribute(hConsole, FG_GRAY);
		printf("cleanupKeyQueue: Move all entries starting from index %i back by %i\n", nextNonEmptyPosition, delta);
		SetConsoleTextAttribute(hConsole, FG_WHITE);
		// if keyQueueFirst = 45, move all entries back by 45
		for (int i = nextNonEmptyPosition; i <= keyQueueLast; i++) {
			keyQueue[i-delta] = keyQueue[i];
			keyQueueStatus[i-delta];
			keyQueueStatus[i] = 0;
		}
		keyQueueLast -= delta;
		return;
	}
	// if keyQueueFirst = 45, move all entries back by 45
	int delta = keyQueueFirst;
	SetConsoleTextAttribute(hConsole, FG_GRAY);
	printf("cleanupKeyQueue: Move all entries back by %i\n", delta);
	SetConsoleTextAttribute(hConsole, FG_WHITE);
	for (int i = keyQueueFirst; i <= keyQueueLast; i++) {
		keyQueue[i-delta] = keyQueue[i];
		// keyQueue[i] = 0;
		keyQueueStatus[i-delta];
		keyQueueStatus[i] = 0;
	}
	keyQueueLast -= delta;
	keyQueueFirst = 0;
}

void SetStdOutToNewConsole() {
	// allocate a console for this app
	AllocConsole();
	// redirect unbuffered STDOUT to the console
	HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	int fileDescriptor = _open_osfhandle((intptr_t)consoleHandle, _A_SYSTEM);
	FILE *fp = _fdopen(fileDescriptor, "w");
	*stdout = *fp;
	setvbuf(stdout, NULL, _IONBF, 0);
	// give the console window a nicer title
	SetConsoleTitle(L"neo-llkh Debug Output");
	// give the console window a bigger buffer size
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (GetConsoleScreenBufferInfo(consoleHandle, &csbi)) {
		COORD bufferSize;
		bufferSize.X = csbi.dwSize.X;
		bufferSize.Y = 9999;
		SetConsoleScreenBufferSize(consoleHandle, bufferSize);
	}
}

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
	switch (fdwCtrlType) {
		// Handle the Ctrl-c signal.
		case CTRL_C_EVENT:
			printf("\nCtrl-c detected!\n");
			if (swapLeftCtrlAndLeftAlt || swapLeftCtrlLeftAltAndLeftWin) {
				printf("Please quit by using the tray icon!\n\n");
				return TRUE;
			} else {
				printf("Exit\n\n");
				trayicon_remove();
				return FALSE;
			}

		// Remove tray icon when terminal (debug window) is being closed
		case CTRL_CLOSE_EVENT:
			printf("Exit\n\n");
			trayicon_remove();
			return FALSE;

		default:
			return FALSE;
	}
}

/**
 * Convert UTF-8 (char) string to UTF-16 (TCHAR) string.
 */
void str2wcs(TCHAR *dest, char *src, size_t n) {
	TCHAR result[n];
	int i = 0;
	int pos = 0;

	for (int i = 0; pos < n && src[i] != 0; i++) {
		int c = src[i]>0 ? (int)src[i] : (int)src[i]+256;
		switch (c) {
			case 0xc3: continue;
			case 0xa4: result[pos] = 0xe4; break; // ä
			case 0xb6: result[pos] = 0xf6; break; // ö
			case 0xbc: result[pos] = 0xfc; break; // ü
			case 0x9f: result[pos] = 0xdf; break; // ß
			default: result[pos] = c;
		}
		pos++;
	}
	result[pos] = 0;
	wcsncpy(dest, result, pos);
}

void handleTapNextReleaseKey(int keyCode, bool isKeyUp) {
	KBDLLHOOKSTRUCT tapNextReleaseKey;
	// memset(&tapNextReleaseKey, 0, sizeof(tapNextReleaseKey));
	switch(keyCode) {
		case MT_CTRL:
			// simulate ctrl key pressed or released
			tapNextReleaseKey.vkCode = VK_LCONTROL;
			tapNextReleaseKey.scanCode = 29;
			tapNextReleaseKey.flags = 0;
			handleSystemKey(tapNextReleaseKey, isKeyUp);
			break;
		case MT_SHIFT:
			// simulate shift key pressed or released
			tapNextReleaseKey.vkCode = VK_SHIFT;
			handleShiftKey(tapNextReleaseKey, isKeyUp);
			break;
		case MT_MOD3:
			// simulate mod3Key pressed or released
			tapNextReleaseKey.scanCode = scanCodeMod3L;
			handleMod3Key(tapNextReleaseKey, isKeyUp);
			handleMod3Key(tapNextReleaseKey, isKeyUp);
			break;
		case MT_MOD4:
			// simulate mod4Key pressed or released
			tapNextReleaseKey.scanCode = scanCodeMod4L;
			handleMod4Key(tapNextReleaseKey, isKeyUp);
			break;
		case MT_ALT:
			// simulate alt key pressed or released
			tapNextReleaseKey.vkCode = VK_LMENU;
			/* tapNextReleaseKey.scanCode = 29; */
			tapNextReleaseKey.flags = 0;
			handleSystemKey(tapNextReleaseKey, isKeyUp);
			break;
		case MT_WIN:
			// simulate windows key pressed or released
			tapNextReleaseKey.vkCode = VK_LWIN;
			/* tapNextReleaseKey.scanCode = 29; */
			tapNextReleaseKey.flags = 0;
			handleSystemKey(tapNextReleaseKey, isKeyUp);
			break;
	}
}

void appendToQueue(KBDLLHOOKSTRUCT keyInfo) {
	// only keyDown events
	if (keyQueueLength && keyInfo.scanCode == keyQueue[keyQueueLast].scanCode)
		return;
	if (keyQueueLast >= QUEUE_SIZE - 1)
		cleanupKeyQueue();
	keyQueueLast++;
	keyQueueLength++;
	int character = MapVirtualKeyA(keyInfo.vkCode, MAPVK_VK_TO_CHAR);
	int tapNextRelease = mappingTapNextRelease[keyInfo.scanCode];
	SetConsoleTextAttribute(hConsole, FG_GRAY);
	printf("Append key '%c%s%s' to queue at index %i ", character, tapNextRelease ? "|" : "", MT_MODIFIER_STRING[tapNextRelease], keyQueueLast);
	if (keyQueueLength == 1)
		printf("(1 key is pressed)\n");
	else if (keyQueueLength > 1)
		printf("(%i keys are pressed)\n", keyQueueLength);
	else
		printf("\n");
	SetConsoleTextAttribute(hConsole, FG_WHITE);
	// printf("keyQueueFirst: %i, keyQueueLast: %i, keyQueueLength: %i\n", keyQueueFirst, keyQueueLast, keyQueueLength);
	keyQueue[keyQueueLast] = keyInfo;
	keyQueueStatus[keyQueueLast] = tapNextRelease ? 2 : 1;
}

// returns true, key release has been handled
bool checkQueue(KBDLLHOOKSTRUCT keyInfo) {
	// only keyUp events
	// definiton: tap = press + release
	bool keyFoundInQueue = false;
	int i = keyQueueFirst;
	while (i <= keyQueueLast) {
		// find released key in queue
		if (keyQueueStatus[i] > 0 && keyInfo.scanCode == keyQueue[i].scanCode) {
			keyFoundInQueue = true;
			// printf("Key released is at index %i in queue.\n", i);
			// no matter what type of key it is:
			// check if keys in the queue pressed earlier are unactivated tap-next-release keys
			for (int j=keyQueueFirst; j<i; j++) {
				if (keyQueueStatus[j] == 2) {
					// send key down for tap-next-release function of this key
					handleTapNextReleaseKey(mappingTapNextRelease[keyQueue[j].scanCode], false);
					// update status (mark as activated)
					keyQueueStatus[j] = 3;
				}
			}
			// depending on key type
			if (keyQueueStatus[i] <= 2) {
				// regular key (no tap-next-release function) or
				// tap-next-release key which has not been activated
				updateStatesAndWriteKey(keyQueue[i], false); // key down
				// release key
				keyQueue[i].flags += 0x80;
				// TODO: Es wäre besser hier down und up auf einmal zu senden, damit notwendige Modifier nicht zweimal gesendet werden.
				updateStatesAndWriteKey(keyQueue[i], true); // key up
			} else {
				// tap-next-release key which was activated
				// send key up for alternative mapping
				handleTapNextReleaseKey(mappingTapNextRelease[keyQueue[i].scanCode], true);
			}
			// set status to 0 (=handled)
			keyQueueStatus[i] = 0;
			int character = MapVirtualKeyA(keyInfo.vkCode, MAPVK_VK_TO_CHAR);
			int tapNextRelease = mappingTapNextRelease[keyInfo.scanCode];
			SetConsoleTextAttribute(hConsole, FG_GRAY);
			printf("Remove key '%c%s%s' from queue at index %i ", character, tapNextRelease ? "|" : "", MT_MODIFIER_STRING[tapNextRelease], i);
			if (keyQueueLength-1 == 1)
				printf("(1 key is pressed)\n");
			else if (keyQueueLength-1 > 1)
				printf("(%i keys are pressed)\n", keyQueueLength);
			else
				printf("\n");
			SetConsoleTextAttribute(hConsole, FG_WHITE);
			// if beginning of queue, move it to next tap-next-release key
			if (i == keyQueueFirst) {
				// queue always begins with tap-next-release keys
				// for (int j=i+1; j<keyQueueLast; j++) {
				int j = i + 1;
				while (j <= keyQueueLast) {
					if (keyQueueStatus[j] >= 2) {
						// make this position the beginning of the queue
						keyQueueFirst = j;
						keyQueueLength--;
						break;
					} else if (keyQueueStatus[j] == 1) {
						// press this key (key down was held back, now it does not depend of other key states anymore)
						updateStatesAndWriteKey(keyQueue[j], false); // key down
						keyQueueLength--;
					}
					j++;
				}
				if (j > keyQueueLast)
					resetKeyQueue();
			} else if (i == keyQueueLast) {
				int j = i - 1;
				while (j >= keyQueueFirst) {
					if (keyQueueStatus[j] > 0) {
						keyQueueLength--;
						keyQueueLast = j;
						break;
					}
					j--;
				}
				if (j < keyQueueFirst)
					resetKeyQueue();
			} else {
				// key released was neither first nor last in queue
				keyQueueLength--;
			}
			break;
		}
		i++;
	}
	// if (keyFoundInQueue) {
	// 	printf("keyQueueFirst: %i, keyQueueLast: %i, keyQueueLength: %i\n", keyQueueFirst, keyQueueLast, keyQueueLength);
	// }
	return keyFoundInQueue;
}

void mapLevels_2_5_6(TCHAR * mappingTableOutput, TCHAR * newChars) {
	TCHAR * l1_lowercase = L"abcdefghijklmnopqrstuvwxyzäöüß.,";

	TCHAR *ptr;
	for (int i = 0; i < LEN; i++) {
		ptr = wcschr(l1_lowercase, mappingTableLevel1[i]);
		if (ptr != NULL && ptr < &l1_lowercase[32]) {
			//printf("i = %d: mappingTableLevel1[i] = %c; ptr = %d; ptr = %s; index = %d\n", i, mappingTableLevel1[i], ptr, ptr, ptr-l1_lowercase+1);
			mappingTableOutput[i] = newChars[ptr-l1_lowercase];
		}
	}
}

void initLevel4SpecialCases() {
	mappingTableLevel4Special[16] = VK_PRIOR;

	if (strcmp(layout, "kou") == 0 || strcmp(layout, "vou") == 0) {
		mappingTableLevel4Special[17] = VK_NEXT;
		mappingTableLevel4Special[18] = VK_UP;
		mappingTableLevel4Special[19] = VK_BACK;
		mappingTableLevel4Special[20] = VK_DELETE;
	} else {
		mappingTableLevel4Special[17] = VK_BACK;
		mappingTableLevel4Special[18] = VK_UP;
		mappingTableLevel4Special[19] = VK_DELETE;
		mappingTableLevel4Special[20] = VK_NEXT;
	}

	mappingTableLevel4Special[30] = VK_HOME;
	mappingTableLevel4Special[31] = VK_LEFT;
	mappingTableLevel4Special[32] = VK_DOWN;
	mappingTableLevel4Special[33] = VK_RIGHT;
	mappingTableLevel4Special[34] = VK_END;

	if (strcmp(layout, "kou") == 0 || strcmp(layout, "vou") == 0) {
		mappingTableLevel4Special[44] = VK_INSERT;
		mappingTableLevel4Special[45] = VK_TAB;
		mappingTableLevel4Special[46] = VK_RETURN;
		mappingTableLevel4Special[47] = VK_ESCAPE;
	} else {
		mappingTableLevel4Special[44] = VK_ESCAPE;
		mappingTableLevel4Special[45] = VK_TAB;
		mappingTableLevel4Special[46] = VK_INSERT;
		mappingTableLevel4Special[47] = VK_RETURN;
	}

	mappingTableLevel4Special[57] = '0'; // space bar

	/** numeric keypad
	 * --------------------
	 * dec hex extended bit
	 *  28  1C 1   Enter
	 *  53  35 1   /
	 *  55  37 0   *
	 *  71  47 0   7 and Home
	 *  74  4A 0   -
	 *  75  4B 0   4 and Left
	 *  76  4C 0   5
	 *  77  4D 0   6 and Right
	 *  78  4E 0   +
	 *  79  4F 0   1 and End
	 *  80  50 0   2 and Down
	 *  81  51 0   3 and PgDn
	 *  82  52 0   0 and Ins
	 *  83  53 0   , and Del
	 */
	mappingTableLevel4Special[71] = VK_HOME;
	mappingTableLevel4Special[72] = VK_UP;
	mappingTableLevel4Special[73] = VK_PRIOR;
	mappingTableLevel4Special[75] = VK_LEFT;
	mappingTableLevel4Special[76] = VK_ESCAPE; // not sure about this one
	mappingTableLevel4Special[77] = VK_RIGHT;
	mappingTableLevel4Special[79] = VK_END;
	mappingTableLevel4Special[80] = VK_DOWN;
	mappingTableLevel4Special[81] = VK_NEXT;
	mappingTableLevel4Special[82] = VK_INSERT;
	mappingTableLevel4Special[83] = VK_DELETE;
}

void initCharacterToScanCodeMap() {
	mapCharacterToScanCode['q'] = 0x10;
	mapCharacterToScanCode['w'] = 0x11;
	mapCharacterToScanCode['e'] = 0x12;
	mapCharacterToScanCode['r'] = 0x13;
	mapCharacterToScanCode['t'] = 0x14;
	mapCharacterToScanCode['z'] = 0x15;
	mapCharacterToScanCode['u'] = 0x16;
	mapCharacterToScanCode['i'] = 0x17;
	mapCharacterToScanCode['o'] = 0x18;
	mapCharacterToScanCode['p'] = 0x19;
	mapCharacterToScanCode[0xfc] = 0x1a; // ü
	mapCharacterToScanCode['+'] = 0x1b;
	mapCharacterToScanCode['a'] = 0x1e;
	mapCharacterToScanCode['s'] = 0x1f;
	mapCharacterToScanCode['d'] = 0x20;
	mapCharacterToScanCode['f'] = 0x21;
	mapCharacterToScanCode['g'] = 0x22;
	mapCharacterToScanCode['h'] = 0x23;
	mapCharacterToScanCode['j'] = 0x24;
	mapCharacterToScanCode['k'] = 0x25;
	mapCharacterToScanCode['l'] = 0x26;
	mapCharacterToScanCode[0xf6] = 0x27; // ö
	mapCharacterToScanCode[0xe4] = 0x28; // ä
	mapCharacterToScanCode['y'] = 0x2c;
	mapCharacterToScanCode['x'] = 0x2d;
	mapCharacterToScanCode['c'] = 0x2e;
	mapCharacterToScanCode['v'] = 0x2f;
	mapCharacterToScanCode['b'] = 0x30;
	mapCharacterToScanCode['n'] = 0x31;
	mapCharacterToScanCode['m'] = 0x32;
	mapCharacterToScanCode[','] = 0x33;
	mapCharacterToScanCode['.'] = 0x34;
	mapCharacterToScanCode['-'] = 0x35;
}

void initLayout() {
	// same for all layouts
	wcscpy(mappingTableLevel1 +  2, L"1234567890-`");
	wcscpy(mappingTableLevel1 + 71, L"789-456+1230.");
	mappingTableLevel1[57] = L' '; // Spacebar → space
	mappingTableLevel1[69] = L'\t'; // NumLock key → tabulator

	mappingTableLevel2[41] = L'\u030C'; // key to the left of the "1" key, "Combining Caron"
	wcscpy(mappingTableLevel2 +  2, L"°§ℓ»«$€„“”—̧");
	wcscpy(mappingTableLevel2 + 71, L"✔✘†-♣€‣+♦♥♠␣."); // numeric keypad
	mappingTableLevel2[57] = L' '; // Spacebar → space
	mappingTableLevel2[69] = L'\t'; // NumLock key → tabulator
	// https://neo-layout.org/grafik/aufsteller/neo20-aufsteller.pdf

	wcscpy(mappingTableLevel3 + 41, L"^");
	wcscpy(mappingTableLevel3 +  2, L"¹²³›‹¢¥‚‘’—̊");
	wcscpy(mappingTableLevel3 + 16, L"…_[]^!<>=&ſ");
	mappingTableLevel3[27] = L'\u0337'; // "Combining Short Solidus Overlay"
	wcscpy(mappingTableLevel3 + 30, L"\\/{}*?()-:@");
	wcscpy(mappingTableLevel3 + 44, L"#$|~`+%\"';");
	wcscpy(mappingTableLevel3 + 71, L"↕↑↨−←:→±↔↓⇌%,"); // numeric keypad
	mappingTableLevel3[55] = L'⋅'; // *-key on numeric keypad
	mappingTableLevel3[57] = L' '; // Spacebar → space
	mappingTableLevel3[69] = L'='; // num-lock-key

	mappingTableLevel4[41] = L'\u0307'; // "Combining Dot Above"
	wcscpy(mappingTableLevel4 +  2, L"ªº№⋮·£¤0/*-");
	mappingTableLevel4[13] = L'\u00A8'; // "Diaresis"
	wcscpy(mappingTableLevel4 + 21, L"¡789+−˝");
	wcscpy(mappingTableLevel4 + 35, L"¿456,.");
	wcscpy(mappingTableLevel4 + 49, L":123;");
	mappingTableLevel4[55] = L'×'; // *-key on numeric keypad
	mappingTableLevel4[74] = L'∖'; // --key on numeric keypad
	mappingTableLevel4[78] = L'∓'; // +-key on numeric keypad
	mappingTableLevel4[69] = L'≠'; // num-lock-key

	// layout dependent
	if (strcmp(layout, "adnw") == 0) {
		wcscpy(mappingTableLevel1 + 16, L"kuü.ävgcljf´");
		wcscpy(mappingTableLevel1 + 30, L"hieaodtrnsß");
		wcscpy(mappingTableLevel1 + 44, L"xyö,qbpwmz");

	} else if (strcmp(layout, "adnwzjf") == 0) {
		wcscpy(mappingTableLevel1 + 16, L"kuü.ävgclßz´");
		wcscpy(mappingTableLevel1 + 30, L"hieaodtrnsf");
		wcscpy(mappingTableLevel1 + 44, L"xyö,qbpwmj");

	} else if (strcmp(layout, "bone") == 0) {
		wcscpy(mappingTableLevel1 + 16, L"jduaxphlmwß´");
		wcscpy(mappingTableLevel1 + 30, L"ctieobnrsgq");
		wcscpy(mappingTableLevel1 + 44, L"fvüäöyz,.k");

	} else if (strcmp(layout, "koy") == 0) {
		wcscpy(mappingTableLevel1 + 16, L"k.o,yvgclßz´");
		wcscpy(mappingTableLevel1 + 30, L"haeiudtrnsf");
		wcscpy(mappingTableLevel1 + 44, L"xqäüöbpwmj");

	} else if (strcmp(layout, "kou") == 0
				|| strcmp(layout, "vou") == 0) {
		if (strcmp(layout, "kou") == 0) {
			wcscpy(mappingTableLevel1 + 16, L"k.ouäqgclfj´");
			wcscpy(mappingTableLevel1 + 30, L"haeiybtrnsß");
			wcscpy(mappingTableLevel1 + 44, L"zx,üöpdwmv");
		} else {  // vou
			wcscpy(mappingTableLevel1 + 16, L"v.ouäqglhfj´");
			wcscpy(mappingTableLevel1 + 30, L"caeiybtrnsß");
			wcscpy(mappingTableLevel1 + 44, L"zx,üöpdwmk");
			/* mappingTapNextRelease[0x3A] = MT_SHIFT; // CapsLock */
			/* mappingTapNextRelease[0x28] = MT_SHIFT; // Ä */
		}

		wcscpy(mappingTableLevel3 + 16, L"@%{}^!<>=&€̷");
		wcscpy(mappingTableLevel3 + 30, L"|`()*?/:-_→");
		wcscpy(mappingTableLevel3 + 44, L"#[]~$+\"'\\;");

		wcscpy(mappingTableLevel4 +  4, L"✔✘·£¤0/*-¨");
		wcscpy(mappingTableLevel4 + 21, L":789+−˝");
		wcscpy(mappingTableLevel4 + 35, L"-456,;");
		wcscpy(mappingTableLevel4 + 49, L"_123.");

	} else if (strcmp(layout, "qwertz") == 0) {
		wcscpy(mappingTableLevel1 + 12, L"ß");
		wcscpy(mappingTableLevel1 + 16, L"qwertzuiopü+");
		wcscpy(mappingTableLevel1 + 30, L"asdfghjklöä");
		wcscpy(mappingTableLevel1 + 44, L"yxcvbnm,.-");

	} else { // neo
		wcscpy(mappingTableLevel1 + 16, L"xvlcwkhgfqß´");
		wcscpy(mappingTableLevel1 + 30, L"uiaeosnrtdy");
		wcscpy(mappingTableLevel1 + 44, L"üöäpzbm,.j");
	}

	// use custom layout if it was defined
	if (wcslen(customLayoutWcs) != 0) {
		if (wcslen(customLayoutWcs) == 32) {
			// custom layout
			wcsncpy(mappingTableLevel1 + 16, customLayoutWcs, 11);
			wcsncpy(mappingTableLevel1 + 30, customLayoutWcs + 11, 11);
			wcsncpy(mappingTableLevel1 + 44, customLayoutWcs + 22, 10);
		} else {
			printf("\ncustomLayout given but its length is %i (expected: 32).\n", wcslen(customLayoutWcs));
		}
	}

	// same for all layouts
	wcscpy(mappingTableLevel1 + 27, L"´");
	wcscpy(mappingTableLevel2 + 27, L"~");
	// slash key is special: it has the same scan code in the main block and the numpad
	wcscpy(numpadSlashKey, L"//÷∕⌀∣");

	// map letters of level 2
	TCHAR * charsLevel2;
	charsLevel2 = L"ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜẞ•–";
	mapLevels_2_5_6(mappingTableLevel2, charsLevel2);

	if (supportLevels5and6) {
		// map main block on levels 5 and 6
		// (Neo does not define characters for u, v and ü on level 5.)
		TCHAR * charsLevel5 = L"αβχδεφγψιθκλμνοπϕρστuvωξυζηϵüςϑϱ";  // a-zäöüß.,
		mapLevels_2_5_6(mappingTableLevel5, charsLevel5);
		TCHAR * charsLevel6 = L"∀⇐ℂΔ∃ΦΓΨ∫Θ⨯Λ⇔ℕ∈ΠℚℝΣ∂⊂√ΩΞ∇ℤℵ∩∪∘↦⇒";  // a-zäöüß.,
		mapLevels_2_5_6(mappingTableLevel6, charsLevel6);

		// add number row and dead key in upper letter row
		mappingTableLevel5[41] = L'\u0309'; // "Combining Hook Above"
		wcscpy(mappingTableLevel5 +  2, L"₁₂₃♂♀⚥ϰ⟨⟩₀?");
		mappingTableLevel5[13] = L'\u1FFE'; // "Greek Dasia"
		mappingTableLevel5[27] = L'\u1FBF'; // "Greek Psili"
		mappingTableLevel5[57] = L'\u00A0'; // space = no-break space
		wcscpy(mappingTableLevel5 + 71, L"≪∩≫⊖⊂⊶⊃⊕≤∪≥‰′"); // numeric keypad

		wcscpy(mappingTableLevel5 + 55, L"⊙");  // *-key on numeric keypad
		wcscpy(mappingTableLevel5 + 69, L"≈");  // num-lock-key

		mappingTableLevel6[41] = L'\u0323'; // "Combining Dot Below"
		wcscpy(mappingTableLevel6 +  2, L"¬∨∧⊥∡∥→∞∝⌀?");
		mappingTableLevel6[13] = L'\u0304'; // "Combining Macron"
		wcscpy(mappingTableLevel6 + 27, L"˘");
		mappingTableLevel6[57] = 0x202f;  // space = narrow no-break space
		wcscpy(mappingTableLevel6 + 71, L"⌈⋂⌉∸⊆⊷⊇∔⌊⋃⌋□"); // numeric keypad
		mappingTableLevel6[83] = 0x02dd; // double acute accent (not sure about this one)
		wcscpy(mappingTableLevel6 + 55, L"⊗");  // *-key on numeric keypad
		wcscpy(mappingTableLevel6 + 69, L"≡");  // num-lock-key
	}

	// if quote/ä is the right level 3 modifier, copy symbol of quote/ä key to backslash/# key
	if (quoteAsMod3R) {
		mappingTableLevel1[43] = mappingTableLevel1[40];
		mappingTableLevel2[43] = mappingTableLevel2[40];
		mappingTableLevel3[43] = mappingTableLevel3[40];
		mappingTableLevel4[43] = mappingTableLevel4[40];
		if (supportLevels5and6) {
			mappingTableLevel5[43] = mappingTableLevel5[40];
			mappingTableLevel6[43] = mappingTableLevel6[40];
		}
	}

	mappingTableLevel2[8] = 0x20AC;  // €

	// level4 special cases
	initLevel4SpecialCases();

	// apply modTap modifiers
	// puts("\nModTap keys:");
	for (int i=0; i<MOD_TAP_LEN && modTap[i].modifier; i++) {
		unsigned int scanCode = mapCharacterToScanCode[(unsigned char)modTap[i].keycode];
		mappingTapNextRelease[scanCode] = modTap[i].modifier;
		// printf("%s (%i), %c (%i), sc=0x%X (%i)\n", MT_MODIFIER_STRING[modTap[i].modifier], modTap[i].modifier, modTap[i].keycode, (unsigned char)modTap[i].keycode, scanCode, scanCode);
    }
}

void toggleBypassMode() {
	bypassMode = !bypassMode;

	HINSTANCE hInstance = GetModuleHandle(NULL);
	HICON icon = bypassMode
		? LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON_DISABLED))
		: LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));

	trayicon_change_icon(icon);
	printf("%i bypass mode \n", bypassMode);
}

/**
 * Map a key scancode to the char that should be displayed after typing
 **/
TCHAR mapScanCodeToChar(unsigned level, char in) {
	switch (level) {
		case 2:
			return mappingTableLevel2[in];
		case 3:
			return mappingTableLevel3[in];
		case 4:
			return mappingTableLevel4[in];
		case 5:
			return mappingTableLevel5[in];
		case 6:
			return mappingTableLevel6[in];
		default: // level 1
			return mappingTableLevel1[in];
	}
}

/**
 * Maps keyInfo flags (https://docs.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-kbdllhookstruct)
 * to dwFlags for keybd_event (https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-keybd_event)
 **/
DWORD dwFlagsFromKeyInfo(KBDLLHOOKSTRUCT keyInfo) {
	DWORD dwFlags = 0;
	if (keyInfo.flags & LLKHF_EXTENDED) dwFlags |= KEYEVENTF_EXTENDEDKEY;
	if (keyInfo.flags & LLKHF_UP) dwFlags |= KEYEVENTF_KEYUP;
	return dwFlags;
}

void sendDown(BYTE vkCode, BYTE scanCode, bool isExtendedKey) {
	keybd_event(vkCode, scanCode, (isExtendedKey ? KEYEVENTF_EXTENDEDKEY : 0), 0);
}

void sendUp(BYTE vkCode, BYTE scanCode, bool isExtendedKey) {
	keybd_event(vkCode, scanCode, (isExtendedKey ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP, 0);
}

void sendDownUp(BYTE vkCode, BYTE scanCode, bool isExtendedKey) {
	sendDown(vkCode, scanCode, isExtendedKey);
	sendUp(vkCode, scanCode, isExtendedKey);
}

void sendUnicodeChar(TCHAR key, KBDLLHOOKSTRUCT keyInfo) {
	KEYBDINPUT kb={0};
	INPUT Input={0};

	kb.wScan = key;
	kb.dwFlags = KEYEVENTF_UNICODE | dwFlagsFromKeyInfo(keyInfo);
	Input.type = INPUT_KEYBOARD;
	Input.ki = kb;
	SendInput(1, &Input, sizeof(Input));
}

/**
 * Sends a char using emulated keyboard input
 * This works for most cases, but not for dead keys etc
 **/
void sendChar(TCHAR key, KBDLLHOOKSTRUCT keyInfo) {
	SHORT keyScanResult = VkKeyScanEx(key, GetKeyboardLayout(0));

	if (keyScanResult == -1 || shiftLockActive || capsLockActive || level4LockActive
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
		sendUnicodeChar(key, keyInfo);
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

		if (altgr) sendDown(VK_RMENU, 56, false);
		if (ctrl) sendDown(VK_CONTROL, 29, false);
		if (alt) sendDown(VK_MENU, 56, false); // ALT
		if (shift) sendDown(VK_SHIFT, 42, false);

		keybd_event(keyInfo.vkCode, keyInfo.scanCode, dwFlagsFromKeyInfo(keyInfo), keyInfo.dwExtraInfo);

		if (altgr) sendUp(VK_RMENU, 56, false);
		if (ctrl) sendUp(VK_CONTROL, 29, false);
		if (alt) sendUp(VK_MENU, 56, false); // ALT
		if (shift) sendUp(VK_SHIFT, 42, false);
	}
}

/**
 * Send a usually dead key by injecting space after (on down).
 * This will add an actual space if actual dead key is followed by "dead" key with this
 **/
void commitDeadKey(KBDLLHOOKSTRUCT keyInfo) {
	if (!(keyInfo.flags & LLKHF_UP)) sendDownUp(VK_SPACE, 57, false);
}

bool handleLayer2SpecialCases(KBDLLHOOKSTRUCT keyInfo) {
	switch(keyInfo.scanCode) {
		case 27:
			sendChar(L'\u0303', keyInfo);  // perispomene (Tilde)
			return true;
		case 41:
			sendChar(L'\u030C', keyInfo);  // caron, wedge, háček (Hatschek)
			return true;
		default:
			return false;
	}
}

bool handleLayer3SpecialCases(KBDLLHOOKSTRUCT keyInfo) {
	switch(keyInfo.scanCode) {
		case 13:
			sendChar(L'\u030A', keyInfo);  // overring
			return true;
		case 20:
			sendUnicodeChar(L'^', keyInfo);
			return true;
		case 27:
			sendChar(L'\u0337', keyInfo);  // bar (diakritischer Schrägstrich)
			return true;
		case 31:
			if (strcmp(layout, "kou") == 0 || strcmp(layout, "vou") == 0) {
				sendUnicodeChar(L'`', keyInfo);
				return true;
			}
			return false;
		case 48:
			if (strcmp(layout, "kou") != 0 && strcmp(layout, "vou") != 0) {
				sendUnicodeChar(L'`', keyInfo);
				return true;
			}
			return false;
		default:
			return false;
	}
}

bool handleLayer4SpecialCases(KBDLLHOOKSTRUCT keyInfo) {
	// return if left Ctrl was injected by AltGr
	if (keyInfo.scanCode == 541) return -1;

	switch(keyInfo.scanCode) {
		case 13:
			sendChar(L'\u00A8', keyInfo);  // diaeresis, umlaut
			return true;
		case 27:
			sendChar(L'\u02DD', keyInfo);  // double acute (doppelter Akut)
			return true;
		case 41:
			sendChar(L'\u0307', keyInfo);  // dot above (Punkt, darüber)
			return true;
	}

	// A second level 4 mapping table for special (non-unicode) keys.
	// Maybe this could be included in the global TCHAR mapping table or level 4!?
	BYTE bScan = 0;

	if (mappingTableLevel4Special[keyInfo.scanCode] != 0) {
		if (mappingTableLevel4Special[keyInfo.scanCode] == VK_RETURN)
			bScan = 0x1c;
		else if (mappingTableLevel4Special[keyInfo.scanCode] == VK_INSERT)
			bScan = 0x52;

		// extended flag (bit 0) is necessary for selecting text with shift + arrow
		keybd_event(mappingTableLevel4Special[keyInfo.scanCode], bScan, dwFlagsFromKeyInfo(keyInfo) | KEYEVENTF_EXTENDEDKEY, 0);

		return true;
	}
	return false;
}

bool isShift(KBDLLHOOKSTRUCT keyInfo) {
	return keyInfo.vkCode == VK_SHIFT
	    || keyInfo.vkCode == VK_LSHIFT
	    || keyInfo.vkCode == VK_RSHIFT;
}

bool isMod3(KBDLLHOOKSTRUCT keyInfo) {
	return keyInfo.scanCode == scanCodeMod3L
	    || keyInfo.scanCode == scanCodeMod3R;
}

bool isMod4(KBDLLHOOKSTRUCT keyInfo) {
	return keyInfo.scanCode == scanCodeMod4L
	    || keyInfo.vkCode == VK_RMENU;
}

bool isSystemKeyPressed() {
	return ctrlLeftPressed || ctrlRightPressed
	    || altLeftPressed
	    || winLeftPressed || winRightPressed;
}

bool isLetter(TCHAR key) {
	return (key >= 65 && key <= 90  // A-Z
	     || key >= 97 && key <= 122 // a-z
	     || key == L'ä' || key == L'Ä'
	     || key == L'ö' || key == L'Ö'
	     || key == L'ü' || key == L'Ü'
	     || key == L'ß' || key == L'ẞ');
}

void toggleShiftLock() {
	shiftLockActive = !shiftLockActive;
	printf("Shift lock %s!\n", shiftLockActive ? "activated" : "deactivated");
}

void toggleCapsLock() {
	capsLockActive = !capsLockActive;
	printf("Caps lock %s!\n", capsLockActive ? "activated" : "deactivated");
}

void logKeyEvent(char *desc, KBDLLHOOKSTRUCT keyInfo, int color) {
	char vkCodeLetter[4] = {'(', keyInfo.vkCode, ')', 0};
	char *keyName;
	switch (keyInfo.vkCode) {
		case VK_LSHIFT:
			keyName = "(Shift left)";
			break;
		case VK_RSHIFT:
			keyName = "(Shift right)";
			break;
		case VK_SHIFT:
			keyName = "(Shift)";
			break;
		case VK_CAPITAL:
			keyName = "(M3 left)";
			break;
		case 0xde:  // ä
			keyName = quoteAsMod3R ? "(M3 right)" : "";
			break;
		case 0xbf:  // #
			keyName = quoteAsMod3R ? "" : "(M3 right)";
			break;
		case VK_OEM_102:
			keyName = "(M4 left [<])";
			break;
		case VK_CONTROL:
			keyName = "(Ctrl)";
			break;
		case VK_LCONTROL:
			keyName = "(Ctrl left)";
			break;
		case VK_RCONTROL:
			keyName = "(Ctrl right)";
			break;
		case VK_MENU:
			keyName = "(Alt)";
			break;
		case VK_LMENU:
			keyName = "(Alt left)";
			break;
		case VK_RMENU:
			keyName = "(Alt right)";
			break;
		case VK_LWIN:
			keyName = "(Win left)";
			break;
		case VK_RWIN:
			keyName = "(Win right)";
			break;
		case VK_BACK:
			keyName = "(Backspace)";
			break;
		case VK_RETURN:
			keyName = "(Return)";
			break;
		case VK_SPACE:
			keyName = "(Spacebar)";
			break;
		case 0x41 ... 0x5A:
			keyName = vkCodeLetter;
			break;
		default:
			keyName = "";
			//keyName = MapVirtualKeyA(keyInfo.vkCode, MAPVK_VK_TO_CHAR);
	}
	char *shiftLockCapsLockInfo = shiftLockActive ? " [shift lock active]"
						: (capsLockActive ? " [caps lock active]" : "");
	char *level4LockInfo = level4LockActive ? " [level4 lock active]" : "";
	char *vkPacket = (desc=="injected" && keyInfo.vkCode == VK_PACKET) ? " (VK_PACKET)" : "";

	if (color < 0)
		color = FG_WHITE;
	SetConsoleTextAttribute(hConsole, color);
	printf(
		"%-13s | sc:%03u vk:0x%02X flags:0x%02X extra:%d %s%s%s%s\n",
		desc, keyInfo.scanCode, keyInfo.vkCode, keyInfo.flags, keyInfo.dwExtraInfo,
		keyName, shiftLockCapsLockInfo, level4LockInfo, vkPacket
	);
	// reset color
	SetConsoleTextAttribute(hConsole, FG_WHITE);
}

unsigned getLevel() {
	unsigned level = 1;

	if (modState.shift != shiftLockActive) // (modState.shift) XOR (shiftLockActive)
		level = 2;
	if (modState.mod3)
		level = (supportLevels5and6 && level == 2) ? 5 : 3;
	if (modState.mod4 != level4LockActive)
		level = (supportLevels5and6 && level == 3) ? 6 : 4;

	return level;
}

void handleShiftKey(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp) {
	bool *pressedShift = keyInfo.vkCode == VK_RSHIFT ? &shiftRightPressed : &shiftLeftPressed;
	bool *otherShift = keyInfo.vkCode == VK_RSHIFT ? &shiftLeftPressed : &shiftRightPressed;

	modState.shift = !isKeyUp;
	*pressedShift = !isKeyUp;

	if (isKeyUp) {
		if (*otherShift && !bypassMode) {
			if (shiftLockEnabled) {
				sendDownUp(VK_CAPITAL, 58, false);
				toggleShiftLock();
			} else if (capsLockEnabled) {
				sendDownUp(VK_CAPITAL, 58, false);
				toggleCapsLock();
			}
		}
		sendUp(keyInfo.vkCode, keyInfo.scanCode, false);
	} else { // key down
		sendDown(keyInfo.vkCode, keyInfo.scanCode, false);
	}
}

/**
 * returns `true` if no systemKey was pressed -> continue execution, `false` otherwise
 **/
bool handleSystemKey(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp) {
	bool newStateValue = !isKeyUp;
	DWORD dwFlags = isKeyUp ? KEYEVENTF_KEYUP : keyInfo.flags;

	// Check also the scan code because AltGr sends VK_LCONTROL with scanCode 541
	if (keyInfo.vkCode == VK_LCONTROL && keyInfo.scanCode == 29) {
		if (swapLeftCtrlAndLeftAlt) {
			altLeftPressed = newStateValue;
			keybd_event(VK_LMENU, 56, dwFlags, 0);
		} else if (swapLeftCtrlLeftAltAndLeftWin) {
			winLeftPressed = newStateValue;
			keybd_event(VK_LWIN, 91, dwFlags, 0);
		} else {
			ctrlLeftPressed = newStateValue;
			keybd_event(VK_LCONTROL, 29, dwFlags, 0);
		}
		return false;
	} else if (keyInfo.vkCode == VK_RCONTROL) {
		ctrlRightPressed = newStateValue;
		keybd_event(VK_RCONTROL, 29, dwFlags, 0);
	} else if (keyInfo.vkCode == VK_LMENU) {
		if (swapLeftCtrlAndLeftAlt || swapLeftCtrlLeftAltAndLeftWin) {
			ctrlLeftPressed = newStateValue;
			keybd_event(VK_LCONTROL, 29, dwFlags, 0);
		} else {
			altLeftPressed = newStateValue;
			keybd_event(VK_LMENU, 56, dwFlags, 0);
		}
		return false;
	} else if (keyInfo.vkCode == VK_LWIN) {
		if (swapLeftCtrlLeftAltAndLeftWin) {
			altLeftPressed = newStateValue;
			keybd_event(VK_LMENU, 56, dwFlags, 0);
		} else {
			winLeftPressed = newStateValue;
			keybd_event(VK_LWIN, 91, dwFlags, 0);
		}
		return false;
	} else if (keyInfo.vkCode == VK_RWIN) {
		winRightPressed = newStateValue;
		keybd_event(VK_RWIN, 92, dwFlags, 0);
		return false;
	}

	return true;
}

void handleMod3Key(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp) {
	if (isKeyUp) {
		if (keyInfo.scanCode == scanCodeMod3R) {
			level3modRightPressed = false;
			double level3modRightPressedDuration = (double) (clock() - level3modRightPressedInstant) / CLOCKS_PER_SEC;
			modState.mod3 = level3modLeftPressed | level3modRightPressed;
			if (mod3RAsReturn && level3modRightAndNoOtherKeyPressed && level3modRightPressedDuration < 0.5) {
				sendUp(keyInfo.vkCode, keyInfo.scanCode, false); // release Mod3_R
				sendDownUp(VK_RETURN, 28, true); // send Return
				level3modRightAndNoOtherKeyPressed = false;
			}
		} else { // scanCodeMod3L (CapsLock)
			level3modLeftPressed = false;
			double level3modLeftPressedDuration = (double) (clock() - level3modLeftPressedInstant) / CLOCKS_PER_SEC;
			modState.mod3 = level3modLeftPressed | level3modRightPressed;
			if (capsLockAsEscape && level3modLeftAndNoOtherKeyPressed && level3modLeftPressedDuration < 0.5) {
				sendUp(VK_CAPITAL, 58, false); // release Mod3_R
				sendDownUp(VK_ESCAPE, 1, true); // send Escape
				level3modLeftAndNoOtherKeyPressed = false;
			}
		}
	} else { // keyDown
		if (keyInfo.scanCode == scanCodeMod3R) {
			if (!level3modRightPressed)
			{
				level3modRightPressed = true;
				level3modRightPressedInstant = clock();
			}
			if (mod3RAsReturn)
				level3modRightAndNoOtherKeyPressed = true;
		} else { // VK_CAPITAL (CapsLock)
			if (!level3modLeftPressed)
			{
				level3modLeftPressed = true;
				level3modLeftPressedInstant = clock();
			}
			if (capsLockAsEscape)
				level3modLeftAndNoOtherKeyPressed = true;
		}
		modState.mod3 = level3modLeftPressed | level3modRightPressed;
	}
}

void handleMod4Key(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp) {
	if (isKeyUp) {
		if (keyInfo.scanCode == scanCodeMod4L) {
			level4modLeftPressed = false;
			double level4modLeftPressedDuration = (double) (clock() - level4modLeftPressedInstant) / CLOCKS_PER_SEC;
			if (level4modRightPressed && level4LockEnabled) {
				level4LockActive = !level4LockActive;
				printf("Level4 lock %s!\n", level4LockActive ? "activated" : "deactivated");
			} else if (mod4LAsTab && level4modLeftAndNoOtherKeyPressed && level4modLeftPressedDuration < 0.5) {
				sendUp(keyInfo.vkCode, keyInfo.scanCode, false); // release Mod4_L
				sendDownUp(VK_TAB, 15, true); // send Tab
				level4modLeftAndNoOtherKeyPressed = false;
				modState.mod4 = level4modLeftPressed | level4modRightPressed;
				return;
			}
		} else { // scanCodeMod4R
			level4modRightPressed = false;
			if (level4modLeftPressed && level4LockEnabled) {
				level4LockActive = !level4LockActive;
				printf("Level4 lock %s!\n", level4LockActive ? "activated" : "deactivated");
			}
		}
		modState.mod4 = level4modLeftPressed | level4modRightPressed;
	} else { // keyDown
		if (keyInfo.scanCode == scanCodeMod4L) {
			if (!level4modLeftPressed)
			{
				level4modLeftPressed = true;
				level4modLeftPressedInstant = clock();
			}
			if (mod4LAsTab)
				level4modLeftAndNoOtherKeyPressed = !(level4modRightPressed || level3modLeftPressed || level3modRightPressed);
		} else { // scanCodeMod4R
			level4modRightPressed = true;
			/* ALTGR triggers two keys: LCONTROL and RMENU
					we don't want to have any of those two here effective but return -1 seems
					to change nothing, so we simply send keyup here.  */
			sendUp(VK_RMENU, 56, false);
		}
		modState.mod4 = level4modLeftPressed | level4modRightPressed;
	}
}

/**
 * updates system key and layerLock states; writes key
 * returns `true` if next hook should be called, `false` otherwise
 **/
bool updateStatesAndWriteKey(KBDLLHOOKSTRUCT keyInfo, bool isKeyUp) {
	bool continueExecution = handleSystemKey(keyInfo, isKeyUp);
	if (!continueExecution) return false;

	unsigned level = getLevel();

	if (isMod3(keyInfo)) {
		// if (keyQueueLength)
		// 	return false;
		handleMod3Key(keyInfo, isKeyUp);
		return false;
	} else if (isMod4(keyInfo)) {
		// if (keyQueueLength)
		// 	return false;
		handleMod4Key(keyInfo, isKeyUp);
		return false;
	} else if ((keyInfo.flags & LLKHF_EXTENDED) && keyInfo.scanCode != 53) {
		// handle numpad slash key (scanCode=53 + extended bit) later
		return true;
	} else if (level == 2 && handleLayer2SpecialCases(keyInfo)) {
		return false;
	} else if (level == 3 && handleLayer3SpecialCases(keyInfo)) {
		return false;
	} else if (level == 4 && handleLayer4SpecialCases(keyInfo)) {
		return false;
	} else if (level == 1 && keyInfo.vkCode >= 0x30 && keyInfo.vkCode <= 0x39) {
		// numbers 0 to 9 -> don't remap
	} else if (!(qwertzForShortcuts && isSystemKeyPressed())) {
		TCHAR key;
		if ((keyInfo.flags & LLKHF_EXTENDED) && keyInfo.scanCode == 53) {
			// slash key ("/") on numpad
			key = numpadSlashKey[level-1];
			keyInfo.flags = 0;
		} else {
			key = mapScanCodeToChar(level, keyInfo.scanCode);
		}
		if (capsLockActive && (level == 1 || level == 2) && isLetter(key)) {
			key = mapScanCodeToChar(level==1 ? 2 : 1, keyInfo.scanCode);
		}
		if (key != 0 && (keyInfo.flags & LLKHF_INJECTED) == 0) {
			// if key must be mapped
			int character = MapVirtualKeyA(keyInfo.vkCode, MAPVK_VK_TO_CHAR);
			TCHAR keyUTF16[] = {key, 0};
			char keyUTF8[4];
			convertToUTF8(keyUTF16, keyUTF8);
			printf("%-13s | sc:%03d %c->%s [0x%04X] (level %u)\n", " mapped", keyInfo.scanCode, character, keyUTF8, key, level);
			sendChar(key, keyInfo);
			return false;
		}
	}

	return true;
}

__declspec(dllexport)
LRESULT CALLBACK keyevent(int code, WPARAM wparam, LPARAM lparam) {

	if (code != HC_ACTION ||
			!(wparam == WM_SYSKEYUP || wparam == WM_KEYUP ||
			  wparam == WM_SYSKEYDOWN || wparam == WM_KEYDOWN)) {
		return CallNextHookEx(NULL, code, wparam, lparam);
	}

	KBDLLHOOKSTRUCT keyInfo = *((KBDLLHOOKSTRUCT *) lparam);

	if (keyInfo.flags & LLKHF_INJECTED) {
		// process injected events like normal, because most probably we are injecting them
		logKeyEvent((keyInfo.flags & LLKHF_UP) ? "injected up" : "injected down", keyInfo, FG_YELLOW);
		return CallNextHookEx(NULL, code, wparam, lparam);
	}

	bool isKeyUp = (wparam == WM_KEYUP || wparam == WM_SYSKEYUP);

	if (isShift(keyInfo)) {
		// if (keyQueueLength)
		// 	return false;
		handleShiftKey(keyInfo, isKeyUp);
		return -1;
	}

	// Shift + Pause
	if (wparam == WM_KEYDOWN && keyInfo.vkCode == VK_PAUSE && modState.shift) {
		toggleBypassMode();
		return -1;
	}

	if (bypassMode) {
		if (keyInfo.vkCode == VK_CAPITAL && !(keyInfo.flags & LLKHF_UP)) {
			// synchronize with capsLock state during bypass
			if (shiftLockEnabled) {
				toggleShiftLock();
			} else if (capsLockEnabled) {
				toggleCapsLock();
			}
		}
		return CallNextHookEx(NULL, code, wparam, lparam);
	}

	if (isKeyUp) {
		logKeyEvent("key up", keyInfo, FG_CYAN);

		if (keyQueueLength) {
			// int index;
			bool keyReleasedHandled = checkQueue(keyInfo);
			if (keyReleasedHandled)
				return -1;
				// return CallNextHookEx(NULL, code, wparam, lparam);
		}
		bool callNext = updateStatesAndWriteKey(keyInfo, true);
		if (!callNext) return -1;

	} else {  // key down
		unsigned level = getLevel();
		printf("\nLEVEL %i", level);
		// if (ctrlLeftPressed) printf(" CTRL_L");
		// if (ctrlRightPressed) printf(" CTRL_R");
		// if (altLeftPressed) printf(" ALT_L");
		// if (winLeftPressed) printf(" WIN_L");
		// if (winRightPressed) printf(" WIN_R");
		// if (shiftLeftPressed) printf(" SHIFT_L");
		// if (shiftRightPressed) printf(" SHIFT_R");
		// if (modState.shift) printf(" shift");
		// if (modState.mod3) printf(" mod3");
		// if (modState.mod4) printf(" mod4");
		printf("\n");

		logKeyEvent("key down", keyInfo, FG_CYAN);

		if (keyQueueLength || mappingTapNextRelease[keyInfo.scanCode]) {
			appendToQueue(keyInfo);
			return -1;
		}

		level3modLeftAndNoOtherKeyPressed = false;
		level3modRightAndNoOtherKeyPressed = false;
		level4modLeftAndNoOtherKeyPressed = false;

		bool callNext = updateStatesAndWriteKey(keyInfo, false);
		if (!callNext) return -1;
	}

	/* Passes the hook information to the next hook procedure in the current hook chain.
	 * 1st Parameter hhk - Optional
	 * 2nd Parameter nCode - The next hook procedure uses this code to determine how to process the hook information.
	 * 3rd Parameter wParam - The wParam value passed to the current hook procedure.
	 * 4th Parameter lParam - The lParam value passed to the current hook procedure
	 */
	return CallNextHookEx(NULL, code, wparam, lparam);
}

DWORD WINAPI hookThreadMain(void *user) {
	HINSTANCE base = GetModuleHandle(NULL);
	MSG msg;

	if (!base) {
		if (!(base = LoadLibrary((wchar_t *) user))) {
			return 1;
		}
	}
	/* Installs an application-defined hook procedure into a hook chain
	 * 1st Parameter idHook: WH_KEYBOARD_LL - The type of hook procedure to be installed.
	 * Installs a hook procedure that monitors low-level keyboard input events.
	 * 2nd Parameter lpfn: LowLevelKeyboardProc - A pointer to the hook procedure.
	 * 3rd Parameter hMod: hExe - A handle to the DLL containing the hook procedure pointed to by the lpfn parameter.
	 * 4th Parameter dwThreadId: 0 - the hook procedure is associated with all existing threads running.
	 * If the function succeeds, the return value is the handle to the hook procedure.
	 * If the function fails, the return value is NULL.
	 */
	keyhook = SetWindowsHookEx(WH_KEYBOARD_LL, keyevent, base, 0);

	/* Message loop retrieves messages from the thread's message queue and dispatches them to the appropriate window procedures.
	 * For more info http://msdn.microsoft.com/en-us/library/ms644928%28v=VS.85%29.aspx#creating_loop
	 * Retrieves a message from the calling thread's message queue.
	 */
	while (GetMessage(&msg, 0, 0, 0) > 0) {
		// Translates virtual-key messages into character messages.
		// TranslateMessage(&msg);
		// Dispatches a message to a window procedure.
		DispatchMessage(&msg);
	}

	/* To free system resources associated with the hook and removes a hook procedure installed in a hook chain
	 * Parameter hhk: hKeyHook - A handle to the hook to be removed.
	 */
	UnhookWindowsHookEx(keyhook);

	return 0;
}

void exitApplication() {
	printf("Clicked Exit button!\n");
	trayicon_remove();
	PostQuitMessage(0);
}

bool fileExists(LPCSTR szPath) {
	DWORD dwAttrib = GetFileAttributesA(szPath);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES
	    && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool checkSetting(char *keyword, char *filename) {
	char returnValue[100];
	GetPrivateProfileStringA("Settings", keyword, "0", returnValue, 100, filename);
	return (strcmp(returnValue, "1") == 0);
}

int main(int argc, char *argv[]) {
	setbuf(stdout, NULL);
	/**
	* find settings.ini (in same folder as neo-llkh.exe)
	*/
	// get path of neo-llkh.exe
	char ini[256];
	GetModuleFileNameA(NULL, ini, 256);
	//printf("exe: %s\n", ini);
	char * pch;
	// find last \ in path
	pch = strrchr(ini, '\\');
	// replace neo-llkh.exe by settings.ini
	strcpy(pch+1, "settings.ini");
	//printf("ini: %s\n", ini);

	/**
	* If settings.ini exists, read in settings.
	* Otherwise check for command line parameters.
	*/
	if (fileExists(ini)) {
		char returnValue[100];

		GetPrivateProfileStringA("Settings", "layout", "neo", layout, 100, ini);

		GetPrivateProfileStringA("Settings", "customLayout", "", customLayout, 65, ini);

		quoteAsMod3R = checkSetting("symmetricalLevel3Modifiers", ini);
		returnAsMod3R = checkSetting("returnKeyAsMod3R", ini);
		tabAsMod4L = checkSetting("tabKeyAsMod4L", ini);
		capsLockEnabled = checkSetting("capsLockEnabled", ini);
		shiftLockEnabled = checkSetting("shiftLockEnabled", ini);
		level4LockEnabled = checkSetting("level4LockEnabled", ini);
		qwertzForShortcuts = checkSetting("qwertzForShortcuts", ini);
		swapLeftCtrlAndLeftAlt = checkSetting("swapLeftCtrlAndLeftAlt", ini);
		swapLeftCtrlLeftAltAndLeftWin = checkSetting("swapLeftCtrlLeftAltAndLeftWin", ini);
		supportLevels5and6 = checkSetting("supportLevels5and6", ini);
		capsLockAsEscape = checkSetting("capsLockAsEscape", ini);
		mod3RAsReturn = checkSetting("mod3RAsReturn", ini);
		mod4LAsTab = checkSetting("mod4LAsTab", ini);
		debugWindow = checkSetting("debugWindow", ini);

		if (capsLockEnabled)
			shiftLockEnabled = false;

		if (swapLeftCtrlLeftAltAndLeftWin)
			swapLeftCtrlAndLeftAlt = false;

		if (debugWindow) {
			// Open Console Window to see printf output
			SetStdOutToNewConsole();
		}

		printf("\nEinstellungen aus %s:\n", ini);
		printf(" Layout: %s\n", layout);
		printf(" customLayout: %s\n", customLayout);
		printf(" symmetricalLevel3Modifiers: %d\n", quoteAsMod3R);
		printf(" returnKeyAsMod3R: %d\n", returnAsMod3R);
		printf(" tabKeyAsMod4L: %d\n", tabAsMod4L);
		printf(" capsLockEnabled: %d\n", capsLockEnabled);
		printf(" shiftLockEnabled: %d\n", shiftLockEnabled);
		printf(" level4LockEnabled: %d\n", level4LockEnabled);
		printf(" qwertzForShortcuts: %d\n", qwertzForShortcuts);
		printf(" swapLeftCtrlAndLeftAlt: %d\n", swapLeftCtrlAndLeftAlt);
		printf(" swapLeftCtrlLeftAltAndLeftWin: %d\n", swapLeftCtrlLeftAltAndLeftWin);
		printf(" supportLevels5and6: %d\n", supportLevels5and6);
		printf(" capsLockAsEscape: %d\n", capsLockAsEscape);
		printf(" mod3RAsReturn: %d\n", mod3RAsReturn);
		printf(" mod4LAsTab: %d\n", mod4LAsTab);
		printf(" debugWindow: %d\n\n", debugWindow);

		// char const* const fileName = argv[1]; /* should check that argc > 1 */
		FILE* file = fopen(ini, "r"); /* should check the result */
		char line[256];
		int i = 0;

		while (fgets(line, sizeof(line), file) && i < MOD_TAP_LEN) {
			if (strstr(line, "=ModTap(") != NULL) {
				printf("%s", line);
				char *token = strtok(line, "=");
				if (token == NULL) continue;
				int keycode = (int)token[0];
				token = strtok(NULL, "(");
				if (token == NULL) continue;
				token = strtok(NULL, ")");
				if (token == NULL) continue;
				if (strcmp(token, "ctrl") == 0) {
					modTap[i].modifier = MT_CTRL;
				} else if (strcmp(token, "shift") == 0) {
					modTap[i].modifier = MT_SHIFT;
				} else if (strcmp(token, "mod3") == 0) {
					modTap[i].modifier = MT_MOD3;
				} else if (strcmp(token, "mod4") == 0) {
					modTap[i].modifier = MT_MOD4;
				} else if (strcmp(token, "alt") == 0) {
					modTap[i].modifier = MT_ALT;
				} else if (strcmp(token, "win") == 0) {
					modTap[i].modifier = MT_WIN;
				} else {
					printf(line);
					printf("Unknown modifier %s\n", token);
					printf("Please use one of these: ctrl, shift, mod3, mod4, alt, win.\n");
					continue;
				}
				modTap[i].keycode = keycode;
				// printf("i=%i, keycode=%i, modifier=%i\n", i, (unsigned char)modTap[i].keycode, modTap[i].modifier);
				i++;
			}
		}
		fclose(file);

	} else {
		printf("\nKeine settings.ini gefunden: %s\n\n", ini);
	}

	if (argc >= 2) {
		printf("\nEinstellungen von der Kommandozeile:");
		const char delimiter[] = "=";
		char *param, *value;
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "neo") == 0
					|| strcmp(argv[i], "adnw") == 0
					|| strcmp(argv[i], "adnwzjf") == 0
					|| strcmp(argv[i], "bone") == 0
					|| strcmp(argv[i], "koy") == 0
					|| strcmp(argv[i], "kou") == 0
					|| strcmp(argv[i], "vou") == 0
					|| strcmp(argv[i], "qwertz") == 0) {
				strncpy(layout, argv[i], 100);
				printf("\n Layout: %s", layout);
				continue;
			}

			if (strstr(argv[i], delimiter) == NULL) {
				printf("\nUnbekannter Parameter: %s", argv[i]);
				continue;
			}

			//printf("\narg%d: %s", i, argv[i]);
			param = strtok(argv[i], delimiter);
			if (param == NULL) {
				printf("\nUnbekannter Parameter: %s", argv[i]);
				continue;
			}

			value = strtok(NULL, delimiter);
			if (value == NULL) {
				printf("\nUnbekannter Parameter: %s", argv[i]);
				continue;
			}

			if (strcmp(param, "debugWindow") == 0) {
				bool debugWindowAlreadyStarted = debugWindow;
				debugWindow = (strcmp(value, "1") == 0);
				if (debugWindow && !debugWindowAlreadyStarted) {
					// Open Console Window to see printf output
					SetStdOutToNewConsole();
				}
				printf("\n debugWindow: %d", debugWindow);

			} else if (strcmp(param, "layout") == 0) {
				strncpy(layout, value, 100);
				printf("\n Layout: %s", layout);

			} else if (strcmp(param, "customLayout") == 0) {
				strncpy(customLayout, value, 65);
				printf("\n Custom layout: %s", customLayout);

			} else if (strcmp(param, "symmetricalLevel3Modifiers") == 0) {
				quoteAsMod3R = (strcmp(value, "1") == 0);
				printf("\n symmetricalLevel3Modifiers: %d", quoteAsMod3R);

			} else if (strcmp(param, "returnKeyAsMod3R") == 0) {
				returnAsMod3R = (strcmp(value, "1") == 0);
				printf("\n returnKeyAsMod3R: %d", returnAsMod3R);

			} else if (strcmp(param, "tabKeyAsMod4L") == 0) {
				tabAsMod4L = (strcmp(value, "1") == 0);
				printf("\n tabKeyAsMod4L: %d", tabAsMod4L);

			} else if (strcmp(param, "capsLockEnabled") == 0) {
				capsLockEnabled = (strcmp(value, "1") == 0);
				printf("\n capsLockEnabled: %d", capsLockEnabled);

			} else if (strcmp(param, "shiftLockEnabled") == 0) {
				shiftLockEnabled = (strcmp(value, "1") == 0);
				printf("\n shiftLockEnabled: %d", shiftLockEnabled);

			} else if (strcmp(param, "level4LockEnabled") == 0) {
				level4LockEnabled = (strcmp(value, "1") == 0);
				printf("\n level4LockEnabled: %d", level4LockEnabled);

			} else if (strcmp(param, "qwertzForShortcuts") == 0) {
				qwertzForShortcuts = (strcmp(value, "1") == 0);
				printf("\n qwertzForShortcuts: %d", qwertzForShortcuts);

			} else if (strcmp(param, "swapLeftCtrlAndLeftAlt") == 0) {
				swapLeftCtrlAndLeftAlt = (strcmp(value, "1") == 0);
				printf("\n swapLeftCtrlAndLeftAlt: %d", swapLeftCtrlAndLeftAlt);

			} else if (strcmp(param, "swapLeftCtrlLeftAltAndLeftWin") == 0) {
				swapLeftCtrlLeftAltAndLeftWin = (strcmp(value, "1") == 0);
				printf("\n swapLeftCtrlLeftAltAndLeftWin: %d", swapLeftCtrlLeftAltAndLeftWin);

			} else if (strcmp(param, "supportLevels5and6") == 0) {
				supportLevels5and6 = (strcmp(value, "1") == 0);
				printf("\n supportLevels5and6: %d", supportLevels5and6);

			} else if (strcmp(param, "capsLockAsEscape") == 0) {
				capsLockAsEscape = (strcmp(value, "1") == 0);
				printf("\n capsLockAsEscape: %d", capsLockAsEscape);

			} else if (strcmp(param, "mod3RAsReturn") == 0) {
				mod3RAsReturn = (strcmp(value, "1") == 0);
				printf("\n mod3RAsReturn: %d", mod3RAsReturn);

			} else if (strcmp(param, "mod4LAsTab") == 0) {
				mod4LAsTab = (strcmp(value, "1") == 0);
				printf("\n mod4LAsTab: %d", mod4LAsTab);

			} else {
				printf("\nUnbekannter Parameter:%s", param);
			}
		}
	}
	// tranform possibly UTF-8 encoded custom layout string to UTF-16
	str2wcs(customLayoutWcs, customLayout, 33);

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

	// console handle: needed for coloring the output
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, FG_WHITE);
	// Catch ctrl-c because it will send keydown for ctrl
	// but then keyup for alt. Then ctrl would be locked.
	// Also needed for removing tray icon when quitting with ctrl-c.
	SetConsoleCtrlHandler(CtrlHandler, TRUE);

	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);

	initCharacterToScanCodeMap();
	initLayout();

	resetKeyQueue();

	/* Retrieves a module handle for the specified module.
	 * parameter is NULL, GetModuleHandle returns a handle to the file used to create the calling process (.exe file).
	 * If the function succeeds, the return value is a handle to the specified module.
	 * If the function fails, the return value is NULL.
	 */
	HINSTANCE hInstance = GetModuleHandle(NULL);
	trayicon_init(LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON)), APPNAME);
	trayicon_add_item(NULL, &toggleBypassMode);
	trayicon_add_item("Exit", &exitApplication);

	/* CreateThread function Creates a thread to execute within the virtual address space of the calling process.
	 * 1st Parameter lpThreadAttributes:  NULL - Thread gets a default security descriptor.
	 * 2nd Parameter dwStackSize:  0  - The new thread uses the default size for the executable.
	 * 3rd Parameter lpStartAddress:  KeyLogger - A pointer to the application-defined function to be executed by the thread
	 * 4th Parameter lpParameter:  argv[0] -  A pointer to a variable to be passed to the thread
	 * 5th Parameter dwCreationFlags: 0 - The thread runs immediately after creation.
	 * 6th Parameter pThreadId(out parameter): NULL - the thread identifier is not returned
	 * If the function succeeds, the return value is a handle to the new thread.
	 */
	DWORD tid;
	HANDLE thread = CreateThread(0, 0, hookThreadMain, argv[0], 0, &tid);

	MSG msg;
	while (GetMessage(&msg, 0, 0, 0) > 0) {
		// this seems to be necessary only for clicking exit in the system tray menu
		DispatchMessage(&msg);
	}
	return 0;
}

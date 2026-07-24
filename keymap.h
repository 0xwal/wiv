#ifndef KEYMAP_H
#define KEYMAP_H

#define KEY_PAD_BEFORE ""
#define KEY_PAD_AFTER ""

#define REPEAT_MARKER "⋅"
#define REPEAT_0 "0"
#define REPEAT_1 "1"
#define REPEAT_2 "2"
#define REPEAT_3 "3"
#define REPEAT_4 "4"
#define REPEAT_5 "5"
#define REPEAT_6 "6"
#define REPEAT_7 "7"
#define REPEAT_8 "8"
#define REPEAT_9 "9"

#define COLOR_BACKGROUND 0x00000000
#define COLOR_SPECIAL_FG 0xebdbb240
#define COLOR_FOREGROUND 0xebdbb2f0

#define DEFAULT_FONT "Sans Bold 40"
#define DISPLAY_MIN_HEIGHT 100
#define REPEAT_FONT_SCALE 0.5
#define REPEAT_DELAY 400  /* ms before first repeat */
#define REPEAT_RATE 40    /* ms between repeats (25Hz) */
#define TEXT_ALIGN_CENTER

typedef struct {
	const char *name; /* xkb keysym name, e.g. "Return", "a", "Control_L" */
	const char *display; /* UTF-8 display text, e.g. "⏎ ", "🅰️", " Ctrl+" */
	const char *fg; /* text color "#RRGGBB" or "#RRGGBBAA", NULL → global fg */
} KeymapEntry;

/* Variadic macro: 2 args → no color override, 3 args → with color */
#define K(...) K_GET_MACRO(__VA_ARGS__, K3, K2)(__VA_ARGS__)
#define K_GET_MACRO(_1, _2, _3, NAME, ...) NAME
#define K2(n, d) {n, d, NULL}
#define K3(n, d, f) {n, d, f}
#define KC(name, fg) {name, NULL, fg}

// clang-format off
static const KeymapEntry keymap[] = {
	// K("Return",    "⏎"),
	K("Return",    "⮐"),
	K("space",     "␣", "#ebdbb230"),
	K("Escape",    "⎌"), // ␛
	K("Control",   "⎈"),
	K("Alt",       "✵"),
	K("Meta",      "✵"),
	K("Shift",     "⇧"),
	K("Super",     "✬"),
	K("Tab",       "⮔"),
	K("backslash", "∖"),
	K("BackSpace", "␈"),
	K("Caps_Lock", "⇪"),
	K("Left",      "⇦"),
	K("Up",        "⇧"),
	K("Down",      "⇩"),
	K("Right",     "⇨"),
	K("KP_Insert", "0"),
	K("KP_End",    "1"),
	K("KP_Down",   "2"),
	K("KP_Next",   "3"),
	K("KP_Left",   "4"),
	K("KP_Begin",  "5"),
	K("KP_Right",  "6"),
	K("KP_Home",   "7"),
	K("KP_Up",     "8"),
	K("KP_Prior",  "9"),
	K("KP_Delete", "."),
	K("KP_Enter",  "⏎"),
	// K("0", "⁰"),
	// K("1", "¹"),
	// K("2", "²"),
	// K("3", "³"),
	// K("4", "⁴"),
	// K("5", "⁵"),
	// K("6", "⁶"),
	// K("7", "⁷"),
	// K("8", "⁸"),
	// K("9", "⁹"),
	// K("0", "₀"),
	// K("1", "₁"),
	// K("2", "₂"),
	// K("3", "₃"),
	// K("4", "₄"),
	// K("5", "₅"),
	// K("6", "₆"),
	// K("7", "₇"),
	// K("8", "₈"),
	// K("9", "₉"),
};
#define KEYMAP_LEN (sizeof(keymap) / sizeof(keymap[0]))

#endif

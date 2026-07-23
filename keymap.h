#ifndef KEYMAP_H
#define KEYMAP_H

#define KEY_PAD_BEFORE ""
#define KEY_PAD_AFTER  ""

typedef struct {
	const char *name;     /* xkb keysym name, e.g. "Return", "a", "Control_L" */
	const char *display;  /* UTF-8 display text, e.g. "⏎ ", "🅰️", " Ctrl+" */
	const char *fg;       /* text color "#RRGGBB" or "#RRGGBBAA", NULL → global fg */
} KeymapEntry;

/* Variadic macro: 2 args → no color override, 3 args → with color */
#define K(...)    K_GET_MACRO(__VA_ARGS__, K3, K2)(__VA_ARGS__)
#define K_GET_MACRO(_1, _2, _3, NAME, ...) NAME
#define K2(n, d)  { n, d, NULL }
#define K3(n, d, f) { n, d, f }
#define KC(name, fg) { name, NULL, fg }

static const KeymapEntry keymap[] = {
	K("Return",    "⏎"),
	K("space",     "␣"),
	K("Escape",    "Esc"),
	K("Control",   "Ctrl+"),
	K("Alt",       "Alt+"),
	K("Meta",      "Alt+"),
	K("Shift",     "⇧"),
	K("Super",     "Super+"),
	K("Tab",       "Tab"),
	K("backslash", "\\"),
	K("BackSpace", "⌫"),
	K("Caps_Lock", "Caps"),
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
	KC("k",    "#B5B520"),
};
#define KEYMAP_LEN (sizeof(keymap) / sizeof(keymap[0]))

#endif

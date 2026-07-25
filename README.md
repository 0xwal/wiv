# wiv — Wayland Input Visual

Displays keypresses on screen on supported Wayland compositors (requires
`wlr_layer_shell_v1` support).

https://github.com/user-attachments/assets/285463b6-3560-42e1-b59a-d565bed25464

Forked from [~sircmpwn/wshowkeys](https://git.sr.ht/~sircmpwn/wshowkeys) | [DreamMaoMao/wshowkeys](https://github.com/DreamMaoMao/wshowkeys) — wiv is a continuation. credits goes to them

## Installation

### nix
```bash
nix run github:0xWal/wiv
```

### building from source

Dependencies:

- cairo
- libinput
- pango
- udev
- wayland
- xkbcommon

```bash
$ meson build
$ ninja -C build
```

Build options:
- `-Ddevpath=` — set device path for input capture
- `-Dwsk_debug=true` — enable debug logging (requires debug build)

### permissions

wiv reads `/dev/input/` devices to capture keypresses.
Add your user to the `input` group:

```bash
sudo usermod -aG input $USER
```

Then **log out and back in** for the change to take effect.

After that, run `./build/wiv` directly — no root or setuid required.

## Usage

```
wiv [-b|-f|-s #RRGGBB[AA]] [-F font] [-t timeout]
    [-a top|left|right|bottom] [-m margin] [-l lenmax]
    [-o output] [-w [pixels]] [-i] [-H height] [-D trace] [-P] [-R]
    [-O opacity] [-c]
```

- *-b #RRGGBB[AA]*: set background color
- *-f #RRGGBB[AA]*: set foreground color
- *-s #RRGGBB[AA]*: set color for special keys
- *-r #RRGGBB[AA]*: set color for repeat count symbols
- *-F font*: set font (Pango format, e.g. 'monospace bold 24')
- *-t timeout*: set timeout before clearing old keystrokes(ms)
- *-a top|left|right|bottom*: anchor the keystrokes to an edge. May be specified
  twice.
- *-m margin*: set a margin (in pixels) from the nearest edge
- *-l lenmax*: set the key layer lenmax
- *-w [pixels]*: use a fixed overlay width instead of resizing per keystroke.
  Without a value, width is computed from `-l` and the current font via Pango.
  With a value, width is `max(pixels, font-computed width for -l)`.
  Text is right-aligned; the overlay hides when empty.
- *-o output*: show only on the specified monitor by name ex: -o eDP-1
- *-i*: inspect mode — show raw xkb keysym names instead of pretty-printed display names
- *-H height*: vertical padding per key row in px (added above/below text, default: 100)
- *-P*: pause — start paused or pause an already-running instance
- *-R*: resume — start normally or resume an already-running instance
- *-D trace*: trace file path for debug logging (debug build only)
- *-O [opacity]*: set global opacity (0.0–1.0). Multiplies the alpha channel of all colors.
  The value must be attached directly to `-O` (no space). When running a second
  instance, communicates with the active instance:
  - `wiv -O.5` — set opacity to 50%
  - `wiv -O` — query current opacity (prints value and exits)
  - `wiv -O"+.1"` — increment opacity by 10%
  - `wiv -O"-.1"` — decrement opacity by 10%
- *-c*: validate keymap config file and exit (prints `OK` or error with line number)
- *-K*: reload keymap config from file (sends reload signal to running instance)

**Environment:**
- *WIV_MASK*: comma-separated key sequence patterns to suppress/sensitive
  input masking

**Pause/Resume:**
wiv is a single-instance application. Running `wiv` a second time fails.
Use `wiv -P` to pause a running instance (clears displayed keys, freezes
the overlay with near-zero CPU usage). Use `wiv -R` to resume it.

**Opacity:**
wiv supports live opacity control via IPC. Launch with `-O<value>` (0.0–1.0)
to set the initial opacity, or adjust a running instance with `wiv -O<value>`.
(The value must be attached directly — no space between `-O` and the number.)

example:
```bash
wiv -a bottom -F 'Sans Bold 30' -s '#B5B520ff' -f  '#ecd29cff' -b '#201B1488' -l 60 -w
```

## Keymap Configuration

wiv loads a keymap config file on startup to override built-in key display
mappings. The file is loaded only when running as the primary instance.

**Path:** `$XDG_CONFIG_HOME/wiv/keymap` (typically `~/.config/wiv/keymap`).
Falls back to `~/.config/wiv/keymap` if `XDG_CONFIG_HOME` is not set.
If the file doesn't exist, the built-in keymap is used unchanged.

**Format:** one entry per line:

```
<key>|[display]|[color]
```

- `<key>`: xkb keysym name (e.g. `Return`, `space`, `KP_Enter`)
- `[display]`: UTF-8 display text (optional, empty = use key name)
- `[color]`: text color `#RRGGBB` or `#RRGGBBAA` (optional, empty = use global fg)

Escape `|` with `\|` for a literal pipe. Use `\\` for a literal backslash.
Lines starting with `#` are comments. Empty lines are ignored.

**Examples:**

```
# modifier keys
Return|⏎|
space|␣|#ebdbb230
KP_Enter|⏎|

# override just color, keep default display
l||#ff0000

# override just display, keep default color
Escape|⎌|

# both NULL — no-op override
BackSpace||
```

Use `wiv -c` to validate the config file without starting the application.

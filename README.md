# wiv — Wayland Input Visual

Displays keypresses on screen on supported Wayland compositors (requires
`wlr_layer_shell_v1` support).




https://github.com/DreamMaoMao/myshowkey/assets/30348075/944d9e92-ea6f-4017-9b50-11ba1a34ee32





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
    [-o output] [-i] [-H height] [-D trace] [-P] [-R]
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
- *-o output*: show only on the specified monitor by name ex: -o eDP-1
- *-i*: inspect mode — show raw xkb keysym names instead of pretty-printed display names
- *-H height*: vertical padding per key row in px (added above/below text, default: 100)
- *-P*: pause — start paused or pause an already-running instance
- *-R*: resume — start normally or resume an already-running instance
- *-D trace*: trace file path for debug logging (debug build only)

**Environment:**
- *WIV_MASK*: comma-separated key sequence patterns to suppress/sensitive
  input masking

**Pause/Resume:**
wiv is a single-instance application. Running `wiv` a second time fails.
Use `wiv -P` to pause a running instance (clears displayed keys, freezes
the overlay with near-zero CPU usage). Use `wiv -R` to resume it.

example:
```bash
wiv -a bottom -F 'Sans Bold 30' -s '#B5B520ff' -f  '#ecd29cff' -b '#201B1488' -l 60
```

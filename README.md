# wshowkeys

Displays keypresses on screen on supported Wayland compositors (requires
`wlr_layer_shell_v1` support).




https://github.com/DreamMaoMao/myshowkey/assets/30348075/944d9e92-ea6f-4017-9b50-11ba1a34ee32





Forked from https://git.sr.ht/~sircmpwn/wshowkeys as Drew has moved onto other thigns.

## Installation

### arch
```bash
yay -S wshowkeys-mao-git
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

### permissions

wshowkeys reads `/dev/input/` devices to capture keypresses.
Add your user to the `input` group:

```bash
sudo usermod -aG input $USER
```

Then **log out and back in** for the change to take effect.

After that, run `./build/wshowkeys` directly — no root or setuid required.

## Usage

```
wshowkeys [-b|-f|-s #RRGGBB[AA]] [-F font] [-t timeout]
    [-a top|left|right|bottom] [-m margin] [-o output]
```

- *-b #RRGGBB[AA]*: set background color
- *-f #RRGGBB[AA]*: set foreground color
- *-s #RRGGBB[AA]*: set color for special keys
- *-F font*: set font (Pango format, e.g. 'monospace 24')
- *-t timeout*: set timeout before clearing old keystrokes(ms)
- *-a top|left|right|bottom*: anchor the keystrokes to an edge. May be specified
  twice.
- *-m margin*: set a margin (in pixels) from the nearest edge
- *-l lenmax*: set the key layer lenmax
- *-o output*: request wshowkeys is shown on the specified output
  (unimplemented)

example:
```bash
wshowkeys -a bottom -F 'Sans Bold 30' -s '#B5B520ff' -f  '#ecd29cff' -b '#201B1488' -l 60
```

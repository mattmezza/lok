# mlock 🐵

**m**onkey **lock**er (or matteo's locker, pick your favorite) — a simple
screen locker for X, in the spirit of [slock](https://tools.suckless.org/slock/)
but with proper text rendering and a three-wise-monkeys state machine.

| idle | typing | wrong password | caps lock |
|------|--------|----------------|-----------|
| ![init](screenshots/1-init.png) | ![typing](screenshots/2-typing.png) | ![failed](screenshots/3-failed.png) | ![caps](screenshots/4-caps.png) |

## Features

- 🐵 idle / 🙈 typing (it's not looking, promise) / 🙊 wrong password /
  🙉 caps lock warning — all emojis configurable per state
- background color per state; the typing state alternates between two
  colors on every keypress, slock style
- configurable title, subtitle and footer text, rendered with pango;
  any of them can be disabled
- the footer is expanded with `strftime(3)`, so it doubles as a clock
- failed attempt counter
- multi-monitor aware: the text stack is centered on every connected
  monitor (XRandR), and every X screen gets its own lock window
- wrong-password feedback via color, emoji and counter; optional
  `failonclear` like slock
- optional DPMS timeout to turn the monitor off while locked
- the whole process is locked in RAM (`mlockall(2)`), so the typed
  password can never end up in swap
- OOM-killer protection on Linux
- runs a command after locking, e.g. `mlock systemctl suspend`

## Requirements

libx11, libxext, libxrandr, pango (pangocairo) and a color emoji font.
On Arch:

```sh
pacman -S --needed libx11 libxext libxrandr pango cairo noto-fonts-emoji
```

## Installation

```sh
sudo make clean install
```

This installs `mlock` setuid root (needed to read `/etc/shadow`; privileges
are dropped to `nobody` immediately after, before any font rendering or X
traffic happens).

## Configuration

Major configuration lives in `config.def.h` (colors, emojis, texts, fonts,
spacing, DPMS timeout, the user/group to drop privileges to). The first
`make` copies it to `config.h`; edit that and recompile.

The texts can also be overridden at runtime:

```
mlock [-v] [-t title] [-s subtitle] [-b bottomtext] [cmd [arg ...]]
```

An empty string disables an element: `mlock -t "" -s "" -b ""` gives you a
bare colored screen with just the monkey.

```sh
mlock -t "gone for lunch" -s "" -b "back at %H:30"
mlock systemctl suspend
xss-lock -- mlock &        # lock automatically on suspend/idle
```

## Testing

`make test` runs a headless end-to-end test (lock → wrong password →
caps lock → correct password → unlock) under Xvfb, using an `LD_PRELOAD`
shim to inject a known password hash so no root is needed. Requires
`xorg-server-xvfb`, `xdotool` and `openssl`.

## Security notes

The usual X11 locker caveats apply: mlock grabs the keyboard and pointer
and disables the OOM killer for itself, but it cannot stop someone from
switching to another VT (disable that in your Xorg config if you care) or
from sysrq-killing the X server. It protects against the casual passerby,
not against a forensics lab.

## License

MIT/X Consortium License, see [LICENSE](LICENSE). Derived from slock by
the suckless.org community.

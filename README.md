# lok 🐵

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
  colors on every keypress, slock style; this typing feedback can be
  disabled
- configurable title, subtitle and footer text, rendered with pango;
  any of them can be disabled
- every text field (title, subtitle, footer) supports live
  `strftime(3)` expansion — any of them can act as a clock;
  controlled per-field via the `-T`/`-S`/`-B` flags or
  `title_datetime_updated`/`subtitle_datetime_updated`/
  `footer_datetime_updated` in `config.h`
- asynchronous fingerprint unlocking through a dedicated fprintd PAM service
- failed attempt counter
- multi-monitor aware: the text stack is centered on every connected
  monitor (XRandR), and every X screen gets its own lock window
- wrong-password feedback via color, emoji and counter; optional
  `failonclear` like slock
- optional DPMS timeout to turn the monitor off while locked
- the whole process is locked in RAM (`mlockall(2)`), so the typed
  password can never end up in swap
- OOM-killer protection on Linux
- runs a command after locking, e.g. `lok systemctl suspend`

## Requirements

libx11, libxext, libxrandr, pango (pangocairo), PAM development headers/library and a color emoji font.
Fingerprint unlocking also requires fprintd.
On Arch:

```sh
sudo pacman -S --needed base-devel pkgconf libx11 libxext libxrandr pango cairo pam fprintd noto-fonts-emoji
```

## Installation

```sh
make
sudo make install
sudo make install-pam
```

This installs `lok` setuid root (needed to read `/etc/shadow`; privileges
are dropped to `nobody` in the UI process before rendering and screen grabs).
A separate PAM supervisor retains the original privilege, captures the real
invoking user's name before the drop, and is forked before reading the password
hash or opening X. Run `lok` as your normal user, **not `sudo lok`**, which would
authenticate root.

`install-pam` explicitly installs `/etc/pam.d/lok-fingerprint` as root-owned mode
0644. It is separate from `install` to avoid replacing a locally customized PAM
policy during routine upgrades. The supplied fingerprint-only policy is:

```pam
auth required pam_fprintd.so max-tries=3 timeout=30
```

Do not include `system-auth` or add `pam_permit` to this service. Only fingerprint
success should satisfy this policy. Missing, symlinked, non-root-owned or
writable-by-group/others service files disable fingerprint authentication;
password unlocking still works. Remove this service file to disable fingerprint
unlocking (uninstall leaves administrator-managed PAM policy in place).

Scanning begins only after all screens lock. Either enrolled index finger can
unlock; password input and Return remain usable during scanning. Failed scans,
missing hardware and PAM errors retry automatically after two seconds. The PAM
policy times out after 30 seconds; a supervisor also kills stalled attempts after
40 seconds and retries. Successful authentication and exiting lok cancel the
worker and close its fprintd connection, releasing the reader.

To test on your laptop, first confirm `fprintd-verify "$USER"`, then run `lok`
from a terminal in your X11 session. Try each enrolled index finger in separate
locks, an unenrolled finger, and waiting through a timeout before retrying. Also
type and submit your password while the reader is scanning. Confirm the reader
is released afterward with another `fprintd-verify "$USER"`. To test the fallback,
move `/etc/pam.d/lok-fingerprint` aside before launching lok, unlock with your
password, then restore it. Keep a second session available during initial testing.

## Configuration

Major configuration lives in `config.def.h` (colors, typing-background
feedback, emojis, texts, fonts, spacing, DPMS timeout, and the user/group to
drop privileges to). The first `make` copies it to `config.h`; edit that and
recompile.

The texts can also be overridden at runtime:

```
lok [-v] [-t title] [-s subtitle] [-b bottomtext] \
      [-T 0/1] [-S 0/1] [-B 0/1] [-A 0/1] [cmd [arg ...]]
```

An empty string disables an element: `lok -t "" -s "" -b ""` gives you a
bare colored screen with just the monkey.

The `-T`/`-S`/`-B` flags enable or disable live `strftime(3)` expansion
for the title, subtitle and footer respectively (default: 1, from
`config.h`). When enabled, format specifiers such as `%H:%M` or `%A`
are replaced with the current time and updated every second.

The `-A` flag enables or disables the alternating typing background
(default: 1, from `typing_background_feedback` in `config.h`). When disabled,
the background stays at the idle color while typing; other typing visuals and
the failed-password and Caps Lock backgrounds are unchanged.

```sh
# footer still acts as a clock; title and subtitle are static
lok -T 0 -S 0

# all three fields show the current time
lok -t "%A %H:%M" -s "%B %d" -b "locker since %H:%M:%S"

# title can be a clock too, footer is static
lok -t "It is %H:%M" -B 0

lok systemctl suspend
xss-lock -- lok &        # lock automatically on suspend/idle
```

## Testing

`make test` runs a headless end-to-end test (lock → wrong password →
caps lock → correct password → unlock) under Xvfb, using an `LD_PRELOAD`
shim to inject a known password hash and a deliberately blocked PAM worker,
so no root is needed and password responsiveness/worker cancellation are tested.
Fork/socket tests also cover fingerprint success, failure, refused prompts,
missing/unsafe PAM policy, timeouts, retries, cleanup and abrupt locker exit. Requires
`xorg-server-xvfb`, `xdotool` and `openssl`.

## Security notes

Fingerprint authentication adds a small privileged supervisor and PAM child.
The UI receives a single success byte on an unnamed socketpair; there is no
public socket, PID-based signal unlock, environment-selected identity or PAM
service, and no password sent to PAM. Only the supervisor can send success, after
its child exits normally with successful `pam_authenticate` and `pam_end`.
PAM prompts are rejected. The PAM child closes the UI socket, and the post-lock
command closes it before exec. EOF on UI death cancels/reaps the PAM child;
normal unlock waits for cleanup before destroying lock windows. The supervisor uses root in all UID slots on setuid launches and disables
core dumps to prevent caller signals or inspection. It
processes only a start byte and cancellation; it never parses user-supplied
commands. Root-owned PAM configuration and its modules remain trusted code.
The shipped policy authenticates fingerprints only; it does not add PAM account
or session management to lok's existing password policy.

The usual X11 locker caveats apply: lok grabs the keyboard and pointer
and disables the OOM killer for itself, but it cannot stop someone from
switching to another VT (disable that in your Xorg config if you care) or
from sysrq-killing the X server. It protects against the casual passerby,
not against a forensics lab.

## License

MIT/X Consortium License, see [LICENSE](LICENSE). Derived from slock by
the suckless.org community.

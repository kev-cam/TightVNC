# TightVNC 1.3.10 for Unix, with Xpra window sharing

This is the last TightVNC release for Unix (1.3.10, March 2009) — the
`Xvnc` X server, `vncviewer`, `vncpasswd`, `vncconnect` and the `vncserver`
script — kept building on current systems, with one addition: the server
can share its windows one by one with [Xpra](https://xpra.org) clients,
next to showing the whole desktop to VNC viewers.

Only `vnc_unixsrc` is built and tested here. `vnc_winsrc` and `vnc_javasrc`
are the 1.3.10 Windows and Java sources as released, untouched.

## What is in the history

1. The TightVNC releases up to 1.3.10, as kept by
   [TurboVNC/tightvnc](https://github.com/TurboVNC/tightvnc).
2. Debian's patches as of package `1:1.3.10-11`, one commit per patch with
   its author and description (`Debian-Patch:` in the message says which):
   a build system of plain Makefiles in place of imake, the fixes for nine
   CVEs, 64 bit and other architecture fixes, builds with current gcc.
3. Our work: Xpra window sharing in `Xvnc`, tests, fixes.

Other people's later work is brought in with
[diaspora](https://github.com/kev-cam/diaspora); `diaspora.conf` names the
peers and the tests an import has to pass.

## Building

The X server needs a C compiler, zlib and libjpeg (`zlib1g-dev`,
`libjpeg-dev`), nothing else:

```
cd vnc_unixsrc
smak server            # or: make server
```

gives `Xvnc/programs/Xserver/Xvnc` (Debian installs it as `Xtightvnc`).
`smak` with no target builds everything; the viewer also needs the X
toolkit libraries (`libxaw7-dev libxmu-dev libxt-dev libxi-dev libx11-dev
libxext-dev`).

The compiler is told `-std=gnu90` unless `CFLAGS` is set: gcc 14 and later
take this code as nothing newer. Build from `vnc_unixsrc` or
`vnc_unixsrc/Xvnc`, not from a directory further down: the definitions the
whole server is compiled with are exported from there.

## Running

As any TightVNC: `vncserver :1` (the script passes options it does not
know on to `Xvnc`), or `Xvnc` directly, which wants to be told where the
fonts and the colour names are:

```
Xvnc :1 -geometry 3200x2000 -depth 24 -rfbauth ~/.vnc/passwd \
     -fp /usr/share/fonts/X11/misc/,/usr/share/fonts/X11/75dpi/ \
     -co /usr/share/X11/rgb -xpra
```

## Sharing windows with Xpra

| option | |
|---|---|
| `-xpra` | listen on `~/.xpra/<host>-<display>`, where `xpra attach :N` and `xpra list` look |
| `-xprasocket path` | listen on this unix socket |
| `-xpraport port` | listen on this TCP port, loopback only (there is no authentication: tunnel it) |

```
xpra attach :1                           # same machine
xpra attach ssh://host/1                 # xpra installed on both ends
ssh -L 14501:localhost:14501 host        # -xpraport 14501: no xpra needed on host
xpra attach tcp://localhost:14501/
```

Every top level window of the VNC desktop becomes a window on the desktop
the client runs on, with its title, placed and resized by that desktop's
window manager; menus and other popups follow their window. Keys and
pointer go back to the window they were made in. A VNC viewer can be
connected at the same time. `xpra list`, `info`, `version` and `id` work;
`xpra stop` is refused. One client shows the windows at a time, and a new
one takes over, as with any Xpra server. Tested with the xpra 6.5.5 client.

**What to know.** This X server is from before the Composite extension: it
keeps one picture of the screen, so a window's pixels exist only where the
window is visible, and only that part can be sent.

- With no window manager running, `Xvnc` keeps the windows apart: one that
  would cover another, or reach off the screen, is moved to a free part of
  the screen. Its place on the VNC screen means nothing to an Xpra client,
  but the screen must be big enough for the windows side by side, hence a
  generous `-geometry` (the framebuffer costs four bytes a pixel).
- Under a window manager the windows stay where they are. The one given
  the focus is raised, and what was covered arrives as it is repainted.
- A window cannot be made larger than the screen.

Not there yet: pointer shapes, the clipboard, the bell, lossy encodings
(windows are sent losslessly, LZ4 compressed), authentication on the TCP
port.

The code: `hw/vnc/xpra_wire.c` is the wire format (packets, rencodeplus,
LZ4) and nothing else; `hw/vnc/xpra.c` is the server side. Both under
`vnc_unixsrc/Xvnc/programs/Xserver`.

## Tests

```
cd vnc_unixsrc
smak check                                   # all of them
tests/run-tests --list
tests/run-tests typing popup                 # some
```

Each test starts an `Xvnc` on a display of its own, runs X programs on it
and drives it over both protocols: an Xpra client written for the purpose
keeps a picture of every window from what it is sent, an RFB client
fetches the framebuffer, and the two have to agree pixel for pixel. Also
tested: typing, clicking, menus, resizing, closing, a second client taking
over, the X server's reset, a VNC viewer across that reset, malformed
input, a window manager, and the real `xpra` client under `Xvfb`. A test
whose programs are not installed (`xterm`, `xclock`, `xmessage`,
`xwininfo`, a window manager, `xpra`, `Xvfb`) is skipped.

`tests/wire_test.c` tests the wire format alone and is built with
`-fsanitize=address,undefined` where the compiler has it. To run
everything against a server built that way:

```
CFLAGS="-g -O1 -std=gnu90 -fsanitize=address" LDFLAGS=-fsanitize=address smak server
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 tests/run-tests
```

## Licence

GNU General Public License, version 2 or later: `vnc_unixsrc/LICENCE.TXT`.
The X server parts carry the X Consortium's and XFree86's notices.

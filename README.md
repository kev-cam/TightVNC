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

The X server needs a C compiler, zlib and libjpeg, nothing else:

```
sudo apt install libjpeg-dev zlib1g-dev
cd vnc_unixsrc
smak server            # or: make server
```

gives the X server, `Xvnc/programs/Xserver/Xvnc`, and `vncpasswd`: what a
machine that runs the server needs.

`smak` with no target builds everything the machine has the headers for
and says what it left out: `vncviewer` wants the X toolkit and `vncconnect`
Xlib, which a machine that only runs the server may not have.

```
sudo apt install libx11-dev libxext-dev libxt-dev libxaw7-dev libxmu-dev
```

Without zlib's or libjpeg's headers the build stops at once and says so.

The compiler is told `-std=gnu90` unless `CFLAGS` is set: gcc 14 and later
take this code as nothing newer. Build from `vnc_unixsrc` or
`vnc_unixsrc/Xvnc`, not from a directory further down: the definitions the
whole server is compiled with are exported from there.

## Installing

```
sudo make install                 # into /usr/local
sudo make install PREFIX=/usr     # over a distribution's tightvncserver
make uninstall                    # with the same PREFIX: takes them away again
```

installs what has been built (it builds nothing itself) under the names
Debian uses, which are the names the `vncserver` script in this tree runs:

| built | installed as |
|---|---|
| `Xvnc/programs/Xserver/Xvnc` | `bin/Xtightvnc` |
| `vncserver` (a script) | `bin/tightvncserver` |
| `vncpasswd/vncpasswd` | `bin/tightvncpasswd` |
| `vncconnect/vncconnect` | `bin/tightvncconnect` |
| `vncviewer/vncviewer` | `bin/xtightvncviewer` |

and their manual pages in `share/man/man1`. After a `git pull`, run `make`
before `make install`: install stops if a source has changed since it was
compiled, or if the server's last build did not get as far as linking, so
that it cannot put in the programs from before the change. On a machine that has
Debian's `tightvncserver` package, the default `/usr/local` is enough to
switch over without touching the package: `/usr/local/bin` comes before
`/usr/bin` on the usual `PATH`, so `tightvncserver` and the `Xtightvnc` it
starts are then these. Check with `command -v Xtightvnc`; a service that
starts the server by its full path, or with a `PATH` of its own, has to be
pointed at the new one. `/etc/tightvncserver.conf` is read if it is there
and is not installed.

## Running

`tightvncserver :1` as with any TightVNC; options the script does not know
go on to the X server, so

```
tightvncserver :1 -geometry 3200x2000 -xpra
```

is a desktop for VNC viewers whose windows Xpra clients can attach to as
well. Or the X server directly (`Xvnc` in the build tree, `Xtightvnc`
installed):

```
Xtightvnc :1 -geometry 3200x2000 -depth 24 -rfbauth ~/.vnc/passwd -xpra
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
one takes over, as with any Xpra server. Tested with the xpra 6.5.5 and 7.0 clients.

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

The pointer takes the shape the application gives it.

**Clipboard.** CLIPBOARD and PRIMARY are shared both ways with the Xpra
client (the stock one, or `xpra-view`): copy in either place, paste in the
other. Contents travel only when something is pasted, so copying costs
nothing; large ones (INCR) are fine, up to 16 MB. When the client leaves,
what it had copied is let go here at once, so nothing waits for it. This
is separate from TightVNC's own cut text for VNC viewers, which still goes
through `CUT_BUFFER0` as before.

This X server has no XFIXES extension either, which GTK applications use
to hear that the clipboard has changed. Running inside it, they do not
notice a copy made after they started, wherever it was made: a Paste menu
item (and the shortcut that goes with it, as Ctrl+Shift+V in
mate-terminal) can stay greyed out, while Ctrl+V in a text field and a
middle click still paste.

Not there yet: the bell, lossy encodings (windows are sent losslessly,
LZ4 compressed), authentication on the TCP port.

The code: `hw/vnc/xpra_wire.c` is the wire format (packets, rencodeplus,
LZ4) and nothing else; `hw/vnc/xpra.c` is the server side (the clipboard is its own section: as
the server is not an X client, it watches SetSelectionOwner,
ConvertSelection, SendEvent and ChangeProperty on their way to the dix).
Both under
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
input, a window manager, the real `xpra` client under `Xvfb`, and `make
install` followed by a session started with the installed
`tightvncserver` script. A test
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

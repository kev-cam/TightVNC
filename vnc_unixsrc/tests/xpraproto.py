# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 D. Kevin Cameron
"""The two protocols Xvnc speaks, enough of each to test it.

An Xpra client (packet framing, rencodeplus, LZ4) that keeps a picture of
every window from the draw packets it is sent, and an RFB client that
fetches the framebuffer.  The two views of the same pixels are what the
tests compare.  Standard library only: these do not use xpra's code, so
that they check the server against the format and not against itself.
"""

import socket
import struct
import time

# ---------------------------------------------------------------------------
# rencodeplus


def rencode(v):
    if v is True:
        return b"\x43"
    if v is False:
        return b"\x44"
    if v is None:
        return b"\x45"
    if isinstance(v, int):
        if 0 <= v < 44:
            return bytes([v])
        if -32 <= v < 0:
            return bytes([70 + (-1 - v)])
        for code, fmt, bits in ((62, "!b", 7), (63, "!h", 15), (64, "!l", 31), (65, "!q", 63)):
            if -(1 << bits) <= v < (1 << bits):
                return bytes([code]) + struct.pack(fmt, v)
        return b"\x3d" + str(v).encode() + b"\x7f"
    if isinstance(v, float):
        return b"\x2c" + struct.pack("!d", v)
    if isinstance(v, str):
        b = v.encode("utf-8")
        if len(b) < 64:
            return bytes([128 + len(b)]) + b
        return str(len(b)).encode() + b":" + b
    if isinstance(v, (bytes, bytearray, memoryview)):
        b = bytes(v)
        return str(len(b)).encode() + b"/" + b
    if isinstance(v, (list, tuple)):
        body = b"".join(rencode(x) for x in v)
        if len(v) < 64:
            return bytes([192 + len(v)]) + body
        return b"\x3b" + body + b"\x7f"
    if isinstance(v, dict):
        body = b"".join(rencode(k) + rencode(x) for k, x in v.items())
        if len(v) < 25:
            return bytes([102 + len(v)]) + body
        return b"\x3c" + body + b"\x7f"
    raise TypeError("cannot encode %r" % type(v))


def _rdecode(data, pos):
    t = data[pos]
    pos += 1
    if t < 44:
        return t, pos
    if t == 44:
        return struct.unpack_from("!d", data, pos)[0], pos + 8
    if 48 <= t <= 57:
        end = pos
        while data[end] not in (58, 47):
            end += 1
        n = int(data[pos - 1:end])
        s = bytes(data[end + 1:end + 1 + n])
        if len(s) != n:
            raise ValueError("string cut short")
        return (s.decode("utf-8") if data[end] == 58 else s), end + 1 + n
    if t in (59, 60):
        items = []
        while data[pos] != 127:
            item, pos = _rdecode(data, pos)
            items.append(item)
        pos += 1
        if t == 59:
            return items, pos
        return dict(zip(items[0::2], items[1::2])), pos
    if t == 61:
        end = data.index(b"\x7f", pos)
        return int(data[pos:end]), end + 1
    if 62 <= t <= 65:
        fmt = ("!b", "!h", "!l", "!q")[t - 62]
        return struct.unpack_from(fmt, data, pos)[0], pos + struct.calcsize(fmt)
    if t == 66:
        return struct.unpack_from("!f", data, pos)[0], pos + 4
    if t == 67:
        return True, pos
    if t == 68:
        return False, pos
    if t == 69:
        return None, pos
    if 70 <= t < 102:
        return -1 - (t - 70), pos
    if 102 <= t < 127:
        d = {}
        for _ in range(t - 102):
            k, pos = _rdecode(data, pos)
            d[k], pos = _rdecode(data, pos)
        return d, pos
    if 128 <= t < 192:
        n = t - 128
        return bytes(data[pos:pos + n]).decode("utf-8"), pos + n
    if t >= 192:
        items = []
        for _ in range(t - 192):
            item, pos = _rdecode(data, pos)
            items.append(item)
        return items, pos
    raise ValueError("bad type byte %d" % t)


def rdecode(data):
    v, pos = _rdecode(data, 0)
    if pos != len(data):
        raise ValueError("%d bytes after the value" % (len(data) - pos))
    return v


# ---------------------------------------------------------------------------
# LZ4 blocks with xpra's four byte length in front


def lz4_decompress(data):
    size = struct.unpack_from("<I", data, 0)[0]
    out = bytearray()
    pos, end = 4, len(data)
    while True:
        token = data[pos]
        pos += 1
        n = token >> 4
        if n == 15:
            while True:
                b = data[pos]
                pos += 1
                n += b
                if b != 255:
                    break
        out += data[pos:pos + n]
        pos += n
        if pos >= end:
            break
        off = data[pos] | (data[pos + 1] << 8)
        pos += 2
        n = token & 15
        if n == 15:
            while True:
                b = data[pos]
                pos += 1
                n += b
                if b != 255:
                    break
        n += 4
        if off == 0 or off > len(out):
            raise ValueError("bad match offset")
        start = len(out) - off
        if off >= n:
            out += out[start:start + n]
        else:
            # the match overlaps its own output: it repeats
            piece = bytes(out[start:])
            out += (piece * (n // off + 1))[:n]
    if len(out) != size:
        raise ValueError("decompressed to %d bytes, not %d" % (len(out), size))
    return bytes(out)


def lz4_store(data):
    """A valid block that compresses nothing: one run of literals."""
    n = len(data)
    out = bytearray(struct.pack("<I", n))
    if n >= 15:
        out.append(0xf0)
        rest = n - 15
        while rest >= 255:
            out.append(255)
            rest -= 255
        out.append(rest)
    else:
        out.append(n << 4)
    return bytes(out + data)


# ---------------------------------------------------------------------------
# Xpra


class Closed(Exception):
    pass


class Window:
    def __init__(self, wid, x, y, w, h, metadata, override_redirect):
        self.wid = wid
        self.x, self.y, self.w, self.h = x, y, w, h
        self.metadata = dict(metadata)
        self.override_redirect = override_redirect
        self.pixels = bytearray(w * h * 4)      # as sent: blue, green, red, x
        self.painted = bytearray(w * h)         # 1 where a draw has reached
        self.draws = 0

    def resize(self, w, h):
        old, oldp, ow, oh = self.pixels, self.painted, self.w, self.h
        self.w, self.h = w, h
        self.pixels = bytearray(w * h * 4)
        self.painted = bytearray(w * h)
        for row in range(min(h, oh)):
            n = min(w, ow)
            self.pixels[row * w * 4:row * w * 4 + n * 4] = old[row * ow * 4:row * ow * 4 + n * 4]
            self.painted[row * w:row * w + n] = oldp[row * ow:row * ow + n]

    def title(self):
        return self.metadata.get("title", "")

    def __repr__(self):
        return "<window %d %r %dx%d+%d+%d%s>" % (
            self.wid, self.title(), self.w, self.h, self.x, self.y,
            " popup" if self.override_redirect else "")


class XpraClient:
    """A client that shows nothing but remembers everything."""

    def __init__(self, target, automap=True):
        if isinstance(target, str):
            self.sock = socket.socket(socket.AF_UNIX)
            self.sock.connect(target)
        else:
            self.sock = socket.create_connection(target)
        self.buf = b""
        self.raw = {}
        self.automap = automap
        self.server = None          # the server's hello
        self.windows = {}
        self.lost = []              # windows that have gone
        self.packets = []           # everything received, in order
        self.disconnected = None    # the reason, once told
        self.closed = False
        self.seq = 0

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
        self.closed = True

    # -- sending

    def send_bytes(self, data):
        self.sock.sendall(data)

    def send(self, *packet, compress=False, chunk=None):
        """chunk=i sends item i of the packet as a raw chunk ahead of it."""
        packet = list(packet)
        out = b""
        if chunk is not None:
            data = bytes(packet[chunk])
            packet[chunk] = b""
            out += struct.pack("!BBBBL", 80, 0x10, 0, chunk, len(data)) + data
        data = rencode(packet)
        level = 0
        if compress:
            data = lz4_store(data)
            level = 0x11
        out += struct.pack("!BBBBL", 80, 0x18, level, 0, len(data)) + data
        self.send_bytes(out)

    def hello(self, request=None, **extra):
        caps = {
            "version": "6.5",
            "rencodeplus": True,
            "encoders": ["rencodeplus"],
            "compressors": ["lz4", "none"],
            "lz4": {"": True},
            "client_type": "tightvnc tests",
            "windows": True,
            "keyboard": True,
            "desktop_size": [1600, 1200],
        }
        if request:
            caps["request"] = request
        caps.update(extra)
        # as the real client does: the hello itself is compressed
        self.send("hello", caps, compress=True)

    # -- receiving

    def _read_packet(self, deadline):
        while True:
            if len(self.buf) >= 8:
                magic, flags, level, index, size = struct.unpack_from("!cBBBL", self.buf)
                if magic != b"P":
                    raise ValueError("not a packet header: %r" % self.buf[:8])
                if len(self.buf) >= 8 + size:
                    data = self.buf[8:8 + size]
                    self.buf = self.buf[8 + size:]
                    if level & 0x10:
                        data = lz4_decompress(data)
                    elif level:
                        raise ValueError("compression %#x" % level)
                    if index:
                        self.raw[index] = data
                        continue
                    packet = rdecode(data)
                    for i, chunk in self.raw.items():
                        packet[i] = chunk
                    self.raw = {}
                    return packet
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            self.sock.settimeout(left)
            try:
                data = self.sock.recv(1 << 20)
            except socket.timeout:
                return None
            except OSError:
                data = b""
            if not data:
                self.closed = True
                raise Closed()
            self.buf += data

    def _handle(self, p):
        t = p[0]
        self.packets.append(p)
        if t == "hello":
            self.server = p[1]
        elif t in ("new-window", "new-override-redirect"):
            wid, x, y, w, h, meta = p[1:7]
            orr = t == "new-override-redirect" or bool(meta.get("override-redirect"))
            win = self.windows[wid] = Window(wid, x, y, w, h, meta, orr)
            if self.automap and not orr:
                self.map_window(win)
        elif t == "lost-window":
            win = self.windows.pop(p[1], None)
            if win:
                self.lost.append(win)
        elif t == "window-metadata":
            if p[1] in self.windows:
                self.windows[p[1]].metadata.update(p[2])
        elif t == "window-resized":
            if p[1] in self.windows:
                self.windows[p[1]].resize(p[2], p[3])
        elif t == "window-move-resize":
            win = self.windows.get(p[1])
            if win:
                win.x, win.y = p[2], p[3]
                if (win.w, win.h) != (p[4], p[5]):
                    win.resize(p[4], p[5])
        elif t == "draw":
            self._draw(p)
        elif t == "disconnect":
            self.disconnected = p[1] if len(p) > 1 else ""

    def _draw(self, p):
        wid, x, y, w, h, coding, data, seq, rowstride, options = p[1:11]
        win = self.windows.get(wid)
        ok = -1
        if win and coding in ("rgb24", "rgb32") and options.get("rgb_format") == "BGRX":
            if options.get("lz4"):
                data = lz4_decompress(data)
            if len(data) == rowstride * h and rowstride >= w * 4:
                for row in range(h):
                    yy = y + row
                    if not 0 <= yy < win.h:
                        continue
                    x0, x1 = max(x, 0), min(x + w, win.w)
                    if x1 <= x0:
                        continue
                    src = row * rowstride + (x0 - x) * 4
                    dst = (yy * win.w + x0) * 4
                    win.pixels[dst:dst + (x1 - x0) * 4] = data[src:src + (x1 - x0) * 4]
                    win.painted[yy * win.w + x0:yy * win.w + x1] = b"\x01" * (x1 - x0)
                win.draws += 1
                ok = 1
        self.send("damage-sequence", seq, wid, w, h, ok, "")

    def pump(self, seconds, until=None):
        """Take in packets for this long, or until until(self) is true.
        Returns what until returned last (True if there was none)."""
        deadline = time.monotonic() + seconds
        while True:
            if until and until(self):
                return True
            try:
                p = self._read_packet(deadline)
            except Closed:
                return bool(until and until(self))
            if p is None:
                return bool(until(self)) if until else True
            self._handle(p)

    def settle(self, quiet=0.4, limit=10.0):
        """Take in packets until none has come for a while."""
        end = time.monotonic() + limit
        while time.monotonic() < end:
            n = len(self.packets)
            self.pump(quiet)
            if len(self.packets) == n or self.closed:
                return

    def attach(self, timeout=10.0):
        self.hello()
        if not self.pump(timeout, lambda c: any(p[0] == "startup-complete" for p in c.packets)):
            raise AssertionError("no startup-complete from the server")
        self.settle()

    # -- what a user does

    def find(self, title=None, cls=None, popup=None):
        for win in self.windows.values():
            if title is not None and title not in win.title():
                continue
            if cls is not None and cls not in win.metadata.get("class-instance", []):
                continue
            if popup is not None and win.override_redirect != popup:
                continue
            return win
        return None

    def map_window(self, win, x=None, y=None):
        self.send("map-window", win.wid, win.x if x is None else x,
                  win.y if y is None else y, win.w, win.h, {}, {})

    def configure(self, win, x, y, w, h):
        self.send("configure-window", win.wid, x, y, w, h, {}, 0, {}, False, win.wid, [0, 0], [])

    def focus(self, win):
        self.send("focus", win.wid if win else 0, [])

    def pointer(self, win, x, y):
        self.seq += 1
        self.send("pointer", 0, self.seq, win.wid, [win.x + x, win.y + y, x, y],
                  {"modifiers": [], "buttons": []})

    def button(self, win, button, pressed, x, y):
        self.seq += 1
        self.send("pointer-button", 0, self.seq, win.wid, button, pressed,
                  [win.x + x, win.y + y, x, y], {"modifiers": [], "buttons": []})

    def click(self, win, x, y, button=1):
        self.pointer(win, x, y)
        self.button(win, button, True, x, y)
        self.button(win, button, False, x, y)

    def key(self, win, keysym, pressed, name=""):
        self.send("key-action", win.wid, name, pressed, [], keysym, "", 0, 0)

    def type(self, win, text):
        names = {"\n": 0xff0d, "\t": 0xff09, "\x04": None}
        for ch in text:
            if ch == "\x04":            # control-d
                self.key(win, 0xffe3, True, "Control_L")
                self.key(win, ord("d"), True, "d")
                self.key(win, ord("d"), False, "d")
                self.key(win, 0xffe3, False, "Control_L")
                continue
            sym = names.get(ch, ord(ch))
            self.key(win, sym, True, ch)
            self.key(win, sym, False, ch)


# ---------------------------------------------------------------------------
# RFB


def _recvn(sock, n):
    buf = bytearray()
    while len(buf) < n:
        d = sock.recv(n - len(buf))
        if not d:
            raise Closed()
        buf += d
    return bytes(buf)


def rfb_grab(port, timeout=10.0):
    """The framebuffer over RFB: (width, height, pixels as blue, green, red, x).

    The pointer is asked for as a shape of its own, so that it is not in
    the picture.
    """
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    try:
        version = _recvn(s, 12)
        if not version.startswith(b"RFB "):
            raise ValueError("not an RFB server: %r" % version)
        s.sendall(b"RFB 003.008\n")
        ntypes = _recvn(s, 1)[0]
        if ntypes == 0:
            n = struct.unpack("!I", _recvn(s, 4))[0]
            raise ValueError("refused: %s" % _recvn(s, n).decode("latin-1"))
        types = _recvn(s, ntypes)
        if 1 not in types:
            raise ValueError("the server wants authentication (types %r)" % list(types))
        s.sendall(b"\x01")
        if struct.unpack("!I", _recvn(s, 4))[0] != 0:
            raise ValueError("security handshake failed")
        s.sendall(b"\x01")              # shared
        w, h = struct.unpack("!HH", _recvn(s, 4))
        _recvn(s, 16)
        _recvn(s, struct.unpack("!I", _recvn(s, 4))[0])
        # 32 bits, depth 24, little endian, true colour, red 16 green 8 blue 0
        s.sendall(struct.pack("!BxxxBBBBHHHBBBxxx", 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
        s.sendall(struct.pack("!BxHii", 2, 2, 0, -239))     # raw, rich cursor
        s.sendall(struct.pack("!BBHHHH", 3, 0, 0, 0, w, h))
        fb = bytearray(w * h * 4)
        got = bytearray(w * h)
        while got.count(0):
            mtype = _recvn(s, 1)[0]
            if mtype == 0:
                nrects = struct.unpack("!xH", _recvn(s, 3))[0]
                for _ in range(nrects):
                    x, y, rw, rh, enc = struct.unpack("!HHHHi", _recvn(s, 12))
                    if enc == -239:
                        _recvn(s, rw * rh * 4 + ((rw + 7) // 8) * rh)
                        continue
                    if enc != 0:
                        raise ValueError("encoding %d was not asked for" % enc)
                    data = _recvn(s, rw * rh * 4)
                    for row in range(rh):
                        dst = ((y + row) * w + x) * 4
                        fb[dst:dst + rw * 4] = data[row * rw * 4:(row + 1) * rw * 4]
                        got[(y + row) * w + x:(y + row) * w + x + rw] = b"\x01" * rw
            elif mtype == 1:
                first, n = struct.unpack("!xHH", _recvn(s, 5))
                _recvn(s, n * 6)
            elif mtype == 2:
                pass
            elif mtype == 3:
                _recvn(s, struct.unpack("!xxxI", _recvn(s, 7))[0])
            else:
                raise ValueError("RFB message type %d" % mtype)
        return w, h, bytes(fb)
    finally:
        s.close()


class RfbViewer:
    """A viewer that stays connected and keeps asking for what has changed."""

    def __init__(self, port, timeout=10.0):
        s = self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        _recvn(s, 12)
        s.sendall(b"RFB 003.008\n")
        types = _recvn(s, _recvn(s, 1)[0])
        if 1 not in types:
            raise ValueError("the server wants authentication")
        s.sendall(b"\x01")
        if struct.unpack("!I", _recvn(s, 4))[0] != 0:
            raise ValueError("security handshake failed")
        s.sendall(b"\x01")
        self.w, self.h = struct.unpack("!HH", _recvn(s, 4))
        _recvn(s, 16)
        _recvn(s, struct.unpack("!I", _recvn(s, 4))[0])
        s.sendall(struct.pack("!BxxxBBBBHHHBBBxxx", 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
        s.sendall(struct.pack("!BxHi", 2, 1, 0))            # raw only: the pointer is drawn in
        self.updates = 0
        self.rects = 0
        self.closed = False
        self.request(False)

    def request(self, incremental=True):
        self.sock.sendall(struct.pack("!BBHHHH", 3, 1 if incremental else 0, 0, 0, self.w, self.h))

    def pump(self, seconds):
        """Take in updates for this long, asking for the next after each."""
        end = time.monotonic() + seconds
        while not self.closed:
            left = end - time.monotonic()
            if left <= 0:
                return
            self.sock.settimeout(left)
            try:
                mtype = self.sock.recv(1)
            except socket.timeout:
                return
            except OSError:
                mtype = b""
            if not mtype:
                self.closed = True
                return
            self.sock.settimeout(10)
            try:
                if mtype[0] == 0:
                    nrects = struct.unpack("!xH", _recvn(self.sock, 3))[0]
                    for _ in range(nrects):
                        x, y, rw, rh, enc = struct.unpack("!HHHHi", _recvn(self.sock, 12))
                        _recvn(self.sock, rw * rh * 4)
                        self.rects += 1
                    self.updates += 1
                    self.request(True)
                elif mtype[0] == 1:
                    first, n = struct.unpack("!xHH", _recvn(self.sock, 5))
                    _recvn(self.sock, n * 6)
                elif mtype[0] == 3:
                    _recvn(self.sock, struct.unpack("!xxxI", _recvn(self.sock, 7))[0])
            except (Closed, OSError):
                self.closed = True

    def pointer(self, x, y, buttons=0):
        self.sock.sendall(struct.pack("!BBHH", 5, buttons, x, y))

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def compare(win, fb, origin=None):
    """Where a window's picture differs from the framebuffer: a list of
    (x, y) window coordinates, at most 20.  Pixels no draw has reached count
    as different; the unused fourth byte does not count."""
    fw, fh, pixels = fb
    ox, oy = origin if origin else (win.x, win.y)
    bad = []
    for row in range(win.h):
        fy = oy + row
        if not 0 <= fy < fh:
            continue
        x0, x1 = max(0, -ox), min(win.w, fw - ox)
        if x1 <= x0:
            continue
        a = win.pixels[(row * win.w + x0) * 4:(row * win.w + x1) * 4]
        b = pixels[(fy * fw + ox + x0) * 4:(fy * fw + ox + x1) * 4]
        painted = win.painted[row * win.w + x0:row * win.w + x1]
        if a == b and 0 not in painted:
            continue
        for i in range(x1 - x0):
            if not painted[i] or a[i * 4:i * 4 + 3] != b[i * 4:i * 4 + 3]:
                bad.append((x0 + i, row))
                if len(bad) >= 20:
                    return bad
    return bad

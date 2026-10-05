#!/usr/bin/env python3
"""Minimal Chrome DevTools Protocol client (stdlib only).
  cdp.py list
  cdp.py <target-substr> eval '<js expression>'      (await'ed, result printed as JSON)
  cdp.py <target-substr> shot out.png
  cdp.py <target-substr> click 'visible text' | clickxy X Y
"""
import sys, json, socket, base64, os, struct, urllib.request, time

PORT = int(os.environ.get("CDP_PORT", "9223"))


class WS:
    def __init__(self, url):
        host, path = url[5:].split("/", 1)
        h, p = host.split(":")
        self.s = socket.create_connection((h, int(p)), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f"GET /{path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(4096)
        if b" 101 " not in buf.split(b"\r\n")[0]:
            raise RuntimeError(buf.decode(errors="replace"))
        self.buf = buf.split(b"\r\n\r\n", 1)[1]
        self.id = 0

    def _read(self, n):
        while len(self.buf) < n:
            d = self.s.recv(65536)
            if not d:
                raise EOFError
            self.buf += d
        r, self.buf = self.buf[:n], self.buf[n:]
        return r

    def send(self, data):
        d = data.encode()
        mask = os.urandom(4)
        n = len(d)
        hdr = b"\x81" + (bytes([0x80 | n]) if n < 126 else b"\xfe" + struct.pack(">H", n) if n < 65536 else b"\xff" + struct.pack(">Q", n))
        self.s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(d)))

    def recv(self):
        out = b""
        while True:
            b0, b1 = self._read(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            out += self._read(n)
            if b0 & 0x80:
                if b0 & 0x0F in (1, 2, 0):
                    return out.decode(errors="replace")
                out = b""

    def call(self, method, **params):
        self.id += 1
        self.send(json.dumps({"id": self.id, "method": method, "params": params}))
        while True:
            m = json.loads(self.recv())
            if m.get("id") == self.id:
                if "error" in m:
                    raise RuntimeError(m["error"])
                return m["result"]


def targets():
    return json.load(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json", timeout=5))


def ev(ws, js):
    r = ws.call("Runtime.evaluate", expression=js, awaitPromise=True, returnByValue=True)
    if "exceptionDetails" in r:
        return {"EXC": r["exceptionDetails"].get("exception", {}).get("description", r["exceptionDetails"])}
    return r["result"].get("value")


def main():
    if sys.argv[1] == "list":
        for t in targets():
            print(t["type"], "|", t["title"], "|", t["url"][:150])
        return
    sub, cmd = sys.argv[2 - 1], sys.argv[2]
    t = [t for t in targets() if sub in t["title"] or sub in t["url"]]
    if not t:
        sys.exit("no target " + sub)
    ws = WS(t[0]["webSocketDebuggerUrl"])
    if cmd == "eval":
        print(json.dumps(ev(ws, sys.argv[3]), indent=1, ensure_ascii=False))
    elif cmd == "shot":
        r = ws.call("Page.captureScreenshot", format="png")
        open(sys.argv[3], "wb").write(base64.b64decode(r["data"]))
        print("saved", sys.argv[3])
    elif cmd in ("click", "clickxy"):
        if cmd == "click":
            pos = ev(ws, """(()=>{const want=%s;const els=[...document.querySelectorAll('*')].filter(e=>{
              const r=e.getBoundingClientRect();if(!r.width||!r.height)return false;
              const own=[...e.childNodes].filter(n=>n.nodeType==3).map(n=>n.textContent.trim()).join(' ');
              return own===want||e.getAttribute('data-test-id')===want||e.getAttribute('aria-label')===want});
              if(!els.length)return null;const i=%d;const r=els[Math.min(i,els.length-1)].getBoundingClientRect();
              return {x:r.x+r.width/2,y:r.y+r.height/2,n:els.length}})()""" % (json.dumps(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else 0))
            if not pos:
                sys.exit("text not found: " + sys.argv[3])
            x, y = pos["x"], pos["y"]
        else:
            x, y = float(sys.argv[3]), float(sys.argv[4])
        ws.call("Input.dispatchMouseEvent", type="mouseMoved", x=x, y=y)
        ws.call("Input.dispatchMouseEvent", type="mousePressed", x=x, y=y, button="left", clickCount=1)
        ws.call("Input.dispatchMouseEvent", type="mouseReleased", x=x, y=y, button="left", clickCount=1)
        print("clicked", x, y)
        time.sleep(0.3)


main()

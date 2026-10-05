import socket, ssl, base64, os, sys, time, struct, threading, json
def listen(port, path, dur, tag):
    end = time.time() + dur
    while time.time() < end:
        try:
            s = ssl._create_unverified_context().wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=2))
            break
        except Exception: time.sleep(0.1)
    else: print(tag, 'no connect'); return
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    buf = b''
    s.settimeout(1)
    def rn(n):
        nonlocal buf
        while len(buf) < n:
            if time.time() > end: raise TimeoutError
            try: d = s.recv(65536)
            except socket.timeout: continue
            if not d: raise EOFError
            buf += d
        r, buf = buf[:n], buf[n:]; return r
    try:
        while b'\r\n\r\n' not in buf: buf += s.recv(4096)
        print(tag, 'connected', buf.split(b'\r\n')[0], flush=True); buf = buf.split(b'\r\n\r\n', 1)[1]
        msg = b''
        while True:
            b0, b1 = rn(2); n = b1 & 0x7f
            if n == 126: n = struct.unpack('>H', rn(2))[0]
            elif n == 127: n = struct.unpack('>Q', rn(8))[0]
            p = rn(n); op = b0 & 0xf
            if op == 9: s.sendall(b'\x8a\x80' + os.urandom(4)); continue
            if op in (0, 1, 2):
                msg += p
                if b0 & 0x80:
                    try: ev = json.loads(msg).get('event')
                    except Exception as e: ev = 'BADJSON ' + repr(msg[:80])
                    print('%s %.1f len=%d fin_frames op=%d event=%s' % (tag, time.time() % 100, len(msg), op, ev), flush=True); msg = b''
            else: print(tag, 'op', op, p[:50], flush=True)
    except Exception as e: print(tag, 'end', type(e).__name__, flush=True)
dur = float(sys.argv[1])
ts = [threading.Thread(target=listen, args=(6327, '/eventing', dur, 'GGEZ ')), threading.Thread(target=listen, args=(6329, '/eventing', dur, 'OLDGG'))]
[t.start() for t in ts]; [t.join() for t in ts]

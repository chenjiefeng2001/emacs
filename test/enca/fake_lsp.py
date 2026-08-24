#!/usr/bin/env python3
"""EVS-5.2.6 fake LSP server: deterministic completion backend with
configurable think-time.  Speaks enough LSP over stdio for the bench:
initialize / initialized / didOpen / didChange / completion.

Env:
  FAKE_DELAY_MS   artificial backend latency per completion (default 90)
  FAKE_ITEMS      number of candidates (default 50)
"""
import json
import os
import sys
import threading
import time

DELAY = float(os.environ.get("FAKE_DELAY_MS", "90")) / 1000.0
ITEMS = int(os.environ.get("FAKE_ITEMS", "50"))

write_lock = threading.Lock()


def send(msg):
    data = json.dumps(msg, separators=(",", ":")).encode()
    with write_lock:
        sys.stdout.buffer.write(
            b"Content-Length: %d\r\n\r\n" % len(data) + data)
        sys.stdout.buffer.flush()


def read_messages():
    """Robust LSP framing: readline() headers, then exact-length body.
    (A bulk read(4096) would block forever on small frames.)"""
    stdin = sys.stdin.buffer
    while True:
        headers = {}
        line = stdin.readline()
        if not line:
            return
        while line not in (b"\r\n", b"\n"):
            k, _, v = line.partition(b":")
            headers[k.strip().lower()] = v.strip()
            line = stdin.readline()
            if not line:
                return
        clen = int(headers.get(b"content-length", b"0"))
        body = b""
        while len(body) < clen:
            chunk = stdin.read(clen - len(body))
            if not chunk:
                return
            body += chunk
        yield json.loads(body)


def candidates(prefix):
    base = "print printf println private printer principle".split()
    out = []
    for i in range(ITEMS):
        lab = "%s_%03d" % (base[i % len(base)] + prefix, i)
        out.append({"label": lab, "kind": 3,
                    "detail": "fake candidate"})
    return {"isIncomplete": False, "items": out}


def main():
    def log(m):
        with open("/tmp/fake_lsp.log", "a") as f:
            f.write(m + "\n")

    log(f"START pid={os.getpid()} delay={DELAY} items={ITEMS}")
    pending = {}

    def delayed_reply(msg_id, params):
        time.sleep(DELAY)
        prefix = ""
        try:
            pos = params["position"]["character"]
            prefix = "p%d" % (pos % 97)
        except Exception:
            pass
        send({"jsonrpc": "2.0", "id": msg_id,
              "result": candidates(prefix)})
        log(f"REPLIED id={msg_id}")

    try:
        for msg in read_messages():
            method = msg.get("method", "")
            log(f"RECV {method} id={msg.get('id')}")
            if method == "initialize":
                send({"jsonrpc": "2.0", "id": msg["id"],
                      "result": {"capabilities": {
                          "completionProvider": {
                              "triggerCharacters": ["."]}}}})
            elif method == "textDocument/completion":
                mid = msg["id"]
                t = threading.Thread(target=delayed_reply,
                                     args=(mid, msg.get("params", {})))
                t.start()
                pending[mid] = t
            elif method == "$/cancelRequest":
                pass
    except Exception as e:
        log(f"DIED {e!r}")
        raise
    finally:
        log("EOF")


if __name__ == "__main__":
    main()

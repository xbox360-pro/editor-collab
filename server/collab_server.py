#!/usr/bin/env python3
"""
Editor Collab relay server.

Zero dependencies - just Python 3.8+.

    python3 collab_server.py               # listens on 0.0.0.0:8484
    python3 collab_server.py --port 9000

Everyone in a room points the mod's "Server URL" setting at this server.
Rooms live in memory; a room is deleted after it has been empty for a while.
"""

import argparse
import json
import random
import string
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOG_LIMIT = 50_000          # ops kept per room before clients get a full resync instead
PEER_TIMEOUT = 15           # seconds without a sync before a peer is considered gone
ROOM_TIMEOUT = 60 * 30      # empty rooms are dropped after this many seconds
MAX_BODY = 64 * 1024 * 1024 # 64 MB, big levels are big

lock = threading.Lock()
rooms = {}
LAN_ADDRESS = ""  # filled in at startup, e.g. "192.168.1.20:8484"


def detect_lan_ip():
    """Best-guess LAN IP of this machine (no packets are actually sent)."""
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


def code(n=6):
    alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  # no 0/O/1/I confusion
    return "".join(random.choice(alphabet) for _ in range(n))


class Room:
    def __init__(self, room_id):
        self.id = room_id
        self.objects = {}   # object id -> save string
        self.log = []       # list of (seq, client, op)
        self.seq = 0
        self.clients = {}   # client id -> {name, x, y, seen}
        self.last_active = time.time()

    def add_client(self, name):
        cid = code(8)
        while cid in self.clients:
            cid = code(8)
        self.clients[cid] = {"name": str(name)[:32] or "Player", "x": 0.0, "y": 0.0, "seen": time.time()}
        self.last_active = time.time()
        return cid

    def apply(self, client, ops):
        for op in ops:
            t = op.get("t")
            oid = op.get("id")
            if not isinstance(oid, str) or not oid:
                continue
            if t in ("add", "upd"):
                s = op.get("s")
                if not isinstance(s, str):
                    continue
                self.objects[oid] = s
                entry = {"t": t, "id": oid, "s": s}
            elif t == "del":
                self.objects.pop(oid, None)
                entry = {"t": "del", "id": oid}
            else:
                continue
            self.seq += 1
            self.log.append((self.seq, client, entry))
        if len(self.log) > LOG_LIMIT:
            self.log = self.log[-LOG_LIMIT:]

    def ops_since(self, since, client):
        """Returns (ops, needs_resync)."""
        if since >= self.seq:
            return [], False
        oldest = self.log[0][0] if self.log else self.seq + 1
        if since + 1 < oldest:
            return None, True
        # log is sorted by seq, so binary-search the start
        lo, hi = 0, len(self.log)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.log[mid][0] <= since:
                lo = mid + 1
            else:
                hi = mid
        return [op for (_, c, op) in self.log[lo:] if c != client], False

    def peers(self, exclude):
        now = time.time()
        return [
            {"id": cid, "name": c["name"], "x": c["x"], "y": c["y"]}
            for cid, c in self.clients.items()
            if cid != exclude and now - c["seen"] < PEER_TIMEOUT
        ]


def cleanup_loop():
    while True:
        time.sleep(30)
        now = time.time()
        with lock:
            for rid in list(rooms):
                room = rooms[rid]
                for cid in list(room.clients):
                    if now - room.clients[cid]["seen"] > PEER_TIMEOUT * 4:
                        del room.clients[cid]
                if not room.clients and now - room.last_active > ROOM_TIMEOUT:
                    del rooms[rid]
                    print(f"[room {rid}] closed (inactive)")


class Handler(BaseHTTPRequestHandler):
    server_version = "EditorCollab/1.0"

    def log_message(self, fmt, *args):
        pass  # keep the console quiet; we print our own events

    def reply(self, status, payload):
        body = json.dumps(payload, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/", "/health"):
            with lock:
                self.reply(200, {"ok": True, "rooms": len(rooms)})
        else:
            self.reply(404, {"error": "Not found"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_BODY:
            return self.reply(413, {"error": "Request too large"})
        try:
            data = json.loads(self.rfile.read(length) or b"{}")
            if not isinstance(data, dict):
                raise ValueError
        except Exception:
            return self.reply(400, {"error": "Bad JSON"})

        route = {
            "/create": self.create,
            "/join": self.join,
            "/sync": self.sync,
            "/leave": self.leave,
        }.get(self.path)
        if not route:
            return self.reply(404, {"error": "Not found"})
        with lock:
            status, payload = route(data)
        self.reply(status, payload)

    # --- routes (called with the lock held) ---

    def create(self, data):
        rid = code()
        while rid in rooms:
            rid = code()
        room = Room(rid)
        rooms[rid] = room
        cid = room.add_client(data.get("name", "Player"))
        print(f"[room {rid}] created by {room.clients[cid]['name']}")
        return 200, {"room": rid, "client": cid, "seq": room.seq, "lan": LAN_ADDRESS}

    def join(self, data):
        rid = str(data.get("room", "")).upper().strip()
        room = rooms.get(rid)
        if not room:
            return 404, {"error": "Room not found - double check the code"}
        cid = room.add_client(data.get("name", "Player"))
        print(f"[room {rid}] {room.clients[cid]['name']} joined ({len(room.objects)} objects)")
        return 200, {"room": rid, "client": cid, "seq": room.seq, "objects": room.objects, "lan": LAN_ADDRESS}

    def sync(self, data):
        room = rooms.get(str(data.get("room", "")))
        if not room:
            return 404, {"error": "Room not found"}
        cid = str(data.get("client", ""))
        client = room.clients.get(cid)
        if not client:
            # server restarted or we timed out - let them back in under the same id
            room.clients[cid] = client = {"name": "Player", "x": 0.0, "y": 0.0, "seen": time.time()}

        client["seen"] = time.time()
        room.last_active = time.time()
        try:
            client["x"] = float(data.get("x", 0))
            client["y"] = float(data.get("y", 0))
        except (TypeError, ValueError):
            pass

        try:
            since = int(data.get("since", 0))
        except (TypeError, ValueError):
            since = 0

        # Work out what they're missing *before* applying their ops, so seq
        # numbers from their own batch don't hide other people's changes.
        incoming, resync = room.ops_since(since, cid)

        ops = data.get("ops")
        if isinstance(ops, list) and ops:
            room.apply(cid, [o for o in ops if isinstance(o, dict)])

        payload = {"seq": room.seq, "peers": room.peers(cid)}
        if resync:
            payload["resync"] = True
            payload["objects"] = room.objects
        else:
            payload["ops"] = incoming
        return 200, payload

    def leave(self, data):
        room = rooms.get(str(data.get("room", "")))
        if room:
            c = room.clients.pop(str(data.get("client", "")), None)
            if c:
                print(f"[room {room.id}] {c['name']} left")
        return 200, {"ok": True}


def main():
    ap = argparse.ArgumentParser(description="Editor Collab relay server")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8484)
    args = ap.parse_args()

    global LAN_ADDRESS
    LAN_ADDRESS = f"{detect_lan_ip()}:{args.port}"

    threading.Thread(target=cleanup_loop, daemon=True).start()
    try:
        srv = ThreadingHTTPServer((args.host, args.port), Handler)
    except OSError as e:
        print(f"\n  Couldn't start on port {args.port}: {e}")
        print("  Is the server already running in another window?\n")
        return

    print()
    print("  ==============================================")
    print("   Editor Collab server is running!")
    print("  ==============================================")
    print()
    print(f"   On this PC, the server box in GD should say:  127.0.0.1:{args.port}")
    print(f"   Friends on your Wi-Fi will connect to:         {LAN_ADDRESS}")
    print( "   (Invites you copy in-game already include this.)")
    print()
    print( "   Friends outside your Wi-Fi? See README > Playing over the internet.")
    print( "   Keep this window open while you build. Close it to stop the server.")
    print()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()

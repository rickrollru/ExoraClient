#!/usr/bin/env python3
"""Exora Client cosmetics service.

Clients tell which DDNet server they play on and under which player number, and which cosmetics set they wear
(by its hash). Other clients on the same server ask who wears what and fetch the sets they do not know yet.

- A set belongs to a random client id; the client proves it owns the id with a secret (only its hash is stored).
- Where a client plays is kept in memory only and forgotten after PRESENCE_TTL seconds.
- Sets nobody has worn for KEEP_DAYS days are deleted.
Only the Python standard library is used.
"""

import hashlib
import json
import os
import re
import sqlite3
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PORT = int(os.environ.get("EXORA_COSMETICS_PORT", "8770"))
DB_PATH = os.environ.get("EXORA_COSMETICS_DB", "/var/lib/exora-cosmetics/cosmetics.db")
PRESENCE_TTL = 30.0
KEEP_DAYS = 30
MAX_BODY = 8192
MAX_BUNDLE = 4096
RATE_WINDOW = 10.0
RATE_LIMIT = 40  # requests per window and address
ONLINE_TTL = 75.0  # a client counts as online this long after its last beat
NEW_IDS_PER_HOUR = 10  # new client ids one address may make in an hour (no filling the disk with made-up ids)
MAX_CLIENTS = 500000  # all ids together, then new ones wait until the old ones are cleaned up
MAX_CONNECTIONS = 200  # connections handled at once; more get "busy"
SOCKET_TIMEOUT = 10  # seconds a connection may take, slow ones are dropped

ID_RE = re.compile(r"^[0-9a-f]{16}$")
SECRET_RE = re.compile(r"^[0-9a-f]{32}$")
HASH_RE = re.compile(r"^[0-9a-f]{8,64}$")
SERVER_RE = re.compile(r"^[0-9a-fA-F.:\[\]]{3,64}$")

lock = threading.Lock()
presence = {}  # server address -> {client id: (player number, name, set hash, time)}
rates = {}  # remote address -> [window start, count]
seen = {}  # client id -> time of the last beat: how many play now, in memory only
new_ids = {}  # remote address -> [hour start, new ids]
slots = threading.BoundedSemaphore(MAX_CONNECTIONS)


def db():
	conn = sqlite3.connect(DB_PATH, timeout=10)
	conn.execute("CREATE TABLE IF NOT EXISTS clients (id TEXT PRIMARY KEY, secret TEXT NOT NULL, hash TEXT, seen REAL NOT NULL)")
	conn.execute("CREATE TABLE IF NOT EXISTS bundles (hash TEXT PRIMARY KEY, data TEXT NOT NULL, seen REAL NOT NULL)")
	return conn


def cleanup_loop():
	while True:
		time.sleep(3600)
		limit = time.time() - KEEP_DAYS * 86400
		try:
			with db() as conn:
				conn.execute("DELETE FROM clients WHERE seen < ?", (limit,))
				conn.execute("DELETE FROM bundles WHERE seen < ? AND hash NOT IN (SELECT hash FROM clients WHERE hash IS NOT NULL)", (limit,))
		except sqlite3.Error:
			pass


class Handler(BaseHTTPRequestHandler):
	server_version = "ExoraCosmetics/1"
	timeout = SOCKET_TIMEOUT

	def handle(self):
		if not slots.acquire(blocking=False):
			return  # too many at once: drop, the client tries again later
		try:
			super().handle()
		finally:
			slots.release()

	def log_message(self, fmt, *args):
		pass  # no access log: nothing about the players is written to the disk

	def reply(self, code, obj=None, text=None):
		body = (json.dumps(obj, separators=(",", ":")) if obj is not None else (text or "")).encode()
		self.send_response(code)
		self.send_header("Content-Type", "application/json" if obj is not None else "text/plain; charset=utf-8")
		self.send_header("Content-Length", str(len(body)))
		self.end_headers()
		self.wfile.write(body)

	def limited(self):
		now = time.time()
		addr = self.client_address[0]
		with lock:
			window = rates.get(addr)
			if not window or now - window[0] > RATE_WINDOW:
				rates[addr] = [now, 1]
				if len(rates) > 100000:
					rates.clear()
				return False
			window[1] += 1
			return window[1] > RATE_LIMIT

	def do_GET(self):
		if self.limited():
			return self.reply(429, {"error": "slow down"})
		url = urlparse(self.path)
		query = parse_qs(url.query)
		if url.path == "/v1/server":
			addr = (query.get("addr") or [""])[0]
			if not SERVER_RE.match(addr):
				return self.reply(400, {"error": "bad address"})
			now = time.time()
			with lock:
				players = presence.get(addr, {})
				out = [{"cid": p[0], "name": p[1], "hash": p[2]} for p in players.values() if now - p[3] < PRESENCE_TTL]
			return self.reply(200, {"players": out})
		if url.path == "/v1/servers":
			# where Exora players are now: {server key: [names]} (the browser filter "Exora")
			now = time.time()
			with lock:
				out = {}
				for addr, players in presence.items():
					names = [p[1] for p in players.values() if now - p[3] < PRESENCE_TTL]
					if names:
						out[addr] = names[:64]
			return self.reply(200, {"servers": out})
		if url.path == "/v1/bundle":
			h = (query.get("hash") or [""])[0]
			if not HASH_RE.match(h):
				return self.reply(400, {"error": "bad hash"})
			with db() as conn:
				row = conn.execute("SELECT data FROM bundles WHERE hash = ?", (h,)).fetchone()
			if not row:
				return self.reply(404, {"error": "unknown"})
			return self.reply(200, {"hash": h, "data": row[0]})
		if url.path == "/v1/ping":
			return self.reply(200, {"ok": True})
		if url.path == "/v1/stats":
			now = time.time()
			with lock:
				online = sum(1 for t in seen.values() if now - t < ONLINE_TTL)
			return self.reply(200, {"online": online})
		return self.reply(404, {"error": "not found"})

	def do_POST(self):
		if self.limited():
			return self.reply(429, {"error": "slow down"})
		if urlparse(self.path).path != "/v1/beat":
			return self.reply(404, {"error": "not found"})
		length = int(self.headers.get("Content-Length") or 0)
		if length <= 0 or length > MAX_BODY:
			return self.reply(413, {"error": "too big"})
		try:
			msg = json.loads(self.rfile.read(length))
			cid_id = str(msg["id"])
			secret = str(msg["secret"])
			addr = str(msg.get("server") or "")
			player = int(msg.get("cid", -1))
			name = str(msg.get("name") or "")[:16]
			h = str(msg.get("hash") or "")
			bundle = msg.get("bundle")
		except (ValueError, KeyError, TypeError):
			return self.reply(400, {"error": "bad request"})
		if not ID_RE.match(cid_id) or not SECRET_RE.match(secret) or (h and not HASH_RE.match(h)):
			return self.reply(400, {"error": "bad id"})
		if bundle is not None and (not isinstance(bundle, str) or len(bundle) > MAX_BUNDLE):
			return self.reply(400, {"error": "bad bundle"})
		secret_hash = hashlib.sha256(secret.encode()).hexdigest()
		now = time.time()
		need = False
		with db() as conn:
			row = conn.execute("SELECT secret FROM clients WHERE id = ?", (cid_id,)).fetchone()
			if row and row[0] != secret_hash:
				return self.reply(403, {"error": "not yours"})
			if not row:
				# a new id: only a few per address and hour, and not past the limit of all ids
				addr_ip = self.client_address[0]
				with lock:
					entry = new_ids.get(addr_ip)
					if not entry or now - entry[0] > 3600:
						entry = new_ids[addr_ip] = [now, 0]
					if entry[1] >= NEW_IDS_PER_HOUR:
						return self.reply(429, {"error": "too many new ids"})
					entry[1] += 1
					if len(new_ids) > 100000:
						new_ids.clear()
				if conn.execute("SELECT COUNT(*) FROM clients").fetchone()[0] >= MAX_CLIENTS:
					return self.reply(503, {"error": "full"})
			if bundle is not None and h:
				if hashlib.sha256(bundle.encode()).hexdigest()[: len(h)] != h:
					return self.reply(400, {"error": "hash does not match"})
				conn.execute("INSERT OR REPLACE INTO bundles (hash, data, seen) VALUES (?, ?, ?)", (h, bundle, now))
			if h:
				known = conn.execute("SELECT 1 FROM bundles WHERE hash = ?", (h,)).fetchone()
				need = known is None
				if known:
					conn.execute("UPDATE bundles SET seen = ? WHERE hash = ?", (now, h))
			conn.execute("INSERT OR REPLACE INTO clients (id, secret, hash, seen) VALUES (?, ?, ?, ?)", (cid_id, secret_hash, h or None, now))
		with lock:
			seen[cid_id] = now
			if len(seen) > 200000:
				for key in [k for k, t in seen.items() if now - t >= ONLINE_TTL]:
					del seen[key]
			for players in presence.values():
				players.pop(cid_id, None)
			if SERVER_RE.match(addr) and 0 <= player < 128 and h and not need:
				presence.setdefault(addr, {})[cid_id] = (player, name, h, now)
			# forget servers nobody plays on any more
			if len(presence) > 1000:
				for key in [k for k, v in presence.items() if all(now - p[3] >= PRESENCE_TTL for p in v.values())]:
					del presence[key]
		return self.reply(200, {"ok": True, "need_bundle": need})


def main():
	os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
	db().close()
	threading.Thread(target=cleanup_loop, daemon=True).start()
	httpd = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
	print(f"exora cosmetics on port {PORT}", flush=True)
	httpd.serve_forever()


if __name__ == "__main__":
	main()

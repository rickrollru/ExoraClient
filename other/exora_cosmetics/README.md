# Exora Client cosmetics service

A small HTTP service that lets Exora Client players see each other's cosmetics (cape, accessory, scarf, tee shader,
neon outline). Only the Python 3 standard library is used.

- A cosmetics set belongs to a random client id; the client proves the id with a secret, only its hash is stored.
- Where a client plays (DDNet server address and player number) is kept in memory only, for 30 seconds.
- Sets nobody wore for 30 days are deleted. Nothing about players is logged.

API (port 8770 by default):

| Request | What it does |
|---|---|
| `POST /v1/beat` `{id, secret, server, cid, name, hash, bundle?}` | "I play here wearing this set"; answers `need_bundle` when the set is unknown |
| `GET /v1/server?addr=ip:port` | who wears what on a DDNet server: `[{cid, name, hash}]` |
| `GET /v1/servers` | every server with Exora players now: `{key: [names]}` (the browser filter) |
| `GET /v1/bundle?hash=...` | a set by its hash |
| `GET /v1/ping` | health check |

Install on a Linux server with systemd: run `deploy.ps1` on Windows (it finds a working ssh key or asks for the
password), or copy `server.py` to `/opt/exora-cosmetics/` and `exora-cosmetics.service` to `/etc/systemd/system/`
and run `systemctl enable --now exora-cosmetics`.

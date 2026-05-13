# API Plan v1 — Sync + SQLite

Date: 2026-05-13

Purpose
- Capture the concrete design, schema, and API plan for moving CSV-backed storage to a local SQLite store with an optional sync service that replicates changes to a remote database in real time and on-demand.

Goals and constraints
- Primary workload: mostly appends (observations recorded while flying).
- Occasional local edits; rare concurrent edits between clients.
- Support offline-first operation with durable local buffering of outbound and inbound events.
- Provide deterministic conflict resolution and idempotent application of events.
- Keep integration surface small so `AppState` changes are minimal.

High-level architecture
- App → `IPointStore` (local) → SQLite DB (`geoscout.db`).
- If sync enabled: App → `SyncService` → Remote sync service (HTTP + WebSocket) → remote DB (Postgres or similar).
- Local DB contains both the current materialized state (`points`) and an append-only change log (`change_events`). The change log acts as outbox/inbox for sync.

Data model
- Reuse `DataPoint` from `scout_core.h` (fields: `recordid`/`id`, `server`, `x`, `y`, `z`, `planet`, `material`, `location`, `quality_min`, `quality_max`, `note`, `poi_type`, `time_info`).

SQLite schema (suggested)
- `points` (current materialized state):
  ```sql
  CREATE TABLE IF NOT EXISTS points (
    recordid INTEGER PRIMARY KEY,
    server TEXT,
    x REAL, y REAL, z REAL,
    planet TEXT, material TEXT,
    location INTEGER,
    quality_min INTEGER, quality_max INTEGER,
    note TEXT,
    poi_type INTEGER,
    poi_time TEXT,
    last_modified_ts INTEGER,
    last_modified_node TEXT
  );
  PRAGMA journal_mode=WAL; -- enable WAL for better concurrency
  ```

- `change_events` (append-only event log, durable outbox/inbox):
  ```sql
  CREATE TABLE IF NOT EXISTS change_events (
    change_id TEXT PRIMARY KEY,    -- uuid v4
    node_id TEXT,                  -- origin client uuid
    seq INTEGER,                   -- local sequence (optional)
    created_ts INTEGER,            -- unix ms
    op TEXT,                       -- "insert"|"update"|"delete" (or "upsert")
    recordid INTEGER,              -- nullable target id
    payload TEXT,                  -- JSON encoded full DataPoint or diff
    applied_ts INTEGER             -- when applied locally (nullable)
  );
  CREATE INDEX IF NOT EXISTS idx_change_events_created ON change_events(created_ts);
  ```

- `sync_checkpoint` (last remote checkpoints / resume):
  ```sql
  CREATE TABLE IF NOT EXISTS sync_checkpoint (
    node_id TEXT PRIMARY KEY,
    last_remote_ts INTEGER,
    last_remote_change_id TEXT
  );
  ```

Why an event table (SQL event store)?
- Append-only events make sync robust: durable outbox, replay, deterministic ordering, and idempotent application. It simplifies handling flakey networks and replays for auditing.

Identifiers and GUIDs
- Each client installation has a `node_id` (UUID v4) stored in local settings. Generated on first run.
- Each local change creates `change_id` (UUID v4).
- Keep original numeric `recordid` values from CSV when migrating; `change_events` will reference those recordids.

ChangeEvent JSON payload (example)
```json
{
  "change_id":"...",
  "node_id":"...",
  "created_ts":1680000000000,
  "op":"upsert",
  "recordid":123,
  "payload":{ /* DataPoint as JSON */ }
}
```

Sync protocol (high level)
- Transport: WebSocket for RT streaming; HTTP for polling / bulk fetch / admin endpoints.
- Messages (JSON):
  - `hello` {node_id, last_known_remote_ts}
  - `event` {change_id, node_id, created_ts, op, recordid, payload}
  - `ack` {change_id}
  - `checkpoint` {node_id, last_change_id, last_ts}
  - `request_full_sync` -- to request a bulk snapshot when catching up

Connection flow
1. Client connects via WebSocket, sends `hello` with `node_id` and local checkpoint.
2. Server streams missed remote events since checkpoint; client persists them to `change_events` (inbox), then applies them to `points` in order.
3. Client streams its pending outbox events (unsent `change_events`) to server; server responds with `ack` when persisted.
4. Server may also request a full sync if divergence detected; client can respond via HTTP snapshot or server-provided snapshot.

Conflict resolution
- Apply events in deterministic order: sort by `(created_ts, node_id, change_id)` lexicographically.
- If an event `change_id` already exists in `change_events` (duplicate), skip.
- When applying an upsert: compare row `last_modified_ts` with event `created_ts`. If local newer, skip; otherwise apply. Tie-breaker: compare `node_id` then `change_id`.
- Deletes are applied as idempotent deletes.

Buffering and overflow handling
- Persist incoming RT messages to `change_events` immediately (guarantees durability).
- Define configuration: `sync.max_outbox_size` and `sync.max_inbox_size` (count or bytes).
- Overflow strategies (ordered by preference):
  1. Backpressure: pause accepting new RT until local process drains; respond with flow-control messages to server.
  2. Swap to archival file: compress old events to disk archive (safe but complex).
  3. Reject new events (not recommended) or drop oldest (dangerous).

Recommendation: implement durable outbox/inbox with configurable retention and backpressure; do not drop events silently.

Migration (CSV → SQLite)
1. Create DB and schema.
2. Read existing `geoscout.csv` using existing `load_points(csv_path)`. Preserve numeric `id`.
3. For each `DataPoint` create a `change_event` with `op=upsert`, `payload`=full DataPoint JSON, `node_id` set to local `node_id` (or `migration`), `created_ts` set to original time or migration time.
4. Insert events and mark them as applied locally (applied_ts = now) after inserting into `points`.

Suggested C++ API (sketch)

`point_store.h` (interface)
```cpp
#pragma once
#include <vector>
#include <string>
#include <optional>
#include "scout_core.h"

struct ChangeEvent {
  std::string change_id;
  std::string node_id;
  int64_t created_ts;
  std::string op; // "upsert"|"delete"
  std::optional<int> recordid;
  std::string payload_json;
  std::optional<int64_t> applied_ts;
};

struct IPointStore {
  virtual ~IPointStore() = default;
  virtual std::vector<DataPoint> load_points() = 0;
  virtual bool append_point(const DataPoint& p, std::string* out_change_id = nullptr) = 0;
  virtual bool overwrite_points(const std::vector<DataPoint>& points) = 0;
  virtual bool push_change_event(const ChangeEvent& ev) = 0; // persist event
};
```

`point_store_sqlite` (notes)
- Implements `IPointStore`.
- On append: run a transaction that inserts/updates `points` and inserts a `change_events` row (applied_ts = NULL for outbox unless we mark applied immediately).
- Use prepared statements + parameter binding for performance.

`sync_service.h` (skeleton)
```cpp
struct ISyncService {
  virtual ~ISyncService() = default;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void notify_new_local_event(const ChangeEvent& ev) = 0; // optional hook
};
```

Integration with `AppState`
- Replace direct CSV calls with a `std::unique_ptr<IPointStore> store;` member in `AppState`.
- On startup choose implementation based on config: CSV-adapter (legacy) or `SqlitePointStore`.
- On append (user adds a point) call `store->append_point(point)` which persists state and creates an outbox event.
- If `sync.enabled`, create `SyncService` instance and `sync_service->start()`; it will read unsent outbox events from DB and stream them.

Threading and concurrency
- Use SQLite `WAL` mode and keep transactions short.
- SyncService runs in its own background thread(s); it must use the store's thread-safe methods or open its own DB connection to the same file (preferred: one DB connection per thread with WAL enabled).

Testing and rollout
1. Add schema and `SqlitePointStore` unit tests: create DB, migrate a small CSV, assert points and events exist.
2. Smoke test: run app with `sync.enabled=false` using new store.
3. Enable `sync.enabled=true` in an environment with a local echo server to validate WebSocket message flow and ack handling.
4. Monitor `change_events` size and retention; tune `sync.max_*` settings.

Next steps (implementation plan)
- Create `point_store.h` + `point_store_sqlite.h/.cpp` (store + schema).
- Scaffold `SyncService` with WebSocket client and message handlers.
- Add migration binary or an in-app migration path.
- Wire `AppState` to use `IPointStore`.

References
- `scout_core.h` — `DataPoint` structure used as canonical shape.
- Existing CSV helpers: `load_points`, `append_point` (can be reused inside migration).

Appendix: Example event application logic (pseudocode)
```text
insert_or_update_event(ev):
  if change_id already applied: return
  if op == delete: delete row with recordid
  else: // upsert
    row = select points where recordid = ev.recordid
    if row exists:
      if (row.last_modified_ts, row.last_modified_node) > (ev.created_ts, ev.node_id):
         ignore // local row is newer
    write row from payload
  mark change_id applied (set applied_ts)
```

-- end of plan v1 --

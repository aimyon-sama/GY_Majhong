# GY Mahjong

[简体中文](README.zh-CN.md)

An online Mahjong game using a client/server architecture. The first target is a single C++ authoritative server that supports about 200 concurrent players, a Web frontend styled after Tenhou, custom rules, and file-based replay/score export without persistent player accounts.

## Goals

- Support around 200 concurrent players, roughly 50 active tables.
- Keep the server authoritative for all game state and legal actions.
- Isolate custom Mahjong rules from networking, storage, and UI.
- Avoid a player database in the first version.
- Export replay logs and final score summaries after each game.
- Build the frontend as a browser client over WebSocket.

## Architecture

```text
frontend
  |
  | WebSocket JSON messages
  v
backend C++ server
  |
  | owns all authoritative state
  v
core game engine
  |
  | emits replay events and score summaries
  v
file exports
```

The backend should treat every client message as a request, not as truth. The frontend only renders snapshots, events, prompts, and legal actions sent by the server.

## Directory Layout

```text
backend/
  app/                    Server executable entry points.
  include/                Public C++ headers shared across backend modules.
  src/
    core/
      tile/               Tile model, tile parsing, wall generation, tile utilities.
      state/              GameState, TableState, PlayerState, turn state.
      rules/              Custom rules, legal action discovery, win checks.
      score/              Score calculation and settlement.
    server/
      gateway/            WebSocket acceptor, message routing, heartbeat.
      lobby/              Room creation, join, ready, matchmaking if needed.
      session/            Temporary player identity, reconnect, connection binding.
      table/              Table actors/state machines and timeout handling.
    storage/
      replay/             Replay writer, score export, crash-safe file flushing.
  tests/                  Backend unit and integration tests.

frontend/
  public/                 Static files served by the frontend dev/build tool.
  src/
    assets/               Tile images, sounds, fonts, and visual resources.
    components/           Reusable UI controls and table widgets.
    net/                  WebSocket client, protocol encoding/decoding.
    state/                Client-side view model derived from server snapshots.
    views/                Lobby, table, replay, and result screens.

protocol/                 Versioned client/server message schemas.
replay/                   Exported replay files during local development.
samples/                  Sample replays, score files, and protocol fixtures.
docs/                     Design notes and rule documentation.
deploy/                   Deployment configs.
scripts/                  Build, test, export, and maintenance scripts.
```

## Backend Layers

### Core

The core layer should not depend on networking or filesystem code. It should be deterministic and testable from command-line tests.

Key responsibilities:

- Tile representation.
- Wall generation and draw order.
- Round-state based legal action discovery.
- Custom win checks and claim resolution.
- Score settlement and tenpai-derived exhaustive-draw scoring.
- Replay event generation.

Implemented core modules:

- `GameEngine`: checks tile-shape rules such as pon, kan, tsumo, ron, tenpai, and multi-ron configuration.
- `PointEngine`: calculates custom point deltas from round results, chicken tiles, kan payments, and tenpai state.
- `RuleEngine`: combines the current `RoundState` with game and point rules to produce legal player actions, resolve competing claims, and calculate final point results.

### Server

The server layer owns connections, rooms, sessions, table lifecycles, and timers.

Key responsibilities:

- Accept WebSocket clients.
- Create and join rooms.
- Bind temporary player tokens to seats.
- Route player commands to the correct table.
- Send state snapshots and event broadcasts.
- Handle disconnect, reconnect, heartbeat, and timeout behavior.

### Storage

Storage remains file-based for the initial version.

Key responsibilities:

- Append replay events as the game progresses.
- Export final score summaries.
- Use atomic write or temp-file rename for completed artifacts.
- Keep enough event data to replay or diagnose an interrupted game.

## Protocol Direction

Use JSON WebSocket messages first. This keeps debugging simple while the game rules are still changing. Binary protocol can wait until there is a measured reason.

Client command example:

```json
{
  "type": "discard",
  "roomId": "room-001",
  "tableId": "table-001",
  "seq": 42,
  "tile": "5m"
}
```

Server event example:

```json
{
  "type": "event",
  "tableId": "table-001",
  "seq": 43,
  "event": {
    "kind": "player_discarded",
    "seat": 2,
    "tile": "5m"
  }
}
```

Server prompt example:

```json
{
  "type": "prompt",
  "tableId": "table-001",
  "seq": 44,
  "actions": [
    { "type": "pon", "tiles": ["5m", "5m"] },
    { "type": "win" },
    { "type": "pass" }
  ],
  "timeoutMs": 8000
}
```

## Replay Format Direction

The internal replay format should be an append-only event log plus final metadata.

```json
{
  "version": 1,
  "rule": "custom-v1",
  "roomId": "room-001",
  "tableId": "table-001",
  "players": ["p0", "p1", "p2", "p3"],
  "initialWall": ["1m", "9p"],
  "events": [
    { "seq": 1, "type": "deal" },
    { "seq": 2, "type": "draw", "seat": 0, "tile": "5s" },
    { "seq": 3, "type": "discard", "seat": 0, "tile": "5s" }
  ],
  "finalScores": [31200, 21800, 25000, 22000]
}
```

Keep this as the source of truth. If a Tenhou-like export is needed later, generate it from this internal replay format.

## Initial Milestones

1. Implement core rule modules for action legality, win checks, claim priority, and scoring. Done for the current custom-rule surface.
2. Keep expanding deterministic tests whenever rule details change.
3. Implement a pure C++ round/table state machine that applies validated actions and emits replay events.
4. Add the WebSocket server and room/table/session lifecycle.
5. Build the minimal browser client for lobby, table, prompts, and results.
6. Export replay JSON and score files at game end.
7. Add reconnect, timeout auto-play, and replay viewer.

## Backend Tests

Configure and run the backend test suite from the repository root:

```powershell
cmake -S backend -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Current test executables:

- `gymj_game_engine_tests`: low-level tile-shape and win/tenpai checks.
- `gymj_rule_engine_tests`: `RoundState` to legal action generation, draw-buffer handling, claim resolution, and point calculation handoff.
- `gymj_point_engine_tests`: custom point settlement details.

## Development Notes

- Start with a single server process. The 200-player target does not require distributed state.
- Keep all authoritative decisions on the server.
- Make game state transitions event-driven and sequence-numbered.
- Prefer deterministic tests around the rule engine before building UI-heavy features.
- Do not introduce a database until requirements require long-term accounts, rankings, inventory, or analytics.

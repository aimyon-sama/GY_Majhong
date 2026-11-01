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

## Current Status

The project now has an end-to-end playable multiplayer path:

- One process manages up to 50 isolated four-player rooms, or about 200 seated players.
- The lobby lists, creates, and joins rooms. Room IDs are 1–32 ASCII digits; the default is `100001`.
- The server owns shuffling, legal actions, competing-claim resolution, scoring, timeouts, and all state transitions.
- Temporary identities support reconnect, connection takeover, handing a seat to AI, and returning to that seat.
- Every room advances independently and writes a separate JSONL replay. A room is reclaimed after its last human leaves.
- The Vue 3 frontend renders the lobby, table, prompts, countdowns, and settlement results.
- C++ unit tests, real WebSocket integration tests, and frontend state-machine tests are included.

Persistent accounts, a database, spectating, chat, browser replay viewing, and production deployment configuration are not implemented yet.

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
      point/              Custom point calculation and settlement.
      room/               Wall, round, and table state machines.
      rules/              Legal actions, win checks, and claim resolution.
    server/
      gateway/            WebSocket access, protocol codecs, routing, and timers.
      lobby/              Multi-room creation, listing, joining, and lifecycle.
      session/            Temporary player identity, reconnect, connection binding.
    storage/
      replay/             Append-only JSONL replay writer.
  tests/                  Backend unit and integration tests.

frontend/
  public/                 Standalone network debug page.
  src/
    components/           Reusable UI controls and table widgets.
    net/                  WebSocket client, protocol encoding/decoding.
    App.vue               Lobby and table UI.
  tests/                  Frontend network state-machine tests.

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

Create and join a numeric room:

```json
{
  "v": 1,
  "type": "create_room",
  "requestTag": "create-1",
  "roomId": "200001"
}
```

Submit an action advertised by the authoritative snapshot:

```json
{
  "v": 1,
  "type": "action",
  "roomId": "200001",
  "roundId": "1",
  "requestId": "7",
  "promptId": "18",
  "action": {"type": "discard", "tile": "5m"}
}
```

See [protocol v1](protocol/v1.md) for complete message shapes, ordering rules, and error codes.

## Replay Format

`Table` owns a `Replay` writer and exports one append-only `.jsonl` file per table,
containing every round's complete `RoundEvent` batches, results, point breakdowns,
and cumulative scores. The default output directory is `replay/`, configurable
through `TableOptions::replay_directory`. See [Replay Format v1](docs/replay.md)
for the file schema, error handling, and JSON dependency setup.

## Quick Start

Requirements:

- CMake 3.24+
- Visual Studio 2022 with the C++ x64 toolchain
- Node.js 22.12+

Build the backend from the repository root:

```powershell
Push-Location backend
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
Pop-Location
```

Start the server. The example uses port `8080`; replays are written under `replay/` in the process working directory:

```powershell
./backend/out/build/windows-msvc-debug/app/Debug/gymj_server.exe 8080 --action-timeout-ms 15000
```

Start the frontend in another terminal:

```powershell
Push-Location frontend
npm install
npm run dev
Pop-Location
```

Open the URL printed by Vite and connect to `ws://127.0.0.1:8080`. Join the default room `100001`, or enter a new numeric ID and select “Create and join.” A game starts after four independent browser tabs enter the same room and all four players ready up. Do not duplicate an already seated tab because that may copy its temporary identity from `sessionStorage`.

See the [frontend guide](frontend/README.md) for client states and four-player testing, and the [server guide](docs/server-v1.md) for backend design details.

## Testing

Run the complete backend suite:

```powershell
Push-Location backend
ctest --preset windows-msvc-debug
Pop-Location
```

Run frontend tests and a production build:

```powershell
Push-Location frontend
npm test
npm run build
Pop-Location
```

Backend coverage includes rules, scoring, wall, round, table, protocol, and sessions. CTest also launches a real server to test transport, full games, timeout AI, room closure, and two-room isolation. Frontend tests cover identity recovery, request synchronization, duplicate-click protection, the room lobby, and error recovery.

On other platforms, configure `backend/` explicitly with a modern C++20 compiler. GCC 8 is not supported.

## Room and Identity Rules

- Room IDs contain 1–32 ASCII digits. Leading zeroes are preserved, so `1` and `01` are different rooms.
- The default room is `100001`. A legacy join recreates a fresh table at that ID after the previous one closes.
- One temporary identity can bind to only one room. While another human remains, disconnecting or leaving hands the seat to AI and does not make it available to replacements.
- A player can restore the seat with the temporary token while the room exists. Once the last human leaves, the room closes and all of its old tokens expire.
- Identities live only in server memory and disappear on restart. The browser stores tokens in `sessionStorage`, scoped by server address.

## Roadmap

- Browser replay listing and playback.
- Production deployment, TLS termination, monitoring, and rate limiting.
- More complete desktop visuals, sound, and mobile layouts.
- Load testing and performance tuning against the 50-table target.

## Development Notes

- Start with a single server process. The 200-player target does not require distributed state.
- Keep all authoritative decisions on the server.
- Make game state transitions event-driven and sequence-numbered.
- Prefer deterministic tests around the rule engine before building UI-heavy features.
- Do not introduce a database until requirements require long-term accounts, rankings, inventory, or analytics.

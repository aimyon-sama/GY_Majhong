# Replay Format v1

GY Mahjong writes one append-only UTF-8 JSON Lines (`.jsonl`) file for each
table. Every non-empty line is one complete JSON object. Records are written in
authoritative order and flushed before the corresponding operation is reported
as persisted.

The default directory is `replay/` relative to the server process working
directory. `TableOptions::replay_directory` can override it. Filenames are
generated uniquely and currently have the form `table-<unique-data>.jsonl`;
readers must not derive game data from the filename.

> [!WARNING]
> Replay files contain concealed hands, private draws, concealed-kan tiles, and
> submitted claims before resolution. They are administrative/full-information
> records and must not be sent to players during a live round.

## Record order

A file contains these records in order:

1. Exactly one `table` header.
2. One `round_start` before every round.
3. One or more `round_events` records while that round advances.
4. Exactly one `round_finished` record when the round settles normally.
5. Optionally, one terminal `table_closed` record when all players leave.

The same file can contain multiple rounds. If the process stops unexpectedly,
the final round may have no `round_finished` or `table_closed` record. A crash
during a filesystem write may also leave one incomplete final line. A tolerant
reader may ignore only an invalid final line; invalid JSON anywhere earlier in
the file indicates corruption.

## Common values

- Tiles use `1m` through `9m`, `1s` through `9s`, and `1p` through `9p`.
  An absent or inapplicable tile is JSON `null`.
- Seats are integers `0` through `3`; `-1` means no applicable seat.
- IDs and sequence values are non-negative integers. JavaScript readers should
  preserve 64-bit values without assuming they always fit safely in `Number`.
- Round stages are `NotActive`, `WaitingDraw`, `WaitingDiscard`,
  `WaitingClaim`, and `Ended`.
- Arrays indexed by seat always use seat order `[0, 1, 2, 3]`.

## `table`

The first record identifies the schema version and table log:

```json
{"type":"table","version":1,"table_id":"table-..."}
```

Readers must check `version` before interpreting later records. Version 1 adds
no compatibility guarantee for unknown record types or fields from a newer
version, so readers should fail clearly on an unsupported version.

## `round_start`

This record captures the players and the configuration used to create the
round:

```json
{"type":"round_start","round_id":1,"players":[{"seat":0,"player_name":"East"},{"seat":1,"player_name":"South"},{"seat":2,"player_name":"West"},{"seat":3,"player_name":"North"}],"config":{"dealer_seat":0,"player_count":4,"action_timeout_ms":8000,"seed":42,"rule":{"game":{"allowMultiRon":true,"playerCount":4},"score":{"tsumo_point":3,"half_same_color_point":8,"same_color_point":17,"dash_chicken_point":3,"hand_chicken_point":1,"kan_point":5,"allow_no_chicken_no_kan":true}}},"wall_source":"shuffled"}
```

Fields:

- `round_id`: one-based, contiguous within the table file.
- `players`: the seat and display name captured at round start. Authentication
  IDs and session tokens are deliberately not stored.
- `config`: dealer, player count, action timeout, full 64-bit wall seed, and
  the rule/score configuration used by the round. The seed supports deterministic
  tests with the same implementation, but v1 does not promise identical
  shuffles across different C++ standard-library implementations.
- `wall_source`: `shuffled` for a production seeded wall or `provided` for the
  fixed-wall test entry point. A `provided` record does not currently serialize
  the supplied wall and therefore cannot reproduce it from `seed` alone.

Replay consumers should apply the persisted event stream rather than regenerate
the wall. The events contain the authoritative hands and draws that actually
occurred.

Field spelling is part of v1. In particular, the outer round configuration uses
`player_count`, while the nested rule configuration retains `playerCount` and
`allowMultiRon`.

## `round_events` and `round_finished`

Both transition records share this shape:

```json
{"type":"round_events","round_id":1,"table_seq":6,"seq_before":1,"seq_after":2,"stage_before":"WaitingDiscard","stage_after":"WaitingClaim","actor_before":0,"actor_after":-1,"events":[],"round_result":null,"point_result":null,"total_points":[0,0,0,0]}
```

- `table_seq`: monotonically increases when the public table state changes.
- `seq_before` and `seq_after`: contiguous per-round transition sequence. In
  v1, every persisted transition advances by exactly one.
- `stage_before` / `stage_after`: round stage on each side of the transition.
- `actor_before` / `actor_after`: acting seat, or `-1` when nobody acts.
- `events`: the authoritative event batch produced by this transition.
- `round_result`: `null` until the round ends; otherwise the complete result.
- `point_result`: `null` until scoring; otherwise the score breakdown.
- `total_points`: cumulative table score after this transition.

The terminal transition of a normally settled round uses
`type: "round_finished"`; all earlier transitions use `round_events`.

### Event object

Every item in `events` includes all of these fields:

```json
{"type":"PlayerDiscard","seq":2,"player_seat":0,"from_seat":-1,"tile":"5m","tiles":[],"action":{"type":"Discard","action_tile":"5m"},"discard_detail":"SimpleDraw"}
```

- `type`: `RoundStarted`, `InitialHands`, `PlayerDraw`, `PlayerDiscard`,
  `ClaimSubmitted`, `MeldDeclared`, `AddKanProposed`, `PlayerWin`,
  `RoundEnded`, `PointsCalculated`, or `ChickenRevealed`.
- `seq`: the transition's `seq_after`; all events in one batch share it.
- `player_seat` and `from_seat`: involved seats, or `-1` when inapplicable.
- `tile`: one tile or `null`.
- `tiles`: a tile array, used notably for complete initial hands.
- `action`: `null` or an object with `type` and `action_tile`. Action types are
  `None`, `Tsumo`, `Ron`, `Pon`, `SelfKan`, `AddKan`, `OpenKan`, `Pass`, and
  `Discard`.
- `discard_detail`: `None`, `SimpleDraw`, `AfterPon`, `AfterOpenKanDraw`,
  `AfterSelfKanDraw`, or `AfterAddKanDraw`.

### Round result

A non-null `round_result` contains:

- `has_winner`, `winner_seat`, `winner_seats`, `discarder_seat`, `win_tile`,
  `win_type`, and `detail`.
- `states`: four final player tile states. Each has `hand`, `river`, `melds`,
  `draw_buffer`, and `discard_buffer`. A meld has `type`, `tile`, and
  `from_seat`.
- `one_sou` and `eight_pin`: `null` or a dash-chicken object with `tile`,
  `discarded_by`, `claimed_by`, and `claim_type`.
- `round_chicken` and `chicken_indicator`.

`win_type` is `NoWinner`, `Ron`, or `Tsumo`. `detail` is `NoWinner`, `Simple`,
`TsumoFromKan`, `RonKanDiscard`, or `RonAddKan`. `winner_seat` remains the first
winner for older single-winner consumers; `winner_seats` is authoritative when
multiple ron is enabled.

### Point result

A non-null `point_result` contains:

- `point_to_others`: a 4-by-4 seat-indexed transfer matrix.
- `delta_result`: four score deltas for this round.
- `detail`: four objects with `total_point`, `point_from_chicken`,
  `point_from_kan`, `point_from_agari`, and `point_from_tenpai`.

For a valid settlement, the sum of `delta_result` is zero and `total_points`
equals the previous cumulative total plus this delta.

## `table_closed`

When the last human leaves or disconnects, the table appends one terminal
record and closes the stream:

```json
{"type":"table_closed","reason":"all_players_left","round_id":2,"table_seq":84,"round_in_progress":true,"total_points":[3,-1,-1,-1]}
```

- `reason` is `all_players_left` in v1.
- `round_in_progress` is true when the current round was abandoned before
  settlement.
- `total_points` preserves only previously settled cumulative scores. Closing
  a table never fabricates a draw or partial-round score result.

The record is written at most once. No later round or event records are valid.

## Storage and failure behavior

- The writer opens a newly generated path in append mode and never truncates an
  existing replay.
- Every record ends with `\n` and is flushed immediately.
- Failure to create the directory, open the file, or write the header prevents
  table construction.
- If `round_start` cannot be written, starting the round is rejected without
  partially advancing the in-memory round.
- A later write failure is retained in `Replay::error()`. Further replay writes
  stop, while the server reports `replay_write_failed` to connected clients.
- `table_closed` is best-effort: its write status is reflected through the same
  stored error channel before the stream closes.

## JSON dependency

Serialization uses the vendored nlohmann/json 3.12.0 headers at
`backend/include/third_party/nlohmann_json_3_12_0/include`. CMake exposes them
through the `nlohmann_json::nlohmann_json` target; configuring the backend does
not download this dependency. `gymj_storage_replay` links that target privately
because JSON types do not appear in its public interface.

The implementation is in `backend/src/storage/replay/replay.cpp`, and the
format invariants and failure cases are exercised by
`backend/tests/table_tests.cpp`.

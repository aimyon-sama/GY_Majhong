// 真实服务器定时器验收：客户端不提交动作，仍应自动出牌、过牌、结算。
import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import { startServer } from './lib/test-server.mjs';

const roomId = 'room-001';
const executable = process.argv[2];
let initialHands;

async function seatPlayers(server) {
  const players = [];
  const tokens = [];
  for (let seat = 0; seat < 4; seat++) {
    const player = await server.connect();
    player.send({ type: 'hello', nickname: `timeout-${seat}` });
    tokens.push((await player.next(m => m.type === 'welcome')).sessionToken);
    assert.equal((await player.request({ type: 'join_room', roomId })).accepted, true);
    players.push(player);
  }
  return { players, tokens };
}

async function ready(players) {
  for (const player of players) {
    assert.equal((await player.request({ type: 'ready', roomId })).accepted, true);
  }
  await Promise.all(players.map(player => player.barrier()));
}

async function resume(server, token) {
  const player = await server.connect();
  player.send({ type: 'hello', sessionToken: token });
  assert.equal((await player.next(m => m.type === 'welcome')).resumed, true);
  await player.next(m => m.type === 'table_update');
  await player.barrier();
  return player;
}

async function action(player, command) {
  player.send({ type: 'action', roomId, ...command });
  return player.next(m => m.type === 'ack' && m.operation === 'action'
    && m.requestId === command.requestId);
}

async function readReplay(server) {
  const directory = path.join(server.directory, 'replay');
  const files = await readdir(directory);
  assert.equal(files.length, 1);
  const text = await readFile(path.join(directory, files[0]), 'utf8');
  return { text, records: text.trim().split('\n').map(line => JSON.parse(line)) };
}

// 留出真实重连耗时，断言同一窗口的剩余时间减少，而不是重置为完整期限。
const reconnectServer = await startServer(executable, ['--action-timeout-ms', '1500', '--seed', '42']);
try {
  const { players, tokens } = await seatPlayers(reconnectServer);
  await ready(players);
  initialHands = players.map(p => p.history.find(m => m.type === 'table_update'
    && m.view.tableStage === 'playing').view.ownTiles.hand);
  const original = players[0].view;
  const command = { roundId: original.roundId, promptId: original.promptId,
    requestId: '1', action: original.availableActions.find(a => a.type === 'discard') };
  assert.ok(command.action);
  await players[0].disconnect();
  await players[1].next(m => m.type === 'table_update' && !m.view.seats[0].connected);
  await delay(450);
  players[0] = await resume(reconnectServer, tokens[0]);
  assert.equal(players[0].view.promptId, original.promptId);
  assert.equal(players[0].view.roundSeq, original.roundSeq, 'no early timeout');
  assert.deepEqual(players[0].view.ownTiles, original.ownTiles);
  assert.ok(players[0].view.remainingMs > 0);
  assert.ok(players[0].view.remainingMs < original.remainingMs - 300, 'resume must not extend deadline');

  // 轮到的玩家保持离线，其他客户端应被动收到自动出牌，无需发送任何动作。
  await players[0].disconnect();
  const discarded = await players[1].next(m => m.type === 'table_update'
    && m.events.some(e => e.type === 'player_discard' && e.seat === 0));
  assert.equal(discarded.view.seats[0].connected, false);
  players[0] = await resume(reconnectServer, tokens[0]);
  assert.equal(players[0].view.selfSeat, 0);
  assert.ok(BigInt(players[0].view.roundSeq) > BigInt(original.roundSeq));
  const beforeLate = players[0].view.tableSeq;
  const late = await action(players[0], command);
  assert.equal(late.accepted, false);
  assert.equal(late.error, 'stale_prompt');
  assert.equal(late.duplicate, false);
  const retry = await action(players[0], command);
  assert.equal(retry.accepted, false);
  assert.equal(retry.duplicate, true);
  assert.equal(retry.tableSeq, late.tableSeq);
  assert.equal((await action(players[0], { ...command, requestId: '2' })).error, 'stale_prompt');
  await players[0].barrier();
  assert.equal(players[0].view.tableSeq, beforeLate, 'late requests must not advance the table');
  assert.equal((await readReplay(reconnectServer)).records.flatMap(r => r.events ?? [])
    .filter(e => e.type === 'PlayerDiscard').length, 1, 'late retry must not discard twice');
} finally {
  await reconnectServer.stop();
}

const server = await startServer(executable, ['--action-timeout-ms', '100', '--seed', '42']);
try {
  const { players, tokens } = await seatPlayers(server);
  await Promise.all(players.map(p => p.barrier()));
  const idleCounts = players.map(p => p.history.length);
  await delay(150);
  assert.deepEqual(players.map(p => p.history.length), idleCounts, 'waiting table must not broadcast ticks');
  await ready(players);
  for (const [seat, player] of players.entries()) {
    assert.deepEqual(player.history.find(m => m.type === 'table_update'
      && m.view.tableStage === 'playing').view.ownTiles.hand, initialHands[seat],
    'same table seed must reproduce initial hands');
  }
  // 不发送出牌/过牌请求，也不主动轮询快照；所有推进都必须来自服务端计时器。
  await Promise.all(players.map(p => p.next(m => m.type === 'table_update'
    && m.view.tableStage === 'round_finished', 30000)));
  await Promise.all(players.map(p => p.barrier()));
  for (const [seat, player] of players.entries()) {
    assert.equal(player.view.roundResult.hasWinner, false);
    assert.ok(player.view.pointResult);
    assert.equal(player.view.remainingMs, null);
    assert.equal(player.view.totalPoints.reduce((sum, n) => sum + BigInt(n), 0n), 0n);
    let seq = 0n;
    for (const message of player.history.filter(m => m.type === 'table_update')) {
      const view = message.view;
      assert.equal(view.selfSeat, seat);
      assert.ok(BigInt(view.tableSeq) >= seq, 'timeout snapshots must remain ordered');
      seq = BigInt(view.tableSeq);
      assert.equal(view.lastRequestId, '0', 'no client game actions were submitted');
      if (view.roundStage !== 'ended') {
        assert.equal(view.roundResult, null);
        for (const publicSeat of view.seats) {
          assert.deepEqual(Object.keys(publicSeat).sort(),
            ['occupied', 'connected', 'ready', 'nickname', 'handSize', 'river', 'melds'].sort());
        }
      }
      for (const event of message.events) {
        assert.notEqual(event.type, 'claim_submitted');
        if (event.seat !== seat) {
          if (event.type === 'initial_hands') assert.deepEqual(event.tiles, []);
          if (event.type === 'player_draw') assert.equal(event.tile, null);
        }
      }
      for (const token of tokens) assert.ok(!JSON.stringify(message).includes(token));
    }
  }
  const replay = await readReplay(server);
  const starts = replay.records.filter(r => r.type === 'round_start');
  assert.equal(starts.length, 1);
  assert.equal(starts[0].config.action_timeout_ms, 100);
  assert.equal(replay.records.filter(r => r.type === 'round_finished').length, 1);
  const events = replay.records.flatMap(r => r.events ?? []);
  assert.ok(events.some(e => e.type === 'ClaimSubmitted'), 'auto-pass must be exercised');
  assert.ok(events.some(e => e.type === 'PlayerDiscard'), 'auto-discard must be exercised');
  assert.equal(events.filter(e => e.type === 'PointsCalculated').length, 1);
  let roundSeq = 0;
  for (const record of replay.records.filter(r => r.events)) {
    assert.equal(record.seq_before, roundSeq, 'replay transitions must be contiguous');
    assert.equal(record.seq_after, roundSeq + 1);
    roundSeq++;
  }
  assert.equal(replay.records.at(-1).type, 'round_finished');
  assert.deepEqual(replay.records.at(-1).total_points.map(String), players[0].view.totalPoints);
  for (const token of tokens) assert.ok(!replay.text.includes(token));
  const settledCounts = players.map(p => p.history.length);
  await delay(250);
  assert.deepEqual(players.map(p => p.history.length), settledCounts, 'settled table must not broadcast ticks');
  assert.equal((await readReplay(server)).text, replay.text, 'settlement must not be written twice');
  console.log('PASS: real timer, idle silence, offline auto-discard, resume deadline, stale retries, private ordered updates, auto-pass, timeout-only round, settlement and replay.');
} finally {
  await server.stop();
}

// 四人真实协议联调：身份、入座、准备、出牌、去重、私有视角和连接替换。
import assert from 'node:assert/strict';
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import { startServer, waitEvent } from './lib/test-server.mjs';

const server = await startServer(process.argv[2]);
const roomId = 'room-001';
const players = [];
const tokens = [];
const sync = () => Promise.all(players.map(player => player.barrier()));
async function act(player, command) {
  player.send({ type: 'action', roomId, ...command });
  return player.next(message => message.type === 'ack' && message.operation === 'action'
    && message.requestId === command.requestId);
}
function privateViews(client) {
  let seq = 0n;
  for (const message of client.history) {
    assert.equal(message.v, 1);
    if (message.type !== 'table_update') continue;
    const view = message.view;
    assert.ok(BigInt(view.tableSeq) >= seq, 'snapshots must not go backwards');
    seq = BigInt(view.tableSeq);
    assert.equal(view.selfSeat, client.view.selfSeat);
    assert.equal(message.roomId, roomId);
    assert.equal(message.recipient_id, undefined);
    // RoundEnded 与 PointsCalculated 是两次推进；前者已经允许公开最终牌面。
    if (view.roundStage !== 'ended') {
      assert.equal(view.roundResult, null);
      for (const seat of view.seats) {
        assert.deepEqual(Object.keys(seat).sort(),
          ['occupied', 'connected', 'ready', 'nickname', 'handSize', 'river', 'melds'].sort());
      }
    }
    for (const event of message.events) {
      assert.notEqual(event.type, 'claim_submitted');
      if (event.seat === view.selfSeat) continue;
      if (event.type === 'initial_hands') assert.deepEqual(event.tiles, []);
      if (event.type === 'player_draw') assert.equal(event.tile, null);
    }
    const wire = JSON.stringify(message);
    for (const token of tokens) assert.ok(!wire.includes(token), 'token leaked in snapshot');
  }
}

try {
  const unauthenticated = await server.connect();
  assert.equal((await unauthenticated.request({ type: 'join_room', roomId })).code, 'hello_required');
  unauthenticated.send({ type: 'hello', sessionToken: '0'.repeat(64) });
  assert.equal((await unauthenticated.next(m => m.type === 'error')).code, 'session_expired');

  for (let seat = 0; seat < 4; seat++) {
    const player = seat === 0 ? unauthenticated : await server.connect();
    players.push(player);
    player.send({ type: 'hello', nickname: '同名玩家' });
    const welcome = await player.next(m => m.type === 'welcome');
    assert.match(welcome.sessionToken, /^[0-9a-f]{64}$/);
    assert.equal(welcome.roomId, null);
    assert.equal(welcome.resumed, false);
    tokens.push(welcome.sessionToken);
    assert.equal((await player.request({ type: 'ready', roomId })).code, 'not_seated');
    assert.equal((await player.request({ type: 'join_room', roomId: 'missing' })).code, 'room_not_found');
    const before = player.history.length;
    assert.equal((await player.request({ type: 'join_room', roomId })).accepted, true);
    await player.barrier();
    assert.equal(player.history[before].type, 'ack', 'ack must precede private snapshot');
    assert.equal(player.view.selfSeat, seat);
  }
  assert.equal(new Set(tokens).size, 4, 'same nickname must not merge identities');
  await sync();

  const repeatedSeq = players[0].view.tableSeq;
  assert.equal((await players[0].request({ type: 'join_room', roomId })).accepted, true);
  await sync();
  assert.equal(players[0].view.tableSeq, repeatedSeq, 'join retry must not allocate another seat');
  players[0].send({ type: 'hello', nickname: 'again' });
  assert.equal((await players[0].next(m => m.type === 'error')).code, 'already_authenticated');

  const fifth = await server.connect();
  fifth.send({ type: 'hello', nickname: 'fifth' });
  await fifth.next(m => m.type === 'welcome');
  const full = await fifth.request({ type: 'join_room', roomId });
  assert.equal(full.accepted, false);
  assert.equal(full.error, 'room_full');
  await sync();
  assert.equal(players[0].view.tableSeq, repeatedSeq);

  for (let seat = 0; seat < 4; seat++) {
    assert.equal((await players[seat].request({ type: 'ready', roomId })).accepted, true);
    await sync();
    assert.equal(players[0].view.tableStage, seat === 3 ? 'playing' : 'waiting_ready');
  }
  const startedSeq = players[0].view.tableSeq;
  for (let seat = 0; seat < 4; seat++) {
    const view = players[seat].view;
    assert.equal(view.roundId, '1');
    assert.equal(view.selfSeat, seat);
    assert.equal(view.ownTiles.hand.length, seat === 0 ? 14 : 13);
    assert.equal(view.availableActions.length > 0, seat === 0);
    assert.equal(view.roundResult, null);
    assert.ok(view.seats.every(s => s.connected && s.occupied && !s.ready));
  }
  assert.equal((await players[3].request({ type: 'ready', roomId })).accepted, false);
  assert.equal((await fifth.request({ type: 'join_room', roomId })).error, 'room_full');
  assert.equal((await players[0].request({ type: 'snapshot', roomId: 'other' })).code, 'room_mismatch');
  const snapshotCounts = players.map(p => p.history.filter(m => m.type === 'table_update').length);
  assert.equal((await players[0].request({ type: 'snapshot', roomId })).accepted, true);
  await sync();
  for (let i = 1; i < 4; i++) {
    assert.equal(players[i].history.filter(m => m.type === 'table_update').length, snapshotCounts[i],
      'private snapshot request must not broadcast');
  }
  assert.equal(players[0].view.tableSeq, startedSeq);

  const dealer = players[0];
  const command = { roundId: dealer.view.roundId, requestId: '1', promptId: dealer.view.promptId,
    action: dealer.view.availableActions.find(a => a.type === 'discard') };
  for (const forged of [{ seat: 1 }, { playerId: '1' }]) {
    dealer.send({ type: 'action', roomId, ...command, ...forged });
    assert.equal((await dealer.next(m => m.type === 'error')).code, 'invalid_field');
  }
  dealer.send({ type: 'action', roomId, ...command, action: { type: 'discard', tile: '0m' } });
  assert.equal((await dealer.next(m => m.type === 'error')).code, 'invalid_tile');
  assert.equal((await act(dealer, { ...command, roundId: '0' })).error, 'stale_round');
  await sync();
  assert.equal(dealer.view.tableSeq, startedSeq, 'rejected commands must not advance game');

  const beforeAction = dealer.history.length;
  const accepted = await act(dealer, command);
  assert.equal(accepted.accepted, true);
  assert.equal(accepted.duplicate, false);
  await sync();
  assert.equal(dealer.history[beforeAction].type, 'ack');
  const afterDiscard = dealer.view.tableSeq;
  assert.ok(BigInt(afterDiscard) > BigInt(startedSeq));
  const retry = await act(dealer, command);
  assert.equal(retry.accepted, true);
  assert.equal(retry.duplicate, true);
  assert.equal(retry.tableSeq, accepted.tableSeq);
  assert.equal((await act(dealer, { ...command, action: { type: 'pass' } })).error, 'stale_request');
  const stalePrompt = { ...command, requestId: '2', promptId: '0' };
  assert.equal((await act(dealer, stalePrompt)).error, 'stale_prompt');
  assert.equal((await act(dealer, stalePrompt)).duplicate, true, 'rejected command must also be idempotent');
  await sync();
  assert.equal(dealer.view.tableSeq, afterDiscard);

  // 让旧连接排队发送快照，再接管，覆盖有写操作时关闭和旧 close 回调迟到。
  const old = players[1];
  const beforeResume = { ...old.view };
  for (let i = 0; i < 12; i++) old.send({ type: 'snapshot', roomId, requestTag: `queued-${i}` });
  const oldClosed = waitEvent(old.socket, 'close');
  const replacement = await server.connect();
  replacement.send({ type: 'hello', sessionToken: tokens[1] });
  const resumed = await replacement.next(m => m.type === 'welcome');
  assert.equal(resumed.resumed, true);
  assert.equal(resumed.roomId, roomId);
  assert.equal((await oldClosed).code, 1000);
  players[1] = replacement;
  await sync();
  assert.equal(replacement.history[0].type, 'welcome');
  assert.equal(replacement.history[1].type, 'table_update');
  assert.equal(replacement.view.selfSeat, 1);
  assert.deepEqual(replacement.view.ownTiles, beforeResume.ownTiles);
  assert.equal(replacement.view.promptId, beforeResume.promptId);
  if (beforeResume.remainingMs !== null) assert.ok(replacement.view.remainingMs <= beforeResume.remainingMs);
  assert.ok(players.every(p => p.view.seats[1].connected), 'old close must not disconnect replacement');
  privateViews(old);

  const offline = players[2];
  const offlineBefore = { ...offline.view };
  await offline.disconnect();
  await players[0].next(m => m.type === 'table_update' && !m.view.seats[2].connected);
  const restored = await server.connect();
  restored.send({ type: 'hello', sessionToken: tokens[2] });
  await restored.next(m => m.type === 'welcome');
  players[2] = restored;
  await sync();
  assert.equal(restored.view.selfSeat, 2);
  assert.deepEqual(restored.view.ownTiles, offlineBefore.ownTiles);
  assert.equal(restored.view.promptId, offlineBefore.promptId);
  privateViews(offline);

  // 始终选择出牌/过，最多 108 张牌即可流局；验证网络链路能推进到结算和牌谱落盘。
  for (let step = 0; players[0].view.tableStage === 'playing' && step < 500; step++) {
    const player = players.find(p => p.view.availableActions.some(a => a.type === 'discard' || a.type === 'pass'));
    assert.ok(player, 'playing table must provide a controllable action');
    const view = player.view;
    const action = view.availableActions.find(a => a.type === 'pass')
      ?? view.availableActions.find(a => a.type === 'discard');
    const result = await act(player, { roundId: view.roundId, promptId: view.promptId,
      requestId: (BigInt(view.lastRequestId) + 1n).toString(), action });
    assert.equal(result.accepted, true, `legal ${action.type} rejected: ${result.error}`);
    await sync();
  }
  assert.equal(players[0].view.tableStage, 'round_finished');
  assert.equal(players[0].view.roundResult.hasWinner, false);
  assert.equal(players[0].view.totalPoints.reduce((sum, point) => sum + BigInt(point), 0n), 0n);
  for (const player of players) privateViews(player);
  const replayFiles = await readdir(path.join(server.directory, 'replay'));
  assert.equal(replayFiles.length, 1);
  const replay = await readFile(path.join(server.directory, 'replay', replayFiles[0]), 'utf8');
  const records = replay.trim().split('\n').map(line => JSON.parse(line));
  assert.equal(records.filter(r => r.type === 'round_start').length, 1, 'ready retry must not restart round');
  assert.equal(records.at(-1).type, 'round_finished');
  for (const token of tokens) assert.ok(!replay.includes(token), 'token must never enter replay');
  console.log('PASS: four-player v1 game, full room, authenticated routing, private snapshots, retries, stale commands, connection takeover, offline resume, full round and replay.');
} finally {
  await server.stop();
}

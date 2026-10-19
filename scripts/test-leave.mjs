import assert from 'node:assert/strict'
import { setTimeout as delay } from 'node:timers/promises'
import { readdir, readFile } from 'node:fs/promises'
import path from 'node:path'
import { startServer } from './lib/test-server.mjs'

const server = await startServer(process.argv[2], ['--action-timeout-ms', '100', '--seed', '42'])
const roomId = 'room-001'
const players = []
const tokens = []
const sync = () => Promise.all(players.map(p => p.barrier()))
async function replay() {
  const directory = path.join(server.directory, 'replay')
  const files = await readdir(directory)
  assert.equal(files.length, 1)
  const text = await readFile(path.join(directory, files[0]), 'utf8')
  // Writer may be appending: only inspect complete JSONL lines.
  return text.split('\n').slice(0, -1).filter(Boolean).map(line => JSON.parse(line))
}
try {
  const guest = await server.connect()
  assert.equal((await guest.request({ type: 'leave_room', roomId })).code, 'hello_required')
  for (let seat = 0; seat < 4; seat++) {
    const player = await server.connect()
    players.push(player)
    player.send({ type: 'hello', nickname: `player-${seat}` })
    tokens.push((await player.next(m => m.type === 'welcome')).sessionToken)
    assert.equal((await player.request({ type: 'join_room', roomId })).accepted, true)
  }
  await sync()
  guest.send({ type: 'hello', nickname: 'fifth-player' })
  await guest.next(m => m.type === 'welcome')
  assert.equal((await guest.request({ type: 'join_room', roomId })).error, 'room_full')
  const leaving = players[0]
  assert.equal((await leaving.request({ type: 'leave_room', roomId: 'other' })).code, 'room_not_found')
  for (const forged of [{ seat: 1 }, { playerId: '1' }]) {
    leaving.send({ type: 'leave_room', roomId, requestTag: 'forged', ...forged })
    assert.equal((await leaving.next(m => m.type === 'error')).code, 'invalid_field')
  }
  assert.equal((await leaving.request({ type: 'leave_room', roomId })).accepted, true)
  await sync()
  assert.equal(players[1].view.seats[0].occupied, true)
  assert.equal(players[1].view.seats[0].connected, false)
  assert.equal(players[1].view.seats[0].ready, true)
  const seq = players[1].view.tableSeq
  assert.equal((await leaving.request({ type: 'leave_room', roomId })).accepted, true)
  await sync()
  assert.equal(players[1].view.tableSeq, seq, 'repeated takeover does not mutate table')
  assert.equal((await guest.request({ type: 'join_room', roomId })).error, 'room_full')
  assert.equal((await leaving.request({ type: 'ready', roomId })).error, 'action_not_allowed')

  // Three humans and one AI can start; an away socket gets no pushed private views.
  const detachedUpdates = leaving.history.filter(m => m.type === 'table_update').length
  for (const player of players.slice(1)) assert.equal((await player.request({ type: 'ready', roomId })).accepted, true)
  await sync()
  assert.equal(players[1].view.tableStage, 'playing')
  assert.equal(leaving.history.filter(m => m.type === 'table_update').length, detachedUpdates)
  leaving.send({ type: 'action', roomId, roundId: '1', promptId: players[1].view.promptId,
    requestId: '1', action: { type: 'pass' } })
  assert.equal((await leaving.next(m => m.type === 'ack' && m.operation === 'action')).error, 'action_not_allowed')
  assert.equal((await leaving.request({ type: 'join_room', roomId })).accepted, true)
  await sync()
  assert.equal(leaving.view.selfSeat, 0)
  assert.equal(leaving.view.seats[0].connected, true, 'return on existing socket takes control')
  assert.equal(leaving.view.lastRequestId, '0', 'rejected away request does not consume watermark')

  // One human keeps the game alive; the other three AI seats auto-ready after settlement.
  for (const player of players.slice(0, 3)) assert.equal((await player.request({ type: 'leave_room', roomId })).accepted, true)
  await players[3].next(m => m.type === 'table_update' && m.view.tableStage === 'round_finished', 15000)
  await players[3].barrier()
  const totals = players[3].view.totalPoints
  assert.ok(players[3].view.seats.slice(0, 3).every(s => s.occupied && !s.connected && s.ready))
  assert.equal((await players[3].request({ type: 'ready', roomId })).accepted, true)
  await players[3].barrier()
  assert.equal(players[3].view.roundId, '2')
  const restored = await server.connect()
  restored.send({ type: 'hello', sessionToken: tokens[0] })
  assert.equal((await restored.next(m => m.type === 'welcome')).roomId, roomId)
  await restored.next(m => m.type === 'table_update')
  assert.equal(restored.view.selfSeat, 0)
  assert.deepEqual(restored.view.totalPoints, totals)
  assert.equal((await restored.request({ type: 'leave_room', roomId })).accepted, true)

  // Last human explicitly leaves although AI clients still have live sockets.
  assert.equal((await players[3].request({ type: 'leave_room', roomId })).accepted, true)
  for (const player of [restored, players[1], players[2], players[3]]) {
    await player.next(m => m.type === 'server_notice' && m.code === 'room_closed')
    assert.equal((await player.request({ type: 'join_room', roomId })).code, 'hello_required')
    assert.equal((await player.request({ type: 'snapshot', roomId })).code, 'hello_required')
  }
  const closed = await replay()
  assert.equal(closed.at(-1).type, 'table_closed')
  assert.equal(closed.at(-1).reason, 'all_players_left')
  assert.equal(closed.at(-1).round_in_progress, true)
  assert.equal(closed.filter(r => r.type === 'round_finished').length, 1, 'abandoned round is not settled')
  assert.deepEqual(closed.at(-1).total_points.map(String), totals)
  await delay(300)
  assert.deepEqual(await replay(), closed, 'closed table has no subsequent AI ticks or rounds')
  for (const token of tokens) {
    const stale = await server.connect()
    stale.send({ type: 'hello', sessionToken: token })
    assert.equal((await stale.next(m => m.type === 'error')).code, 'session_expired')
  }

  // Fresh game reuses the fixed room address, never the closed table's seats or scores.
  assert.equal((await guest.request({ type: 'join_room', roomId })).accepted, true)
  await guest.barrier()
  assert.equal(guest.view.selfSeat, 0)
  assert.equal(guest.view.roundId, '0')
  assert.deepEqual(guest.view.totalPoints, ['0', '0', '0', '0'])
  for (const player of [restored, players[1], players[2], players[3]]) await player.disconnect()
  await guest.barrier()
  assert.equal((await guest.request({ type: 'snapshot', roomId })).accepted, true)
  await guest.barrier()
  assert.ok(guest.view.seats[0].connected, 'old close callbacks cannot disconnect a fresh seat')
  assert.equal((await guest.request({ type: 'leave_room', roomId })).accepted, true)
  await guest.next(m => m.type === 'server_notice' && m.code === 'room_closed')

  // A partially filled waiting table also closes; next verify all-socket disconnection during play.
  const freshPlayers = []
  const freshTokens = []
  for (let seat = 0; seat < 4; ++seat) {
    const player = await server.connect()
    player.send({ type: 'hello', nickname: `fresh-${seat}` })
    freshTokens.push((await player.next(m => m.type === 'welcome')).sessionToken)
    assert.equal((await player.request({ type: 'join_room', roomId })).accepted, true)
    freshPlayers.push(player)
  }
  for (const player of freshPlayers) assert.equal((await player.request({ type: 'ready', roomId })).accepted, true)
  await Promise.all(freshPlayers.map(p => p.barrier()))
  const activeToken = freshTokens[0]
  const takeover = await server.connect()
  takeover.send({ type: 'hello', sessionToken: activeToken })
  await takeover.next(m => m.type === 'welcome')
  await takeover.next(m => m.type === 'table_update')
  await Promise.all(freshPlayers.slice(1).map(p => p.disconnect()))
  await takeover.barrier()
  assert.equal((await takeover.request({ type: 'snapshot', roomId })).accepted, true,
    'replaced connection close must not close table with one human remaining')
  await takeover.disconnect()
  const replayDirectory = path.join(server.directory, 'replay')
  let files, texts, allRecords
  const deadline = Date.now() + 5000
  do {
    files = await readdir(replayDirectory)
    texts = await Promise.all(files.map(file => readFile(path.join(replayDirectory, file), 'utf8')))
    allRecords = texts.map(text => text.trim().split('\n').map(line => JSON.parse(line)))
    if (allRecords.every(records => records.at(-1).type === 'table_closed')) break
    assert.ok(Date.now() < deadline, 'last socket disconnect must close the game')
    await delay(20)
  } while (true)
  assert.equal(files.length, 3)
  assert.equal(allRecords.filter(records => records.at(-1).round_in_progress).length, 2)
  await delay(300)
  assert.deepEqual(await Promise.all(files.map(file => readFile(path.join(replayDirectory, file), 'utf8'))), texts)
  for (const token of freshTokens) {
    const stale = await server.connect()
    stale.send({ type: 'hello', sessionToken: token })
    assert.equal((await stale.next(m => m.type === 'error')).code, 'session_expired')
  }
  console.log('PASS: AI with humans, last-human leave/disconnect closes table, stale tokens rejected, terminal replay, fresh game and late-close isolation.')

} finally {
  await server.stop()
}

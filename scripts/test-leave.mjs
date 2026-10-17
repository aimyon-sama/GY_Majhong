import assert from 'node:assert/strict'
import { startServer } from './lib/test-server.mjs'

const server = await startServer(process.argv[2], ['--action-timeout-ms', '100', '--seed', '42'])
const roomId = 'room-001'
const players = []
const tokens = []
const sync = () => Promise.all(players.map(p => p.barrier()))
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
  guest.send({ type: 'hello', nickname: 'replacement' })
  await guest.next(m => m.type === 'welcome')
  assert.equal((await guest.request({ type: 'join_room', roomId })).error, 'room_full')
  const leaving = players[0]
  assert.equal((await leaving.request({ type: 'leave_room', roomId: 'other' })).code, 'room_not_found')
  for (const forged of [{ seat: 1 }, { playerId: '1' }]) {
    leaving.send({ type: 'leave_room', roomId, requestTag: 'forged', ...forged })
    assert.equal((await leaving.next(m => m.type === 'error')).code, 'invalid_field')
  }
  for (const player of players.slice(0, 2)) assert.equal((await player.request({ type: 'ready', roomId })).accepted, true)
  await sync()
  const before = players[1].view.tableSeq
  assert.equal((await leaving.request({ type: 'leave_room', roomId })).accepted, true)
  await sync()
  assert.equal(players[1].view.tableSeq, String(BigInt(before) + 1n))
  assert.equal(players[1].view.seats[0].occupied, false)
  assert.ok(players[1].view.seats.every(seat => !seat.ready))
  const vacatedSeq = players[1].view.tableSeq
  assert.equal((await leaving.request({ type: 'leave_room', roomId })).accepted, true)
  await sync()
  assert.equal(players[1].view.tableSeq, vacatedSeq, 'repeated unseated leave does not change table')
  const detachedUpdates = leaving.history.filter(m => m.type === 'table_update').length
  assert.equal((await guest.request({ type: 'join_room', roomId })).accepted, true)
  await guest.barrier()
  assert.equal(guest.view.selfSeat, 0)
  for (const type of ['snapshot', 'ready']) {
    assert.equal((await leaving.request({ type, roomId })).code, 'not_seated')
  }
  leaving.send({ type: 'action', roomId, roundId: '0', promptId: '0', requestId: '1', action: { type: 'pass' } })
  assert.equal((await leaving.next(m => m.type === 'error')).code, 'not_seated')
  await leaving.barrier()
  assert.equal(leaving.history.filter(m => m.type === 'table_update').length, detachedUpdates,
    'departed connection receives no subsequent private views')
  await leaving.disconnect()
  await guest.barrier()
  assert.equal(guest.view.seats[0].connected, true, 'departed close cannot disconnect replacement')
  const restored = await server.connect()
  restored.send({ type: 'hello', sessionToken: tokens[0] })
  const welcome = await restored.next(m => m.type === 'welcome')
  assert.equal(welcome.resumed, true)
  assert.equal(welcome.roomId, null)
  await restored.barrier()
  assert.equal(restored.view, null)
  assert.equal((await restored.request({ type: 'join_room', roomId })).error, 'room_full')
  assert.equal((await guest.request({ type: 'leave_room', roomId })).accepted, true)
  assert.equal((await restored.request({ type: 'join_room', roomId })).accepted, true)
  players[0] = restored
  await sync()
  for (const player of players) assert.equal((await player.request({ type: 'ready', roomId })).accepted, true)
  await sync()
  assert.equal(players[0].view.tableStage, 'playing')
  assert.equal((await restored.request({ type: 'leave_room', roomId })).error, 'action_not_allowed')
  await sync()
  assert.ok(players.every(p => p.view.seats[0].occupied))
  await restored.next(m => m.type === 'table_update' && m.view.tableStage === 'round_finished', 15000)
  await sync()
  for (const player of players) assert.equal((await player.request({ type: 'leave_room', roomId })).accepted, true)
  for (const player of players) assert.equal((await player.request({ type: 'join_room', roomId })).accepted, true)
  await sync()
  assert.ok(players.every(p => p.view.tableStage === 'waiting_ready' && p.view.roundResult === null))
  assert.deepEqual(players[0].view.totalPoints, ['0', '0', '0', '0'])
  for (const player of players) assert.equal((await player.request({ type: 'ready', roomId })).accepted, true)
  await sync()
  assert.equal(players[0].view.roundId, '2')
  console.log('PASS: authenticated leave, vacancy broadcasts, replacement, revoked seat access, resume, active-round rejection and empty-table reuse.')
} finally {
  await server.stop()
}

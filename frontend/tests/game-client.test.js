import assert from 'node:assert/strict'
import { test } from 'node:test'
import { GameClient, savedAddress } from '../src/net/game-client.js'

class FakeSocket extends EventTarget {
  static instances = []
  readyState = 0
  sent = []
  constructor(url) { super(); this.url = url; FakeSocket.instances.push(this) }
  send(text) { this.sent.push(JSON.parse(text)) }
  close() { this.readyState = 3 }
  open() { this.readyState = 1; this.dispatchEvent(new Event('open')) }
  receive(message) { this.dispatchEvent(new MessageEvent('message', { data: JSON.stringify({ v: 1, ...message }) })) }
}
function storage() {
  const data = new Map()
  return { getItem: key => data.get(key), setItem: (key, value) => data.set(key, value), removeItem: key => data.delete(key) }
}
function snapshot(overrides = {}) {
  return { selfSeat: 0, tableStage: 'playing', tableSeq: '10', roundId: '1', promptId: '5',
    lastRequestId: '9007199254740993', seats: [{ ready: false }],
    ownTiles: { hand: ['1m', '2m'], drawBuffer: '2m' },
    availableActions: [{ type: 'discard', tile: '1m' }], ...overrides }
}
function setup(t, options = {}) {
  const client = new GameClient({ WebSocketImpl: FakeSocket, storage: storage(), ...options })
  t.after(() => client.disconnect())
  client.connect('ws://localhost:8080', '甲')
  const socket = FakeSocket.instances.at(-1)
  socket.open()
  return { client, socket }
}
function enter(socket, view = snapshot()) {
  socket.receive({ type: 'welcome', sessionToken: 'a'.repeat(64), nickname: '甲', roomId: null })
  const join = socket.sent.at(-1)
  socket.receive({ type: 'ack', operation: 'join_room', requestTag: join.requestTag, accepted: true })
  socket.receive({ type: 'table_update', roomId: 'room-001', view })
}
function synchronize(socket, view) {
  const request = socket.sent.at(-1)
  assert.equal(request.type, 'snapshot')
  socket.receive({ type: 'ack', operation: 'snapshot', requestTag: request.requestTag, accepted: true })
  socket.receive({ type: 'table_update', roomId: 'room-001', view })
}

test('hello → join → snapshot; commands never mutate tiles, double clicks are blocked until synchronized', t => {
  const { client, socket } = setup(t)
  assert.deepEqual(socket.sent[0], { v: 1, type: 'hello', nickname: '甲' })
  enter(socket)
  const before = structuredClone(client.state.view)
  assert.equal(client.act({ type: 'discard', tile: '1m' }), true)
  const command = socket.sent.at(-1)
  assert.equal(command.requestId, '9007199254740994')
  assert.equal(client.act(command.action), false)
  assert.deepEqual(client.state.view, before)
  socket.receive({ type: 'ack', operation: 'action', requestId: command.requestId, accepted: true })
  assert.equal(client.state.pending, true)
  socket.receive({ type: 'table_update', roomId: 'room-001', view: snapshot({ tableSeq: '11' }) })
  assert.equal(client.state.pending, true, 'broadcast is not the requested synchronization snapshot')
  synchronize(socket, snapshot({ tableSeq: '12', ownTiles: { hand: ['2m'], drawBuffer: null } }))
  assert.equal(client.state.pending, false)
  assert.deepEqual(client.state.view.ownTiles.hand, ['2m'])
  socket.receive({ type: 'table_update', roomId: 'room-001', view: before })
  assert.equal(client.state.view.tableSeq, '12')
})

test('rejected actions refresh state and consume a local request number even if snapshot watermark is unchanged', t => {
  const { client, socket } = setup(t)
  enter(socket)
  client.act({ type: 'discard', tile: '1m' })
  const command = socket.sent.at(-1)
  socket.receive({ type: 'ack', operation: 'action', requestId: command.requestId,
    accepted: false, error: 'stale_prompt' })
  synchronize(socket, snapshot())
  assert.match(client.state.notice, /操作窗口已结束/)
  client.act(command.action)
  assert.equal(socket.sent.at(-1).requestId, '9007199254740995')
  socket.receive({ type: 'error', code: 'stale_request', requestId: socket.sent.at(-1).requestId })
  synchronize(socket, snapshot({ roundId: '2', lastRequestId: '0' }))
  client.act(command.action)
  assert.equal(socket.sent.at(-1).requestId, '1')
})

test('refresh restores saved identity; welcome alone cannot enable actions; old socket events are ignored', t => {
  const saved = storage()
  const { client, socket } = setup(t, { storage: saved })
  enter(socket)
  client.disconnect()
  assert.equal(client.act({ type: 'discard', tile: '1m' }), false)
  const restored = new GameClient({ WebSocketImpl: FakeSocket, storage: saved })
  t.after(() => restored.disconnect())
  restored.connect(savedAddress(saved), '不同昵称')
  const next = FakeSocket.instances.at(-1)
  next.open()
  assert.deepEqual(next.sent[0], { v: 1, type: 'hello', sessionToken: 'a'.repeat(64) })
  next.receive({ type: 'welcome', sessionToken: 'a'.repeat(64), nickname: '甲', resumed: true, roomId: 'room-001' })
  assert.equal(restored.state.status, 'syncing')
  assert.equal(restored.act({ type: 'discard', tile: '1m' }), false)
  next.receive({ type: 'table_update', roomId: 'room-001', view: snapshot({ tableSeq: '2' }) })
  assert.equal(restored.state.status, 'online')
  assert.equal(next.sent.length, 1, 'restored seat does not rejoin or replay previous clicks')
  client.connect('ws://localhost:8080', '甲')
  socket.receive({ type: 'error', code: 'session_expired' })
  socket.dispatchEvent(new Event('close'))
  assert.equal(client.state.status, 'connecting')
})

test('expired identity is cleared and next connection creates a new session', t => {
  const { client, socket } = setup(t)
  enter(socket)
  client.disconnect()
  client.connect('ws://localhost:8080', '甲')
  const next = FakeSocket.instances.at(-1)
  next.open()
  next.receive({ type: 'error', code: 'session_expired' })
  assert.equal(client.state.view, null)
  assert.equal(client.state.status, 'disconnected')
  client.connect('ws://localhost:8080', '乙')
  const fresh = FakeSocket.instances.at(-1)
  fresh.open()
  assert.equal(fresh.sent[0].nickname, '乙')
  assert.equal(fresh.sent[0].sessionToken, undefined)
})

test('full room and failed storage have useful messages without exposing credentials', t => {
  const denied = { getItem() { throw new Error() }, setItem() { throw new Error() } }
  const { client, socket } = setup(t, { storage: denied })
  socket.receive({ type: 'welcome', sessionToken: 'b'.repeat(64), nickname: '甲', roomId: null })
  socket.receive({ type: 'ack', operation: 'join_room', requestTag: socket.sent.at(-1).requestTag,
    accepted: false, error: 'room_full' })
  assert.equal(client.state.status, 'lobby')
  assert.match(client.state.notice, /房间已满/)
  assert.match(client.state.storageWarning, /无法保存身份/)
  assert.ok(!JSON.stringify(client.state).includes('b'.repeat(64)))
  client.disconnect()
  client.connect('ws://localhost:8081', '甲')
  const other = FakeSocket.instances.at(-1)
  other.open()
  assert.equal(other.sent[0].sessionToken, undefined, 'credentials are scoped to server address')
})

test('ready waits for authoritative snapshot; unanswered requests time out', async t => {
  const { client, socket } = setup(t, { timeoutMs: 25 })
  enter(socket, snapshot({ tableStage: 'waiting_ready', availableActions: [] }))
  assert.equal(client.ready(), true)
  assert.equal(client.ready(), false)
  assert.equal(client.state.view.seats[0].ready, false)
  const request = socket.sent.at(-1)
  socket.receive({ type: 'ack', operation: 'ready', requestTag: request.requestTag, accepted: true })
  synchronize(socket, snapshot({ tableStage: 'waiting_ready', seats: [{ ready: true }] }))
  assert.equal(client.ready(), false)
  client.disconnect()
  client.connect('ws://localhost:8080', '甲')
  await new Promise(resolve => setTimeout(resolve, 60))
  assert.equal(client.state.status, 'disconnected')
  assert.match(client.state.notice, /超时/)
})

test('unavailable storage warns, and failure to delete an expired token does not trap reconnects', t => {
  const unavailable = setup(t, { storage: null })
  enter(unavailable.socket)
  assert.match(unavailable.client.state.storageWarning, /无法保存身份/)
  const denied = {
    getItem: () => 'c'.repeat(64),
    removeItem() { throw new Error('denied') },
  }
  const { client, socket } = setup(t, { storage: denied })
  assert.equal(socket.sent[0].sessionToken, 'c'.repeat(64))
  socket.receive({ type: 'error', code: 'session_expired' })
  client.connect('ws://localhost:8080', '新玩家')
  const next = FakeSocket.instances.at(-1)
  next.open()
  assert.equal(next.sent[0].nickname, '新玩家')
  assert.equal(next.sent[0].sessionToken, undefined)
})

test('leave waits for ack, releases local view, keeps identity and explicitly rejoins', t => {
  const { client, socket } = setup(t)
  enter(socket, snapshot({ tableStage: 'waiting_ready' }))
  const before = client.state.view
  assert.equal(client.leave(), true)
  assert.equal(client.leave(), false)
  assert.equal(client.state.view, before, 'no optimistic seat release')
  const request = socket.sent.at(-1)
  assert.equal(request.type, 'leave_room')
  socket.receive({ type: 'ack', operation: 'leave_room', requestTag: 'unrelated', accepted: true })
  assert.equal(client.state.pending, true)
  socket.receive({ type: 'ack', operation: 'leave_room', requestTag: request.requestTag, accepted: true })
  assert.equal(client.state.status, 'lobby')
  assert.equal(client.state.view, null)
  assert.equal(client.state.roomId, null)
  assert.equal(client.tokenFor('ws://localhost:8080/'), 'a'.repeat(64))
  socket.receive({ type: 'table_update', roomId: 'room-001', view: before })
  assert.equal(client.state.view, null, 'late snapshot cannot resurrect a departed seat')
  assert.equal(client.ready(), false)
  assert.equal(client.join(), true)
  assert.equal(client.join(), false)
  const join = socket.sent.at(-1)
  socket.receive({ type: 'ack', operation: 'join_room', requestTag: join.requestTag, accepted: true })
  socket.receive({ type: 'table_update', roomId: 'room-001', view: snapshot({ tableSeq: '20', lastRequestId: '0' }) })
  assert.equal(client.state.status, 'online')
  assert.equal(client.state.roomId, 'room-001')
  assert.equal(client.act({ type: 'discard', tile: '1m' }), true)
  assert.equal(socket.sent.at(-1).requestId, '1', 'new seat has fresh action numbering')
})

test('resuming a released identity stays in lobby without automatically taking a seat', t => {
  const { client, socket } = setup(t)
  socket.receive({ type: 'welcome', sessionToken: 'a'.repeat(64), nickname: '甲', resumed: true, roomId: null })
  assert.equal(client.state.status, 'lobby')
  assert.equal(socket.sent.length, 1, 'only hello was sent')
  assert.equal(client.join(), true)
  const join = socket.sent.at(-1)
  socket.receive({ type: 'ack', operation: 'join_room', requestTag: join.requestTag, accepted: false, error: 'room_full' })
  assert.equal(client.state.status, 'lobby')
  assert.equal(client.state.pending, false)
  assert.equal(client.join(), true, 'full room can retry without reconnecting')
})

test('leave is disabled during play; a race with starting the round restores authoritative state', t => {
  const { client, socket } = setup(t)
  enter(socket)
  assert.equal(client.leave(), false)
  socket.receive({ type: 'table_update', roomId: 'room-001', view: snapshot({ tableStage: 'round_finished' }) })
  assert.equal(client.leave(), true)
  const request = socket.sent.at(-1)
  socket.receive({ type: 'ack', operation: 'leave_room', requestTag: request.requestTag,
    accepted: false, error: 'action_not_allowed' })
  assert.equal(client.state.pending, true)
  synchronize(socket, snapshot({ tableSeq: '21', roundId: '2' }))
  assert.equal(client.state.status, 'online')
  assert.equal(client.state.view.tableStage, 'playing')
  assert.match(client.state.notice, /暂时不能离桌/)
  assert.equal(client.leave(), false)
})

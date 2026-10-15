// 使用正式前端客户端连接真实 C++ 服务端，验证页面所依赖的状态流。
import assert from 'node:assert/strict'
import { setTimeout as delay } from 'node:timers/promises'
import { GameClient } from '../frontend/src/net/game-client.js'
import { startServer } from './lib/test-server.mjs'

const server = await startServer(process.argv[2], ['--action-timeout-ms', '15000', '--seed', '42'])
const clients = []
const stores = []
async function until(check, description) {
  const deadline = Date.now() + 5000
  while (!check()) {
    if (Date.now() >= deadline) throw new Error(`Timeout: ${description}`)
    await delay(5)
  }
}
function createClient(store = new Map()) {
  const client = new GameClient({ storage: {
    getItem: key => store.get(key), setItem: (key, value) => store.set(key, value), removeItem: key => store.delete(key),
  } })
  clients.push(client)
  return client
}
async function barrier(client) {
  const socket = client.socket
  const nonce = 'frontend-test'
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { cleanup(); reject(new Error('ping timeout')) }, 5000)
    function cleanup() { clearTimeout(timer); socket.removeEventListener('message', receive) }
    function receive(event) {
      const message = JSON.parse(event.data)
      if (message.type === 'pong' && message.nonce === nonce) { cleanup(); resolve() }
    }
    socket.addEventListener('message', receive)
    client.send({ type: 'ping', nonce })
  })
}
let players = []
async function sync() {
  await until(() => players.every(p => p.state.status === 'online' && !p.state.pending), 'clients synchronized')
  await Promise.all(players.map(barrier))
}

try {
  const probe = await server.connect()
  const address = probe.socket.url
  await probe.disconnect()
  for (let seat = 0; seat < 4; seat++) {
    const store = new Map()
    stores.push(store)
    const client = createClient(store)
    players.push(client)
    client.connect(address, `玩家${seat}`)
    await until(() => client.state.status === 'online', 'join room')
    assert.equal(client.state.view.selfSeat, seat)
  }
  await sync()
  const fifth = createClient()
  fifth.connect(address, '第五人')
  await until(() => fifth.state.status === 'disconnected', 'full room rejection')
  assert.match(fifth.state.notice, /房间已满/)

  for (const client of players) {
    assert.equal(client.ready(), true)
    assert.equal(client.ready(), false, 'double ready is blocked')
    await sync()
  }
  assert.ok(players.every(p => p.state.view.tableStage === 'playing'))
  const dealer = players[0]
  const before = structuredClone(dealer.state.view)
  const action = before.availableActions.find(a => a.type === 'discard')
  assert.equal(dealer.act(action), true)
  assert.equal(dealer.act(action), false)
  assert.deepEqual(dealer.state.view, before, 'click cannot locally discard a tile')
  await sync()
  assert.ok(BigInt(dealer.state.view.tableSeq) > BigInt(before.tableSeq))
  assert.equal(dealer.state.view.ownTiles.hand.length, 13)

  // 刷新页面对应创建新客户端对象，恢复凭据、座位和服务端请求水位。
  const previous = players[0]
  const previousView = structuredClone(previous.state.view)
  previous.disconnect()
  const restored = createClient(stores[0])
  players[0] = restored
  restored.connect(address, '不应覆盖原昵称')
  await sync()
  assert.equal(restored.state.nickname, '玩家0')
  assert.equal(restored.state.view.selfSeat, 0)
  assert.equal(restored.state.view.lastRequestId, previousView.lastRequestId)
  assert.deepEqual(restored.state.view.ownTiles, previousView.ownTiles)

  // 始终出牌/过，使用前端的编号与同步逻辑完成一局，再准备下一局。
  for (let step = 0; players[0].state.view.tableStage === 'playing' && step < 500; step++) {
    const player = players.find(p => p.state.view.availableActions.some(a => ['discard', 'pass'].includes(a.type)))
    assert.ok(player, 'playing table must have an actionable client')
    const actions = player.state.view.availableActions
    assert.equal(player.act(actions.find(a => a.type === 'pass') ?? actions.find(a => a.type === 'discard')), true)
    await sync()
  }
  assert.ok(players.every(p => p.state.view.tableStage === 'round_finished'))
  assert.ok(players.every(p => p.state.view.pointResult && p.state.view.roundResult))
  for (const player of players) {
    assert.equal(player.ready(), true)
    await sync()
  }
  assert.ok(players.every(p => p.state.view.roundId === '2' && p.state.view.tableStage === 'playing'))
  console.log('PASS: frontend client joins, readies, submits authoritative actions, restores identity, completes a round and starts the next.')
} finally {
  for (const client of clients) client.disconnect()
  await server.stop()
}

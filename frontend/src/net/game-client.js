const DEFAULT_ROOM = '100001'
const ROOM_ID_PATTERN = /^[0-9]{1,32}$/
const SESSION_PREFIX = 'gymj.game.session:'
const ADDRESS_KEY = 'gymj.game.address'
const errors = {
  room_full: '房间已满，请稍后重试。',
  room_not_found: '房间不存在，请刷新房间列表。',
  invalid_room_id: '房间号必须是 1 至 32 位数字。',
  room_exists: '该房间已经存在，请直接加入。',
  room_limit_reached: '服务器房间数已达上限，请加入现有房间。',
  already_in_room: '当前身份已经绑定到一个房间。',
  session_expired: '原身份已失效，请重新进入房间。',
  invalid_session: '保存的身份无效，请重新进入房间。',
  invalid_nickname: '昵称需要 1 至 24 个字符，且不能包含控制字符。',
  stale_round: '小局已结束，正在更新牌桌。',
  stale_prompt: '操作窗口已结束，正在更新牌桌。',
  stale_request: '操作编号已过期，正在更新牌桌。',
  action_not_allowed: '当前无法执行此操作，正在更新牌桌。',
  replay_write_failed: '牌谱保存失败，请联系管理员。',
  start_failed: '开局失败，请稍后重新准备。',
  advance_failed: '牌局推进失败，请联系管理员。',
}

function browserStorage() {
  try { return globalThis.sessionStorage } catch { return null }
}

export function savedAddress(storage = browserStorage()) {
  try { return storage?.getItem(ADDRESS_KEY) || 'ws://127.0.0.1:8080' }
  catch { return 'ws://127.0.0.1:8080' }
}

// 与 Vue 无关，便于用同一客户端验证真实 WebSocket 链路。
export class GameClient {
  constructor({ onChange = () => {}, WebSocketImpl = globalThis.WebSocket,
    storage = browserStorage(), timeoutMs = 10000 } = {}) {
    this.onChange = onChange
    this.WebSocketImpl = WebSocketImpl
    this.storage = storage
    this.timeoutMs = timeoutMs
    this.socket = null
    this.timer = null
    this.serial = 0
    this.tokens = new Map()
    this.state = { status: 'idle', view: null, roomId: null, rooms: [], nickname: '',
      pending: false, notice: '填写昵称，连接后进入房间。' }
  }

  publish(patch) {
    this.state = { ...this.state, ...patch }
    this.onChange(this.state)
  }

  tokenFor(address) {
    if (this.tokens.has(address)) return this.tokens.get(address)
    try { return this.storage?.getItem(SESSION_PREFIX + address) }
    catch { return null }
  }

  saveToken(token) {
    // null 也缓存：浏览器拒绝删除存储时，不再读回已经失效的旧令牌。
    this.tokens.set(this.address, token)
    try {
      if (!this.storage) throw new Error('Storage unavailable')
      if (token) this.storage.setItem(SESSION_PREFIX + this.address, token)
      else this.storage.removeItem(SESSION_PREFIX + this.address)
      this.publish({ storageWarning: '' })
    } catch {
      this.publish({ storageWarning: '浏览器无法保存身份；当前页面可重连，刷新后可能无法恢复座位。' })
    }
  }

  armTimeout() {
    clearTimeout(this.timer)
    this.timer = setTimeout(() => this.disconnect('等待服务器超时，请重新连接以同步牌桌。'), this.timeoutMs)
  }

  connect(address, nickname) {
    if (this.socket) return
    try {
      const url = new URL(address.trim())
      if (!['ws:', 'wss:'].includes(url.protocol) || url.username || url.password || url.hash) throw new Error()
      this.address = url.href
    } catch {
      this.publish({ notice: '请输入有效的 ws:// 或 wss:// 服务地址。' })
      return
    }
    const token = this.tokenFor(this.address)
    if (!token && !nickname.trim()) {
      this.publish({ notice: '请输入昵称。' })
      return
    }
    this.pending = null
    this.snapshotTag = null
    this.awaitingSnapshot = false
    this.requestRound = null
    this.requestId = 0n
    this.publish({ status: 'connecting', view: null, pending: false,
      notice: token ? '正在恢复原身份…' : '正在连接服务器…' })
    let socket
    try { socket = new this.WebSocketImpl(this.address) }
    catch { this.disconnect('无法连接服务器，请检查服务地址。'); return }
    this.socket = socket
    this.armTimeout()
    socket.addEventListener('open', () => {
      if (this.socket !== socket) return
      this.publish({ status: 'authenticating' })
      this.send({ type: 'hello', ...(token ? { sessionToken: token } : { nickname: nickname.trim() }) })
    })
    socket.addEventListener('message', event => {
      if (this.socket !== socket) return
      try { this.receive(JSON.parse(event.data)) }
      catch { this.disconnect('收到无法识别的服务器消息，请重新连接。') }
    })
    socket.addEventListener('error', () => {
      if (this.socket === socket) this.disconnect('连接失败，请确认服务器已启动并检查服务地址。')
    })
    socket.addEventListener('close', () => {
      if (this.socket === socket) this.disconnect('连接已断开，请重新连接；若身份被其他页面接管，请使用该页面。')
    })
  }

  send(message) {
    if (this.socket?.readyState !== 1) return false
    try { this.socket.send(JSON.stringify({ v: 1, ...message })); return true }
    catch { this.disconnect('发送失败，请重新连接以同步牌桌。'); return false }
  }

  lobby(type, roomId = this.state.roomId) {
    if (!roomId) return false
    const requestTag = String(++this.serial)
    this.pending = { type, requestTag, roomId }
    this.publish({ pending: true })
    this.armTimeout()
    return this.send({ type, roomId, requestTag })
  }

  refreshRooms() {
    if (this.state.status !== 'lobby' || this.state.pending) return false
    const requestTag = String(++this.serial)
    this.pending = { type: 'list_rooms', requestTag }
    this.publish({ pending: true })
    this.armTimeout()
    return this.send({ type: 'list_rooms', requestTag })
  }

  sync() {
    this.snapshotTag = String(++this.serial)
    this.publish({ status: 'syncing', pending: true })
    this.armTimeout()
    this.send({ type: 'snapshot', roomId: this.state.roomId, requestTag: this.snapshotTag })
  }

  enterLobby(notice, roomId = null) {
    clearTimeout(this.timer)
    this.pending = null
    this.snapshotTag = null
    this.awaitingSnapshot = false
    this.requestRound = null
    this.requestId = 0n
    this.publish({ status: roomId ? 'away' : 'lobby', roomId, view: null, pending: false, notice })
  }

  join(roomId = DEFAULT_ROOM) {
    if (!['lobby', 'away'].includes(this.state.status) || this.state.pending) return false
    const target = this.state.status === 'away' ? this.state.roomId : roomId.trim()
    if (!ROOM_ID_PATTERN.test(target)) {
      this.publish({ notice: errors.invalid_room_id })
      return false
    }
    this.publish({ status: 'syncing', notice: '正在进入房间…' })
    this.lobby('join_room', target)
    return true
  }

  create(roomId) {
    if (this.state.status !== 'lobby' || this.state.pending) return false
    const target = roomId.trim()
    if (!ROOM_ID_PATTERN.test(target)) {
      this.publish({ notice: errors.invalid_room_id })
      return false
    }
    this.publish({ status: 'syncing', notice: '正在创建房间…' })
    this.lobby('create_room', target)
    return true
  }

  leave() {
    const { status, pending, view } = this.state
    if (status !== 'online' || pending || !view) return false
    this.lobby('leave_room')
    this.publish({ notice: '正在交给 AI 代管，等待服务器确认…' })
    return true
  }

  ready() {
    const { status, pending, view } = this.state
    if (status !== 'online' || pending || !view || view.tableStage === 'playing'
      || view.seats[view.selfSeat].ready) return false
    this.lobby('ready')
    return true
  }

  act(action) {
    const { status, pending, view } = this.state
    if (status !== 'online' || pending || !view || view.tableStage !== 'playing') return false
    const legal = view.availableActions.find(item => item.type === action.type && item.tile === action.tile)
    if (!legal) return false
    const command = { type: 'action', roomId: this.state.roomId, roundId: view.roundId,
      promptId: view.promptId, requestId: String(++this.requestId), action: { ...legal } }
    this.pending = command
    this.publish({ pending: true, notice: '操作已发送，等待服务器确认…' })
    this.armTimeout()
    return this.send(command)
  }

  receive(message) {
    if (message.v !== 1) throw new Error('Unsupported protocol')
    if (message.type === 'welcome') {
      this.saveToken(message.sessionToken)
      try { this.storage?.setItem(ADDRESS_KEY, this.address) } catch {}
      this.publish({ status: 'syncing', nickname: message.nickname,
        roomId: message.roomId,
        notice: message.resumed ? '身份已恢复。' : '身份已创建，请选择房间。' })
      // 恢复已入座身份时，welcome 后由服务器主动发送私有快照。
      if (message.roomId) this.awaitingSnapshot = true
      else {
        this.enterLobby(message.resumed ? '身份已恢复，当前未入座，请选择房间。' : '身份已创建，请选择或创建房间。')
        this.refreshRooms()
      }
    } else if (message.type === 'room_list') {
      if (this.pending?.type !== 'list_rooms' || message.requestTag !== this.pending.requestTag) return
      clearTimeout(this.timer)
      this.pending = null
      this.publish({ rooms: message.rooms, pending: false,
        notice: message.rooms.length ? '房间列表已更新。' : '暂无房间，可以创建一个。' })
    } else if (message.type === 'table_update') {
      if (['lobby', 'away'].includes(this.state.status)) return
      if (this.state.roomId && message.roomId !== this.state.roomId) return
      const view = message.view
      if (this.state.view && BigInt(view.tableSeq) < BigInt(this.state.view.tableSeq)) return
      if (this.requestRound !== view.roundId) {
        this.requestRound = view.roundId
        this.requestId = 0n
      }
      if (BigInt(view.lastRequestId) > this.requestId) this.requestId = BigInt(view.lastRequestId)
      const synchronized = this.awaitingSnapshot
      if (synchronized) {
        this.awaitingSnapshot = false
        this.pending = null
        clearTimeout(this.timer)
      }
      // 完整替换快照；events 不再重复修改手牌、牌河或分数。
      this.publish({ view, roomId: message.roomId, ...(synchronized ? { status: 'online', pending: false } : {}) })
    } else if (message.type === 'ack') {
      if (message.requestTag && message.requestTag === this.snapshotTag) {
        this.snapshotTag = null
        if (!message.accepted) return this.disconnect('无法同步牌桌，请重新连接。')
        this.awaitingSnapshot = true
        return
      }
      const pending = this.pending
      if (!pending || message.operation !== pending.type) return
      if (pending.type === 'action' ? message.requestId !== pending.requestId
        : message.requestTag !== pending.requestTag) return
      if (pending.type === 'leave_room') {
        if (message.accepted) return this.enterLobby('仍有玩家在桌时，AI 代管并自动准备；全员离开后关局，不能返回。', this.state.roomId)
        this.publish({ notice: '交给 AI 失败，正在同步牌桌。' })
        this.sync()
        return
      }
      this.publish({ notice: message.accepted
        ? (pending.type === 'action' ? '操作已确认。' : pending.type === 'ready' ? '已准备，等待其他玩家。' : '已进入房间，请准备。')
        : (errors[message.error] || '请求未被接受，请稍后重试。') })
      if (['join_room', 'create_room'].includes(pending.type)) {
        if (!message.accepted) return this.enterLobby(this.state.notice)
        this.publish({ roomId: pending.roomId })
        this.awaitingSnapshot = true
      } else {
        // 拒绝和重复请求可能没有广播，主动取快照后才解除按钮锁定。
        this.sync()
      }
    } else if (message.type === 'error') {
      const notice = errors[message.code] || '服务器无法处理请求，请重新连接后重试。'
      if (['session_expired', 'invalid_session'].includes(message.code)) {
        this.saveToken(null)
        this.publish({ view: null, roomId: null })
        this.disconnect(notice)
      } else if (['join_room', 'create_room'].includes(this.pending?.type)
          && message.requestTag === this.pending.requestTag) {
        this.enterLobby(notice)
        this.refreshRooms()
      } else if (this.pending?.type === 'action' && message.requestId === this.pending.requestId) {
        this.publish({ notice })
        this.sync()
      } else this.disconnect(notice)
    } else if (message.type === 'server_notice') {
      if (message.code === 'room_closed') {
        this.saveToken(null)
        this.publish({ view: null, roomId: null })
        this.disconnect('所有玩家均已离开，本局已关闭，无法重连恢复。可重新连接开始新对局。')
        return
      }
      this.publish({ notice: errors[message.code] || '服务器暂时出现问题，请稍后重试。' })
    }
  }

  disconnect(notice = this.state.view
    ? '已断开连接；若仍有玩家在桌，可返回原座位。全员离开则本局关闭，无法恢复。' : '已断开连接，可重新连接恢复身份。') {
    const socket = this.socket
    this.socket = null // 忽略旧连接迟到的消息和 close 回调。
    clearTimeout(this.timer)
    this.pending = null
    this.snapshotTag = null
    this.awaitingSnapshot = false
    try { socket?.close() } catch {}
    this.publish({ status: 'disconnected', pending: false, notice })
  }
}

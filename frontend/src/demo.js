// 只提供展示数据，不实现麻将规则或网络通信。
export const scenes = [
  ['waiting_ready', '等待准备'],
  ['discard', '轮到自己出牌'],
  ['claim', '碰 / 明杠 / 过'],
  ['kan', '暗杠 / 加杠'],
  ['tsumo', '自摸'],
  ['ron', '荣和'],
  ['waiting', '等待其他玩家'],
  ['draw_result', '流局结算'],
  ['win_result', '胡牌结算'],
]

export const actionNames = {
  discard: '出牌', pon: '碰', open_kan: '明杠', self_kan: '暗杠',
  add_kan: '加杠', tsumo: '自摸', ron: '荣和', pass: '过',
}

export function tileName(tile) {
  if (!tile) return '牌背'
  const ranks = ['一', '二', '三', '四', '五', '六', '七', '八', '九']
  return `${ranks[Number(tile[0]) - 1]}${{ m: '万', s: '索', p: '筒' }[tile[1]]}`
}

export function makeView(scene, nickname, roundId = '1') {
  const finished = scene.endsWith('_result')
  const view = {
    selfSeat: 0,
    roomId: 'room-001',
    roundId,
    tableStage: scene === 'waiting_ready' ? 'waiting_ready' : finished ? 'round_finished' : 'playing',
    dealerSeat: 0,
    actingSeat: ['claim', 'ron', 'waiting'].includes(scene) ? 1 : 0,
    tilesRemaining: finished ? 0 : 48,
    remainingMs: ['discard', 'claim', 'kan', 'tsumo', 'ron'].includes(scene) ? 8000 : null,
    seats: ['我', '下家', '对家', '上家'].map((name, seat) => ({
      nickname: seat === 0 ? nickname : `示例玩家${seat}`,
      occupied: true,
      connected: true,
      ready: scene === 'waiting_ready' && seat !== 0,
      handSize: 13,
      river: scene === 'waiting_ready' ? [] : [['2m', '8s'], ['3p'], ['5m', '6p'], ['9s']][seat],
      melds: [],
    })),
    ownTiles: { hand: ['1m', '1m', '2m', '3m', '4m', '5p', '5p', '5p', '2s', '3s', '4s', '7s', '8s', '9s'], drawBuffer: '9s' },
    availableActions: [],
    pendingAction: null,
    totalPoints: ['0', '0', '0', '0'],
    result: null,
  }
  if (scene === 'waiting_ready') view.ownTiles = { hand: [], drawBuffer: null }
  if (['claim', 'ron', 'waiting'].includes(scene)) {
    view.ownTiles.hand.pop()
    view.ownTiles.drawBuffer = null
  }
  if (scene === 'claim') {
    view.pendingAction = { type: 'discard', tile: '5p' }
    view.availableActions = [{ type: 'pon', tile: '5p' }, { type: 'open_kan', tile: '5p' }, { type: 'pass' }]
  }
  if (scene === 'kan') {
    view.ownTiles = { hand: ['1m', '1m', '1m', '1m', '3m', '4m', '5m', '6s', '7s', '9p'], drawBuffer: '9p' }
    view.seats[0].melds = [{ type: 'pon', tile: '9p' }]
    view.availableActions = [{ type: 'self_kan', tile: '1m' }, { type: 'add_kan', tile: '9p' }]
  }
  if (scene === 'tsumo') view.availableActions = [{ type: 'tsumo' }]
  if (scene === 'ron') {
    view.pendingAction = { type: 'discard', tile: '9s' }
    view.availableActions = [{ type: 'ron' }, { type: 'pass' }]
  }
  if (['discard', 'kan', 'tsumo'].includes(scene)) {
    view.availableActions.push(...[...new Set(view.ownTiles.hand)].map(tile => ({ type: 'discard', tile })))
  }
  view.seats[0].handSize = view.ownTiles.hand.length
  if (scene === 'waiting_ready') view.seats.forEach(seat => { seat.handSize = 0 })
  if (finished) {
    const won = scene === 'win_result'
    const delta = won ? [9, -3, -3, -3] : [3, -1, -1, -1]
    view.totalPoints = delta.map(String)
    view.result = {
      reason: won ? '自己胡牌（示例）' : '牌山摸完，流局（示例）',
      chickenTile: '1s',
      details: delta.map((total, seat) => ({
        nickname: view.seats[seat].nickname, chicken: 0, kan: 0,
        agari: won ? total : 0, tenpai: won ? 0 : total, total,
      })),
    }
  }
  return view
}

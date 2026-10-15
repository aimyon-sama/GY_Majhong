export const actionNames = {
  discard: '出牌', pon: '碰', open_kan: '明杠', self_kan: '暗杠',
  add_kan: '加杠', tsumo: '自摸', ron: '荣和', pass: '过',
}

export function tileName(tile) {
  if (!tile) return '牌背'
  const ranks = ['一', '二', '三', '四', '五', '六', '七', '八', '九']
  return `${ranks[Number(tile[0]) - 1]}${{ m: '万', s: '索', p: '筒' }[tile[1]]}`
}

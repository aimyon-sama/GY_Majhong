<script setup>
import { computed, onMounted, onUnmounted, ref, shallowRef, watch } from 'vue'
import SeatPanel from './components/SeatPanel.vue'
import HandPanel from './components/HandPanel.vue'
import ResultPanel from './components/ResultPanel.vue'
import { actionNames, tileName } from './tile-labels.js'
import { GameClient, savedAddress } from './net/game-client.js'

const address = ref(savedAddress())
const nickname = ref('玩家')
const selected = ref(null)
const client = new GameClient({ onChange: value => { state.value = value } })
const state = shallowRef(client.state)
const view = computed(() => state.value.view)
const connected = computed(() => !['idle', 'disconnected'].includes(state.value.status))
const locked = computed(() => state.value.status !== 'online' || state.value.pending)
const specialActions = computed(() => view.value?.availableActions.filter(action => action.type !== 'discard') ?? [])
const positions = ['自己', '下家', '对家', '上家']
const seats = computed(() => view.value ? positions.map((position, offset) => {
  const index = (view.value.selfSeat + offset) % 4
  return { position, index, seat: view.value.seats[index] }
}) : [])
const stageNames = { waiting_ready: '等待准备', playing: '对局中', round_finished: '本局结束' }
const statusNames = { idle: '未连接', connecting: '连接中', authenticating: '确认身份中',
  syncing: '同步牌桌中', online: '已连接', lobby: '已连接 · 未入座', disconnected: '已断开' }
const canReady = computed(() => !locked.value && view.value && view.value.tableStage !== 'playing'
  && !view.value.seats[view.value.selfSeat].ready)
const canLeave = computed(() => !locked.value && view.value && view.value.tableStage !== 'playing')
// 组件使用的结果仅由服务端结算字段派生，不在前端计算分数。
const result = computed(() => {
  const current = view.value
  if (!current?.roundResult || !current.pointResult) return null
  return {
    reason: current.roundResult.hasWinner
      ? `${current.roundResult.winnerSeats.map(seat => current.seats[seat].nickname).join('、')} ${current.roundResult.winType === 'tsumo' ? '自摸' : '荣和'}`
      : '牌山摸完，流局',
    chickenTile: current.roundResult.roundChicken,
    details: current.pointResult.detail.map((detail, seat) => ({ ...detail, nickname: current.seats[seat].nickname })),
  }
})
const remainingMs = ref(null)
let deadline = null
let timer
watch(view, current => {
  selected.value = null
  remainingMs.value = current?.remainingMs ?? null
  deadline = remainingMs.value === null ? null : performance.now() + remainingMs.value
})
function updateClock() {
  remainingMs.value = deadline === null ? null : Math.max(0, deadline - performance.now())
}
function connect() { client.connect(address.value, nickname.value) }
function discard() {
  if (selected.value === null) return
  client.act({ type: 'discard', tile: view.value.ownTiles.hand[selected.value] })
}
onMounted(() => {
  timer = setInterval(updateClock, 100)
  try {
    if (client.tokenFor(new URL(address.value).href)) connect()
  } catch { /* 无效地址交给连接表单报告。 */ }
})
onUnmounted(() => { clearInterval(timer); client.disconnect() })
</script>

<template>
  <main>
    <header>
      <h1>GY Mahjong</h1>
      <p>{{ statusNames[state.status] }}<span v-if="state.nickname"> · {{ state.nickname }}</span></p>
    </header>
    <section aria-labelledby="entry-title">
      <h2 id="entry-title">进入房间</h2>
      <form @submit.prevent="connect">
        <label>服务地址 <input v-model="address" required :disabled="connected" spellcheck="false" /></label>
        <label>昵称 <input v-model="nickname" required maxlength="48" :disabled="connected" /></label>
        <span>房间：room-001</span>
        <button type="submit" :disabled="connected || !nickname.trim()">{{ state.status === 'disconnected' ? '重新连接' : '连接并进入' }}</button>
        <button v-if="connected" type="button" @click="client.disconnect()">断开连接</button>
        <button v-if="state.status === 'lobby'" type="button" :disabled="state.pending" @click="client.join()">加入房间</button>
      </form>
      <p>此标签页会保存临时身份，刷新或重新连接可恢复原座位。服务重启后需重新进入。</p>
    </section>
    <p role="status" aria-live="polite">{{ state.notice }}</p>
    <p v-if="state.storageWarning" role="alert">{{ state.storageWarning }}</p>
    <template v-if="view">
      <p v-if="state.status !== 'online'">牌桌尚未同步，操作暂不可用。</p>
      <section aria-labelledby="table-title">
        <h2 id="table-title">牌桌 · {{ state.roomId }}</h2>
        <button type="button" :disabled="!canLeave" @click="client.leave()">离桌并释放座位</button>
        <p>等待准备或结算后可离桌；离桌清除自己的累计分，其他玩家保留累计分并重新准备。</p>
        <p v-if="view.tableStage === 'playing'">对局中不能离桌，断开连接后仍保留座位并由服务器托管。</p>
        <p>第 {{ view.roundId }} 局 · {{ stageNames[view.tableStage] }} · 牌山剩余 {{ view.tilesRemaining }} 张</p>
        <p>我的操作倒计时：{{ state.status !== 'online' ? '等待同步' : remainingMs === null ? '无' : `${Math.ceil(remainingMs / 1000)} 秒` }}</p>
        <p v-if="view.pendingAction">待响应的牌：{{ tileName(view.pendingAction.tile) }}</p>
        <div class="seats">
          <SeatPanel v-for="item in seats" :key="item.index" :seat="item.seat" :position="item.position"
            :dealer="item.index === view.dealerSeat" :active="view.tableStage === 'playing' && item.index === view.actingSeat"
            :self="item.index === view.selfSeat" :points="view.totalPoints[item.index]" />
        </div>
        <button v-if="view.tableStage === 'waiting_ready'" type="button" :disabled="!canReady" @click="client.ready()">
          {{ view.seats[view.selfSeat].ready ? '已准备，等待其他玩家' : '准备' }}
        </button>
      </section>
      <HandPanel :tiles="view.ownTiles" :actions="view.availableActions" :selected="selected"
        :locked="locked || view.tableStage !== 'playing'" @select="selected = $event" @discard="discard" />
      <section v-if="view.tableStage === 'playing'" aria-labelledby="actions-title">
        <h2 id="actions-title">操作</h2>
        <div class="button-row">
          <button v-for="action in specialActions" :key="`${action.type}-${action.tile ?? ''}`" type="button"
            :disabled="locked" @click="client.act(action)">
            {{ actionNames[action.type] }} {{ action.tile ? tileName(action.tile) : '' }}
          </button>
        </div>
        <p v-if="state.pending">正在等待服务器确认和同步牌桌。</p>
        <p v-else-if="!view.availableActions.length">等待其他玩家操作。</p>
        <p v-else-if="!specialActions.length">请选择手牌，然后点击“打出选中的牌”。</p>
      </section>
      <ResultPanel v-if="result" :result="result" :total-points="view.totalPoints" :ready-disabled="!canReady" @next="client.ready()" />
    </template>
  </main>
</template>

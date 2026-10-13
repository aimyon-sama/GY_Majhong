<script setup>
import { computed, ref } from 'vue'
import SeatPanel from './components/SeatPanel.vue'
import HandPanel from './components/HandPanel.vue'
import ResultPanel from './components/ResultPanel.vue'
import { actionNames, makeView, scenes, tileName } from './demo.js'

const nickname = ref('玩家')
const entered = ref(false)
const scene = ref('waiting_ready')
const round = ref(1)
const view = ref(makeView(scene.value, nickname.value))
const selected = ref(null)
const locked = ref(false)
const notice = ref('填写昵称并进入原型，或使用“示例场景”查看页面状态。')
const specialActions = computed(() => view.value.availableActions.filter(action => action.type !== 'discard'))
const positions = ['自己', '下家', '对家', '上家']
const stageNames = { waiting_ready: '等待准备', playing: '对局中', round_finished: '本局结束' }

function loadScene(value) {
  scene.value = value
  view.value = makeView(value, nickname.value.trim() || '玩家', String(round.value))
  selected.value = null
  locked.value = false
  notice.value = `正在展示：${scenes.find(([key]) => key === value)[1]}。`
}

function enter() {
  if (!nickname.value.trim()) return
  entered.value = true
  loadScene('waiting_ready')
}

function previewScene(event) {
  entered.value = true
  loadScene(event.target.value)
}

function chooseAction(action) {
  if (locked.value) return
  if (action.type === 'discard') {
    const index = selected.value
    if (index === null || view.value.ownTiles.hand[index] !== action.tile) return
    view.value.ownTiles.hand.splice(index, 1)
    view.value.ownTiles.drawBuffer = null
    view.value.seats[0].river.push(action.tile)
    view.value.seats[0].handSize = view.value.ownTiles.hand.length
    selected.value = null
  }
  if (action.type === 'tsumo' || action.type === 'ron') {
    loadScene('win_result')
  } else {
    locked.value = true
    view.value.remainingMs = null
  }
  notice.value = `已选择${actionNames[action.type]}${action.tile ? ` ${tileName(action.tile)}` : ''}（本地演示）。可切换示例场景继续查看。`
}

function nextRound() {
  round.value++
  loadScene('waiting_ready')
  notice.value = '已返回准备状态，点击“准备”预览下一局。'
}
</script>

<template>
  <main>
    <header>
      <h1>GY Mahjong</h1>
      <p>Vue 静态原型 · 示例数据 · 未连接服务器</p>
    </header>

    <section aria-labelledby="entry-title">
      <h2 id="entry-title">进入房间</h2>
      <form @submit.prevent="enter">
        <label>昵称 <input v-model="nickname" required maxlength="24" :disabled="entered" /></label>
        <span>房间：room-001</span>
        <button type="submit" :disabled="entered || !nickname.trim()">进入原型</button>
        <button v-if="entered" type="button" @click="entered = false; round = 1; loadScene('waiting_ready')">返回入口</button>
      </form>
    </section>

    <section aria-labelledby="demo-title">
      <h2 id="demo-title">示例场景</h2>
      <label>查看状态 <select :value="scene" @change="previewScene">
        <option v-for="[value, label] in scenes" :key="value" :value="value">{{ label }}</option>
      </select></label>
      <button type="button" @click="entered = true; loadScene(scene)">重置当前示例</button>
      <p>用于检查内容和按钮，不进行规则判定。示例倒计时保持不变。</p>
    </section>

    <p role="status" aria-live="polite">{{ notice }}</p>

    <template v-if="entered">
      <section aria-labelledby="table-title">
        <h2 id="table-title">牌桌 · {{ view.roomId }}</h2>
        <p>第 {{ view.roundId }} 局 · {{ stageNames[view.tableStage] }} · 牌山剩余 {{ view.tilesRemaining }} 张</p>
        <p>我的操作倒计时：{{ view.remainingMs === null ? '无' : `${view.remainingMs / 1000} 秒（示例）` }}</p>
        <p v-if="view.pendingAction">待响应的牌：{{ tileName(view.pendingAction.tile) }}</p>
        <div class="seats">
          <SeatPanel v-for="(seat, index) in view.seats" :key="index" :seat="seat" :position="positions[index]"
            :dealer="index === view.dealerSeat" :active="view.tableStage === 'playing' && index === view.actingSeat"
            :self="index === view.selfSeat" :points="view.totalPoints[index]" />
        </div>
        <button v-if="view.tableStage === 'waiting_ready'" type="button" @click="loadScene('discard')">准备（预览开局）</button>
      </section>

      <HandPanel :tiles="view.ownTiles" :actions="view.availableActions" :selected="selected"
        :locked="locked || view.tableStage !== 'playing'" @select="selected = $event"
        @discard="chooseAction({ type: 'discard', tile: view.ownTiles.hand[selected] })" />

      <section v-if="view.tableStage === 'playing'" aria-labelledby="actions-title">
        <h2 id="actions-title">操作</h2>
        <div class="button-row">
          <button v-for="action in specialActions" :key="`${action.type}-${action.tile ?? ''}`" type="button"
            :disabled="locked" @click="chooseAction(action)">
            {{ actionNames[action.type] }} {{ action.tile ? tileName(action.tile) : '' }}
          </button>
        </div>
        <p v-if="locked">操作已记录，请切换示例场景继续查看。</p>
        <p v-else-if="!view.availableActions.length">等待其他玩家操作。</p>
        <p v-else-if="!specialActions.length">请选择手牌，然后点击“打出选中的牌”。</p>
      </section>

      <ResultPanel v-if="view.result" :result="view.result" :total-points="view.totalPoints" @next="nextRound" />
    </template>
  </main>
</template>

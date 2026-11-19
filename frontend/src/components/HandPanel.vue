<script setup>
import { computed } from 'vue'
import { tileName } from '../tile-labels.js'
const props = defineProps({ tiles: Object, actions: Array, selected: Number, locked: Boolean })
const emit = defineEmits(['select', 'discard'])
const allowed = computed(() => new Set(props.actions.filter(a => a.type === 'discard').map(a => a.tile)))
</script>

<template>
  <section aria-labelledby="hand-title">
    <h2 id="hand-title">我的手牌（{{ tiles.hand.length }} 张）</h2>
    <p v-if="!tiles.hand.length">准备后等待发牌。</p>
    <div class="button-row">
      <button v-for="(tile, index) in tiles.hand" :key="index" type="button"
        :disabled="locked || !allowed.has(tile)" :aria-pressed="selected === index"
        @click="emit('select', index)">{{ tileName(tile) }}</button>
    </div>
    <p>刚摸到：{{ tiles.drawBuffer ? tileName(tiles.drawBuffer) : '无' }}（已包含在手牌中）</p>
    <p>已选：{{ selected !== null ? tileName(tiles.hand[selected]) : '未选择' }}</p>
    <button type="button" :disabled="locked || selected === null || !allowed.has(tiles.hand[selected])"
      @click="emit('discard')">打出选中的牌</button>
  </section>
</template>

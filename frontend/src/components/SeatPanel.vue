<script setup>
import { actionNames, tileName } from '../demo.js'
defineProps({ seat: Object, position: String, dealer: Boolean, active: Boolean, self: Boolean, points: String })
</script>

<template>
  <fieldset>
    <legend>{{ position }} · {{ seat.nickname }} {{ dealer ? '（庄家）' : '' }}</legend>
    <p>
      {{ seat.connected ? '在线' : '离线' }} · {{ seat.ready ? '已准备' : '未准备' }}
      · 累计分：{{ points }} <strong v-if="active">· 当前行动</strong>
    </p>
    <p v-if="!self">手牌：{{ seat.handSize }} 张 <span aria-hidden="true">{{ '▧ '.repeat(seat.handSize) }}</span></p>
    <p>牌河：<span v-for="(tile, index) in seat.river" :key="index">{{ tileName(tile) }} </span><span v-if="!seat.river.length">暂无弃牌</span></p>
    <p>副露：<span v-for="(meld, index) in seat.melds" :key="index">{{ actionNames[meld.type] }} {{ tileName(meld.tile) }}；</span><span v-if="!seat.melds.length">无</span></p>
  </fieldset>
</template>

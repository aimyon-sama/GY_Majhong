<script setup>
import { tileName } from '../demo.js'
defineProps({ result: Object, totalPoints: Array })
defineEmits(['next'])
</script>

<template>
  <section aria-labelledby="result-title">
    <h2 id="result-title">本局结果</h2>
    <p>{{ result.reason }} · 本局鸡牌：{{ tileName(result.chickenTile) }}</p>
    <table>
      <caption>小分明细（示例数据）</caption>
      <thead><tr><th scope="col">玩家</th><th scope="col">鸡分</th><th scope="col">杠分</th><th scope="col">胡牌分</th><th scope="col">听牌分</th><th scope="col">本局变化</th><th scope="col">累计分</th></tr></thead>
      <tbody><tr v-for="(detail, index) in result.details" :key="index">
        <th scope="row">{{ detail.nickname }}</th><td>{{ detail.chicken }}</td><td>{{ detail.kan }}</td>
        <td>{{ detail.agari }}</td><td>{{ detail.tenpai }}</td><td>{{ detail.total }}</td><td>{{ totalPoints[index] }}</td>
      </tr></tbody>
    </table>
    <button type="button" @click="$emit('next')">下一局准备</button>
  </section>
</template>

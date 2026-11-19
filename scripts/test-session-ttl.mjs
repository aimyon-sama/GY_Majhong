// Node.js 22+: node scripts/test-session-ttl.mjs <path/to/gymj_server.exe>
// 验证未入座大厅身份的 TTL 恢复窗口、过期回收和身份数量上限。
import assert from 'node:assert/strict';
import { startServer } from './lib/test-server.mjs';

const server = await startServer(process.argv[2], [
  '--lobby-session-ttl-seconds', '1',
  '--max-lobby-sessions', '4',
]);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function hello(message) {
  const client = await server.connect();
  client.send({ type: 'hello', ...message });
  const response = await client.next(event => event.type === 'welcome' || event.type === 'error');
  return { client, response };
}

try {
  // TTL 内断开再恢复：令牌仍有效。
  const first = await hello({ nickname: 'lobby-1' });
  assert.equal(first.response.type, 'welcome');
  assert.equal(first.response.resumed, false);
  const token = first.response.sessionToken;
  await first.client.disconnect();

  const resumed = await hello({ sessionToken: token });
  assert.equal(resumed.response.type, 'welcome', 'identity should resume inside the lobby TTL');
  assert.equal(resumed.response.resumed, true);
  await resumed.client.disconnect();

  // 达到身份上限后拒绝新的大厅身份，已有令牌不受影响。
  for (const nickname of ['lobby-2', 'lobby-3', 'lobby-4']) {
    const created = await hello({ nickname });
    assert.equal(created.response.type, 'welcome');
    await created.client.disconnect();
  }
  const overLimit = await hello({ nickname: 'lobby-5' });
  assert.equal(overLimit.response.type, 'error');
  assert.equal(overLimit.response.code, 'rate_limited');
  await overLimit.client.disconnect();

  // 超过 TTL 后旧身份被定期回收，上限随之释放。
  await sleep(2500);
  const afterSweep = await hello({ nickname: 'lobby-6' });
  assert.equal(afterSweep.response.type, 'welcome', 'expired lobby identities should free the cap');
  const staleToken = afterSweep.response.sessionToken;
  await afterSweep.client.disconnect();

  // 同一令牌在 TTL 之后必须失效；1.5 秒不足以保证周期扫描已运行，
  // 因此这里也验证认证入口自己执行 TTL 检查。
  await sleep(1500);
  const expired = await hello({ sessionToken: staleToken });
  assert.equal(expired.response.type, 'error');
  assert.equal(expired.response.code, 'session_expired');
  await expired.client.disconnect();

  console.log('PASS: lobby session TTL resume, periodic expiry and session cap.');
} finally {
  await server.stop();
}

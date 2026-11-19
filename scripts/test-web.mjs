// Node.js 22+: node scripts/test-web.mjs <path/to/gymj_server.exe>
import assert from 'node:assert/strict';
import { startServer, waitEvent } from './lib/test-server.mjs';

const server = await startServer(process.argv[2]);
try {
  const first = await server.connect();
  const second = await server.connect();
  first.send({ type: 'ping' });
  assert.deepEqual(await first.next(), { v: 1, type: 'pong' });
  const nonce = '你好 <script>test</script>';
  first.send({ type: 'ping', nonce });
  assert.deepEqual(await first.next(), { v: 1, type: 'pong', nonce });
  assert.equal(second.history.length, 0);

  for (const [message, code] of [
    ['{broken', 'invalid_json'], ['[]', 'invalid_json'], ['null', 'invalid_json'],
    ['{}', 'unsupported_version'], ['{"v":1}', 'invalid_type'],
    ['{"v":1,"type":123}', 'invalid_type'],
    ['{"v":1,"type":"ping","nonce":123}', 'invalid_field'],
    ['{"v":1,"type":"unknown"}', 'unknown_type'],
    ['{"v":1,"type":"echo","payload":"legacy"}', 'unknown_type'],
    ['['.repeat(64) + '0' + ']'.repeat(64), 'json_too_deep'],
  ]) {
    first.socket.send(message);
    const response = await first.next();
    assert.equal(response.v, 1);
    assert.equal(response.type, 'error');
    assert.equal(response.code, code);
  }
  first.socket.send(new Uint8Array([1, 2]));
  const binaryError = await first.next();
  assert.equal(binaryError.v, 1);
  assert.equal(binaryError.code, 'text_only');
  await first.barrier();

  for (let i = 0; i < 40; i++) first.send({ type: 'ping', nonce: String(i) });
  const received = [];
  for (let i = 0; i < 40; i++) received.push((await first.next()).nonce);
  assert.deepEqual(received, Array.from({ length: 40 }, (_, i) => String(i)));
  assert.equal((await first.disconnect()).code, 1000);

  const reconnected = await server.connect();
  await reconnected.barrier();
  const oversizedClosed = waitEvent(reconnected.socket, 'close');
  reconnected.socket.send('x'.repeat(70 * 1024));
  await oversizedClosed;
  await second.barrier();
  assert.equal((await second.disconnect()).code, 1000);
  console.log('PASS: v1 ping, concurrent clients, input validation, JSON depth, binary rejection, send order, reconnect, oversized message isolation.');
} finally {
  await server.stop();
}

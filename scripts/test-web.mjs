// Node.js 22+: node scripts/test-web.mjs <path/to/gymj_server.exe>
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import path from 'node:path';

if (!process.argv[2] || typeof WebSocket === 'undefined') {
  throw new Error('Usage (Node.js 22+): node scripts/test-web.mjs <server executable>');
}
const child = spawn(path.resolve(process.argv[2]), ['0'], {
  windowsHide: true,
  stdio: ['ignore', 'pipe', 'pipe'],
});
const sockets = [];
let diagnostics = '';
child.stderr.on('data', data => diagnostics += data);
const exited = new Promise(resolve => {
  child.once('exit', resolve);
  child.once('error', resolve);
});

function waitEvent(socket, name) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { cleanup(); reject(new Error(`Timeout waiting for ${name}`)); }, 5000);
    function success(event) { cleanup(); resolve(event); }
    function failure() { cleanup(); reject(new Error(`Connection failed while waiting for ${name}`)); }
    function cleanup() {
      clearTimeout(timer);
      socket.removeEventListener(name, success);
      socket.removeEventListener('error', failure);
    }
    socket.addEventListener(name, success, { once: true });
    socket.addEventListener('error', failure, { once: true });
  });
}

async function connect(url) {
  const socket = new WebSocket(url);
  sockets.push(socket);
  await waitEvent(socket, 'open');
  return socket;
}

async function request(socket, message) {
  const response = waitEvent(socket, 'message');
  socket.send(message);
  return JSON.parse((await response).data);
}

async function disconnect(socket) {
  const closed = waitEvent(socket, 'close');
  socket.close(1000, 'test complete');
  assert.equal((await closed).code, 1000);
}

try {
  const url = await new Promise((resolve, reject) => {
    let stdout = '';
    const timer = setTimeout(() => finish(new Error('Server startup timeout')), 10000);
    function finish(error, value) {
      clearTimeout(timer);
      child.stdout.off('data', onData);
      child.off('error', onError);
      child.off('exit', onExit);
      if (error) reject(error); else resolve(value);
    }
    function onData(data) {
      stdout += data;
      const match = stdout.match(/ws:\/\/127\.0\.0\.1:\d+\r?\n/);
      if (match) finish(null, match[0].trim());
    }
    function onError(error) { finish(error); }
    function onExit(code) { finish(new Error(`Server exited ${code}: ${diagnostics}`)); }
    child.stdout.on('data', onData);
    child.once('error', onError);
    child.once('exit', onExit);
  });
  const first = await connect(url);
  const second = await connect(url);
  let secondMessages = 0;
  second.addEventListener('message', () => secondMessages++);
  assert.deepEqual(await request(first, '{"type":"ping"}'), { type: 'pong' });
  const payload = '\u4f60\u597d <script>test</script>';
  assert.deepEqual(await request(first, JSON.stringify({ type: 'echo', payload })), { type: 'echo', payload });
  assert.equal(secondMessages, 0);

  for (const [message, code] of [
    ['{broken', 'invalid_json'],
    ['[]', 'invalid_json'],
    ['null', 'invalid_json'],
    ['{}', 'invalid_type'],
    ['{"type":123}', 'invalid_type'],
    ['{"type":"echo","payload":123}', 'invalid_payload'],
    ['{"type":"unknown"}', 'unknown_type'],
    ['['.repeat(64) + '0' + ']'.repeat(64), 'json_too_deep'],
  ]) {
    assert.deepEqual(await request(first, message), { type: 'error', code });
  }
  assert.equal((await request(first, new Uint8Array([1, 2]))).code, 'text_only');
  assert.equal((await request(first, '{"type":"ping"}')).type, 'pong');

  const received = [];
  const burst = new Promise((resolve, reject) => {
    const timer = setTimeout(() => { first.removeEventListener('message', onMessage); reject(new Error('Burst timeout')); }, 5000);
    function onMessage(event) {
      received.push(JSON.parse(event.data).payload);
      if (received.length === 40) {
        clearTimeout(timer);
        first.removeEventListener('message', onMessage);
        resolve();
      }
    }
    first.addEventListener('message', onMessage);
  });
  for (let i = 0; i < 40; i++) first.send(JSON.stringify({ type: 'echo', payload: String(i) }));
  await burst;
  assert.deepEqual(received, Array.from({ length: 40 }, (_, i) => String(i)));
  await disconnect(first);

  const reconnected = await connect(url);
  assert.equal((await request(reconnected, '{"type":"ping"}')).type, 'pong');
  const oversizedClosed = waitEvent(reconnected, 'close');
  reconnected.send('x'.repeat(70 * 1024));
  await oversizedClosed;
  assert.equal((await request(second, '{"type":"ping"}')).type, 'pong');
  await disconnect(second);
  console.log('PASS: concurrent clients, echo/ping, input validation, JSON depth, binary rejection, send order, reconnect, oversized message isolation.');
} finally {
  for (const socket of sockets) {
    if (socket.readyState < WebSocket.CLOSING) socket.close();
  }
  child.kill();
  await exited;
}

// 真实 WebSocket 测试共用的启动器。每次测试使用独立目录，牌谱不污染开发数据。
import { spawn } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

export async function startServer(executable, args = []) {
  if (!executable || typeof WebSocket === 'undefined') {
    throw new Error('Usage (Node.js 22+): node scripts/test-*.mjs <server executable>');
  }
  const temporaryRoot = path.resolve(tmpdir());
  const directory = await mkdtemp(path.join(temporaryRoot, 'gymj-network-'));
  const child = spawn(path.resolve(executable), ['0', ...args], {
    cwd: directory, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
  });
  const clients = [];
  let diagnostics = '';
  child.stderr.on('data', data => diagnostics += data);
  const exited = new Promise(resolve => {
    child.once('exit', resolve);
    child.once('error', resolve);
  });
  async function stop() {
    for (const client of clients) {
      if (client.socket.readyState < WebSocket.CLOSING) client.socket.close();
    }
    child.kill();
    await exited;
    // 只清理本启动器创建的目录，绝不递归删除调用者提供的路径。
    const resolved = path.resolve(directory);
    if (path.dirname(resolved) !== temporaryRoot || !path.basename(resolved).startsWith('gymj-network-')) {
      throw new Error('Unexpected test directory');
    }
    await rm(resolved, { recursive: true, force: true });
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
    return {
      directory, stop,
      async connect() {
        const client = new Client(url);
        clients.push(client);
        await waitEvent(client.socket, 'open');
        return client;
      },
    };
  } catch (error) {
    await stop();
    throw error;
  }
}

export function waitEvent(socket, name) {
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

class Client {
  socket;
  history = [];
  pending = [];
  waiters = [];
  view = null;
  serial = 0;

  constructor(url) {
    this.socket = new WebSocket(url);
    this.socket.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      this.history.push(message);
      if (message.type === 'table_update') this.view = message.view;
      const index = this.waiters.findIndex(waiter => waiter.predicate(message));
      if (index < 0) this.pending.push(message);
      else this.waiters.splice(index, 1)[0].resolve(message);
    });
  }

  send(message) { this.socket.send(JSON.stringify({ v: 1, ...message })); }

  next(predicate = () => true, timeoutMs = 5000) {
    const index = this.pending.findIndex(predicate);
    if (index >= 0) return Promise.resolve(this.pending.splice(index, 1)[0]);
    return new Promise((resolve, reject) => {
      const waiter = { predicate, resolve: value => { clearTimeout(timer); resolve(value); } };
      const timer = setTimeout(() => {
        this.waiters.splice(this.waiters.indexOf(waiter), 1);
        reject(new Error('Timeout waiting for protocol message'));
      }, timeoutMs);
      this.waiters.push(waiter);
    });
  }

  async barrier() {
    // 同一连接发送有序；收到 pong 后，前面已经入队的快照都应到达。
    const nonce = `barrier-${++this.serial}`;
    this.send({ type: 'ping', nonce });
    await this.next(message => message.type === 'pong' && message.nonce === nonce);
  }

  async request(message) {
    const requestTag = `request-${++this.serial}`;
    this.send({ ...message, requestTag });
    return this.next(response => response.requestTag === requestTag);
  }

  async disconnect() {
    const closed = waitEvent(this.socket, 'close');
    this.socket.close(1000, 'test complete');
    return closed;
  }
}

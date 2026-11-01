// Node.js 22+: node scripts/test-multi-room.mjs <path/to/gymj_server.exe>
import assert from 'node:assert/strict';
import { startServer } from './lib/test-server.mjs';

const server = await startServer(process.argv[2], ['--seed', '424242']);
const defaultRoom = '100001';
const alphaRoom = '200001';
const betaRoom = '200002';
const temporaryRoom = '000003';

async function authenticated(name) {
  const client = await server.connect();
  client.send({ type: 'hello', nickname: name });
  const welcome = await client.next(message => message.type === 'welcome');
  assert.equal(welcome.nickname, name);
  assert.equal(welcome.roomId, null);
  return client;
}

async function createRoom(client, roomId) {
  const ack = await client.request({ type: 'create_room', roomId });
  assert.equal(ack.type, 'ack');
  assert.equal(ack.operation, 'create_room');
  assert.equal(ack.accepted, true);
  const update = await client.next(message => message.type === 'table_update' && message.roomId === roomId);
  assert.equal(update.view.tableStage, 'waiting_ready');
}

async function joinRoom(client, roomId) {
  const ack = await client.request({ type: 'join_room', roomId });
  assert.equal(ack.operation, 'join_room');
  assert.equal(ack.accepted, true);
  await client.next(message => message.type === 'table_update' && message.roomId === roomId);
}

async function ready(client, roomId) {
  const ack = await client.request({ type: 'ready', roomId });
  assert.equal(ack.operation, 'ready');
  assert.equal(ack.accepted, true);
}

try {
  const unauthenticated = await server.connect();
  const denied = await unauthenticated.request({ type: 'list_rooms' });
  assert.equal(denied.type, 'error');
  assert.equal(denied.code, 'hello_required');

  const alpha = await authenticated('甲一');
  const initial = await alpha.request({ type: 'list_rooms' });
  assert.deepEqual(initial.rooms.map(room => room.roomId), [defaultRoom]);
  await createRoom(alpha, alphaRoom);

  const validation = await authenticated('验证者');
  const duplicate = await validation.request({ type: 'create_room', roomId: alphaRoom });
  assert.equal(duplicate.accepted, false);
  assert.equal(duplicate.error, 'room_exists');
  const invalid = await validation.request({ type: 'create_room', roomId: 'room2' });
  assert.equal(invalid.accepted, false);
  assert.equal(invalid.error, 'invalid_room_id');

  const temporary = await authenticated('临时房主');
  await createRoom(temporary, temporaryRoom);
  const left = await temporary.request({ type: 'leave_room', roomId: temporaryRoom });
  assert.equal(left.accepted, true);
  await temporary.next(message => message.type === 'server_notice' && message.code === 'room_closed');
  const afterClose = await validation.request({ type: 'list_rooms' });
  assert.equal(afterClose.rooms.some(room => room.roomId === temporaryRoom), false);

  const beta = await authenticated('乙一');
  await createRoom(beta, betaRoom);
  const alphaPlayers = [alpha];
  const betaPlayers = [beta];
  for (let seat = 2; seat <= 4; ++seat) {
    const alphaPlayer = await authenticated(`甲${seat}`);
    const betaPlayer = await authenticated(`乙${seat}`);
    await joinRoom(alphaPlayer, alphaRoom);
    await joinRoom(betaPlayer, betaRoom);
    alphaPlayers.push(alphaPlayer);
    betaPlayers.push(betaPlayer);
  }

  const listed = await validation.request({ type: 'list_rooms' });
  const byId = new Map(listed.rooms.map(room => [room.roomId, room]));
  assert.equal(byId.get(alphaRoom).players, 4);
  assert.equal(byId.get(betaRoom).players, 4);
  assert.equal(byId.get(defaultRoom).players, 0);

  for (const player of alphaPlayers) await ready(player, alphaRoom);
  for (const player of betaPlayers) await ready(player, betaRoom);
  await Promise.all([...alphaPlayers.map(player => player.next(message =>
    message.type === 'table_update' && message.roomId === alphaRoom && message.view.tableStage === 'playing')),
  ...betaPlayers.map(player => player.next(message =>
    message.type === 'table_update' && message.roomId === betaRoom && message.view.tableStage === 'playing'))]);

  for (const player of alphaPlayers) {
    assert.equal(player.history.some(message => message.type === 'table_update' && message.roomId === betaRoom), false);
  }
  for (const player of betaPlayers) {
    assert.equal(player.history.some(message => message.type === 'table_update' && message.roomId === alphaRoom), false);
  }
  const playing = await validation.request({ type: 'list_rooms' });
  const playingById = new Map(playing.rooms.map(room => [room.roomId, room]));
  assert.equal(playingById.get(alphaRoom).playing, true);
  assert.equal(playingById.get(betaRoom).playing, true);

  // 默认房间关闭后，已绑定其他房间的玩家不能通过失败的入房请求重建它。
  const defaultOwner = await authenticated('默认房主');
  await joinRoom(defaultOwner, defaultRoom);
  const defaultLeft = await defaultOwner.request({ type: 'leave_room', roomId: defaultRoom });
  assert.equal(defaultLeft.accepted, true);
  await defaultOwner.next(message => message.type === 'server_notice' && message.code === 'room_closed');
  const beforeRejectedJoin = await validation.request({ type: 'list_rooms' });
  assert.equal(beforeRejectedJoin.rooms.some(room => room.roomId === defaultRoom), false);
  const crossRoomJoin = await alpha.request({ type: 'join_room', roomId: defaultRoom });
  assert.equal(crossRoomJoin.type, 'error');
  assert.equal(crossRoomJoin.code, 'already_in_room');
  const afterRejectedJoin = await validation.request({ type: 'list_rooms' });
  assert.deepEqual(afterRejectedJoin.rooms, beforeRejectedJoin.rooms,
    'rejected join must not create a room or change room occupancy');
  const originalSnapshot = await alpha.request({ type: 'snapshot', roomId: alphaRoom });
  assert.equal(originalSnapshot.accepted, true, 'rejected join preserves the original room binding');

  // 未入座身份仍能正常重建默认房间。
  await joinRoom(validation, defaultRoom);
  const afterRecreation = await validation.request({ type: 'list_rooms' });
  const recreated = afterRecreation.rooms.find(room => room.roomId === defaultRoom);
  assert.equal(recreated.players, 1);
  assert.equal(recreated.connected, 1);
  assert.equal(recreated.playing, false);
  console.log('PASS: room listing, atomic creation, validation, two-table startup, delivery isolation, rejected join without side effects, and default room recreation.');
} finally {
  await server.stop();
}

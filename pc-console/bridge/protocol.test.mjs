import assert from 'node:assert/strict';
import test from 'node:test';
import { buildControlPacket, buildHeartbeat, COMMANDS, PACKET_SIZE } from './protocol.mjs';

test('builds the exact 30-byte little-endian TAKEOFF packet', () => {
  const { packet, checksum } = buildControlPacket('TAKEOFF');
  assert.equal(packet.length, PACKET_SIZE);
  assert.equal(packet.readUInt16LE(0), 0xaa55);
  assert.equal(packet.readUInt8(2), COMMANDS.TAKEOFF);
  assert.equal(packet.readUInt16LE(28), checksum);
});

test('maps waypoint and gimbal fields to the existing AGX packet layout', () => {
  const waypoint = buildControlPacket('CHANGESTO', { latitude: 22.5, longitude: 113.9, altitude: 65, speed: 5 }).packet;
  assert.equal(waypoint.readDoubleLE(4), 22.5);
  assert.equal(waypoint.readDoubleLE(12), 113.9);
  assert.equal(waypoint.readFloatLE(20), 65);
  assert.equal(waypoint.readFloatLE(24), 5);

  const gimbal = buildControlPacket('GIMBAL_ROTATE', { pitch: -30, roll: 1, yaw: 12, time: 0.8 }).packet;
  assert.equal(gimbal.readDoubleLE(4), -30);
  assert.equal(gimbal.readDoubleLE(12), 1);
  assert.equal(gimbal.readFloatLE(20), 12);
  assert.ok(Math.abs(gimbal.readFloatLE(24) - 0.8) < 0.0001);
});

test('rejects unsafe coordinate ranges and creates JSON heartbeat', () => {
  assert.throws(() => buildControlPacket('CHANGESTO', { latitude: 91, longitude: 0, altitude: 20 }), /纬度/);
  const heartbeat = JSON.parse(buildHeartbeat().toString('utf8'));
  assert.equal(heartbeat.msg_type, 'heartbeat');
  assert.equal(heartbeat.status, 'alive');
});

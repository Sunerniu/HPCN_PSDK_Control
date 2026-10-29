export const COMMANDS = Object.freeze({
  TAKEOFF: 0x01,
  LAND: 0x02,
  GOHOME: 0x03,
  CANCEL_GOHOME: 0x04,
  HOVER: 0x05,
  CONFIRM_LAND: 0x06,
  FORCE_LAND: 0x07,
  CANCEL_LAND: 0x08,
  PLANSTO: 0x13,
  CHANGESTO: 0x15,
  HEIGHTTO: 0x17,
  NAV_START: 0x20,
  NAV_STOP: 0x21,
  NAV_PAUSE: 0x22,
  NAV_RESUME: 0x23,
  NAV_CLEAR: 0x24,
  STATUS: 0x30,
  AUTH: 0x31,
  HEARTBEAT: 0x3f,
  GIMBAL_ROTATE: 0x40,
  GIMBAL_RESET: 0x41,
});

const HEADER = 0xaa55;
export const PACKET_SIZE = 30;

function finite(value, fallback = 0) {
  return Number.isFinite(Number(value)) ? Number(value) : fallback;
}

function inRange(value, minimum, maximum, label) {
  if (!Number.isFinite(value) || value < minimum || value > maximum) {
    throw new Error(`${label} 必须在 ${minimum}～${maximum} 范围内`);
  }
}

export function normalizeCommand(name, params = {}) {
  const command = String(name ?? '').toUpperCase();
  if (!(command in COMMANDS)) throw new Error(`不支持的控制指令: ${command || '(空)'}`);

  let latitude = 0;
  let longitude = 0;
  let altitude = 0;
  let speed = 0;

  if (command === 'PLANSTO' || command === 'CHANGESTO') {
    latitude = finite(params.latitude, Number.NaN);
    longitude = finite(params.longitude, Number.NaN);
    altitude = finite(params.altitude, Number.NaN);
    speed = finite(params.speed, 0);
    inRange(latitude, -90, 90, '纬度');
    inRange(longitude, -180, 180, '经度');
    inRange(altitude, 0, 1500, '绝对高度');
    inRange(speed, 0, 30, '航点速度');
  } else if (command === 'HEIGHTTO') {
    altitude = finite(params.altitude, Number.NaN);
    inRange(altitude, 0, 1500, '绝对高度');
  } else if (command === 'GIMBAL_ROTATE') {
    latitude = finite(params.pitch, Number.NaN);
    longitude = finite(params.roll, 0);
    altitude = finite(params.yaw, 0);
    speed = finite(params.time, 0.5);
    inRange(latitude, -180, 180, '云台 pitch');
    inRange(longitude, -180, 180, '云台 roll');
    inRange(altitude, -180, 180, '云台 yaw');
    inRange(speed, 0.1, 30, '云台动作时间');
  }

  return { command, code: COMMANDS[command], latitude, longitude, altitude, speed };
}

export function buildControlPacket(name, params = {}) {
  const normalized = normalizeCommand(name, params);
  const packet = Buffer.alloc(PACKET_SIZE);
  packet.writeUInt16LE(HEADER, 0);
  packet.writeUInt8(normalized.code, 2);
  packet.writeUInt8(0, 3);
  packet.writeDoubleLE(normalized.latitude, 4);
  packet.writeDoubleLE(normalized.longitude, 12);
  packet.writeFloatLE(normalized.altitude, 20);
  packet.writeFloatLE(normalized.speed, 24);

  let checksum = 0;
  for (let index = 0; index < 28; index += 1) checksum = (checksum + packet[index]) & 0xffff;
  packet.writeUInt16LE(checksum, 28);
  return { packet, normalized, checksum };
}

export function buildHeartbeat(uavId = 1) {
  return Buffer.from(JSON.stringify({
    msg_type: 'heartbeat',
    timestamp: Math.floor(Date.now() / 1000),
    uav_id: uavId,
    status: 'alive',
  }));
}

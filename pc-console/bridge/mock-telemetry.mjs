import dgram from 'node:dgram';

const targetPort = Number(process.env.MOCK_TELEMETRY_PORT ?? 14550);
const socket = dgram.createSocket('udp4');
const packet = {
  packet_time: 0,
  GPS_time: null,
  attitude: { pitch: -2.4, roll: 1.2, yaw: 128.7 },
  velocity: { vx: 3.1, vy: 1.4, vz: 0.0 },
  battery: { voltage: 48120, current: -3100, percent: 76, capacity: 12600 },
  flight_status: 2,
  display_mode: 10,
  height: 28.4,
  home_set: 1,
  gimbal: { pitch: -30, roll: 0, yaw: 4.2 },
  position_source: 'RTK Available',
  gps: { valid: true, lat: 22.542812, lon: 113.958902, alt: 53.1, fix_state: 3 },
  rtk: { valid: true, lat: 22.5428164, lon: 113.9589091, alt: 53.2, position_info: 50 },
  navigation: {
    status: 'RUNNING', is_navigating: true, has_target: true, last_error: 0,
    current_target: { lat: 22.5432, lon: 113.9594, alt: 65, speed: 5 },
    queue_count: 2,
    queue: [
      { lat: 22.5436, lon: 113.9598, alt: 65 },
      { lat: 22.544, lon: 113.9602, alt: 68 },
    ],
  },
};

let sent = 0;
const timer = setInterval(() => {
  const now = Date.now();
  packet.packet_time = Math.floor(now / 1000);
  packet.GPS_time = now * 1000;
  packet.attitude.yaw = (packet.attitude.yaw + 0.15) % 360;
  socket.send(Buffer.from(JSON.stringify(packet)), targetPort, '127.0.0.1', (error) => {
    if (error) {
      console.error(error.message);
      process.exitCode = 1;
    }
  });
  sent += 1;
  if (sent >= 40) {
    clearInterval(timer);
    setTimeout(() => {
      console.log(`40 mock telemetry packets sent to udp://127.0.0.1:${targetPort}`);
      socket.close();
    }, 100);
  }
}, 100);

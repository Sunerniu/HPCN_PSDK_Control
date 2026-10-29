import dgram from 'node:dgram';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { buildControlPacket, buildHeartbeat } from './protocol.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const RUNTIME_DIR = path.join(HERE, 'runtime');
const CONFIG_PATH = path.join(RUNTIME_DIR, 'config.json');
const HLS_DIR = path.join(RUNTIME_DIR, 'hls');
const HTTP_HOST = process.env.BRIDGE_HTTP_HOST ?? '127.0.0.1';
const HTTP_PORT = Number(process.env.BRIDGE_HTTP_PORT ?? 4100);
const UI_ORIGIN = process.env.BRIDGE_UI_ORIGIN ?? 'http://localhost:3000';
const TELEMETRY_TIMEOUT_MS = 3000;

const DEFAULT_CONFIG = Object.freeze({
  agxHost: '10.29.174.183',
  tcpPort: 8080,
  commandUdpPort: 14551,
  telemetryPort: 14550,
  rtspPort: 8554,
  rtspPath: '/m350/fpv',
  transport: 'udp',
  heartbeatEnabled: true,
  autoVideo: true,
});

fs.mkdirSync(RUNTIME_DIR, { recursive: true });

function readConfig() {
  try { return validateConfig({ ...DEFAULT_CONFIG, ...JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8')) }); }
  catch { return { ...DEFAULT_CONFIG }; }
}

function validatePort(value, label) {
  const port = Number(value);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error(`${label} 必须是 1～65535 的整数`);
  return port;
}

function validateConfig(input) {
  const agxHost = String(input.agxHost ?? '').trim();
  if (!/^[a-zA-Z0-9.-]+$/.test(agxHost)) throw new Error('AGX 地址格式无效');
  const rtspPath = String(input.rtspPath ?? '').trim();
  if (!/^\/[a-zA-Z0-9._/-]+$/.test(rtspPath) || rtspPath.includes('..')) throw new Error('RTSP 路径格式无效');
  const transport = input.transport === 'tcp' ? 'tcp' : input.transport === 'udp' ? 'udp' : null;
  if (!transport) throw new Error('控制传输只能选择 UDP 或 TCP');
  return {
    agxHost,
    tcpPort: validatePort(input.tcpPort, 'TCP 端口'),
    commandUdpPort: validatePort(input.commandUdpPort, 'UDP 命令端口'),
    telemetryPort: validatePort(input.telemetryPort, '遥测端口'),
    rtspPort: validatePort(input.rtspPort, 'RTSP 端口'),
    rtspPath,
    transport,
    heartbeatEnabled: Boolean(input.heartbeatEnabled),
    autoVideo: Boolean(input.autoVideo),
  };
}

let config = readConfig();
let latestTelemetry = null;
let latestTelemetryAt = 0;
let telemetrySocket = null;
let udpCommandSocket = dgram.createSocket('udp4');
let tcpSocket = null;
let tcpConnecting = null;
let tcpReconnectTimer = null;
let ffmpegProcess = null;
let videoRestartTimer = null;
let videoGeneration = 0;
let lastHeartbeatErrorLogAt = 0;
let videoFailureCount = 0;
let lastVideoErrorLogAt = 0;
let shuttingDown = false;
let sseClients = new Set();
let logs = [];
let video = { state: 'stopped', ready: false, error: '' };
const ffmpegAvailable = spawnSync('ffmpeg', ['-version'], { stdio: 'ignore' }).status === 0;

function log(level, message) {
  const entry = { id: `${Date.now()}-${Math.random().toString(16).slice(2)}`, at: Date.now(), level, message };
  logs = [entry, ...logs].slice(0, 100);
  console.log(`[${new Date(entry.at).toISOString()}] ${level.toUpperCase()} ${message}`);
  broadcast('log', entry);
}

function currentStatus() {
  const age = latestTelemetryAt ? Date.now() - latestTelemetryAt : null;
  return {
    bridgeOnline: true,
    telemetryOnline: age !== null && age <= TELEMETRY_TIMEOUT_MS,
    telemetryAgeMs: age,
    tcpConnected: Boolean(tcpSocket && !tcpSocket.destroyed),
    ffmpegAvailable,
    video,
    config,
    latestTelemetry,
    logs,
  };
}

function broadcast(type, data) {
  const payload = `data: ${JSON.stringify({ type, data })}\n\n`;
  for (const response of sseClients) response.write(payload);
}

function mergeTelemetry(previous, next) {
  if (!previous) return next;
  const merged = { ...previous, ...next };
  for (const key of ['gps', 'rtk', 'attitude', 'velocity', 'battery', 'gimbal', 'navigation', 'precise_position']) {
    if (previous[key] && next[key] && !Array.isArray(next[key])) merged[key] = { ...previous[key], ...next[key] };
  }
  return merged;
}

function writeJson(response, status, body) {
  response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...corsHeaders() });
  response.end(JSON.stringify(body));
}

function corsHeaders() {
  return {
    'Access-Control-Allow-Origin': UI_ORIGIN,
    'Access-Control-Allow-Methods': 'GET,POST,PUT,OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
  };
}

async function readBody(request) {
  let text = '';
  for await (const chunk of request) {
    text += chunk;
    if (text.length > 65536) throw new Error('请求内容过大');
  }
  return text ? JSON.parse(text) : {};
}

function startTelemetryListener() {
  if (telemetrySocket) telemetrySocket.close();
  telemetrySocket = dgram.createSocket({ type: 'udp4', reuseAddr: true });
  telemetrySocket.on('message', (message, remote) => {
    try {
      const parsed = JSON.parse(message.toString('utf8'));
      latestTelemetry = mergeTelemetry(latestTelemetry, parsed);
      latestTelemetryAt = Date.now();
      broadcast('telemetry', latestTelemetry);
    } catch { log('warning', `忽略来自 ${remote.address} 的非 JSON 遥测包`); }
  });
  telemetrySocket.on('error', (error) => log('error', `遥测监听错误: ${error.message}`));
  telemetrySocket.bind(config.telemetryPort, '0.0.0.0', () => log('success', `正在监听 UDP 遥测 0.0.0.0:${config.telemetryPort}`));
}

function closeTcp() {
  if (tcpReconnectTimer) clearTimeout(tcpReconnectTimer);
  tcpReconnectTimer = null;
  if (tcpSocket) tcpSocket.destroy();
  tcpSocket = null;
  tcpConnecting = null;
}

function scheduleTcpReconnect() {
  if (shuttingDown || config.transport !== 'tcp' || tcpReconnectTimer) return;
  tcpReconnectTimer = setTimeout(() => { tcpReconnectTimer = null; ensureTcp().catch(() => undefined); }, 1500);
}

function ensureTcp() {
  if (tcpSocket && !tcpSocket.destroyed) return Promise.resolve(tcpSocket);
  if (tcpConnecting) return tcpConnecting;
  tcpConnecting = new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: config.agxHost, port: config.tcpPort });
    const timeout = setTimeout(() => socket.destroy(new Error('TCP 连接超时')), 1800);
    socket.setNoDelay(true);
    socket.setKeepAlive(true, 1000);
    socket.once('connect', () => {
      clearTimeout(timeout); tcpSocket = socket; tcpConnecting = null;
      log('success', `TCP 控制长连接已建立 ${config.agxHost}:${config.tcpPort}`);
      resolve(socket); broadcast('status', currentStatus());
    });
    socket.once('error', (error) => {
      clearTimeout(timeout); tcpConnecting = null;
      if (tcpSocket === socket) tcpSocket = null;
      log('warning', `TCP 控制链路不可用: ${error.message}`);
      reject(error); scheduleTcpReconnect();
    });
    socket.on('close', () => {
      if (tcpSocket === socket) {
        tcpSocket = null;
        log('warning', 'TCP 控制长连接已断开');
        broadcast('status', currentStatus());
        scheduleTcpReconnect();
      }
    });
  });
  return tcpConnecting;
}

function sendUdp(buffer) {
  return new Promise((resolve, reject) => {
    udpCommandSocket.send(buffer, config.commandUdpPort, config.agxHost, (error) => error ? reject(error) : resolve());
  });
}

async function sendCommand(command, params) {
  const { packet, normalized } = buildControlPacket(command, params);
  if (config.transport === 'udp') await sendUdp(packet);
  else {
    const socket = await ensureTcp();
    await new Promise((resolve, reject) => socket.write(packet, (error) => error ? reject(error) : resolve()));
  }
  log('success', `${normalized.command} 指令已通过 ${config.transport.toUpperCase()} 写入网络（AGX 协议无 ACK）`);
  return normalized;
}

function sendHeartbeat() {
  if (!config.heartbeatEnabled || shuttingDown) return;
  sendUdp(buildHeartbeat())
    .then(() => undefined)
    .catch((error) => {
      const now = Date.now();
      if (now - lastHeartbeatErrorLogAt >= 15000) {
        lastHeartbeatErrorLogAt = now;
        log('warning', `心跳发送失败: ${error.message}`);
      }
    });
}

function rtspUrl() { return `rtsp://${config.agxHost}:${config.rtspPort}${config.rtspPath}`; }

function stopVideo(manual = true) {
  videoGeneration += 1;
  if (videoRestartTimer) clearTimeout(videoRestartTimer);
  videoRestartTimer = null;
  if (ffmpegProcess) {
    const processToStop = ffmpegProcess;
    ffmpegProcess = null;
    processToStop.kill('SIGTERM');
    setTimeout(() => { if (processToStop.exitCode === null) processToStop.kill('SIGKILL'); }, 1500).unref();
  }
  video = { state: 'stopped', ready: false, error: manual ? '' : video.error };
  broadcast('status', currentStatus());
}

function scheduleVideoRestart() {
  if (shuttingDown || !config.autoVideo || videoRestartTimer) return;
  const delay = Math.min(30000, 4000 * (2 ** Math.min(videoFailureCount, 3)));
  videoRestartTimer = setTimeout(() => { videoRestartTimer = null; startVideo(false); }, delay);
}

function startVideo(manual = false) {
  if (manual) videoFailureCount = 0;
  stopVideo(false);
  const generation = ++videoGeneration;
  if (!ffmpegAvailable) {
    video = { state: 'error', ready: false, error: 'PC 未找到 FFmpeg' };
    log('error', '未找到 FFmpeg，无法将 RTSP 桥接为浏览器视频');
    return;
  }
  fs.rmSync(HLS_DIR, { recursive: true, force: true });
  fs.mkdirSync(HLS_DIR, { recursive: true });
  video = { state: 'starting', ready: false, error: '' };
  const playlist = path.join(HLS_DIR, 'index.m3u8');
  const segmentPattern = path.join(HLS_DIR, 'segment-%06d.ts');
  const args = [
    '-hide_banner', '-loglevel', 'warning', '-rtsp_transport', 'tcp', '-timeout', '5000000',
    '-i', rtspUrl(), '-map', '0:v:0', '-an', '-c:v', 'copy',
    '-f', 'hls', '-hls_time', '1', '-hls_list_size', '4',
    '-hls_flags', 'delete_segments+append_list+omit_endlist+independent_segments',
    '-hls_segment_filename', segmentPattern, playlist,
  ];
  ffmpegProcess = spawn('ffmpeg', args, { stdio: ['ignore', 'ignore', 'pipe'] });
  let lastError = '';
  ffmpegProcess.stderr.on('data', (chunk) => { lastError = chunk.toString('utf8').trim().split('\n').at(-1) ?? ''; });
  const readyTimer = setInterval(() => {
    if (generation !== videoGeneration) return clearInterval(readyTimer);
    if (fs.existsSync(playlist)) {
      clearInterval(readyTimer);
      video = { state: 'running', ready: true, error: '' };
      videoFailureCount = 0;
      log('success', `视频桥接已就绪 ${rtspUrl()}`);
      broadcast('status', currentStatus());
    }
  }, 250);
  ffmpegProcess.once('exit', (code) => {
    clearInterval(readyTimer);
    if (generation !== videoGeneration) return;
    ffmpegProcess = null;
    video = { state: 'error', ready: false, error: lastError || `FFmpeg 退出 (${code ?? 'signal'})` };
    videoFailureCount += 1;
    if (Date.now() - lastVideoErrorLogAt >= 15000) {
      lastVideoErrorLogAt = Date.now();
      log('warning', `RTSP 视频桥接已停止: ${video.error}`);
    }
    broadcast('status', currentStatus());
    scheduleVideoRestart();
  });
  ffmpegProcess.once('error', (error) => {
    clearInterval(readyTimer);
    if (generation !== videoGeneration) return;
    video = { state: 'error', ready: false, error: error.message };
    log('error', `FFmpeg 启动失败: ${error.message}`);
  });
  broadcast('status', currentStatus());
}

function serveStreamFile(urlPath, response) {
  const fileName = path.basename(urlPath);
  if (!/^(index\.m3u8|segment-\d+\.ts)$/.test(fileName)) return writeJson(response, 404, { error: '视频片段不存在' });
  const filePath = path.join(HLS_DIR, fileName);
  if (!fs.existsSync(filePath)) return writeJson(response, 404, { error: '视频尚未就绪' });
  response.writeHead(200, {
    'Content-Type': fileName.endsWith('.m3u8') ? 'application/vnd.apple.mpegurl' : 'video/mp2t',
    'Cache-Control': 'no-store, no-cache, must-revalidate',
    ...corsHeaders(),
  });
  fs.createReadStream(filePath).pipe(response);
}

async function handleRequest(request, response) {
  const url = new URL(request.url, `http://${request.headers.host ?? 'localhost'}`);
  const requestOrigin = request.headers.origin;
  if (requestOrigin && requestOrigin !== UI_ORIGIN) return writeJson(response, 403, { error: '不允许的页面来源' });
  if (request.method === 'OPTIONS') { response.writeHead(204, corsHeaders()); return response.end(); }
  try {
    if (request.method === 'GET' && url.pathname === '/api/status') return writeJson(response, 200, currentStatus());
    if (request.method === 'GET' && url.pathname === '/api/events') {
      response.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', Connection: 'keep-alive', ...corsHeaders() });
      response.write(`data: ${JSON.stringify({ type: 'status', data: currentStatus() })}\n\n`);
      sseClients.add(response);
      request.on('close', () => sseClients.delete(response));
      return;
    }
    if (request.method === 'POST' && url.pathname === '/api/command') {
      const body = await readBody(request);
      const normalized = await sendCommand(body.command, body.params);
      return writeJson(response, 200, { ok: true, command: normalized, acknowledged: false });
    }
    if (request.method === 'PUT' && url.pathname === '/api/config') {
      const previous = config;
      config = validateConfig(await readBody(request));
      fs.writeFileSync(CONFIG_PATH, `${JSON.stringify(config, null, 2)}\n`);
      closeTcp();
      if (previous.telemetryPort !== config.telemetryPort) startTelemetryListener();
      if (config.transport === 'tcp') ensureTcp().catch(() => undefined);
      if (config.autoVideo) startVideo(true); else stopVideo(true);
      log('info', `通信配置已更新，AGX=${config.agxHost}，控制=${config.transport.toUpperCase()}`);
      return writeJson(response, 200, { ok: true, status: currentStatus() });
    }
    if (request.method === 'POST' && url.pathname === '/api/video/restart') {
      startVideo(true); return writeJson(response, 202, { ok: true });
    }
    if (request.method === 'GET' && url.pathname.startsWith('/stream/')) return serveStreamFile(url.pathname, response);
    return writeJson(response, 404, { error: '接口不存在' });
  } catch (error) {
    log('error', error instanceof Error ? error.message : '未知请求错误');
    return writeJson(response, 400, { error: error instanceof Error ? error.message : '请求处理失败' });
  }
}

const server = http.createServer((request, response) => handleRequest(request, response));
server.listen(HTTP_PORT, HTTP_HOST, () => {
  log('success', `PC 通信桥接服务已启动 http://${HTTP_HOST}:${HTTP_PORT}`);
  log('info', `AGX 目标 ${config.agxHost}，控制 ${config.transport.toUpperCase()}，RTSP ${rtspUrl()}`);
});

startTelemetryListener();
if (config.transport === 'tcp') ensureTcp().catch(() => undefined);
if (config.autoVideo) startVideo(true);
const heartbeatTimer = setInterval(sendHeartbeat, 1000);
const statusTimer = setInterval(() => broadcast('status', currentStatus()), 1000);

function shutdown() {
  if (shuttingDown) return;
  shuttingDown = true;
  clearInterval(heartbeatTimer); clearInterval(statusTimer);
  closeTcp(); stopVideo(true);
  if (telemetrySocket) telemetrySocket.close();
  udpCommandSocket.close();
  for (const response of sseClients) response.end();
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 1500).unref();
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);

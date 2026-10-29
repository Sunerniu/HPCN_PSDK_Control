'use client';

import {
  Activity, Antenna, BatteryMedium, Camera, ChevronRight, CircleGauge,
  Clock3, Crosshair, Gauge, House, MapPin, Navigation, Pause, PlaneLanding,
  PlaneTakeoff, Play, Radio, RotateCcw, Satellite, Settings, ShieldAlert,
  SlidersHorizontal, Square, Video, X, Zap,
} from 'lucide-react';
import { FormEvent, useEffect, useMemo, useRef, useState } from 'react';

const BRIDGE_URL = process.env.NEXT_PUBLIC_BRIDGE_URL ?? 'http://127.0.0.1:4100';
type NumericMap = Record<string, number>;

type Telemetry = {
  packet_time?: number; GPS_time?: number | null; position_source?: string;
  gps?: { valid?: boolean; lat?: number; lon?: number; alt?: number; fix_state?: number };
  rtk?: { valid?: boolean; lat?: number; lon?: number; alt?: number; position_info?: number };
  attitude?: NumericMap; velocity?: NumericMap; battery?: NumericMap; gimbal?: NumericMap;
  height?: number; flight_status?: number; display_mode?: number; home_set?: number;
  navigation?: {
    status?: string; is_navigating?: boolean; has_target?: boolean; last_error?: number;
    current_target?: { lat?: number; lon?: number; alt?: number; speed?: number };
    queue_count?: number; queue?: Array<{ lat?: number; lon?: number; alt?: number }>;
  };
};

type BridgeConfig = {
  agxHost: string; tcpPort: number; commandUdpPort: number; telemetryPort: number;
  rtspPort: number; rtspPath: string; transport: 'udp' | 'tcp';
  heartbeatEnabled: boolean; autoVideo: boolean;
};
type EventLog = { id: string; at: number; level: 'info' | 'success' | 'warning' | 'error'; message: string };
type BridgeStatus = {
  bridgeOnline: boolean; telemetryOnline: boolean; telemetryAgeMs: number | null;
  tcpConnected: boolean; ffmpegAvailable: boolean;
  video: { state: string; ready: boolean; error?: string };
  config: BridgeConfig; latestTelemetry: Telemetry | null; logs: EventLog[];
};
type PendingAction = { command: string; title: string; detail: string; tone?: 'danger' | 'normal'; params?: Record<string, number> };

const DEFAULT_CONFIG: BridgeConfig = {
  agxHost: '10.29.174.183', tcpPort: 8080, commandUdpPort: 14551,
  telemetryPort: 14550, rtspPort: 8554, rtspPath: '/m350/fpv',
  transport: 'udp', heartbeatEnabled: true, autoVideo: true,
};
const INITIAL_STATUS: BridgeStatus = {
  bridgeOnline: false, telemetryOnline: false, telemetryAgeMs: null, tcpConnected: false,
  ffmpegAvailable: false, video: { state: 'stopped', ready: false },
  config: DEFAULT_CONFIG, latestTelemetry: null, logs: [],
};
const COMMAND_COPY: Record<string, Pick<PendingAction, 'title' | 'detail' | 'tone'>> = {
  TAKEOFF: { title: '确认自动起飞', detail: '飞控将执行 DJI 默认起飞流程。请确认桨叶区域净空、定位正常且已获得控制权。' },
  LAND: { title: '确认自动降落', detail: '当前导航任务会被清除，飞机将进入自动降落流程。' },
  GOHOME: { title: '确认返航', detail: '当前导航任务会被清除，飞机将按照已设置的返航高度执行返航。' },
  FORCE_LAND: { title: '确认强制降落', detail: '强制降落会忽略部分智能降落检查，仅应在紧急情况下使用。', tone: 'danger' },
  NAV_CLEAR: { title: '清空导航任务', detail: '当前目标和航点队列将被清空，同时发送悬停命令。' },
};

function number(value: unknown, digits = 1, fallback = '—') {
  return typeof value === 'number' && Number.isFinite(value) ? value.toFixed(digits) : fallback;
}
function formatClock(epoch?: number) {
  return epoch ? new Date(epoch * 1000).toLocaleTimeString('zh-CN', { hour12: false }) : '--:--:--';
}
function formatLogTime(epoch: number) { return new Date(epoch).toLocaleTimeString('zh-CN', { hour12: false }); }
async function api(path: string, init?: RequestInit) {
  const response = await fetch(`${BRIDGE_URL}${path}`, { ...init, headers: { 'Content-Type': 'application/json', ...(init?.headers ?? {}) } });
  const body = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(body.error ?? `请求失败 (${response.status})`);
  return body;
}

function VideoSurface({ status, onRestart }: { status: BridgeStatus; onRestart: () => void }) {
  const videoRef = useRef<HTMLVideoElement>(null);
  const [playerError, setPlayerError] = useState('');
  useEffect(() => {
    const video = videoRef.current;
    if (!video || !status.video.ready) return;
    const source = `${BRIDGE_URL}/stream/index.m3u8?ts=${Date.now()}`;
    setPlayerError('');
    if (video.canPlayType('application/vnd.apple.mpegurl')) {
      video.src = source; video.play().catch(() => undefined);
      return () => { video.pause(); video.removeAttribute('src'); };
    }
    let disposed = false;
    let hlsPlayer: import('hls.js').default | null = null;
    void import('hls.js').then(({ default: Hls }) => {
      if (disposed) return;
      if (!Hls.isSupported()) return setPlayerError('当前浏览器不支持 HLS 播放');
      hlsPlayer = new Hls({ lowLatencyMode: true, liveSyncDurationCount: 2, liveMaxLatencyDurationCount: 5, maxLiveSyncPlaybackRate: 1.25 });
      hlsPlayer.loadSource(source); hlsPlayer.attachMedia(video);
      hlsPlayer.on(Hls.Events.MANIFEST_PARSED, () => video.play().catch(() => undefined));
      hlsPlayer.on(Hls.Events.ERROR, (_, data) => { if (data.fatal) setPlayerError('视频播放链路中断，请重试'); });
    });
    return () => { disposed = true; hlsPlayer?.destroy(); };
  }, [status.video.ready]);
  return <section className="video-card panel"><div className="video-stage">
    <video ref={videoRef} muted autoPlay playsInline aria-label="M350 FPV 实时视频" />
    {!status.video.ready && <div className="video-empty"><div className="video-empty-icon"><Video size={28} /></div>
      <strong>{status.video.state === 'starting' ? '正在建立视频链路' : '等待 FPV 视频'}</strong>
      <span>{status.video.error || 'RTSP 将由 PC 桥接为浏览器可播放的低延迟 HLS'}</span>
      <button className="ghost-button" onClick={onRestart}><RotateCcw size={15} />重新连接视频</button></div>}
    {playerError && <div className="video-error">{playerError}</div>}
    <div className="video-topbar"><span className={`live-pill ${status.video.ready ? 'is-live' : ''}`}><span /> LIVE · FPV</span><span className="stream-meta">640×360 · 10 FPS · 800 Kbps</span></div>
    <div className="reticle" aria-hidden="true"><span /><i /></div>
    <div className="video-bottom"><span><Camera size={14} /> M350 / FPV</span><span>{status.video.ready ? '硬件转码链路正常' : '视频未就绪'}</span></div>
  </div></section>;
}

function Metric({ label, value, unit, icon: Icon, tone }: { label: string; value: string; unit?: string; icon: typeof Gauge; tone?: string }) {
  return <div className={`metric ${tone ?? ''}`}><div className="metric-icon"><Icon size={17} /></div><div><span>{label}</span><strong>{value}<small>{unit}</small></strong></div></div>;
}

export default function Home() {
  const [status, setStatus] = useState<BridgeStatus>(INITIAL_STATUS);
  const [logs, setLogs] = useState<EventLog[]>([]);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [routeOpen, setRouteOpen] = useState(false);
  const [pending, setPending] = useState<PendingAction | null>(null);
  const [busy, setBusy] = useState(false);
  const [toast, setToast] = useState('');
  const [clock, setClock] = useState<Date | null>(null);
  const [configDraft, setConfigDraft] = useState<BridgeConfig>(DEFAULT_CONFIG);
  const [route, setRoute] = useState({ latitude: '', longitude: '', altitude: '', speed: '5' });
  const [routeMode, setRouteMode] = useState<'replace' | 'append'>('replace');
  const [height, setHeight] = useState('');
  const [gimbalPitch, setGimbalPitch] = useState('-30');
  const telemetry = status.latestTelemetry;
  const position = telemetry?.rtk?.valid ? telemetry.rtk : telemetry?.gps;
  const velocity = telemetry?.velocity;
  const speed = velocity ? Math.hypot(velocity.vx ?? 0, velocity.vy ?? 0) : undefined;
  const navStatus = telemetry?.navigation?.status ?? 'IDLE';
  const battery = telemetry?.battery?.percent;
  const connected = status.telemetryOnline;
  const attitudeStyle = useMemo(() => ({ transform: `translateY(${Math.max(-22, Math.min(22, (telemetry?.attitude?.pitch ?? 0) * 1.2))}px) rotate(${-(telemetry?.attitude?.roll ?? 0)}deg)` }), [telemetry?.attitude?.pitch, telemetry?.attitude?.roll]);

  useEffect(() => {
    const timer = window.setInterval(() => setClock(new Date()), 1000);
    api('/api/status').then((next) => { setStatus(next); setLogs(next.logs ?? []); setConfigDraft(next.config); }).catch(() => undefined);
    const source = new EventSource(`${BRIDGE_URL}/api/events`);
    source.onmessage = (event) => {
      const payload = JSON.parse(event.data);
      if (payload.type === 'status') setStatus(payload.data);
      if (payload.type === 'telemetry') setStatus((current) => ({ ...current, telemetryOnline: true, telemetryAgeMs: 0, latestTelemetry: payload.data }));
      if (payload.type === 'log') setLogs((current) => [payload.data, ...current].slice(0, 80));
    };
    source.onerror = () => setStatus((current) => ({ ...current, bridgeOnline: false }));
    return () => { window.clearInterval(timer); source.close(); };
  }, []);
  useEffect(() => { if (!toast) return; const timer = window.setTimeout(() => setToast(''), 2800); return () => window.clearTimeout(timer); }, [toast]);

  const executeCommand = async (command: string, params?: Record<string, number>) => {
    setBusy(true);
    try { await api('/api/command', { method: 'POST', body: JSON.stringify({ command, params }) }); setToast(`指令已发送 · ${command}`); setPending(null); }
    catch (error) { setToast(error instanceof Error ? error.message : '指令发送失败'); }
    finally { setBusy(false); }
  };
  const requestCommand = (command: string, params?: Record<string, number>) => {
    const copy = COMMAND_COPY[command]; if (copy) setPending({ command, params, ...copy }); else executeCommand(command, params);
  };
  const submitRoute = (event: FormEvent) => {
    event.preventDefault();
    const params = { latitude: Number(route.latitude), longitude: Number(route.longitude), altitude: Number(route.altitude), speed: Number(route.speed) };
    if (!Number.isFinite(params.latitude) || !Number.isFinite(params.longitude) || !Number.isFinite(params.altitude)) return setToast('请填写有效的航点坐标和绝对高度');
    const replacing = routeMode === 'replace';
    setPending({
      command: replacing ? 'CHANGESTO' : 'PLANSTO', params,
      title: replacing ? '确认切换目标航点' : '确认追加航点',
      detail: `目标 ${params.latitude.toFixed(6)}, ${params.longitude.toFixed(6)}，绝对高度 ${params.altitude.toFixed(1)} m。${replacing ? '当前导航目标将被替换。' : '目标将追加到当前任务队列。'}`,
    });
  };
  const saveConfig = async (event: FormEvent) => {
    event.preventDefault(); setBusy(true);
    try { const next = await api('/api/config', { method: 'PUT', body: JSON.stringify(configDraft) }); setStatus(next.status); setSettingsOpen(false); setToast('通信配置已保存并重新连接'); }
    catch (error) { setToast(error instanceof Error ? error.message : '配置保存失败'); }
    finally { setBusy(false); }
  };
  const restartVideo = () => api('/api/video/restart', { method: 'POST' }).then(() => setToast('正在重新建立视频链路')).catch((error) => setToast(error.message));

  return <main className="app-shell">
    <header className="topbar"><div className="brand"><div className="brand-mark"><Navigation size={19} /></div><div><strong>AEROLINK</strong><span>M350 任务控制台</span></div></div>
      <div className="connection-strip"><span className={`status-dot ${connected ? 'online' : ''}`} /><div><strong>{connected ? '飞行器在线' : '等待遥测'}</strong><span>{status.config.agxHost} · {status.config.transport.toUpperCase()}</span></div><i />
        <div className="link-stat"><Radio size={14} /><span>遥测</span><strong>{connected ? `${Math.max(0, status.telemetryAgeMs ?? 0)} ms` : '离线'}</strong></div><div className="link-stat"><Video size={14} /><span>视频</span><strong>{status.video.ready ? '已连接' : '未连接'}</strong></div></div>
      <div className="top-actions"><div className="utc-clock"><span>LOCAL</span><strong>{clock ? clock.toLocaleTimeString('zh-CN', { hour12: false }) : '--:--:--'}</strong></div><button className="icon-button" aria-label="打开通信设置" onClick={() => { setConfigDraft(status.config); setSettingsOpen(true); }}><Settings size={18} /></button></div>
    </header>

    <div className="workspace"><section className="primary-column"><VideoSurface status={status} onRestart={restartVideo} />
      <section className="flight-strip panel"><div className="horizon-widget"><div className="horizon-sphere"><div className="horizon-plane" style={attitudeStyle}><span className="sky" /><span className="earth" /><i /></div><div className="aircraft-mark"><span /><b /></div></div><div><span>姿态仪</span><strong>{number(telemetry?.attitude?.yaw, 0)}°</strong><small>航向</small></div></div>
        <Metric label="相对高度" value={number(telemetry?.height, 1)} unit="m" icon={Gauge} /><Metric label="水平速度" value={number(speed, 1)} unit="m/s" icon={CircleGauge} />
        <Metric label="电池电量" value={number(battery, 0)} unit="%" icon={BatteryMedium} tone={typeof battery === 'number' && battery < 25 ? 'warning' : ''} /><Metric label="定位来源" value={telemetry?.position_source ?? '—'} icon={Satellite} tone={telemetry?.rtk?.valid ? 'success' : ''} /></section>

      <section className="mission-grid"><div className="mission-card panel"><div className="section-heading"><div><span>MISSION</span><h2>任务与航点</h2></div><button className="text-button" onClick={() => setRouteOpen(true)}>规划航点 <ChevronRight size={15} /></button></div>
        <div className="mission-status"><div className={`mission-orbit ${navStatus === 'RUNNING' ? 'running' : ''}`}><Navigation size={22} /></div><div><span>当前任务状态</span><strong>{navStatus}</strong><small>{telemetry?.navigation?.has_target ? '正在执行目标航点' : '没有活动目标'}</small></div><div className="queue-count"><strong>{telemetry?.navigation?.queue_count ?? 0}</strong><span>待执行航点</span></div></div>
        <div className="target-row"><div><MapPin size={15} /><span>当前目标</span></div><code>{telemetry?.navigation?.has_target ? `${number(telemetry.navigation.current_target?.lat, 6)}, ${number(telemetry.navigation.current_target?.lon, 6)}` : '尚未设置'}</code><span>{telemetry?.navigation?.has_target ? `${number(telemetry.navigation.current_target?.alt, 1)} m` : '—'}</span></div>
        <div className="mission-actions"><button onClick={() => requestCommand('NAV_RESUME')}><Play size={15} />启动 / 恢复</button><button onClick={() => requestCommand('NAV_STOP')}><Pause size={15} />暂停并悬停</button><button onClick={() => requestCommand('NAV_CLEAR')}><Square size={14} />清空任务</button></div></div>
        <div className="location-card panel"><div className="section-heading"><div><span>POSITION</span><h2>实时位置</h2></div><span className={`fix-badge ${telemetry?.rtk?.valid ? 'rtk' : ''}`}>{telemetry?.rtk?.valid ? 'RTK FIX' : telemetry?.gps?.valid ? 'GPS 3D' : 'NO FIX'}</span></div>
          <div className="coordinate"><span>LATITUDE</span><strong>{number(position?.lat, 7)}</strong></div><div className="coordinate"><span>LONGITUDE</span><strong>{number(position?.lon, 7)}</strong></div><div className="location-footer"><span><Crosshair size={14} />绝对海拔 <strong>{number(position?.alt, 1)} m</strong></span><span><House size={14} />返航点 <strong>{telemetry?.home_set ? '已设置' : '未知'}</strong></span></div></div>
      </section></section>

      <aside className="control-column"><section className="panel control-panel"><div className="section-heading"><div><span>FLIGHT CONTROL</span><h2>飞行控制</h2></div><ShieldAlert size={18} /></div>
        <div className="primary-actions"><button className="action-takeoff" onClick={() => requestCommand('TAKEOFF')}><PlaneTakeoff size={21} /><span><strong>自动起飞</strong><small>DJI 默认起飞流程</small></span></button><button className="action-land" onClick={() => requestCommand('LAND')}><PlaneLanding size={21} /><span><strong>自动降落</strong><small>清除任务并降落</small></span></button></div>
        <button className="hover-button" onClick={() => requestCommand('HOVER')}><Square size={15} fill="currentColor" />立即悬停</button><div className="command-grid"><button onClick={() => requestCommand('GOHOME')}><House size={17} /><span>返航</span></button><button onClick={() => requestCommand('CANCEL_GOHOME')}><X size={17} /><span>取消返航</span></button><button onClick={() => requestCommand('CONFIRM_LAND')}><ChevronRight size={17} /><span>确认降落</span></button><button className="danger-outline" onClick={() => requestCommand('FORCE_LAND')}><ShieldAlert size={17} /><span>强制降落</span></button></div></section>

        <section className="panel precision-panel"><div className="section-heading"><div><span>PRECISION</span><h2>精准控制</h2></div><SlidersHorizontal size={18} /></div>
          <form className="height-form" onSubmit={(event) => { event.preventDefault(); const altitude = Number(height); if (!Number.isFinite(altitude)) return setToast('请输入有效绝对高度'); setPending({ command: 'HEIGHTTO', params: { altitude }, title: '确认调整绝对高度', detail: `保持水平位置和航向，调整到绝对海拔 ${altitude.toFixed(1)} m。` }); }}><label><span>目标绝对海拔</span><div><input value={height} onChange={(e) => setHeight(e.target.value)} inputMode="decimal" placeholder="65.0" /><i>m</i></div></label><button type="submit">执行定高</button></form>
          <div className="gimbal-control"><div><span>云台俯仰</span><strong>{gimbalPitch}°</strong></div><input type="range" min="-90" max="30" step="1" value={gimbalPitch} onChange={(e) => setGimbalPitch(e.target.value)} /><div className="gimbal-actions"><button onClick={() => requestCommand('GIMBAL_ROTATE', { pitch: Number(gimbalPitch), roll: 0, yaw: 0, time: 0.8 })}><Camera size={15} />转到角度</button><button onClick={() => requestCommand('GIMBAL_RESET')}><RotateCcw size={15} />复位</button></div></div></section>

        <section className="panel event-panel"><div className="section-heading"><div><span>SYSTEM LOG</span><h2>链路事件</h2></div><Activity size={18} /></div><div className="event-list">{logs.length === 0 ? <div className="empty-events"><Antenna size={18} />等待 PC 桥接服务事件</div> : logs.slice(0, 6).map((log) => <div className={`event-row ${log.level}`} key={log.id}><span /><time>{formatLogTime(log.at)}</time><p>{log.message}</p></div>)}</div></section>
      </aside></div>

    <footer className="statusbar"><span><Clock3 size={13} />飞机时间 {telemetry?.GPS_time ? formatClock(telemetry.GPS_time / 1_000_000) : '未同步'}</span><span><Zap size={13} />控制链路 {status.config.transport.toUpperCase()} · {status.config.transport === 'tcp' ? status.tcpConnected ? '已连接' : '断开' : '无连接模式'}</span><span className="statusbar-right">最后遥测 {status.telemetryAgeMs == null ? '—' : `${status.telemetryAgeMs} ms 前`}</span></footer>

    {settingsOpen && <div className="overlay" onMouseDown={() => setSettingsOpen(false)}><form className="drawer" onSubmit={saveConfig} onMouseDown={(e) => e.stopPropagation()}><div className="drawer-header"><div><span>CONNECTION</span><h2>PC ↔ AGX 通信设置</h2></div><button type="button" onClick={() => setSettingsOpen(false)}><X size={18} /></button></div><p className="drawer-intro">浏览器只连接本机桥接服务，原生 TCP、UDP 和 RTSP 均由 PC 本地进程处理。</p>
      <div className="form-grid"><label className="wide"><span>AGX IP 地址</span><input value={configDraft.agxHost} onChange={(e) => setConfigDraft({ ...configDraft, agxHost: e.target.value })} /></label><label><span>TCP 命令端口</span><input type="number" value={configDraft.tcpPort} onChange={(e) => setConfigDraft({ ...configDraft, tcpPort: Number(e.target.value) })} /></label><label><span>UDP 命令端口</span><input type="number" value={configDraft.commandUdpPort} onChange={(e) => setConfigDraft({ ...configDraft, commandUdpPort: Number(e.target.value) })} /></label><label><span>PC 遥测监听端口</span><input type="number" value={configDraft.telemetryPort} onChange={(e) => setConfigDraft({ ...configDraft, telemetryPort: Number(e.target.value) })} /></label><label><span>RTSP 端口</span><input type="number" value={configDraft.rtspPort} onChange={(e) => setConfigDraft({ ...configDraft, rtspPort: Number(e.target.value) })} /></label><label className="wide"><span>RTSP 路径</span><input value={configDraft.rtspPath} onChange={(e) => setConfigDraft({ ...configDraft, rtspPath: e.target.value })} /></label></div>
      <fieldset><legend>控制指令传输</legend><div className="segmented"><button type="button" className={configDraft.transport === 'udp' ? 'active' : ''} onClick={() => setConfigDraft({ ...configDraft, transport: 'udp' })}>UDP · 推荐</button><button type="button" className={configDraft.transport === 'tcp' ? 'active' : ''} onClick={() => setConfigDraft({ ...configDraft, transport: 'tcp' })}>TCP · 长连接</button></div></fieldset>
      <label className="toggle-row"><span><strong>持续发送心跳</strong><small>每秒向 AGX UDP 14551 发送 JSON 心跳</small></span><input type="checkbox" checked={configDraft.heartbeatEnabled} onChange={(e) => setConfigDraft({ ...configDraft, heartbeatEnabled: e.target.checked })} /></label><label className="toggle-row"><span><strong>自动启动视频桥接</strong><small>使用 FFmpeg 将 RTSP 无转码封装为浏览器 HLS</small></span><input type="checkbox" checked={configDraft.autoVideo} onChange={(e) => setConfigDraft({ ...configDraft, autoVideo: e.target.checked })} /></label><div className="drawer-footer"><button type="button" className="secondary" onClick={() => setSettingsOpen(false)}>取消</button><button type="submit" disabled={busy}>保存并重连</button></div></form></div>}

    {routeOpen && <div className="overlay" onMouseDown={() => setRouteOpen(false)}><form className="drawer route-drawer" onSubmit={submitRoute} onMouseDown={(e) => e.stopPropagation()}><div className="drawer-header"><div><span>WAYPOINT</span><h2>设置目标航点</h2></div><button type="button" onClick={() => setRouteOpen(false)}><X size={18} /></button></div><p className="drawer-intro">航点高度为绝对海拔。下发前请根据当前 RTK/GPS 高度确认参考系一致。</p><fieldset><legend>航点处理方式</legend><div className="segmented"><button type="button" className={routeMode === 'replace' ? 'active' : ''} onClick={() => setRouteMode('replace')}>替换当前目标</button><button type="button" className={routeMode === 'append' ? 'active' : ''} onClick={() => setRouteMode('append')}>追加到任务队列</button></div></fieldset><div className="form-grid"><label><span>纬度</span><input value={route.latitude} onChange={(e) => setRoute({ ...route, latitude: e.target.value })} placeholder={number(position?.lat, 7, '22.5428120')} /></label><label><span>经度</span><input value={route.longitude} onChange={(e) => setRoute({ ...route, longitude: e.target.value })} placeholder={number(position?.lon, 7, '113.9589020')} /></label><label><span>绝对高度 / m</span><input value={route.altitude} onChange={(e) => setRoute({ ...route, altitude: e.target.value })} placeholder="65.0" /></label><label><span>最大速度 / m/s</span><input value={route.speed} onChange={(e) => setRoute({ ...route, speed: e.target.value })} /></label></div><div className="route-notice"><ShieldAlert size={17} /><span>当前协议没有远端 ACK。界面显示“已发送”只代表 PC 已写入网络，是否执行必须结合导航遥测确认。</span></div><div className="drawer-footer"><button type="button" className="secondary" onClick={() => setRouteOpen(false)}>取消</button><button type="submit">检查并下发</button></div></form></div>}

    {pending && <div className="overlay confirm-overlay" onMouseDown={() => !busy && setPending(null)}><div className={`confirm-card ${pending.tone === 'danger' ? 'danger' : ''}`} onMouseDown={(e) => e.stopPropagation()}><div className="confirm-icon">{pending.tone === 'danger' ? <ShieldAlert size={23} /> : <Navigation size={23} />}</div><span>FLIGHT COMMAND</span><h2>{pending.title}</h2><p>{pending.detail}</p><div className="confirm-actions"><button onClick={() => setPending(null)} disabled={busy}>返回检查</button><button className="confirm" onClick={() => executeCommand(pending.command, pending.params)} disabled={busy}>{busy ? '正在发送…' : '确认发送指令'}</button></div></div></div>}
    {toast && <div className="toast"><span /><p>{toast}</p></div>}
  </main>;
}

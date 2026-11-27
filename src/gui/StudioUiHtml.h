#pragma once

namespace syncwave {

inline constexpr const char* kStudioUiHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>SyncWave</title>
<style>
  :root {
    --bg: #000000;
    --surface: #0b0b0e;
    --surface-2: #141419;
    --surface-3: #1e1e26;
    --border: #272730;
    --border-hi: #f4f4f5;
    --text: #ffffff;
    --text-dim: #a1a1aa;
    --text-muted: #71717a;
    --mint: #00e599;
    --amber: #ffb800;
    --crimson: #ff3355;
    --cyan: #00d2ff;
    --radius: 2px;
    --font: "Segoe UI Variable Text", "Segoe UI", system-ui, -apple-system, sans-serif;
    --mono: "Cascadia Mono", "Consolas", ui-monospace, monospace;
  }

  * { box-sizing: border-box; margin: 0; padding: 0; user-select: none; }

  html, body {
    height: 100%;
    background: var(--bg);
    color: var(--text);
    font-family: var(--font);
    font-size: 13px;
    line-height: 1.4;
    overflow: hidden;
  }

  .app {
    display: flex;
    flex-direction: column;
    height: 100vh;
    background: var(--bg);
  }

  /* Header */
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 22px;
    background: var(--bg);
    border-bottom: 2px solid var(--border);
    gap: 16px;
  }

  .brand {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .brand-mark {
    width: 34px;
    height: 34px;
    border-radius: var(--radius);
    background: var(--mint);
    color: #000000;
    border: 2px solid #ffffff;
    box-shadow: 3px 3px 0px #272730;
    display: grid;
    place-items: center;
  }

  .brand-title {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .brand-name {
    font-size: 16px;
    font-weight: 800;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: var(--text);
  }

  .status-pill {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    font-family: var(--mono);
    font-size: 10.5px;
    font-weight: 700;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--text-muted);
  }

  .dot {
    width: 7px;
    height: 7px;
    background: var(--text-muted);
  }

  .status-pill.live { color: var(--mint); }
  .status-pill.live .dot { background: var(--mint); }

  .header-controls {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .segmented {
    display: inline-flex;
    background: var(--surface);
    border: 2px solid var(--border);
    border-radius: var(--radius);
    padding: 2px;
    gap: 2px;
  }

  .seg-btn {
    background: transparent;
    border: none;
    color: var(--text-dim);
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    padding: 6px 11px;
    border-radius: var(--radius);
    cursor: pointer;
  }
  .seg-btn:hover { color: var(--text); }
  .seg-btn.active {
    background: #ffffff;
    color: #000000;
  }

  .btn-icon {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 34px;
    height: 34px;
    border-radius: var(--radius);
    border: 2px solid var(--border);
    background: var(--surface);
    color: var(--text);
    cursor: pointer;
    box-shadow: 2px 2px 0px #272730;
    transition: transform 0.08s, box-shadow 0.08s;
  }
  .btn-icon:hover { border-color: #ffffff; }
  .btn-icon:active { transform: translate(2px, 2px); box-shadow: 0px 0px 0px #000; }

  .btn-primary {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 8px 18px;
    border-radius: var(--radius);
    border: 2px solid #ffffff;
    background: var(--mint);
    color: #000000;
    font-family: var(--mono);
    font-size: 12px;
    font-weight: 800;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    cursor: pointer;
    box-shadow: 3px 3px 0px #ffffff;
    transition: transform 0.08s, box-shadow 0.08s;
  }
  .btn-primary:hover { transform: translate(-1px, -1px); box-shadow: 4px 4px 0px #ffffff; }
  .btn-primary:active { transform: translate(2px, 2px); box-shadow: 0px 0px 0px #ffffff; }

  .btn-primary.stop {
    background: var(--crimson);
    color: #ffffff;
    border-color: #ffffff;
    box-shadow: 3px 3px 0px #ffffff;
  }

  /* Main content */
  .main {
    flex: 1;
    overflow-y: auto;
    padding: 18px 22px;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }

  .main::-webkit-scrollbar { width: 6px; }
  .main::-webkit-scrollbar-thumb { background: var(--border); }

  .section-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .section-title {
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: var(--text);
  }

  .section-sub {
    font-size: 11.5px;
    color: var(--text-muted);
  }

  .banner {
    display: none;
    align-items: center;
    justify-content: space-between;
    padding: 10px 14px;
    border-radius: var(--radius);
    font-size: 12px;
    gap: 12px;
  }
  .banner.show { display: flex; }
  .banner.error {
    background: #23060c;
    border: 2px solid var(--crimson);
    color: #ffffff;
    box-shadow: 3px 3px 0px var(--crimson);
  }
  .banner.hint {
    background: #141000;
    border: 2px solid var(--amber);
    color: #fde68a;
    box-shadow: 3px 3px 0px var(--amber);
  }
  .banner strong { color: #ffffff; font-weight: 700; }
  .kbd {
    display: inline-block;
    padding: 1px 6px;
    background: #000000;
    border: 2px solid var(--amber);
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    color: var(--amber);
  }

  .device-list {
    display: flex;
    flex-direction: column;
    gap: 12px;
  }

  .device-card {
    background: var(--surface);
    border: 2px solid var(--border);
    border-radius: var(--radius);
    padding: 14px 16px;
    transition: border-color 0.12s, opacity 0.12s;
  }

  .device-card.enabled {
    border-color: var(--border-hi);
    box-shadow: 4px 4px 0px #1e1e26;
  }

  .device-card.disabled {
    opacity: 0.52;
  }

  .device-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }

  .device-id-group {
    display: flex;
    align-items: center;
    gap: 12px;
    min-width: 0;
  }

  /* Brutalist Square Toggle */
  .switch {
    position: relative;
    width: 40px;
    height: 22px;
    flex-shrink: 0;
  }
  .switch input { opacity: 0; width: 0; height: 0; }
  .switch-track {
    position: absolute;
    inset: 0;
    background: #000000;
    border: 2px solid var(--border);
    border-radius: var(--radius);
    cursor: pointer;
    transition: background 0.12s, border-color 0.12s;
  }
  .switch-track::after {
    content: "";
    position: absolute;
    top: 2px;
    left: 2px;
    width: 14px;
    height: 14px;
    background: var(--text-muted);
    transition: transform 0.12s, background 0.12s;
  }
  .switch input:checked + .switch-track {
    background: var(--mint);
    border-color: #ffffff;
  }
  .switch input:checked + .switch-track::after {
    transform: translateX(18px);
    background: #000000;
  }

  .dev-icon {
    width: 32px;
    height: 32px;
    border-radius: var(--radius);
    background: var(--surface-2);
    border: 2px solid var(--border);
    display: grid;
    place-items: center;
    color: var(--text);
    flex-shrink: 0;
  }

  .dev-meta {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }

  .dev-name {
    font-size: 14px;
    font-weight: 700;
    color: var(--text);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .dev-tags {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-top: 3px;
  }

  .tag {
    font-family: var(--mono);
    font-size: 10px;
    font-weight: 700;
    text-transform: uppercase;
    padding: 1px 6px;
    background: #000000;
    border: 1px solid var(--border);
    color: var(--text-dim);
  }
  .tag.bt { color: var(--cyan); border-color: var(--cyan); }
  .tag.virt { color: var(--amber); border-color: var(--amber); }

  .dev-badge {
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    padding: 4px 9px;
    background: #000000;
    border: 2px solid var(--border);
    color: var(--text-dim);
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .dev-badge.locked {
    color: #000000;
    background: var(--mint);
    border-color: #ffffff;
  }

  /* Controls grid inside active card */
  .device-controls {
    display: grid;
    grid-template-columns: 1fr 1.25fr;
    gap: 18px;
    margin-top: 14px;
    padding-top: 13px;
    border-top: 2px solid var(--border);
  }

  .device-card.disabled .device-controls {
    display: none;
  }

  .ctrl-group {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .ctrl-label-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .ctrl-label {
    font-family: var(--mono);
    font-size: 10.5px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--text-dim);
  }

  .ctrl-val {
    font-family: var(--mono);
    font-size: 12.5px;
    font-weight: 800;
    color: var(--text);
    font-variant-numeric: tabular-nums;
  }

  .slider-row {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  /* Brutalist Fader */
  input[type="range"] {
    -webkit-appearance: none;
    appearance: none;
    flex: 1;
    height: 8px;
    background: var(--surface-3);
    border: 1px solid var(--border);
    outline: none;
    cursor: pointer;
  }

  input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    appearance: none;
    width: 13px;
    height: 22px;
    background: #ffffff;
    border: 2px solid #000000;
    box-shadow: 2px 2px 0px #000000;
    cursor: pointer;
  }
  input[type="range"]::-webkit-slider-thumb:hover {
    background: var(--mint);
  }

  .mini-btn {
    height: 26px;
    padding: 0 8px;
    border-radius: var(--radius);
    border: 2px solid var(--border);
    background: #000000;
    color: var(--text);
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    cursor: pointer;
    transition: border-color 0.1s, background 0.1s;
  }
  .mini-btn:hover { border-color: #ffffff; background: var(--surface-3); }
  .mini-btn.active {
    background: var(--crimson);
    color: #ffffff;
    border-color: #ffffff;
  }

  /* Footer & Diagnostics Drawer */
  .footer {
    background: var(--bg);
    border-top: 2px solid var(--border);
  }

  .diag-drawer {
    display: none;
    padding: 12px 22px;
    border-bottom: 2px solid var(--border);
    background: var(--surface);
  }
  .diag-drawer.open { display: block; }

  .diag-table {
    width: 100%;
    border-collapse: collapse;
    font-family: var(--mono);
    font-size: 11px;
    font-variant-numeric: tabular-nums;
  }
  .diag-table th {
    text-align: left;
    color: var(--text-muted);
    font-weight: 700;
    text-transform: uppercase;
    padding: 4px 8px;
    border-bottom: 2px solid var(--border);
  }
  .diag-table td {
    padding: 6px 8px;
    color: var(--text);
    border-bottom: 1px solid var(--border);
  }

  .footer-bar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 22px;
    font-size: 11.5px;
    color: var(--text-dim);
  }

  .footer-stats {
    font-family: var(--mono);
    font-variant-numeric: tabular-nums;
  }

  .btn-link {
    background: var(--surface);
    border: 2px solid var(--border);
    padding: 3px 10px;
    color: var(--text);
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    cursor: pointer;
  }
  .btn-link:hover { border-color: #ffffff; }
</style>
</head>
<body>
<div class="app">
  <header class="header">
    <div class="brand">
      <div class="brand-mark">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="square" stroke-linejoin="miter">
          <path d="M2 12h3l2.5-6 4 12 3.5-8 2.5 2H22"/>
        </svg>
      </div>
      <div class="brand-title">
        <div class="brand-name">SyncWave</div>
        <div class="status-pill" id="statusPill">
          <span class="dot"></span>
          <span id="statusText">Standby</span>
        </div>
      </div>
    </div>

    <div class="header-controls">
      <div class="segmented" title="Synchronization Algorithm">
        <button class="seg-btn active" data-mode="adaptive">Adaptive</button>
        <button class="seg-btn" data-mode="software">Static</button>
        <button class="seg-btn" data-mode="none">Direct</button>
      </div>
      <button class="btn-icon" id="btnRefresh" title="Refresh audio devices">
        <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="square" stroke-linejoin="miter">
          <polyline points="23 4 23 10 17 10"></polyline>
          <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"></path>
        </svg>
      </button>
      <button class="btn-primary" id="btnSyncToggle">
        <span id="btnSyncLabel">Start Sync</span>
      </button>
    </div>
  </header>

  <main class="main">
    <div class="banner error" id="errorBanner">
      <span id="errorText"></span>
    </div>

    <div class="banner hint" id="vlcHintBanner">
      <span>
        <strong>External VLC Lip-Sync:</strong> Press <span class="kbd">J</span> in VLC until audio delay is <strong id="vlcDelayRec">-200 ms</strong> to match screen video.
      </span>
    </div>

    <div class="section-head">
      <span class="section-title">Output Endpoints</span>
      <span class="section-sub">Independent hardware master volume (50% default, auto-saved).</span>
    </div>

    <div class="device-list" id="deviceList"></div>
  </main>

  <footer class="footer">
    <div class="diag-drawer" id="diagDrawer">
      <table class="diag-table">
        <thead>
          <tr>
            <th>Endpoint</th>
            <th>State</th>
            <th>Drift</th>
            <th>Phase Error</th>
            <th>Delay</th>
            <th>Eff. Latency</th>
            <th>Underruns</th>
          </tr>
        </thead>
        <tbody id="diagBody"></tbody>
      </table>
    </div>
    <div class="footer-bar">
      <span class="footer-stats" id="footerStats">48000 HZ FLOAT32 // IDLE</span>
      <button class="btn-link" id="btnToggleDiag">Diagnostics &#9662;</button>
    </div>
  </footer>
</div>

<script>
  const PREFS_KEY = 'syncwave_prefs_v1';

  const state = {
    devices: [],
    enabledIds: new Set(),
    volumes: {},   // id -> 0..100 (default 50 on first launch)
    muted: {},     // id -> bool
    delays: {},    // id -> 0..400 ms
    syncMode: 'adaptive',
    isRunning: false,
    telemetry: null,
    diagOpen: false
  };

  function loadPrefs() {
    try {
      const raw = localStorage.getItem(PREFS_KEY);
      return raw ? JSON.parse(raw) : { syncMode: 'adaptive', devices: {} };
    } catch {
      return { syncMode: 'adaptive', devices: {} };
    }
  }

  function savePrefs() {
    try {
      const prefs = { syncMode: state.syncMode, devices: {} };
      state.devices.forEach(d => {
        prefs.devices[d.id] = {
          enabled: state.enabledIds.has(d.id),
          volume: state.volumes[d.id] ?? 50,
          muted: !!state.muted[d.id],
          delay: state.delays[d.id] ?? 0
        };
      });
      localStorage.setItem(PREFS_KEY, JSON.stringify(prefs));
    } catch {}
  }

  function post(obj) {
    if (window.chrome && window.chrome.webview) {
      window.chrome.webview.postMessage(obj);
    }
  }

  function getSelectedOrderedIds() {
    return state.devices.filter(d => state.enabledIds.has(d.id)).map(d => d.id);
  }

  function pushAllDelays() {
    const selected = getSelectedOrderedIds();
    const delays = selected.map(id => state.delays[id] || 0);
    post({ cmd: 'setDelays', delays });
  }

  function applyAllEndpointVolumes() {
    state.devices.forEach(d => {
      if (state.enabledIds.has(d.id)) {
        const vol = (state.volumes[d.id] ?? 50) / 100.0;
        const muted = !!state.muted[d.id];
        post({ cmd: 'setEndpointVolume', id: d.id, volume: vol });
        post({ cmd: 'setEndpointMuted', id: d.id, muted });
      }
    });
  }

  function sliderStyle(pct, colorVar) {
    return `background: linear-gradient(to right, ${colorVar} 0%, ${colorVar} ${pct}%, #1e1e26 ${pct}%, #1e1e26 100%)`;
  }

  function speakerSvg(isBt) {
    if (isBt) {
      return `<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="square" stroke-linejoin="miter"><path d="M3 18v-6a9 9 0 0 1 18 0v6"/><path d="M21 19a2 2 0 0 1-2 2h-1v-6h3zM3 19a2 2 0 0 0 2 2h1v-6H3z"/></svg>`;
    }
    return `<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="square" stroke-linejoin="miter"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/></svg>`;
  }

  function renderDevices() {
    const list = document.getElementById('deviceList');
    list.innerHTML = '';

    const epMap = new Map();
    if (state.telemetry && state.telemetry.endpoints) {
      state.telemetry.endpoints.forEach(ep => epMap.set(ep.id, ep));
    }

    const selected = getSelectedOrderedIds();

    state.devices.forEach(dev => {
      const enabled = state.enabledIds.has(dev.id);
      const activeIdx = selected.indexOf(dev.id);
      const vol = state.volumes[dev.id] ?? 50;
      const muted = !!state.muted[dev.id];
      const delay = state.delays[dev.id] ?? 0;
      const ep = epMap.get(dev.id);

      const card = document.createElement('div');
      card.className = `device-card ${enabled ? 'enabled' : 'disabled'}`;

      let badgeHtml = `<span class="dev-badge">${enabled ? 'Ready' : 'Bypassed'}</span>`;
      if (enabled && state.isRunning && ep) {
        const isLocked = ep.driftState === 'Locked' || ep.syncState !== 'Disabled';
        const ppm = (ep.driftPpm >= 0 ? '+' : '') + ep.driftPpm.toFixed(1) + ' ppm';
        badgeHtml = `<span class="dev-badge ${isLocked ? 'locked' : ''}">${ep.driftState} // ${ppm}</span>`;
      }

      let tags = '';
      if (dev.isBluetooth) tags += `<span class="tag bt">Bluetooth</span>`;
      if (dev.isVirtual) tags += `<span class="tag virt">Loopback Carrier</span>`;
      if (dev.isDefault) tags += `<span class="tag">Default</span>`;

      const delayPct = (delay / 400.0) * 100.0;

      card.innerHTML = `
        <div class="device-top">
          <div class="device-id-group">
            <label class="switch">
              <input type="checkbox" ${enabled ? 'checked' : ''} data-toggle="${dev.id}" />
              <span class="switch-track"></span>
            </label>
            <div class="dev-icon">${speakerSvg(dev.isBluetooth)}</div>
            <div class="dev-meta">
              <div class="dev-name">${dev.name}</div>
              <div class="dev-tags">${tags}</div>
            </div>
          </div>
          ${badgeHtml}
        </div>
        <div class="device-controls">
          <div class="ctrl-group">
            <div class="ctrl-label-row">
              <span class="ctrl-label">Master Volume (Hardware)</span>
              <span class="ctrl-val" id="vol-val-${activeIdx}">${muted ? 'MUTED' : vol + '%'}</span>
            </div>
            <div class="slider-row">
              <button class="mini-btn ${muted ? 'active' : ''}" data-mute="${dev.id}">${muted ? 'UNMUTE' : 'MUTE'}</button>
              <input type="range" min="0" max="100" value="${vol}" style="${sliderStyle(vol, 'var(--mint)')}" data-vol="${dev.id}" />
            </div>
          </div>
          <div class="ctrl-group">
            <div class="ctrl-label-row">
              <span class="ctrl-label">Sync Delay</span>
              <span class="ctrl-val" id="delay-val-${activeIdx}">${delay} ms</span>
            </div>
            <div class="slider-row">
              <button class="mini-btn" data-nudge="${dev.id}" data-delta="-5">-5</button>
              <button class="mini-btn" data-nudge="${dev.id}" data-delta="-1">-1</button>
              <input type="range" min="0" max="400" value="${delay}" style="${sliderStyle(delayPct, 'var(--amber)')}" data-delay="${dev.id}" />
              <button class="mini-btn" data-nudge="${dev.id}" data-delta="1">+1</button>
              <button class="mini-btn" data-nudge="${dev.id}" data-delta="5">+5</button>
              <button class="mini-btn" data-nudge="${dev.id}" data-delta="reset">0</button>
            </div>
          </div>
        </div>
      `;

      list.appendChild(card);
    });

    // Wire events
    list.querySelectorAll('input[data-toggle]').forEach(el => {
      el.addEventListener('change', (e) => {
        const id = e.target.dataset.toggle;
        if (e.target.checked) state.enabledIds.add(id);
        else state.enabledIds.delete(id);
        savePrefs();
        renderDevices();
        applyAllEndpointVolumes();
        if (state.isRunning) {
          post({ cmd: 'startSync', outputs: getSelectedOrderedIds(), syncMode: state.syncMode });
          pushAllDelays();
        }
      });
    });

    list.querySelectorAll('input[data-vol]').forEach(el => {
      el.addEventListener('input', (e) => {
        const id = e.target.dataset.vol;
        const val = parseInt(e.target.value, 10);
        state.volumes[id] = val;
        e.target.style.cssText = sliderStyle(val, 'var(--mint)');
        const idx = getSelectedOrderedIds().indexOf(id);
        if (idx >= 0) {
          const lbl = document.getElementById(`vol-val-${idx}`);
          if (lbl && !state.muted[id]) lbl.textContent = val + '%';
        }
        post({ cmd: 'setEndpointVolume', id, volume: val / 100.0 });
        savePrefs();
      });
    });

    list.querySelectorAll('button[data-mute]').forEach(el => {
      el.addEventListener('click', (e) => {
        const id = e.target.dataset.mute;
        state.muted[id] = !state.muted[id];
        post({ cmd: 'setEndpointMuted', id, muted: !!state.muted[id] });
        savePrefs();
        renderDevices();
      });
    });

    list.querySelectorAll('input[data-delay]').forEach(el => {
      el.addEventListener('input', (e) => {
        const id = e.target.dataset.delay;
        const val = parseInt(e.target.value, 10);
        state.delays[id] = val;
        e.target.style.cssText = sliderStyle((val / 400.0) * 100.0, 'var(--amber)');
        const idx = getSelectedOrderedIds().indexOf(id);
        if (idx >= 0) {
          const lbl = document.getElementById(`delay-val-${idx}`);
          if (lbl) lbl.textContent = val + ' ms';
        }
        pushAllDelays();
        savePrefs();
      });
    });

    list.querySelectorAll('button[data-nudge]').forEach(el => {
      el.addEventListener('click', (e) => {
        const id = e.target.dataset.nudge;
        const delta = e.target.dataset.delta;
        const cur = state.delays[id] ?? 0;
        const next = delta === 'reset' ? 0 : Math.max(0, Math.min(400, cur + parseInt(delta, 10)));
        state.delays[id] = next;
        pushAllDelays();
        savePrefs();
        renderDevices();
      });
    });
  }

  function updateTelemetryUi(t) {
    state.telemetry = t;
    state.isRunning = !!t.isRunning;

    const pill = document.getElementById('statusPill');
    const statusText = document.getElementById('statusText');
    const btnSync = document.getElementById('btnSyncToggle');
    const btnSyncLabel = document.getElementById('btnSyncLabel');

    if (state.isRunning) {
      pill.classList.add('live');
      statusText.textContent = 'LIVE // SYSTEM AUDIO';
      btnSync.classList.add('stop');
      btnSyncLabel.textContent = 'Stop Sync';
    } else {
      pill.classList.remove('live');
      statusText.textContent = 'STANDBY';
      btnSync.classList.remove('stop');
      btnSyncLabel.textContent = 'Start Sync';
    }

    const epMap = new Map();
    (t.endpoints || []).forEach(ep => epMap.set(ep.id, ep));
    document.querySelectorAll('.device-card').forEach((card, i) => {
      const dev = state.devices[i];
      if (!dev) return;
      const badge = card.querySelector('.dev-badge');
      if (!badge) return;
      const enabled = state.enabledIds.has(dev.id);
      const ep = epMap.get(dev.id);
      if (enabled && state.isRunning && ep) {
        const isLocked = ep.driftState === 'Locked' || ep.syncState !== 'Disabled';
        const ppm = (ep.driftPpm >= 0 ? '+' : '') + ep.driftPpm.toFixed(1) + ' ppm';
        badge.className = `dev-badge ${isLocked ? 'locked' : ''}`;
        badge.textContent = `${ep.driftState} // ${ppm}`;
      } else {
        badge.className = 'dev-badge';
        badge.textContent = enabled ? 'Ready' : 'Bypassed';
      }
    });

    const vlcBanner = document.getElementById('vlcHintBanner');
    if (state.isRunning && t.maxLatencyMs >= 60) {
      const rec = Math.round(t.maxLatencyMs / 5) * 5;
      document.getElementById('vlcDelayRec').textContent = `-${rec} ms`;
      vlcBanner.classList.add('show');
    } else {
      vlcBanner.classList.remove('show');
    }

    document.getElementById('footerStats').textContent =
      `${t.sampleRate || 48000} HZ FLOAT32 // CPU ${t.cpu.toFixed(1)}% // ${t.memoryMb.toFixed(0)} MB`;

    const tbody = document.getElementById('diagBody');
    tbody.innerHTML = (t.endpoints || []).map(ep => `
      <tr>
        <td>${ep.name}</td>
        <td>${ep.driftState}</td>
        <td>${(ep.driftPpm >= 0 ? '+' : '') + ep.driftPpm.toFixed(1)} ppm</td>
        <td>${ep.phaseErrorMs.toFixed(2)} ms</td>
        <td>${ep.delayMs.toFixed(0)} ms</td>
        <td>${ep.effectiveLatencyMs.toFixed(1)} ms</td>
        <td>${ep.underruns}</td>
      </tr>
    `).join('');
  }

  // Segmented sync mode
  document.querySelectorAll('.seg-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.seg-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      state.syncMode = btn.dataset.mode;
      post({ cmd: 'setSyncMode', mode: state.syncMode });
      savePrefs();
    });
  });

  document.getElementById('btnRefresh').addEventListener('click', () => {
    post({ cmd: 'refreshDevices' });
  });

  document.getElementById('btnSyncToggle').addEventListener('click', () => {
    if (state.isRunning) {
      post({ cmd: 'stopSync' });
    } else {
      const outputs = getSelectedOrderedIds();
      applyAllEndpointVolumes();
      post({ cmd: 'startSync', outputs, syncMode: state.syncMode });
      pushAllDelays();
    }
  });

  document.getElementById('btnToggleDiag').addEventListener('click', () => {
    state.diagOpen = !state.diagOpen;
    document.getElementById('diagDrawer').classList.toggle('open', state.diagOpen);
  });

  if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener('message', (event) => {
      const msg = event.data;
      if (!msg) return;
      if (msg.type === 'devices') {
        const prefs = loadPrefs();
        if (prefs.syncMode) {
          state.syncMode = prefs.syncMode;
          document.querySelectorAll('.seg-btn').forEach(b => {
            b.classList.toggle('active', b.dataset.mode === state.syncMode);
          });
          post({ cmd: 'setSyncMode', mode: state.syncMode });
        }

        state.devices = msg.devices || [];
        state.enabledIds.clear();
        state.devices.forEach(d => {
          const saved = prefs.devices && prefs.devices[d.id];
          if (saved) {
            if (saved.enabled) state.enabledIds.add(d.id);
            state.volumes[d.id] = typeof saved.volume === 'number' ? saved.volume : 50;
            state.muted[d.id] = !!saved.muted;
            state.delays[d.id] = typeof saved.delay === 'number' ? saved.delay : 0;
          } else {
            // First time: auto-enable physical outputs, default volume to 50%
            if (!d.isVirtual) state.enabledIds.add(d.id);
            state.volumes[d.id] = 50;
            state.muted[d.id] = false;
            state.delays[d.id] = 0;
          }
        });
        savePrefs();
        renderDevices();
        applyAllEndpointVolumes();
      } else if (msg.type === 'telemetry') {
        updateTelemetryUi(msg);
      } else if (msg.type === 'error') {
        const banner = document.getElementById('errorBanner');
        if (msg.message) {
          document.getElementById('errorText').textContent = msg.message;
          banner.classList.add('show');
        } else {
          banner.classList.remove('show');
        }
      }
    });
  }

  post({ cmd: 'init' });
</script>
</body>
</html>
)HTML";

} // namespace syncwave

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
    --bg: #0e1014;
    --surface: #15181f;
    --surface-2: #1b1f28;
    --surface-3: #222733;
    --border: rgba(255, 255, 255, 0.08);
    --border-strong: rgba(255, 255, 255, 0.14);
    --text: #f3f4f6;
    --text-dim: #9ca3af;
    --text-muted: #6b7280;
    --accent: #10b981;
    --accent-dim: rgba(16, 185, 129, 0.14);
    --sky: #38bdf8;
    --danger: #ef4444;
    --danger-dim: rgba(239, 68, 68, 0.14);
    --amber: #f59e0b;
    --amber-dim: rgba(245, 158, 11, 0.12);
    --radius: 10px;
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
  }

  /* Header */
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 22px;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
    gap: 16px;
  }

  .brand {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .brand-mark {
    width: 30px;
    height: 30px;
    border-radius: 8px;
    background: linear-gradient(145deg, #1b222c, #11141a);
    border: 1px solid var(--border-strong);
    display: grid;
    place-items: center;
    color: var(--accent);
  }

  .brand-title {
    display: flex;
    flex-direction: column;
    gap: 1px;
  }

  .brand-name {
    font-size: 15px;
    font-weight: 600;
    letter-spacing: -0.01em;
    color: var(--text);
  }

  .status-pill {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    font-size: 11px;
    font-weight: 500;
    color: var(--text-dim);
  }

  .dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: var(--text-muted);
    transition: background 0.2s, box-shadow 0.2s;
  }

  .status-pill.live {
    color: var(--accent);
  }
  .status-pill.live .dot {
    background: var(--accent);
    box-shadow: 0 0 8px var(--accent);
  }

  .header-controls {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .segmented {
    display: inline-flex;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 3px;
    gap: 2px;
  }

  .seg-btn {
    background: transparent;
    border: none;
    color: var(--text-dim);
    font-family: var(--font);
    font-size: 12px;
    font-weight: 500;
    padding: 5px 11px;
    border-radius: 6px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .seg-btn:hover { color: var(--text); }
  .seg-btn.active {
    background: var(--surface-3);
    color: var(--text);
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.06);
  }

  .btn-primary {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 8px 18px;
    border-radius: 8px;
    border: 1px solid rgba(16, 185, 129, 0.4);
    background: var(--accent);
    color: #041b11;
    font-family: var(--font);
    font-size: 13px;
    font-weight: 600;
    cursor: pointer;
    transition: filter 0.15s, transform 0.08s;
  }
  .btn-primary:hover { filter: brightness(1.08); }
  .btn-primary:active { transform: scale(0.98); }

  .btn-primary.stop {
    background: var(--danger-dim);
    color: #fca5a5;
    border-color: rgba(239, 68, 68, 0.35);
  }

  .btn-icon {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 32px;
    height: 32px;
    border-radius: 8px;
    border: 1px solid var(--border);
    background: var(--surface-2);
    color: var(--text-dim);
    cursor: pointer;
    transition: all 0.15s;
  }
  .btn-icon:hover { color: var(--text); border-color: var(--border-strong); }

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
  .main::-webkit-scrollbar-thumb { background: var(--surface-3); border-radius: 3px; }

  .section-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .section-title {
    font-size: 11px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.07em;
    color: var(--text-muted);
  }

  .section-sub {
    font-size: 11.5px;
    color: var(--text-dim);
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
    background: var(--danger-dim);
    border: 1px solid rgba(239, 68, 68, 0.3);
    color: #fecaca;
  }
  .banner.hint {
    background: var(--surface);
    border: 1px solid var(--border);
    color: var(--text-dim);
  }
  .banner strong { color: var(--text); font-weight: 600; }
  .kbd {
    display: inline-block;
    padding: 1px 6px;
    border-radius: 4px;
    background: var(--surface-3);
    border: 1px solid var(--border-strong);
    font-family: var(--mono);
    font-size: 11px;
    color: var(--text);
  }

  .device-list {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .device-card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px 16px;
    transition: border-color 0.18s, background 0.18s, opacity 0.18s;
  }

  .device-card.enabled {
    border-color: var(--border-strong);
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.03);
  }

  .device-card.disabled {
    opacity: 0.62;
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

  /* Toggle switch */
  .switch {
    position: relative;
    width: 36px;
    height: 20px;
    flex-shrink: 0;
  }
  .switch input { opacity: 0; width: 0; height: 0; }
  .switch-track {
    position: absolute;
    inset: 0;
    background: var(--surface-3);
    border: 1px solid var(--border-strong);
    border-radius: 999px;
    cursor: pointer;
    transition: background 0.18s, border-color 0.18s;
  }
  .switch-track::after {
    content: "";
    position: absolute;
    top: 2px;
    left: 2px;
    width: 14px;
    height: 14px;
    border-radius: 50%;
    background: var(--text-dim);
    transition: transform 0.18s, background 0.18s;
  }
  .switch input:checked + .switch-track {
    background: var(--accent-dim);
    border-color: rgba(16, 185, 129, 0.55);
  }
  .switch input:checked + .switch-track::after {
    transform: translateX(16px);
    background: var(--accent);
  }

  .dev-icon {
    width: 30px;
    height: 30px;
    border-radius: 7px;
    background: var(--surface-2);
    border: 1px solid var(--border);
    display: grid;
    place-items: center;
    color: var(--text-dim);
    flex-shrink: 0;
  }

  .dev-meta {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }

  .dev-name {
    font-size: 13.5px;
    font-weight: 600;
    color: var(--text);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .dev-tags {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-top: 2px;
  }

  .tag {
    font-size: 10.5px;
    font-weight: 500;
    padding: 1px 6px;
    border-radius: 4px;
    background: var(--surface-2);
    border: 1px solid var(--border);
    color: var(--text-dim);
  }
  .tag.bt { color: var(--sky); border-color: rgba(56, 189, 248, 0.25); background: rgba(56, 189, 248, 0.08); }
  .tag.virt { color: var(--amber); border-color: rgba(245, 158, 11, 0.25); background: var(--amber-dim); }

  .dev-badge {
    font-family: var(--mono);
    font-size: 11px;
    padding: 3px 8px;
    border-radius: 6px;
    background: var(--surface-2);
    border: 1px solid var(--border);
    color: var(--text-dim);
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .dev-badge.locked {
    color: var(--accent);
    background: var(--accent-dim);
    border-color: rgba(16, 185, 129, 0.3);
  }

  /* Controls grid inside active card */
  .device-controls {
    display: grid;
    grid-template-columns: 1fr 1.25fr;
    gap: 18px;
    margin-top: 14px;
    padding-top: 12px;
    border-top: 1px solid var(--border);
  }

  .device-card.disabled .device-controls {
    display: none;
  }

  .ctrl-group {
    display: flex;
    flex-direction: column;
    gap: 7px;
  }

  .ctrl-label-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .ctrl-label {
    font-size: 11px;
    font-weight: 500;
    color: var(--text-dim);
  }

  .ctrl-val {
    font-family: var(--mono);
    font-size: 12px;
    font-weight: 600;
    color: var(--text);
    font-variant-numeric: tabular-nums;
  }

  .slider-row {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  input[type="range"] {
    -webkit-appearance: none;
    appearance: none;
    flex: 1;
    height: 5px;
    border-radius: 999px;
    background: var(--surface-3);
    outline: none;
    cursor: pointer;
  }

  input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    appearance: none;
    width: 14px;
    height: 14px;
    border-radius: 50%;
    background: #f3f4f6;
    border: 2px solid #0e1014;
    box-shadow: 0 1px 4px rgba(0,0,0,0.6);
    cursor: pointer;
    transition: transform 0.1s;
  }
  input[type="range"]::-webkit-slider-thumb:hover { transform: scale(1.15); }

  .mini-btn {
    height: 24px;
    padding: 0 7px;
    border-radius: 5px;
    border: 1px solid var(--border);
    background: var(--surface-2);
    color: var(--text-dim);
    font-family: var(--mono);
    font-size: 11px;
    cursor: pointer;
    transition: all 0.12s;
  }
  .mini-btn:hover { color: var(--text); border-color: var(--border-strong); background: var(--surface-3); }
  .mini-btn.active {
    background: var(--danger-dim);
    color: #fca5a5;
    border-color: rgba(239, 68, 68, 0.35);
  }

  /* Footer & Diagnostics Drawer */
  .footer {
    background: var(--surface);
    border-top: 1px solid var(--border);
  }

  .diag-drawer {
    display: none;
    padding: 12px 22px;
    border-bottom: 1px solid var(--border);
    background: #111319;
  }
  .diag-drawer.open { display: block; }

  .diag-table {
    width: 100%;
    border-collapse: collapse;
    font-family: var(--mono);
    font-size: 11.5px;
    font-variant-numeric: tabular-nums;
  }
  .diag-table th {
    text-align: left;
    color: var(--text-muted);
    font-weight: 500;
    padding: 4px 8px;
    border-bottom: 1px solid var(--border);
  }
  .diag-table td {
    padding: 6px 8px;
    color: var(--text-dim);
    border-bottom: 1px solid rgba(255,255,255,0.03);
  }

  .footer-bar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 9px 22px;
    font-size: 11.5px;
    color: var(--text-muted);
  }

  .footer-stats {
    font-family: var(--mono);
    font-variant-numeric: tabular-nums;
  }

  .btn-link {
    background: none;
    border: none;
    color: var(--text-dim);
    font-family: var(--font);
    font-size: 11.5px;
    font-weight: 500;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 5px;
  }
  .btn-link:hover { color: var(--text); }
</style>
</head>
<body>
<div class="app">
  <header class="header">
    <div class="brand">
      <div class="brand-mark">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
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
        <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
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
        <strong>Watching in external VLC?</strong> To match video lip-sync across your outputs, press <span class="kbd">J</span> in VLC until audio delay is <strong id="vlcDelayRec">-200 ms</strong>.
      </span>
    </div>

    <div class="section-head">
      <span class="section-title">Output Endpoints</span>
      <span class="section-sub">Toggle devices to sync. Windows audio auto-routes on Start and restores on Stop.</span>
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
      <span class="footer-stats" id="footerStats">48000 Hz Float32 &middot; Idle</span>
      <button class="btn-link" id="btnToggleDiag">Diagnostics &#9662;</button>
    </div>
  </footer>
</div>

<script>
  const state = {
    devices: [],
    enabledIds: new Set(),
    volumes: {},   // id -> 0..100
    muted: {},     // id -> bool
    delays: {},    // id -> 0..400 ms
    syncMode: 'adaptive',
    isRunning: false,
    telemetry: null,
    diagOpen: false
  };

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

  function pushAllVolumes() {
    const selected = getSelectedOrderedIds();
    selected.forEach((id, idx) => {
      const vol = (state.volumes[id] ?? 100) / 100.0;
      const muted = !!state.muted[id];
      post({ cmd: 'setDeviceVolume', index: idx, volume: vol });
      post({ cmd: 'setDeviceMuted', index: idx, muted });
    });
  }

  function speakerSvg(isBt) {
    if (isBt) {
      return `<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M3 18v-6a9 9 0 0 1 18 0v6"/><path d="M21 19a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3zM3 19a2 2 0 0 0 2 2h1a2 2 0 0 0 2-2v-3a2 2 0 0 0-2-2H3z"/></svg>`;
    }
    return `<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/></svg>`;
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
      const vol = state.volumes[dev.id] ?? 100;
      const muted = !!state.muted[dev.id];
      const delay = state.delays[dev.id] ?? 0;
      const ep = epMap.get(dev.id);

      const card = document.createElement('div');
      card.className = `device-card ${enabled ? 'enabled' : 'disabled'}`;

      let badgeHtml = `<span class="dev-badge">${enabled ? 'Ready' : 'Bypassed'}</span>`;
      if (enabled && state.isRunning && ep) {
        const isLocked = ep.driftState === 'Locked' || ep.syncState !== 'Disabled';
        const ppm = (ep.driftPpm >= 0 ? '+' : '') + ep.driftPpm.toFixed(1) + ' ppm';
        badgeHtml = `<span class="dev-badge ${isLocked ? 'locked' : ''}">${ep.driftState} &middot; ${ppm}</span>`;
      }

      let tags = '';
      if (dev.isBluetooth) tags += `<span class="tag bt">Bluetooth</span>`;
      if (dev.isVirtual) tags += `<span class="tag virt">Loopback Carrier</span>`;
      if (dev.isDefault) tags += `<span class="tag">Windows Default</span>`;

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
              <span class="ctrl-label">Output Gain</span>
              <span class="ctrl-val" id="vol-val-${activeIdx}">${muted ? 'MUTED' : vol + '%'}</span>
            </div>
            <div class="slider-row">
              <button class="mini-btn ${muted ? 'active' : ''}" data-mute="${dev.id}">${muted ? 'Unmute' : 'Mute'}</button>
              <input type="range" min="0" max="100" value="${vol}" data-vol="${dev.id}" />
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
              <input type="range" min="0" max="400" value="${delay}" data-delay="${dev.id}" />
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
        renderDevices();
        if (state.isRunning) {
          post({ cmd: 'startSync', outputs: getSelectedOrderedIds(), syncMode: state.syncMode });
          pushAllDelays();
          pushAllVolumes();
        }
      });
    });

    list.querySelectorAll('input[data-vol]').forEach(el => {
      el.addEventListener('input', (e) => {
        const id = e.target.dataset.vol;
        const val = parseInt(e.target.value, 10);
        state.volumes[id] = val;
        const idx = getSelectedOrderedIds().indexOf(id);
        if (idx >= 0) {
          const lbl = document.getElementById(`vol-val-${idx}`);
          if (lbl && !state.muted[id]) lbl.textContent = val + '%';
          post({ cmd: 'setDeviceVolume', index: idx, volume: val / 100.0 });
        }
      });
    });

    list.querySelectorAll('button[data-mute]').forEach(el => {
      el.addEventListener('click', (e) => {
        const id = e.target.dataset.mute;
        state.muted[id] = !state.muted[id];
        const idx = getSelectedOrderedIds().indexOf(id);
        if (idx >= 0) {
          post({ cmd: 'setDeviceMuted', index: idx, muted: !!state.muted[id] });
        }
        renderDevices();
      });
    });

    list.querySelectorAll('input[data-delay]').forEach(el => {
      el.addEventListener('input', (e) => {
        const id = e.target.dataset.delay;
        const val = parseInt(e.target.value, 10);
        state.delays[id] = val;
        const idx = getSelectedOrderedIds().indexOf(id);
        if (idx >= 0) {
          const lbl = document.getElementById(`delay-val-${idx}`);
          if (lbl) lbl.textContent = val + ' ms';
        }
        pushAllDelays();
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
      statusText.textContent = 'Live · System Audio';
      btnSync.classList.add('stop');
      btnSyncLabel.textContent = 'Stop Sync';
    } else {
      pill.classList.remove('live');
      statusText.textContent = 'Standby';
      btnSync.classList.remove('stop');
      btnSyncLabel.textContent = 'Start Sync';
    }

    // Update live badges without destroying active sliders while dragging
    const selected = getSelectedOrderedIds();
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
        badge.innerHTML = `${ep.driftState} &middot; ${ppm}`;
      } else {
        badge.className = 'dev-badge';
        badge.textContent = enabled ? 'Ready' : 'Bypassed';
      }
    });

    // Smart VLC lip-sync tip when running with significant output delay
    const vlcBanner = document.getElementById('vlcHintBanner');
    if (state.isRunning && t.maxLatencyMs >= 60) {
      const rec = Math.round(t.maxLatencyMs / 5) * 5;
      document.getElementById('vlcDelayRec').textContent = `-${rec} ms`;
      vlcBanner.classList.add('show');
    } else {
      vlcBanner.classList.remove('show');
    }

    // Footer & diagnostics
    document.getElementById('footerStats').textContent =
      `${t.sampleRate || 48000} Hz Float32 · CPU ${t.cpu.toFixed(1)}% · ${t.memoryMb.toFixed(0)} MB`;

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
      post({ cmd: 'startSync', outputs, syncMode: state.syncMode });
      pushAllDelays();
      pushAllVolumes();
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
        const firstLoad = state.devices.length === 0;
        state.devices = msg.devices || [];
        if (firstLoad) {
          state.devices.forEach(d => {
            if (!d.isVirtual) state.enabledIds.add(d.id);
          });
        }
        renderDevices();
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

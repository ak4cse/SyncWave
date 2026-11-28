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
    --mint-ink: #000000;
    --amber: #ffb800;
    --crimson: #ff3355;
    --cyan: #00d2ff;
    --shadow-card: 4px 4px 0px #1e1e26;
    --shadow-btn: 3px 3px 0px #ffffff;
    --border-w: 2px;
    --radius: 2px;
    --thumb-w: 13px;
    --thumb-h: 22px;
    --thumb-radius: 0px;
    --font: "Segoe UI Variable Text", "Segoe UI", system-ui, -apple-system, sans-serif;
    --mono: "Cascadia Mono", "Consolas", ui-monospace, monospace;
  }

  /* Light Mode Palette */
  body[data-theme="light"] {
    --bg: #f3f4f6;
    --surface: #ffffff;
    --surface-2: #e5e7eb;
    --surface-3: #d1d5db;
    --border: #d1d5db;
    --border-hi: #111827;
    --text: #09090b;
    --text-dim: #4b5563;
    --text-muted: #6b7280;
    --mint: #059669;
    --mint-ink: #ffffff;
    --amber: #d97706;
    --crimson: #e11d48;
    --cyan: #0284c7;
    --shadow-card: 4px 4px 0px #111827;
    --shadow-btn: 3px 3px 0px #111827;
  }

  /* Style 2: Studio (Rounded, soft 1px hairline) */
  body[data-style="studio"] {
    --radius: 10px;
    --border-w: 1px;
    --thumb-w: 16px;
    --thumb-h: 16px;
    --thumb-radius: 50%;
    --shadow-card: 0 4px 16px rgba(0, 0, 0, 0.28);
    --shadow-btn: 0 2px 8px rgba(0, 229, 153, 0.25);
  }
  body[data-style="studio"][data-theme="light"] {
    --shadow-card: 0 2px 10px rgba(0, 0, 0, 0.06);
    --shadow-btn: 0 2px 8px rgba(5, 150, 105, 0.22);
  }

  /* Style 3: Minimal (Flat, crisp, zero shadows) */
  body[data-style="minimal"] {
    --radius: 4px;
    --border-w: 1px;
    --thumb-w: 10px;
    --thumb-h: 18px;
    --thumb-radius: 2px;
    --shadow-card: none;
    --shadow-btn: none;
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
    padding: 12px 20px;
    background: var(--surface);
    border-bottom: var(--border-w) solid var(--border);
    gap: 12px;
  }

  .brand {
    display: flex;
    align-items: center;
    gap: 11px;
  }

  .brand-mark {
    width: 36px;
    height: 36px;
    display: grid;
    place-items: center;
    flex-shrink: 0;
  }

  .brand-title { display: flex; flex-direction: column; gap: 2px; }
  .brand-name {
    font-size: 15.5px;
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
    font-size: 10px;
    font-weight: 700;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--text-muted);
  }
  .dot { width: 7px; height: 7px; border-radius: var(--radius); background: var(--text-muted); }
  .status-pill.live { color: var(--mint); }
  .status-pill.live .dot { background: var(--mint); }

  .header-controls {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .segmented {
    display: inline-flex;
    background: var(--bg);
    border: var(--border-w) solid var(--border);
    border-radius: var(--radius);
    padding: 2px;
    gap: 2px;
  }

  .seg-btn {
    background: transparent;
    border: none;
    color: var(--text-dim);
    font-family: var(--mono);
    font-size: 10.5px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    padding: 5px 9px;
    border-radius: var(--radius);
    cursor: pointer;
  }
  .seg-btn:hover { color: var(--text); }
  .seg-btn.active {
    background: var(--border-hi);
    color: var(--bg);
  }

  .style-select {
    height: 32px;
    padding: 0 8px;
    border-radius: var(--radius);
    border: var(--border-w) solid var(--border);
    background: var(--surface-2);
    color: var(--text);
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    cursor: pointer;
    outline: none;
  }
  .style-select:hover { border-color: var(--border-hi); }

  .btn-icon {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 32px;
    height: 32px;
    border-radius: var(--radius);
    border: var(--border-w) solid var(--border);
    background: var(--surface-2);
    color: var(--text);
    font-family: var(--mono);
    font-weight: 800;
    font-size: 13px;
    cursor: pointer;
    transition: transform 0.08s;
  }
  .btn-icon:hover { border-color: var(--border-hi); }
  .btn-icon:active { transform: translate(1px, 1px); }

  .btn-primary {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 7px 16px;
    border-radius: var(--radius);
    border: var(--border-w) solid var(--border-hi);
    background: var(--mint);
    color: var(--mint-ink);
    font-family: var(--mono);
    font-size: 12px;
    font-weight: 800;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    cursor: pointer;
    box-shadow: var(--shadow-btn);
    transition: transform 0.08s, box-shadow 0.08s;
  }
  .btn-primary:hover { transform: translate(-1px, -1px); }
  .btn-primary:active { transform: translate(2px, 2px); box-shadow: none; }
  .btn-primary.stop {
    background: var(--crimson);
    color: #ffffff;
  }

  /* Main */
  .main {
    flex: 1;
    overflow-y: auto;
    padding: 16px 20px;
    display: flex;
    flex-direction: column;
    gap: 13px;
  }
  .main::-webkit-scrollbar { width: 6px; }
  .main::-webkit-scrollbar-thumb { background: var(--border); }

  .section-head { display: flex; align-items: center; justify-content: space-between; }
  .section-title {
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: var(--text);
  }
  .section-sub { font-size: 11.5px; color: var(--text-muted); }

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
    background: rgba(255, 51, 85, 0.14);
    border: var(--border-w) solid var(--crimson);
    color: var(--text);
  }
  .banner.hint {
    background: rgba(255, 184, 0, 0.10);
    border: var(--border-w) solid var(--amber);
    color: var(--text);
  }
  .kbd {
    display: inline-block;
    padding: 1px 6px;
    background: var(--bg);
    border: var(--border-w) solid var(--amber);
    border-radius: var(--radius);
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    color: var(--amber);
  }

  .device-list { display: flex; flex-direction: column; gap: 12px; }

  .device-card {
    background: var(--surface);
    border: var(--border-w) solid var(--border);
    border-radius: var(--radius);
    padding: 14px 16px;
    transition: border-color 0.12s, opacity 0.12s;
  }
  .device-card.enabled {
    border-color: var(--border-hi);
    box-shadow: var(--shadow-card);
  }
  .device-card.disabled { opacity: 0.55; }

  .device-top { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
  .device-id-group { display: flex; align-items: center; gap: 12px; min-width: 0; }

  .switch { position: relative; width: 40px; height: 22px; flex-shrink: 0; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .switch-track {
    position: absolute;
    inset: 0;
    background: var(--bg);
    border: var(--border-w) solid var(--border);
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
    border-radius: var(--radius);
    background: var(--text-muted);
    transition: transform 0.12s, background 0.12s;
  }
  body[data-style="studio"] .switch-track,
  body[data-style="studio"] .switch-track::after { border-radius: 999px; }

  .switch input:checked + .switch-track {
    background: var(--mint);
    border-color: var(--border-hi);
  }
  .switch input:checked + .switch-track::after {
    transform: translateX(18px);
    background: var(--mint-ink);
  }

  .dev-icon {
    width: 32px;
    height: 32px;
    border-radius: var(--radius);
    background: var(--surface-2);
    border: var(--border-w) solid var(--border);
    display: grid;
    place-items: center;
    color: var(--text);
    flex-shrink: 0;
  }

  .dev-meta { display: flex; flex-direction: column; min-width: 0; }
  .dev-name {
    font-size: 14px;
    font-weight: 700;
    color: var(--text);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .dev-tags { display: flex; align-items: center; gap: 6px; margin-top: 3px; }
  .tag {
    font-family: var(--mono);
    font-size: 10px;
    font-weight: 700;
    text-transform: uppercase;
    padding: 1px 6px;
    border-radius: var(--radius);
    background: var(--bg);
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
    border-radius: var(--radius);
    background: var(--bg);
    border: var(--border-w) solid var(--border);
    color: var(--text-dim);
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .dev-badge.locked {
    color: var(--mint-ink);
    background: var(--mint);
    border-color: var(--border-hi);
  }

  .device-controls {
    display: grid;
    grid-template-columns: 1fr 1.25fr;
    gap: 18px;
    margin-top: 14px;
    padding-top: 13px;
    border-top: var(--border-w) solid var(--border);
  }
  .device-card.disabled .device-controls { display: none; }

  .ctrl-group { display: flex; flex-direction: column; gap: 8px; }
  .ctrl-label-row { display: flex; align-items: center; justify-content: space-between; }
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
  .slider-row { display: flex; align-items: center; gap: 7px; }

  input[type="range"] {
    -webkit-appearance: none;
    appearance: none;
    flex: 1;
    height: 7px;
    border-radius: var(--radius);
    background: var(--surface-3);
    border: 1px solid var(--border);
    outline: none;
    cursor: pointer;
  }
  input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    appearance: none;
    width: var(--thumb-w);
    height: var(--thumb-h);
    border-radius: var(--thumb-radius);
    background: var(--border-hi);
    border: 2px solid var(--bg);
    box-shadow: 1px 1px 0px var(--bg);
    cursor: pointer;
  }
  input[type="range"]::-webkit-slider-thumb:hover { background: var(--mint); }

  .mini-btn {
    height: 26px;
    padding: 0 8px;
    border-radius: var(--radius);
    border: var(--border-w) solid var(--border);
    background: var(--bg);
    color: var(--text);
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    cursor: pointer;
  }
  .mini-btn:hover { border-color: var(--border-hi); background: var(--surface-3); }
  .mini-btn.active {
    background: var(--crimson);
    color: #ffffff;
    border-color: var(--border-hi);
  }

  /* Footer & Diagnostics */
  .footer { background: var(--surface); border-top: var(--border-w) solid var(--border); }
  .diag-drawer {
    display: none;
    padding: 12px 20px;
    border-bottom: var(--border-w) solid var(--border);
    background: var(--bg);
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
    border-bottom: var(--border-w) solid var(--border);
  }
  .diag-table td { padding: 6px 8px; color: var(--text); border-bottom: 1px solid var(--border); }

  .footer-bar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 9px 20px;
    font-size: 11.5px;
    color: var(--text-dim);
  }
  .footer-stats { font-family: var(--mono); font-variant-numeric: tabular-nums; }
  .btn-link {
    background: var(--surface-2);
    border: var(--border-w) solid var(--border);
    border-radius: var(--radius);
    padding: 3px 10px;
    color: var(--text);
    font-family: var(--mono);
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    cursor: pointer;
  }
  .btn-link:hover { border-color: var(--border-hi); }

  /* Cute First-Time Guide Modal */
  .modal-backdrop {
    display: none;
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.82);
    z-index: 100;
    place-items: center;
    padding: 20px;
  }
  .modal-backdrop.open { display: grid; }
  .modal {
    width: 100%;
    max-width: 540px;
    background: var(--surface);
    border: var(--border-w) solid var(--border-hi);
    border-radius: var(--radius);
    box-shadow: 6px 6px 0px var(--mint);
    padding: 22px 24px;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }
  .modal-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .modal-title {
    font-size: 18px;
    font-weight: 800;
    color: var(--text);
  }
  .modal-tagline {
    font-size: 12.5px;
    color: var(--mint);
    font-weight: 600;
    margin-top: 2px;
  }
  .guide-steps {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  .guide-step {
    background: var(--bg);
    border: var(--border-w) solid var(--border);
    border-radius: var(--radius);
    padding: 10px 12px;
    display: flex;
    gap: 12px;
    align-items: flex-start;
  }
  .step-num {
    width: 24px;
    height: 24px;
    flex-shrink: 0;
    background: var(--mint);
    color: var(--mint-ink);
    font-family: var(--mono);
    font-weight: 800;
    font-size: 12px;
    display: grid;
    place-items: center;
    border-radius: var(--radius);
  }
  .step-body h4 { font-size: 13px; font-weight: 700; color: var(--text); margin-bottom: 2px; }
  .step-body p { font-size: 12px; color: var(--text-dim); line-height: 1.45; }
  .modal-foot {
    display: flex;
    justify-content: flex-end;
    padding-top: 4px;
  }
</style>
</head>
<body data-theme="dark" data-style="brutalist">
<div class="app">
  <header class="header">
    <div class="brand">
      <div class="brand-mark">
        <svg width="36" height="36" viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <linearGradient id="swFrame" x1="120" y1="130" x2="900" y2="900" gradientUnits="userSpaceOnUse">
              <stop stop-color="#13C8FF"/><stop offset="0.52" stop-color="#2867FF"/><stop offset="1" stop-color="#B23BFF"/>
            </linearGradient>
            <linearGradient id="swWave" x1="300" y1="300" x2="740" y2="730" gradientUnits="userSpaceOnUse">
              <stop stop-color="#20D8FF"/><stop offset="0.48" stop-color="#286BFF"/><stop offset="1" stop-color="#B33CFF"/>
            </linearGradient>
            <linearGradient id="swBars" x1="180" y1="0" x2="844" y2="0" gradientUnits="userSpaceOnUse">
              <stop stop-color="#16D5FF"/><stop offset="0.55" stop-color="#267BFF"/><stop offset="1" stop-color="#B43DFF"/>
            </linearGradient>
          </defs>
          <rect x="72" y="72" width="880" height="880" rx="220" fill="#080D20"/>
          <rect x="80" y="80" width="864" height="864" rx="212" fill="none" stroke="url(#swFrame)" stroke-width="24"/>
          <g fill="none" stroke="url(#swBars)" stroke-width="38" stroke-linecap="round">
            <path d="M190 496v32"/><path d="M250 442v140"/><path d="M310 390v244"/><path d="M370 452v120"/>
            <path d="M654 452v120"/><path d="M714 390v244"/><path d="M774 442v140"/><path d="M834 496v32"/>
          </g>
          <path d="M690 300H470c-105 0-176 61-176 145 0 68 48 110 137 141l160 56c54 19 82 47 82 83 0 44-39 75-105 75H334"
                fill="none" stroke="url(#swWave)" stroke-width="92" stroke-linecap="round" stroke-linejoin="round"/>
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

      <select class="style-select" id="styleSelect" title="UI Style Preset">
        <option value="brutalist">UI: Brutalist</option>
        <option value="studio">UI: Studio</option>
        <option value="minimal">UI: Minimal</option>
      </select>

      <button class="btn-icon" id="btnTheme" title="Toggle Dark / Light Mode">
        <span id="themeIcon">&#9728;</span>
      </button>

      <button class="btn-icon" id="btnGuide" title="Quick Start Guide">?</button>

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
      <span style="display:flex;align-items:center;gap:10px;">
        <span class="section-sub">50% default hardware volume, auto-saved.</span>
        <button class="mini-btn" id="btnTestClick" title="Play a short crisp click across all enabled outputs to test sync">Test Click</button>
      </span>
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
      <span style="display:flex;align-items:center;gap:12px;">
        <span class="footer-stats" style="color:var(--text-muted);">Built by Ankush</span>
        <button class="btn-link" id="btnToggleDiag">Diagnostics &#9662;</button>
      </span>
    </div>
  </footer>
</div>

<!-- Friendly First-Time Welcome Guide -->
<div class="modal-backdrop" id="guideModal">
  <div class="modal">
    <div class="modal-head">
      <div>
        <div class="modal-title">Hey! Welcome to SyncWave</div>
        <div class="modal-tagline">Movie night sorted &mdash; no more &ldquo;bro pause it, my earbuds are lagging!&rdquo;</div>
      </div>
      <button class="btn-icon" id="btnCloseGuide">&times;</button>
    </div>
    <div class="guide-steps">
      <div class="guide-step">
        <div class="step-num">1</div>
        <div class="step-body">
          <h4>Turn ON everyone&rsquo;s speakers or earbuds</h4>
          <p>Flip the switch ON for each device you want to use together. Everyone gets their own volume slider (starting at a chill <strong>50%</strong> so nobody&rsquo;s ears get blasted), and we&rsquo;ll remember your levels next time.</p>
        </div>
      </div>
      <div class="guide-step">
        <div class="step-num">2</div>
        <div class="step-body">
          <h4>Click Start Sync &amp; play anything you want</h4>
          <p>Hit <strong>Start Sync</strong>, then open VLC, YouTube, Netflix, or Spotify as usual. No need to mess with Windows Sound settings &mdash; everything goes back to normal automatically when you stop or close SyncWave.</p>
        </div>
      </div>
      <div class="guide-step">
        <div class="step-num">3</div>
        <div class="step-body">
          <h4>Hear an echo? Fix it in 5 seconds</h4>
          <p>Bluetooth earbuds naturally arrive a tiny bit later than wired speakers. Slowly slide <strong>Sync Delay</strong> up on the <em>wired speaker</em> until both ears lock together. (Tip: in VLC, press <span class="kbd">J</span> if mouths look slightly ahead of the audio.)</p>
        </div>
      </div>
    </div>
    <div class="modal-foot">
      <button class="btn-primary" id="btnGuideDone">Got it, let&rsquo;s go!</button>
    </div>
  </div>
</div>

<script>
  const PREFS_KEY = 'syncwave_prefs_v1';
  const ONBOARD_KEY = 'syncwave_onboarded_v1';

  const state = {
    devices: [],
    enabledIds: new Set(),
    volumes: {},
    muted: {},
    delays: {},
    syncMode: 'adaptive',
    theme: 'dark',
    uiStyle: 'brutalist',
    isRunning: false,
    telemetry: null,
    diagOpen: false
  };

  function loadPrefs() {
    try {
      const raw = localStorage.getItem(PREFS_KEY);
      return raw ? JSON.parse(raw) : { syncMode: 'adaptive', theme: 'dark', uiStyle: 'brutalist', devices: {} };
    } catch {
      return { syncMode: 'adaptive', theme: 'dark', uiStyle: 'brutalist', devices: {} };
    }
  }

  function savePrefs() {
    try {
      const prefs = {
        syncMode: state.syncMode,
        theme: state.theme,
        uiStyle: state.uiStyle,
        devices: {}
      };
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

  function applyAppearance() {
    document.body.setAttribute('data-theme', state.theme);
    document.body.setAttribute('data-style', state.uiStyle);
    document.getElementById('styleSelect').value = state.uiStyle;
    document.getElementById('themeIcon').innerHTML = state.theme === 'dark' ? '&#9728;' : '&#9790;';
    post({ cmd: 'setTheme', mode: state.theme });
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
    return `background: linear-gradient(to right, ${colorVar} 0%, ${colorVar} ${pct}%, var(--surface-3) ${pct}%, var(--surface-3) 100%)`;
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

  document.querySelectorAll('.seg-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.seg-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      state.syncMode = btn.dataset.mode;
      post({ cmd: 'setSyncMode', mode: state.syncMode });
      savePrefs();
    });
  });

  document.getElementById('styleSelect').addEventListener('change', (e) => {
    state.uiStyle = e.target.value;
    applyAppearance();
    savePrefs();
  });

  document.getElementById('btnTheme').addEventListener('click', () => {
    state.theme = state.theme === 'dark' ? 'light' : 'dark';
    applyAppearance();
    savePrefs();
  });

  function openGuide() { document.getElementById('guideModal').classList.add('open'); }
  function closeGuide() {
    document.getElementById('guideModal').classList.remove('open');
    try { localStorage.setItem(ONBOARD_KEY, '1'); } catch {}
  }

  document.getElementById('btnGuide').addEventListener('click', openGuide);
  document.getElementById('btnCloseGuide').addEventListener('click', closeGuide);
  document.getElementById('btnGuideDone').addEventListener('click', closeGuide);

  document.getElementById('btnRefresh').addEventListener('click', () => {
    post({ cmd: 'refreshDevices' });
  });

  document.getElementById('btnTestClick').addEventListener('click', () => {
    const outputs = getSelectedOrderedIds();
    applyAllEndpointVolumes();
    pushAllDelays();
    post({ cmd: 'playClick', outputs });
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
        state.theme = prefs.theme || 'dark';
        state.uiStyle = prefs.uiStyle || 'brutalist';
        applyAppearance();

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
            if (!d.isVirtual) state.enabledIds.add(d.id);
            state.volumes[d.id] = 50;
            state.muted[d.id] = false;
            state.delays[d.id] = 0;
          }
        });
        savePrefs();
        renderDevices();
        applyAllEndpointVolumes();

        try {
          if (!localStorage.getItem(ONBOARD_KEY)) {
            openGuide();
          }
        } catch {}
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

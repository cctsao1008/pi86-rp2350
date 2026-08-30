#!/usr/bin/env python3
"""Local web console for the canonical RP86 host runtime.

This intentionally stays dependency-free and delegates hardware ownership to
``tools/rp86.py``.  It is a local development UI, not a remotely exposed
service.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
RP86 = ROOT / "rp86.py"

INDEX_HTML = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RP86 Development Console</title>
<style>
:root {
  color-scheme: dark;
  --bg: #0b0f14;
  --panel: #111820;
  --panel2: #0d141b;
  --line: #26323e;
  --text: #e7edf3;
  --muted: #91a0ad;
  --good: #6fdc8c;
  --bad: #ff7b72;
  --warn: #f2cc60;
  --accent: #7aa2f7;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  background: var(--bg);
  color: var(--text);
  font: 14px/1.45 ui-monospace, SFMono-Regular, Consolas, monospace;
}
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 18px 22px;
  border-bottom: 1px solid var(--line);
  background: #0d1218;
  position: sticky;
  top: 0;
  z-index: 10;
}
h1 { font-size: 18px; margin: 0; }
.sub { color: var(--muted); font-size: 12px; }
main {
  display: grid;
  grid-template-columns: repeat(12, minmax(0, 1fr));
  gap: 14px;
  padding: 16px;
  max-width: 1500px;
  margin: 0 auto;
}
.card {
  grid-column: span 6;
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 10px;
  overflow: hidden;
  min-height: 180px;
}
.card.wide { grid-column: span 12; }
.card-head {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  align-items: center;
  padding: 11px 13px;
  background: var(--panel2);
  border-bottom: 1px solid var(--line);
  font-weight: 700;
}
.card-body { padding: 13px; }
button {
  border: 1px solid #3a4c60;
  background: #182330;
  color: var(--text);
  border-radius: 7px;
  padding: 7px 11px;
  cursor: pointer;
  font: inherit;
}
button:hover { border-color: var(--accent); }
button.danger:hover { border-color: var(--bad); color: var(--bad); }
button:disabled { opacity: .5; cursor: wait; }
.toolbar { display: flex; flex-wrap: wrap; gap: 8px; }
.statusline { display: flex; gap: 10px; align-items: center; margin-bottom: 10px; }
.dot { width: 9px; height: 9px; border-radius: 50%; background: var(--muted); display: inline-block; }
.dot.good { background: var(--good); box-shadow: 0 0 10px #6fdc8c80; }
.dot.bad { background: var(--bad); }
pre {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-word;
  background: #080c10;
  border: 1px solid #1d2730;
  border-radius: 7px;
  padding: 11px;
  min-height: 115px;
  max-height: 420px;
  overflow: auto;
}
table { width: 100%; border-collapse: collapse; }
th, td { text-align: left; padding: 7px 8px; border-bottom: 1px solid var(--line); }
th { color: var(--muted); font-weight: 600; }
.muted { color: var(--muted); }
.good { color: var(--good); }
.bad { color: var(--bad); }
.warn { color: var(--warn); }
@media (max-width: 850px) {
  .card { grid-column: span 12; }
  header { align-items: flex-start; flex-direction: column; }
}
</style>
</head>
<body>
<header>
  <div>
    <h1>RP86 Development Console</h1>
    <div class="sub">Intel 8086 / NEC V30 × RP2350 · local host console</div>
  </div>
  <div class="toolbar">
    <button id="refreshAll">Refresh</button>
    <button id="autoBtn">Auto refresh: ON</button>
  </div>
</header>
<main>
  <section class="card">
    <div class="card-head"><span>Runtime status</span><span id="runtimeBadge" class="muted">unknown</span></div>
    <div class="card-body">
      <div class="statusline"><span id="runtimeDot" class="dot"></span><span id="runtimeText">Not checked</span></div>
      <pre id="statusOut">Press Refresh to query the canonical RP86 runtime.</pre>
    </div>
  </section>

  <section class="card">
    <div class="card-head"><span>USB / HID devices</span><button id="devicesBtn">Scan</button></div>
    <div class="card-body">
      <div id="deviceTable" class="muted">Not scanned</div>
    </div>
  </section>

  <section class="card">
    <div class="card-head"><span>Runtime control</span><span class="warn">physical hardware</span></div>
    <div class="card-body">
      <div class="toolbar" style="margin-bottom:12px">
        <button id="rebootBtn" class="danger">Reboot RP2350</button>
        <button id="bootBtn" class="danger">Enter UF2 bootloader</button>
      </div>
      <div class="muted">These actions use the same canonical control path as <code>tools/rp86.py</code>.</div>
    </div>
  </section>

  <section class="card">
    <div class="card-head"><span>Protocol self-test</span><button id="simulateBtn">Run simulation</button></div>
    <div class="card-body"><pre id="simulateOut">Not run</pre></div>
  </section>

  <section class="card wide">
    <div class="card-head"><span>Host event log</span><button id="clearLog">Clear</button></div>
    <div class="card-body"><pre id="logOut"></pre></div>
  </section>
</main>
<script>
const $ = id => document.getElementById(id);
let autoRefresh = true;
let busyStatus = false;

function now() { return new Date().toLocaleTimeString(); }
function log(msg) {
  const el = $('logOut');
  el.textContent += `[${now()}] ${msg}\n`;
  el.scrollTop = el.scrollHeight;
}
async function api(path, options={}) {
  const response = await fetch(path, options);
  const data = await response.json().catch(() => ({ok:false,error:'invalid JSON response'}));
  if (!response.ok || !data.ok) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}
async function refreshStatus() {
  if (busyStatus) return;
  busyStatus = true;
  try {
    const data = await api('/api/status');
    $('statusOut').textContent = data.stdout || '(no output)';
    $('runtimeDot').className = 'dot good';
    $('runtimeText').textContent = 'RP86 runtime reachable';
    $('runtimeBadge').textContent = 'ONLINE';
    $('runtimeBadge').className = 'good';
  } catch (e) {
    $('statusOut').textContent = String(e.message);
    $('runtimeDot').className = 'dot bad';
    $('runtimeText').textContent = 'Runtime not reachable';
    $('runtimeBadge').textContent = 'OFFLINE';
    $('runtimeBadge').className = 'bad';
  } finally { busyStatus = false; }
}
async function scanDevices() {
  $('devicesBtn').disabled = true;
  try {
    const data = await api('/api/devices');
    const rows = data.devices || [];
    if (!rows.length) {
      $('deviceTable').innerHTML = '<span class="muted">No RP86 HID device found.</span>';
      return;
    }
    $('deviceTable').innerHTML = `<table><thead><tr><th>VID</th><th>PID</th><th>Serial</th><th>Product</th></tr></thead><tbody>${rows.map(d => `<tr><td>${d.vid ?? ''}</td><td>${d.pid ?? ''}</td><td>${d.serial ?? ''}</td><td>${d.product ?? ''}</td></tr>`).join('')}</tbody></table>`;
    log(`Found ${rows.length} RP86 HID device(s)`);
  } catch (e) {
    $('deviceTable').innerHTML = `<span class="bad">${e.message}</span>`;
    log(`Device scan failed: ${e.message}`);
  } finally { $('devicesBtn').disabled = false; }
}
async function simulate() {
  $('simulateBtn').disabled = true;
  try {
    const data = await api('/api/simulate');
    $('simulateOut').textContent = JSON.stringify(data.result, null, 2);
    log('Protocol simulation PASS');
  } catch (e) {
    $('simulateOut').textContent = e.message;
    log(`Protocol simulation failed: ${e.message}`);
  } finally { $('simulateBtn').disabled = false; }
}
async function control(action, label) {
  if (!confirm(`${label}?`)) return;
  const button = action === 'reboot' ? $('rebootBtn') : $('bootBtn');
  button.disabled = true;
  try {
    const data = await api('/api/control', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({action})
    });
    log(`${label}: ${data.stdout.trim() || 'acknowledged'}`);
    setTimeout(refreshStatus, 1200);
  } catch (e) {
    log(`${label} failed: ${e.message}`);
    alert(e.message);
  } finally { button.disabled = false; }
}
$('refreshAll').onclick = () => { refreshStatus(); scanDevices(); };
$('devicesBtn').onclick = scanDevices;
$('simulateBtn').onclick = simulate;
$('rebootBtn').onclick = () => control('reboot', 'Reboot RP2350');
$('bootBtn').onclick = () => control('bootloader', 'Enter UF2 bootloader');
$('clearLog').onclick = () => $('logOut').textContent = '';
$('autoBtn').onclick = () => {
  autoRefresh = !autoRefresh;
  $('autoBtn').textContent = `Auto refresh: ${autoRefresh ? 'ON' : 'OFF'}`;
};
setInterval(() => { if (autoRefresh) refreshStatus(); }, 2500);
refreshStatus();
scanDevices();
</script>
</body>
</html>
"""


def _run_rp86(*args: str, timeout: float = 8.0) -> dict[str, object]:
    command = [sys.executable, str(RP86), *args]
    try:
        completed = subprocess.run(
            command,
            cwd=str(ROOT.parent),
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        return {
            "ok": False,
            "error": f"RP86 command timed out after {timeout:.1f}s",
            "stdout": exc.stdout or "",
            "stderr": exc.stderr or "",
        }
    return {
        "ok": completed.returncode == 0,
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "command": command[2:],
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "RP86Web/1.0"

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"[rp86-web] {self.address_string()} - {fmt % args}")

    def _send_json(self, payload: dict[str, object], status: int = 200) -> None:
        raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def _send_html(self) -> None:
        raw = INDEX_HTML.encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/":
            self._send_html()
            return
        if path == "/api/status":
            result = _run_rp86("--status", "--timeout", "3", timeout=5.0)
            self._send_json(result, 200 if result["ok"] else 503)
            return
        if path == "/api/devices":
            result = _run_rp86("--list-devices", "--json", timeout=5.0)
            if not result["ok"]:
                self._send_json(result, 503)
                return
            try:
                devices = json.loads(str(result["stdout"]))
            except json.JSONDecodeError as exc:
                self._send_json({"ok": False, "error": f"invalid device JSON: {exc}"}, 500)
                return
            self._send_json({"ok": True, "devices": devices})
            return
        if path == "/api/simulate":
            result = _run_rp86("--simulate", "--json", timeout=3.0)
            if not result["ok"]:
                self._send_json(result, 500)
                return
            try:
                simulation = json.loads(str(result["stdout"]))
            except json.JSONDecodeError as exc:
                self._send_json({"ok": False, "error": f"invalid simulation JSON: {exc}"}, 500)
                return
            self._send_json({"ok": True, "result": simulation})
            return
        self._send_json({"ok": False, "error": "not found"}, 404)

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        if path != "/api/control":
            self._send_json({"ok": False, "error": "not found"}, 404)
            return
        length = int(self.headers.get("Content-Length", "0") or "0")
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            self._send_json({"ok": False, "error": "invalid JSON body"}, 400)
            return
        action = payload.get("action")
        if action not in {"reboot", "bootloader"}:
            self._send_json({"ok": False, "error": "unsupported control action"}, 400)
            return
        flag = "--reboot" if action == "reboot" else "--bootloader"
        result = _run_rp86(flag, "--timeout", "5", timeout=8.0)
        self._send_json(result, 200 if result["ok"] else 503)


def main() -> int:
    parser = argparse.ArgumentParser(description="local Web GUI for tools/rp86.py")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8086)
    args = parser.parse_args()

    if args.host not in {"127.0.0.1", "localhost", "::1"}:
        parser.error("rp86_web is intentionally local-only; use 127.0.0.1 or localhost")

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"RP86 Web Console: http://{args.host}:{args.port}")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

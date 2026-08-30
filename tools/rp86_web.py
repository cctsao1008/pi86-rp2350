#!/usr/bin/env python3
"""Local web console for the canonical RP86 host runtime.

The web UI is dependency-free and local-only.  If an RP86 Host Broker already
exists, the Web console attaches as a client.  If no broker exists, it starts a
headless persistent RP86 session which becomes the single hardware owner and
publishes broker telemetry for the Web UI and other clients.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
RP86 = ROOT / "rp86.py"

# tools/ is already on sys.path when this file is launched as tools/rp86_web.py.
from rp86_runtime.broker import BrokerClient, discover_brokers, select_broker

_owned_runtime: subprocess.Popen[str] | None = None
_owner_mode = "not-started"
_owner_error: str | None = None

INDEX_HTML = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RP86 Development Console</title>
<script>
(()=>{try{const saved=localStorage.getItem('rp86-theme');document.documentElement.dataset.theme=saved==='light'?'light':'dark'}catch(_){document.documentElement.dataset.theme='dark'}})();
</script>
<style>
:root{color-scheme:dark;--bg:#0b0f14;--panel:#111820;--panel2:#0d141b;--line:#26323e;--text:#e7edf3;--muted:#91a0ad;--good:#6fdc8c;--bad:#ff7b72;--warn:#f2cc60;--accent:#7aa2f7;--header:#0d1218;--control:#182330;--control-line:#3a4c60;--pre:#080c10;--pre-line:#1d2730;--placeholder-line:#314050;--metric-line:#27323d}
:root[data-theme="light"]{color-scheme:light;--bg:#f3f5f7;--panel:#ffffff;--panel2:#f7f9fb;--line:#c9d1d9;--text:#1f2328;--muted:#59636e;--good:#1a7f37;--bad:#cf222e;--warn:#9a6700;--accent:#0969da;--header:#ffffff;--control:#f6f8fa;--control-line:#afb8c1;--pre:#f6f8fa;--pre-line:#d0d7de;--placeholder-line:#afb8c1;--metric-line:#d8dee4}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 ui-monospace,SFMono-Regular,Consolas,monospace;transition:background .15s ease,color .15s ease}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:18px 22px;border-bottom:1px solid var(--line);background:var(--header);position:sticky;top:0;z-index:10}h1{font-size:18px;margin:0}.sub{color:var(--muted);font-size:12px}
main{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:14px;padding:16px;max-width:1600px;margin:0 auto}.card{grid-column:span 6;background:var(--panel);border:1px solid var(--line);border-radius:10px;overflow:hidden;min-height:170px}.card.wide{grid-column:span 12}.card.third{grid-column:span 4}
.card-head{display:flex;justify-content:space-between;gap:12px;align-items:center;padding:11px 13px;background:var(--panel2);border-bottom:1px solid var(--line);font-weight:700}.card-body{padding:13px}
button,input{border:1px solid var(--control-line);background:var(--control);color:var(--text);border-radius:7px;padding:7px 11px;font:inherit}button{cursor:pointer}button:hover{border-color:var(--accent)}button.danger:hover{border-color:var(--bad);color:var(--bad)}button:disabled{opacity:.5;cursor:wait}
.toolbar{display:flex;flex-wrap:wrap;gap:8px}.statusline{display:flex;gap:10px;align-items:center;margin-bottom:10px}.dot{width:9px;height:9px;border-radius:50%;background:var(--muted);display:inline-block}.dot.good{background:var(--good);box-shadow:0 0 10px color-mix(in srgb,var(--good) 50%,transparent)}.dot.bad{background:var(--bad)}
.metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:7px 18px}.metric{display:flex;justify-content:space-between;border-bottom:1px dotted var(--metric-line);padding:5px 0}.metric .k{color:var(--muted)}.metric .v{text-align:right}.hero{font-size:20px;font-weight:700;margin-bottom:12px}.hero small{font-size:12px;font-weight:400;color:var(--muted)}
pre{margin:0;white-space:pre-wrap;word-break:break-word;background:var(--pre);border:1px solid var(--pre-line);border-radius:7px;padding:11px;min-height:115px;max-height:420px;overflow:auto}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:7px 8px;border-bottom:1px solid var(--line)}th{color:var(--muted);font-weight:600}.muted{color:var(--muted)}.good{color:var(--good)}.bad{color:var(--bad)}.warn{color:var(--warn)}
.placeholder{border:1px dashed var(--placeholder-line);border-radius:8px;padding:16px;color:var(--muted);min-height:115px}.console-line{display:flex;gap:8px;margin-top:9px}.console-line input{flex:1}.small{font-size:12px}@media(max-width:1000px){.card,.card.third{grid-column:span 12}.metrics{grid-template-columns:1fr}header{align-items:flex-start;flex-direction:column}}
</style>
</head>
<body>
<header><div><h1>RP86 Development Console</h1><div class="sub">Intel 8086 / NEC V30 × RP2350 · physical-processor first</div></div><div class="toolbar"><button id="refreshAll">Refresh</button><button id="autoBtn">Auto refresh: ON</button><button id="themeBtn" title="Switch to light theme">☀ Light</button></div></header>
<main>
<section class="card"><div class="card-head"><span>Physical processor</span><span id="cpuBadge" class="muted">UNKNOWN</span></div><div class="card-body">
<div class="hero"><span id="cpuName">8086-class processor</span> <small id="brokerMode">waiting for telemetry</small></div><div class="statusline"><span id="cpuDot" class="dot"></span><span id="cpuAlive">Not checked</span></div>
<div class="metrics">
<div class="metric"><span class="k">Broker state</span><span class="v" id="cpuState">--</span></div><div class="metric"><span class="k">Physical processor</span><span class="v" id="cpuIdentity">UNKNOWN</span></div>
<div class="metric"><span class="k">Identity policy</span><span class="v" id="identityPolicy">--</span></div><div class="metric"><span class="k">Heartbeat</span><span class="v" id="heartbeat">--</span></div>
<div class="metric"><span class="k">RTT</span><span class="v" id="rtt">--</span></div><div class="metric"><span class="k">Request sequence</span><span class="v" id="requestSeq">--</span></div>
<div class="metric"><span class="k">CPU sequence</span><span class="v" id="cpuSeq">--</span></div><div class="metric"><span class="k">Device</span><span class="v" id="deviceId">--</span></div>
</div></div></section>

<section class="card"><div class="card-head"><span>Workload / execution</span><span id="workloadBadge" class="muted">TELEMETRY</span></div><div class="card-body"><div class="metrics">
<div class="metric"><span class="k">Workload ID</span><span class="v" id="workloadId">--</span></div><div class="metric"><span class="k">Workload state</span><span class="v" id="workloadState">--</span></div><div class="metric"><span class="k">Clock mode</span><span class="v" id="clockMode">--</span></div><div class="metric"><span class="k">CPU cycles</span><span class="v" id="cpuCycles">--</span></div><div class="metric"><span class="k">Boot ID</span><span class="v" id="bootId">--</span></div><div class="metric"><span class="k">Command sequence</span><span class="v" id="commandSeq">--</span></div>
</div><div class="muted small" style="margin-top:12px">The Web console now starts a background persistent RP86 owner when no Host Broker exists. Existing brokers are attached without taking hardware ownership.</div></div></section>

<section class="card wide"><div class="card-head"><span>Physical processor console</span><span class="muted">next native integration point</span></div><div class="card-body"><pre id="consoleOut">Console command transport is not exposed by the current broker RPC yet. The terminal CLI remains canonical for interactive commands.</pre><div class="console-line"><input id="consoleInput" placeholder="V30> (read-only placeholder in this version)" disabled><button disabled>Send</button></div></div></section>
<section class="card third"><div class="card-head"><span>Memory viewer</span><span class="muted">planned</span></div><div class="card-body"><div class="placeholder">Next step: broker-backed memory read/write API using the existing runtime memory service.</div></div></section>
<section class="card third"><div class="card-head"><span>Bus trace</span><span class="muted">planned</span></div><div class="card-body"><div class="placeholder">Next step: expose retained physical bus trace records through the broker instead of scraping status text.</div></div></section>
<section class="card third"><div class="card-head"><span>Runtime control</span><span class="warn">physical hardware</span></div><div class="card-body"><div class="toolbar" style="margin-bottom:12px"><button id="rebootBtn" class="danger">Reboot RP2350</button><button id="bootBtn" class="danger">Enter UF2 bootloader</button></div><div class="muted small">Uses the same canonical control path as <code>tools/rp86.py</code>.</div></div></section>
<section class="card"><div class="card-head"><span>RP2350 runtime status</span><span id="runtimeBadge" class="muted">unknown</span></div><div class="card-body"><div class="statusline"><span id="runtimeDot" class="dot"></span><span id="runtimeText">Not checked</span></div><pre id="statusOut">Press Refresh to query the canonical RP86 runtime.</pre></div></section>
<section class="card"><div class="card-head"><span>USB / HID devices</span><button id="devicesBtn">Scan</button></div><div class="card-body"><div id="deviceTable" class="muted">Not scanned</div></div></section>
<section class="card"><div class="card-head"><span>Protocol self-test</span><button id="simulateBtn">Run simulation</button></div><div class="card-body"><pre id="simulateOut">Not run</pre></div></section>
<section class="card"><div class="card-head"><span>Host broker</span><span id="brokerBadge" class="muted">unknown</span></div><div class="card-body"><pre id="brokerOut">No broker query yet.</pre></div></section>
<section class="card wide"><div class="card-head"><span>Host event log</span><button id="clearLog">Clear</button></div><div class="card-body"><pre id="logOut"></pre></div></section>
</main>
<script>
const $=id=>document.getElementById(id);let autoRefresh=true,busyStatus=false,busyProcessor=false;
function now(){return new Date().toLocaleTimeString()}function log(msg){const el=$('logOut');el.textContent+=`[${now()}] ${msg}\n`;el.scrollTop=el.scrollHeight}function val(v,f='--'){return v===null||v===undefined||v===''?f:String(v)}
function theme(){return document.documentElement.dataset.theme==='light'?'light':'dark'}function syncThemeButton(){$('themeBtn').textContent=theme()==='light'?'◐ Dark':'☀ Light';$('themeBtn').title=theme()==='light'?'Switch to dark theme':'Switch to light theme'}function toggleTheme(){const next=theme()==='dark'?'light':'dark';document.documentElement.dataset.theme=next;try{localStorage.setItem('rp86-theme',next)}catch(_){}syncThemeButton()}
async function api(path,options={}){const r=await fetch(path,options);const d=await r.json().catch(()=>({ok:false,error:'invalid JSON response'}));if(!r.ok||!d.ok)throw new Error(d.error||`HTTP ${r.status}`);return d}
function setCpuOffline(reason){$('cpuDot').className='dot bad';$('cpuAlive').textContent=reason||'No live broker telemetry';$('cpuBadge').textContent='NO BROKER';$('cpuBadge').className='warn';$('brokerMode').textContent='runtime-only view';$('cpuIdentity').textContent='UNKNOWN'}
function renderProcessor(data){const s=data.snapshot||{},native=s.native_processor||null,policy=(data.processor||'auto').toLowerCase(),completed=Number(s.completed||0),liveness=!!native&&completed>0;const names={'nec-v30':'NEC V30','intel-8086':'Intel 8086'};$('cpuName').textContent=native?(names[String(native).toLowerCase()]||String(native).toUpperCase()):'8086-class processor';$('cpuIdentity').textContent=native?(names[String(native).toLowerCase()]||String(native).toUpperCase()):'UNKNOWN';$('identityPolicy').textContent=policy==='auto'?'AUTO DETECT':policy.toUpperCase();$('cpuState').textContent=val(s.state);$('heartbeat').textContent=`${completed} completed / ${val(s.lost,0)} lost`;$('rtt').textContent=completed?`${Number(s.last_ms||0).toFixed(1)} ms`:'--';$('requestSeq').textContent=val(s.request_sequence);$('cpuSeq').textContent=val(s.cpu_sequence);$('deviceId').textContent=val(data.device_id);$('bootId').textContent=val(s.boot_id);$('commandSeq').textContent=val(s.command_sequence);$('workloadId').textContent=val(s.workload_id);$('workloadState').textContent=val(s.workload_state);$('clockMode').textContent=val(s.workload_clock_mode);$('cpuCycles').textContent=val(s.workload_cycles);if(liveness){$('cpuDot').className='dot good';$('cpuAlive').textContent='Physical processor liveness proven';$('cpuBadge').textContent='ALIVE';$('cpuBadge').className='good'}else{$('cpuDot').className='dot';$('cpuAlive').textContent='Host Broker active; CPU liveness not yet proven';$('cpuBadge').textContent='UNPROVEN';$('cpuBadge').className='warn'}const mode=data.owner_mode==='web-owned'?'Web-owned background runtime':data.owner_mode==='existing'?'Existing Host Broker':'Host Broker';$('brokerMode').textContent=`${mode} · ${val(data.device_id)}`;$('brokerBadge').textContent=data.owner_mode==='web-owned'?'WEB OWNER':'ACTIVE';$('brokerBadge').className='good';$('brokerOut').textContent=JSON.stringify(data,null,2)}
async function refreshProcessor(){if(busyProcessor)return;busyProcessor=true;try{renderProcessor(await api('/api/processor'))}catch(e){setCpuOffline(e.message);$('brokerBadge').textContent='NONE';$('brokerBadge').className='muted';$('brokerOut').textContent=e.message}finally{busyProcessor=false}}
async function refreshStatus(){if(busyStatus)return;busyStatus=true;try{const d=await api('/api/status');$('statusOut').textContent=d.stdout||'(no output)';$('runtimeDot').className='dot good';$('runtimeText').textContent='RP86 runtime reachable';$('runtimeBadge').textContent='ONLINE';$('runtimeBadge').className='good'}catch(e){$('statusOut').textContent=String(e.message);$('runtimeDot').className='dot bad';$('runtimeText').textContent='Runtime not reachable';$('runtimeBadge').textContent='OFFLINE';$('runtimeBadge').className='bad'}finally{busyStatus=false}}
async function scanDevices(){$('devicesBtn').disabled=true;try{const d=await api('/api/devices'),rows=d.devices||[];if(!rows.length){$('deviceTable').innerHTML='<span class="muted">No RP86 HID device found.</span>';return}$('deviceTable').innerHTML=`<table><thead><tr><th>VID</th><th>PID</th><th>Serial</th><th>Product</th></tr></thead><tbody>${rows.map(x=>`<tr><td>${x.vid??''}</td><td>${x.pid??''}</td><td>${x.serial??''}</td><td>${x.product??''}</td></tr>`).join('')}</tbody></table>`;log(`Found ${rows.length} RP86 HID device(s)`)}catch(e){$('deviceTable').innerHTML=`<span class="bad">${e.message}</span>`;log(`Device scan failed: ${e.message}`)}finally{$('devicesBtn').disabled=false}}
async function simulate(){$('simulateBtn').disabled=true;try{const d=await api('/api/simulate');$('simulateOut').textContent=JSON.stringify(d.result,null,2);log('Protocol simulation PASS')}catch(e){$('simulateOut').textContent=e.message;log(`Protocol simulation failed: ${e.message}`)}finally{$('simulateBtn').disabled=false}}
async function control(action,label){if(!confirm(`${label}?`))return;const b=action==='reboot'?$('rebootBtn'):$('bootBtn');b.disabled=true;try{const d=await api('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action})});log(`${label}: ${d.stdout.trim()||'acknowledged'}`);setTimeout(()=>{refreshStatus();refreshProcessor()},1200)}catch(e){log(`${label} failed: ${e.message}`);alert(e.message)}finally{b.disabled=false}}
function refreshAll(){refreshProcessor();refreshStatus();scanDevices()}$('refreshAll').onclick=refreshAll;$('devicesBtn').onclick=scanDevices;$('simulateBtn').onclick=simulate;$('rebootBtn').onclick=()=>control('reboot','Reboot RP2350');$('bootBtn').onclick=()=>control('bootloader','Enter UF2 bootloader');$('clearLog').onclick=()=>$('logOut').textContent='';$('autoBtn').onclick=()=>{autoRefresh=!autoRefresh;$('autoBtn').textContent=`Auto refresh: ${autoRefresh?'ON':'OFF'}`};$('themeBtn').onclick=toggleTheme;syncThemeButton();setInterval(()=>{if(autoRefresh){refreshProcessor();refreshStatus()}},2000);refreshAll();
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


def _active_broker():
    try:
        return select_broker(discover_brokers())
    except RuntimeError:
        raise


def _ensure_runtime_owner(wait_seconds: float = 4.0) -> dict[str, object]:
    """Attach to an existing broker or start one headlessly for the Web UI."""
    global _owned_runtime, _owner_mode, _owner_error

    try:
        record = _active_broker()
    except RuntimeError as exc:
        _owner_error = str(exc)
        return {"ok": False, "error": _owner_error}
    if record is not None:
        _owner_mode = "existing"
        _owner_error = None
        return {"ok": True, "mode": _owner_mode, "device_id": record.device_id}

    command = [
        sys.executable,
        str(RP86),
        "--interactive",
        "--heartbeat",
        "--attach",
        "--display",
        "quiet",
        "--interval",
        "1.0",
    ]
    try:
        _owned_runtime = subprocess.Popen(
            command,
            cwd=str(ROOT.parent),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except OSError as exc:
        _owner_error = f"failed to start background RP86 runtime: {exc}"
        return {"ok": False, "error": _owner_error}

    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        if _owned_runtime.poll() is not None:
            _owner_error = (
                "background RP86 runtime exited before publishing a Host Broker "
                f"(exit {_owned_runtime.returncode})"
            )
            return {"ok": False, "error": _owner_error}
        try:
            record = _active_broker()
        except RuntimeError as exc:
            _owner_error = str(exc)
            return {"ok": False, "error": _owner_error}
        if record is not None:
            _owner_mode = "web-owned"
            _owner_error = None
            return {"ok": True, "mode": _owner_mode, "device_id": record.device_id}
        time.sleep(0.1)

    _owner_mode = "starting"
    _owner_error = "background RP86 runtime is still starting; no Host Broker yet"
    return {"ok": False, "error": _owner_error}


def _stop_owned_runtime() -> None:
    global _owned_runtime
    process = _owned_runtime
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=2.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2.0)
    _owned_runtime = None


def _processor_snapshot() -> dict[str, object]:
    """Return read-only physical-processor telemetry from the active broker."""
    try:
        record = _active_broker()
    except RuntimeError as exc:
        return {"ok": False, "error": str(exc), "owner_mode": _owner_mode}
    if record is None:
        detail = _owner_error or "No active RP86 Host Broker."
        return {"ok": False, "error": detail, "owner_mode": _owner_mode}
    try:
        reply = BrokerClient(record, f"web-{os.getpid()}").hello()
    except (OSError, RuntimeError, ValueError) as exc:
        return {"ok": False, "error": f"Host Broker telemetry failed: {exc}"}
    if not reply.get("ok"):
        return {"ok": False, "error": str(reply.get("error") or "broker hello failed")}
    return {
        "ok": True,
        "owner_mode": _owner_mode,
        "device_id": record.device_id,
        "processor": reply.get("processor", record.processor),
        "tcp_port": record.tcp_port,
        "udp_port": record.udp_port,
        "snapshot": dict(reply.get("snapshot") or {}),
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "RP86Web/3.2"

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
        if path == "/api/processor":
            result = _processor_snapshot()
            self._send_json(result, 200 if result["ok"] else 503)
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
    parser = argparse.ArgumentParser(description="local RP86 development web console")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8086)
    args = parser.parse_args()
    if args.host not in {"127.0.0.1", "localhost", "::1"}:
        parser.error("rp86-web is intentionally local-only; use 127.0.0.1")

    ownership = _ensure_runtime_owner()
    if ownership.get("ok"):
        print(
            "RP86 runtime: "
            + ("attached to existing Host Broker" if ownership["mode"] == "existing" else "background owner started")
            + f" ({ownership.get('device_id', 'unknown')})"
        )
    else:
        print(f"RP86 runtime warning: {ownership.get('error')}")
        print("The Web console will still start so device/runtime state is visible.")

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"RP86 Development Console: http://{args.host}:{args.port}")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        _stop_owned_runtime()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

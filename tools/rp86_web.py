#!/usr/bin/env python3
"""Local web console for the canonical RP86 host runtime.

The web UI is dependency-free and local-only. If an RP86 Host Broker already
exists, the Web console attaches as a client. If no broker exists, it starts a
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
from rp86_runtime.core import Message, NativeServiceWitness, TYPE_COMMAND
from rp86_runtime.memory import format_memory_dump, memory_read_request, parse_memory_read

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
(()=>{try{const theme=localStorage.getItem('rp86-theme');const view=localStorage.getItem('rp86-view');document.documentElement.dataset.theme=theme==='light'?'light':'dark';document.documentElement.dataset.view=view==='runtime'?'runtime':'overview'}catch(_){document.documentElement.dataset.theme='dark';document.documentElement.dataset.view='overview'}})();
</script>
<style>
:root{color-scheme:dark;--bg:#0b0f14;--panel:#111820;--panel2:#0d141b;--line:#26323e;--text:#e7edf3;--muted:#91a0ad;--good:#6fdc8c;--bad:#ff7b72;--warn:#f2cc60;--accent:#7aa2f7;--header:#0d1218;--control:#182330;--control-line:#3a4c60;--pre:#080c10;--pre-line:#1d2730;--metric-line:#27323d}
:root[data-theme="light"]{color-scheme:light;--bg:#f3f5f7;--panel:#ffffff;--panel2:#f7f9fb;--line:#c9d1d9;--text:#1f2328;--muted:#59636e;--good:#1a7f37;--bad:#cf222e;--warn:#9a6700;--accent:#0969da;--header:#ffffff;--control:#f6f8fa;--control-line:#afb8c1;--pre:#f6f8fa;--pre-line:#d0d7de;--metric-line:#d8dee4}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 ui-monospace,SFMono-Regular,Consolas,monospace;transition:background .15s ease,color .15s ease}header{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:16px 22px;border-bottom:1px solid var(--line);background:var(--header);position:sticky;top:0;z-index:20}h1{font-size:18px;margin:0}.sub{color:var(--muted);font-size:12px}.toolbar,.tabs,.quick{display:flex;flex-wrap:wrap;gap:8px}.tabs button.active{border-color:var(--accent);color:var(--accent)}
.statebar{position:sticky;top:69px;z-index:15;display:flex;flex-wrap:wrap;gap:18px;align-items:center;padding:8px 22px;border-bottom:1px solid var(--line);background:var(--panel2);font-size:12px}.stateitem{display:flex;gap:7px;align-items:center}.dot{width:9px;height:9px;border-radius:50%;background:var(--muted);display:inline-block}.dot.good{background:var(--good);box-shadow:0 0 10px color-mix(in srgb,var(--good) 50%,transparent)}.dot.bad{background:var(--bad)}.dot.warn{background:var(--warn)}
main{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:14px;padding:16px;max-width:1600px;margin:0 auto}.card{grid-column:span 6;background:var(--panel);border:1px solid var(--line);border-radius:10px;overflow:hidden;min-height:170px}.card.wide{grid-column:span 12}.card.third{grid-column:span 4}.card-head{display:flex;justify-content:space-between;gap:12px;align-items:center;padding:11px 13px;background:var(--panel2);border-bottom:1px solid var(--line);font-weight:700}.card-body{padding:13px}
button,input{border:1px solid var(--control-line);background:var(--control);color:var(--text);border-radius:7px;padding:7px 11px;font:inherit}button{cursor:pointer}button:hover{border-color:var(--accent)}button.danger:hover{border-color:var(--bad);color:var(--bad)}button:disabled{opacity:.5;cursor:wait}.danger-zone{border-top:1px solid var(--line);margin-top:13px;padding-top:13px}.statusline{display:flex;gap:10px;align-items:center;margin-bottom:10px}
.metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:7px 18px}.metric{display:flex;justify-content:space-between;border-bottom:1px dotted var(--metric-line);padding:5px 0}.metric .k{color:var(--muted)}.metric .v{text-align:right}.hero{font-size:20px;font-weight:700;margin-bottom:12px}.hero small{font-size:12px;font-weight:400;color:var(--muted)}
pre{margin:0;white-space:pre-wrap;word-break:break-word;background:var(--pre);border:1px solid var(--pre-line);border-radius:7px;padding:11px;min-height:115px;max-height:420px;overflow:auto}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:7px 8px;border-bottom:1px solid var(--line)}th{color:var(--muted);font-weight:600}.muted{color:var(--muted)}.good{color:var(--good)}.bad{color:var(--bad)}.warn{color:var(--warn)}.small{font-size:12px}.console-line{display:flex;gap:8px;margin-top:9px}.console-line input{flex:1}.memory-line{display:grid;grid-template-columns:minmax(0,1fr) 90px auto;gap:8px;margin-bottom:10px}.memory-line input{min-width:0}.cap-state{white-space:nowrap}.runtime-only{display:none}:root[data-view="runtime"] .overview-only{display:none}:root[data-view="runtime"] .runtime-only{display:block}:root[data-view="runtime"] section.runtime-only{display:block}:root[data-view="overview"] .overview-only{display:block}:root[data-view="overview"] section.runtime-only{display:none}details{border:1px solid var(--line);border-radius:8px;padding:8px 10px;margin-top:10px}summary{cursor:pointer;color:var(--muted)}
@media(max-width:1000px){.card,.card.third{grid-column:span 12}.metrics{grid-template-columns:1fr}header{align-items:flex-start;flex-direction:column}.statebar{top:126px}.memory-line{grid-template-columns:1fr}}
</style>
</head>
<body>
<header>
<div><h1>RP86 Development Console</h1><div class="sub">Intel 8086 / NEC V30 × RP2350 · machine operation + live state</div></div>
<div class="toolbar"><div class="tabs"><button id="overviewTab">Overview</button><button id="runtimeTab">Runtime</button></div><button id="refreshAll">Refresh</button><button id="autoBtn">Auto refresh: ON</button><button id="themeBtn" title="Switch color theme">☀ Light</button></div>
</header>
<div class="statebar">
<div class="stateitem"><span id="hostDot" class="dot"></span><span>HOST</span><strong id="hostState">UNKNOWN</strong></div>
<div class="stateitem"><span id="rpDot" class="dot"></span><span>RP2350</span><strong id="rpState">UNKNOWN</strong></div>
<div class="stateitem"><span id="barCpuDot" class="dot"></span><span id="barCpuName">PROCESSOR</span><strong id="barCpuState">UNKNOWN</strong></div>
<div class="stateitem"><span>Clock</span><strong id="barClock">--</strong></div>
<div class="stateitem"><span>HB</span><strong id="barHeartbeat">--</strong></div>
<div class="stateitem"><span>Updated</span><strong id="lastUpdate">--</strong></div>
</div>
<main>
<section class="card overview-only"><div class="card-head"><span>Physical processor</span><span id="cpuBadge" class="muted">UNKNOWN</span></div><div class="card-body"><div class="hero"><span id="cpuName">8086-class processor</span></div><div class="statusline"><span id="cpuDot" class="dot"></span><span id="cpuAlive">Not checked</span></div><div class="metrics"><div class="metric"><span class="k">Heartbeat</span><span class="v" id="heartbeat">--</span></div><div class="metric"><span class="k">Clock</span><span class="v" id="overviewClock">--</span></div><div class="metric"><span class="k">Runtime</span><span class="v" id="overviewRuntime">--</span></div><div class="metric"><span class="k">Last update</span><span class="v" id="overviewUpdated">--</span></div></div></div></section>

<section class="card overview-only"><div class="card-head"><span>Workload / execution</span><span id="workloadBadge" class="muted">LIVE STATE</span></div><div class="card-body"><div class="metrics"><div class="metric"><span class="k">State</span><span class="v" id="workloadState">--</span></div><div class="metric"><span class="k">Clock</span><span class="v" id="clockMode">--</span></div><div class="metric" id="cpuCyclesMetric" style="display:none"><span class="k">CPU cycles</span><span class="v" id="cpuCycles">--</span></div></div></div></section>

<section class="card wide overview-only"><div class="card-head"><span>Physical processor console</span><span class="good">NATIVE TYPE_COMMAND</span></div><div class="card-body"><pre id="consoleOut">Direct TYPE_COMMAND payload to the physical processor through the Host Broker. This is not the RP86 Host shell. Payload limit: 14 UTF-8 bytes.</pre><div class="console-line"><input id="consoleInput" maxlength="14" placeholder="TYPE_COMMAND payload (max 14 UTF-8 bytes)"><button id="consoleSend">Send</button><button id="consoleClear">Clear</button></div><div class="muted small" style="margin-top:8px">Use ↑ / ↓ for local command history.</div></div></section>

<section class="card wide overview-only"><div class="card-head"><span>Memory viewer</span><span class="good">READ-ONLY</span></div><div class="card-body"><div class="memory-line"><input id="memoryAddress" value="0x00000" placeholder="Address, e.g. 0x7FF0"><input id="memoryLength" value="32" inputmode="numeric" placeholder="1-40"><button id="memoryRead">Read</button></div><div class="quick" style="margin-bottom:10px"><button data-mem-step="-1">Previous</button><button data-mem-len="16">16</button><button data-mem-len="32">32</button><button data-mem-len="40">40</button><button data-mem-step="1">Next</button></div><pre id="memoryOut">Internal SRAM 00000-3FFFF. Read 1-40 bytes through the existing memory service.</pre></div></section>

<section class="card runtime-only"><div class="card-head"><span>RP2350 runtime</span><span id="runtimeBadge" class="muted">unknown</span></div><div class="card-body"><div class="statusline"><span id="runtimeDot" class="dot"></span><span id="runtimeText">Not checked</span></div><div class="metrics"><div class="metric"><span class="k">State</span><span class="v" id="runtimeState">--</span></div><div class="metric"><span class="k">Execution clock</span><span class="v" id="runtimeClock">--</span></div><div class="metric"><span class="k">Processor memory</span><span class="v">Internal SRAM · 256 KiB · 00000-3FFFF</span></div><div class="metric"><span class="k">PSRAM</span><span class="v">Not configured</span></div><div class="metric"><span class="k">NOR Flash</span><span class="v">W25Q128JV · 16 MiB</span></div><div class="metric"><span class="k">Filesystem</span><span class="v">FAT16 · RP-FLASH</span></div><div class="metric"><span class="k">Workload</span><span class="v" id="runtimeWorkload">--</span></div></div><details><summary>Full RP2350 runtime status</summary><pre id="statusOut">Press Refresh to query the canonical RP86 runtime.</pre></details></div></section>

<section class="card runtime-only"><div class="card-head"><span>Host session</span><span id="hostSessionBadge" class="muted">unknown</span></div><div class="card-body"><div class="metrics"><div class="metric"><span class="k">Session</span><span class="v" id="runtimeSession">--</span></div><div class="metric"><span class="k">Device</span><span class="v" id="runtimeDevice">--</span></div><div class="metric"><span class="k">Connection</span><span class="v" id="runtimeConnection">--</span></div></div></div></section>

<section class="card runtime-only"><div class="card-head"><span>Capabilities</span><span class="muted">CURRENT PLATFORM</span></div><div class="card-body"><table><tbody><tr><td>Processor console</td><td class="good cap-state">AVAILABLE</td></tr><tr><td>Memory</td><td class="good cap-state">READ-ONLY</td></tr><tr><td>Bus trace</td><td class="warn cap-state">FIRMWARE-ONLY</td></tr><tr><td>Flash filesystem</td><td class="good cap-state">AVAILABLE</td></tr><tr><td>PSRAM</td><td class="muted cap-state">NOT CONFIGURED</td></tr><tr><td>SD</td><td class="muted cap-state">HARDWARE ONLY</td></tr><tr><td>DVI</td><td class="muted cap-state">NOT IMPLEMENTED</td></tr><tr><td>PIO-USB</td><td class="muted cap-state">NOT IMPLEMENTED</td></tr></tbody></table><div class="muted small" style="margin-top:10px">Bus trace is retained by firmware; structured Host access is not exposed in the current ABI. No CDC scraping is used.</div></div></section>

<section class="card runtime-only"><div class="card-head"><span>RP86 USB device</span><button id="devicesBtn">Rescan</button></div><div class="card-body"><div id="deviceTable" class="muted">Not scanned</div></div></section>

<section class="card runtime-only"><div class="card-head"><span>Runtime control</span><span class="warn">PHYSICAL HARDWARE</span></div><div class="card-body"><button id="rebootBtn">Reboot RP2350</button><div class="danger-zone"><div class="bad small" style="margin-bottom:8px">Danger zone</div><button id="bootBtn" class="danger">Enter UF2 bootloader</button><div class="muted small" style="margin-top:8px">UF2 bootloader stops the current RP86 runtime.</div></div></div></section>

<section class="card wide runtime-only"><div class="card-head"><span>Host event log</span><button id="clearLog">Clear</button></div><div class="card-body"><pre id="logOut"></pre></div></section>
</main>
<script>
const $=id=>document.getElementById(id);let autoRefresh=true,busyStatus=false,busyProcessor=false,lastProcessorAt=0,consoleHistory=[],consoleHistoryIndex=0;
function now(){return new Date().toLocaleTimeString()}function log(msg){const el=$('logOut');el.textContent+=`[${now()}] ${msg}\n`;el.scrollTop=el.scrollHeight}function val(v,f='--'){return v===null||v===undefined||v===''?f:String(v)}
function theme(){return document.documentElement.dataset.theme==='light'?'light':'dark'}function syncThemeButton(){$('themeBtn').textContent=theme()==='dark'?'☀ Light':'◐ Dark';$('themeBtn').title=theme()==='dark'?'Switch to light theme':'Switch to dark theme'}function toggleTheme(){const next=theme()==='dark'?'light':'dark';document.documentElement.dataset.theme=next;try{localStorage.setItem('rp86-theme',next)}catch(_){}syncThemeButton()}
function view(){return document.documentElement.dataset.view==='runtime'?'runtime':'overview'}function setView(next){document.documentElement.dataset.view=next;try{localStorage.setItem('rp86-view',next)}catch(_){}syncViewButtons();if(next==='runtime'){refreshStatus()}}function syncViewButtons(){$('overviewTab').classList.toggle('active',view()==='overview');$('runtimeTab').classList.toggle('active',view()==='runtime')}
async function api(path,options={}){const r=await fetch(path,options);const d=await r.json().catch(()=>({ok:false,error:'invalid JSON response'}));if(!r.ok||!d.ok)throw new Error(d.error||`HTTP ${r.status}`);return d}
function ageText(ts){if(!ts)return'--';const s=(Date.now()-ts)/1000;return s<1?'just now':`${s.toFixed(s<10?1:0)} s ago`}function setBarDot(id,state){$(id).className='dot '+state}
function updateAges(){const age=lastProcessorAt?Date.now()-lastProcessorAt:Infinity;$('lastUpdate').textContent=ageText(lastProcessorAt);$('overviewUpdated').textContent=ageText(lastProcessorAt);if(age>6000&&lastProcessorAt){$('barCpuState').textContent='STALE';setBarDot('barCpuDot','warn');$('cpuBadge').textContent='STALE';$('cpuBadge').className='warn'}}
function setCpuOffline(reason){$('cpuDot').className='dot bad';$('cpuAlive').textContent=reason||'No live broker telemetry';$('cpuBadge').textContent='NO BROKER';$('cpuBadge').className='warn';$('hostState').textContent='DISCONNECTED';setBarDot('hostDot','bad');$('barCpuState').textContent='UNKNOWN';setBarDot('barCpuDot','');$('runtimeConnection').textContent='DISCONNECTED';$('hostSessionBadge').textContent='OFFLINE';$('hostSessionBadge').className='bad'}
function renderProcessor(data){lastProcessorAt=Date.now();const s=data.snapshot||{},native=s.native_processor||null,completed=Number(s.completed||0),liveness=!!native&&completed>0;const names={'nec-v30':'NEC V30','intel-8086':'Intel 8086'},cpuLabel=native?(names[String(native).toLowerCase()]||String(native).toUpperCase()):'8086-class processor';$('cpuName').textContent=cpuLabel;$('barCpuName').textContent=native?cpuLabel:'PROCESSOR';$('heartbeat').textContent=`${completed} completed / ${val(s.lost,0)} lost`;$('barHeartbeat').textContent=`${completed}/${val(s.lost,0)}`;$('workloadState').textContent=val(s.workload_state);$('clockMode').textContent=val(s.workload_clock_mode);$('cpuCycles').textContent=val(s.workload_cycles);const activeCycles=Number(s.workload_cycles||0)>0&&!['EMPTY','STOPPED'].includes(String(s.workload_state||'').toUpperCase());$('cpuCyclesMetric').style.display=activeCycles?'flex':'none';const clockText=String(s.workload_clock_mode||'--').replace('FREE-RUNNING','1.000 MHz');$('overviewClock').textContent=clockText;$('barClock').textContent=clockText;$('runtimeClock').textContent=val(s.workload_clock_mode);$('runtimeWorkload').textContent=val(s.workload_state);$('runtimeState').textContent=val(s.state);$('hostState').textContent='CONNECTED';setBarDot('hostDot','good');const mode=data.owner_mode==='web-owned'?'WEB OWNER':data.owner_mode==='existing'?'ATTACHED':'ACTIVE';$('runtimeSession').textContent=mode;$('runtimeDevice').textContent=val(data.device_id);$('runtimeConnection').textContent='ACTIVE';$('hostSessionBadge').textContent=mode;$('hostSessionBadge').className='good';if(liveness){$('cpuDot').className='dot good';$('cpuAlive').textContent='Physical processor liveness proven';$('cpuBadge').textContent='ALIVE';$('cpuBadge').className='good';$('barCpuState').textContent='ALIVE';setBarDot('barCpuDot','good')}else{$('cpuDot').className='dot warn';$('cpuAlive').textContent='Host connected; processor liveness not yet proven';$('cpuBadge').textContent='UNPROVEN';$('cpuBadge').className='warn';$('barCpuState').textContent='UNPROVEN';setBarDot('barCpuDot','warn')}updateAges()}
async function refreshProcessor(){if(busyProcessor)return;busyProcessor=true;try{renderProcessor(await api('/api/processor'))}catch(e){setCpuOffline(e.message)}finally{busyProcessor=false}}
async function refreshStatus(){if(busyStatus)return;busyStatus=true;try{const d=await api('/api/status');$('statusOut').textContent=d.stdout||'(no output)';$('runtimeDot').className='dot good';$('runtimeText').textContent='RP86 runtime reachable';$('runtimeBadge').textContent='ONLINE';$('runtimeBadge').className='good';$('rpState').textContent='ONLINE';setBarDot('rpDot','good');$('overviewRuntime').textContent='ONLINE'}catch(e){$('statusOut').textContent=String(e.message);$('runtimeDot').className='dot bad';$('runtimeText').textContent='Runtime not reachable';$('runtimeBadge').textContent='OFFLINE';$('runtimeBadge').className='bad';$('rpState').textContent='OFFLINE';setBarDot('rpDot','bad');$('overviewRuntime').textContent='OFFLINE'}finally{busyStatus=false}}
async function scanDevices(){$('devicesBtn').disabled=true;try{const d=await api('/api/devices'),rows=d.devices||[];if(!rows.length){$('deviceTable').innerHTML='<span class="muted">No RP86 HID device found.</span>';return}const x=rows[0];$('deviceTable').innerHTML=`<div class="statusline"><span class="dot good"></span><strong>CONNECTED</strong></div><div class="metrics"><div class="metric"><span class="k">Serial</span><span class="v">${x.serial??'--'}</span></div><div class="metric"><span class="k">Product</span><span class="v">${x.product??'RP86 HID'}</span></div></div>`;log(`Found ${rows.length} RP86 HID device(s)`)}catch(e){$('deviceTable').innerHTML=`<span class="bad">${e.message}</span>`;log(`Device scan failed: ${e.message}`)}finally{$('devicesBtn').disabled=false}}
async function sendConsole(){const input=$('consoleInput'),button=$('consoleSend'),text=input.value;if(!text)return;consoleHistory.push(text);consoleHistoryIndex=consoleHistory.length;button.disabled=true;input.disabled=true;const out=$('consoleOut');out.textContent+=`\nTYPE_COMMAND > ${text}\n`;try{const d=await api('/api/console',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({text})});out.textContent+=`${d.processor||'processor'} < ${d.reply}\n`;out.scrollTop=out.scrollHeight;input.value='';refreshProcessor()}catch(e){out.textContent+=`ERROR: ${e.message}\n`;out.scrollTop=out.scrollHeight;log(`Processor command failed: ${e.message}`)}finally{button.disabled=false;input.disabled=false;input.focus()}}
function memoryAddress(){try{return Number(BigInt($('memoryAddress').value))}catch(_){return 0}}function memoryLength(){const n=Number($('memoryLength').value);return Number.isFinite(n)&&n>0?n:32}function stepMemory(direction){let next=memoryAddress()+direction*memoryLength();next=Math.max(0,Math.min(0x3FFFF,next));$('memoryAddress').value='0x'+next.toString(16).toUpperCase().padStart(5,'0');readMemory()}
async function readMemory(){const button=$('memoryRead'),out=$('memoryOut');button.disabled=true;try{const d=await api('/api/memory',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({address:$('memoryAddress').value,length:$('memoryLength').value})});out.textContent=d.dump;log(`Memory read 0x${Number(d.address).toString(16).toUpperCase().padStart(5,'0')} + ${d.length}`)}catch(e){out.textContent=`ERROR: ${e.message}`;log(`Memory read failed: ${e.message}`)}finally{button.disabled=false}}
async function control(action,label){const detail=action==='bootloader'?`${label}?\n\nThe current RP86 runtime will stop.`:`${label}?`;if(!confirm(detail))return;const b=action==='reboot'?$('rebootBtn'):$('bootBtn');b.disabled=true;try{const d=await api('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action})});log(`${label}: ${d.stdout.trim()||'acknowledged'}`);if(action==='reboot'){$('rpState').textContent='RESTARTING';setBarDot('rpDot','warn');$('barCpuState').textContent='UNAVAILABLE';setBarDot('barCpuDot','warn')}setTimeout(()=>{refreshStatus();refreshProcessor()},1200)}catch(e){log(`${label} failed: ${e.message}`);alert(e.message)}finally{b.disabled=false}}
function refreshAll(){refreshProcessor();refreshStatus()}
$('overviewTab').onclick=()=>setView('overview');$('runtimeTab').onclick=()=>setView('runtime');$('refreshAll').onclick=refreshAll;$('devicesBtn').onclick=scanDevices;$('rebootBtn').onclick=()=>control('reboot','Reboot RP2350');$('bootBtn').onclick=()=>control('bootloader','Enter UF2 bootloader');$('consoleSend').onclick=sendConsole;$('consoleClear').onclick=()=>$('consoleOut').textContent='Physical processor console cleared.';$('consoleInput').onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();sendConsole()}else if(e.key==='ArrowUp'){e.preventDefault();if(consoleHistoryIndex>0)consoleHistoryIndex--;$('consoleInput').value=consoleHistory[consoleHistoryIndex]||''}else if(e.key==='ArrowDown'){e.preventDefault();if(consoleHistoryIndex<consoleHistory.length)consoleHistoryIndex++;$('consoleInput').value=consoleHistory[consoleHistoryIndex]||''}};$('memoryRead').onclick=readMemory;$('memoryAddress').onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();readMemory()}};$('memoryLength').onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();readMemory()}};document.querySelectorAll('[data-mem-len]').forEach(b=>b.onclick=()=>{$('memoryLength').value=b.dataset.memLen});document.querySelectorAll('[data-mem-step]').forEach(b=>b.onclick=()=>stepMemory(Number(b.dataset.memStep)));$('clearLog').onclick=()=>$('logOut').textContent='';$('autoBtn').onclick=()=>{autoRefresh=!autoRefresh;$('autoBtn').textContent=`Auto refresh: ${autoRefresh?'ON':'OFF'}`};$('themeBtn').onclick=toggleTheme;syncThemeButton();syncViewButtons();setInterval(()=>{if(autoRefresh)refreshProcessor();updateAges()},2000);setInterval(()=>{if(autoRefresh)refreshStatus()},10000);refreshAll();
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


def _broker_client(prefix: str) -> tuple[object, BrokerClient] | tuple[None, None]:
    record = _active_broker()
    if record is None:
        return None, None
    return record, BrokerClient(record, f"{prefix}-{os.getpid()}")


def _processor_command(text: str, timeout: float = 3.0) -> dict[str, object]:
    """Send one native TYPE_COMMAND through the active Host Broker."""
    payload = text.encode("utf-8")
    if not payload:
        return {"ok": False, "error": "command is empty"}
    if len(payload) > 14:
        return {"ok": False, "error": "command exceeds the 14-byte native mailbox limit"}

    try:
        record, client = _broker_client("web-console")
    except RuntimeError as exc:
        return {"ok": False, "error": str(exc)}
    if record is None or client is None:
        return {"ok": False, "error": "No active RP86 Host Broker."}

    last_error = "processor command failed"
    for attempt in range(2):
        try:
            hello = client.hello()
            if not hello.get("ok"):
                raise RuntimeError(str(hello.get("error") or "broker hello failed"))
            snapshot = dict(hello.get("snapshot") or {})
            sequence = int(snapshot.get("request_sequence") or 1) & 0xFFFFFFFF or 1
            request = Message(TYPE_COMMAND, sequence, payload)
            result = client.exchange(
                request.encode(),
                f"web-console-{os.getpid()}-{time.time_ns()}-{attempt}",
                timeout,
            )
            if not result.get("ok"):
                last_error = str(result.get("error") or "processor command failed")
                continue
            reply = Message.decode(bytes.fromhex(str(result["reply_hex"])))
            witness = NativeServiceWitness.decode(reply.payload)
            return {
                "ok": True,
                "processor": witness.processor,
                "reply": witness.text.decode("ascii", errors="replace"),
                "request_sequence": sequence,
                "boot_id": witness.boot_id,
                "cpu_sequence": witness.cpu_sequence,
                "command_sequence": witness.command_sequence,
                "latency_ms": float(result.get("latency_ms") or 0.0),
            }
        except (OSError, RuntimeError, ValueError, KeyError) as exc:
            last_error = str(exc)
            if attempt == 0:
                time.sleep(0.03)
    return {"ok": False, "error": last_error}


def _memory_read(address_value: object, length_value: object, timeout: float = 3.0) -> dict[str, object]:
    """Read one bounded Internal-SRAM window through the active Host Broker."""
    try:
        address = int(str(address_value), 0)
        length = int(str(length_value), 0)
    except (TypeError, ValueError):
        return {"ok": False, "error": "address and length must be integers"}
    if not 1 <= length <= 40:
        return {"ok": False, "error": "memory viewer length must be 1-40 bytes"}

    try:
        record, client = _broker_client("web-memory")
    except RuntimeError as exc:
        return {"ok": False, "error": str(exc)}
    if record is None or client is None:
        return {"ok": False, "error": "No active RP86 Host Broker."}

    last_error = "memory read failed"
    for attempt in range(2):
        try:
            hello = client.hello()
            if not hello.get("ok"):
                raise RuntimeError(str(hello.get("error") or "broker hello failed"))
            snapshot = dict(hello.get("snapshot") or {})
            sequence = int(snapshot.get("request_sequence") or 1) & 0xFFFFFFFF or 1
            request = memory_read_request(address, length, sequence)
            result = client.exchange(
                request.encode(),
                f"web-memory-{os.getpid()}-{time.time_ns()}-{attempt}",
                timeout,
            )
            if not result.get("ok"):
                last_error = str(result.get("error") or "memory read failed")
                continue
            reply = Message.decode(bytes.fromhex(str(result["reply_hex"])))
            data = parse_memory_read(reply, request)
            return {
                "ok": True,
                "address": address,
                "length": len(data),
                "hex": data.hex(),
                "dump": format_memory_dump(address, data),
                "request_sequence": sequence,
                "latency_ms": float(result.get("latency_ms") or 0.0),
            }
        except (OSError, RuntimeError, ValueError, KeyError) as exc:
            last_error = str(exc)
            if attempt == 0:
                time.sleep(0.03)
    return {"ok": False, "error": last_error}


class Handler(BaseHTTPRequestHandler):
    server_version = "RP86Web/3.6"

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
        if path not in {"/api/control", "/api/console", "/api/memory"}:
            self._send_json({"ok": False, "error": "not found"}, 404)
            return
        length = int(self.headers.get("Content-Length", "0") or "0")
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            self._send_json({"ok": False, "error": "invalid JSON body"}, 400)
            return

        if path == "/api/console":
            text = payload.get("text")
            if not isinstance(text, str):
                self._send_json({"ok": False, "error": "text must be a string"}, 400)
                return
            result = _processor_command(text)
            self._send_json(result, 200 if result["ok"] else 503)
            return

        if path == "/api/memory":
            result = _memory_read(payload.get("address"), payload.get("length"))
            self._send_json(result, 200 if result["ok"] else 400 if "integer" in str(result.get("error")) or "1-40" in str(result.get("error")) or "Internal SRAM" in str(result.get("error")) else 503)
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

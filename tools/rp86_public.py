#!/usr/bin/env python3
"""Minimal network-capable RP86 Web gateway.

Cloudflare Tunnel or any reverse proxy may publish this local HTTP service.
The gateway never touches RP2350 hardware directly; it delegates to the
existing WebApi/RPBridge path and adds only browser-session ownership.
"""

from __future__ import annotations

import argparse
import json
import secrets
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent

from rp86_web_api import WebApi

API = WebApi(ROOT)
MAX_REQUEST_BYTES = 2 * 1024 * 1024


class PublicSession:
    """In-memory exclusive ownership for one physical RP86 device."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._token: str | None = None
        self._owner: str | None = None
        self._acquired_at: float | None = None
        self._last_seen: float | None = None

    def snapshot(self, token: str | None = None) -> dict[str, object]:
        with self._lock:
            value = self._snapshot_unlocked()
            value["mine"] = bool(
                self._token is not None
                and token
                and secrets.compare_digest(token, self._token)
            )
            return value

    def acquire(
        self, token: str | None = None, owner: str | None = None
    ) -> tuple[dict[str, object], int]:
        now = time.time()
        with self._lock:
            if self._token is not None:
                if token and secrets.compare_digest(token, self._token):
                    self._last_seen = now
                    return {
                        "ok": True,
                        "resumed": True,
                        "token": self._token,
                        "session": self._snapshot_unlocked(),
                    }, HTTPStatus.OK
                return {
                    "ok": False,
                    "error": "physical processor is already owned",
                    "session": self._snapshot_unlocked(),
                }, HTTPStatus.CONFLICT
            self._token = secrets.token_urlsafe(32)
            self._owner = owner or "browser"
            self._acquired_at = now
            self._last_seen = now
            return {
                "ok": True,
                "resumed": False,
                "token": self._token,
                "session": self._snapshot_unlocked(),
            }, HTTPStatus.OK

    def release(self, token: str | None) -> tuple[dict[str, object], int]:
        with self._lock:
            if self._token is None:
                return {
                    "ok": True,
                    "released": False,
                    "session": self._snapshot_unlocked(),
                }, HTTPStatus.OK
            if not token or not secrets.compare_digest(token, self._token):
                return {
                    "ok": False,
                    "error": "session ownership required",
                }, HTTPStatus.FORBIDDEN
            self._token = None
            self._owner = None
            self._acquired_at = None
            self._last_seen = None
            return {
                "ok": True,
                "released": True,
                "session": self._snapshot_unlocked(),
            }, HTTPStatus.OK

    def authorize(self, token: str | None) -> bool:
        now = time.time()
        with self._lock:
            if self._token is None or not token:
                return False
            if not secrets.compare_digest(token, self._token):
                return False
            self._last_seen = now
            return True

    def _snapshot_unlocked(self) -> dict[str, object]:
        return {
            "owned": self._token is not None,
            "owner": self._owner,
            "acquired_at": self._acquired_at,
            "last_seen": self._last_seen,
        }


SESSIONS = PublicSession()


INDEX_HTML = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RP86 Physical Processor</title>
<style>
:root{color-scheme:dark;font-family:ui-monospace,SFMono-Regular,Consolas,monospace}
body{margin:0;background:#0b0f14;color:#e7edf3}
main{max-width:920px;margin:0 auto;padding:28px 18px}
h1{font-size:20px;margin:0 0 6px}.sub{color:#91a0ad;margin-bottom:24px}
.card{border:1px solid #26323e;background:#111820;border-radius:10px;padding:16px;margin:12px 0}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
button,input,select{border:1px solid #3a4c60;background:#182330;color:#e7edf3;border-radius:7px;padding:8px 11px;font:inherit}
button{cursor:pointer}button:hover{border-color:#7aa2f7}
pre{white-space:pre-wrap;word-break:break-word;background:#080c10;border:1px solid #1d2730;border-radius:7px;padding:12px;min-height:80px}
.k{color:#91a0ad}.good{color:#6fdc8c}.bad{color:#ff7b72}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px 20px}
@media(max-width:700px){.grid{grid-template-columns:1fr}}
</style>
</head>
<body>
<main>
<h1>RP86 Physical Processor</h1>
<div class="sub">Intel 8086 / NEC V30 × RP2350 · remote browser host</div>

<section class="card">
  <div class="row">
    <button id="acquire">Acquire</button>
    <button id="release">Release</button>
    <button id="refresh">Refresh</button>
  </div>
  <div class="grid" style="margin-top:14px">
    <div><span class="k">Session</span> <strong id="session">--</strong></div>
    <div><span class="k">Processor</span> <strong id="processor">--</strong></div>
    <div><span class="k">RP2350</span> <strong id="rp2350">--</strong></div>
    <div><span class="k">Runtime</span> <strong id="runtime">--</strong></div>
  </div>
</section>

<section class="card">
  <strong>Native workload</strong>
  <div class="row" style="margin-top:10px">
    <input id="workloadFile" type="file" accept=".bin,.p86w">
    <input id="loadAddress" value="0x10000" title="Load address for raw .bin">
    <input id="entry" placeholder="entry CS:IP (auto)">
    <input id="stack" placeholder="stack SS:SP (default 0000:0000)">
    <select id="clock">
      <option value="auto">AUTO</option>
      <option value="free-running">FREE-RUNNING</option>
      <option value="clock-stepped">CLOCK-STEPPED</option>
    </select>
    <button id="load">Load</button>
  </div>
  <div class="row" style="margin-top:10px">
    <button id="run">Run</button>
    <button id="stop">Stop</button>
    <button id="restart">Restart</button>
    <button id="workloadStatus">Status</button>
  </div>
  <div class="grid" style="margin-top:14px">
    <div><span class="k">Workload</span> <strong id="workloadId">--</strong></div>
    <div><span class="k">State</span> <strong id="workloadState">--</strong></div>
    <div><span class="k">Clock</span> <strong id="workloadClock">--</strong></div>
    <div><span class="k">Cycles</span> <strong id="workloadCycles">--</strong></div>
    <div><span class="k">Result</span> <strong id="workloadResult">--</strong></div>
    <div><span class="k">Completion</span> <strong id="workloadCompletion">--</strong></div>
  </div>
  <pre id="workloadOut">No workload loaded.</pre>
</section>

<section class="card">
  <strong>Native command</strong>
  <div class="row" style="margin-top:10px">
    <input id="console" placeholder="TYPE_COMMAND payload" maxlength="14">
    <button id="send">Send</button>
  </div>
  <pre id="consoleOut">No command sent.</pre>
</section>

<section class="card">
  <strong>Memory</strong>
  <div class="row" style="margin-top:10px">
    <input id="address" value="0x00000">
    <input id="length" value="32" size="5">
    <button id="memory">Read</button>
  </div>
  <pre id="memoryOut">No memory read.</pre>
</section>

<section class="card">
  <strong>RP2350 control</strong>
  <div class="row" style="margin-top:10px">
    <button id="reboot">Reboot</button>
    <button id="bootloader">Bootloader</button>
  </div>
  <pre id="controlOut">No control request.</pre>
</section>
</main>

<script>
const TOKEN_KEY='rp86-public-session';
const $=id=>document.getElementById(id);
function token(){return localStorage.getItem(TOKEN_KEY)||''}
function cpuName(value){return {'intel-8086':'Intel 8086','nec-v30':'NEC V30'}[value]||value||'UNKNOWN'}
async function api(path, options={}){
  const headers={'Content-Type':'application/json',...(options.headers||{})};
  if(token()) headers['X-RP86-Session']=token();
  const response=await fetch(path,{...options,headers});
  let body={}; try{body=await response.json()}catch(_){}
  if(!response.ok) throw new Error(body.error||`${response.status} ${response.statusText}`);
  return body;
}
function show(id, value){$(id).textContent=typeof value==='string'?value:JSON.stringify(value,null,2)}
function showWorkload(snapshot={}){
  $('workloadId').textContent=snapshot.workload_id||'--';
  $('workloadState').textContent=snapshot.workload_state||'--';
  $('workloadClock').textContent=snapshot.workload_clock_mode||'--';
  $('workloadCycles').textContent=snapshot.workload_cycles??'--';
  $('workloadResult').textContent=snapshot.workload_result_structured
    ?(snapshot.workload_result_pass?'PASS':'FAIL'):'--';
  $('workloadCompletion').textContent=snapshot.workload_completion_reason||'--';
  if(snapshot.workload_native_output) $('workloadOut').textContent=snapshot.workload_native_output;
}
async function refresh(){
  try{
    const [session, processor]=await Promise.all([api('/api/session'),api('/api/processor')]);
    $('session').textContent=session.mine?'OWNED BY THIS BROWSER':session.owned?'BUSY':'AVAILABLE';
    $('processor').textContent=cpuName(processor.processor);
    $('rp2350').textContent=processor.ok?'CONNECTED':'OFFLINE';
    $('runtime').textContent=processor.state||processor.broker_state||processor.owner_mode||'--';
    showWorkload(processor.snapshot||{});
  }catch(error){$('rp2350').textContent='OFFLINE';$('runtime').textContent=error.message}
}
function fileBase64(file){
  return new Promise((resolve,reject)=>{
    const reader=new FileReader();
    reader.onload=()=>resolve(String(reader.result).split(',',2)[1]||'');
    reader.onerror=()=>reject(reader.error||new Error('file read failed'));
    reader.readAsDataURL(file);
  });
}
$('acquire').onclick=async()=>{
  try{
    const body=await api('/api/acquire',{method:'POST',body:JSON.stringify({token:token(),owner:'browser'})});
    if(body.token) localStorage.setItem(TOKEN_KEY,body.token);
    await refresh();
  }catch(error){alert(error.message)}
};
$('release').onclick=async()=>{
  try{
    await api('/api/release',{method:'POST',body:'{}'});
    localStorage.removeItem(TOKEN_KEY);
    await refresh();
  }catch(error){alert(error.message)}
};
$('refresh').onclick=refresh;
$('load').onclick=async()=>{
  const file=$('workloadFile').files[0];
  if(!file){show('workloadOut','Select a .bin or .p86w workload.');return}
  try{
    const body={
      name:file.name,
      data:await fileBase64(file),
      address:$('loadAddress').value,
      entry:$('entry').value,
      stack:$('stack').value,
      clock:$('clock').value
    };
    show('workloadOut',await api('/api/workload',{method:'POST',body:JSON.stringify(body)}));
    await refresh();
  }catch(error){show('workloadOut',error.message)}
};
async function workloadControl(action){
  try{
    const body=await api('/api/workload/control',{method:'POST',body:JSON.stringify({action})});
    show('workloadOut',body);
    await refresh();
  }catch(error){show('workloadOut',error.message)}
}
$('run').onclick=()=>workloadControl('run');
$('stop').onclick=()=>workloadControl('stop');
$('restart').onclick=()=>workloadControl('restart');
$('workloadStatus').onclick=()=>workloadControl('status');
$('send').onclick=async()=>{
  try{show('consoleOut',await api('/api/console',{method:'POST',body:JSON.stringify({text:$('console').value})}))}
  catch(error){show('consoleOut',error.message)}
};
$('memory').onclick=async()=>{
  try{show('memoryOut',await api('/api/memory',{method:'POST',body:JSON.stringify({address:$('address').value,length:$('length').value})}))}
  catch(error){show('memoryOut',error.message)}
};
async function control(action){
  try{show('controlOut',await api('/api/control',{method:'POST',body:JSON.stringify({action})}))}
  catch(error){show('controlOut',error.message)}
}
$('reboot').onclick=()=>control('reboot');
$('bootloader').onclick=()=>control('bootloader');
refresh(); setInterval(refresh,1000);
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    server_version = "RP86Public/0.2"

    def _security_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'self'; style-src 'self' 'unsafe-inline'; "
            "script-src 'self' 'unsafe-inline'; connect-src 'self'; "
            "img-src 'self' data:; frame-ancestors 'none'",
        )

    def _send_json(self, payload: dict[str, object], status: int = 200) -> None:
        raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self._security_headers()
        self.end_headers()
        self.wfile.write(raw)

    def _send_html(self, text: str, status: int = 200) -> None:
        raw = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self._security_headers()
        self.end_headers()
        self.wfile.write(raw)

    def _read_json(self) -> dict[str, object]:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as exc:
            raise ValueError("invalid Content-Length") from exc
        if length < 0 or length > MAX_REQUEST_BYTES:
            raise ValueError("request body is too large")
        raw = self.rfile.read(length) if length else b"{}"
        try:
            value = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError("request body must be a JSON object") from exc
        if not isinstance(value, dict):
            raise ValueError("request body must be a JSON object")
        return value

    def _session_token(self, payload: dict[str, object] | None = None) -> str | None:
        header = self.headers.get("X-RP86-Session")
        if header:
            return header
        if payload is not None:
            value = payload.get("token")
            if isinstance(value, str) and value:
                return value
        return None

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/":
            self._send_html(INDEX_HTML)
            return
        if path == "/api/session":
            self._send_json(SESSIONS.snapshot(self._session_token()))
            return
        if path in {"/api/processor", "/api/status", "/api/devices"}:
            result, status = API.get(path)
            self._send_json(result, status)
            return
        self._send_json({"ok": False, "error": "not found"}, HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        try:
            payload = self._read_json()
        except ValueError as exc:
            self._send_json({"ok": False, "error": str(exc)}, HTTPStatus.BAD_REQUEST)
            return

        token = self._session_token(payload)
        if path == "/api/acquire":
            owner = payload.get("owner")
            result, status = SESSIONS.acquire(
                token,
                owner if isinstance(owner, str) and owner else None,
            )
            self._send_json(result, status)
            return
        if path == "/api/release":
            result, status = SESSIONS.release(token)
            self._send_json(result, status)
            return

        if not SESSIONS.authorize(token):
            self._send_json(
                {"ok": False, "error": "session ownership required"},
                HTTPStatus.FORBIDDEN,
            )
            return

        result, status = API.post(path, payload)
        self._send_json(result, status)

    def log_message(self, format: str, *args: object) -> None:
        print(f"[RP86 PUBLIC] {self.address_string()} - {format % args}")


def _start_runtime_owner() -> None:
    result = API.ensure_runtime_owner()
    if not result.get("ok"):
        print(f"RP86 runtime owner unavailable: {result.get('error')}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="minimal network-capable RP86 Web gateway"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument(
        "--no-runtime-owner",
        action="store_true",
        help="do not start/attach the canonical RP86 background runtime",
    )
    args = parser.parse_args()

    if not 0 <= args.port <= 65535:
        parser.error("--port must be between 0 and 65535")

    if not args.no_runtime_owner:
        threading.Thread(
            target=_start_runtime_owner,
            name="rp86-public-runtime-owner",
            daemon=True,
        ).start()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    host, port = server.server_address[:2]
    print(f"RP86 Public Web = http://{host}:{port}")
    print("Cloudflare Tunnel should target this local HTTP endpoint.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        API.stop_owned_runtime()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

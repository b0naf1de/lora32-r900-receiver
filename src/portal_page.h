// AUTO-GENERATED from experiments/ui/redesign-device.html — do not edit by hand.
static const char PORTAL_PAGE[] PROGMEM = R"R900PAGE(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><meta name="format-detection" content="telephone=no"><link rel="icon" href="data:,"><title>R900 Reader · Setup</title> <style>:root{--bg:#0b0f14;--bg2:#0e131b;--panel:#121925;--panel-2:#161f2e;--line:#222d3d;--line-2:#2c3a4f;--ink:#e8edf4;--ink-dim:#9aa7ba;--ink-faint:#697689;--accent:#2ee6c4;--accent-deep:#11b89b;--accent-ink:#04201b;--amber:#f5b94a;--danger:#ff5d5d;--good:#43d17f;--radius:14px;--mono:ui-monospace,"SF Mono",SFMono-Regular,Menlo,Consolas,monospace;--sans:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}*{box-sizing:border-box}html,body{margin:0}body{background:radial-gradient(120% 60% at 50% -10%,#14202e 0,rgba(20,32,46,0) 60%),linear-gradient(180deg,var(--bg) 0,var(--bg2) 100%);background-attachment:fixed;color:var(--ink);font-family:var(--sans);font-size:15px;line-height:1.45;-webkit-font-smoothing:antialiased;padding-bottom:108px}.wrap{max-width:480px;margin:0 auto;padding:0 16px}.topbar{position:sticky;top:0;z-index:30;background:linear-gradient(180deg,rgba(11,15,20,.96),rgba(11,15,20,.82));backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}.topbar .wrap{display:flex;align-items:center;gap:10px;height:54px}.logo{width:30px;height:30px;border-radius:8px;flex:0 0 auto;background:radial-gradient(120% 120% at 30% 20%,#1c8f7d,#0c5f53);display:grid;place-items:center;box-shadow:inset 0 0 0 1px rgba(46,230,196,.35),0 2px 8px rgba(0,0,0,.4)}.logo svg{width:17px;height:17px;display:block}.title{font-weight:700;letter-spacing:.2px;font-size:15px}.subtitle{font-size:11px;color:var(--ink-faint);letter-spacing:.3px;margin-top:1px}.ap-pill{margin-left:auto;display:flex;align-items:center;gap:6px;font-size:11px;color:var(--ink-dim);background:var(--panel);border:1px solid var(--line);border-radius:999px;padding:5px 10px}.dot{width:7px;height:7px;border-radius:50%;background:var(--good);box-shadow:0 0 8px var(--good)}.conn{display:flex;align-items:center;gap:10px;margin:14px 0 4px;background:linear-gradient(90deg,rgba(67,209,127,.1),rgba(67,209,127,0));border:1px solid var(--line);border-left:3px solid var(--good);border-radius:var(--radius);padding:11px 13px;font-size:13px}.conn b{color:var(--ink)}.conn .ip{font-family:var(--mono);color:var(--ink-dim);font-size:12px}.conn.off{background:linear-gradient(90deg,rgba(245,185,74,.1),rgba(245,185,74,0));border-left-color:var(--amber)}.conn.off .dot{background:var(--amber);box-shadow:0 0 8px var(--amber)}.section{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);margin:14px 0;overflow:hidden}.sec-head{display:flex;align-items:center;gap:10px;padding:13px 15px;border-bottom:1px solid var(--line)}.sec-num{font-family:var(--mono);font-size:11px;color:var(--accent);border:1px solid var(--line-2);border-radius:6px;padding:2px 7px;background:rgba(46,230,196,.06);flex:0 0 auto}.sec-title{font-weight:650;font-size:14px;letter-spacing:.2px}.sec-hint{margin-left:auto;font-size:11px;color:var(--ink-faint)}.sec-body{padding:14px 15px}.list{border:1px solid var(--line);border-radius:10px;overflow:hidden;background:var(--bg2)}.ap{display:flex;align-items:center;gap:12px;padding:11px 13px;cursor:pointer;border-bottom:1px solid var(--line);transition:background .12s;-webkit-tap-highlight-color:transparent}.ap:last-child{border-bottom:0}.ap:hover{background:var(--panel-2)}.ap.sel{background:linear-gradient(90deg,rgba(46,230,196,.12),rgba(46,230,196,.02))}.ap.sel .ssid{color:#fff}.ap .check{width:18px;height:18px;border-radius:50%;flex:0 0 auto;border:2px solid var(--line-2);display:grid;place-items:center}.ap.sel .check{border-color:var(--accent);background:var(--accent)}.ap.sel .check::after{content:"";width:6px;height:6px;border-radius:50%;background:var(--accent-ink)}.ssid{font-size:14px;font-weight:550;flex:1 1 auto;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.ap .meta{display:flex;align-items:center;gap:9px;flex:0 0 auto;color:var(--ink-faint)}.lock{width:13px;height:13px;opacity:.75}.bars{display:flex;align-items:flex-end;gap:2px;height:14px}.bars i{width:3px;border-radius:1px;background:var(--line-2)}.bars i:nth-child(1){height:4px}.bars i:nth-child(2){height:7px}.bars i:nth-child(3){height:10px}.bars i:nth-child(4){height:14px}.bars.s1 i:nth-child(-n+1),.bars.s2 i:nth-child(-n+2),.bars.s3 i:nth-child(-n+3),.bars.s4 i:nth-child(-n+4){background:var(--accent)}.list-foot{display:flex;align-items:center;justify-content:space-between;margin-top:10px}.linkbtn{background:none;border:0;color:var(--accent);font:inherit;font-size:13px;font-weight:600;cursor:pointer;padding:6px 2px;display:inline-flex;align-items:center;gap:6px}.linkbtn svg{width:14px;height:14px}.scanning .linkbtn svg{animation:spin 1s linear infinite}@keyframes spin{to{transform:rotate(360deg)}}.muted{color:var(--ink-faint);font-size:12px}.empty{padding:14px;text-align:center;color:var(--ink-faint);font-size:12.5px}.field{margin-top:13px}.field:first-child{margin-top:0}label{display:block;font-size:12px;color:var(--ink-dim);margin:0 0 5px 2px;font-weight:600;letter-spacing:.2px}input[type=text],input[type=password],input[type=number]{width:100%;background:var(--bg2);color:var(--ink);border:1px solid var(--line-2);border-radius:9px;padding:11px 12px;font-size:15px;font-family:var(--sans);transition:border-color .12s,box-shadow .12s}input.mono{font-family:var(--mono);letter-spacing:.5px}input:focus{outline:none;border-color:var(--accent-deep);box-shadow:0 0 0 3px rgba(46,230,196,.14)}input::placeholder{color:var(--ink-faint)}input:disabled{opacity:.5}.hint{font-size:11.5px;color:var(--ink-faint);margin:5px 2px 0;line-height:1.4}.row{display:flex;gap:10px}.row .field{flex:1;margin-top:13px}.pwd-wrap{position:relative}.pwd-wrap input{padding-right:54px}.eye{position:absolute;right:6px;top:50%;transform:translateY(-50%);background:none;border:0;color:var(--ink-dim);font-size:11px;font-weight:600;cursor:pointer;padding:7px 8px;letter-spacing:.4px}.eye:hover{color:var(--ink)}.inline-note{display:flex;gap:9px;align-items:flex-start;margin-top:12px;padding:10px 12px;border-radius:9px;background:rgba(46,230,196,.05);border:1px solid rgba(46,230,196,.16);font-size:12px;color:var(--ink-dim)}.inline-note svg{width:15px;height:15px;flex:0 0 auto;margin-top:1px;color:var(--accent)}.inline-note.warn{background:rgba(245,185,74,.06);border-color:rgba(245,185,74,.22)}.inline-note.warn svg{color:var(--amber)}.meter{display:flex;align-items:center;gap:11px;padding:11px 12px;border-bottom:1px solid var(--line);-webkit-tap-highlight-color:transparent}.meter:last-child{border-bottom:0}.meter.watched{background:linear-gradient(90deg,rgba(46,230,196,.07),transparent)}.box{width:20px;height:20px;border-radius:6px;flex:0 0 auto;cursor:pointer;border:2px solid var(--line-2);display:grid;place-items:center;transition:.12s}.box.on{background:var(--accent);border-color:var(--accent)}.box svg{width:12px;height:12px;color:var(--accent-ink);opacity:0;transition:.12s}.box.on svg{opacity:1}.m-main{flex:1 1 auto;min-width:0}.m-id{font-family:var(--mono);font-size:14px;font-weight:600;letter-spacing:.5px}.m-sub{font-size:11px;color:var(--ink-faint);display:flex;align-items:center;gap:7px;margin-top:2px}.sig{font-family:var(--mono)}.nick{width:96px;flex:0 0 auto;background:var(--bg2);color:var(--ink);border:1px solid var(--line-2);border-radius:7px;padding:7px 8px;font-size:13px;transition:.12s}.nick:disabled{opacity:.35}.star{background:none;border:0;cursor:pointer;flex:0 0 auto;color:var(--line-2);padding:4px;display:grid;place-items:center;transition:.12s}.star svg{width:19px;height:19px}.star:hover{color:var(--ink-faint)}.star.on{color:var(--amber);filter:drop-shadow(0 0 5px rgba(245,185,74,.4))}.add-row{display:flex;gap:8px;margin-top:12px}.add-row input{flex:1}.btn-ghost{background:var(--panel-2);color:var(--ink);border:1px solid var(--line-2);border-radius:9px;padding:0 16px;font:inherit;font-weight:600;font-size:13px;cursor:pointer;white-space:nowrap;transition:.12s}.btn-ghost:hover{border-color:var(--accent-deep);color:#fff}.btn-ghost:disabled{opacity:.6;cursor:default}.test-row{display:flex;align-items:center;gap:11px;margin-top:12px;flex-wrap:wrap}.testline{font-size:12.5px;font-weight:600;color:var(--ink-faint)}.testline.ok{color:var(--good)}.testline.bad{color:var(--amber)}.summary{margin-top:13px}.summary .lbl{font-size:11px;color:var(--ink-faint);text-transform:uppercase;letter-spacing:.6px;margin-bottom:7px}.chips{display:flex;flex-wrap:wrap;gap:7px}.chip{display:inline-flex;align-items:center;gap:6px;background:var(--panel-2);border:1px solid var(--line-2);border-radius:999px;padding:5px 8px 5px 10px;font-size:12px;font-family:var(--mono)}.chip.pref{border-color:rgba(245,185,74,.5);background:rgba(245,185,74,.08)}.chip .nm{font-family:var(--sans);color:var(--ink-dim)}.chip .x{cursor:pointer;color:var(--ink-faint);width:16px;height:16px;border-radius:50%;display:grid;place-items:center;font-size:14px;line-height:1}.chip .x:hover{color:var(--danger);background:rgba(255,93,93,.12)}.chip .pin{color:var(--amber);width:12px;height:12px}.empty-chip{font-size:12px;color:var(--ink-faint);font-style:italic}.toggle-row{display:flex;align-items:center;gap:12px}.toggle-row .t-text{flex:1}.toggle-row .t-text .t-title{font-weight:600;font-size:14px}.toggle-row .t-text .t-desc{font-size:11.5px;color:var(--ink-faint);margin-top:2px}.switch{position:relative;width:46px;height:27px;flex:0 0 auto;cursor:pointer}.switch input{opacity:0;width:0;height:0;position:absolute}.track{position:absolute;inset:0;border-radius:999px;background:var(--line);border:1px solid var(--line-2);transition:.18s}.knob{position:absolute;top:3px;left:3px;width:21px;height:21px;border-radius:50%;background:#cdd6e2;transition:.18s;box-shadow:0 1px 3px rgba(0,0,0,.5)}.switch input:checked+.track{background:var(--accent-deep);border-color:var(--accent)}.switch input:checked+.track+.knob{transform:translateX(19px);background:#04221c}.danger-zone{border-color:rgba(255,93,93,.28)}.danger-zone .sec-num{color:var(--danger);border-color:rgba(255,93,93,.3);background:rgba(255,93,93,.06)}.btn-danger{width:100%;background:rgba(255,93,93,.08);color:var(--danger);border:1px solid rgba(255,93,93,.4);border-radius:9px;padding:11px;font:inherit;font-weight:600;cursor:pointer;transition:.12s}.btn-danger:hover{background:rgba(255,93,93,.16)}.actionbar{position:fixed;left:0;right:0;bottom:0;z-index:40;background:linear-gradient(180deg,rgba(11,15,20,.4),rgba(11,15,20,.97) 40%);backdrop-filter:blur(10px);border-top:1px solid var(--line);padding:12px 16px calc(12px + env(safe-area-inset-bottom))}.actionbar .wrap{padding:0;display:flex;flex-direction:column;gap:9px}.btn-row{display:flex;gap:10px}.btn{flex:1;border:0;border-radius:11px;padding:13px;font:inherit;font-weight:700;font-size:15px;cursor:pointer;transition:.12s;display:inline-flex;align-items:center;justify-content:center;gap:8px}.btn svg{width:16px;height:16px}.btn:disabled{opacity:.55;cursor:default}.btn-primary{background:linear-gradient(180deg,var(--accent),var(--accent-deep));color:var(--accent-ink);box-shadow:0 4px 16px rgba(46,230,196,.22)}.btn-primary:hover{filter:brightness(1.06)}.btn-primary:active{transform:translateY(1px)}.btn-primary.dirty{animation:pulse 2s ease-in-out infinite}@keyframes pulse{0%,100%{box-shadow:0 4px 16px rgba(46,230,196,.22)}50%{box-shadow:0 4px 22px rgba(46,230,196,.5)}}.btn-secondary{flex:0 0 auto;background:var(--panel-2);color:var(--ink);border:1px solid var(--line-2);padding:13px 18px}.btn-secondary:hover{border-color:var(--accent-deep);color:#fff}.toast{position:fixed;left:50%;bottom:130px;transform:translateX(-50%) translateY(20px);background:#0f2a25;color:#bff7ec;border:1px solid var(--accent-deep);padding:11px 16px;border-radius:10px;font-size:13px;font-weight:600;display:flex;align-items:center;gap:9px;z-index:60;opacity:0;pointer-events:none;transition:.25s;box-shadow:0 8px 24px rgba(0,0,0,.5);max-width:88vw}.toast svg{width:16px;height:16px;color:var(--accent);flex:0 0 auto}.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}.toast.err{background:#2a1212;color:#ffd0d0;border-color:var(--danger)}.toast.err svg{color:var(--danger)}.modal-bg{position:fixed;inset:0;background:rgba(5,8,12,.72);backdrop-filter:blur(3px);z-index:70;display:none;align-items:center;justify-content:center;padding:24px}.modal-bg.show{display:flex}.modal{background:var(--panel);border:1px solid var(--line-2);border-radius:16px;padding:22px;max-width:360px;width:100%;box-shadow:0 24px 60px rgba(0,0,0,.6)}.modal h3{margin:0 0 8px;font-size:17px}.modal p{margin:0 0 16px;font-size:13.5px;color:var(--ink-dim);line-height:1.5}.modal .m-actions{display:flex;gap:10px}.modal .btn{flex:1;font-size:14px;padding:11px}.modal input{margin-bottom:16px}.foot-brand{text-align:center;color:var(--ink-faint);font-size:11px;margin:18px 0 6px;letter-spacing:.3px;line-height:1.7}.foot-brand a{color:var(--accent);text-decoration:none;font-weight:600}.foot-brand a:hover{text-decoration:underline}</style> </head><body><div class="topbar"><div class="wrap"><div class="logo"><svg viewBox="0 0 24 24" fill="none"><path d="M12 3s6 6.5 6 11a6 6 0 0 1-12 0c0-4.5 6-11 6-11z" fill="#bff7ec"/></svg></div><div><div class="title">R900&nbsp;Reader</div><div class="subtitle">Setup portal</div></div><div class="ap-pill"><span class="dot"></span> setup mode</div></div></div><div class="wrap"><div class="conn off" id="conn"><span class="dot"></span><div style="flex:1" id="connText">Loading…</div></div><div class="section"><div class="sec-head"><span class="sec-num">01</span><span class="sec-title">Wi-Fi network</span><span class="sec-hint" id="wifiHint">scanning…</span></div><div class="sec-body"><div class="list" id="apList"><div class="empty">Loading networks…</div></div><div class="list-foot"><span class="muted" id="apFootNote">Tap a network to select it</span><button class="linkbtn" id="rescan" type="button"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-2.6-6.4"/><path d="M21 3v5h-5"/></svg> Rescan </button></div><div class="field"><label for="ssid">Network name (SSID)</label><input type="text" id="ssid" autocomplete="off" autocapitalize="none" autocorrect="off" placeholder="Select above, or type a hidden SSID"></div><div class="field"><label for="wifiPass">Password</label><div class="pwd-wrap"><input type="password" id="wifiPass" autocomplete="new-password" placeholder="Network password"><button class="eye" type="button" data-eye="wifiPass" hidden>SHOW</button></div><div class="hint pw-keep" id="wifiPassKeep" hidden>Leave blank to keep the saved password.</div></div><div class="test-row"><button class="btn-ghost" id="testBtn" type="button">Test connection</button><span class="testline" id="testResult" hidden></span></div></div></div><div class="section"><div class="sec-head"><span class="sec-num">02</span><span class="sec-title">MQTT</span><span class="sec-hint">optional</span></div><div class="sec-body"><div class="field"><label for="mqttHost">Broker address</label><input type="text" id="mqttHost" class="mono" autocapitalize="none" autocorrect="off" placeholder="IP or hostname"><div class="hint">Leave blank to run <b>standalone</b> — decode on the display only, no reporting.</div></div><div class="row"><div class="field" style="flex:0 0 110px"><label for="mqttPort">Port</label><input type="number" id="mqttPort" class="mono" placeholder="1883"></div><div class="field"><label for="mqttUser">Username</label><input type="text" id="mqttUser" autocapitalize="none" autocorrect="off" placeholder="optional"></div></div><div class="field"><label for="mqttPass">Password</label><div class="pwd-wrap"><input type="password" id="mqttPass" autocomplete="new-password" placeholder="optional"><button class="eye" type="button" data-eye="mqttPass" hidden>SHOW</button></div><div class="hint pw-keep" id="mqttPassKeep" hidden>Leave blank to keep the saved password.</div></div><div class="field"><label for="mqttTopic">Topic</label><input type="text" id="mqttTopic" class="mono" autocapitalize="none" autocorrect="off" placeholder="r900_meter"><div class="hint">Base topic the device publishes under (e.g. <span class="mono">r900_meter/&lt;id&gt;/state</span>).</div></div><div class="inline-note" id="mqttNote"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><path d="M12 8v5M12 16.5v.5" stroke-linecap="round"/></svg><span id="mqttNoteText">Publishing meter readings to your MQTT broker.</span></div></div></div><div class="section"><div class="sec-head"><span class="sec-num">03</span><span class="sec-title">Meters to watch</span><span class="sec-hint" id="watchCount">0 / 8</span></div><div class="sec-body"><div class="hint" style="margin:0 0 11px"> Check the meters you want to follow. Star one as <b>preferred</b> — the device locks its channel to keep that meter heard. Watch nothing to show every meter in range. </div><div class="list" id="meterList"><div class="empty">Loading meters…</div></div><div class="add-row"><input type="text" id="manualId" class="mono" inputmode="numeric" maxlength="10" placeholder="Add a meter ID by hand…"><button class="btn-ghost" id="addManual" type="button">Add</button></div><div class="summary"><div class="lbl">Watching</div><div class="chips" id="watchChips"></div></div></div></div><div class="section"><div class="sec-head"><span class="sec-num">04</span><span class="sec-title">Diagnostics</span></div><div class="sec-body"><div class="toggle-row"><div class="t-text"><div class="t-title">Extra diagnostic sensors</div><div class="t-desc">Publishes radio frequency &amp; decode-age sensors to MQTT.</div></div><label class="switch"><input type="checkbox" id="diag"><span class="track"></span><span class="knob"></span></label></div></div></div><div class="section danger-zone"><div class="sec-head"><span class="sec-num">05</span><span class="sec-title">Factory reset</span></div><div class="sec-body"><div class="hint" style="margin:0 0 12px">Erases all settings, saved networks and discovered meters, then reboots into setup.</div><button class="btn-danger" id="resetBtn" type="button">Erase all settings…</button></div></div><div class="foot-brand"> R900 Reader firmware · <a href="https://github.com/b0naf1de/lora32-r900-receiver" target="_blank" rel="noopener">About &amp; more info&nbsp;↗</a></div></div><div class="actionbar"><div class="wrap"><div class="btn-row"><button class="btn btn-primary" id="saveBtn"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12l5 5L20 7"/></svg> Save </button><button class="btn btn-secondary" id="applyBtn">Apply &amp; restart</button></div></div></div><div class="toast" id="toast"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12l5 5L20 7"/></svg><span id="toastText">Saved</span></div><div class="modal-bg" id="resetModal"><div class="modal"><h3>Erase everything?</h3><p>This wipes Wi-Fi, MQTT, watched meters and the discovered list, then reboots. Type <b style="color:var(--danger);font-family:var(--mono)">RESET</b> to confirm.</p><input type="text" id="resetConfirm" class="mono" autocapitalize="characters" autocorrect="off" placeholder="RESET"><div class="m-actions"><button class="btn btn-secondary" id="resetCancel" style="flex:1">Cancel</button><button class="btn btn-danger" id="resetGo" style="flex:1;opacity:.5" disabled>Erase</button></div></div></div><div class="modal-bg" id="applyModal"><div class="modal"><h3>Apply &amp; restart?</h3><p>Saves your changes and reboots the device so it can join <b id="applySsid">the network</b>. The setup page will disconnect. Re-open it anytime by double-tapping the RST button.</p><div class="m-actions"><button class="btn btn-secondary" id="applyCancel">Cancel</button><button class="btn btn-primary" id="applyGo">Restart now</button></div></div></div> <script>"use strict";
const $ = s => document.querySelector(s);
const esc = s => String(s==null?"":s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const bars = r => r>=-55?4 : r>=-67?3 : r>=-78?2 : 1;
const signalWord = d => d>=-70?"strong" : d>=-85?"fair" : "weak";
function subline(m){
if(m.manual) return '<span>added by hand</span>';
if(typeof m.dbm!=="number") return '<span class="sig">—</span><span>·</span><span>not heard yet</span>';
return '<span class="sig">'+m.dbm+' dBm</span><span>·</span><span>'+signalWord(m.dbm)+'</span>';
}
let APS=[], METERS=[], manualMeters=[], selSsid="", ORIG_SSID="";
const PW = {  // firmware sends only booleans; values are never shipped to the page
wifiPass:{ saved:false, base:"Network password", isSaved:()=>selSsid===ORIG_SSID && !!ORIG_SSID },
mqttPass:{ saved:false, base:"optional",         isSaved:()=>true },
};
const allMeters = () => METERS.concat(manualMeters);
function dirty(){ $("#saveBtn").classList.add("dirty"); }
function clearDirty(){ $("#saveBtn").classList.remove("dirty"); }
async function getJSON(path){ const r=await fetch(path,{cache:"no-store"}); if(!r.ok) throw new Error(r.status); return r.json(); }
async function postJSON(path,body){
const r=await fetch(path,{method:"POST",headers:{"Content-Type":"application/json"},body:body?JSON.stringify(body):undefined});
if(!r.ok) throw new Error(r.status); return r.json();
}
const LOCK='<svg class="lock" viewBox="0 0 24 24" fill="currentColor"><path d="M6 10V8a6 6 0 1 1 12 0v2h1a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1v-9a1 1 0 0 1 1-1h1zm2 0h8V8a4 4 0 1 0-8 0v2z"/></svg>';
function renderAPs(){
const el=$("#apList");
if(!APS.length){ el.innerHTML='<div class="empty">No networks found — tap Rescan.</div>'; return; }
el.innerHTML = APS.map((a,i)=>`
<div class="ap${a.ssid===selSsid?' sel':''}" data-i="${i}">
<div class="check"></div>
<div class="ssid">${esc(a.ssid)}</div>
<div class="meta">${a.lock?LOCK:''}<div class="bars s${bars(a.rssi)}"><i></i><i></i><i></i><i></i></div></div>
</div>`).join("");
el.querySelectorAll(".ap").forEach(d=>d.onclick=()=>selectAP(APS[+d.dataset.i]));
}
function selectAP(a){
selSsid=a.ssid; $("#ssid").value=a.ssid;
$("#apFootNote").textContent = a.lock ? "Enter the password below" : a.ssid+" is open — no password";
$("#wifiPass").disabled = !a.lock;
refreshPw("wifiPass");
if(a.lock) $("#wifiPass").focus();
renderAPs(); dirty();
}
$("#ssid").addEventListener("input",e=>{ selSsid=e.target.value; const a=APS.find(x=>x.ssid===selSsid); $("#wifiPass").disabled=a?!a.lock:false; refreshPw("wifiPass"); renderAPs(); dirty(); });
$("#rescan").onclick=async function(){
const body=this.closest(".sec-body"); body.classList.add("scanning"); $("#apFootNote").textContent="Scanning…";
try{ const r=await getJSON("/api/scan"); APS=(r.aps||[]).map(a=>({ssid:a.ssid,rssi:a.rssi,lock:!!a.enc})); renderAPs(); $("#wifiHint").textContent=APS.length+" found"; $("#apFootNote").textContent=selSsid?("Selected: "+selSsid):"Tap a network to select it"; }
catch(e){ $("#apFootNote").textContent="Scan failed — try again"; }
finally{ body.classList.remove("scanning"); }
};
$("#testBtn").onclick=async function(){
const btn=this, res=$("#testResult"), ssid=$("#ssid").value.trim();
res.hidden=false;
if(!ssid){ res.className="testline bad"; res.textContent="Enter a network name first."; return; }
const label=btn.textContent; btn.disabled=true; btn.textContent="Testing…";
res.className="testline"; res.textContent="Trying to connect (up to 10s)…";
try{
const r=await postJSON("/api/testwifi",{ssid, wifiPw:$("#wifiPass").value});
if(r && r.connected){ res.className="testline ok"; res.textContent="✓ Connected — "+(r.ip||""); }
else{ res.className="testline bad"; res.textContent="✗ Couldn't connect — check the network name and password."; }
}catch(e){ res.className="testline bad"; res.textContent="✗ Test failed — device unreachable."; }
finally{ btn.disabled=false; btn.textContent=label; }
};
function updateMqttNote(){
const host=$("#mqttHost").value.trim(), n=$("#mqttNote"), t=$("#mqttNoteText");
if(host){ n.classList.remove("warn"); t.innerHTML="Publishing meter readings to your MQTT broker."; }
else{ n.classList.add("warn"); t.innerHTML="<b style='color:var(--amber)'>Standalone mode</b> — no broker set. Meters decode on the display only."; }
}
$("#mqttHost").addEventListener("input",()=>{ updateMqttNote(); dirty(); });
["mqttPort","mqttUser","mqttTopic"].forEach(id=>$("#"+id).addEventListener("input",dirty));
const CHECK='<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3.2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12l5 5L20 7"/></svg>';
const STAR='<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 2l2.9 6.3 6.9.7-5.1 4.6 1.4 6.8L12 17.8 5.9 20.4l1.4-6.8L2.2 9l6.9-.7z"/></svg>';
const PIN='<svg class="pin" viewBox="0 0 24 24" fill="currentColor"><path d="M12 2l2.9 6.3 6.9.7-5.1 4.6 1.4 6.8L12 17.8 5.9 20.4l1.4-6.8L2.2 9l6.9-.7z"/></svg>';
function renderMeters(){
const el=$("#meterList"), ms=allMeters();
if(!ms.length){ el.innerHTML='<div class="empty">No meters heard yet — let the device run a while.</div>'; return; }
el.innerHTML = ms.map((m,i)=>`
<div class="meter${m.watched?' watched':''}" data-i="${i}">
<div class="box${m.watched?' on':''}" data-act="watch">${CHECK}</div>
<div class="m-main">
<div class="m-id">${m.id}</div>
<div class="m-sub">${subline(m)}</div>
</div>
<input class="nick" maxlength="10" placeholder="nickname" value="${esc(m.nick)}" ${m.watched?'':'disabled'} data-act="nick">
<button class="star${m.pref?' on':''}" data-act="star" title="Set preferred">${STAR}</button>
</div>`).join("");
el.querySelectorAll(".meter").forEach(d=>{
const m=allMeters()[+d.dataset.i];
d.querySelector("[data-act=watch]").onclick=()=>{ m.watched=!m.watched; renderAll(); dirty(); };
d.querySelector("[data-act=star]").onclick=()=>{ const was=m.pref; allMeters().forEach(x=>x.pref=false); if(!was){ m.pref=true; m.watched=true; } renderAll(); dirty(); };
d.querySelector("[data-act=nick]").oninput=e=>{ m.nick=e.target.value; updateChips(); dirty(); };
});
}
function updateChips(){
const w=allMeters().filter(m=>m.watched);
$("#watchCount").textContent=w.length+" / 8";
if(!w.length){ $("#watchChips").innerHTML='<span class="empty-chip">Nothing selected — showing all meters in range</span>'; return; }
$("#watchChips").innerHTML=w.map(m=>`<span class="chip${m.pref?' pref':''}">${m.pref?PIN:''}${m.id}${m.nick?`<span class="nm">${esc(m.nick)}</span>`:''}<span class="x" data-id="${m.id}">×</span></span>`).join("");
$("#watchChips").querySelectorAll(".x").forEach(x=>x.onclick=()=>{ const m=allMeters().find(a=>a.id===x.dataset.id); m.watched=false; renderAll(); dirty(); });
}
function autoPref(){
allMeters().forEach(m=>{ if(m.pref && !m.watched) m.pref=false; });
const w=allMeters().filter(m=>m.watched);
if(w.length===1){ allMeters().forEach(m=>m.pref=false); w[0].pref=true; }
}
function renderAll(){ autoPref(); renderMeters(); updateChips(); }
$("#addManual").onclick=()=>{
const v=$("#manualId").value.trim().replace(/\D/g,"");
if(!v) return;
if(allMeters().some(m=>m.id===v)){ $("#manualId").value=""; return; }
manualMeters.push({id:v,manual:true,watched:true,nick:"",pref:false});
$("#manualId").value=""; renderAll(); dirty();
};
$("#manualId").addEventListener("keydown",e=>{ if(e.key==="Enter"){ e.preventDefault(); $("#addManual").click(); } });
function refreshPw(id){
const inp=$("#"+id), cfg=PW[id], eye=document.querySelector('[data-eye="'+id+'"]'), keep=$("#"+id+"Keep");
const saved = cfg.saved && cfg.isSaved() && !inp.value;
inp.placeholder = saved ? "•••••••• saved — leave blank to keep" : cfg.base;
if(keep) keep.hidden = !saved;
if(inp.value && !inp.disabled){ eye.hidden=false; }
else{ eye.hidden=true; inp.type="password"; eye.textContent="SHOW"; }
}
Object.keys(PW).forEach(id=>{
$("#"+id).addEventListener("input",()=>{ refreshPw(id); dirty(); });
document.querySelector('[data-eye="'+id+'"]').addEventListener("click",function(){
const inp=$("#"+id), show=inp.type==="password"; inp.type=show?"text":"password"; this.textContent=show?"HIDE":"SHOW";
});
});
$("#diag").addEventListener("change",dirty);
let toastT;
function toast(msg,isErr){
$("#toastText").textContent=msg;
const t=$("#toast"); t.classList.toggle("err",!!isErr); t.classList.add("show");
clearTimeout(toastT); toastT=setTimeout(()=>t.classList.remove("show"),isErr?3200:2200);
}
function collect(){
const watch=allMeters().filter(m=>m.watched).map(m=>({id:Number(m.id),nick:m.nick.trim().replace(/[,:]/g,"").slice(0,10)}));
const pref=allMeters().find(m=>m.pref);
return {
ssid:$("#ssid").value.trim(),
wifiPw:$("#wifiPass").value,                       // "" = keep existing
server:$("#mqttHost").value.trim(),
port:Number($("#mqttPort").value)||1883,
user:$("#mqttUser").value,
mqttPw:$("#mqttPass").value,                       // "" = keep existing
topic:$("#mqttTopic").value.trim(),
watch, preferred: pref?Number(pref.id):0,
diag:$("#diag").checked
};
}
function passwordsNowSaved(){
if($("#wifiPass").value){ PW.wifiPass.saved=true; ORIG_SSID=$("#ssid").value.trim(); $("#wifiPass").value=""; }
if($("#mqttPass").value){ PW.mqttPass.saved=true; $("#mqttPass").value=""; }
refreshPw("wifiPass"); refreshPw("mqttPass");
}
$("#saveBtn").onclick=async()=>{
const b=$("#saveBtn"); b.disabled=true;
try{ await postJSON("/api/save",collect()); passwordsNowSaved(); clearDirty(); toast("Saved"); }
catch(e){ toast("Save failed — check connection",true); }
finally{ b.disabled=false; }
};
$("#applyBtn").onclick=()=>{ $("#applySsid").textContent=selSsid||"the network"; $("#applyModal").classList.add("show"); };
$("#applyCancel").onclick=()=>$("#applyModal").classList.remove("show");
$("#applyGo").onclick=async function(){
this.disabled=true;
try{ await postJSON("/api/save",collect()); await postJSON("/api/apply"); $("#applyModal").classList.remove("show"); toast("Applying… device will restart and reconnect."); }
catch(e){ toast("Apply failed — check connection",true); }
finally{ this.disabled=false; }
};
$("#resetBtn").onclick=()=>$("#resetModal").classList.add("show");
$("#resetCancel").onclick=()=>$("#resetModal").classList.remove("show");
$("#resetConfirm").addEventListener("input",e=>{ const ok=e.target.value.trim()==="RESET", g=$("#resetGo"); g.disabled=!ok; g.style.opacity=ok?1:.5; });
$("#resetGo").onclick=async()=>{ $("#resetModal").classList.remove("show"); try{ await postJSON("/api/factory"); toast("Erasing… device will wipe and restart."); }catch(e){ toast("Reset failed — check connection",true); } };
function renderConn(ip,ap){
const c=$("#conn"), t=$("#connText");
if(ip){ c.classList.remove("off"); t.innerHTML=`Connected to <b>${esc(ap||"")}</b>`; c.insertAdjacentHTML("beforeend",`<span class="ip" id="connIp"></span>`); $("#connIp")&&($("#connIp").textContent=ip); }
else{ c.classList.add("off"); t.innerHTML="Not connected — choose a Wi-Fi network below."; }
}
function applyState(s){
ORIG_SSID = s.ssid||""; selSsid = s.ssid||"";
APS = (s.aps||[]).map(a=>({ssid:a.ssid,rssi:a.rssi,lock:!!a.enc}));
METERS = (s.meters||[]).map(m=>({id:String(m.id),dbm:m.dbm,nick:m.nick||"",watched:!!m.watched,pref:!!m.preferred}));
manualMeters=[];
$("#ssid").value=s.ssid||"";
$("#mqttHost").value=s.server||"";
$("#mqttPort").value=s.port||1883;
$("#mqttUser").value=s.user||"";
$("#mqttTopic").value=s.topic||"";
$("#wifiPass").value=""; $("#mqttPass").value="";
$("#diag").checked=!!s.diag;
PW.wifiPass.saved=!!s.wifiPwSet; PW.mqttPass.saved=!!s.mqttPwSet;
const sel=APS.find(a=>a.ssid===selSsid); $("#wifiPass").disabled = sel?!sel.lock:false;
$("#wifiHint").textContent = APS.length?(APS.length+" found"):"none found";
$("#apFootNote").textContent = selSsid?("Selected: "+selSsid):"Tap a network to select it";
renderConn(s.ip,s.ap); renderAPs(); updateMqttNote(); renderAll();
refreshPw("wifiPass"); refreshPw("mqttPass");
clearDirty();
}
async function init(){
try{ applyState(await getJSON("/api/state")); }
catch(e){
$("#connText").textContent="Couldn't reach the device. Pull to refresh / reload.";
$("#apList").innerHTML='<div class="empty">Couldn’t load — reload the page.</div>';
$("#meterList").innerHTML='<div class="empty">Couldn’t load — reload the page.</div>';
toast("Couldn't load settings",true);
}
}
init();</script> </body></html>)R900PAGE";

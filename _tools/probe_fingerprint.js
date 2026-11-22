const fs = require('fs');
const path = require('path');
const http = require('http');
const { spawn } = require('child_process');
const crypto = require('crypto');

const root = 'C:/Users/1106/Documents/deepseek-harness/default-workspace/OpenFingerprintBrowser';
const payloadPath = path.join(process.env.TEMP, 'payload.js');
const payload = fs.readFileSync(payloadPath, 'utf8');

const EDGE = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const port = 9500 + Math.floor(Math.random() * 400);
const userDataDir = path.join(process.env.TEMP, 'ofb-probe-' + Date.now());

function getJson(url) {
  return new Promise((resolve, reject) => {
    http.get(url, (res) => {
      let body = '';
      res.on('data', (c) => (body += c));
      res.on('end', () => {
        try { resolve(JSON.parse(body)); } catch (e) { reject(e); }
      });
    }).on('error', reject);
  });
}

function waitForPort(deadline) {
  return new Promise((resolve, reject) => {
    const tick = async () => {
      try {
        const v = await getJson('http://127.0.0.1:' + port + '/json/version');
        resolve(v);
      } catch (e) {
        if (Date.now() > deadline) return reject(new Error('devtools 未就绪'));
        setTimeout(tick, 120);
      }
    };
    tick();
  });
}

class Cdp {
  constructor(ws) {
    this.ws = ws;
    this.id = 0;
    this.pending = new Map();
    ws.addEventListener('message', (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && this.pending.has(msg.id)) {
        const { resolve, reject } = this.pending.get(msg.id);
        this.pending.delete(msg.id);
        msg.error ? reject(new Error(JSON.stringify(msg.error))) : resolve(msg.result);
      }
    });
  }

  send(method, params, sessionId) {
    const id = ++this.id;
    const body = { id, method, params: params || {} };
    if (sessionId) body.sessionId = sessionId;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.ws.send(JSON.stringify(body));
      setTimeout(() => {
        if (this.pending.has(id)) {
          this.pending.delete(id);
          reject(new Error('超时: ' + method));
        }
      }, 20000);
    });
  }
}

function connect(url) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    ws.addEventListener('open', () => resolve(ws));
    ws.addEventListener('error', reject);
  });
}

async function main() {
  const proc = spawn(EDGE, [
    '--remote-debugging-port=' + port,
    '--user-data-dir=' + userDataDir,
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-quic',
    '--headless=new',
    'about:blank',
  ], { stdio: 'ignore' });

  const results = {};

  try {
    const version = await waitForPort(Date.now() + 25000);
    const ws = await connect(version.webSocketDebuggerUrl);
    const cdp = new Cdp(ws);

    // 先开一个空白目标，注入挂上去之后再导航
    const { targetId } = await cdp.send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await cdp.send('Target.attachToTarget', { targetId, flatten: true });

    await cdp.send('Page.enable', {}, sessionId);
    await cdp.send('Runtime.enable', {}, sessionId);
    await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: payload }, sessionId);

    // 用 data URL 造一个独立文档，验证注入在新文档上生效
    await cdp.send('Page.navigate', { url: 'data:text/html,<html><body>probe</body></html>' }, sessionId);
    await new Promise((r) => setTimeout(r, 1200));

    const probe = `(function(){
      var out = {};
      try { out.timezone = Intl.DateTimeFormat().resolvedOptions().timeZone; } catch(e) { out.timezone = 'ERR:'+e.message; }
      try { out.locale = Intl.DateTimeFormat().resolvedOptions().locale; } catch(e) { out.locale = 'ERR'; }
      try { out.language = navigator.language; } catch(e) { out.language = 'ERR'; }
      try { out.languages = navigator.languages; } catch(e) { out.languages = 'ERR'; }
      try { out.offset = new Date(2026, 0, 1).getTimezoneOffset(); } catch(e) { out.offset = 'ERR'; }
      try { out.cores = navigator.hardwareConcurrency; } catch(e) { out.cores = 'ERR'; }
      try { out.mem = navigator.deviceMemory; } catch(e) { out.mem = 'ERR'; }
      try { out.screen = [screen.width, screen.height, screen.availHeight]; } catch(e) { out.screen = 'ERR'; }
      try { out.hasArial = document.fonts.check('12px Arial'); } catch(e) { out.hasArial = 'ERR'; }
      try { out.hasGeorgia = document.fonts.check('12px Georgia'); } catch(e) { out.hasGeorgia = 'ERR'; }
      try {
        var c = document.createElement('canvas'); c.width=200; c.height=40;
        var x = c.getContext('2d'); x.fillText('probe', 10, 20);
        out.canvasHash = x.getImageData(0,0,200,40).data.slice(0,400).join(',');
      } catch(e) { out.canvasHash = 'ERR'; }
      try {
        var g = document.createElement('canvas').getContext('webgl');
        var dbg = g && g.getExtension('WEBGL_debug_renderer_info');
        out.glVendor = g && dbg ? g.getParameter(dbg.UNMASKED_VENDOR_WEBGL) : 'none';
        out.glRenderer = g && dbg ? g.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : 'none';
      } catch(e) { out.gl = 'ERR'; }
      try { out.toStringLeak = Date.prototype.getTimezoneOffset.toString(); } catch(e) { out.toStringLeak='ERR'; }
      try { out.dtfLeak = Intl.DateTimeFormat.toString(); } catch(e) { out.dtfLeak='ERR'; }
      return JSON.stringify(out);
    })()`;

    const r = await cdp.send('Runtime.evaluate', { expression: probe, returnByValue: true }, sessionId);
    results.injected = JSON.parse(r.result.value);

    // 同一浏览器的另一个未注入目标作为对照
    const base = await cdp.send('Target.createTarget', { url: 'data:text/html,<html><body>base</body></html>' });
    const baseAttach = await cdp.send('Target.attachToTarget', { targetId: base.targetId, flatten: true });
    await cdp.send('Runtime.enable', {}, baseAttach.sessionId);
    await new Promise((r2) => setTimeout(r2, 600));
    const rb = await cdp.send('Runtime.evaluate', { expression: probe, returnByValue: true }, baseAttach.sessionId);
    results.baseline = JSON.parse(rb.result.value);

    ws.close();
  } finally {
    try { proc.kill(); } catch (e) {}
    try { fs.rmSync(userDataDir, { recursive: true, force: true }); } catch (e) {}
  }

  const inj = results.injected || {};
  const basev = results.baseline || {};
  console.log('=== 基线 ===');
  console.log(JSON.stringify({
    timezone: basev.timezone, locale: basev.locale, language: basev.language,
    offset: basev.offset, cores: basev.cores, screen: basev.screen,
  }, null, 2));
  console.log('=== 注入后 ===');
  console.log(JSON.stringify({
    timezone: inj.timezone, locale: inj.locale, language: inj.language,
    languages: inj.languages, offset: inj.offset, cores: inj.cores, mem: inj.mem,
    screen: inj.screen, hasArial: inj.hasArial, hasGeorgia: inj.hasGeorgia,
    glVendor: inj.glVendor, glRenderer: inj.glRenderer,
    toStringLeak: inj.toStringLeak, dtfLeak: inj.dtfLeak,
  }, null, 2));

  const checks = [
    ['时区', inj.timezone === 'Asia/Tokyo'],
    ['locale', inj.locale === 'ja-JP'],
    ['language', inj.language === 'ja-JP'],
    ['偏移', inj.offset === -540],
    ['cores', inj.cores === 4],
    ['字体白名单生效', inj.hasArial === true && inj.hasGeorgia === false],
    ['toString 未暴露', String(inj.toStringLeak || '').indexOf('native code') >= 0],
    ['DTF 未暴露', String(inj.dtfLeak || '').indexOf('native code') >= 0],
  ];
  console.log('=== 判定 ===');
  let fail = 0;
  for (const [name, ok] of checks) {
    console.log((ok ? 'PASS ' : 'FAIL ') + name);
    if (!ok) fail++;
  }
  process.exitCode = fail === 0 ? 0 : 2;
}

main().catch((e) => {
  console.error('探针失败: ' + e.message);
  process.exitCode = 1;
});

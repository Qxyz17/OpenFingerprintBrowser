const fs = require('fs');
const path = require('path');
const http = require('http');
const { spawn } = require('child_process');

const root = 'C:/Users/1106/Documents/deepseek-harness/default-workspace/OpenFingerprintBrowser';
const payload = fs.readFileSync(path.join(process.env.TEMP, 'payload.js'), 'utf8');

const EDGE = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const port = 9800 + Math.floor(Math.random() * 150);
const userDataDir = path.join(process.env.TEMP, 'ofb-reload-' + Date.now());

const getJson = (url) => new Promise((res, rej) => {
  http.get(url, (r) => { let b = ''; r.on('data', (c) => b += c); r.on('end', () => { try { res(JSON.parse(b)); } catch (e) { rej(e); } }); }).on('error', rej);
});

const waitPort = (deadline) => new Promise((res, rej) => {
  const tick = async () => {
    try { res(await getJson('http://127.0.0.1:' + port + '/json/version')); }
    catch (e) { if (Date.now() > deadline) return rej(new Error('no devtools')); setTimeout(tick, 120); }
  };
  tick();
});

const probe = `JSON.stringify({tz:Intl.DateTimeFormat().resolvedOptions().timeZone,
  lang:navigator.language, off:new Date().getTimezoneOffset(), sw:screen.width})`;

class Cdp {
  constructor(ws) {
    this.ws = ws; this.id = 0; this.pending = new Map(); this.sessions = new Map();
    ws.addEventListener('message', (ev) => {
      const m = JSON.parse(ev.data);
      if (m.id && this.pending.has(m.id)) {
        const p = this.pending.get(m.id); this.pending.delete(m.id);
        m.error ? p.reject(new Error(JSON.stringify(m.error))) : p.resolve(m.result);
      } else if (m.method === 'Target.attachedToTarget') {
        this.sessions.set(m.params.targetInfo.targetId, m.params.sessionId);
        if (this.onAttach) this.onAttach(m.params);
      } else if (m.method === 'Fetch.requestPaused') {
        this.send('Fetch.continueRequest', { requestId: m.params.requestId },
          m.params.sessionId || this.lastSid).catch(() => {});
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
      setTimeout(() => { if (this.pending.has(id)) { this.pending.delete(id); reject(new Error('timeout ' + method)); } }, 15000);
    });
  }
}

const connect = (url) => new Promise((res, rej) => {
  const ws = new WebSocket(url);
  ws.addEventListener('open', () => res(ws));
  ws.addEventListener('error', rej);
});

async function main() {
  const proc = spawn(EDGE, [
    '--remote-debugging-port=' + port,
    '--user-data-dir=' + userDataDir,
    '--no-first-run', '--no-default-browser-check',
    '--disable-quic', '--headless=new',
    'about:blank',
  ], { stdio: 'ignore' });

  try {
    const v = await waitPort(Date.now() + 25000);
    const ws = await connect(v.webSocketDebuggerUrl);
    const cdp = new Cdp(ws);

    cdp.onAttach = async (params) => {
      if (params.targetInfo.type !== 'page') {
        cdp.send('Runtime.runIfWaitingForDebugger', {}, params.sessionId).catch(() => {});
        return;
      }
      const sid = params.sessionId;
      cdp.lastSid = sid;
      try {
        await cdp.send('Page.enable', {}, sid);
        await cdp.send('Runtime.enable', {}, sid);
        await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: payload }, sid);
        await cdp.send('Fetch.enable', { patterns: [{ urlPattern: '*', requestStage: 'Request' }] }, sid);
        await cdp.send('Runtime.runIfWaitingForDebugger', {}, sid);
        // 文档已经提交，重载一次让新文档吃到注入
        cdp.send('Page.reload', { ignoreCache: false }, sid).catch(() => {});
      } catch (e) {
        console.log('注入失败 ' + e.message);
      }
    };

    await cdp.send('Target.setAutoAttach', {
      autoAttach: true, waitForDebuggerOnStart: true, flatten: true,
      filter: [{ type: 'page', exclude: false }],
    });

    await new Promise((r) => setTimeout(r, 1200));

    for (let i = 0; i < 2; i++) {
      await cdp.send('Target.createTarget', { url: 'data:text/html,<html><body>tab' + i + '</body></html>' });
    }
    await new Promise((r) => setTimeout(r, 3000));

    console.log('--- 结果 ---');
    const list = await getJson('http://127.0.0.1:' + port + '/json/list');
    let pass = 0, total = 0;
    for (const item of list.filter((x) => x.type === 'page')) {
      const sid = cdp.sessions.get(item.id);
      if (!sid) { console.log('无 session: ' + item.url); continue; }
      try {
        const r = await cdp.send('Runtime.evaluate', { expression: probe, returnByValue: true }, sid);
        const d = JSON.parse(r.result.value);
        const ok = d.tz === 'Asia/Tokyo' && d.lang === 'ja-JP' && d.off === -540;
        total++; if (ok) pass++;
        console.log((ok ? 'PASS ' : 'FAIL ') + (item.url || '').slice(0, 40) +
          ' tz=' + d.tz + ' lang=' + d.lang + ' off=' + d.off + ' sw=' + d.sw);
      } catch (e) {
        console.log('查询失败 ' + e.message);
      }
    }
    console.log('通过 ' + pass + '/' + total);
    ws.close();
  } finally {
    try { proc.kill(); } catch (e) {}
    try { fs.rmSync(userDataDir, { recursive: true, force: true }); } catch (e) {}
  }
}

main().catch((e) => { console.error('失败: ' + e.message); process.exitCode = 1; });

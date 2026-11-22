const fs = require('fs');
const path = require('path');
const http = require('http');
const { spawn } = require('child_process');

const root = 'C:/Users/1106/Documents/deepseek-harness/default-workspace/OpenFingerprintBrowser';
const payload = fs.readFileSync(path.join(process.env.TEMP, 'payload.js'), 'utf8');

const EDGE = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const port = 9600 + Math.floor(Math.random() * 300);
const userDataDir = path.join(process.env.TEMP, 'ofb-flow-' + Date.now());

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

class Cdp {
  constructor(ws) {
    this.ws = ws; this.id = 0; this.pending = new Map(); this.events = [];
    ws.addEventListener('message', (ev) => {
      const m = JSON.parse(ev.data);
      if (m.id && this.pending.has(m.id)) {
        const p = this.pending.get(m.id); this.pending.delete(m.id);
        m.error ? p.reject(new Error(JSON.stringify(m.error))) : p.resolve(m.result);
      } else if (m.method) this.events.push(m);
    });
  }
  send(method, params, sessionId) {
    const id = ++this.id;
    const body = { id, method, params: params || {} };
    if (sessionId) body.sessionId = sessionId;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.ws.send(JSON.stringify(body));
      setTimeout(() => { if (this.pending.has(id)) { this.pending.delete(id); reject(new Error('timeout ' + method)); } }, 20000);
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
    '--lang=ja-JP', '--accept-lang=ja-JP,ja,en',
    'about:blank',
  ], { stdio: 'ignore' });

  try {
    const v = await waitPort(Date.now() + 25000);
    const ws = await connect(v.webSocketDebuggerUrl);
    const cdp = new Cdp(ws);

    // 完全照抄 BrowserSession 的顺序
    const { targetId } = await cdp.send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await cdp.send('Target.attachToTarget', { targetId, flatten: true });
    console.log('session=' + sessionId);

    await cdp.send('Page.enable', {}, sessionId);
    await cdp.send('Runtime.enable', {}, sessionId);

    const added = await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: payload }, sessionId);
    console.log('addScript=' + JSON.stringify(added));

    await cdp.send('Fetch.enable', {
      patterns: [{ urlPattern: '*', requestStage: 'Request' }],
    }, sessionId);

    // 复刻拦截放行：所有 paused 请求立即 continue
    let paused = 0;
    ws.addEventListener('message', (ev) => {
      const m = JSON.parse(ev.data);
      if (m.method === 'Fetch.requestPaused') {
        paused++;
        cdp.send('Fetch.continueRequest', { requestId: m.params.requestId }, m.params.sessionId || sessionId)
          .catch((e) => console.log('continue err: ' + e.message));
      }
    });

    await cdp.send('Page.navigate', { url: 'data:text/html,<html><body>x</body></html>' }, sessionId);
    await new Promise((r) => setTimeout(r, 1500));

    const probe = `JSON.stringify({
      tz: Intl.DateTimeFormat().resolvedOptions().timeZone,
      locale: Intl.DateTimeFormat().resolvedOptions().locale,
      lang: navigator.language,
      langs: navigator.languages,
      off: new Date().getTimezoneOffset(),
      marker: document.documentElement.getAttribute('data-px'),
      ua: navigator.userAgent.slice(0,40)
    })`;
    const r = await cdp.send('Runtime.evaluate', { expression: probe, returnByValue: true }, sessionId);
    console.log('注入目标结果: ' + r.result.value);
    console.log('Fetch pausing 次数: ' + paused);

    // 对照：同一浏览器新开目标，不注入
    const b = await cdp.send('Target.createTarget', { url: 'about:blank' });
    const ba = await cdp.send('Target.attachToTarget', { targetId: b.targetId, flatten: true });
    await cdp.send('Runtime.enable', {}, ba.sessionId);
    await new Promise((r2) => setTimeout(r2, 500));
    const rb = await cdp.send('Runtime.evaluate', { expression: probe, returnByValue: true }, ba.sessionId);
    console.log('对照目标结果: ' + rb.result.value);

    ws.close();
  } finally {
    try { proc.kill(); } catch (e) {}
    try { fs.rmSync(userDataDir, { recursive: true, force: true }); } catch (e) {}
  }
}

main().catch((e) => { console.error('失败: ' + e.message); process.exitCode = 1; });

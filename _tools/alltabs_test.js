const fs = require('fs');
const path = require('path');
const http = require('http');
const { spawn } = require('child_process');

const root = 'C:/Users/1106/Documents/deepseek-harness/default-workspace/OpenFingerprintBrowser';
const payload = fs.readFileSync(path.join(process.env.TEMP, 'payload.js'), 'utf8');

const EDGE = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const port = 9700 + Math.floor(Math.random() * 200);
const userDataDir = path.join(process.env.TEMP, 'ofb-alltabs-' + Date.now());

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
    this.ws = ws; this.id = 0; this.pending = new Map();
    ws.addEventListener('message', (ev) => {
      const m = JSON.parse(ev.data);
      if (m.id && this.pending.has(m.id)) {
        const p = this.pending.get(m.id); this.pending.delete(m.id);
        m.error ? p.reject(new Error(JSON.stringify(m.error))) : p.resolve(m.result);
      } else if (m.method === 'Target.attachedToTarget') {
        if (this.onAttach) this.onAttach(m.params);
      } else if (m.method === 'Fetch.requestPaused') {
        const sid = m.params.sessionId || m.sessionId;
        this.send('Fetch.continueRequest', { requestId: m.params.requestId }, sid).catch(() => {});
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
      setTimeout(() => { if (this.pending.has(id)) { this.pending.delete(id); reject(new Error('timeout ' + method)); } }, 20000);
    });
  }
}

const connect = (url) => new Promise((res, rej) => {
  const ws = new WebSocket(url);
  ws.addEventListener('open', () => res(ws));
  ws.addEventListener('error', rej);
});

const probe = `JSON.stringify({tz:Intl.DateTimeFormat().resolvedOptions().timeZone,
  locale:Intl.DateTimeFormat().resolvedOptions().locale,
  lang:navigator.language, off:new Date().getTimezoneOffset(),
  sw:screen.width, sh:screen.height, cores:navigator.hardwareConcurrency})`;

async function main() {
  const proc = spawn(EDGE, [
    '--remote-debugging-port=' + port,
    '--user-data-dir=' + userDataDir,
    '--no-first-run', '--no-default-browser-check',
    '--disable-quic', '--headless=new',
    'about:blank',
  ], { stdio: 'ignore' });

  const injectedSessions = new Set();

  try {
    const v = await waitPort(Date.now() + 25000);
    const ws = await connect(v.webSocketDebuggerUrl);
    const cdp = new Cdp(ws);

    cdp.onAttach = async (params) => {
      const info = params.targetInfo;
      if (info.type !== 'page') {
        cdp.send('Runtime.runIfWaitingForDebugger', {}, params.sessionId).catch(() => {});
        return;
      }
      const sid = params.sessionId;
      try {
        await cdp.send('Page.enable', {}, sid);
        await cdp.send('Runtime.enable', {}, sid);
        await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: payload }, sid);
        await cdp.send('Fetch.enable', { patterns: [{ urlPattern: '*', requestStage: 'Request' }] }, sid);
        injectedSessions.add(sid);
        await cdp.send('Runtime.runIfWaitingForDebugger', {}, sid);
        console.log('注入标签页 ' + sid.slice(0, 8) + ' url=' + info.url);
      } catch (e) {
        console.log('注入失败 ' + e.message);
      }
    };

    await cdp.send('Target.setAutoAttach', {
      autoAttach: true, waitForDebuggerOnStart: true, flatten: true,
      filter: [{ type: 'page', exclude: false }],
    });

    await new Promise((r) => setTimeout(r, 1500));

    // 新开三个标签页，模拟用户手动点开
    const urls = [
      'data:text/html,<html><body>tab1</body></html>',
      'data:text/html,<html><body>tab2</body></html>',
      'data:text/html,<html><body>tab3</body></html>',
    ];
    for (const u of urls) {
      await cdp.send('Target.createTarget', { url: u });
    }
    await new Promise((r) => setTimeout(r, 2500));

    console.log('--- 全部 page 目标结果 ---');
    const list = await getJson('http://127.0.0.1:' + port + '/json/list');
    for (const item of list.filter((x) => x.type === 'page')) {
      const a = await cdp.send('Target.attachToTarget', { targetId: item.id, flatten: true });
      try {
        await cdp.send('Runtime.enable', {}, a.sessionId);
        const r = await cdp.send('Runtime.evaluate',
          { expression: probe, returnByValue: true }, a.sessionId);
        const v2 = JSON.parse(r.result.value);
        const ok = v2.tz === 'Asia/Tokyo' && v2.lang === 'ja-JP';
        console.log((ok ? 'PASS ' : 'FAIL ') + (item.url || '').slice(0, 40) +
          ' -> tz=' + v2.tz + ' lang=' + v2.lang + ' off=' + v2.off +
          ' screen=' + v2.sw + 'x' + v2.sh + ' cores=' + v2.cores);
      } catch (e) {
        console.log('查询失败 ' + e.message);
      }
    }

    ws.close();
  } finally {
    try { proc.kill(); } catch (e) {}
    try { fs.rmSync(userDataDir, { recursive: true, force: true }); } catch (e) {}
  }
}

main().catch((e) => { console.error('失败: ' + e.message); process.exitCode = 1; });

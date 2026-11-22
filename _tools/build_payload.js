const fs = require('fs');
const path = require('path');

const root = 'C:/Users/1106/Documents/deepseek-harness/default-workspace/OpenFingerprintBrowser';
const js = fs.readFileSync(path.join(root, 'injector/fingerprint.js'), 'utf8');

const cfg = {
  seed: 12345,
  timezone: 'Asia/Tokyo',
  timezoneOffset: 540,
  locale: 'ja-JP',
  language: 'ja-JP',
  locales: ['ja-JP', 'ja'],
  languages: ['ja-JP', 'ja'],
  hardwareConcurrency: 4,
  deviceMemory: 8,
  platform: 'Win32',
  screenWidth: 1440,
  screenHeight: 900,
  screenAvailWidth: 1440,
  screenAvailHeight: 860,
  colorDepth: 24,
  devicePixelRatio: 1.0,
  maxTouchPoints: 0,
  userAgent: '',
  fonts: ['Arial', 'Segoe UI', 'Yu Gothic'],
  fontFallbackWidth: 7.2,
  canvasNoise: true,
  canvasBlock: false,
  canvasFill: '#ffffff',
  audioNoise: true,
  webglVendor: 'Google Inc. (Intel)',
  webglRenderer: 'ANGLE (Intel, Intel(R) UHD Graphics 620 Direct3D11 vs_5_0 ps_5_0, D3D11)',
  webrtcMode: 'block',
  emojiStyle: 'native',
  plugins: ['PDF Viewer'],
};

const payload = js.replace('__PX_CFG__', JSON.stringify(cfg));
const out = path.join(process.env.TEMP, 'payload.js');
fs.writeFileSync(out, payload, 'utf8');
console.log('payload bytes=' + Buffer.byteLength(payload, 'utf8'));

try {
  new Function(payload);
  console.log('syntax OK');
} catch (e) {
  console.log('syntax FAIL: ' + e.message);
  process.exitCode = 1;
}

$ErrorActionPreference = 'Stop'
$root = "C:\Users\1106\Documents\deepseek-harness\default-workspace\OpenFingerprintBrowser"
$utf8 = New-Object System.Text.UTF8Encoding($false)

$js = [System.IO.File]::ReadAllText("$root\injector\fingerprint.js", [System.Text.Encoding]::UTF8)

$cfg = @'
{
 "seed": 12345,
 "timezone": "Asia/Tokyo",
 "timezoneOffset": 540,
 "locale": "ja-JP",
 "language": "ja-JP",
 "locales": ["ja-JP","ja"],
 "languages": ["ja-JP","ja"],
 "hardwareConcurrency": 4,
 "deviceMemory": 8,
 "platform": "Win32",
 "screenWidth": 1440,
 "screenHeight": 900,
 "screenAvailWidth": 1440,
 "screenAvailHeight": 860,
 "colorDepth": 24,
 "devicePixelRatio": 1.0,
 "maxTouchPoints": 0,
 "userAgent": "",
 "fonts": ["Arial","Segoe UI","Yu Gothic"],
 "fontFallbackWidth": 7.2,
 "canvasNoise": true,
 "canvasBlock": false,
 "canvasFill": "#ffffff",
 "audioNoise": true,
 "webglVendor": "Google Inc. (Intel)",
 "webglRenderer": "ANGLE (Intel, Intel(R) UHD Graphics 620 Direct3D11 vs_5_0 ps_5_0, D3D11)",
 "webrtcMode": "block",
 "emojiStyle": "native",
 "plugins": ["PDF Viewer"]
}
'@

$payload = $js.Replace('__PX_CFG__', $cfg)
$out = "$env:TEMP\payload.js"
[System.IO.File]::WriteAllText($out, $payload, $utf8)
Write-Output "payload bytes=$((Get-Item $out).Length)"

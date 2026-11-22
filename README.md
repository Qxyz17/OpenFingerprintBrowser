# 打开指纹浏览器

每个环境独立窗口、独立 user-data-dir、独立指纹。开一个环境就是开一个浏览器，配置存在本地，互不影响。

界面是 WPF（.NET Framework 4.8），CDP 驱动浏览器，网络层由一个 C++ 拦截引擎配合。

## 它做什么

| 项目 | 手段 |
| --- | --- |
| 时区 | 覆盖 `Date.prototype.getTimezoneOffset`、`Intl.DateTimeFormat` 的 `timeZone`，连同 `resolvedOptions` 一起改 |
| 时区偏移 | `toString` / `toTimeString` 里的 `GMT±HHMM` 一并替换 |
| 语言 | `navigator.language`、`navigator.languages`，以及各 `Intl` 构造器的默认 locale |
| Intl 区域 | `DateTimeFormat`、`NumberFormat`、`Collator`、`PluralRules`、`RelativeTimeFormat`、`ListFormat` 注入时统一带 `localeMatcher: lookup` 和配置 locale |
| 已安装中文字体 | `document.fonts.check` 与 `measureText` 按白名单作答，白名单外的字体一律按回退宽度算 |
| Emoji 渲染风格 | 按配置选择原生或文本呈现 |
| Canvas | 逐像素微抖动，或整体填成纯色 |
| WebGL | 改写 `UNMASKED_VENDOR` / `UNMASKED_RENDERER` 等参数 |
| Audio | FFT 数据叠加极小噪声 |
| 硬件参数 | `hardwareConcurrency`、`deviceMemory`、`platform`、`maxTouchPoints` |
| 屏幕 | `screen.*`、`devicePixelRatio`、`outerWidth/Height` |
| WebRTC 泄漏 | 浏览器侧 `disable_non_proxied_udp` 强制走代理；注入侧过滤 host 与 srflx 候选 |
| DNS 泄漏 | 主机名取自 TLS SNI 交给上游代理解析，本机不做解析 |

## 注入是怎么抢到时机

Chromium 的 `--remote-debugging-port` 打开后按这个顺序走：

1. 建一个 `about:blank` 目标
2. `Target.attachToTarget` 拿到 session
3. `Page.addScriptToEvaluateOnNewDocument` 挂上注入脚本
4. `Fetch.enable` 扣住所有请求
5. 注入完成后才逐个 `Fetch.continueRequest` 放行
6. 最后 `Page.navigate` 到起始页

因为脚本挂在新文档创建之前，页面拿不到任何注入前的执行机会。请求在放行前一直被拦住，网站也拿不到首包。

## 目录

```
injector/fingerprint.js   注入载荷，编译时嵌入 exe
ui/Browser/               CDP 连接、会话、profile 存储、注入装配
ui/                       WPF 界面
proxifier/                C++ 网络层，WinDivert 拦截与转发
```

## 构建

```
dotnet build ui/PxUi.csproj -c Release
```

引擎另编：

```
cmake -S proxifier -B proxifier/build -G "Visual Studio 17 2022" -A x64
cmake --build proxifier/build --config Release
```

GitHub Actions 里两者都会跑，产物在 [`dist`](.github/workflows/proxifier.yml)。

## 数据位置

```
%LOCALAPPDATA%\PxUi\profiles\<id>.json   每个环境的配置
%LOCALAPPDATA%\PxUi\profiles\<id>\       该环境的浏览器数据
%LOCALAPPDATA%\PxUi\pxui.log             日志
```

## 限制

- 注入层是 JS 覆盖，不是改内核。`Function.prototype.toString` 已经做了 native 伪装，但高级检测方仍可能找到破绽
- 只处理 TCP。QUIC 由 `--disable-quic` 关掉，否则连接会绕过代理
- 浏览器本体用系统已装的 Chrome 或 Edge，需要自己指定路径
- 每个环境一个浏览器进程，开多了吃内存

## 许可

MIT

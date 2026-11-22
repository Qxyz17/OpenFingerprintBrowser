## 内容

| 文件 | 说明 |
| --- | --- |
| PxUi.exe | 界面主程序 |
| proxifier.dll | 拦截引擎 |
| WinDivert.dll | 驱动接口 |
| WinDivert64.sys | 内核驱动 |

解压后直接运行 `PxUi.exe`，需要管理员权限。五个文件必须放在同一目录。

## 能做什么

- 按进程名拦截出站 TCP，目标进程为 Chrome 和 Edge，其余进程流量不动
- 上游支持 SOCKS5 和 HTTP CONNECT，带用户名密码认证
- 主机名取自 TLS 的 SNI 交给上游代理解析，本机不做 DNS 查询
- 界面实时开关，勾选目标应用后引擎自动重启

## 不能做什么

- 只处理 TCP。UDP 和 QUIC 不经过重定向，Chrome 的 QUIC 连接会绕过代理
- 不修改浏览器指纹：时区、时间、语言、字体、Canvas、WebGL 一律不碰
- 不做 HTTPS 解密

## 校验

```
SHA-256 2d9f0e7ea21009226ffbf1b13f56e8dd9b022b79431ea94c359f6f1ba2031b30  PxUi-0.1.0-win-x64.zip
```

## 已知问题

- 首次运行会释放并加载 WinDivert 驱动，部分安全软件会拦截
- 多网卡环境下代端口绑定在首个可用 IPv4 上，出站接口不一致时代理会失败
- 端口段设置改动需要先停止引擎

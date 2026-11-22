# PxUi

Windows 下的按进程代理工具。拦截指定进程的出站 TCP 连接，转发到上游代理，其余进程不受影响。

界面是 WPF（.NET Framework 4.8），拦截引擎是 C++ 动态库，两者通过 `px_*` C 接口衔接。

## 功能

- 目标进程按名字匹配，支持 Chrome 和 Edge，运行中改名单自动重载引擎
- 上游支持 SOCKS5 和 HTTP CONNECT，带用户名密码认证
- 主机名取自 TLS 的 SNI，交给上游代理解析，本机不做 DNS 查询
- 设置项改完即写盘，端口段改动需要停一次引擎
- 日志同时写界面和 `%LOCALAPPDATA%\PxUi\pxui.log`

## 目录

```
proxifier/        C++ 拦截引擎，编译成 proxifier.dll
  src/api.cpp     对外 C 接口
  src/engine.cpp  WinDivert 收包、四元组改写、代端口监听
  src/relay.cpp   上游代理握手与双向转发
  src/sni.cpp     TLS ClientHello 里取 SNI
  src/targets.cpp 目标与代端口映射
ui/               WPF 界面，编译成 PxUi.exe
.github/workflows 构建流程
```

## 工作方式

WinDivert 在网络层截获本机出站 TCP 包。SYN 包到达时用 `GetExtendedTcpTable` 按四元组反查属主 PID，命中目标进程就把目标地址改写到本机网卡的某个代端口上。代端口按目标地址分配，上面挂着监听线程，接受连接后与上游代理握手，再在两个 socket 之间搬运字节。回包方向把源地址改回原始目标，客户端看到的就是自己原本要连的地址。

环回地址在 WinDivert 的包路径上看不到，所以重定向落到真实网卡 IPv4 上。

## 构建

需要 .NET SDK 8+ 和 VS 2022 的 C++ 工具链，或者直接用 GitHub Actions。

```
cmake -S proxifier -B proxifier/build -G "Visual Studio 17 2022" -A x64
cmake --build proxifier/build --config Release
dotnet build ui/PxUi.csproj -c Release
```

WinDivert SDK 放在 `proxifier/third_party/windivert`，构建时从 release 下载。

## 运行

`PxUi.exe` 需要管理员权限，清单里已声明 `requireAdministrator`。程序目录里必须同时有 `proxifier.dll`、`WinDivert.dll`、`WinDivert64.sys`，缺任何一个启动即失败。

配置写在 `%LOCALAPPDATA%\PxUi\config.ini`，也可以用 `pxcli --config <path>` 走命令行。

## 限制

- 只处理 TCP。UDP 和 QUIC 不经过重定向，Chrome 走 QUIC 的连接会绕过代理
- 进程名匹配在每次 SYN 时按 1.5 秒缓存的快照查表，刚启动的进程可能要等一下才纳入
- 代端口占在网卡 IP 上，多网卡环境需要确认出站接口与代端口同侧
- WinDivert 驱动需要签名环境；测试机上要允许加载未签名驱动才能自编译版本可用

## 许可

MIT

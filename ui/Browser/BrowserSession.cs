using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;

namespace PxUi.Browser
{
    /// <summary>
    /// 单条 CDP 连接的请求响应封装。命令按 id 配对，事件走回调。
    /// </summary>
    internal sealed class CdpConnection : IDisposable
    {
        private readonly ClientWebSocket _ws = new ClientWebSocket();
        private readonly SemaphoreSlim _sendLock = new SemaphoreSlim(1, 1);
        private readonly Dictionary<int, TaskCompletionSource<JObject>> _pending =
            new Dictionary<int, TaskCompletionSource<JObject>>();
        private readonly object _gate = new object();
        private int _nextId;
        private Task _reader;

        public event Action<string, JObject> Event;

        public async Task ConnectAsync(string url, CancellationToken token)
        {
            _ws.Options.KeepAliveInterval = TimeSpan.FromSeconds(20);
            await _ws.ConnectAsync(new Uri(url), token).ConfigureAwait(false);

            _reader = Task.Run(() => ReadLoop(token));
        }

        private async Task ReadLoop(CancellationToken token)
        {
            var buffer = new byte[64 * 1024];
            var pending = new MemoryStream();

            while (!token.IsCancellationRequested && _ws.State == WebSocketState.Open)
            {
                WebSocketReceiveResult result;
                try
                {
                    result = await _ws.ReceiveAsync(new ArraySegment<byte>(buffer), token)
                        .ConfigureAwait(false);
                }
                catch
                {
                    break;
                }

                if (result.MessageType == WebSocketMessageType.Close)
                {
                    break;
                }

                pending.Write(buffer, 0, result.Count);
                if (!result.EndOfMessage)
                {
                    continue;
                }

                string text = Encoding.UTF8.GetString(pending.ToArray());
                pending.SetLength(0);

                JObject msg;
                try { msg = JObject.Parse(text); }
                catch { continue; }

                if (msg["id"] != null)
                {
                    int id = msg.Value<int>("id");
                    TaskCompletionSource<JObject> src;
                    lock (_gate)
                    {
                        if (!_pending.TryGetValue(id, out src)) continue;
                        _pending.Remove(id);
                    }
                    src.TrySetResult(msg);
                }
                else if (msg["method"] != null)
                {
                    Event?.Invoke(msg.Value<string>("method"), msg);
                }
            }

            lock (_gate)
            {
                foreach (var kv in _pending)
                {
                    kv.Value.TrySetException(new IOException("CDP 连接断开"));
                }
                _pending.Clear();
            }
        }

        public async Task<JObject> SendAsync(string method, JObject parameters,
            CancellationToken token, int timeoutMs = 15000)
        {
            int id;
            var src = new TaskCompletionSource<JObject>(
                TaskCreationOptions.RunContinuationsAsynchronously);

            lock (_gate)
            {
                id = ++_nextId;
                _pending[id] = src;
            }

            var payload = new JObject
            {
                ["id"] = id,
                ["method"] = method,
                ["params"] = parameters ?? new JObject()
            };

            byte[] bytes = Encoding.UTF8.GetBytes(payload.ToString(Newtonsoft.Json.Formatting.None));

            await _sendLock.WaitAsync(token).ConfigureAwait(false);
            try
            {
                await _ws.SendAsync(new ArraySegment<byte>(bytes),
                    WebSocketMessageType.Text, true, token).ConfigureAwait(false);
            }
            finally
            {
                _sendLock.Release();
            }

            using (var cts = CancellationTokenSource.CreateLinkedTokenSource(token))
            {
                cts.CancelAfter(timeoutMs);
                using (cts.Token.Register(() => src.TrySetException(
                    new TimeoutException("CDP 命令超时: " + method))))
                {
                    var resp = await src.Task.ConfigureAwait(false);
                    if (resp["error"] != null)
                    {
                        throw new InvalidOperationException(method + ": " + resp["error"]);
                    }
                    return resp["result"] as JObject ?? new JObject();
                }
            }
        }

        public void Dispose()
        {
            try { _ws.Dispose(); } catch { }
        }
    }

    /// <summary>
    /// 每个 profile 一个 Chromium 实例，独立 user-data-dir，独立调试端口。
    /// </summary>
    internal sealed class BrowserSession : IDisposable
    {
        private readonly Profile _profile;
        private Process _process;
        private CdpConnection _connection;
        private int _debugPort;
        private CancellationTokenSource _cts;

        public BrowserSession(Profile profile)
        {
            _profile = profile;
        }

        public bool IsRunning
        {
            get
            {
                try { return _process != null && !_process.HasExited; }
                catch { return false; }
            }
        }

        public async Task StartAsync(Action<string> log, CancellationToken token)
        {
            _cts = CancellationTokenSource.CreateLinkedTokenSource(token);
            _debugPort = FreePort();

            string profileDir = Path.Combine(AppConfig.ProfileRoot, _profile.Id);
            Directory.CreateDirectory(profileDir);

            string exe = _profile.BrowserPath;
            if (string.IsNullOrEmpty(exe) || !File.Exists(exe))
            {
                throw new FileNotFoundException("浏览器路径无效: " + exe);
            }

            var args = new List<string>
            {
                "--remote-debugging-port=" + _debugPort,
                "--user-data-dir=" + profileDir,
                "--no-first-run",
                "--no-default-browser-check",
                // 关掉 QUIC，否则连接绕过代理
                "--disable-quic",
                "--lang=" + _profile.Language,
                "--accept-lang=" + string.Join(",", _profile.AcceptLanguages)
            };

            if (_profile.ProxyMode != "direct" && _profile.ProxyPort > 0)
            {
                args.Add("--proxy-server=http://127.0.0.1:" + _profile.ProxyPort);
                // 只走代理，不走直连 UDP，WebRTC 拿不到内网地址
                args.Add("--force-webrtc-ip-handling-policy=disable_non_proxied_udp");
                args.Add("--webrtc-ip-handling-policy=disable_non_proxied_udp");
            }

            if (!_profile.WebRtc)
            {
                args.Add("--disable-webrtc");
            }

            if (!string.IsNullOrEmpty(_profile.Args))
            {
                args.AddRange(SplitArgs(_profile.Args));
            }

            var psi = new ProcessStartInfo
            {
                FileName = exe,
                Arguments = string.Join(" ", args.ConvertAll(Quote).ToArray()),
                UseShellExecute = false,
                WorkingDirectory = Path.GetDirectoryName(exe)
            };

            _process = Process.Start(psi);
            if (_process == null)
            {
                throw new InvalidOperationException("浏览器启动失败");
            }

            // 等调试端口就绪，再接管
            await WaitForDevToolsAsync(log, _cts.Token).ConfigureAwait(false);

            string wsUrl = await ResolveTargetAsync(_cts.Token).ConfigureAwait(false);
            _connection = new CdpConnection();
            _connection.Event += (m, p) => OnEvent(m, p, log);
            await _connection.ConnectAsync(wsUrl, _cts.Token).ConfigureAwait(false);

            await ApplyAsync(_profile, log, _cts.Token).ConfigureAwait(false);
        }

        private async Task ApplyAsync(Profile profile, Action<string> log, CancellationToken token)
        {
            // 注入脚本必须先于任何页面脚本，加在空白页上，之后的导航才继承
            string payload = Injector.Build(profile);

            var create = await _connection.SendAsync("Target.createTarget",
                new JObject { ["url"] = "about:blank" }, token).ConfigureAwait(false);

            string targetId = create.Value<string>("targetId");

            var attach = await _connection.SendAsync("Target.attachToTarget",
                new JObject { ["targetId"] = targetId, ["flatten"] = true }, token)
                .ConfigureAwait(false);

            string sessionId = attach.Value<string>("sessionId");

            await SendToSession(sessionId, "Page.enable", null, token).ConfigureAwait(false);
            await SendToSession(sessionId, "Runtime.enable", null, token).ConfigureAwait(false);

            await SendToSession(sessionId, "Page.addScriptToEvaluateOnNewDocument",
                new JObject { ["source"] = payload }, token).ConfigureAwait(false);

            // 先扣住所有请求，注入完成后才放行，网站拿不到任何注入前的执行机会
            await SendToSession(sessionId, "Fetch.enable", new JObject
            {
                ["patterns"] = new JArray
                {
                    new JObject { ["urlPattern"] = "*", ["requestStage"] = "Request" }
                }
            }, token).ConfigureAwait(false);

            _connection.Event += (method, msg) =>
            {
                if (method != "Fetch.requestPaused")
                {
                    return;
                }
                var p = msg["params"] as JObject;
                if (p == null) return;
                string requestId = p.Value<string>("requestId");
                Task.Run(async () =>
                {
                    try
                    {
                        await SendToSession("Fetch.continueRequest",
                            new JObject { ["requestId"] = requestId }, CancellationToken.None)
                            .ConfigureAwait(false);
                    }
                    catch { }
                });
            };

            log("注入就绪，session " + sessionId);

            if (!string.IsNullOrEmpty(profile.StartUrl))
            {
                await SendToSession(sessionId, "Page.navigate",
                    new JObject { ["url"] = profile.StartUrl }, token).ConfigureAwait(false);
            }

            _sessionId = sessionId;
        }

        private string _sessionId;

        private async Task<JObject> SendToSession(string sessionId, string method,
            JObject parameters, CancellationToken token)
        {
            var p = parameters ?? new JObject();
            p["sessionId"] = sessionId;
            return await _connection.SendAsync(method, p, token).ConfigureAwait(false);
        }

        private void OnEvent(string method, JObject payload, Action<string> log)
        {
            if (method == "Inspector.targetCrashed")
            {
                log("页面崩溃");
            }
        }

        private async Task WaitForDevToolsAsync(Action<string> log, CancellationToken token)
        {
            string url = "http://127.0.0.1:" + _debugPort + "/json/version";
            for (int i = 0; i < 150; i++)
            {
                token.ThrowIfCancellationRequested();
                try
                {
                    using (var wc = new WebClient())
                    {
                        string body = wc.DownloadString(url);
                        if (!string.IsNullOrEmpty(body))
                        {
                            return;
                        }
                    }
                }
                catch
                {
                }
                await Task.Delay(100, token).ConfigureAwait(false);
            }
            throw new TimeoutException("调试端口未就绪");
        }

        private async Task<string> ResolveTargetAsync(CancellationToken token)
        {
            string url = "http://127.0.0.1:" + _debugPort + "/json/version";
            await Task.Yield();
            using (var wc = new WebClient())
            {
                var info = JObject.Parse(wc.DownloadString(url));
                string ws = info.Value<string>("webSocketDebuggerUrl");
                if (string.IsNullOrEmpty(ws))
                {
                    throw new InvalidOperationException("未取得 webSocketDebuggerUrl");
                }
                return ws;
            }
        }

        private static int FreePort()
        {
            var l = new System.Net.Sockets.TcpListener(IPAddress.Loopback, 0);
            l.Start();
            int port = ((IPEndPoint)l.LocalEndpoint).Port;
            l.Stop();
            return port;
        }

        private static string Quote(string s)
        {
            return s.IndexOf(' ') >= 0 ? "\"" + s + "\"" : s;
        }

        private static IEnumerable<string> SplitArgs(string line)
        {
            // 只按空格切，带引号的参数原样保留
            var parts = new List<string>();
            var cur = new StringBuilder();
            bool quoted = false;
            foreach (char c in line)
            {
                if (c == '"') { quoted = !quoted; continue; }
                if (c == ' ' && !quoted)
                {
                    if (cur.Length > 0) { parts.Add(cur.ToString()); cur.Clear(); }
                    continue;
                }
                cur.Append(c);
            }
            if (cur.Length > 0) parts.Add(cur.ToString());
            return parts;
        }

        public void Dispose()
        {
            try { _cts?.Cancel(); } catch { }
            try { _connection?.Dispose(); } catch { }
            try
            {
                if (_process != null && !_process.HasExited)
                {
                    _process.Kill();
                }
            }
            catch { }
        }
    }
}

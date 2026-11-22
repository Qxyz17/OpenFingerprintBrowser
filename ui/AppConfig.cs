using System;
using System.IO;
using System.Text;

namespace PxUi
{
    /// <summary>配置文件读写，键值格式与引擎侧解析器一致</summary>
    internal static class AppConfig
    {
        public string UpstreamType = "socks5";
        public string UpstreamHost = "127.0.0.1";
        public int UpstreamPort = 7897;
        public string Username = "";
        public string Password = "";

        public bool TargetChrome = true;
        public bool TargetEdge = true;

        public int BasePort = 40000;
        public int PortCount = 512;
        public int FlowIdleSeconds = 180;

        public static string Dir =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PxUi");

        public static string ConfigPath => Path.Combine(Dir, "config.ini");

        public static string LogPath => Path.Combine(Dir, "pxui.log");

        public static AppConfig Load()
        {
            var c = new AppConfig();
            try
            {
                if (!File.Exists(ConfigPath)) return c;
                foreach (var raw in File.ReadAllLines(ConfigPath, Encoding.UTF8))
                {
                    var line = raw.Trim();
                    if (line.Length == 0 || line.StartsWith("#") || line.StartsWith("[")) continue;
                    int eq = line.IndexOf('=');
                    if (eq <= 0) continue;
                    string key = line.Substring(0, eq).Trim().ToLowerInvariant();
                    string val = line.Substring(eq + 1).Trim();
                    switch (key)
                    {
                        case "upstream.type": c.UpstreamType = val.ToLowerInvariant(); break;
                        case "upstream.host": c.UpstreamHost = val; break;
                        case "upstream.port": if (int.TryParse(val, out var p1)) c.UpstreamPort = p1; break;
                        case "upstream.username": c.Username = val; break;
                        case "upstream.password": c.Password = val; break;
                        case "base_port": if (int.TryParse(val, out var p2)) c.BasePort = p2; break;
                        case "port_count": if (int.TryParse(val, out var p3)) c.PortCount = p3; break;
                        case "flow_idle_seconds": if (int.TryParse(val, out var p4)) c.FlowIdleSeconds = p4; break;
                    }
                }
            }
            catch { }
            return c;
        }

        public void Save()
        {
            Directory.CreateDirectory(Dir);
            var sb = new StringBuilder();
            sb.AppendLine("# 上游代理，支持 http / socks5");
            sb.AppendLine("upstream.type = " + UpstreamType);
            sb.AppendLine("upstream.host = " + UpstreamHost);
            sb.AppendLine("upstream.port = " + UpstreamPort);
            sb.AppendLine("upstream.username = " + Username);
            sb.AppendLine("upstream.password = " + Password);
            sb.AppendLine();
            sb.AppendLine("# 空列表时引擎不拦截任何进程");
            sb.AppendLine("processes = " + string.Join(", ", TargetNames()));
            sb.AppendLine("exclude_processes =");
            sb.AppendLine();
            sb.AppendLine("base_port = " + BasePort);
            sb.AppendLine("port_count = " + PortCount);
            sb.AppendLine("flow_idle_seconds = " + FlowIdleSeconds);
            File.WriteAllText(ConfigPath, sb.ToString(), new UTF8Encoding(false));
        }

        System.Collections.Generic.List<string> TargetNames()
        {
            var list = new System.Collections.Generic.List<string>();
            if (TargetChrome) list.Add("chrome.exe");
            if (TargetEdge) list.Add("msedge.exe");
            return list;
        }
    }
}

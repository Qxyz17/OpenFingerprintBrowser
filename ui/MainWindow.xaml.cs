using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Media;

namespace PxUi
{
    public partial class MainWindow : Window
    {
        private readonly AppConfig _cfg = App.Config;
        private bool _applying;
        private Native.LogSink _sink;

        public MainWindow()
        {
            InitializeComponent();
            _sink = OnEngineLog;
            Native.SetLogHandler(_sink);
            LoadToUi();
            RefreshStatus();
        }

        private void LoadToUi()
        {
            _applying = true;

            ChromeCheck.IsChecked = _cfg.TargetChrome;
            EdgeCheck.IsChecked = _cfg.TargetEdge;

            TypeBox.SelectedIndex = _cfg.UpstreamType == "http" ? 1 : 0;
            HostBox.Text = _cfg.UpstreamHost;
            PortBox.Text = _cfg.UpstreamPort.ToString();
            UserBox.Text = _cfg.Username;
            PassBox.Text = _cfg.Password;

            BasePortBox.Text = _cfg.BasePort.ToString();
            PortCountBox.Text = _cfg.PortCount.ToString();
            IdleBox.Text = _cfg.FlowIdleSeconds.ToString();

            _applying = false;
        }

        private void ReadFromUi()
        {
            _cfg.TargetChrome = ChromeCheck.IsChecked == true;
            _cfg.TargetEdge = EdgeCheck.IsChecked == true;

            _cfg.UpstreamType = TypeBox.SelectedIndex == 1 ? "http" : "socks5";
            _cfg.UpstreamHost = HostBox.Text.Trim();
            if (int.TryParse(PortBox.Text.Trim(), out var port) && port > 0 && port < 65536)
                _cfg.UpstreamPort = port;
            _cfg.Username = UserBox.Text.Trim();
            _cfg.Password = PassBox.Text;

            if (int.TryParse(BasePortBox.Text.Trim(), out var bp) && bp >= 1024 && bp < 65000)
                _cfg.BasePort = bp;
            if (int.TryParse(PortCountBox.Text.Trim(), out var pc) && pc > 0 && pc <= 4096)
                _cfg.PortCount = pc;
            if (int.TryParse(IdleBox.Text.Trim(), out var idle) && idle >= 10)
                _cfg.FlowIdleSeconds = idle;
        }

        private bool Running => Native.px_is_running() == 1;

        private void RefreshStatus()
        {
            bool running = Running;
            StatusDot.Fill = running
                ? (Brush)new SolidColorBrush(Color.FromRgb(0x22, 0xA0, 0x6B))
                : new SolidColorBrush(Color.FromRgb(0x9A, 0xA1, 0xAC));
            StatusText.Text = running ? "运行中" : "未启动";
            ToggleButton.Content = running ? "停止" : "启动";

            // 端口段改动需要重启引擎才生效，运行中锁住
            BasePortBox.IsEnabled = !running;
            PortCountBox.IsEnabled = !running;
        }

        private void Toggle_Click(object sender, RoutedEventArgs e)
        {
            if (Running)
            {
                Native.px_stop();
                AppendLog("引擎已停止");
                RefreshStatus();
                return;
            }

            ReadFromUi();
            _cfg.Save();

            int rc = Native.px_start(AppConfig.ConfigPath);
            if (rc != 0)
            {
                string err = Native.LastError();
                AppendLog("启动失败: " + (string.IsNullOrEmpty(err) ? "错误码 " + rc : err));
                MessageBox.Show(this, string.IsNullOrEmpty(err) ? "错误码 " + rc : err, "启动失败",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            else
            {
                AppendLog("引擎已启动");
            }
            RefreshStatus();
        }

        private void Target_Changed(object sender, RoutedEventArgs e)
        {
            if (_applying) return;
            ReadFromUi();
            _cfg.Save();

            // 名单改动要靠重载生效，运行中就自动重启一次
            if (Running)
            {
                Native.px_stop();
                int rc = Native.px_start(AppConfig.ConfigPath);
                AppendLog(rc == 0 ? "名单已更新，引擎重启" : "名单已更新，重启失败");
            }
            RefreshStatus();
        }

        private void OnEngineLog(IntPtr text)
        {
            string line = text == IntPtr.Zero ? "" : Marshal.PtrToStringAnsi(text);
            if (string.IsNullOrEmpty(line)) return;
            Dispatcher.BeginInvoke(new Action(() => AppendLog(line)));
        }

        private void AppendLog(string line)
        {
            App.Log(line);
            LogBox.AppendText(DateTime.Now.ToString("HH:mm:ss") + " " + line + Environment.NewLine);
            LogBox.ScrollToEnd();
        }

        private void ClearLog_Click(object sender, RoutedEventArgs e)
        {
            LogBox.Clear();
        }

        private void Min_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

        private void Max_Click(object sender, RoutedEventArgs e)
        {
            WindowState = WindowState == WindowState.Maximized
                ? WindowState.Normal
                : WindowState.Maximized;
        }

        private void Close_Click(object sender, RoutedEventArgs e) => Close();

        protected override void OnClosed(EventArgs e)
        {
            Native.px_stop();
            base.OnClosed(e);
        }
    }
}

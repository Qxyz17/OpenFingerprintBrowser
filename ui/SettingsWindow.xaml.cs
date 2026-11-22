using System;
using System.Windows;
using System.Windows.Media;

namespace PxUi
{
    public partial class SettingsWindow : Window
    {
        private readonly AppConfig _cfg = App.Config;

        public SettingsWindow()
        {
            InitializeComponent();
            LoadFromConfig();
            RefreshStatus();
        }

        private void LoadFromConfig()
        {
            TypeBox.SelectedIndex = _cfg.UpstreamType == "http" ? 1 : 0;
            HostBox.Text = _cfg.UpstreamHost;
            PortBox.Text = _cfg.UpstreamPort.ToString();
            UserBox.Text = _cfg.Username;
            PassBox.Text = _cfg.Password;
        }

        private bool Running => Native.px_is_running() == 1;

        private void RefreshStatus()
        {
            bool running = Running;
            StatusDot.Fill = running
                ? new SolidColorBrush(Color.FromRgb(0x22, 0xA0, 0x6B))
                : new SolidColorBrush(Color.FromRgb(0x9A, 0xA1, 0xAC));
            StatusText.Text = running ? "运行中" : "未启动";
            ToggleButton.Content = running ? "停止" : "启动";
        }

        private void Toggle_Click(object sender, RoutedEventArgs e)
        {
            if (Running)
            {
                Native.px_stop();
                App.Log("引擎已停止");
                RefreshStatus();
                return;
            }

            ReadIntoConfig();
            _cfg.Save();

            int rc = Native.px_start(AppConfig.ConfigPath);
            if (rc != 0)
            {
                string err = Native.LastError();
                MessageBox.Show(this, string.IsNullOrEmpty(err) ? "错误码 " + rc : err, "启动失败",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            else
            {
                App.Log("引擎已启动");
            }
            RefreshStatus();
        }

        private void ReadIntoConfig()
        {
            _cfg.UpstreamType = TypeBox.SelectedIndex == 1 ? "http" : "socks5";
            _cfg.UpstreamHost = HostBox.Text.Trim();
            if (int.TryParse(PortBox.Text.Trim(), out var port) && port > 0 && port < 65536)
                _cfg.UpstreamPort = port;
            _cfg.Username = UserBox.Text.Trim();
            _cfg.Password = PassBox.Text;
        }

        private void Save_Click(object sender, RoutedEventArgs e)
        {
            ReadIntoConfig();
            _cfg.Save();
            DialogResult = true;
        }

        private void Close_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
        }
    }
}

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using PxUi.Browser;

namespace PxUi
{
    public partial class MainWindow : Window
    {
        private readonly ObservableCollection<ProfileRow> _rows = new ObservableCollection<ProfileRow>();
        private readonly Dictionary<string, BrowserSession> _sessions =
            new Dictionary<string, BrowserSession>();

        public MainWindow()
        {
            InitializeComponent();
            ProfileList.ItemsSource = _rows;
            Reload();
        }

        internal static readonly Brush RunningBrush =
            new SolidColorBrush(Color.FromRgb(0x22, 0xA0, 0x6B));
        internal static readonly Brush IdleBrush =
            new SolidColorBrush(Color.FromRgb(0x9A, 0xA1, 0xAC));

        internal void Reload()
        {
            _rows.Clear();
            var list = ProfileStore.List();
            foreach (var p in list)
            {
                _rows.Add(new ProfileRow(p, IsRunning(p.Id) ? "运行中" : "未启动",
                    IsRunning(p.Id) ? RunningBrush : IdleBrush));
            }
            CountText.Text = list.Count + " 个配置";
            EmptyHint.Visibility = list.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        }

        internal bool IsRunning(string id)
        {
            BrowserSession s;
            return _sessions.TryGetValue(id, out s) && s.IsRunning;
        }

        private void New_Click(object sender, RoutedEventArgs e)
        {
            var profile = new Profile { Name = "环境 " + (_rows.Count + 1) };
            profile.BrowserPath = BrowserLocator.Detect();

            var editor = new ProfileWindow(profile, true) { Owner = this };
            if (editor.ShowDialog() == true)
            {
                ProfileStore.Save(profile);
                Reload();
                AppendLog("已创建 " + profile.Name);
            }
        }

        private void Edit_Click(object sender, RoutedEventArgs e)
        {
            var row = (sender as Button)?.Tag as ProfileRow;
            if (row == null) return;

            var editor = new ProfileWindow(row.Profile, false) { Owner = this };
            if (editor.ShowDialog() == true)
            {
                ProfileStore.Save(row.Profile);
                Reload();
            }
        }

        private void Open_Click(object sender, RoutedEventArgs e)
        {
            var row = (sender as Button)?.Tag as ProfileRow;
            if (row == null) return;
            OpenProfile(row.Profile);
        }

        private void ProfileList_DoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            var row = ProfileList.SelectedItem as ProfileRow;
            if (row == null) return;
            OpenProfile(row.Profile);
        }

        private void OpenProfile(Profile profile)
        {
            if (IsRunning(profile.Id))
            {
                AppendLog(profile.Name + " 已在运行");
                return;
            }

            var session = new BrowserSession(profile);
            _sessions[profile.Id] = session;
            AppendLog("启动 " + profile.Name);

            System.Threading.Tasks.Task.Run(async () =>
            {
                try
                {
                    await session.StartAsync(msg => Dispatcher.BeginInvoke(
                        new Action(() => AppendLog(profile.Name + ": " + msg))),
                        System.Threading.CancellationToken.None);
                }
                catch (Exception ex)
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        AppendLog(profile.Name + " 启动失败: " + ex.Message);
                        _sessions.Remove(profile.Id);
                        Reload();
                    }));
                }
            });

            Reload();
        }

        private void Delete_Click(object sender, RoutedEventArgs e)
        {
            var row = (sender as Button)?.Tag as ProfileRow;
            if (row == null) return;

            if (MessageBox.Show(this, "删除 " + row.Profile.Name + " 及其浏览器数据？", "确认",
                    MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK)
            {
                return;
            }

            BrowserSession session;
            if (_sessions.TryGetValue(row.Profile.Id, out session))
            {
                session.Dispose();
                _sessions.Remove(row.Profile.Id);
            }

            ProfileStore.Delete(row.Profile);
            try
            {
                string dir = System.IO.Path.Combine(AppConfig.ProfileRoot, row.Profile.Id);
                if (System.IO.Directory.Exists(dir))
                {
                    System.IO.Directory.Delete(dir, true);
                }
            }
            catch
            {
            }

            Reload();
        }

        private void Proxy_Click(object sender, RoutedEventArgs e)
        {
            new SettingsWindow { Owner = this }.ShowDialog();
        }

        private void AppendLog(string line)
        {
            App.Log(line);
        }

        private void ClearLog_Click(object sender, RoutedEventArgs e) { }

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
            foreach (var s in _sessions.Values)
            {
                s.Dispose();
            }
            _sessions.Clear();
            Application.Current.Shutdown();
        }
    }

    internal sealed class ProfileRow
    {
        public ProfileRow(Profile p, string status, Brush color)
        {
            Profile = p;
            StatusText = status;
            StatusColor = color;
        }

        public Profile Profile { get; }
        public string Name => Profile.Name;
        public string Remark => Profile.Remark;
        public string BrowserKind => Profile.BrowserKind;
        public string Timezone => Profile.Timezone;
        public string Language => Profile.Language;
        public string StatusText { get; }
        public Brush StatusColor { get; }
    }

    internal static class BrowserLocator
    {
        public static string Detect()
        {
            string[] candidates =
            {
                @"C:\Program Files\Google\Chrome\Application\chrome.exe",
                @"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
                @"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
                @"C:\Program Files\Microsoft\Edge\Application\msedge.exe"
            };
            foreach (var path in candidates)
            {
                if (System.IO.File.Exists(path)) return path;
            }
            return "";
        }
    }
}

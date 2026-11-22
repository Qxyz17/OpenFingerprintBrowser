using System;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using PxUi.Browser;

namespace PxUi
{
    public partial class ProfileWindow : Window
    {
        private readonly Profile _profile;
        private readonly bool _creating;
        private bool _loading;

        public ProfileWindow(Profile profile, bool creating)
        {
            InitializeComponent();
            _profile = profile;
            _creating = creating;

            TitleText.Text = creating ? "新建环境" : "编辑环境";
            LoadPresets();
            LoadFromProfile();
        }

        private void LoadPresets()
        {
            PresetBox.Items.Clear();
            PresetBox.Items.Add("自定义");
            foreach (var p in ProfileDefaults.Presets)
            {
                PresetBox.Items.Add(p.Name + "  " + p.Tz);
            }
            PresetBox.SelectedIndex = 0;
        }

        private void LoadFromProfile()
        {
            _loading = true;

            NameBox.Text = _profile.Name;
            RemarkBox.Text = _profile.Remark;
            StartUrlBox.Text = _profile.StartUrl;
            BrowserBox.Text = _profile.BrowserPath;
            DetectText.Text = string.IsNullOrEmpty(_profile.BrowserPath)
                ? "未检测到浏览器，手动指定 chrome.exe 或 msedge.exe"
                : "";

            TimezoneBox.Text = _profile.Timezone;
            OffsetBox.Text = _profile.TimezoneOffset.ToString(CultureInfo.InvariantCulture);
            LangBox.Text = _profile.Language;
            AcceptBox.Text = _profile.AcceptLanguagesRaw;
            LocaleBox.Text = _profile.Locale;
            EmojiBox.SelectedIndex = _profile.EmojiStyle == "text" ? 1 : 0;

            ScreenWBox.Text = _profile.ScreenWidth.ToString();
            ScreenHBox.Text = _profile.ScreenHeight.ToString();
            AvailHBox.Text = _profile.ScreenAvailHeight.ToString();
            DprBox.Text = _profile.DevicePixelRatio.ToString(CultureInfo.InvariantCulture);
            TouchBox.Text = _profile.MaxTouchPoints.ToString();

            CoresBox.Text = _profile.HardwareConcurrency.ToString();
            MemBox.Text = _profile.DeviceMemory.ToString(CultureInfo.InvariantCulture);
            PlatformBox.Text = _profile.Platform;

            CanvasNoiseCheck.IsChecked = _profile.CanvasNoise;
            CanvasBlockCheck.IsChecked = _profile.CanvasBlock;
            AudioNoiseCheck.IsChecked = _profile.AudioNoise;
            GlVendorBox.Text = _profile.WebglVendor;
            GlRendererBox.Text = _profile.WebglRenderer;

            FontsBox.Text = _profile.FontsRaw;
            FontCountText.Text = _profile.Fonts.Count + " 个字体";

            WebRtcCheck.IsChecked = _profile.WebRtc;
            WebRtcModeBox.SelectedIndex = _profile.WebRtcMode == "off" ? 1 : 0;
            ProxyCheck.IsChecked = _profile.ProxyMode != "direct";

            _loading = false;
        }

        private void Preset_Changed(object sender, SelectionChangedEventArgs e)
        {
            if (_loading || PresetBox.SelectedIndex <= 0) return;

            var preset = ProfileDefaults.Presets[PresetBox.SelectedIndex - 1];
            TimezoneBox.Text = preset.Tz;
            OffsetBox.Text = preset.Offset.ToString(CultureInfo.InvariantCulture);
            LangBox.Text = preset.Lang;
            AcceptBox.Text = preset.Lang + "," + preset.Lang.Split('-')[0];
            LocaleBox.Text = preset.Lang;
        }

        private void Timezone_Changed(object sender, TextChangedEventArgs e)
        {
            // 时区名改了就把偏移按当前时间算出来，用户还能再手改
            if (_loading) return;
            string tz = TimezoneBox.Text.Trim();
            if (tz.Length == 0) return;

            try
            {
                var zone = TimeZoneInfo.FindSystemTimeZoneById(tz);
                int offset = (int)zone.GetUtcOffset(DateTime.UtcNow).TotalMinutes;
                OffsetBox.Text = offset.ToString(CultureInfo.InvariantCulture);
            }
            catch
            {
            }
        }

        private void WebRtc_Toggle(object sender, RoutedEventArgs e)
        {
            if (WebRtcModeBox != null)
            {
                WebRtcModeBox.IsEnabled = WebRtcCheck.IsChecked == true;
            }
        }

        private void Browse_Click(object sender, RoutedEventArgs e)
        {
            var dlg = new OpenFileDialog
            {
                Filter = "浏览器|chrome.exe;msedge.exe|可执行文件|*.exe",
                Title = "选择浏览器"
            };
            if (dlg.ShowDialog() == true)
            {
                BrowserBox.Text = dlg.FileName;
                DetectText.Text = "";
            }
        }

        private void Save_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrWhiteSpace(NameBox.Text))
            {
                MessageBox.Show(this, "名称不能为空", "提示", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            if (string.IsNullOrWhiteSpace(BrowserBox.Text))
            {
                MessageBox.Show(this, "需要指定浏览器路径", "提示", MessageBoxButton.OK,
                    MessageBoxImage.Warning);
                return;
            }

            _profile.Name = NameBox.Text.Trim();
            _profile.Remark = RemarkBox.Text.Trim();
            _profile.StartUrl = StartUrlBox.Text.Trim();
            _profile.BrowserPath = BrowserBox.Text.Trim();
            _profile.BrowserKind = _profile.BrowserPath.ToLowerInvariant().Contains("msedge")
                ? "edge" : "chrome";

            _profile.Timezone = TimezoneBox.Text.Trim();
            if (int.TryParse(OffsetBox.Text, out var off)) _profile.TimezoneOffset = off;
            _profile.Language = LangBox.Text.Trim();
            _profile.AcceptLanguagesRaw = AcceptBox.Text.Trim();
            _profile.Locale = LocaleBox.Text.Trim();
            _profile.EmojiStyle = EmojiBox.SelectedIndex == 1 ? "text" : "native";

            if (int.TryParse(ScreenWBox.Text, out var sw)) _profile.ScreenWidth = sw;
            if (int.TryParse(ScreenHBox.Text, out var sh)) _profile.ScreenHeight = sh;
            if (int.TryParse(AvailHBox.Text, out var ah)) _profile.ScreenAvailHeight = ah;
            _profile.ScreenAvailWidth = _profile.ScreenWidth;
            if (double.TryParse(DprBox.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var dpr))
                _profile.DevicePixelRatio = dpr;
            if (int.TryParse(TouchBox.Text, out var tp)) _profile.MaxTouchPoints = tp;

            if (int.TryParse(CoresBox.Text, out var cores)) _profile.HardwareConcurrency = cores;
            if (double.TryParse(MemBox.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var mem))
                _profile.DeviceMemory = mem;
            _profile.Platform = PlatformBox.Text.Trim();

            _profile.CanvasNoise = CanvasNoiseCheck.IsChecked == true;
            _profile.CanvasBlock = CanvasBlockCheck.IsChecked == true;
            _profile.AudioNoise = AudioNoiseCheck.IsChecked == true;
            _profile.WebglVendor = GlVendorBox.Text.Trim();
            _profile.WebglRenderer = GlRendererBox.Text.Trim();

            _profile.FontsRaw = FontsBox.Text;
            FontCountText.Text = _profile.Fonts.Count + " 个字体";

            _profile.WebRtc = WebRtcCheck.IsChecked == true;
            _profile.WebRtcMode = WebRtcModeBox.SelectedIndex == 1 ? "off" : "block";
            _profile.ProxyMode = ProxyCheck.IsChecked == true ? "pxui" : "direct";

            DialogResult = true;
        }

        private void Close_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
        }
    }
}

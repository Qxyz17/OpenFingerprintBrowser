using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using Newtonsoft.Json;

namespace PxUi.Browser
{
    /// <summary>单个 profile 的全部指纹参数</summary>
    public sealed class Profile
    {
        public string Id = Guid.NewGuid().ToString("N");
        public string Name = "新配置";
        public string Remark = "";

        public string BrowserPath = "";
        public string BrowserKind = "chrome";

        public string StartUrl = "https://www.aboutmy.info";

        // 时区
        public string Timezone = "America/New_York";
        public int TimezoneOffset = -300;

        // 语言与区域
        public string Language = "en-US";
        public string Locale = "en-US";
        public string AcceptLanguagesRaw = "en-US,en";

        // 硬件
        public int HardwareConcurrency = 8;
        public double DeviceMemory = 8;
        public string Platform = "Win32";

        // 屏幕
        public int ScreenWidth = 1920;
        public int ScreenHeight = 1080;
        public int ScreenAvailWidth = 1920;
        public int ScreenAvailHeight = 1032;
        public int ColorDepth = 24;
        public double DevicePixelRatio = 1.0;
        public int MaxTouchPoints = 0;

        public string UserAgent = "";

        // 字体白名单
        public string FontsRaw = ProfileDefaults.WindowsFonts;

        // 噪声
        public bool CanvasNoise = true;
        public bool CanvasBlock = false;
        public string CanvasFill = "#ffffff";
        public bool AudioNoise = true;
        public string WebglVendor = "Google Inc. (Intel)";
        public string WebglRenderer = "ANGLE (Intel, Intel(R) UHD Graphics 620 Direct3D11 vs_5_0 ps_5_0, D3D11)";

        // 网络
        public string ProxyMode = "pxui";
        public int ProxyPort = 0;
        public bool WebRtc = true;
        public string WebRtcMode = "block";

        // Emoji 渲染倾向
        public string EmojiStyle = "native";

        public string Args = "";

        public long Seed = 0;

        public List<string> AcceptLanguages
        {
            get
            {
                var list = new List<string>();
                foreach (var s in AcceptLanguagesRaw.Split(','))
                {
                    var t = s.Trim();
                    if (t.Length > 0) list.Add(t);
                }
                if (list.Count == 0) list.Add(Language);
                return list;
            }
        }

        public List<string> Fonts
        {
            get
            {
                var list = new List<string>();
                foreach (var s in FontsRaw.Split('\n'))
                {
                    var t = s.Trim();
                    if (t.Length > 0) list.Add(t);
                }
                return list;
            }
        }

        public static Profile FromJson(string json)
        {
            return JsonConvert.DeserializeObject<Profile>(json) ?? new Profile();
        }

        public string ToJson()
        {
            return JsonConvert.SerializeObject(this, Formatting.Indented);
        }

        public long EffectiveSeed
        {
            get
            {
                if (Seed != 0) return Seed;
                unchecked
                {
                    long h = 1469598103934665603L;
                    foreach (char c in Id) { h = (h ^ c) * 1099511628211L; }
                    return h;
                }
            }
        }
    }

    internal static class ProfileDefaults
    {
        // 常见 Windows 10 机器上可枚举到的字体，作为可见集合
        public const string WindowsFonts =
            "Arial\nArial Black\nCalibri\nCambria\nCandara\nComic Sans MS\nConsolas\nConstantia\n" +
            "Corbel\nCourier New\nEbrima\nFranklin Gothic Medium\nGabriola\nGadugi\nGeorgia\n" +
            "Impact\nInk Free\nJavanese Text\nLeelawadee UI\nLucida Console\nLucida Sans Unicode\n" +
            "Malgun Gothic\nMicrosoft Himalaya\nMicrosoft JhengHei\nMicrosoft New Tai Lue\n" +
            "Microsoft PhagsPa\nMicrosoft Sans Serif\nMicrosoft Tai Le\nMicrosoft YaHei\n" +
            "Microsoft Yi Baiti\nMingLiU-ExtB\nMongolian Baiti\nMS Gothic\nMV Boli\nMyanmar Text\n" +
            "Nirmala UI\nPalatino Linotype\nSegoe MDL2 Assets\nSegoe Print\nSegoe Script\n" +
            "Segoe UI\nSegoe UI Emoji\nSegoe UI Historic\nSegoe UI Symbol\nSimSun\nSitka Text\n" +
            "Sylfaen\nSymbol\nTahoma\nTimes New Roman\nTrebuchet MS\nVerdana\nWebdings\nWingdings\n" +
            "Yu Gothic";

        public static readonly (string Name, string Tz, int Offset, string Lang)[] Presets =
        {
            ("美国 纽约", "America/New_York", -300, "en-US"),
            ("美国 洛杉矶", "America/Los_Angeles", -480, "en-US"),
            ("英国 伦敦", "Europe/London", 0, "en-GB"),
            ("德国 柏林", "Europe/Berlin", 60, "de-DE"),
            ("日本 东京", "Asia/Tokyo", 540, "ja-JP"),
            ("新加坡", "Asia/Singapore", 480, "en-SG"),
            ("中国 香港", "Asia/Hong_Kong", 480, "zh-HK"),
            ("澳大利亚 悉尼", "Australia/Sydney", 600, "en-AU"),
        };
    }
}

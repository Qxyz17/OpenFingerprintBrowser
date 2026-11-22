using System;
using System.Text;
using Newtonsoft.Json;

namespace PxUi.Browser
{
    /// <summary>把 profile 编译成注入脚本</summary>
    internal static class Injector
    {
        private static string _template;

        private static string Template
        {
            get
            {
                if (_template != null) return _template;
                _template = LoadTemplate();
                return _template;
            }
        }

        private static string LoadTemplate()
        {
            // 脚本作为嵌入资源随程序分发，避免外部文件被替换
            var asm = typeof(Injector).Assembly;
            using (var stream = asm.GetManifestResourceStream("PxUi.fingerprint.js"))
            {
                if (stream == null)
                {
                    throw new InvalidOperationException("缺少注入脚本资源");
                }
                using (var reader = new System.IO.StreamReader(stream, Encoding.UTF8))
                {
                    return reader.ReadToEnd();
                }
            }
        }

        public static string Build(Profile p)
        {
            var cfg = new
            {
                seed = p.EffectiveSeed,
                timezone = p.Timezone,
                timezoneOffset = p.TimezoneOffset,
                locale = p.Locale,
                language = p.Language,
                locales = p.AcceptLanguages,
                languages = p.AcceptLanguages,
                hardwareConcurrency = p.HardwareConcurrency,
                deviceMemory = p.DeviceMemory,
                platform = p.Platform,
                screenWidth = p.ScreenWidth,
                screenHeight = p.ScreenHeight,
                screenAvailWidth = p.ScreenAvailWidth,
                screenAvailHeight = p.ScreenAvailHeight,
                colorDepth = p.ColorDepth,
                devicePixelRatio = p.DevicePixelRatio,
                maxTouchPoints = p.MaxTouchPoints,
                userAgent = p.UserAgent,
                fonts = p.Fonts,
                fontFallbackWidth = 7.2,
                canvasNoise = p.CanvasNoise,
                canvasBlock = p.CanvasBlock,
                canvasFill = p.CanvasFill,
                audioNoise = p.AudioNoise,
                webglVendor = p.WebglVendor,
                webglRenderer = p.WebglRenderer,
                webrtcMode = p.WebRtcMode,
                emojiStyle = p.EmojiStyle,
                plugins = new[] { "PDF Viewer", "Chrome PDF Viewer", "Chromium PDF Viewer",
                    "Microsoft Edge PDF Viewer", "WebKit built-in PDF" }
            };

            string json = JsonConvert.SerializeObject(cfg, Formatting.None);
            return Template.Replace("__PX_CFG__", json);
        }
    }
}

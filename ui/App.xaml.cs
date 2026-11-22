using System;
using System.IO;
using System.Text;
using System.Windows;

namespace PxUi
{
    public partial class App : Application
    {
        internal static AppConfig Config;
        internal static string LogFilePath;

        protected override void OnStartup(StartupEventArgs e)
        {
            Config = AppConfig.Load();
            LogFilePath = AppConfig.LogPath;
            try { Directory.CreateDirectory(AppConfig.Dir); } catch { }

            base.OnStartup(e);

            var window = new MainWindow();
            MainWindow = window;
            window.Show();
        }

        internal static void Log(string text)
        {
            try
            {
                File.AppendAllText(LogFilePath,
                    DateTime.Now.ToString("HH:mm:ss") + " " + text + Environment.NewLine,
                    new UTF8Encoding(false));
            }
            catch { }
        }
    }
}

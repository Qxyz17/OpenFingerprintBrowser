using System;
using System.Runtime.InteropServices;
using System.Text;

namespace PxUi
{
    /// <summary>proxifier.dll 的 P/Invoke 绑定</summary>
    internal static class Native
    {
        private const string Dll = "proxifier.dll";

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void LogSink(IntPtr text);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern int px_start(string configPath);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void px_stop();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int px_is_running();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int px_last_error(byte[] buffer, int bufferLen);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void px_set_log(LogSink sink);

        private static LogSink _sink;

        internal static void SetLogHandler(LogSink sink)
        {
            _sink = sink;
            px_set_log(sink);
        }

        internal static string LastError()
        {
            var buf = new byte[1024];
            int n = px_last_error(buf, buf.Length);
            return n > 0 ? Encoding.UTF8.GetString(buf, 0, n) : "";
        }
    }
}

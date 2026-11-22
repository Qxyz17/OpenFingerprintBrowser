using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Newtonsoft.Json;

namespace PxUi.Browser
{
    /// <summary>profile 目录的读写，一个 profile 一个 json</summary>
    internal static class ProfileStore
    {
        public static string Root => Path.Combine(AppConfig.Dir, "profiles");

        private static string FileOf(string id) => Path.Combine(Root, id + ".json");

        public static List<Profile> List()
        {
            var result = new List<Profile>();
            try
            {
                Directory.CreateDirectory(Root);
                foreach (var file in Directory.GetFiles(Root, "*.json"))
                {
                    try
                    {
                        result.Add(Profile.FromJson(File.ReadAllText(file, Encoding.UTF8)));
                    }
                    catch
                    {
                    }
                }
            }
            catch
            {
            }
            result.Sort((a, b) => string.Compare(a.Name, b.Name, StringComparison.CurrentCulture));
            return result;
        }

        public static void Save(Profile p)
        {
            Directory.CreateDirectory(Root);
            File.WriteAllText(FileOf(p.Id), p.ToJson(), new UTF8Encoding(false));
        }

        public static void Delete(Profile p)
        {
            try
            {
                string f = FileOf(p.Id);
                if (File.Exists(f)) File.Delete(f);
            }
            catch
            {
            }
        }
    }
}

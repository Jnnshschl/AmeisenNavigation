using System;
using System.Collections.Generic;
using System.Text;

namespace AmeisenNavigation.Tester.Services
{
    /// <summary>
    /// Parses and provides lookups for WoW's md5translate.trs file,
    /// which maps minimap tile paths to hash-based filenames.
    /// </summary>
    public sealed class Md5TranslateTable
    {
        private readonly Dictionary<string, string> _entries;

        private Md5TranslateTable(Dictionary<string, string> entries)
        {
            _entries = entries;
        }

        /// <summary>
        /// Try to look up a hash filename for a given trs key.
        /// </summary>
        public bool TryGetValue(string key, out string? value)
            => _entries.TryGetValue(key, out value);

        /// <summary>
        /// Parse md5translate.trs raw data into a lookup table.
        /// Returns null if the data is empty or invalid.
        /// </summary>
        public static Md5TranslateTable? Parse(byte[] trsData)
        {
            if (trsData == null || trsData.Length == 0)
                return null;

            var dict = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            string content = Encoding.UTF8.GetString(trsData);

            foreach (string line in content.Split('\n', StringSplitOptions.RemoveEmptyEntries))
            {
                string trimmed = line.Trim('\r', ' ');
                if (trimmed.Length == 0) continue;

                int tabIdx = trimmed.IndexOf('\t');
                if (tabIdx < 0) continue;

                string mapKey = trimmed[..tabIdx].Trim();
                string hashValue = trimmed[(tabIdx + 1)..].Trim();

                if (mapKey.Length > 0 && hashValue.Length > 0)
                    dict[mapKey] = hashValue;
            }

            return dict.Count > 0 ? new Md5TranslateTable(dict) : null;
        }
    }
}

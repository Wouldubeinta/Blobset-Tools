namespace PackageIO
{
    using System;
    using System.IO;
    using System.Text;
    using System.Text.RegularExpressions;

    public class Functions
    {
        public static bool CompareBytes(byte[] Arg0, byte[] Arg1)
        {
            return (Conversions.ObjectToHex(Arg0) == Conversions.ObjectToHex(Arg1));
        }

        public static int FileLen(string FilePath)
        {
            return FilePath.Length;
        }

        public static string GetDirectory(string filename)
        {
            return Path.GetDirectoryName(filename);
        }

        public static string GetRatio(long Arg0, long Arg1)
        {
            return (Convert.ToDouble(Arg0 / Arg1)).ToString() + "%";
        }

        public static bool IsNumeric(long Numeric)
        {
            return new Regex(@"^[0-9]+\d").IsMatch(Numeric.ToString());
        }

        public static bool IsValidHex(string Hex)
        {
            return new Regex("^[A-Fa-f0-9]*$", RegexOptions.IgnoreCase).IsMatch(Hex);
        }

        public static bool IsValidUnicode(string Unicode)
        {
            return (Unicode.Length == LenB(Unicode));
        }

        public static int LenB(string ObjStr)
        {
            if (ObjStr.Length == 0)
            {
                return 0;
            }
            return Encoding.Unicode.GetByteCount(ObjStr);
        }

        public static byte[] RemoveAt(byte[] Bytes, int Index)
        {
            return Conversions.HexToByteArray(Conversions.ObjectToHex(Bytes).Remove(Index, 2));
        }

        public static byte[] RemoveByte(byte[] Bytes, byte ByteToRemove)
        {
            return Conversions.HexToByteArray(Conversions.ObjectToHex(Bytes).Replace(ByteToRemove.ToString(), ""));
        }

        public static Array ReverseArray(Array Buffer)
        {
            Array.Reverse(Buffer);
            return Buffer;
        }

        public static string RoundBytes(long ByteCount)
        {
            if (ByteCount < 0x400)
            {
                return (ByteCount.ToString() + " b");
            }
            if ((ByteCount >= 0x400) && (ByteCount < 0x100000))
            {
                return (Convert.ToDouble(ByteCount / 1024.0).ToString() + " kb");
            }
            if ((ByteCount >= 0x100000) && (ByteCount < 0x40000000))
            {
                return (Convert.ToDouble(ByteCount / 1024.0 / 1024.0).ToString() + " mb");
            }
            if ((ByteCount < 0x40000000) || (ByteCount >= 0x10000000000))
            {
                throw new Exception("WTF!");
            }
            return (Convert.ToDouble(ByteCount / 1024.0 / 1024.0 / 1024.0).ToString() + " gb");
        }

        public static string FormatSize(ulong bytes)
        {
            string[] UNITS = new string[] { "B", "KB", "MB", "GB", "TB", "PB", "EB" };

            int c = 0;
            for (c = 0; c < UNITS.Length; c++)
            {
                ulong m = (ulong)1 << ((c + 1) * 10);
                if (bytes < m)
                    break;
            }

            double n = bytes / (double)((ulong)1 << (c * 10));
            return string.Format("{0:0.##} {1}", n, UNITS[c]);
        }

        public static byte[] SwapSex(byte[] Buffer)
        {
            return (byte[])ReverseArray(Buffer);
        }
    }
}


namespace PackageIO.Algorithms
{
    using System.IO;
    using System.Security.Cryptography;

    public class MD5
    {
        private static readonly MD5CryptoServiceProvider MD5Crypto = new();

        public static byte[] Compute(byte[] Bytes)
        {
            return MD5Crypto.ComputeHash(Bytes);
        }

        public static object Compute(MemoryStream File, long Offset, int Length)
        {
            BinaryReader reader = new(File) { BaseStream = { Position = Offset } };
            byte[] buffer = reader.ReadBytes(Length);
            reader.Close();
            return MD5Crypto.ComputeHash(buffer);
        }

        public static byte[] Compute(byte[] Bytes, long Offset, int Length)
        {
            return MD5Crypto.ComputeHash(Bytes, (int)Offset, Length);
        }

        public static byte[] Compute(string File, long Offset, int Length)
        {
            BinaryReader reader = new(new FileStream(File, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite)) { BaseStream = { Position = Offset } };
            byte[] buffer = reader.ReadBytes(Length);
            reader.Close();
            return MD5Crypto.ComputeHash(buffer);
        }
    }
}


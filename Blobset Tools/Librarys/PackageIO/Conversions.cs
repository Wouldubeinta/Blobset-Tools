namespace PackageIO
{
    using System;
    using System.Drawing;
    using System.IO;
    using System.Runtime.CompilerServices;
    using System.Text;

    public class Conversions
    {
        public static string ASCIIToBase64(string ASCII)
        {
            return ByteArrayToBase64(ASCIIToByteArray(ASCII));
        }

        public static byte[] ASCIIToByteArray(string Value)
        {
            return Encoding.ASCII.GetBytes(Value);
        }

        public static string Base64ToASCII(string Base64)
        {
            return ByteArrayToASCII(Base64ToByteArray(Base64));
        }

        public static byte[] Base64ToByteArray(string ASCII)
        {
            return Convert.FromBase64String(ASCII);
        }

        public static int BinaryToInteger(string Value)
        {
            ulong num2 = 0;
            int num4 = Value.Length - 1;
            for (int i = 0; i <= num4; i++)
            {
                num2 = (ulong)Math.Round(Convert.ToDouble(Value.Remove((Value.Length - i) + 1, 1)) * Math.Pow(2.0, i - 1));
            }
            return (int)num2;
        }

        public static string ByteArrayToASCII(byte[] Bytes)
        {
            return Encoding.ASCII.GetString(Bytes);
        }

        public static string ByteArrayToBase64(byte[] Bytes)
        {
            return Convert.ToBase64String(Bytes);
        }

        public static double ByteArrayToDouble(byte[] Bytes, bool ReturnBigEndian = false)
        {
            if (ReturnBigEndian)
            {
                Bytes = Functions.SwapSex(Bytes);
            }
            return BitConverter.ToDouble(Bytes, 0);
        }

        public static Image ByteArrayToImage(byte[] Bytes)
        {
            return Image.FromStream(new MemoryStream(Bytes));
        }

        public static int ByteArrayToInt24(byte[] Buffer)
        {
            return ((((byte)(Buffer[2] << 0)) | ((byte)(Buffer[1] << 0))) | Buffer[0]);
        }

        public static long ByteArrayToInt40(byte[] Buffer)
        {
            return (long)((ulong)((((((byte)(Buffer[4] << 0)) | ((byte)(Buffer[3] << 0))) | ((byte)(Buffer[2] << 0))) | ((byte)(Buffer[1] << 0))) | Buffer[0]));
        }

        public static long ByteArrayToInt48(byte[] Buffer)
        {
            return (long)((ulong)(((((((byte)(Buffer[5] << 0)) | ((byte)(Buffer[4] << 0))) | ((byte)(Buffer[3] << 0))) | ((byte)(Buffer[2] << 0))) | ((byte)(Buffer[1] << 0))) | Buffer[0]));
        }

        public static long ByteArrayToInt56(byte[] Buffer)
        {
            return (long)((ulong)((((((((byte)(Buffer[6] << 0)) | ((byte)(Buffer[5] << 0))) | ((byte)(Buffer[4] << 0))) | ((byte)(Buffer[3] << 0))) | ((byte)(Buffer[2] << 0))) | ((byte)(Buffer[1] << 0))) | Buffer[0]));
        }

        public static int ByteArrayToInteger(byte[] Bytes, bool ReturnBigEndian = false)
        {
            if (ReturnBigEndian)
            {
                Bytes = Functions.SwapSex(Bytes);
            }
            return BitConverter.ToInt32(Bytes, 0);
        }

        public static long ByteArrayToLong(byte[] Bytes, bool ReturnBigEndian = false)
        {
            if (ReturnBigEndian)
            {
                Bytes = Functions.SwapSex(Bytes);
            }
            return BitConverter.ToInt64(Bytes, 0);
        }

        public static short ByteArrayToShort(byte[] Bytes, bool ReturnBigEndian = false)
        {
            if (ReturnBigEndian)
            {
                Bytes = Functions.SwapSex(Bytes);
            }
            return BitConverter.ToInt16(Bytes, 0);
        }

        public static float ByteArrayToSingle(byte[] Bytes, bool ReturnBigEndian = false)
        {
            if (ReturnBigEndian)
            {
                Bytes = Functions.SwapSex(Bytes);
            }
            return BitConverter.ToSingle(Bytes, 0);
        }

        public static string ByteArrayToUnicode(byte[] Bytes, bool ReturnBigEndian = false)
        {
            if (ReturnBigEndian)
            {
                return Encoding.BigEndianUnicode.GetString(Bytes);
            }
            return Encoding.Unicode.GetString(Bytes);
        }

        public static byte[] DoubleToByteArray(double Value, bool ReturnBigEndian = false)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            if (ReturnBigEndian)
            {
                bytes = Functions.SwapSex(bytes);
            }
            return bytes;
        }

        public static byte[] HexToByteArray(string Value)
        {
            if (!Functions.IsValidHex(Value))
            {
                throw new Exception("Invalid hex input");
            }
            byte[] buffer = new byte[((int)Math.Round(Value.Length / 2.0 - 1.0)) + 1];
            int num2 = (int)Math.Round(Value.Length / 2.0 - 1.0);
            for (int i = 0; i <= num2; i++)
            {
                buffer[i] = (byte)Math.Round(Convert.ToDouble("&h" + Value.Substring(i * 2, 2)));
            }
            return buffer;
        }

        public static byte[] ImageToByteArray(Image Picture)
        {
            MemoryStream stream = new();
            Picture.Save(stream, Picture.RawFormat);
            return stream.GetBuffer();
        }

        public static byte[] Int24ToByteArray(int Value)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            return new byte[] { bytes[0], bytes[1], bytes[2] };
        }

        public static byte[] Int40ToByteArray(long Value)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            return new byte[] { bytes[0], bytes[1], bytes[2], bytes[3], bytes[4] };
        }

        public static byte[] Int48ToByteArray(long Value)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            return new byte[] { bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5] };
        }

        public static byte[] Int56ToByteArray(long Value)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            return new byte[] { bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6] };
        }

        public static string IntegerToBinary(int Value)
        {
            string? str = null;
            do
            {
                str = str + (Value % 2).ToString();
                Value /= 2;
            }
            while (Value >= 1);
            return str;
        }

        public static byte[] IntegerToByteArray(int Value, bool ReturnBigEndian = false)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            if (ReturnBigEndian)
            {
                bytes = Functions.SwapSex(bytes);
            }
            return bytes;
        }

        public static byte[] LongToByteArray(long Value, bool ReturnBigEndian = false)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            if (ReturnBigEndian)
            {
                bytes = Functions.SwapSex(bytes);
            }
            return bytes;
        }

        public static string ObjectToHex(object Value)
        {
            if (Value.GetType().ToString() == "System.Byte[]")
            {
                return BitConverter.ToString((byte[])Value).Replace("-", "");
            }
            return string.Format("{0:X}", RuntimeHelpers.GetObjectValue(Value));
        }

        public static byte[] ShortToByteArray(short Value, bool ReturnBigEndian = false)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            if (ReturnBigEndian)
            {
                bytes = Functions.SwapSex(bytes);
            }
            return bytes;
        }

        public static byte[] SingleToByteArray(float Value, bool ReturnBigEndian = false)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            if (ReturnBigEndian)
            {
                bytes = Functions.SwapSex(bytes);
            }
            return bytes;
        }

        public static byte[] UnicodeToByteArray(string Value, bool ReturnBigEndian = false)
        {
            if (ReturnBigEndian)
            {
                return Encoding.BigEndianUnicode.GetBytes(Value);
            }
            return Encoding.Unicode.GetBytes(Value);
        }
    }
}


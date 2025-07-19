namespace PackageIO
{
    using System;
    using System.Collections.Generic;
    using System.IO;
    using System.Text;

    public class Reader
    {
        public Endian CurrentEndian;
        public long LastPosition;
        private readonly Stream OpenStream;
        private readonly BinaryReader br;

        public Reader(FileStream Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            OpenStream = null;
            br = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0;
            OpenStream = Package;
            br = new BinaryReader(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public Reader(string Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            OpenStream = null;
            br = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0;
            OpenStream = new FileStream(Package, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite);
            br = new BinaryReader(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public Reader(byte[] Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            OpenStream = null;
            br = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0;
            OpenStream = new MemoryStream(Package);
            br = new BinaryReader(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public Reader(MemoryStream Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            OpenStream = null;
            br = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0;
            OpenStream = Package;
            br = new BinaryReader(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public Reader(Stream Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            OpenStream = null;
            br = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0;
            OpenStream = Package;
            br = new BinaryReader(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public void Close()
        {
            Flush();
            br.Close();
            OpenStream.Close();
        }

        public void Flush()
        {
            OpenStream.Flush();
        }

        public byte PeekByte()
        {
            byte num = ReadByte();
            Position = LastPosition;
            return num;
        }

        public byte[] PeekBytes(int Length)
        {
            return PeekBytes(Length, CurrentEndian);
        }

        public byte[] PeekBytes(int Length, Endian EndianType)
        {
            byte[] buffer = ReadBytes(Length, CurrentEndian);
            Position = LastPosition;
            return buffer;
        }

        public char PeekChar()
        {
            char ch = ReadChar();
            Position = LastPosition;
            return ch;
        }

        public char[] PeekChars(int Length)
        {
            char[] chArray = ReadChars(Length);
            Position = LastPosition;
            return chArray;
        }

        public string PeekHex(int Length)
        {
            return PeekHex(Length, CurrentEndian);
        }

        public string PeekHex(int Length, Endian EndianType)
        {
            string str = ReadHexString(Length, EndianType);
            Position = LastPosition;
            return str;
        }

        public string PeekString(int Length)
        {
            string str = ReadString(Length);
            Position = LastPosition;
            return str;
        }

        public string PeekUnicode(int Length)
        {
            return PeekUnicode(Length, CurrentEndian);
        }

        public string PeekUnicode(int Length, Endian EndianType)
        {
            string str = ReadUnicodeString(Length, EndianType);
            Position = LastPosition;
            return str;
        }

        public int Read()
        {
            LastPosition = OpenStream.Position;
            return br.Read();
        }

        public int Read(byte[] Value, int Length)
        {
            return Read(Value, 0, Length);
        }

        public int Read(char[] Value, int Length)
        {
            return Read(Value, 0, Length);
        }

        public int Read(byte[] Value, int Index, int Length)
        {
            LastPosition = OpenStream.Position;
            return br.Read(Value, Index, Length);
        }

        public int Read(char[] Value, int Index, int Length)
        {
            LastPosition = OpenStream.Position;
            return br.Read(Value, Index, Length);
        }

        public bool ReadBoolean()
        {
            LastPosition = OpenStream.Position;
            return br.ReadBoolean();
        }

        public byte ReadByte()
        {
            LastPosition = OpenStream.Position;
            return br.ReadByte();
        }

        public byte[] ReadBytes(int Length)
        {
            return ReadBytes(Length, CurrentEndian);
        }

        public byte[] ReadBytes(int Length, Endian EndianType)
        {
            LastPosition = OpenStream.Position;
            byte[] buffer = br.ReadBytes(Length);
            if (EndianType == Endian.Big)
            {
                Functions.SwapSex(buffer);
            }
            return buffer;
        }

        public byte[] ReadBytes(int Index, int Length)
        {
            return ReadBytes(Index, Length, CurrentEndian);
        }

        public byte[] ReadBytes(int Index, int Length, Endian EndianType)
        {
            LastPosition = OpenStream.Position;
            byte[] destinationArray = new byte[(Length - 1) + 1];
            Array.Copy(ReadBytes(Length, EndianType), Index, destinationArray, 0, Length);
            return destinationArray;
        }

        public char ReadChar()
        {
            LastPosition = OpenStream.Position;
            return br.ReadChar();
        }

        public char[] ReadChars(int Length)
        {
            LastPosition = OpenStream.Position;
            return br.ReadChars(Length);
        }

        public double ReadDouble()
        {
            return ReadDouble(CurrentEndian);
        }

        public double ReadDouble(Endian EndianType)
        {
            return BitConverter.ToDouble(ReadBytes(8, EndianType), 0);
        }

        public string ReadHexString(int Length)
        {
            return ReadHexString(Length, CurrentEndian);
        }

        public string ReadHexString(int Length, Endian EndianType)
        {
            return Conversions.ObjectToHex(ReadBytes(Length, EndianType));
        }

        public short ReadInt16()
        {
            return ReadInt16(CurrentEndian);
        }

        public short ReadInt16(Endian EndianType)
        {
            return BitConverter.ToInt16(ReadBytes(2, EndianType), 0);
        }

        public int ReadInt24()
        {
            return ReadInt24(CurrentEndian);
        }

        public int ReadInt24(Endian EndianType)
        {
            return Conversions.ByteArrayToInt24(ReadBytes(3, EndianType));
        }

        public int ReadInt32()
        {
            return ReadInt32(CurrentEndian);
        }

        public int ReadInt32(Endian EndianType)
        {
            return BitConverter.ToInt32(ReadBytes(4, EndianType), 0);
        }

        public long ReadInt40()
        {
            return ReadInt40(CurrentEndian);
        }

        public long ReadInt40(Endian EndianType)
        {
            return Conversions.ByteArrayToInt40(ReadBytes(5, EndianType));
        }

        public long ReadInt48()
        {
            return ReadInt48(CurrentEndian);
        }

        public long ReadInt48(Endian EndianType)
        {
            return Conversions.ByteArrayToInt48(ReadBytes(6, EndianType));
        }

        public long ReadInt56()
        {
            return ReadInt56(CurrentEndian);
        }

        public long ReadInt56(Endian EndianType)
        {
            return Conversions.ByteArrayToInt56(ReadBytes(7, EndianType));
        }

        public long ReadInt64()
        {
            return ReadInt64(CurrentEndian);
        }

        public long ReadInt64(Endian EndianType)
        {
            return BitConverter.ToInt64(ReadBytes(8, EndianType), 0);
        }

        public sbyte ReadInt8()
        {
            LastPosition = OpenStream.Position;
            return br.ReadSByte();
        }

        public float ReadSingle()
        {
            return ReadSingle(CurrentEndian);
        }

        public float ReadSingle(Endian EndianType)
        {
            return BitConverter.ToSingle(ReadBytes(4, EndianType), 0);
        }

        public string ReadString(int Length)
        {
            return ReadString(Length, CurrentEndian);
        }

        public string ReadString(int Length, Endian EndianType)
        {
            return Encoding.ASCII.GetString(ReadBytes(Length, EndianType));
        }

        public string ReadNullTerminatedString()
        {
            List<byte> strBytes = [];
            byte chr;
            while ((chr = br.ReadByte()) != 0)
                strBytes.Add(chr);
            return Encoding.ASCII.GetString(strBytes.ToArray());
        }

        public ushort ReadUInt16()
        {
            return ReadUInt16(CurrentEndian);
        }

        public ushort ReadUInt16(Endian EndianType)
        {
            return BitConverter.ToUInt16(ReadBytes(2, EndianType), 0);
        }

        public uint ReadUInt24()
        {
            return ReadUInt24(CurrentEndian);
        }

        public uint ReadUInt24(Endian EndianType)
        {
            return (uint)Conversions.ByteArrayToInt24(ReadBytes(3, EndianType));
        }

        public uint ReadUInt32()
        {
            return ReadUInt32(CurrentEndian);
        }

        public uint ReadUInt32(Endian EndianType)
        {
            return BitConverter.ToUInt32(ReadBytes(4, EndianType), 0);
        }

        public ulong ReadUInt40()
        {
            return ReadUInt40(CurrentEndian);
        }

        public ulong ReadUInt40(Endian EndianType)
        {
            return (ulong)Conversions.ByteArrayToInt40(ReadBytes(5, EndianType));
        }

        public ulong ReadUInt48()
        {
            return ReadUInt48(CurrentEndian);
        }

        public ulong ReadUInt48(Endian EndianType)
        {
            return (ulong)Conversions.ByteArrayToInt48(ReadBytes(6, EndianType));
        }

        public ulong ReadUInt56()
        {
            return ReadUInt56(CurrentEndian);
        }

        public ulong ReadUInt56(Endian EndianType)
        {
            return (ulong)Conversions.ByteArrayToInt56(ReadBytes(7, EndianType));
        }

        public ulong ReadUInt64()
        {
            return ReadUInt64(CurrentEndian);
        }

        public ulong ReadUInt64(Endian EndianType)
        {
            return BitConverter.ToUInt64(ReadBytes(8, EndianType), 0);
        }

        public string ReadUnicodeString(int Length)
        {
            return ReadUnicodeString(Length, CurrentEndian);
        }

        public string ReadUnicodeString(int Length, Endian EndianType)
        {
            byte[] bytes = ReadBytes(Length * 2, Endian.Little);
            if (EndianType == Endian.Big)
            {
                return Encoding.BigEndianUnicode.GetString(bytes);
            }
            return Encoding.Unicode.GetString(bytes);
        }

        public List<int> Search(byte[] Criteria, long Position = 0, bool ReturnOnFirst = true)
        {
            List<int> list = [];
            int num2 = (int)(Length - 1L);
            for (int i = (int)Position; i <= num2; i++)
            {
                if ((i + Criteria.Length) > (Length - 1L))
                {
                    return list;
                }
                this.Position = i;
                byte[] buffer = ReadBytes(Criteria.Length);
                if (Functions.CompareBytes(Criteria, buffer) || Functions.CompareBytes(Criteria, Functions.SwapSex(buffer)))
                {
                    list.Add(i);
                    if (ReturnOnFirst)
                    {
                        return list;
                    }
                }
            }
            return list;
        }

        public List<int> SearchNumeric(long Criteria, IntType IntType, bool UnSigned = false, long Position = 0, bool ReturnOnFirst = true)
        {
            byte[] buffer;
            switch (IntType)
            {
                case IntType.Int8:
                    buffer = new byte[] { (byte)((sbyte)Criteria) };
                    break;

                case IntType.Int16:
                    if (!UnSigned)
                        buffer = Conversions.ShortToByteArray((short)Criteria, false);
                    else
                        buffer = Conversions.ShortToByteArray((short)((ushort)Criteria), false);
                    break;

                case IntType.Int24:
                    if (!UnSigned)
                        buffer = Conversions.Int24ToByteArray((int)Criteria);
                    else
                        buffer = Conversions.Int24ToByteArray((int)((uint)Criteria));
                    break;

                case IntType.Int32:
                    if (!UnSigned)
                        buffer = Conversions.IntegerToByteArray((int)Criteria, false);
                    else
                        buffer = Conversions.IntegerToByteArray((int)((uint)Criteria), false);
                    break;

                case IntType.Int48:
                    if (!UnSigned)
                        buffer = Conversions.Int48ToByteArray(Criteria);
                    else
                        buffer = Conversions.Int48ToByteArray((long)((ulong)Criteria));
                    break;

                case IntType.Int56:
                    if (!UnSigned)
                        buffer = Conversions.Int56ToByteArray(Criteria);
                    else
                        buffer = Conversions.Int56ToByteArray((long)((ulong)Criteria));
                    break;

                case IntType.Int64:
                    if (!UnSigned)
                        buffer = Conversions.LongToByteArray(Criteria, false);
                    else
                        buffer = Conversions.LongToByteArray((long)((ulong)Criteria), false);
                    break;

                case IntType.Single:
                    buffer = Conversions.SingleToByteArray(Criteria, false);
                    break;

                default:
                    buffer = Conversions.DoubleToByteArray(Criteria, false);
                    break;
            }
            return Search(buffer, Position, ReturnOnFirst);
        }

        public List<int> SearchString(string Criteria, StringType StringType, long Position = 0L, bool ReturnOnFirst = true)
        {
            byte[] bytes;
            switch (StringType)
            {
                case StringType.ASCII:
                    bytes = Encoding.ASCII.GetBytes(Criteria);
                    break;

                case StringType.Unicode:
                    bytes = Encoding.Unicode.GetBytes(Criteria);
                    break;

                case StringType.BigEndianUnicode:
                    bytes = Encoding.BigEndianUnicode.GetBytes(Criteria);
                    break;

                default:
                    bytes = Conversions.HexToByteArray(Criteria);
                    break;
            }
            return Search(bytes, Position, ReturnOnFirst);
        }

        public void Seek(long Offset)
        {
            Seek(Offset, SeekOrigin.Begin);
        }

        public void Seek(long Offset, SeekOrigin SeekOrigin)
        {
            LastPosition = OpenStream.Position;
            br.BaseStream.Seek(Offset, SeekOrigin);
        }

        public void SwapEndian()
        {
            if (CurrentEndian == Endian.Little)
            {
                CurrentEndian = Endian.Big;
            }
            else if (CurrentEndian == Endian.Big)
            {
                CurrentEndian = Endian.Little;
            }
        }

        public long Length
        {
            get
            {
                return OpenStream.Length;
            }
        }

        public long Position
        {
            get
            {
                return OpenStream.Position;
            }
            set
            {
                LastPosition = OpenStream.Position;
                OpenStream.Position = value;
            }
        }

        public Stream BaseStream
        {
            get
            {
                return OpenStream;
            }
        }

        public enum IntType
        {
            Int8,
            Int16,
            Int24,
            Int32,
            Int48,
            Int56,
            Int64,
            Single,
            Double
        }

        public enum StringType
        {
            ASCII,
            Unicode,
            BigEndianUnicode,
            Hexadecimal
        }
    }
}


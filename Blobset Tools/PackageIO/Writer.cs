namespace PackageIO
{
    using System;
    using System.IO;
    using System.Text;

    public class Writer
    {
        public Endian CurrentEndian;
        public long LastPosition;
        private readonly Stream OpenStream;
        private readonly BinaryWriter bw;

        public Writer(FileStream Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            OpenStream = null;
            bw = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0;
            OpenStream = Package;
            bw = new BinaryWriter(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public Writer(string Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            OpenStream = null;
            bw = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0;
            OpenStream = new FileStream(Package, FileMode.OpenOrCreate, FileAccess.Write, FileShare.Write);
            bw = new BinaryWriter(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public Writer(byte[] Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            OpenStream = null;
            bw = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0;
            OpenStream = new MemoryStream(Package);
            bw = new BinaryWriter(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public Writer(MemoryStream Package, Endian EndianType = (Endian)1, long Position = 0L)
        {
            OpenStream = null;
            bw = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0L;
            OpenStream = Package;
            bw = new BinaryWriter(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public Writer(Stream Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            OpenStream = null;
            bw = null;
            CurrentEndian = Endian.Little;
            LastPosition = 0;
            OpenStream = Package;
            bw = new BinaryWriter(OpenStream);
            this.Position = Position;
            CurrentEndian = EndianType;
        }

        public Writer(string Package, FileMode Mode, Endian EndianType = (Endian)1, long Position = 0)
            : this(new FileStream(Package, Mode, FileAccess.ReadWrite, FileShare.ReadWrite), EndianType, Position)
        {
        }

        public void Close()
        {
            Flush();
            bw.Close();
            OpenStream.Close();
        }

        public void Flush()
        {
            bw.Flush();
            OpenStream.Flush();
        }

        public void Seek(long Offset)
        {
            Seek(Offset, SeekOrigin.Begin);
        }

        public void Seek(long Offset, SeekOrigin SeekOrigin)
        {
            LastPosition = OpenStream.Position;
            bw.BaseStream.Seek(Offset, SeekOrigin);
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

        public void Write(byte[] Buffer)
        {
            Write(Buffer, CurrentEndian);
        }

        public void Write(byte[] Buffer, Endian EndianType)
        {
            LastPosition = OpenStream.Position;
            if (EndianType == Endian.Big)
            {
                Functions.SwapSex(Buffer);
            }
            bw.Write(Buffer, 0, Buffer.Length);
        }

        public void Write(byte[] Buffer, int Length)
        {
            Write(Buffer, Length, CurrentEndian);
        }

        public void Write(byte[] Buffer, int Length, Endian EndianType)
        {
            LastPosition = OpenStream.Position;
            if (EndianType == Endian.Big)
            {
                Functions.SwapSex(Buffer);
            }
            bw.Write(Buffer, 0, Length);
        }

        public void Write(byte[] Buffer, int Index, int Length)
        {
            Write(Buffer, Index, Length, CurrentEndian);
        }

        public void Write(byte[] Buffer, int Index, int Length, Endian EndianType)
        {
            LastPosition = OpenStream.Position;
            if (EndianType == Endian.Big)
            {
                Functions.SwapSex(Buffer);
            }
            bw.Write(Buffer, Index, Length);
        }

        public void WriteBool(bool Value)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            Write(bytes, 0, 1);
        }

        public void WriteChar(char Value)
        {
            LastPosition = OpenStream.Position;
            bw.Write(Value);
        }

        public void WriteChars(char[] Value)
        {
            foreach (char ch in Value)
            {
                WriteChar(ch);
            }
        }

        public void WriteDouble(double Value)
        {
            WriteDouble(Value, CurrentEndian);
        }

        public void WriteDouble(double Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            Write(bytes, 0, 8, EndianType);
        }

        public void WriteHexString(string Value)
        {
            byte[] buffer = Conversions.HexToByteArray(Value);
            Write(buffer, 0, buffer.Length);
        }

        public void WriteHexString(string Value, int Length)
        {
            WriteHexString(Value, Length, CurrentEndian);
        }

        public void WriteHexString(string Value, int Length, Endian EndianType)
        {
            Write(Conversions.HexToByteArray(Value), Length, EndianType);
        }

        public void WriteInt16(short Value)
        {
            WriteInt16(Value, CurrentEndian);
        }

        public void WriteInt16(short Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            Write(bytes, 0, 2, EndianType);
        }

        public void WriteInt24(int Value)
        {
            WriteInt24(Value, CurrentEndian);
        }

        public void WriteInt24(int Value, Endian EndianType)
        {
            Write(Conversions.Int24ToByteArray(Value), 0, 3, EndianType);
        }

        public void WriteInt32(int Value)
        {
            WriteInt32(Value, CurrentEndian);
        }

        public void WriteInt32(int Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            Write(bytes, 0, 4, EndianType);
        }

        public void WriteInt40(long Value)
        {
            WriteInt40(Value, CurrentEndian);
        }

        public void WriteInt40(long Value, Endian EndianType)
        {
            Write(Conversions.Int40ToByteArray(Value), 0, 5, EndianType);
        }

        public void WriteInt48(long Value)
        {
            WriteInt48(Value, CurrentEndian);
        }

        public void WriteInt48(long Value, Endian EndianType)
        {
            Write(Conversions.Int48ToByteArray(Value), 0, 6, EndianType);
        }

        public void WriteInt56(long Value)
        {
            WriteInt56(Value, CurrentEndian);
        }

        public void WriteInt56(long Value, Endian EndianType)
        {
            Write(Conversions.Int56ToByteArray(Value), 0, 7, EndianType);
        }

        public void WriteInt64(long Value)
        {
            WriteInt64(Value, CurrentEndian);
        }

        public void WriteInt64(long Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            Write(bytes, 0, 8, EndianType);
        }

        public void WriteInt8(sbyte Value)
        {
            WriteInt8(Value, CurrentEndian);
        }

        public void WriteInt8(sbyte Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes((int)Value);
            Write(bytes, 0, 1, EndianType);
        }

        public void WriteSingle(float Value)
        {
            WriteSingle(Value, CurrentEndian);
        }

        public void WriteSingle(float Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            Write(bytes, 0, 4, EndianType);
        }

        public void WriteString(string Value)
        {
            WriteString(Value, 0, Value.Length);
        }

        public void WriteString(string Value, int Length)
        {
            WriteString(Value, 0, Length);
        }

        public void WriteString(string Value, int StartIndex, int Length)
        {
            byte[] bytes = Encoding.ASCII.GetBytes(Value);
            Write(bytes, StartIndex, Length);
        }

        public void WriteUInt8(byte Value)
        {
            WriteUInt8(Value, CurrentEndian);
        }

        public void WriteUInt8(byte Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes((ushort)Value);
            Write(bytes, 0, 1, EndianType);
        }

        public void WriteUInt16(ushort Value)
        {
            WriteUInt16(Value, CurrentEndian);
        }

        public void WriteUInt16(ushort Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            Write(bytes, 0, 2, EndianType);
        }

        public void WriteUInt24(uint Value)
        {
            WriteUInt24(Value, CurrentEndian);
        }

        public void WriteUInt24(uint Value, Endian EndianType)
        {
            Write(Conversions.Int24ToByteArray((int)Value), 0, 3, EndianType);
        }

        public void WriteUInt32(uint Value)
        {
            WriteUInt32(Value, CurrentEndian);
        }

        public void WriteUInt32(uint Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            Write(bytes, 0, 4, EndianType);
        }

        public void WriteUInt40(ulong Value)
        {
            WriteUInt40(Value, CurrentEndian);
        }

        public void WriteUInt40(ulong Value, Endian EndianType)
        {
            Write(Conversions.Int40ToByteArray((long)Value), 0, 5, EndianType);
        }

        public void WriteUInt48(ulong Value)
        {
            WriteUInt48(Value, CurrentEndian);
        }

        public void WriteUInt48(ulong Value, Endian EndianType)
        {
            Write(Conversions.Int48ToByteArray((long)Value), 0, 6, EndianType);
        }

        public void WriteUInt56(ulong Value)
        {
            WriteUInt56(Value, CurrentEndian);
        }

        public void WriteUInt56(ulong Value, Endian EndianType)
        {
            Write(Conversions.Int56ToByteArray((long)Value), 0, 7, EndianType);
        }

        public void WriteUInt64(ulong Value)
        {
            WriteUInt64(Value, CurrentEndian);
        }

        public void WriteUInt64(ulong Value, Endian EndianType)
        {
            byte[] bytes = BitConverter.GetBytes(Value);
            Write(bytes, 0, 8, EndianType);
        }

        public void WriteUnicodeString(string Value)
        {
            char[] chars = Value.ToCharArray();
            if (CurrentEndian == Endian.Big)
            {
                Write(Encoding.BigEndianUnicode.GetBytes(chars), Endian.Little);
            }
            else
            {
                Write(Encoding.Unicode.GetBytes(chars), Endian.Little);
            }
        }

        public void WriteUnicodeString(string Value, int Length)
        {
            WriteUnicodeString(Value, 0, Length, CurrentEndian);
        }

        public void WriteUnicodeString(string Value, int StartIndex, int Length)
        {
            WriteUnicodeString(Value, StartIndex, Length, CurrentEndian);
        }

        public void WriteUnicodeString(string Value, int StartIndex, int Length, Endian EndianType)
        {
            char[] chars = Value.ToCharArray();
            if (EndianType == Endian.Big)
            {
                Write(Encoding.BigEndianUnicode.GetBytes(chars), StartIndex, Length, Endian.Little);
            }
            else
            {
                Write(Encoding.Unicode.GetBytes(chars), StartIndex, Length, Endian.Little);
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
    }
}


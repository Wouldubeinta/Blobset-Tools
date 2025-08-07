namespace PackageIO
{
    using System.IO;
    using System.Runtime.InteropServices;

    public class IO
    {
        private PackageLayout Package;
        public Reader? Reader;
        public Writer? Writer;

        public IO(FileStream Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            Reader = null;
            Writer = null;
            this.Package.Package = Package;
            this.Package.EndianType = EndianType;
            this.Package.Position = Position;
            Set(this.Package);
        }

        public IO(MemoryStream Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            Reader = null;
            Writer = null;
            this.Package.Package = Package;
            this.Package.EndianType = EndianType;
            this.Package.Position = Position;
            Set(this.Package);
        }

        public IO(byte[] Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            Reader = null;
            Writer = null;
            this.Package.Package = new MemoryStream(Package);
            this.Package.EndianType = EndianType;
            this.Package.Position = Position;
            Set(this.Package);
        }

        public IO(Stream Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            Reader = null;
            Writer = null;
            this.Package.Package = Package;
            this.Package.EndianType = EndianType;
            this.Package.Position = Position;
            Set(this.Package);
        }

        public IO(string Package, Endian EndianType = (Endian)1, long Position = 0)
        {
            Reader = null;
            Writer = null;
            this.Package.Package = new FileStream(Package, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite);
            this.Package.EndianType = EndianType;
            this.Package.Position = Position;
            Set(this.Package);
        }

        public void Close()
        {
            Reader = null;
            Writer = null;
            Package.Package.Close();
            Package = new PackageLayout();
            Package.Package = Stream;
        }

        public long LastPosition(IOType Type)
        {
            if (Type == IOType.Writer)
            {
                return Writer.LastPosition;
            }
            return Reader.LastPosition;
        }

        private void Set(PackageLayout Value)
        {
            Reader = new Reader(Value.Package, Value.EndianType, Value.Position);
            Writer = new Writer(Value.Package, Value.EndianType, Value.Position);
        }

        public void SwapEndian()
        {
            if (CurrentEndian == Endian.Little)
            {
                CurrentEndian = Endian.Big;
            }
            else
            {
                CurrentEndian = Endian.Little;
            }
            Reader.CurrentEndian = CurrentEndian;
            Writer.CurrentEndian = CurrentEndian;
        }

        public Endian CurrentEndian
        {
            get
            {
                return Package.EndianType;
            }
            set
            {
                Package.EndianType = value;
                Reader.CurrentEndian = value;
                Writer.CurrentEndian = value;
            }
        }

        public long Length
        {
            get
            {
                return Package.Package.Length;
            }
        }

        public long Position
        {
            get
            {
                return Package.Position;
            }
            set
            {
                Package.Position = value;
                Reader.Position = value;
                Writer.Position = value;
            }
        }

        public Stream Stream
        {
            get
            {
                return Package.Package;
            }
            set
            {
                PackageLayout layout = new();
                if (value.Length <= Package.Position)
                {
                    layout.Position = Package.Position;
                }
                else
                {
                    layout.Position = 0;
                }
                Close();
                Package = layout;
                Set(Package);
            }
        }

        public enum IOType
        {
            Writer,
            Reader
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PackageLayout
        {
            public Stream Package;
            public Endian EndianType;
            public long Position;
        }
    }
}


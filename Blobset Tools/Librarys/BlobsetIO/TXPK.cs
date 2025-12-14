using Blobset_Tools;
using PackageIO;
using System.Text;

namespace BlobsetIO
{
    /// <summary>
    /// DDS TXPK Class.
    /// </summary>
    /// <remarks>
    ///   Blobset Tools. Written by Wouldubeinta
    ///   Copyright (C) 2025 Wouldy Mods.
    ///   
    ///   This program is free software; you can redistribute it and/or
    ///   modify it under the terms of the GNU General Public License
    ///   as published by the Free Software Foundation; either version 3
    ///   of the License, or (at your option) any later version.
    ///   
    ///   This program is distributed in the hope that it will be useful,
    ///   but WITHOUT ANY WARRANTY; without even the implied warranty of
    ///   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    ///   GNU General Public License for more details.
    ///   
    ///   You should have received a copy of the GNU General Public License
    ///   along with this program; if not, write to the Free Software
    ///   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
    /// 
    ///   The author may be contacted at:
    ///   Discord: Wouldubeinta
    /// </remarks>
    /// <history>
    /// [Wouldubeinta]	   01/06/2025	Created
    /// </history>
    public class TXPK
    {
        #region Fields
        private uint magic = 0; // Should be  - KPXT
        private uint filesCount = 0;
        private Entry[]? entries;
        #endregion

        public TXPK()
        {
            entries = null;
        }

        public class Entry
        {
            public uint DDSPathOffset = 0;
            public uint DDSPathSize = 0;
            public uint DDSDataOffset = 0;
            public uint DDSDataSize = 0;

            public uint DDSWidth = 0;
            public uint DDSHeight = 0;
            public uint DDSImageType = 1;
            public uint DDSFilePathOffset = 20; // SizeOf uint(DDSWidth + DDSHeight + DDSMipmapCount + DDSFilePathOffset + ConsoleDDSHeaderOffset) Always 0x14.
            public uint ConsoleDDSHeaderOffset = 0; //  On PS3 or Xbox 360 - ConsoleDDSHeaderOffset, On PC - DDSDataSize.
            public string DDSFilePath = string.Empty;

            // ** For PS3 and Xbox 360 only **
            public uint BufferSize = 0;
            public byte DDSType = 0; // DXT1 No Alpha - 136, DXT5 - 168
            public byte DDSMipMaps = 0;
            public uint Unknown1 = 33554432; // Always - 33554432
            public ushort Unknown2 = 0; // DXT1 No Alpha - 43748
            public ushort DDSWidth2 = 0;
            public ushort DDSHeight2 = 0;
            public ushort DDSImageType2 = 0;
            public ushort Unknown3 = 0; // ?
            public ushort Unknown4 = 0; // ?
            public ushort Unknown5 = 0; // only has a value in DXT5 textures, not sure what it's used for.
            public uint Reserved = 0; // ?
        }

        #region Properties
        public uint Magic
        {
            get { return magic; }
            set { magic = value; }
        }

        public uint FilesCount
        {
            get { return filesCount; }
            set { filesCount = value; }
        }

        public Entry[]? Entries
        {
            get { return entries; }
            set { entries = value; }
        }
        #endregion

        #region "Deserialize"
        /// <summary>
        /// Deserialize TXPK stream
        /// </summary>
        /// <param name="input">TXPK input stream</param>
        public void Deserialize(Reader input)
        {
            Endian endian = Endian.Little;

            if (Global.isBigendian)
                endian = Endian.Big;

            input.CurrentEndian = endian;

            Magic = input.ReadUInt32();

            if (Magic != 1415073867) // KPXT
            {
                MessageBox.Show("This isn't a TXPK File", "Wrong Magic", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            FilesCount = input.ReadUInt32();

            Entries = new Entry[FilesCount];

            for (int i = 0; i < FilesCount; i++)
            {
                Entries[i] = new Entry();
                Entries[i].DDSPathOffset = input.ReadUInt32();
                Entries[i].DDSPathSize = input.ReadUInt32();
                Entries[i].DDSDataOffset = input.ReadUInt32();
                Entries[i].DDSDataSize = input.ReadUInt32();
            }

            for (int i = 0; i < FilesCount; i++)
            {
                input.Position = Entries[i].DDSPathOffset;
                Entries[i].DDSWidth = input.ReadUInt32();
                Entries[i].DDSHeight = input.ReadUInt32();
                Entries[i].DDSImageType = input.ReadUInt32();
                Entries[i].DDSFilePathOffset = input.ReadUInt32();
                Entries[i].ConsoleDDSHeaderOffset = input.ReadUInt32();
                Entries[i].DDSFilePath = input.ReadNullTerminatedString();

                if (Global.isBigendian)
                {
                    int bufferSize = Convert.ToInt32(Entries[i].ConsoleDDSHeaderOffset - Entries[i].DDSFilePathOffset - (Entries[i].DDSFilePath.Length + 1));
                    input.ReadBytes(bufferSize);
                    Entries[i].BufferSize = (uint)bufferSize;
                    Entries[i].DDSType = input.ReadByte();
                    Entries[i].DDSMipMaps = input.ReadByte();
                    Entries[i].Unknown1 = input.ReadUInt32();
                    Entries[i].Unknown2 = input.ReadUInt16();
                    Entries[i].DDSWidth2 = input.ReadUInt16();
                    Entries[i].DDSHeight2 = input.ReadUInt16();
                    Entries[i].DDSImageType2 = input.ReadUInt16();
                    Entries[i].Unknown3 = input.ReadUInt16();
                    Entries[i].Unknown4 = input.ReadUInt16();
                    Entries[i].Unknown5 = input.ReadUInt16();
                    Entries[i].Reserved = input.ReadUInt32();
                }
            }
        }
        #endregion

        #region "Serialize"
        /// <summary>
        /// Serialize TXPK stream
        /// </summary>
        /// <param name="writer">TXPK output stream</param>
        public void Serialize(FileStream writer)
        {
            writer.Position = 0;

            if (Global.isBigendian)
            {
                writer.Write(BitConverter.GetBytes(1263556692), 0, 4); // TXPK
                writer.Write(Functions.SwapSex(BitConverter.GetBytes(FilesCount)), 0, 4);

                for (int i = 0; i < FilesCount; i++)
                {
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSPathOffset)), 0, 4);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSPathSize)), 0, 4);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSDataOffset)), 0, 4);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSDataSize)), 0, 4);
                }

                for (int i = 0; i < FilesCount; i++)
                {
                    Entries[i].DDSPathOffset = (uint)writer.Position;
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSWidth)), 0, 4);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSHeight)), 0, 4);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSImageType)), 0, 4);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSFilePathOffset)), 0, 4);
                    Entries[i].ConsoleDDSHeaderOffset = (uint)(Entries[i].DDSFilePathOffset + Entries[i].DDSFilePath.Length + Entries[i].BufferSize + 1);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].ConsoleDDSHeaderOffset)), 0, 4);
                    byte[] ddsFilePathByteArray = Encoding.ASCII.GetBytes(Entries[i].DDSFilePath);
                    writer.Write(ddsFilePathByteArray, 0, ddsFilePathByteArray.Length);
                    writer.WriteByte(0);

                    // Console DDS Header Info
                    byte[] buffer = new byte[(int)Entries[i].BufferSize];
                    writer.Write(buffer, 0, (int)Entries[i].BufferSize);
                    writer.WriteByte(Entries[i].DDSType);
                    writer.WriteByte(Entries[i].DDSMipMaps);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].Unknown1)), 0, 4);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].Unknown2)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSWidth2)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSHeight2)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].DDSImageType2)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].Unknown3)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].Unknown4)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].Unknown5)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Entries[i].Reserved)), 0, 4);
                }
            }
            else
            {
                writer.Write(BitConverter.GetBytes(1415073867), 0, 4); // KPXT
                writer.Write(BitConverter.GetBytes(FilesCount), 0, 4);

                for (int i = 0; i < FilesCount; i++)
                {
                    writer.Write(BitConverter.GetBytes(Entries[i].DDSPathOffset), 0, 4);
                    writer.Write(BitConverter.GetBytes(Entries[i].DDSPathSize), 0, 4);
                    writer.Write(BitConverter.GetBytes(Entries[i].DDSDataOffset), 0, 4);
                    writer.Write(BitConverter.GetBytes(Entries[i].DDSDataSize), 0, 4);
                }

                for (int i = 0; i < FilesCount; i++)
                {
                    Entries[i].DDSPathOffset = (uint)writer.Position;
                    writer.Write(BitConverter.GetBytes(Entries[i].DDSWidth), 0, 4);
                    writer.Write(BitConverter.GetBytes(Entries[i].DDSHeight), 0, 4);
                    writer.Write(BitConverter.GetBytes(Entries[i].DDSImageType), 0, 4);
                    writer.Write(BitConverter.GetBytes(Entries[i].DDSFilePathOffset), 0, 4);
                    writer.Write(BitConverter.GetBytes(Entries[i].ConsoleDDSHeaderOffset), 0, 4);
                    byte[] ddsFilePathByteArray = Encoding.ASCII.GetBytes(Entries[i].DDSFilePath);
                    writer.Write(ddsFilePathByteArray, 0, ddsFilePathByteArray.Length);
                    writer.WriteByte(0);
                }
            }
            writer.Flush();
        }
        #endregion
    }
}

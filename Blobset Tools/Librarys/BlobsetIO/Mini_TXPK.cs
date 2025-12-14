using Blobset_Tools;
using PackageIO;
using System.Text;

namespace BlobsetIO
{
    /// <summary>
    /// DDS Mini TXPK Class.
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
    public class Mini_TXPK
    {
        #region Fields
        private uint ddsWidth = 0;
        private uint ddsHeight = 0;
        private uint ddsImageType = 1; // Not sure what this is really. It seems if a image is a material, the value is 2 else it's a 1.  
        private uint ddsFilePathOffset = 20; // SizeOf uint(DDSWidth + DDSHeight + DDSImageType + DDSFilePathOffset + consoleDDSHeaderOffset) Always 0x14.
        private uint consoleDDSHeaderOffset = 0; //  On PS3 or Xbox 360 - ConsoleDDSHeaderOffset, On PC - DDSDataSize.
        private string? ddsFilePath = string.Empty;

        // ** For PS3 only **
        private uint bufferSize = 0;
        private byte ddsType = 0; // DXT1 No Alpha - 136
        private byte ddsMipMaps = 0;
        private uint unknown1 = 33554432; // Always - 33554432
        private ushort unknown2 = 0; // DXT1 No Alpha - 43748
        private ushort ddsWidth2 = 0;
        private ushort ddsHeight2 = 0;
        private ushort ddsImageType2 = 0;
        private ushort unknown3 = 0; // ?
        private ushort unknown4 = 0; // ?
        private ushort unknown5 = 0; // only has a value in DXT5 textures
        private uint reserved = 0; // ?
        #endregion

        #region Properties
        public uint DDSWidth
        {
            get { return ddsWidth; }
            set { ddsWidth = value; }
        }

        public uint DDSHeight
        {
            get { return ddsHeight; }
            set { ddsHeight = value; }
        }

        public uint DDSImageType
        {
            get { return ddsImageType; }
            set { ddsImageType = value; }
        }

        public uint DDSFilePathOffset
        {
            get { return ddsFilePathOffset; }
            set { ddsFilePathOffset = value; }
        }

        public uint ConsoleDDSHeaderOffset
        {
            get { return consoleDDSHeaderOffset; }
            set { consoleDDSHeaderOffset = value; }
        }

        public string? DDSFilePath
        {
            get { return ddsFilePath; }
            set { ddsFilePath = value; }
        }

        public uint BufferSize
        {
            get { return bufferSize; }
            set { bufferSize = value; }
        }

        public byte DDSType
        {
            get { return ddsType; }
            set { ddsType = value; }
        }

        public byte DDSMipMaps
        {
            get { return ddsMipMaps; }
            set { ddsMipMaps = value; }
        }

        public uint Unknown1
        {
            get { return unknown1; }
            set { unknown1 = value; }
        }

        public ushort Unknown2
        {
            get { return unknown2; }
            set { unknown2 = value; }
        }

        public ushort DDSWidth2
        {
            get { return ddsWidth2; }
            set { ddsWidth2 = value; }
        }

        public ushort DDSHeight2
        {
            get { return ddsHeight2; }
            set { ddsHeight2 = value; }
        }

        public ushort DDSImageType2
        {
            get { return ddsImageType2; }
            set { ddsImageType2 = value; }
        }

        public ushort Unknown3
        {
            get { return unknown3; }
            set { unknown3 = value; }
        }

        public ushort Unknown4
        {
            get { return unknown4; }
            set { unknown4 = value; }
        }

        public ushort Unknown5
        {
            get { return unknown5; }
            set { unknown5 = value; }
        }

        public uint Reserved
        {
            get { return reserved; }
            set { reserved = value; }
        }
        #endregion

        #region "Deserialize"
        /// <summary>
        /// Deserialize Mini TXPK stream
        /// </summary>
        /// <param name="input">Mini TXPK input stream</param>
        public void Deserialize(Reader input)
        {
            Endian endian = Endian.Little;

            if (Global.isBigendian)
                endian = Endian.Big;

            input.CurrentEndian = endian;

            DDSWidth = input.ReadUInt32();
            DDSHeight = input.ReadUInt32();
            DDSImageType = input.ReadUInt32();
            DDSFilePathOffset = input.ReadUInt32(); // always 0x14
            ConsoleDDSHeaderOffset = input.ReadUInt32();
            DDSFilePath = input.ReadNullTerminatedString();
            DDSFilePath = DDSFilePath.Replace("/", @"\");

            if (Global.gameInfo.GameId > 15)
                DDSFilePath = DDSFilePath.Replace(".badds", ".dds");

            if (Global.isBigendian)
            {
                int bufferSize = Convert.ToInt32(ConsoleDDSHeaderOffset - DDSFilePathOffset - (DDSFilePath.Length + 1));
                input.ReadBytes(bufferSize);

                BufferSize = (uint)bufferSize;
                DDSType = input.ReadByte();
                DDSMipMaps = input.ReadByte();
                Unknown1 = input.ReadUInt32();
                Unknown2 = input.ReadUInt16();
                DDSWidth2 = input.ReadUInt16();
                DDSHeight2 = input.ReadUInt16();
                DDSImageType2 = input.ReadUInt16();
                Unknown3 = input.ReadUInt16();
                Unknown4 = input.ReadUInt16();
                Unknown5 = input.ReadUInt16();
                Reserved = input.ReadUInt32();
            }
        }
        #endregion

        #region "Serialize"
        /// <summary>
        /// Serialize Mini TXPK stream
        /// </summary>
        /// <param name="fileIn">DDS file</param>
        /// <param name="filePath">Mini TXPK DDS file path</param>
        /// <param name="fileOut">File out path</param>
        public void Serialize(string fileIn, string filePath, FileStream writer)
        {
            Reader? reader = null;

            try
            {
                reader = new Reader(fileIn);

                reader.Position = 12;
                DDSWidth = reader.ReadUInt32();
                DDSHeight = reader.ReadUInt32();
                DDSWidth2 = (ushort)DDSWidth;
                DDSHeight2 = (ushort)DDSHeight;
                byte imageType = 1;

                if (Path.GetFileNameWithoutExtension(fileIn).Contains("mat.pc"))
                    imageType = 2;

                DDSImageType = imageType;
                DDSImageType2 = (ushort)DDSImageType;
                DDSFilePathOffset = 20;
                DDSFilePath = @"convant2-temp-intermediate/" + filePath.Replace(@"\", "/");
                ConsoleDDSHeaderOffset = Global.isBigendian ? (uint)(DDSFilePathOffset + DDSFilePath.Length + BufferSize + 1) : (uint)Utilities.FileInfo(fileIn);
                byte[] ddsFilePathByteArray = Encoding.ASCII.GetBytes(DDSFilePath);

                byte[] _DDSWidth = Global.isBigendian ? Functions.SwapSex(BitConverter.GetBytes(DDSWidth)) : BitConverter.GetBytes(DDSWidth);
                writer.Write(_DDSWidth, 0, 4);
                byte[] _DDSHeight = Global.isBigendian ? Functions.SwapSex(BitConverter.GetBytes(DDSHeight)) : BitConverter.GetBytes(DDSHeight);
                writer.Write(_DDSHeight, 0, 4);
                byte[] _DDSImageType = Global.isBigendian ? Functions.SwapSex(BitConverter.GetBytes(DDSImageType)) : BitConverter.GetBytes(DDSImageType);
                writer.Write(_DDSImageType, 0, 4);
                byte[] _DDSFilePathOffset = Global.isBigendian ? Functions.SwapSex(BitConverter.GetBytes(DDSFilePathOffset)) : BitConverter.GetBytes(DDSFilePathOffset);
                writer.Write(_DDSFilePathOffset, 0, 4);
                byte[] _ConsoleDDSHeaderOffset = Global.isBigendian ? Functions.SwapSex(BitConverter.GetBytes(ConsoleDDSHeaderOffset)) : BitConverter.GetBytes(consoleDDSHeaderOffset);
                writer.Write(_ConsoleDDSHeaderOffset, 0, 4);
                writer.Write(ddsFilePathByteArray, 0, ddsFilePathByteArray.Length);
                writer.WriteByte(0);

                if (Global.platforms == Enums.Platforms.PS3)
                {
                    // Console DDS Header Info
                    byte[] buffer = new byte[BufferSize];
                    writer.Write(buffer, 0, (int)BufferSize);
                    writer.WriteByte(DDSType);
                    writer.WriteByte(DDSMipMaps);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Unknown1)), 0, 4);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Unknown2)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(DDSWidth2)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(DDSHeight2)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(DDSImageType2)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Unknown3)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Unknown4)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Unknown5)), 0, 2);
                    writer.Write(Functions.SwapSex(BitConverter.GetBytes(Reserved)), 0, 4);
                }

                writer.Flush();
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (reader != null) { reader.Close(); reader = null; }
            }
        }
        #endregion

        public byte[] WriteAndRead(string fileIn, string filePath)
        {
            Reader? readerDDS = null;
            Writer? bw = null;
            MemoryStream? ms = null;
            byte[]? headerData = null;

            try
            {
                readerDDS = new Reader(fileIn);

                readerDDS.Position = 12;
                DDSWidth = readerDDS.ReadUInt32();
                DDSHeight = readerDDS.ReadUInt32();
                DDSWidth2 = (ushort)DDSWidth;
                DDSHeight2 = (ushort)DDSHeight;
                byte imageType = 1;

                if (Path.GetFileNameWithoutExtension(fileIn).Contains("mat.pc"))
                    imageType = 2;

                DDSImageType = imageType;
                DDSImageType2 = (ushort)DDSImageType;
                DDSFilePathOffset = 20;
                DDSFilePath = @"convant2-temp-intermediate/" + filePath.Replace(@"\", "/");
                ConsoleDDSHeaderOffset = Global.isBigendian ? (uint)(DDSFilePathOffset + DDSFilePath.Length + BufferSize + 1) : (uint)Utilities.FileInfo(fileIn);

                byte[] ddsFilePathByteArray = Encoding.ASCII.GetBytes(DDSFilePath);

                int headerSize = (int)(DDSFilePathOffset + DDSFilePath.Length + BufferSize + 1);

                headerData = new byte[headerSize + 24];

                ms = new(headerData);
                bw = new(ms, Global.isBigendian ? Endian.Big : Endian.Little);

                bw.WriteUInt32(DDSWidth);
                bw.WriteUInt32(DDSHeight);
                bw.WriteUInt32(DDSImageType);
                bw.WriteUInt32(DDSFilePathOffset);
                bw.WriteUInt32(ConsoleDDSHeaderOffset);
                bw.Write(ddsFilePathByteArray, Endian.Little);
                bw.WriteInt8(0);

                if (Global.platforms == Enums.Platforms.PS3)
                {
                    // Console DDS Header Info
                    byte[] buffer = new byte[BufferSize];
                    bw.Write(buffer);
                    bw.WriteUInt8(DDSType, Endian.Little);
                    bw.WriteUInt8(DDSMipMaps, Endian.Little);
                    bw.WriteUInt32(Unknown1);
                    bw.WriteUInt16(Unknown2);
                    bw.WriteUInt16(DDSWidth2);
                    bw.WriteUInt16(DDSHeight2);
                    bw.WriteUInt16(DDSImageType2);
                    bw.WriteUInt16(Unknown3);
                    bw.WriteUInt16(Unknown4);
                    bw.WriteUInt16(Unknown5);
                    bw.WriteUInt32(Reserved);
                }

                bw.Flush();
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (readerDDS != null) { readerDDS.Close(); readerDDS = null; }
                if (bw != null) { bw.Close(); bw = null; }
                if (ms != null) { ms.Close(); ms = null; }
            }
            return headerData;
        }
    }
}

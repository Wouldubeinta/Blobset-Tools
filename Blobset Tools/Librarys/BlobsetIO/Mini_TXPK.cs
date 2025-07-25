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
    ///   as published by the Free Software Foundation; either version 2
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
        private uint headerSize = 20; // SizeOf uint(DDSWidth + DDSHeight + DDSImageType + HeaderSize + DDSDataSize) Always 0x14.
        private uint ddsDataSize = 0;
        private string ddsFilePath = string.Empty;
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

        public uint HeaderSize
        {
            get { return headerSize; }
            set { headerSize = value; }
        }

        public uint DDSDataSize
        {
            get { return ddsDataSize; }
            set { ddsDataSize = value; }
        }

        public string? DDSFilePath
        {
            get { return ddsFilePath; }
            set { ddsFilePath = value; }
        }
        #endregion

        #region "Deserialize"
        /// <summary>
        /// Deserialize Mini TXPK stream
        /// </summary>
        /// <param name="input">Mini TXPK input stream</param>
        public void Deserialize(Reader input)
        {
            DDSWidth = input.ReadUInt32();
            DDSHeight = input.ReadUInt32();
            DDSImageType = input.ReadUInt32();
            HeaderSize = input.ReadUInt32(); // always 0x14
            DDSDataSize = input.ReadUInt32();
            DDSFilePath = input.ReadNullTerminatedString();
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

                byte imageType = 1;

                if (Path.GetFileNameWithoutExtension(fileIn).Contains("mat.pc"))
                    imageType = 2;

                DDSImageType = imageType;
                HeaderSize = 20;
                DDSDataSize = (uint)Utilities.FileInfo(fileIn);
                DDSFilePath = @"convant2-temp-intermediate/" + filePath.Replace(@"\", "/");

                writer.Write(BitConverter.GetBytes(DDSWidth), 0, 4);
                writer.Write(BitConverter.GetBytes(DDSHeight), 0, 4);
                writer.Write(BitConverter.GetBytes(DDSImageType), 0, 4);
                writer.Write(BitConverter.GetBytes(HeaderSize), 0, 4);
                writer.Write(BitConverter.GetBytes(DDSDataSize), 0, 4);
                byte[] ddsFilePathByteArray = Encoding.ASCII.GetBytes(DDSFilePath);
                writer.Write(ddsFilePathByteArray, 0, ddsFilePathByteArray.Length);
                writer.WriteByte(0);
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
    }
}

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
            public uint DDSDataSize1 = 0;

            public uint DDSWidth = 0;
            public uint DDSHeight = 0;
            public uint DDSImageType = 1;
            public uint HeaderSize = 20; // SizeOf uint(DDSWidth + DDSHeight + DDSMipmapCount + HeaderSize + DDSDataSize2) Always 0x14.
            public uint DDSDataSize2 = 0; // Don't know why this is shown twice?
            public string DDSFilePath = string.Empty;
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
                Entries[i].DDSDataSize1 = input.ReadUInt32();
            }

            for (int i = 0; i < FilesCount; i++)
            {
                input.Position = Entries[i].DDSPathOffset;
                Entries[i].DDSWidth = input.ReadUInt32();
                Entries[i].DDSHeight = input.ReadUInt32();
                Entries[i].DDSImageType = input.ReadUInt32();
                Entries[i].HeaderSize = input.ReadUInt32();
                Entries[i].DDSDataSize2 = input.ReadUInt32();
                Entries[i].DDSFilePath = input.ReadNullTerminatedString();
            }
        }
        #endregion

        #region "Serialize"
        /// <summary>
        /// Serialize TXPK stream
        /// </summary>
        /// <param name="output">TXPK output stream</param>
        public void Serialize(FileStream output)
        {
            output.Position = 0;
            output.Write(BitConverter.GetBytes(1415073867), 0, 4); // KPXT
            output.Write(BitConverter.GetBytes(FilesCount), 0, 4);

            for (int i = 0; i < FilesCount; i++)
            {
                output.Write(BitConverter.GetBytes(Entries[i].DDSPathOffset), 0, 4);
                output.Write(BitConverter.GetBytes(Entries[i].DDSPathSize), 0, 4);
                output.Write(BitConverter.GetBytes(Entries[i].DDSDataOffset), 0, 4);
                output.Write(BitConverter.GetBytes(Entries[i].DDSDataSize1), 0, 4);
            }

            for (int i = 0; i < FilesCount; i++)
            {
                Entries[i].DDSPathOffset = (uint)output.Position;
                output.Write(BitConverter.GetBytes(Entries[i].DDSWidth), 0, 4);
                output.Write(BitConverter.GetBytes(Entries[i].DDSHeight), 0, 4);
                output.Write(BitConverter.GetBytes(Entries[i].DDSImageType), 0, 4);
                output.Write(BitConverter.GetBytes(Entries[i].HeaderSize), 0, 4);
                output.Write(BitConverter.GetBytes(Entries[i].DDSDataSize2), 0, 4);
                byte[] ddsFilePathByteArray = Encoding.ASCII.GetBytes(Entries[i].DDSFilePath);
                output.Write(ddsFilePathByteArray, 0, ddsFilePathByteArray.Length);
                output.WriteByte(0);
            }
            output.Flush();
        }
        #endregion
    }
}

using Blobset_Tools;
using PackageIO;

namespace BlobsetIO
{
    /// <summary>
    /// M3MP Class.
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
    public class M3MP
    {
        #region Fields
        private uint magic = 0; // Should be  - M3MP
        private uint filesCount = 0;
        private uint chunksCount = 0;
        private uint compressedDataOffset = 0;
        private UnCompressedEntry[]? uncompressedEntries;
        private CompressedEntry[]? compressedEntries;
        #endregion

        public M3MP()
        {
            uncompressedEntries = null;
            compressedEntries = null;
        }

        public class UnCompressedEntry
        {
            public UncompressedDataInfo? UncompressedDataInfo;
            public string? FilePath;
        }

        public class CompressedEntry
        {
            public ChunkCompressedDataInfo? CompressedDataInfo;
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

        public uint ChunksCount
        {
            get { return chunksCount; }
            set { chunksCount = value; }
        }

        public uint CompressedDataOffset
        {
            get { return compressedDataOffset; }
            set { compressedDataOffset = value; }
        }

        public UnCompressedEntry[] UnCompressedEntries
        {
            get { return uncompressedEntries; }
            set { uncompressedEntries = value; }
        }

        public CompressedEntry[] CompressedEntries
        {
            get { return compressedEntries; }
            set { compressedEntries = value; }
        }
        #endregion

        public class UncompressedDataInfo
        {
            public uint Offset = 0;
            public uint Size = 0;
            public uint FilePathOffset = 0;
            public uint Reserved = 0; // Allways 0
        }

        public class ChunkCompressedDataInfo
        {
            public uint Offset = 0;
            public uint CompressedSize = 0;
            public uint UnCompressedSize = 0;
        }

        #region "Deserialize"
        /// <summary>
        /// Deserialize M3MP stream
        /// </summary>
        /// <param name="input">M3MP input stream</param>
        public void Deserialize(Reader input)
        {
            Endian endian = Endian.Little;

            if (Global.isBigendian)
                endian = Endian.Big;

            input.CurrentEndian = endian;

            Magic = input.ReadUInt32();

            if (Magic != 1347236685) // M3MP
            {
                MessageBox.Show("This isn't a M3MP File", "Wrong Magic", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            FilesCount = input.ReadUInt32();
            ChunksCount = input.ReadUInt32();
            CompressedDataOffset = input.ReadUInt32();

            UnCompressedEntries = new UnCompressedEntry[FilesCount];

            for (int i = 0; i < FilesCount; i++)
            {
                UnCompressedEntries[i] = new();
                UnCompressedEntries[i].UncompressedDataInfo = new();
                UnCompressedEntries[i].UncompressedDataInfo.Offset = input.ReadUInt32();
                UnCompressedEntries[i].UncompressedDataInfo.Size = input.ReadUInt32();
                UnCompressedEntries[i].UncompressedDataInfo.FilePathOffset = input.ReadUInt32();
                UnCompressedEntries[i].UncompressedDataInfo.Reserved = input.ReadUInt32();
            }

            for (int i = 0; i < FilesCount; i++)
            {
                UnCompressedEntries[i].FilePath = input.ReadNullTerminatedString();
            }

            CompressedEntries = new CompressedEntry[ChunksCount];

            for (int i = 0; i < ChunksCount; i++)
            {
                CompressedEntries[i] = new();
                CompressedEntries[i].CompressedDataInfo = new();
                CompressedEntries[i].CompressedDataInfo.Offset = input.ReadUInt32();
                CompressedEntries[i].CompressedDataInfo.CompressedSize = input.ReadUInt32();
                CompressedEntries[i].CompressedDataInfo.UnCompressedSize = input.ReadUInt32();
            }
        }
        #endregion

        #region "Serialize"
        /// <summary>
        /// Serialize M3MP stream
        /// </summary>
        /// <param name="output">M3MP output stream</param>
        public void Serialize(Writer output)
        {
            Endian endian = Endian.Little;

            if (Global.isBigendian)
                endian = Endian.Big;

            output.CurrentEndian = endian;

            output.WriteUInt32(1347236685); // M3MP
            output.WriteUInt32(FilesCount);
            output.WriteUInt32(ChunksCount);
            output.WriteUInt32(CompressedDataOffset);

            for (int i = 0; i < FilesCount; i++)
            {
                output.WriteUInt32(UnCompressedEntries[i].UncompressedDataInfo.Offset);
                output.WriteUInt32(UnCompressedEntries[i].UncompressedDataInfo.Size);
                output.WriteUInt32(UnCompressedEntries[i].UncompressedDataInfo.FilePathOffset);
                output.WriteUInt32(UnCompressedEntries[i].UncompressedDataInfo.Reserved);
            }

            output.CurrentEndian = Endian.Little;

            for (int i = 0; i < FilesCount; i++)
            {
                UnCompressedEntries[i].UncompressedDataInfo.FilePathOffset = (uint)output.Position;
                output.WriteString(UnCompressedEntries[i].FilePath);
                output.WriteInt8(0);
            }

            output.CurrentEndian = endian;

            for (int i = 0; i < ChunksCount; i++)
            {
                output.WriteUInt32(CompressedEntries[i].CompressedDataInfo.Offset);
                output.WriteUInt32(CompressedEntries[i].CompressedDataInfo.CompressedSize);
                output.WriteUInt32(CompressedEntries[i].CompressedDataInfo.UnCompressedSize);
            }
        }
        #endregion
    }
}

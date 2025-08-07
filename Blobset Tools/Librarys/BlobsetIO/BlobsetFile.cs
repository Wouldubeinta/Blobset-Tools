using Blobset_Tools;
using PackageIO;

namespace BlobsetIO
{
    /// <summary>
    /// Blobset File Class.
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
    /// [Wouldubeinta]	   02/06/2025	Created
    /// </history>
    public class BlobsetFile
    {
        #region Fields
        private byte[]? sHA1Hash = new byte[20]; // only used in older games
        private uint magic = 0; // Should be  - BLOB or BOLB
        private uint blobsetCount = 0;
        private uint filesCount = 0;
        private Entry[]? entries;
        #endregion

        public BlobsetFile()
        {
            entries = null;
        }

        public class Entry
        {
            public string FolderHashName = string.Empty;
            public string FileHashName = string.Empty;

            public uint MainFinalOffSet = 0;
            public uint MainCompressedSize = 0;
            public uint MainUnCompressedSize = 0;
            public uint VramFinalOffSet = 0;
            public uint VramCompressedSize = 0;
            public uint VramUnCompressedSize = 0;
            public uint BlobSetNumber = 0;
        }

        #region Properties
        public byte[] SHA1Hash
        {
            get { return sHA1Hash; }
            set { sHA1Hash = value; }
        }

        public uint Magic
        {
            get { return magic; }
            set { magic = value; }
        }

        public uint BlobsetCount
        {
            get { return blobsetCount; }
            set { blobsetCount = value; }
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
        /// Deserialize Blobset stream
        /// </summary>
        /// <param name="input">Blobset input stream</param>
        public void Deserialize(Reader input, Enums.BlobsetVersion blobsetVersion)
        {
            if (blobsetVersion == Enums.BlobsetVersion.v2)
                SHA1Hash = input.ReadBytes(20);

            Magic = input.ReadUInt32();

            if (Magic != 1112493122 && Magic != 1112297282) // BLOB or BOLB
            {
                MessageBox.Show("This isn't a Blobset File", "Wrong Magic", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            if (blobsetVersion != Enums.BlobsetVersion.v3 && blobsetVersion != Enums.BlobsetVersion.v4)
                BlobsetCount = input.ReadUInt32();

            FilesCount = input.ReadUInt32();

            Entries = new Entry[FilesCount];

            for (int i = 0; i < FilesCount; i++)
            {
                Entries[i] = new Entry();

                if (blobsetVersion == Enums.BlobsetVersion.v4)
                {
                    ulong fileFolderHashName = input.ReadUInt64();
                    byte[] intBytes = BitConverter.GetBytes(fileFolderHashName);
                    string folderName = string.Format("{0:x2}", intBytes[7]);
                    string fileName = string.Format("{0:x2}{1:x2}{2:x2}{3:x2}{4:x2}{5:x2}{6:x2}", intBytes[6], intBytes[5], intBytes[4], intBytes[3], intBytes[2], intBytes[1], intBytes[0]);
                    Entries[i].FolderHashName = folderName;
                    Entries[i].FileHashName = fileName;
                }
                else if (blobsetVersion == Enums.BlobsetVersion.v1 || blobsetVersion == Enums.BlobsetVersion.v2)
                {
                    Entries[i].MainFinalOffSet = input.ReadUInt32();
                    Entries[i].MainCompressedSize = input.ReadUInt32();
                    Entries[i].MainUnCompressedSize = input.ReadUInt32();
                    Entries[i].VramFinalOffSet = input.ReadUInt32();
                    Entries[i].VramCompressedSize = input.ReadUInt32();
                    Entries[i].VramUnCompressedSize = input.ReadUInt32();
                    uint fileFolderHashName = input.ReadUInt32();
                    byte[] intBytes = BitConverter.GetBytes(fileFolderHashName);
                    string folderName = string.Format("{0:x2}", intBytes[3]);
                    string fileName = string.Format("{0:x2}{1:x2}{2:x2}", intBytes[2], intBytes[1], intBytes[0]);
                    Entries[i].FolderHashName = folderName;
                    Entries[i].FileHashName = fileName;
                    Entries[i].BlobSetNumber = input.ReadUInt32();
                }
                else
                {
                    Entries[i].MainCompressedSize = input.ReadUInt32();
                    Entries[i].MainUnCompressedSize = input.ReadUInt32();
                    Entries[i].VramCompressedSize = input.ReadUInt32();
                    Entries[i].VramUnCompressedSize = input.ReadUInt32();
                    uint fileFolderHashName = input.ReadUInt32();
                    byte[] intBytes = BitConverter.GetBytes(fileFolderHashName);
                    string folderName = string.Format("{0:x2}", intBytes[3]);
                    string fileName = string.Format("{0:x2}{1:x2}{2:x2}", intBytes[2], intBytes[1], intBytes[0]);
                    input.ReadUInt32();
                    Entries[i].FolderHashName = folderName;
                    Entries[i].FileHashName = fileName;
                }
            }

            if (blobsetVersion == Enums.BlobsetVersion.v4)
            {
                for (int i = 0; i < FilesCount; i++)
                {
                    Entries[i].MainCompressedSize = input.ReadUInt32();
                    Entries[i].MainUnCompressedSize = input.ReadUInt32();
                    Entries[i].VramCompressedSize = input.ReadUInt32();
                    Entries[i].VramUnCompressedSize = input.ReadUInt32();
                }
            }
        }
        #endregion

        #region "Serialize"
        /// <summary>
        /// Serialize Blobset stream v4
        /// </summary>
        /// <param name="output">Blobset output stream</param>
        public void Serialize(Writer output)
        {
            output.WriteUInt32(1112493122); // BLOB
            output.WriteUInt32(FilesCount);

            for (int i = 0; i < FilesCount; i++)
            {
                output.WriteHexString(Entries[i].FolderHashName);
                output.WriteHexString(Entries[i].FileHashName);
            }

            for (int i = 0; i < FilesCount; i++)
            {
                output.WriteUInt32(Entries[i].MainCompressedSize);
                output.WriteUInt32(Entries[i].MainUnCompressedSize);
                output.WriteUInt32(Entries[i].VramCompressedSize);
                output.WriteUInt32(Entries[i].VramUnCompressedSize);
            }
        }
        #endregion
    }
}

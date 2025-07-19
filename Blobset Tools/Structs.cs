using Pfim;

namespace Blobset_Tools
{
    /// <summary>
    /// Where all the structs are stored.
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
    ///   The author may be contacted at:
    ///   Discord: Wouldubeinta
    /// </remarks>
    /// <history>
    /// [Wouldubeinta]	   02/06/2025	Created
    /// </history>
    public class Structs
    {
        public struct FileIndexInfo
        {
            public int MappingIndex;
            public string FileName;
            public int BlobsetIndex;
            public string FilePath;
            public string FolderHash;
            public string FileHash;
        }

        public struct ChunkInfo
        {
            public long Offset;
            public int ChunkAmount;
        }

        public struct DDSInfo
        {
            public int Size;
            public int Offset;
            public int Length;
            public int MipMap;
            public PixelFormat PFormat;
            public ImageFormat IFormat;
        }
    }
}

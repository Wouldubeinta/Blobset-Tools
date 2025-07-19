using Blobset_Tools;


namespace BlobsetIO
{
    /// <summary>
    /// Utilities Class that holds all the usefull functions for the blobset tools.
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
    /// [Wouldubeinta]	   01/07/2025	Created
    /// </history>
    internal class Utilities
    {
        /// <summary>
        /// Get ChunkCount from uncompressed file size.
        /// </summary>
        /// <param name="UnCommpressedS">Uncompressed file size</param>
        /// <param name="ChunkSize">Chunk Size. For blobset file v4 - 262144, v1, v2 and v3 - 32768</param>
        public static int ChunkAmount(int UnCommpressedSize, int ChunkSize = 262144)
        {
            int ChunkCount = 0;
            decimal ChunkSizeDec = (decimal)UnCommpressedSize / ChunkSize;

            if (UnCommpressedSize < ChunkSize)
            {
                ChunkCount = 1;
            }
            else if (decimal.Round(ChunkSizeDec, 0) == ChunkSizeDec)
            {
                ChunkCount = UnCommpressedSize / ChunkSize;
            }
            else if (decimal.Round(ChunkSizeDec, 0) != ChunkSizeDec)
            {
                ChunkCount = (UnCommpressedSize / ChunkSize) + 1;
            }
            return ChunkCount;
        }

        /// <summary>
        /// Gets a ChunkSize array from uncompressed file size and chunk count.
        /// </summary>
        /// <param name="UnCommpressedSize">Uncompressed file size</param>
        /// <param name="ChunkAmount">Chunk Count</param>
        /// <param name="ChunkSize">Chunk Size. For blobset file v3 - 262144, v1 and v2 - 32768</param>
        public static long[] ChunkSizes(int UnCommpressedSize, int ChunkAmount, int ChunkSize = 262144)
        {
            long[] ChunkS = new long[ChunkAmount];
            decimal ChunkSizeDec = (decimal)UnCommpressedSize / ChunkSize;

            if (UnCommpressedSize < ChunkSize)
            {
                ChunkS[0] = UnCommpressedSize;
            }
            else if (decimal.Round(ChunkSizeDec, 0) == ChunkSizeDec)
            {
                for (int i = 0; i < ChunkAmount; i++)
                {
                    ChunkS[i] = ChunkSize;
                }
            }
            else if (decimal.Round(ChunkSizeDec, 0) != ChunkSizeDec)
            {
                for (int i = 0; i < ChunkAmount - 1; i++)
                {
                    ChunkS[i] = ChunkSize;
                }

                int Chunk = UnCommpressedSize / ChunkSize;
                int LastChunkSize = ChunkSize * Chunk;
                ChunkS[ChunkAmount - 1] = UnCommpressedSize - LastChunkSize;
            }
            return ChunkS;
        }

        public static int LinesLength(string file)
        {
            return File.ReadLines(file).Count();
        }

        public static long FileInfo(string file)
        {
            return new FileInfo(file).Length;
        }

        public static string[] DirectoryInfo(string folder, string Char)
        {
            return Directory.GetFiles(folder, Char, SearchOption.AllDirectories);
        }

        public static string FormatSize(ulong bytes)
        {
            string[] units = ["B", "KB", "MB", "GB", "TB", "PB", "EB"];

            int c;
            for (c = 0; c < units.Length; c++)
            {
                ulong m = (ulong)1 << ((c + 1) * 10);
                if (bytes < m)
                    break;
            }

            double n = bytes / (double)((ulong)1 << (c * 10));
            return string.Format("{0:0.##} {1}", n, units[c]);
        }

        public static FileMapping GetFileMappingIndex(int index, FileMapping fileMapping)
        {
            FileMapping? fm = null;

            foreach (var entry in fileMapping.Entries)
            {
                if (entry.Index == index)
                {
                    fm = new FileMapping();
                    FileMapping.Entry fme = new();
                    fme.Index = entry.Index;
                    fme.FilePath = entry.FilePath;
                    fme.FileNameHash = entry.FileNameHash;
                    fme.FolderHash = entry.FolderHash;
                    fm.Entries.Add(fme);
                    return fm;
                }
            }
            return fm;
        }

        public static FileMapping GetFileMappingIndex(string filePath, FileMapping fileMapping)
        {
            FileMapping? fm = null;

            foreach (var entry in fileMapping.Entries)
            {
                if (entry.FilePath == filePath)
                {
                    fm = new FileMapping();
                    FileMapping.Entry fme = new();
                    fme.Index = entry.Index;
                    fme.FilePath = entry.FilePath;
                    fme.FileNameHash = entry.FileNameHash;
                    fme.FolderHash = entry.FolderHash;
                    fm.Entries.Add(fme);
                    return fm;
                }
            }
            return fm;
        }
    }
}

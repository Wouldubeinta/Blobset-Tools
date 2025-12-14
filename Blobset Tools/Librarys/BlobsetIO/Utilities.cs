using Blobset_Tools;
using PackageIO;
using static Blobset_Tools.Enums;


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
    ///   as published by the Free Software Foundation; either version 3
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
        public static int ChunkAmount(int uncompressedSize, int chunkSize = 262144)
        {
            if (uncompressedSize <= 0)
                return 1;

            if (chunkSize <= 0)
                return 1;

            // Calculate the number of chunks using ceiling division
            return (int)Math.Ceiling((double)uncompressedSize / chunkSize);
        }

        /// <summary>
        /// Gets a ChunkSize array from uncompressed file size and chunk count.
        /// </summary>
        /// <param name="UnCommpressedSize">Uncompressed file size</param>
        /// <param name="ChunkAmount">Chunk Count</param>
        /// <param name="ChunkSize">Chunk Size. For blobset file v3 - 262144, v1 and v2 - 32768</param>
        public static long[] ChunkSizes(int uncompressedSize, int chunkAmount, int chunkSize = 262144)
        {
            if (chunkAmount <= 0)
                throw new ArgumentOutOfRangeException(nameof(chunkAmount), "Chunk amount must be greater than zero.");

            if (chunkSize <= 0)
                throw new ArgumentOutOfRangeException(nameof(chunkSize), "Chunk size must be greater than zero.");

            long[] chunkSizes = new long[chunkAmount];

            // Calculate the size of each chunk
            long fullChunkSize = chunkSize;
            long totalFullChunks = uncompressedSize / fullChunkSize;
            long remainder = uncompressedSize % fullChunkSize;

            // Fill the array with chunk sizes
            for (int i = 0; i < chunkAmount; i++)
            {
                if (i < totalFullChunks)
                    chunkSizes[i] = fullChunkSize;
                else if (i == totalFullChunks && remainder > 0)
                    chunkSizes[i] = remainder;
                else
                    chunkSizes[i] = 0; // Remaining chunks will be zero if there are not enough bytes
            }
            return chunkSizes;
        }

        public static void DumpBlobsetHeader(string blobsetFile, string filename)
        {
            Reader? br = null;
            Writer? bw = null;

            try
            {
                // Retrieve platform details
                var platformDetails = GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];

                // Define the base path for game-related files
                string basePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt);

                FileStream fin = new(blobsetFile, FileMode.Open,
                FileAccess.ReadWrite, FileShare.ReadWrite);

                FileStream fout = new(Path.Combine(basePath, "backup", filename + ".header"), FileMode.Create,
                FileAccess.ReadWrite, FileShare.ReadWrite);

                br = new Reader(fin, Global.isBigendian ? Endian.Big : Endian.Little);
                bw = new Writer(fout, Endian.Little);

                const int HeaderSize = 12;
                const int DefaultHashSize = 20;
                const int NoHashSize = 0;

                int hashSize = DefaultHashSize;

                switch (Global.gameInfo.GameId)
                {
                    case 0: // AFLL has no 20 byte hash
                    case 5 when Global.isBigendian: // TableTop Cricket has no hash if big-endian
                        hashSize = NoHashSize;
                        break;
                }

                // Set the initial position to read header size
                br.Position = hashSize + HeaderSize;
                int headerSize = br.ReadInt32();
                br.Position = 0;
                byte[] header = br.ReadBytes(headerSize, Endian.Little);

                bw.Write(header, Endian.Little);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null)
                    br.Close();
                if (bw != null)
                    bw.Close();
            }
        }

        public static byte[] ReadBlobsetHeader(string filename)
        {
            Reader? br = null;
            byte[]? header = null;

            try
            {
                // Retrieve platform details
                var platformDetails = GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];

                // Define the base path for game-related files
                string basePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt);
                string blobsetHeader = Path.Combine(basePath, "backup", filename + ".header");

                if (!File.Exists(blobsetHeader))
                    return header;

                FileStream fin = new(blobsetHeader, FileMode.Open,
                FileAccess.ReadWrite, FileShare.ReadWrite);

                int headerSize = (int)fin.Length;
                header = new byte[headerSize];

                br = new Reader(fin, Endian.Little);
                header = br.ReadBytes(headerSize, Endian.Little);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
            }
            return header;
        }

        public static void ResetBlobset(string file)
        {
            Reader? br = null;
            Writer? bw = null;

            try
            {
                FileStream fin = new(Global.currentPath + @"\data\blobset.header", FileMode.Open,
                FileAccess.ReadWrite, FileShare.ReadWrite);

                FileStream fout = new(file + "data-0.blobset.pc", FileMode.Open,
                FileAccess.ReadWrite, FileShare.ReadWrite);

                br = new Reader(fin, Endian.Little);
                bw = new Writer(fout, Endian.Little);

                byte[] header = br.ReadBytes((int)fin.Length, Endian.Little);

                bw.Write(header, Endian.Little);

                if (File.Exists(file + "data-1.blobset.pc"))
                {
                    File.Delete(file + "data-1.blobset.pc");
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null)
                    br.Close();
                if (bw != null)
                    bw.Close();
            }
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

        public static string GetGameVersion()
        {
            string gameVersion = string.Empty;
            int gameID = Global.gameInfo.GameId;

            switch (gameID)
            {
                case (int)Game.AFLL:
                case (int)Game.RLL2:
                case (int)Game.RLL2WCE:
                case (int)Game.DBC14:
                case (int)Game.RLL3:
                case (int)Game.TTC:
                case (int)Game.CPL16:
                    gameVersion = "1.00";
                    break;
                case (int)Game.DBC17:
                case (int)Game.MTBOD:
                case (int)Game.AC:
                case (int)Game.RLL4:
                    string gv1 = Global.gameInfo.GameLocation.Replace(@"data-0.blobset.pc", string.Empty) + "version.txt";
                    if (File.Exists(gv1))
                        gameVersion = File.ReadAllText(gv1);
                    break;
                default:
                    string gv2 = Global.gameInfo.GameLocation.Replace(@"data\data-0.blobset.pc", string.Empty) + "version.txt";
                    if (File.Exists(gv2))
                        gameVersion = File.ReadAllText(gv2);
                    break;
            }
            return gameVersion;
        }

        public static Dictionary<string, string> GetPlatformInfo(Platforms platform)
        {
            var platformInfo = new Dictionary<string, string>();

            switch (platform)
            {
                case Platforms.PS3:
                    platformInfo["Platform"] = "Playstation 3";
                    platformInfo["PlatformExt"] = "ps3";
                    break;
                case Platforms.Xbox360:
                    platformInfo["Platform"] = "Xbox 360";
                    platformInfo["PlatformExt"] = "xbox360";
                    break;
                case Platforms.Windows:
                default:
                    platformInfo["Platform"] = "Windows";
                    platformInfo["PlatformExt"] = "pc";
                    break;
            }

            return platformInfo;
        }

        /// <summary>
        /// Resize image size and return it;
        /// </summary>
        /// <param name="width">Image width.</param>
        /// <param name="height">Image height.</param>
        /// <param name="maxWidth">Max Width.</param>
        /// <param name="maxHeight">Max Height.</param>
        /// <returns>Return new size.</returns>
        public static Size ResizeImageSize(int width, int height, int maxWidth, int maxHeight)
        {
            var ratioX = (double)maxWidth / width;
            var ratioY = (double)maxHeight / height;
            var ratio = Math.Min(ratioX, ratioY);

            var newWidth = (int)(width * ratio);
            var newHeight = (int)(height * ratio);

            return new Size(newWidth, newHeight);
        }
    }
}

using BlobsetIO;
using PackageIO;
using System.ComponentModel;

namespace Blobset_Tools
{
    /// <summary>
    /// Extract different blobset versions.
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
    /// [Wouldubeinta]	   02/06/2025	Created
    /// </history>
    public class Extract
    {
        #region Extract Blobset Version 1
        /// <summary>
        /// Read Blobset header and than extract's file's to a folder - version 1.
        /// </summary>
        /// <param name="blobsetfile">File path to file blobset file</param>
        /// <param name="Extract_bgw">The extract Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		05/07/2025	Created
        /// </history>
        public static bool BlobsetV1(string blobsetfile, BackgroundWorker Extract_bgw)
        {
            Reader? blobsetContent_br = null;
            FileStream? writer = null;
            string progress = string.Empty;

            try
            {
                if (Global.blobsetHeaderData == null)
                {
                    MessageBox.Show("Something went wrong reading the blobset", "Blobset Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return true;
                }

                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];
                string blobsetFilename = Path.GetFileName(blobsetfile);

                BlobsetFile blobset = Global.blobsetHeaderData;
                string folder = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, blobsetFilename);

                if (Directory.Exists(folder))
                {
                    Directory.Delete(folder, true);
                    Directory.CreateDirectory(folder);
                }
                else
                    Directory.CreateDirectory(folder);

                Endian endian = Endian.Little;

                if (Global.isBigendian)
                    endian = Endian.Big;

                IniFile settings = new(Path.Combine(Global.currentPath, "Settings.ini"));
                bool skipUnknownFiles = "false" == settings.Read("SkipUnknown", "Settings") ? false : true;

                for (int i = 0; i < blobset.FilesCount; i++)
                {
                    string filePath = blobsetfile.Replace("0.blob", blobset.Entries[i].BlobSetNumber + ".blob");

                    blobsetContent_br = new(filePath, endian);

                    uint mainFinalOffSet = blobset.Entries[i].MainFinalOffSet;
                    uint mainCompressedSize = blobset.Entries[i].MainCompressedSize;
                    uint mainUnCompressedSize = blobset.Entries[i].MainUnCompressedSize;
                    uint vramFinalOffSet = blobset.Entries[i].VramFinalOffSet;
                    uint vramCompressedSize = blobset.Entries[i].VramCompressedSize;
                    uint vramUnCompressedSize = blobset.Entries[i].VramUnCompressedSize;

                    if (mainCompressedSize == mainUnCompressedSize)
                    {
                        if (vramCompressedSize == vramUnCompressedSize)
                        {
                            blobsetContent_br.Position = mainFinalOffSet + 20; // Offset to convant2 string.

                            if (blobsetContent_br.ReadString(4, Endian.Little) == "conv") // Makes sure it's a mini DDS TXPK.
                            {
                                blobsetContent_br.Position = mainFinalOffSet;

                                Mini_TXPK? mini_TXPK = new();
                                mini_TXPK.Deserialize(blobsetContent_br);

                                string ddsFilePath = Path.Combine(folder, mini_TXPK.DDSFilePath);
                                string ddsFilePathDir = Path.Combine(folder, Path.GetDirectoryName(mini_TXPK.DDSFilePath));
                                Directory.CreateDirectory(ddsFilePathDir);

                                if (Global.isBigendian)
                                    IO.ReadWriteCunkDDSData(blobsetContent_br, ddsFilePath, (int)vramUnCompressedSize, mini_TXPK);
                                else
                                    IO.ReadWriteCunkData(blobsetContent_br, ddsFilePath, (int)vramUnCompressedSize);

                                progress = mini_TXPK.DDSFilePath;
                            }
                            else
                            {
                                blobsetContent_br.Position = mainFinalOffSet;
                                int magic = blobsetContent_br.ReadInt32();
                                blobsetContent_br.Position = mainFinalOffSet;

                                switch (magic)
                                {
                                    case (int)Enums.FileType.PM3M:
                                    case (int)Enums.FileType.M3MP:
                                        string m3mpTempPath = Path.Combine(Global.currentPath, "temp", $"{i}.m3mp");
                                        if (File.Exists(m3mpTempPath)) { File.Delete(m3mpTempPath); }
                                        IO.ReadWriteCunkData(blobsetContent_br, m3mpTempPath, (int)mainUnCompressedSize);
                                        LZMA_IO.M3MPDecompressAndWrite(m3mpTempPath, folder);
                                        progress = Path.Combine("m3mp", "uncompressed", $"{i}.m3mp");
                                        break;
                                    case (int)Enums.FileType.FSB:
                                        string fsbName = Path.Combine(folder, "sound", "fsb", $"{i}.fsb");
                                        Directory.CreateDirectory(Path.GetDirectoryName(fsbName));
                                        IO.ReadWriteCunkData(blobsetContent_br, fsbName, (int)mainUnCompressedSize);
                                        progress = Path.Combine("sound", "fsb", $"{i}.fsb");
                                        break;
                                    case (int)Enums.FileType.BANK:
                                    case (int)Enums.FileType.WAV:
                                        string audioType = "bank";

                                        switch ((Enums.Game)Global.gameInfo.GameId)
                                        {
                                            case Enums.Game.AFLL:
                                            case Enums.Game.DBC14:
                                            case Enums.Game.RLL2:
                                                audioType = "wav";
                                                break;
                                        }

                                        string audioName = Path.Combine(folder, "sound", audioType, $"{i}." + audioType);
                                        Directory.CreateDirectory(Path.GetDirectoryName(audioName));
                                        IO.ReadWriteCunkData(blobsetContent_br, audioName, (int)mainUnCompressedSize);
                                        progress = Path.Combine("sound", audioType, $"{i}." + audioType);
                                        break;
                                    case (int)Enums.FileType.PNG:
                                        string pngName = Path.Combine(folder, "png", $"{i}.png");
                                        Directory.CreateDirectory(Path.GetDirectoryName(pngName));
                                        IO.ReadWriteCunkData(blobsetContent_br, pngName, (int)mainUnCompressedSize);
                                        progress = Path.Combine("png", $"{i}.png");
                                        break;
                                    default:
                                        if (!skipUnknownFiles)
                                        {
                                            string unknownName = Path.Combine(folder, "unknown", "mainuncompressed_vramuncompressed", $"{i}.dat");
                                            Directory.CreateDirectory(Path.GetDirectoryName(unknownName));
                                            IO.ReadWriteCunkData(blobsetContent_br, unknownName, (int)mainUnCompressedSize);
                                            progress = Path.Combine("unknown", "mainuncompressed_vramuncompressed", $"{i}.dat");
                                        }
                                        break;
                                }
                            }
                        }
                        else
                        {
                            blobsetContent_br.Position = mainFinalOffSet + 20; // Offset to convant2 string.

                            if (blobsetContent_br.ReadString(4, Endian.Little) == "conv") // Makes sure it's a mini DDS TXPK.
                            {
                                blobsetContent_br.Position = mainFinalOffSet;

                                Mini_TXPK? mini_TXPK = new();
                                mini_TXPK.Deserialize(blobsetContent_br);

                                string ddsFilePath = Path.Combine(folder, mini_TXPK.DDSFilePath);
                                string ddsFilePathDir = Path.Combine(folder, Path.GetDirectoryName(mini_TXPK.DDSFilePath));
                                Directory.CreateDirectory(ddsFilePathDir);

                                writer = new(ddsFilePath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                if (Global.isBigendian)
                                    LZMA_IO.DecompressChunkAndWriteDDS(blobsetContent_br, writer, mini_TXPK, (int)vramUnCompressedSize);
                                else
                                    LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                if (writer != null) { writer.Dispose(); writer = null; }
                                progress = mini_TXPK.DDSFilePath;
                            }
                            else
                            {
                                blobsetContent_br.Position = mainFinalOffSet;
                                int magic = blobsetContent_br.ReadInt32();
                                blobsetContent_br.Position = mainFinalOffSet;

                                switch (magic)
                                {
                                    case (int)Enums.FileType.KPXT:
                                    case (int)Enums.FileType.TXPK:
                                        string txpkTempPath = Path.Combine(Global.currentPath, "temp", $"{i}.txpk");

                                        if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }

                                        writer = new(txpkTempPath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                        byte[] txpkHeader = blobsetContent_br.ReadBytes((int)mainUnCompressedSize, Endian.Little);

                                        writer.Write(txpkHeader, 0, (int)mainUnCompressedSize);
                                        LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                        IO.TXPK_DDS_Extractor(txpkTempPath, folder, (int)mainUnCompressedSize);

                                        if (writer != null) { writer.Dispose(); writer = null; }
                                        if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }
                                        progress = Path.Combine("dds_txpk", $"{i}.txpk");
                                        break;
                                    default:
                                        if (!skipUnknownFiles)
                                        {
                                            if (Enums.Game.TTC == (Enums.Game)Global.gameInfo.GameId)
                                            {
                                                byte[]? miniTxpkHeader = LZMA_IO.DecompressChunkAndRead(blobsetContent_br, (int)mainUnCompressedSize);

                                                Reader? miniTXPK_br = new(miniTxpkHeader);

                                                Mini_TXPK? mini_TXPK = new();
                                                mini_TXPK.Deserialize(miniTXPK_br);

                                                if (miniTXPK_br != null) { miniTXPK_br.Close(); miniTXPK_br = null; }

                                                string ddsFilePath = Path.Combine(folder, mini_TXPK.DDSFilePath);
                                                string ddsFilePathDir = Path.Combine(folder, Path.GetDirectoryName(mini_TXPK.DDSFilePath));
                                                Directory.CreateDirectory(ddsFilePathDir);

                                                writer = new(ddsFilePath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                                if (Global.isBigendian)
                                                    LZMA_IO.DecompressChunkAndWriteDDS(blobsetContent_br, writer, mini_TXPK, (int)vramUnCompressedSize);
                                                else
                                                    LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                                if (writer != null) { writer.Dispose(); writer = null; }
                                                progress = mini_TXPK.DDSFilePath;
                                            }
                                            else
                                            {
                                                string unknownName = Path.Combine(folder, "unknown", "mainuncompressed_vramcompressed", $"{i}.dat");
                                                Directory.CreateDirectory(Path.GetDirectoryName(unknownName));

                                                writer = new(unknownName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                                byte[] unknownHeader = blobsetContent_br.ReadBytes((int)mainUnCompressedSize, Endian.Little);
                                                writer.Write(unknownHeader, 0, (int)mainUnCompressedSize);
                                                LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                                progress = Path.Combine("unknown", "mainuncompressed_vramcompressed", $"{i}.dat"); ;
                                                if (writer != null) { writer.Dispose(); writer = null; }
                                            }
                                        }
                                        break;
                                }
                            }
                        }
                    }
                    else
                    {
                        blobsetContent_br.Position = mainFinalOffSet;
                        int chunkCount = blobsetContent_br.ReadInt32();
                        int[] chunkCompressedSize = new int[chunkCount];

                        for (int j = 0; j < chunkCount; j++)
                        {
                            chunkCompressedSize[j] = blobsetContent_br.ReadInt32();
                            chunkCompressedSize[j] = chunkCompressedSize[j] -= 4;
                        }

                        int chunkUnCompressedSize = blobsetContent_br.ReadInt32();

                        int magic = 0;

                        if (chunkCompressedSize[0] == chunkUnCompressedSize)
                            magic = blobsetContent_br.ReadInt32();
                        else
                        {
                            byte[] firstChunk = blobsetContent_br.ReadBytes(chunkCompressedSize[0], Endian.Little);
                            magic = LZMA_IO.DecompressAndReadMagic(firstChunk, chunkUnCompressedSize);
                        }

                        blobsetContent_br.Position = mainFinalOffSet;

                        if (vramCompressedSize == vramUnCompressedSize)
                        {
                            switch (magic)
                            {
                                case (int)Enums.FileType.PM3M:
                                case (int)Enums.FileType.M3MP:
                                    string m3mpTempPath = Path.Combine(Global.currentPath, "temp", $"{i}.m3mp");

                                    if (File.Exists(m3mpTempPath)) { File.Delete(m3mpTempPath); }

                                    writer = new(m3mpTempPath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                    LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                    if (writer != null) { writer.Dispose(); writer = null; }

                                    LZMA_IO.M3MPDecompressAndWrite(m3mpTempPath, folder);

                                    if (File.Exists(m3mpTempPath)) { File.Delete(m3mpTempPath); }
                                    progress = Path.Combine("m3mp", "compressed", $"{i}.m3mp");
                                    break;
                                case (int)Enums.FileType.MINI:
                                    int miniTxpkChunkCount = blobsetContent_br.ReadInt32();
                                    int[] miniTxpkChunkCompressedSize = new int[miniTxpkChunkCount];

                                    for (int j = 0; j < miniTxpkChunkCount; j++)
                                    {
                                        miniTxpkChunkCompressedSize[j] = blobsetContent_br.ReadInt32();
                                        miniTxpkChunkCompressedSize[j] = miniTxpkChunkCompressedSize[j] -= 4;
                                    }

                                    int miniTxpkChunkUnCompressedSize = blobsetContent_br.ReadInt32();

                                    byte[]? miniTxpkHeader = null;

                                    if (miniTxpkChunkCompressedSize[0] == miniTxpkChunkUnCompressedSize)
                                        miniTxpkHeader = blobsetContent_br.ReadBytes(miniTxpkChunkUnCompressedSize, Endian.Little);
                                    else
                                    {
                                        byte[] miniTxpkData = blobsetContent_br.ReadBytes(miniTxpkChunkCompressedSize[0], Endian.Little);
                                        miniTxpkHeader = LZMA_IO.DecompressAndRead(miniTxpkData, miniTxpkChunkUnCompressedSize);
                                    }

                                    Reader? miniTXPK_br = new(miniTxpkHeader);

                                    Mini_TXPK? mini_TXPK = new();
                                    mini_TXPK.Deserialize(miniTXPK_br);

                                    string ddsFilePath = Path.Combine(folder, mini_TXPK.DDSFilePath);
                                    string ddsFilePathDir = Path.Combine(folder, Path.GetDirectoryName(mini_TXPK.DDSFilePath));
                                    Directory.CreateDirectory(ddsFilePathDir);

                                    int dataSize = (int)(mainUnCompressedSize + vramUnCompressedSize - miniTXPK_br.Position);

                                    if (Global.isBigendian)
                                        IO.ReadWriteCunkDDSData(vramUnCompressedSize > 0 ? blobsetContent_br : miniTXPK_br, ddsFilePath, dataSize, mini_TXPK);
                                    else
                                        IO.ReadWriteCunkData(vramUnCompressedSize > 0 ? blobsetContent_br : miniTXPK_br, ddsFilePath, dataSize);

                                    if (miniTXPK_br != null) { miniTXPK_br.Close(); miniTXPK_br = null; }
                                    progress = mini_TXPK.DDSFilePath;
                                    break;
                                case (int)Enums.FileType.BSB_BE:
                                case (int)Enums.FileType.BSB:
                                    string bsbName = Path.Combine(folder, "ui", "bsb", $"{i}.bsb");
                                    Directory.CreateDirectory(Path.GetDirectoryName(bsbName));

                                    writer = new(bsbName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                    LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                    progress = Path.Combine("ui", "bsb", $"{i}.bsb");

                                    if (writer != null) { writer.Dispose(); writer = null; }
                                    break;
                                default:
                                    if (!skipUnknownFiles)
                                    {
                                        string unknownName = Path.Combine(folder, "unknown", "maincompressed", $"{i}.dat");
                                        Directory.CreateDirectory(Path.GetDirectoryName(unknownName));

                                        writer = new(unknownName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                        LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                        progress = Path.Combine("unknown", "maincompressed", $"{i}.dat"); ;

                                        if (writer != null) { writer.Dispose(); writer = null; }
                                    }
                                    break;
                            }
                        }
                        else
                        {
                            switch (magic)
                            {
                                case (int)Enums.FileType.MINI:
                                    byte[]? miniTxpkHeader = LZMA_IO.DecompressChunkAndRead(blobsetContent_br, (int)mainUnCompressedSize);

                                    Reader? miniTXPK_br = new(miniTxpkHeader);

                                    Mini_TXPK? mini_TXPK = new();
                                    mini_TXPK.Deserialize(miniTXPK_br);

                                    if (miniTXPK_br != null) { miniTXPK_br.Close(); miniTXPK_br = null; }

                                    string ddsFilePath = Path.Combine(folder, mini_TXPK.DDSFilePath);
                                    string ddsFilePathDir = Path.Combine(folder, Path.GetDirectoryName(mini_TXPK.DDSFilePath));
                                    Directory.CreateDirectory(ddsFilePathDir);

                                    writer = new(ddsFilePath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                    if (Global.isBigendian)
                                        LZMA_IO.DecompressChunkAndWriteDDS(blobsetContent_br, writer, mini_TXPK, (int)vramUnCompressedSize);
                                    else
                                        LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                    if (writer != null) { writer.Dispose(); writer = null; }
                                    progress = mini_TXPK.DDSFilePath;
                                    break;
                                case (int)Enums.FileType.KPXT:
                                case (int)Enums.FileType.TXPK:
                                    string txpkTempPath = Path.Combine(Global.currentPath, "temp", $"{i}.txpk");

                                    if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }

                                    writer = new(txpkTempPath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                    LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);
                                    LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                    IO.TXPK_DDS_Extractor(txpkTempPath, folder, (int)mainUnCompressedSize);

                                    if (writer != null) { writer.Dispose(); writer = null; }
                                    if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }
                                    progress = Path.Combine("dds_txpk", $"{i}.txpk");
                                    break;
                                case (int)Enums.FileType.BSB_BE:
                                case (int)Enums.FileType.BSB:
                                    string bsbName = Path.Combine(folder, "ui", "bsb", $"{i}.bsb");
                                    Directory.CreateDirectory(Path.GetDirectoryName(bsbName));

                                    writer = new(bsbName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                    LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);
                                    LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                    progress = Path.Combine("ui", "bsb", $"{i}.bsb");

                                    if (writer != null) { writer.Dispose(); writer = null; }
                                    break;
                                default:
                                    if (!skipUnknownFiles)
                                    {
                                        string unknownName = Path.Combine(folder, "unknown", "maincompressed_vramcompressed", $"{i}.dat");
                                        Directory.CreateDirectory(Path.GetDirectoryName(unknownName));

                                        writer = new(unknownName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                        LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);
                                        LZMA_IO.DecompressChunkAndWrite(blobsetContent_br, writer);

                                        progress = Path.Combine("unknown", "maincompressed_vramcompressed", $"{i}.dat");

                                        if (writer != null) { writer.Dispose(); writer = null; }
                                    }
                                    break;
                            }
                        }
                    }

                    int percentProgress = (i + 1) * 100 / (int)blobset.FilesCount;
                    Extract_bgw.ReportProgress(percentProgress, progress);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : {ex.Message}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return true;
            }
            finally
            {
                if (writer != null) { writer.Dispose(); writer = null; }
                DeleteAllTmpFiles(Path.Combine(Global.currentPath, "temp"));
            }

            return false;
        }
        #endregion

        #region Extract Blobset Version 2
        /// <summary>
        /// Read Blobset header and than extract's file's to a folder - version 2.
        /// </summary>
        /// <param name="blobsetfile">File path to file blobset file</param>
        /// <param name="Extract_bgw">The extract Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		05/07/2025	Created
        /// </history>
        public static bool BlobsetV2(string blobsetfile, BackgroundWorker Extract_bgw)
        {
            bool error = true;
            return error;
        }
        #endregion

        #region Extract Blobset Version 3
        /// <summary>
        /// Read Blobset header and than extract's file's to a folder - version 3.
        /// </summary>
        /// <param name="blobsetfile">File path to file blobset file</param>
        /// <param name="Extract_bgw">The extract Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		05/07/2025	Created
        /// </history>
        public static bool BlobsetV3(string blobsetfile, BackgroundWorker Extract_bgw)
        {
            bool error = true;
            return error;
        }
        #endregion

        #region Extract Blobset Version 4
        /// <summary>
        /// Read Blobset header and than extract's file's to a folder - version 4.
        /// </summary>
        /// <param name="blobsetfile">File path to blobset file</param>
        /// <param name="Extract_bgw">The extract Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		01/07/2025	Created
        /// </history>
        public static bool BlobsetV4(string blobsetfile, BackgroundWorker Extract_bgw)
        {
            Reader? blobsetContent_br = null;
            FileStream? writer = null;
            string progress = string.Empty;
            string _filePath = string.Empty;

            try
            {
                if (Global.blobsetHeaderData == null)
                {
                    MessageBox.Show("Something went wrong reading the blobset", "Blobset Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return true;
                }

                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];
                string blobsetFilename = Path.GetFileName(blobsetfile);

                BlobsetFile blobset = Global.blobsetHeaderData;
                string folder = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, blobsetFilename);

                if (Directory.Exists(folder))
                {
                    Directory.Delete(folder, true);
                    Directory.CreateDirectory(folder);
                }
                else
                    Directory.CreateDirectory(folder);

                IniFile settings = new(Path.Combine(Global.currentPath, "Settings.ini"));
                bool skipUnknownFiles = "false" == settings.Read("SkipUnknown", "Settings") ? false : true;

                for (int i = 0; i < blobset.FilesCount; i++)
                {
                    string folderName = blobset.Entries[i].FolderHashName;
                    string fileName = blobset.Entries[i].FileHashName;
                    string filePath = $@"{Functions.GetDirectory(blobsetfile)}\{folderName}\{fileName}";
                    _filePath = filePath;

                    if (File.Exists(filePath))
                    {
                        FileStream blobsetContentFs = new(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                        blobsetContent_br = new(blobsetContentFs);

                        uint mainCompressedSize = blobset.Entries[i].MainCompressedSize;
                        uint mainUnCompressedSize = blobset.Entries[i].MainUnCompressedSize;
                        uint vramFinalOffSet = mainCompressedSize;
                        uint vramCompressedSize = blobset.Entries[i].VramCompressedSize;
                        uint vramUnCompressedSize = blobset.Entries[i].VramUnCompressedSize;

                        if (blobsetContent_br.Length > 4)
                        {
                            if (mainCompressedSize == mainUnCompressedSize)
                            {
                                if (vramCompressedSize == vramUnCompressedSize)
                                {
                                    blobsetContent_br.Position = 20; // Offset to convant2 string.

                                    if (blobsetContent_br.ReadString(4, Endian.Little) == "conv") // Makes sure it's a mini DDS TXPK.
                                    {
                                        blobsetContent_br.Position = 0;

                                        Mini_TXPK? mini_TXPK = new();
                                        mini_TXPK.Deserialize(blobsetContent_br);

                                        string ddsFilePath = Path.Combine(folder, mini_TXPK.DDSFilePath);
                                        string ddsFilePathDir = Path.Combine(folder, Path.GetDirectoryName(mini_TXPK.DDSFilePath));
                                        Directory.CreateDirectory(ddsFilePathDir);
                                        IO.ReadWriteData(blobsetContent_br, ddsFilePath);
                                        progress = mini_TXPK.DDSFilePath;
                                    }
                                    else
                                    {
                                        blobsetContent_br.Position = 0;
                                        int magic = blobsetContent_br.ReadInt32();
                                        blobsetContent_br.Position = 0;

                                        switch (magic)
                                        {
                                            case (int)Enums.FileType.M3MP:
                                                ZSTD_IO.M3MPDecompressAndWrite(filePath, folder, Extract_bgw);
                                                progress = Path.Combine("m3mp", "uncompressed", $"{i}.m3mp");
                                                break;
                                            case (int)Enums.FileType.WiseBNK:
                                                string bnkName = Path.Combine(folder, "sound", "bnk", $"{i}.bnk");
                                                Directory.CreateDirectory(Path.GetDirectoryName(bnkName));
                                                IO.ReadWriteData(blobsetContent_br, bnkName);
                                                progress = Path.Combine("sound", "bnk", $"{i}.bnk");
                                                break;
                                            case (int)Enums.FileType.WiseWEM:
                                                string wemName = Path.Combine(folder, "sound", "wem", $"{i}.wem");
                                                Directory.CreateDirectory(Path.GetDirectoryName(wemName));
                                                IO.ReadWriteData(blobsetContent_br, wemName);
                                                progress = Path.Combine("sound", "wem", $"{i}.wem");
                                                break;
                                            default:
                                                if (!skipUnknownFiles)
                                                {
                                                    string unknownName = Path.Combine(folder, "unknown", "mainuncompressed_vramuncompressed", $"{i}.dat");
                                                    Directory.CreateDirectory(Path.GetDirectoryName(unknownName));
                                                    IO.ReadWriteData(blobsetContent_br, unknownName);
                                                    progress = Path.Combine("unknown", "mainuncompressed_vramuncompressed", $"{i}.dat");
                                                }
                                                break;
                                        }
                                    }
                                }
                                else
                                {
                                    blobsetContent_br.Position = 20; // Offset to convant2 string.

                                    if (blobsetContent_br.ReadString(4, Endian.Little) == "conv") // Makes sure it's a mini DDS TXPK.
                                    {
                                        blobsetContent_br.Position = 0;

                                        Mini_TXPK? mini_TXPK = new();
                                        mini_TXPK.Deserialize(blobsetContent_br);

                                        string ddsFilePath = Path.Combine(folder, mini_TXPK.DDSFilePath);
                                        string ddsFilePathDir = Path.Combine(folder, Path.GetDirectoryName(mini_TXPK.DDSFilePath));
                                        Directory.CreateDirectory(ddsFilePathDir);

                                        writer = new(ddsFilePath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                        ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                        if (writer != null) { writer.Dispose(); writer = null; }
                                        progress = mini_TXPK.DDSFilePath;
                                    }
                                    else
                                    {
                                        blobsetContent_br.Position = 0;

                                        string txpkTempPath = Path.Combine(Global.currentPath, "temp", $"{i}.txpk");

                                        if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }

                                        writer = new(txpkTempPath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                        byte[] txpkHeader = blobsetContent_br.ReadBytes((int)mainUnCompressedSize);

                                        writer.Write(txpkHeader, 0, (int)mainUnCompressedSize);
                                        ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                        progress = IO.TXPK_DDS_Extractor(txpkTempPath, folder, (int)mainUnCompressedSize);

                                        if (writer != null) { writer.Dispose(); writer = null; }
                                        if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }
                                    }
                                }
                            }
                            else
                            {
                                int compressedSize = blobsetContent_br.ReadInt32();
                                int tmp = compressedSize -= 4;
                                compressedSize = tmp;

                                bool isCompressed = true;

                                byte[] firstChunk = blobsetContent_br.ReadBytes(compressedSize);

                                byte[] ZstdMagicArray = [firstChunk[0], firstChunk[1], firstChunk[2], firstChunk[3]];
                                uint ZstdMagic = BitConverter.ToUInt32(ZstdMagicArray);

                                if (ZstdMagic != 4247762216)
                                    isCompressed = false;

                                int magic = ZSTD_IO.DecompressAndReadMagic(firstChunk, isCompressed);

                                blobsetContent_br.Position = 0;

                                if (vramCompressedSize == vramUnCompressedSize)
                                {
                                    switch (magic)
                                    {
                                        case (int)Enums.FileType.M3MP:
                                            string m3mpTempPath = Path.Combine(Global.currentPath, "temp", $"{i}.m3mp");

                                            if (File.Exists(m3mpTempPath)) { File.Delete(m3mpTempPath); }

                                            writer = new(m3mpTempPath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                            ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                            if (writer != null) { writer.Dispose(); writer = null; }

                                            ZSTD_IO.M3MPDecompressAndWrite(m3mpTempPath, folder, Extract_bgw);

                                            progress = Path.Combine("m3mp", "compressed", $"{i}.m3mp");
                                            if (File.Exists(m3mpTempPath)) { File.Delete(m3mpTempPath); }
                                            break;
                                        default:
                                            if (!skipUnknownFiles)
                                            {
                                                string unknownName = Path.Combine(folder, "unknown", "maincompressed", $"{i}.dat");
                                                Directory.CreateDirectory(Path.GetDirectoryName(unknownName));

                                                writer = new(unknownName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                                ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                                progress = Path.Combine("unknown", "maincompressed", $"{i}.dat");
                                                if (writer != null) { writer.Dispose(); writer = null; }
                                            }
                                            break;
                                    }
                                }
                                else
                                {
                                    switch (magic)
                                    {
                                        case (int)Enums.FileType.MINI:

                                            int miniTXPKHeaderCompressedSize = blobsetContent_br.ReadInt32();
                                            int miniTXPKHeaderTmp = miniTXPKHeaderCompressedSize -= 4;
                                            miniTXPKHeaderCompressedSize = miniTXPKHeaderTmp;

                                            byte[] headerChunk = blobsetContent_br.ReadBytes(miniTXPKHeaderCompressedSize);

                                            bool isMiniTXPKHeaderCompressed = true;

                                            byte[] ZstdMagicArrayHeader = [headerChunk[0], headerChunk[1], headerChunk[2], headerChunk[3]];
                                            uint ZstdMagicHeader = BitConverter.ToUInt32(ZstdMagicArrayHeader);

                                            if (ZstdMagicHeader != 4247762216)
                                                isMiniTXPKHeaderCompressed = false;

                                            byte[] miniHeader = ZSTD_IO.DecompressAndRead(headerChunk, isMiniTXPKHeaderCompressed);

                                            Reader miniTXPK_br = new(miniHeader);

                                            Mini_TXPK? mini_TXPK = new();
                                            mini_TXPK.Deserialize(miniTXPK_br);

                                            if (miniTXPK_br != null) { miniTXPK_br.Close(); miniTXPK_br = null; }

                                            string ddsFilePath = Path.Combine(folder, mini_TXPK.DDSFilePath);
                                            string ddsFilePathDir = Path.Combine(folder, Path.GetDirectoryName(mini_TXPK.DDSFilePath));
                                            Directory.CreateDirectory(ddsFilePathDir);

                                            writer = new(ddsFilePath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                            ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                            if (writer != null) { writer.Dispose(); writer = null; }
                                            progress = mini_TXPK.DDSFilePath;
                                            break;
                                        case (int)Enums.FileType.TXPK:
                                            string txpkTempPath = Path.Combine(Global.currentPath, "temp", $"{i}.txpk");

                                            if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }

                                            writer = new(txpkTempPath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                            ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                            progress = IO.TXPK_DDS_Extractor(txpkTempPath, folder, (int)mainUnCompressedSize);

                                            if (writer != null) { writer.Dispose(); writer = null; }
                                            if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }
                                            break;
                                        default:
                                            if (!skipUnknownFiles)
                                            {
                                                string unknownName = Path.Combine(folder, "unknown", "maincompressed_vramcompressed", $"{i}.dat");
                                                Directory.CreateDirectory(Path.GetDirectoryName(unknownName));

                                                writer = new(unknownName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                                ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                                progress = Path.Combine("unknown", "maincompressed_vramcompressed", $"{i}.dat");

                                                if (writer != null) { writer.Dispose(); writer = null; }
                                            }
                                            break;
                                    }
                                }
                            }
                        }
                        else
                        {
                            if (!skipUnknownFiles)
                            {
                                string unknownName = Path.Combine(folder, "unknown", "mainuncompressed", $"{i}.dat");
                                Directory.CreateDirectory(Path.GetDirectoryName(unknownName));
                                IO.ReadWriteData(blobsetContent_br, unknownName);
                                progress = Path.Combine("unknown", "mainuncompressed", $"{i}.dat");
                            }
                            break;
                        }
                    }

                    int percentProgress = (i + 1) * 100 / (int)blobset.FilesCount;
                    Extract_bgw.ReportProgress(percentProgress, progress);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : \n\nFile: {_filePath} \n\n {ex.Message}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return true;
            }
            finally
            {
                if (writer != null) { writer.Dispose(); writer = null; }
                DeleteAllTmpFiles(Path.Combine(Global.currentPath, "temp"));
            }
            return false;
        }
        #endregion

        private static void DeleteAllTmpFiles(string folderPath)
        {
            if (Directory.Exists(folderPath))
            {
                string[] files = Directory.GetFiles(folderPath);
                foreach (string file in files)
                {
                    File.Delete(file);
                }
            }
        }
    }
}

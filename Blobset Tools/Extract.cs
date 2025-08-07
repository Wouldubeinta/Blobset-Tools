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
        public static void BlobsetV1(string blobsetfile, BackgroundWorker Extract_bgw)
        {

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
        public static void BlobsetV2(string blobsetfile, BackgroundWorker Extract_bgw)
        {

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
        public static void BlobsetV3(string blobsetfile, BackgroundWorker Extract_bgw)
        {

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
            bool error = false;

            try
            {
                if (Global.blobsetHeaderData == null)
                {
                    error = true;
                    MessageBox.Show("Something went wrong reading the blobset", "Blobset Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return error;
                }

                BlobsetFile blobset = Global.blobsetHeaderData;
                string folder = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\data-0.blobset\";

                if (Directory.Exists(folder))
                {
                    Directory.Delete(folder, true);
                    Directory.CreateDirectory(folder);
                }
                else
                    Directory.CreateDirectory(folder);

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

                                    if (blobsetContent_br.ReadString(4) == "conv") // Makes sure it's a mini DDS TXPK.
                                    {
                                        blobsetContent_br.Position = 0;

                                        Mini_TXPK? mini_TXPK = new();
                                        mini_TXPK.Deserialize(blobsetContent_br);

                                        if (Properties.Settings.Default.GameID == (int)Enums.Game.RL26)
                                            mini_TXPK.DDSFilePath = mini_TXPK.DDSFilePath.Replace(".badds", ".dds");

                                        string ddsFilePath = folder + mini_TXPK.DDSFilePath.Replace("/", @"\");
                                        string ddsFilePathDir = folder + Path.GetDirectoryName(mini_TXPK.DDSFilePath.Replace("/", @"\"));
                                        Directory.CreateDirectory(ddsFilePathDir);
                                        IO.ReadWriteData(blobsetContent_br, ddsFilePath);
                                        progress = mini_TXPK.DDSFilePath.Replace("/", @"\");
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
                                                break;
                                            case (int)Enums.FileType.WiseBNK:
                                                string bnkName = folder + @"\sound\bnk\" + i.ToString() + ".bnk";
                                                Directory.CreateDirectory(Path.GetDirectoryName(bnkName));
                                                IO.ReadWriteData(blobsetContent_br, bnkName);
                                                progress = @"sound\bnk\" + i.ToString() + ".bnk";
                                                break;
                                            case (int)Enums.FileType.WiseWEM:
                                                string wemName = folder + @"\sound\wem\" + i.ToString() + ".wem";
                                                Directory.CreateDirectory(Path.GetDirectoryName(wemName));
                                                IO.ReadWriteData(blobsetContent_br, wemName);
                                                progress = @"sound\wem\" + i.ToString() + ".wem";
                                                break;
                                            default:
                                                if (!Properties.Settings.Default.SkipUnknown)
                                                {
                                                    string unknownName = folder + @"\unknown\mainuncompressed_vramuncompressed\" + i.ToString() + ".dat";
                                                    Directory.CreateDirectory(Path.GetDirectoryName(unknownName));
                                                    IO.ReadWriteData(blobsetContent_br, unknownName);
                                                    progress = @"\unknown\mainuncompressed_vramuncompressed\" + i.ToString() + ".dat";
                                                }
                                                break;
                                        }
                                    }
                                }
                                else
                                {
                                    blobsetContent_br.Position = 20; // Offset to convant2 string.

                                    if (blobsetContent_br.ReadString(4) == "conv") // Makes sure it's a mini DDS TXPK.
                                    {
                                        blobsetContent_br.Position = 0;

                                        Mini_TXPK? mini_TXPK = new();
                                        mini_TXPK.Deserialize(blobsetContent_br);

                                        if (Properties.Settings.Default.GameID == (int)Enums.Game.RL26)
                                            mini_TXPK.DDSFilePath = mini_TXPK.DDSFilePath.Replace(".badds", ".dds");

                                        string ddsFilePath = folder + mini_TXPK.DDSFilePath.Replace("/", @"\");
                                        string ddsFilePathDir = folder + Path.GetDirectoryName(mini_TXPK.DDSFilePath.Replace("/", @"\"));
                                        Directory.CreateDirectory(ddsFilePathDir);

                                        writer = new(ddsFilePath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                        ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                        if (writer != null) { writer.Dispose(); writer = null; }
                                        progress = mini_TXPK.DDSFilePath.Replace("/", @"\");
                                    }
                                    else
                                    {
                                        blobsetContent_br.Position = 0;

                                        string txpkTempPath = Global.currentPath + @"\temp\" + i.ToString() + ".txpk";

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
                                            string m3mpTempPath = Global.currentPath + @"\temp\" + i.ToString() + ".m3mp";

                                            if (File.Exists(m3mpTempPath)) { File.Delete(m3mpTempPath); }

                                            writer = new(m3mpTempPath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                            ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                            if (writer != null) { writer.Dispose(); writer = null; }

                                            ZSTD_IO.M3MPDecompressAndWrite(m3mpTempPath, folder, Extract_bgw);

                                            if (File.Exists(m3mpTempPath)) { File.Delete(m3mpTempPath); }
                                            break;
                                        default:
                                            if (!Properties.Settings.Default.SkipUnknown)
                                            {
                                                string unknownName = folder + @"\unknown\maincompressed\" + i.ToString() + ".dat";
                                                Directory.CreateDirectory(Path.GetDirectoryName(unknownName));

                                                writer = new(unknownName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                                ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                                progress = @"unknown\maincompressed\" + i.ToString() + ".dat";

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

                                            if (Properties.Settings.Default.GameID == (int)Enums.Game.RL26)
                                                mini_TXPK.DDSFilePath = mini_TXPK.DDSFilePath.Replace(".badds", ".dds");

                                            if (miniTXPK_br != null) { miniTXPK_br.Close(); miniTXPK_br = null; }

                                            string ddsFilePath = folder + mini_TXPK.DDSFilePath.Replace("/", @"\");
                                            string ddsFilePathDir = folder + Path.GetDirectoryName(mini_TXPK.DDSFilePath.Replace("/", @"\"));
                                            Directory.CreateDirectory(ddsFilePathDir);

                                            writer = new(ddsFilePath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                            ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                            if (writer != null) { writer.Dispose(); writer = null; }
                                            progress = mini_TXPK.DDSFilePath.Replace("/", @"\");
                                            break;
                                        case (int)Enums.FileType.TXPK:
                                            string txpkTempPath = Global.currentPath + @"\temp\" + i.ToString() + ".txpk";

                                            if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }

                                            writer = new(txpkTempPath, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                            ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                            progress = IO.TXPK_DDS_Extractor(txpkTempPath, folder, (int)mainUnCompressedSize);

                                            if (writer != null) { writer.Dispose(); writer = null; }
                                            if (File.Exists(txpkTempPath)) { File.Delete(txpkTempPath); }
                                            break;
                                        default:
                                            if (!Properties.Settings.Default.SkipUnknown)
                                            {
                                                string unknownName = folder + @"\unknown\maincompressed_vramcompressed\" + i.ToString() + ".dat";
                                                Directory.CreateDirectory(Path.GetDirectoryName(unknownName));

                                                writer = new(unknownName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                                                ZSTD_IO.DecompressChunk(blobsetContent_br, writer);

                                                progress = @"unknown\maincompressed_vramcompressed\" + i.ToString() + ".dat";

                                                if (writer != null) { writer.Dispose(); writer = null; }
                                            }
                                            break;
                                    }
                                }
                            }
                        }
                    }

                    int percentProgress = 100 * i / (int)blobset.FilesCount;
                    Extract_bgw.ReportProgress(percentProgress, progress);
                }
            }
            catch (Exception arg)
            {
                error = true;
                MessageBox.Show("Error occurred, report it to Wouldy : \n\nFile: " + _filePath + "\n\n" + arg, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (writer != null) { writer.Dispose(); writer = null; }

                if (File.Exists(Global.currentPath + @"\temp\m3mpData.tmp"))
                    File.Delete(Global.currentPath + @"\temp\m3mpData.tmp");

                if (File.Exists(Global.currentPath + @"\temp\txpkData.tmp"))
                    File.Delete(Global.currentPath + @"\temp\txpkData.tmp");
            }
            return error;
        }
        #endregion


    }
}

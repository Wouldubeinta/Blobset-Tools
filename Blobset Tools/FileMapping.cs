using BlobsetIO;
using PackageIO;
using System.ComponentModel;

namespace Blobset_Tools
{
    /// <summary>
    /// FileMapping is used on BigAnt games to map all the file locations.
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
    public class FileMapping
    {
        #region Fields
        private int _FilesCount = 0;
        private readonly List<Entry> _Entries;
        #endregion

        public FileMapping()
        {
            _Entries = new List<Entry>();
        }

        public class Entry
        {
            public int Index = 0;
            public string FilePath = string.Empty;
            public string FolderHash = string.Empty;
            public string FileNameHash = string.Empty;
        }

        #region Properties
        public int FilesCount
        {
            get { return _FilesCount; }
            set { _FilesCount = value; }
        }

        public List<Entry> Entries
        {
            get { return _Entries; }
        }
        #endregion

        #region Read
        /// <summary>
        /// Read's the BlobsetFileMapping.bin for file mapping v4.
        /// </summary>
        /// <param name="br">Binary Reader Stream</param>
        /// <history>
        /// [Wouldubeinta]		01/07/2025	Created
        /// </history>
        public void Read(Reader br)
        {
            FilesCount = br.ReadInt32();

            List<Entry> entries = new();

            for (int i = 0; i < FilesCount; i++)
            {
                var entry = new Entry();
                entry.Index = br.ReadInt32();
                int filePathSize = br.ReadByte();
                entry.FilePath = br.ReadString(filePathSize);
                int folderHashSize = br.ReadByte();
                entry.FolderHash = br.ReadString(folderHashSize);
                int fileNameHashSize = br.ReadByte();
                entry.FileNameHash = br.ReadString(fileNameHashSize);
                entries.Add(entry);
            }

            Entries.Clear();
            Entries.AddRange(entries);
        }
        #endregion

        #region WriteV1
        /// <summary>
        /// Write's the BlobsetFileMapping.bin for file mapping version 1.
        /// </summary>
        /// <param name="blobsetfile">File path to blobset file</param>
        /// <param name="FileMapping_bgw">The extract Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		01/07/2025	Created
        /// </history>
        public static bool WriteV1(string blobsetfile, BackgroundWorker FileMapping_bgw)
        {
            Reader? blobsetContent_br = null;
            FileStream? writer = null;
            BinaryWriter? bw = null;
            string progress = string.Empty;
            int fileMappingSize = 0;
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

                BlobsetFile? blobset = Global.blobsetHeaderData;

                string BlobsetFileMapping = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\data\BlobsetFileMapping.bin";
                string TxpkFileList = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\TXPK_File_List.csv";
                string M3mpFileList = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\M3MP_File_List.csv";

                if (File.Exists(BlobsetFileMapping))
                    File.Delete(BlobsetFileMapping);

                writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                bw = new(writer);

                bw.Write(fileMappingSize);
                bw.Flush();
                if (bw != null) { bw.Close(); bw = null; }

                if (File.Exists(TxpkFileList))
                    File.Delete(TxpkFileList);

                if (File.Exists(M3mpFileList))
                    File.Delete(M3mpFileList);

                for (int i = 0; i < blobset.FilesCount; i++)
                {
                    string folderName = blobset.Entries[i].FolderHashName;
                    string fileName = blobset.Entries[i].FileHashName;
                    string filePath = blobsetfile.Replace("0.blob", blobset.Entries[i].BlobSetNumber + ".blob"); ;
                    _filePath = filePath;

                    FileStream blobsetContentFs = new(filePath, FileMode.Open, FileAccess.ReadWrite);
                    blobsetContent_br = new(blobsetContentFs);

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
                            blobsetContent_br.Position = mainFinalOffSet;

                            int magic = blobsetContent_br.ReadInt32();

                            blobsetContent_br.Position = mainFinalOffSet;

                            switch (magic)
                            {
                                case (int)Enums.FileType.FSB:
                                    string fsbName = @"sound\" + i.ToString() + ".fsb";

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(fsbName);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = fsbName;
                                    if (bw != null) { bw.Close(); bw = null; }
                                    break;
                                case (int)Enums.FileType.FEV1:
                                    string fevName = @"sound\AFLL.fev";

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(fevName);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = fevName;
                                    if (bw != null) { bw.Close(); bw = null; }
                                    break;
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
                                byte[] firstChunk = blobsetContent_br.ReadBytes(chunkCompressedSize[0]);
                                magic = LZMA_IO.DecompressAndReadMagic(firstChunk, chunkUnCompressedSize);
                            }

                            blobsetContent_br.Position = mainFinalOffSet;

                            switch (magic)
                            {
                                case (int)Enums.FileType.TXPK:
                                    string txpkName = @"dds_txpk\" + i.ToString() + ".txpk";

                                    int txpkChunkCount = blobsetContent_br.ReadInt32();
                                    int[] txpkChunkCompressedSize = new int[txpkChunkCount];

                                    for (int j = 0; j < txpkChunkCount; j++)
                                    {
                                        txpkChunkCompressedSize[j] = blobsetContent_br.ReadInt32();
                                        txpkChunkCompressedSize[j] = txpkChunkCompressedSize[j] -= 4;
                                    }

                                    int txpkChunkUnCompressedSize = blobsetContent_br.ReadInt32();
                                    byte[]? txpkHeader = null;

                                    if (txpkChunkCompressedSize[0] == txpkChunkUnCompressedSize)
                                        txpkHeader = blobsetContent_br.ReadBytes(txpkChunkUnCompressedSize);
                                    else
                                    {
                                        byte[] txpkData = blobsetContent_br.ReadBytes(txpkChunkCompressedSize[0]);
                                        txpkHeader = LZMA_IO.DecompressAndRead(txpkData, txpkChunkUnCompressedSize);
                                    }

                                    Reader? txpk_br = new(txpkHeader);

                                    TXPK txpk = new();
                                    txpk.Deserialize(txpk_br);

                                    StreamWriter? txpk_sw = File.AppendText(TxpkFileList);

                                    foreach (var entry in txpk.Entries)
                                    {
                                        string DDSFilePath = entry.DDSFilePath.Replace("/", @"\");
                                        txpk_sw.WriteLine(DDSFilePath + ".dds" + "," + i.ToString());
                                        txpk_sw.Flush();
                                    }

                                    txpk_sw.WriteLine(string.Empty);
                                    txpk_sw.Flush();

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(txpkName);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = txpkName;
                                    if (txpk_br != null) { txpk_br.Close(); txpk_br = null; }
                                    if (txpk_sw != null) { txpk_sw.Dispose(); txpk_sw = null; }
                                    if (bw != null) { bw.Close(); bw = null; }
                                    break;

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
                            byte[] firstChunk = blobsetContent_br.ReadBytes(chunkCompressedSize[0]);
                            magic = LZMA_IO.DecompressAndReadMagic(firstChunk, chunkUnCompressedSize);
                        }

                        blobsetContent_br.Position = mainFinalOffSet;

                        if (vramCompressedSize != vramUnCompressedSize)
                        {
                            switch (magic)
                            {
                                case (int)Enums.FileType.BMf:
                                    string bmfName = @"font\" + i.ToString() + ".bmf";

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(bmfName);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = bmfName;
                                    if (bw != null) { bw.Close(); bw = null; }
                                    break;
                                case (int)Enums.FileType.BSB:
                                    string bsbName = @"bsb\" + i.ToString() + ".bsb";

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(bsbName);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = bsbName;
                                    break;
                                case (int)Enums.FileType.TXPK:
                                    string txpkName = @"dds_txpk\" + i.ToString() + ".txpk";

                                    int txpkChunkCount = blobsetContent_br.ReadInt32();
                                    int[] txpkChunkCompressedSize = new int[txpkChunkCount];

                                    for (int j = 0; j < txpkChunkCount; j++)
                                    {
                                        txpkChunkCompressedSize[j] = blobsetContent_br.ReadInt32();
                                        txpkChunkCompressedSize[j] = txpkChunkCompressedSize[j] -= 4;
                                    }

                                    int txpkChunkUnCompressedSize = blobsetContent_br.ReadInt32();
                                    byte[]? txpkHeader = null;

                                    if (txpkChunkCompressedSize[0] == txpkChunkUnCompressedSize)
                                        txpkHeader = blobsetContent_br.ReadBytes(txpkChunkUnCompressedSize);
                                    else
                                    {
                                        byte[] txpkData = blobsetContent_br.ReadBytes(txpkChunkCompressedSize[0]);
                                        txpkHeader = LZMA_IO.DecompressAndRead(txpkData, txpkChunkUnCompressedSize);
                                    }

                                    Reader? txpk_br = new(txpkHeader);

                                    TXPK txpk = new();
                                    txpk.Deserialize(txpk_br);

                                    StreamWriter? txpk_sw = File.AppendText(TxpkFileList);

                                    foreach (var entry in txpk.Entries)
                                    {
                                        string DDSFilePath = entry.DDSFilePath.Replace("/", @"\");
                                        txpk_sw.WriteLine(DDSFilePath + ".dds" + "," + i.ToString());
                                        txpk_sw.Flush();
                                    }

                                    txpk_sw.WriteLine(string.Empty);
                                    txpk_sw.Flush();

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(txpkName);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = txpkName;
                                    if (txpk_br != null) { txpk_br.Close(); txpk_br = null; }
                                    if (txpk_sw != null) { txpk_sw.Dispose(); txpk_sw = null; }
                                    if (bw != null) { bw.Close(); bw = null; }
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
                                        miniTxpkHeader = blobsetContent_br.ReadBytes(miniTxpkChunkUnCompressedSize);
                                    else
                                    {
                                        byte[] miniTxpkData = blobsetContent_br.ReadBytes(miniTxpkChunkCompressedSize[0]);
                                        miniTxpkHeader = LZMA_IO.DecompressAndRead(miniTxpkData, miniTxpkChunkUnCompressedSize);
                                    }

                                    Reader? minTxpk_br = new(miniTxpkHeader);

                                    Mini_TXPK mini_TXPK = new();
                                    mini_TXPK.Deserialize(minTxpk_br);

                                    if (minTxpk_br != null) { minTxpk_br.Close(); minTxpk_br = null; }

                                    string ddsFilePath = mini_TXPK.DDSFilePath.Replace("convant2-temp-intermediate/", "").Replace("/", @"\");

                                    mini_TXPK.DDSFilePath = ddsFilePath;

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(mini_TXPK.DDSFilePath);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = mini_TXPK.DDSFilePath;
                                    if (bw != null) { bw.Close(); bw = null; }
                                    break;
                                default:
                                    if (!Properties.Settings.Default.SkipUnknown)
                                    {
                                        string unknownFileName = @"unknown\maincompressed_vramcompressed\" + i.ToString() + ".dat";

                                        writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                        bw = new(writer);

                                        bw.Write(i);
                                        bw.Write(unknownFileName);
                                        bw.Write(folderName);
                                        bw.Write(fileName);
                                        bw.Flush();

                                        fileMappingSize++;

                                        progress = unknownFileName;
                                        if (bw != null) { bw.Close(); bw = null; }
                                    }
                                    break;
                            }
                        }
                        else
                        {
                            switch (magic)
                            {
                                case (int)Enums.FileType.BMf:
                                    string bmfName = @"font\" + i.ToString() + ".bmf";

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(bmfName);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = bmfName;
                                    if (bw != null) { bw.Close(); bw = null; }
                                    break;
                                case (int)Enums.FileType.BSB:
                                    string bsbName = @"bsb\" + i.ToString() + ".bsb";

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(bsbName);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = bsbName;
                                    break;
                                case (int)Enums.FileType.M3MP:
                                    string m3mpName = @"m3mp\" + i.ToString() + ".m3mp";

                                    List<byte[]> m3mpHeaderChunk = new(3);
                                    int m3mpHeaderSize = 0;

                                    int m3mpChunkCount = blobsetContent_br.ReadInt32();
                                    int[] m3mpChunkCompressedSize = new int[m3mpChunkCount];

                                    for (int j = 0; j < m3mpChunkCount; j++)
                                    {
                                        m3mpChunkCompressedSize[j] = blobsetContent_br.ReadInt32();
                                        m3mpChunkCompressedSize[j] = m3mpChunkCompressedSize[j] -= 4;
                                    }

                                    for (int j = 0; j < m3mpChunkCount; j++)
                                    {
                                        int m3mpChunkUnCompressedSize = blobsetContent_br.ReadInt32();

                                        if (m3mpChunkCompressedSize[j] == m3mpChunkUnCompressedSize)
                                            m3mpHeaderChunk.Add(blobsetContent_br.ReadBytes(m3mpChunkUnCompressedSize));
                                        else
                                        {
                                            byte[] m3mpData = blobsetContent_br.ReadBytes(m3mpChunkCompressedSize[j]);
                                            m3mpHeaderChunk.Add(LZMA_IO.DecompressAndRead(m3mpData, m3mpChunkUnCompressedSize));
                                        }

                                        m3mpHeaderSize += m3mpChunkUnCompressedSize;
                                    }

                                    byte[] m3mpHeader = IO.CombineDataChunks(m3mpHeaderChunk, m3mpHeaderSize);

                                    Reader? m3mp_br = new(m3mpHeader);

                                    M3MP m3mp = new();
                                    m3mp.Deserialize(m3mp_br);

                                    StreamWriter? m3mp_sw = File.AppendText(M3mpFileList);

                                    foreach (var entry in m3mp.UnCompressedEntries)
                                    {
                                        m3mp_sw.WriteLine(entry.FilePath.Replace("/", @"\") + "," + i.ToString());
                                        m3mp_sw.Flush();
                                    }

                                    m3mp_sw.WriteLine(string.Empty);
                                    m3mp_sw.Flush();

                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                    bw = new(writer);

                                    bw.Write(i);
                                    bw.Write(m3mpName);
                                    bw.Write(folderName);
                                    bw.Write(fileName);
                                    bw.Flush();

                                    fileMappingSize++;

                                    progress = m3mpName;
                                    if (m3mp_br != null) { m3mp_br.Close(); m3mp_br = null; }
                                    if (m3mp_sw != null) { m3mp_sw.Dispose(); m3mp_sw = null; }
                                    if (bw != null) { bw.Close(); bw = null; }
                                    break;
                                default:
                                    if (!Properties.Settings.Default.SkipUnknown)
                                    {
                                        string unknownFileName = @"unknown\maincompressed\" + i.ToString() + ".dat";

                                        writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                        bw = new(writer);

                                        bw.Write(i);
                                        bw.Write(unknownFileName);
                                        bw.Write(folderName);
                                        bw.Write(fileName);
                                        bw.Flush();

                                        fileMappingSize++;

                                        progress = unknownFileName;
                                        if (bw != null) { bw.Close(); bw = null; }
                                    }
                                    break;
                            }
                        }
                    }

                    int percentProgress = 100 * i / (int)blobset.FilesCount;
                    FileMapping_bgw.ReportProgress(percentProgress, progress);
                    if (blobsetContent_br != null) { blobsetContent_br.Close(); blobsetContent_br = null; }
                }

                writer = new(BlobsetFileMapping, FileMode.Open, FileAccess.Write, FileShare.ReadWrite);
                bw = new(writer);

                bw.BaseStream.Position = 0;
                bw.Write(fileMappingSize);
                if (bw != null) { bw.Close(); bw = null; }
            }
            catch (Exception ex)
            {
                error = true;
                MessageBox.Show("Error occurred, report it to Wouldy : \n\nFile: " + _filePath + "\n\n" + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (writer != null) { writer.Dispose(); writer = null; }
                CreateModFolders();
            }
            return error;
        }
        #endregion

        #region WriteV2
        /// <summary>
        /// Write's the BlobsetFileMapping.bin for file mapping version 2.
        /// </summary>
        /// <param name="blobsetfile">File path to blobset file</param>
        /// <param name="FileMapping_bgw">The extract Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		01/07/2025	Created
        /// </history>
        public static bool WriteV2(string blobsetfile, BackgroundWorker FileMapping_bgw)
        {
            bool error = false;
            return error;
        }
        #endregion

        #region WriteV3
        /// <summary>
        /// Write's the BlobsetFileMapping.bin for file mapping version 3.
        /// </summary>
        /// <param name="blobsetfile">File path to blobset file</param>
        /// <param name="FileMapping_bgw">The extract Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		01/07/2025	Created
        /// </history>
        public static bool WriteV3(string blobsetfile, BackgroundWorker FileMapping_bgw)
        {
            bool error = false;
            return error;
        }
        #endregion

        #region WriteV4
        /// <summary>
        /// Write's the BlobsetFileMapping.bin for file mapping version 4.
        /// </summary>
        /// <param name="blobsetfile">File path to blobset file</param>
        /// <param name="FileMapping_bgw">The extract Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		01/07/2025	Created
        /// </history>
        public static bool WriteV4(string blobsetfile, BackgroundWorker FileMapping_bgw)
        {
            Reader? blobsetContent_br = null;
            FileStream? writer = null;
            BinaryWriter? bw = null;
            string progress = string.Empty;
            int fileMappingSize = 0;
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

                BlobsetFile? blobset = Global.blobsetHeaderData;

                /*
                // For testing purposes
                string folderName1 = blobset.Entries[62292].FolderHashName;
                string fileName1 = blobset.Entries[62292].FileHashName;
                uint mainCompressedSize1 = blobset.Entries[62292].MainCompressedSize;
                uint mainUnCompressedSize1 = blobset.Entries[62292].MainUnCompressedSize;
                uint vramCompressedSize1 = blobset.Entries[62292].VramCompressedSize;
                uint vramUnCompressedSize1 = blobset.Entries[62292].VramUnCompressedSize;
                List<string> files = new List<string>();
                files.Add("Folder: " + folderName1);
                files.Add("File: " + fileName1);
                files.Add("MainCompressedSize: " +  mainCompressedSize1.ToString());
                files.Add("MainUnCompressedSize: " + mainUnCompressedSize1.ToString());
                files.Add("VramCompressedSize: " + vramCompressedSize1.ToString());
                files.Add("VramUnCompressedSize: " + vramUnCompressedSize1.ToString());
                File.AppendAllLines(Global.currentPath + @"\test.txt", contents: files);
                */

                string BlobsetFileMapping = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\data\BlobsetFileMapping.bin";
                string TxpkFileList = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\TXPK_File_List.csv";
                string M3mpFileList = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\M3MP_File_List.csv";

                if (File.Exists(BlobsetFileMapping))
                    File.Delete(BlobsetFileMapping);

                writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                bw = new(writer);

                bw.Write(fileMappingSize);
                bw.Flush();
                if (bw != null) { bw.Close(); bw = null; }

                if (File.Exists(TxpkFileList))
                    File.Delete(TxpkFileList);

                if (File.Exists(M3mpFileList))
                    File.Delete(M3mpFileList);

                for (int i = 0; i < blobset.FilesCount; i++)
                {
                    string folderName = blobset.Entries[i].FolderHashName;
                    string fileName = blobset.Entries[i].FileHashName;
                    string filePath = $@"{Functions.GetDirectory(blobsetfile)}\{folderName}\{fileName}";
                    _filePath = filePath;

                    if (File.Exists(filePath))
                    {
                        FileStream blobsetContentFs = new(filePath, FileMode.Open, FileAccess.ReadWrite);
                        blobsetContent_br = new(blobsetContentFs);

                        uint mainCompressedSize = blobset.Entries[i].MainCompressedSize;
                        uint mainUnCompressedSize = blobset.Entries[i].MainUnCompressedSize;
                        uint vramFinalOffSet = mainCompressedSize;
                        uint vramCompressedSize = blobset.Entries[i].VramCompressedSize;
                        uint vramUnCompressedSize = blobset.Entries[i].VramUnCompressedSize;

                        int chunkCount = 0;
                        long[]? ChunkSizes = null;

                        if (vramCompressedSize != 0)
                        {
                            chunkCount = Utilities.ChunkAmount((int)vramUnCompressedSize);
                            ChunkSizes = Utilities.ChunkSizes((int)vramUnCompressedSize, chunkCount);
                        }
                        else
                        {
                            chunkCount = Utilities.ChunkAmount((int)mainUnCompressedSize);
                            ChunkSizes = Utilities.ChunkSizes((int)mainUnCompressedSize, chunkCount);
                        }

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

                                        Mini_TXPK mini_TXPK = new();
                                        mini_TXPK.Deserialize(blobsetContent_br);

                                        if (Properties.Settings.Default.GameID == (int)Enums.Game.RL26)
                                            mini_TXPK.DDSFilePath = mini_TXPK.DDSFilePath.Replace(".badds", ".dds");

                                        string ddsFilePath = mini_TXPK.DDSFilePath.Replace("convant2-temp-intermediate/", "").Replace("/", @"\");
                                        mini_TXPK.DDSFilePath = ddsFilePath;

                                        writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                        bw = new(writer);

                                        bw.Write(i);
                                        bw.Write(mini_TXPK.DDSFilePath);
                                        bw.Write(folderName);
                                        bw.Write(fileName);
                                        bw.Flush();

                                        fileMappingSize++;

                                        progress = mini_TXPK.DDSFilePath;
                                        if (bw != null) { bw.Close(); bw = null; }
                                    }
                                    else
                                    {
                                        blobsetContent_br.Position = 0;
                                        int magic = blobsetContent_br.ReadInt32();
                                        blobsetContent_br.Position = 0;

                                        switch (magic)
                                        {
                                            case (int)Enums.FileType.M3MP:
                                                string m3mpName = @"m3mp\uncompressed\" + i.ToString() + ".m3mp";

                                                M3MP m3mp = new();
                                                m3mp.Deserialize(blobsetContent_br);

                                                StreamWriter? m3mp_sw = File.AppendText(M3mpFileList);

                                                foreach (var entry in m3mp.UnCompressedEntries)
                                                {
                                                    m3mp_sw.WriteLine(entry.FilePath.Replace("/", @"\") + "," + i.ToString());
                                                    m3mp_sw.Flush();
                                                }

                                                m3mp_sw.WriteLine(string.Empty);
                                                m3mp_sw.Flush();

                                                writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                                bw = new(writer);

                                                bw.Write(i);
                                                bw.Write(m3mpName);
                                                bw.Write(folderName);
                                                bw.Write(fileName);
                                                bw.Flush();

                                                fileMappingSize++;

                                                progress = m3mpName;
                                                if (m3mp_sw != null) { m3mp_sw.Dispose(); m3mp_sw = null; }
                                                if (bw != null) { bw.Close(); bw = null; }
                                                break;
                                            case (int)Enums.FileType.WiseBNK:
                                                string bnkName = @"sound\bnk\" + i.ToString() + ".bnk";

                                                writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                                bw = new(writer);

                                                bw.Write(i);
                                                bw.Write(bnkName);
                                                bw.Write(folderName);
                                                bw.Write(fileName);
                                                bw.Flush();

                                                fileMappingSize++;

                                                progress = bnkName;
                                                if (bw != null) { bw.Close(); bw = null; }
                                                break;
                                            case (int)Enums.FileType.WiseWEM:
                                                string wemName = @"sound\wem\" + i.ToString() + ".wem";

                                                writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                                bw = new(writer);

                                                bw.Write(i);
                                                bw.Write(wemName);
                                                bw.Write(folderName);
                                                bw.Write(fileName);
                                                bw.Flush();

                                                fileMappingSize++;

                                                progress = wemName;
                                                if (bw != null) { bw.Close(); bw = null; }
                                                break;
                                            case (int)Enums.FileType.TXPK:

                                                blobsetContent_br.Position = 0;

                                                string txpkFileName = @"dds_txpk\" + i.ToString() + ".txpk";

                                                writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                                bw = new(writer);

                                                bw.Write(i);
                                                bw.Write(txpkFileName);
                                                bw.Write(folderName);
                                                bw.Write(fileName);
                                                bw.Flush();

                                                fileMappingSize++;

                                                progress = txpkFileName;
                                                if (bw != null) { bw.Close(); bw = null; }
                                                break;
                                            default:
                                                if (!Properties.Settings.Default.SkipUnknown)
                                                {
                                                    string unknownFileName = @"unknown\mainuncompressed_vramuncompressed\" + i.ToString() + ".dat";

                                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                                    bw = new(writer);

                                                    bw.Write(i);
                                                    bw.Write(unknownFileName);
                                                    bw.Write(folderName);
                                                    bw.Write(fileName);
                                                    bw.Flush();

                                                    fileMappingSize++;

                                                    progress = unknownFileName;
                                                    if (bw != null) { bw.Close(); bw = null; }
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

                                        Mini_TXPK mini_TXPK = new();
                                        mini_TXPK.Deserialize(blobsetContent_br);

                                        if (Properties.Settings.Default.GameID == (int)Enums.Game.RL26)
                                            mini_TXPK.DDSFilePath = mini_TXPK.DDSFilePath.Replace(".badds", ".dds");

                                        string ddsFilePath = mini_TXPK.DDSFilePath.Replace("convant2-temp-intermediate/", "").Replace("/", @"\");
                                        mini_TXPK.DDSFilePath = ddsFilePath;

                                        writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                        bw = new(writer);

                                        bw.Write(i);
                                        bw.Write(mini_TXPK.DDSFilePath);
                                        bw.Write(folderName);
                                        bw.Write(fileName);
                                        bw.Flush();

                                        fileMappingSize++;

                                        progress = mini_TXPK.DDSFilePath;
                                        if (bw != null) { bw.Close(); bw = null; }
                                    }
                                    else
                                    {
                                        blobsetContent_br.Position = 0;
                                        int magic = blobsetContent_br.ReadInt32();
                                        blobsetContent_br.Position = 0;

                                        switch (magic)
                                        {
                                            case (int)Enums.FileType.TXPK:

                                                blobsetContent_br.Position = 0;

                                                string txpkFileName = @"dds_txpk\" + i.ToString() + ".txpk";

                                                writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                                bw = new(writer);

                                                bw.Write(i);
                                                bw.Write(txpkFileName);
                                                bw.Write(folderName);
                                                bw.Write(fileName);
                                                bw.Flush();

                                                fileMappingSize++;

                                                progress = txpkFileName;
                                                if (bw != null) { bw.Close(); bw = null; }
                                                break;
                                            default:
                                                if (!Properties.Settings.Default.SkipUnknown)
                                                {
                                                    string unknownFileName = @"unknown\mainuncompressed_vramcompressed\" + i.ToString() + ".dat";

                                                    writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                                    bw = new(writer);

                                                    bw.Write(i);
                                                    bw.Write(unknownFileName);
                                                    bw.Write(folderName);
                                                    bw.Write(fileName);
                                                    bw.Flush();

                                                    fileMappingSize++;

                                                    progress = unknownFileName;
                                                    if (bw != null) { bw.Close(); bw = null; }
                                                }
                                                break;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                blobsetContent_br.Position = 0;
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
                                            string m3mpName = @"m3mp\compressed\" + i.ToString() + ".m3mp";

                                            List<byte[]> m3mpHeaderChunk = new(3);
                                            int m3mpHeaderSize = 0;

                                            for (int j = 0; j < 3; j++)
                                            {
                                                int m3mpCompressedSize = blobsetContent_br.ReadInt32();
                                                int m3mpTmp = m3mpCompressedSize -= 4;
                                                m3mpCompressedSize = m3mpTmp;

                                                bool isM3mpCompressed = true;

                                                byte[] m3mpData = blobsetContent_br.ReadBytes(m3mpCompressedSize);

                                                byte[] ZstdMagicArrayM3mp = [m3mpData[0], m3mpData[1], m3mpData[2], m3mpData[3]];
                                                uint ZstdMagicM3mp = BitConverter.ToUInt32(ZstdMagicArrayM3mp);

                                                if (ZstdMagicM3mp != 4247762216)
                                                    isM3mpCompressed = false;

                                                m3mpHeaderChunk.Add(ZSTD_IO.DecompressAndRead(m3mpData, isM3mpCompressed));
                                                m3mpHeaderSize += (int)ChunkSizes[j];
                                            }

                                            byte[] m3mpHeader = IO.CombineDataChunks(m3mpHeaderChunk, m3mpHeaderSize);

                                            Reader? m3mp_br = new(m3mpHeader);

                                            M3MP m3mp = new();
                                            m3mp.Deserialize(m3mp_br);

                                            StreamWriter? m3mp_sw = File.AppendText(M3mpFileList);

                                            foreach (var entry in m3mp.UnCompressedEntries)
                                            {
                                                m3mp_sw.WriteLine(entry.FilePath.Replace("/", @"\") + "," + i.ToString());
                                                m3mp_sw.Flush();
                                            }

                                            m3mp_sw.WriteLine(string.Empty);
                                            m3mp_sw.Flush();

                                            writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                            bw = new(writer);

                                            bw.Write(i);
                                            bw.Write(m3mpName);
                                            bw.Write(folderName);
                                            bw.Write(fileName);
                                            bw.Flush();

                                            fileMappingSize++;

                                            progress = m3mpName;
                                            if (m3mp_br != null) { m3mp_br.Close(); m3mp_br = null; }
                                            if (m3mp_sw != null) { m3mp_sw.Dispose(); m3mp_sw = null; }
                                            if (bw != null) { bw.Close(); bw = null; }
                                            break;
                                        default:
                                            if (!Properties.Settings.Default.SkipUnknown)
                                            {
                                                string unknownFileName = @"unknown\maincompressed_vramuncompressed\" + i.ToString() + ".dat";

                                                writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                                bw = new(writer);

                                                bw.Write(i);
                                                bw.Write(unknownFileName);
                                                bw.Write(folderName);
                                                bw.Write(fileName);
                                                bw.Flush();

                                                fileMappingSize++;

                                                progress = unknownFileName;
                                                if (bw != null) { bw.Close(); bw = null; }
                                            }
                                            break;
                                    }
                                }
                                else
                                {
                                    switch (magic)
                                    {
                                        case (int)Enums.FileType.TXPK:
                                            string txpkName = @"dds_txpk\" + i.ToString() + ".txpk";

                                            int txpkCompressedChunkSize = blobsetContent_br.ReadInt32();
                                            int txpkTmp = txpkCompressedChunkSize -= 4;
                                            txpkCompressedChunkSize = txpkTmp;

                                            bool isTxpkCompressed = true;

                                            byte[] txpkData = blobsetContent_br.ReadBytes(txpkCompressedChunkSize);

                                            byte[] ZstdMagicArrayTxpk = [txpkData[0], txpkData[1], txpkData[2], txpkData[3]];
                                            uint ZstdMagicTxpk = BitConverter.ToUInt32(ZstdMagicArrayTxpk);

                                            if (ZstdMagicTxpk != 4247762216)
                                                isTxpkCompressed = false;

                                            byte[] txpkHeader = ZSTD_IO.DecompressAndRead(txpkData, isTxpkCompressed);

                                            Reader? txpk_br = new(txpkHeader);

                                            TXPK txpk = new();
                                            txpk.Deserialize(txpk_br);

                                            StreamWriter? txpk_sw = File.AppendText(TxpkFileList);

                                            foreach (var entry in txpk.Entries)
                                            {
                                                string DDSFilePath = entry.DDSFilePath.Replace("/", @"\");
                                                txpk_sw.WriteLine(DDSFilePath + ".dds" + "," + i.ToString());
                                                txpk_sw.Flush();
                                            }

                                            txpk_sw.WriteLine(string.Empty);
                                            txpk_sw.Flush();

                                            writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                            bw = new(writer);

                                            bw.Write(i);
                                            bw.Write(txpkName);
                                            bw.Write(folderName);
                                            bw.Write(fileName);
                                            bw.Flush();

                                            fileMappingSize++;

                                            progress = txpkName;
                                            if (txpk_br != null) { txpk_br.Close(); txpk_br = null; }
                                            if (txpk_sw != null) { txpk_sw.Dispose(); txpk_sw = null; }
                                            if (bw != null) { bw.Close(); bw = null; }
                                            break;
                                        case (int)Enums.FileType.MINI:
                                            int miniTxpkCompressedChunkSize = blobsetContent_br.ReadInt32();
                                            int miniTxpkTmp = miniTxpkCompressedChunkSize -= 4;
                                            miniTxpkCompressedChunkSize = miniTxpkTmp;

                                            bool isMiniTxpkCompressed = true;

                                            byte[] miniTxpkData = blobsetContent_br.ReadBytes(miniTxpkCompressedChunkSize);

                                            byte[] ZstdMagicArrayMiniTxpk = [miniTxpkData[0], miniTxpkData[1], miniTxpkData[2], miniTxpkData[3]];
                                            uint ZstdMagicMiniTxpk = BitConverter.ToUInt32(ZstdMagicArrayMiniTxpk);

                                            if (ZstdMagicMiniTxpk != 4247762216)
                                                isMiniTxpkCompressed = false;

                                            byte[] miniTxpkHeader = ZSTD_IO.DecompressAndRead(miniTxpkData, isMiniTxpkCompressed);

                                            Reader? minTxpk_br = new(miniTxpkHeader);

                                            Mini_TXPK mini_TXPK = new();
                                            mini_TXPK.Deserialize(minTxpk_br);

                                            if (minTxpk_br != null) { minTxpk_br.Close(); minTxpk_br = null; }

                                            if (Properties.Settings.Default.GameID == (int)Enums.Game.RL26)
                                                mini_TXPK.DDSFilePath = mini_TXPK.DDSFilePath.Replace(".badds", ".dds");

                                            string ddsFilePath = mini_TXPK.DDSFilePath.Replace("convant2-temp-intermediate/", "").Replace("/", @"\");

                                            mini_TXPK.DDSFilePath = ddsFilePath;

                                            writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                            bw = new(writer);

                                            bw.Write(i);
                                            bw.Write(mini_TXPK.DDSFilePath);
                                            bw.Write(folderName);
                                            bw.Write(fileName);
                                            bw.Flush();

                                            fileMappingSize++;

                                            progress = mini_TXPK.DDSFilePath;
                                            if (bw != null) { bw.Close(); bw = null; }
                                            break;
                                        default:
                                            if (!Properties.Settings.Default.SkipUnknown)
                                            {
                                                string unknownFileName = @"unknown\maincompressed_vramcompressed\" + i.ToString() + ".dat";

                                                writer = new(BlobsetFileMapping, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                                                bw = new(writer);

                                                bw.Write(i);
                                                bw.Write(unknownFileName);
                                                bw.Write(folderName);
                                                bw.Write(fileName);
                                                bw.Flush();

                                                fileMappingSize++;

                                                progress = unknownFileName;
                                                if (bw != null) { bw.Close(); bw = null; }
                                            }
                                            break;
                                    }
                                }
                            }
                        }
                    }

                    int percentProgress = 100 * i / (int)blobset.FilesCount;
                    FileMapping_bgw.ReportProgress(percentProgress, progress);
                }

                writer = new(BlobsetFileMapping, FileMode.Open, FileAccess.Write, FileShare.ReadWrite);
                bw = new(writer);

                bw.BaseStream.Position = 0;
                bw.Write(fileMappingSize);
                if (bw != null) { bw.Close(); bw = null; }
            }
            catch (Exception ex)
            {
                error = true;
                MessageBox.Show("Error occurred, report it to Wouldy : \n\nFile: " + _filePath + "\n\n" + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (writer != null) { writer.Dispose(); writer = null; }
                CreateModFolders();
            }
            return error;
        }
        #endregion

        #region Create Mod Folders
        /// <summary>
        /// Creates all the folders for mods.
        /// </summary>
        /// <history>
        /// [Wouldubeinta]		11/07/2025	Created
        /// </history>
        private static void CreateModFolders()
        {
            Reader? br = null;
            Writer? bw = null;
            FileMapping? fileMapping = null;

            try
            {
                string BlobsetFileMapping = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\data\BlobsetFileMapping.bin";

                if (File.Exists(BlobsetFileMapping))
                {
                    br = new(BlobsetFileMapping);

                    fileMapping = new();
                    fileMapping.Read(br);

                    if (fileMapping == null || fileMapping.FilesCount == 0)
                        return;

                    foreach (var entry in fileMapping.Entries)
                        Directory.CreateDirectory(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\mods\" + Path.GetDirectoryName(entry.FilePath));

                    // Put File Mapping in alphabetical order.

                    IOrderedEnumerable<Entry> fm = fileMapping.Entries.OrderBy(fm => fm.FilePath);

                    if (br != null) { br.Close(); br = null; }

                    bw = new(BlobsetFileMapping);
                    bw.Position = 0;
                    bw.WriteInt32(fileMapping.FilesCount);

                    foreach (var item in fm) 
                    {
                        bw.WriteInt32(item.Index, Endian.Little);
                        bw.WriteUInt8(Convert.ToByte(item.FilePath.Length), Endian.Little);
                        bw.WriteString(item.FilePath);
                        bw.WriteUInt8(Convert.ToByte(item.FolderHash.Length));
                        bw.WriteString(item.FolderHash);
                        bw.WriteUInt8(Convert.ToByte(item.FileNameHash.Length));
                        bw.WriteString(item.FileNameHash);
                    }
                }
                else
                    return;
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (bw != null) { bw.Close(); bw = null; }
                if (fileMapping != null) { fileMapping = null; }
            }
        }
        #endregion
    }
}

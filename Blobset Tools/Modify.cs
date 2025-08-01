using BlobsetIO;
using PackageIO;
using System.ComponentModel;

namespace Blobset_Tools
{
    /// <summary>
    /// Modify different blobset version's.
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
    /// [Wouldubeinta]	   15/07/2025	Created
    /// </history>
    public class Modify
    {
        #region Modify Blobset Version 1
        /// <summary>
        /// Read Filemapping and than create file's for the blobset - version 1.
        /// </summary>
        /// <param name="blobsetfile">File path to blobset file</param>
        /// <param name="Modify_bgw">The modify Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		05/07/2025	Created
        /// </history>
        public static void BlobsetV1(string blobsetfile, BackgroundWorker Modify_bgw)
        {

        }
        #endregion

        #region Modify Blobset Version 2
        /// <summary>
        /// Read Filemapping and than create file's for the blobset - version 2.
        /// </summary>
        /// <param name="blobsetfile">File path to blobset file</param>
        /// <param name="Modify_bgw">The modify Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		05/07/2025	Created
        /// </history>
        public static void BlobsetV2(string blobsetfile, BackgroundWorker Modify_bgw)
        {

        }
        #endregion

        #region Modify Blobset Version 3
        /// <summary>
        /// Read Filemapping and than create file's for the blobset - version 3.
        /// </summary>
        /// <param name="blobsetfile">File path to blobset file</param>
        /// <param name="Modify_bgw">The modify Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		05/07/2025	Created
        /// </history>
        public static void BlobsetV3(string blobsetfile, BackgroundWorker Modify_bgw)
        {

        }
        #endregion

        #region Modify Blobset Version 4
        /// <summary>
        /// Read Filemapping and than create file's for the blobset - version 4. BigAnts newer games.
        /// </summary>
        /// <param name="blobsetfile">File path to blobset file</param>
        /// <param name="Modify_bgw">The modify Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		15/07/2025	Created
        /// </history>
        public static bool BlobsetV4(string blobsetFile, BackgroundWorker Modify_bgw)
        {
            Writer? blobsetHeader_bw = null;
            FileStream? writer = null;

            string progress = string.Empty;
            string _filePath = string.Empty;
            int chunkSize = 262144;
            bool error = false;

            try
            {
                if (Global.blobsetHeaderData == null)
                {
                    error = true;
                    MessageBox.Show("Blobset header data is null", "Blobset Header Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return error;
                }

                int blobsetFileCount = (int)Global.blobsetHeaderData.FilesCount;

                string[] ddsfileList = Utilities.DirectoryInfo(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\mods", "*.dds");
                string[] txpkfileList = Utilities.DirectoryInfo(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\mods", "*.txpk");
                string[] m3mpfileList = Utilities.DirectoryInfo(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\mods", "*.m3mp");
                string[] wemfileList = Utilities.DirectoryInfo(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\mods", "*.wem");
                string[] bnkfileList = Utilities.DirectoryInfo(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\mods", "*.bnk");
                //string[] datfileList = Utilities.DirectoryInfo(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\mods", "*.dat");

                int ddsListLength = ddsfileList.Length;
                int txpkListLength = txpkfileList.Length;
                int m3mpListLength = m3mpfileList.Length;
                int wemListLength = wemfileList.Length;
                int bnkListLength = bnkfileList.Length;
                //int datListLength = datfileList.Length;
                //int fullListLength = ddsListLength + txpkListLength + m3mpListLength + wemListLength + bnkListLength + datListLength;
                int fullListLength = ddsListLength + txpkListLength + m3mpListLength + wemListLength + bnkListLength;

                if (fullListLength == 0)
                {
                    error = true;
                    MessageBox.Show("No files to modify blobset", "No Supported Files Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return error;
                }

                Reader fileMapping_br = new(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\data\BlobsetFileMapping.bin");

                FileMapping fileMapping = new();
                fileMapping.Read(fileMapping_br);

                if (fileMapping == null)
                {
                    MessageBox.Show("Looks like the file mapping data is corrupted. Might need to run 'Update File Mapping Data' in Settings or Validate Steam Files", "File Mapping Data Error", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return error;
                }

                List<int> mainCompressedSize = [];
                List<int> mainUncompressedSize = [];
                List<int> vramCompressedSize = [];
                List<int> vramUncompressedSize = [];
                List<int> headerIndex = [];

                string filePathRemove = Global.currentPath + "\\games\\" + Properties.Settings.Default.GameName + "\\mods\\";
                string backupFilePath = Global.currentPath + "\\games\\" + Properties.Settings.Default.GameName + "\\backup\\";

                string gameLocation = Path.GetDirectoryName(blobsetFile) + @"\";

                for (int i = 0; i < ddsListLength; i++)
                {
                    FileMapping fm = Utilities.GetFileMappingIndex(ddsfileList[i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fm == null)
                    {
                        error = true;
                        MessageBox.Show(ddsfileList[i] + " - fileIndex can't be found, make sure the dds file name or location is correct. Example: Don't have this in the file name (1).", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return error;
                    }

                    string folderName = fm.Entries[0].FolderHash;
                    string fileName = fm.Entries[0].FileNameHash;
                    string filePath = gameLocation + folderName + @"\" + fileName;
                    _filePath = filePath;

                    if (!File.Exists(backupFilePath + folderName + @"\" + fileName))
                    {
                        Directory.CreateDirectory(backupFilePath + folderName + @"\");
                        File.Move(filePath, backupFilePath + folderName + @"\" + fileName);
                    }

                    if (File.Exists(filePath))
                        File.Delete(filePath);

                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    Mini_TXPK mini_TXPK = new();
                    mini_TXPK.Serialize(ddsfileList[i], ddsfileList[i].Replace(filePathRemove, string.Empty), writer);

                    int mainUnCompSize = (int)Utilities.FileInfo(filePath);
                    int vramUncompSize = (int)Utilities.FileInfo(ddsfileList[i]);

                    mainCompressedSize.Add(mainUnCompSize);
                    mainUncompressedSize.Add(mainUnCompSize);

                    int chunkCount = Utilities.ChunkAmount(vramUncompSize);
                    long[] chunkSizes = Utilities.ChunkSizes(vramUncompSize, chunkCount);

                    Reader? br = new(ddsfileList[i]);

                    for (int j = 0; j < chunkCount; j++)
                    {
                        byte[] ddsChunkData = br.ReadBytes((int)chunkSizes[j]);

                        if ((int)chunkSizes[j] == chunkSize)
                            ZSTD_IO.CompressAndWrite(ddsChunkData, writer, (int)chunkSizes[j]);
                        else
                            IO.ReadWriteModifyData(ddsChunkData, writer, (int)chunkSizes[j]);
                    }

                    vramCompressedSize.Add((int)Utilities.FileInfo(filePath) - mainUnCompSize);
                    vramUncompressedSize.Add(vramUncompSize);

                    headerIndex.Add(Convert.ToInt32(fm.Entries[0].Index));

                    progress = ddsfileList[i].Replace(filePathRemove, string.Empty);
                    int percentProgress = 100 * i / ddsListLength;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                for (int i = 0; i < txpkListLength; i++)
                {
                    if (!File.Exists(txpkfileList[i].Replace(".txpk", ".xml")))
                    {
                        error = false;
                        MessageBox.Show("Can't find TXPK xml info - " + txpkfileList[i].Replace(".txpk", ".xml"), "XML File Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return error;
                    }

                    ModifyFileInfo txpkXmlInfo = IO.XmlDeserialize<ModifyFileInfo>(txpkfileList[i].Replace(".txpk", ".xml"));

                    if (txpkXmlInfo == null)
                    {
                        error = false;
                        MessageBox.Show(txpkfileList[i].Replace(".txpk", ".xml") + " - XmlDeserialize failed.", "Modify XML Info Was Null", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return error;
                    }

                    FileMapping fm = Utilities.GetFileMappingIndex(txpkXmlInfo.Index, fileMapping);

                    if (fm == null)
                    {
                        error = true;
                        MessageBox.Show(txpkfileList[i] + " - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return error;
                    }

                    string folderName = fm.Entries[0].FolderHash;
                    string fileName = fm.Entries[0].FileNameHash;
                    string filePath = gameLocation + folderName + @"\" + fileName;
                    _filePath = filePath;

                    if (!File.Exists(backupFilePath + folderName + @"\" + fileName))
                    {
                        Directory.CreateDirectory(backupFilePath + folderName + @"\");
                        File.Move(filePath, backupFilePath + folderName + @"\" + fileName);
                    }

                    if (File.Exists(filePath))
                        File.Delete(filePath);

                    Reader? mainBr = new(txpkfileList[i]);

                    TXPK txpk = new();
                    txpk.Deserialize(mainBr);

                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    txpk.Serialize(writer);

                    int mainChunkCount = Utilities.ChunkAmount(txpkXmlInfo.MainUnCompressedSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(txpkXmlInfo.MainUnCompressedSize, mainChunkCount);

                    int vramChunkCount = Utilities.ChunkAmount(txpkXmlInfo.VramUnCompressedSize);
                    long[] vramChunkSizes = Utilities.ChunkSizes(txpkXmlInfo.VramUnCompressedSize, vramChunkCount);

                    Reader? txpkHeaderBr = new(filePath);

                    writer.Position = 0;

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] txpkMainChunkData = txpkHeaderBr.ReadBytes((int)mainChunkSizes[j]);
                        ZSTD_IO.CompressAndWrite(txpkMainChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    int txpkMainCompressedSize = (int)writer.Position;
                    mainCompressedSize.Add(txpkMainCompressedSize);
                    mainUncompressedSize.Add(txpkXmlInfo.MainUnCompressedSize);

                    if (txpkHeaderBr != null) { txpkHeaderBr.Close(); txpkHeaderBr = null; }

                    for (int j = 0; j < vramChunkCount; j++)
                    {
                        byte[] txpkVramChunkData = mainBr.ReadBytes((int)vramChunkSizes[j]);

                        if ((int)vramChunkSizes[j] == chunkSize)
                            ZSTD_IO.CompressAndWrite(txpkVramChunkData, writer, (int)vramChunkSizes[j]);
                        else
                            IO.ReadWriteModifyData(txpkVramChunkData, writer, (int)vramChunkSizes[j]);
                    }

                    vramCompressedSize.Add((int)Utilities.FileInfo(filePath) - txpkMainCompressedSize);
                    vramUncompressedSize.Add(txpkXmlInfo.VramUnCompressedSize);

                    progress = txpkfileList[i].Replace(filePathRemove, string.Empty);

                    headerIndex.Add(txpkXmlInfo.Index);

                    int percentProgress = 100 * i / txpkListLength;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (mainBr != null) { mainBr.Close(); mainBr = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                for (int i = 0; i < m3mpListLength; i++)
                {
                    Reader? br = new(m3mpfileList[i]);

                    ModifyFileInfo m3mpFileInfo = IO.XmlDeserialize<ModifyFileInfo>(m3mpfileList[i].Replace(".m3mp", ".xml"));

                    if (m3mpFileInfo == null)
                    {
                        error = false;
                        MessageBox.Show(m3mpfileList[i].Replace(".m3mp", ".xml") + " - XmlDeserialize failed.", "Modify XML Info Was Null", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return error;
                    }

                    FileMapping fm = Utilities.GetFileMappingIndex(m3mpFileInfo.Index, fileMapping);

                    if (fm == null)
                    {
                        error = true;
                        MessageBox.Show(m3mpfileList[i] + " - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return error;
                    }

                    string folderName = fm.Entries[0].FolderHash;
                    string fileName = fm.Entries[0].FileNameHash;
                    string filePath = gameLocation + folderName + @"\" + fileName;
                    _filePath = filePath;

                    if (!File.Exists(backupFilePath + folderName + @"\" + fileName))
                    {
                        Directory.CreateDirectory(backupFilePath + folderName + @"\");
                        File.Move(filePath, backupFilePath + folderName + @"\" + fileName);
                    }

                    if (File.Exists(filePath))
                        File.Delete(filePath);

                    int mainUncompSize = m3mpFileInfo.MainUnCompressedSize;
                    int mainChunkCount = Utilities.ChunkAmount(mainUncompSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainUncompSize, mainChunkCount);

                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] m3mpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(m3mpChunkData, writer, (int)mainChunkSizes[j]);
                    }
                    mainCompressedSize.Add(mainUncompSize);

                    mainUncompressedSize.Add(mainUncompSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    progress = m3mpfileList[i].Replace(filePathRemove, string.Empty);

                    headerIndex.Add(m3mpFileInfo.Index);

                    int percentProgress = 100 * i / m3mpListLength;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                for (int i = 0; i < wemListLength; i++)
                {
                    FileMapping fmIndex = Utilities.GetFileMappingIndex(wemfileList[i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fmIndex == null)
                    {
                        error = true;
                        MessageBox.Show(wemfileList[i] + " - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return error;
                    }

                    string folderName = fmIndex.Entries[0].FolderHash;
                    string fileName = fmIndex.Entries[0].FileNameHash;
                    string filePath = gameLocation + folderName + @"\" + fileName;
                    _filePath = filePath;

                    if (!File.Exists(backupFilePath + folderName + @"\" + fileName))
                    {
                        Directory.CreateDirectory(backupFilePath + folderName + @"\");
                        File.Move(filePath, backupFilePath + folderName + @"\" + fileName);
                    }

                    if (File.Exists(filePath))
                        File.Delete(filePath);

                    int mainSize = (int)Utilities.FileInfo(wemfileList[i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(wemfileList[i]);
                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fmIndex.Entries[0].Index);

                    progress = wemfileList[i].Replace(filePathRemove, string.Empty);

                    int percentProgress = 100 * i / wemListLength;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                for (int i = 0; i < bnkListLength; i++)
                {
                    FileMapping fmIndex = Utilities.GetFileMappingIndex(bnkfileList[i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fmIndex == null)
                    {
                        error = true;
                        MessageBox.Show(bnkfileList[i] + " - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return error;
                    }

                    string folderName = fmIndex.Entries[0].FolderHash;
                    string fileName = fmIndex.Entries[0].FileNameHash;
                    string filePath = gameLocation + folderName + @"\" + fileName;
                    _filePath = filePath;

                    if (!File.Exists(backupFilePath + folderName + @"\" + fileName))
                    {
                        Directory.CreateDirectory(backupFilePath + folderName + @"\");
                        File.Move(filePath, backupFilePath + folderName + @"\" + fileName);
                    }

                    if (File.Exists(filePath))
                        File.Delete(filePath);

                    int mainSize = (int)Utilities.FileInfo(bnkfileList[i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(bnkfileList[i]);
                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fmIndex.Entries[0].Index);

                    progress = bnkfileList[i].Replace(filePathRemove, string.Empty);

                    int percentProgress = 100 * i / bnkListLength;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                if (!File.Exists(backupFilePath + "data-0.blobset.pc"))
                {
                    File.Copy(blobsetFile, backupFilePath + "data-0.blobset.pc");
                }

                blobsetHeader_bw = new Writer(blobsetFile);

                int HeaderSize = 16;
                int DataOffset = 8 + (blobsetFileCount * 8);

                for (int i = 0; i < fullListLength; i++)
                {
                    if (headerIndex[i] == 0)
                        blobsetHeader_bw.Position = DataOffset;
                    else
                        blobsetHeader_bw.Position = (HeaderSize * headerIndex[i]) + DataOffset;

                    blobsetHeader_bw.WriteInt32(mainCompressedSize[i]); // mainCompressedSize
                    blobsetHeader_bw.WriteInt32(mainUncompressedSize[i]); // mainUncompressedSize
                    blobsetHeader_bw.WriteInt32(vramCompressedSize[i]); // vramCompressedSize
                    blobsetHeader_bw.WriteInt32(vramUncompressedSize[i]); // vramUncompressedSize
                }
            }
            catch (Exception arg)
            {
                error = true;
                MessageBox.Show("Error occurred, report it to Wouldy : \n\nFile: " + _filePath + "\n\n" + arg, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (blobsetHeader_bw != null) { blobsetHeader_bw.Close(); blobsetHeader_bw = null; }
                UI.BlobsetHeaderData();
            }
            return error;
        }
        #endregion
    }
}

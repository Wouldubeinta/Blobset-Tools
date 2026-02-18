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
        public static bool BlobsetV1(string blobsetFile, BackgroundWorker Modify_bgw)
        {
            Reader? fileMapping_br = null;
            Writer? blobsetHeader_bw = null;
            FileStream? writer = null;

            string progress = string.Empty;
            string _filePath = string.Empty;
            int chunkSize = 32768;

            try
            {
                // Retrieve platform details
                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];

                // Define the base path for game-related files
                string basePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt);

                string modsFolder = Path.Combine(basePath, "mods");
                string ddsHeaderData = Path.Combine(basePath, "data", "ddsHeaderData");

                // Define an array of file extensions to search for
                string[] fileExtensions = { "dds", "txpk", "m3mp", "bsb", "bmf", "bank", "fsb", "fev", "wav", "dat" };

                // Initialize a dictionary to hold file lists and their lengths
                var fileLists = new Dictionary<string, string[]>();
                int fullListLength = 0;

                // Iterate over each file extension, populate the dictionary, and calculate total length
                foreach (var extension in fileExtensions)
                {
                    string[] fileList = Utilities.DirectoryInfo(modsFolder, "*." + extension);
                    fileLists[extension] = fileList;
                    fullListLength += fileList.Length;
                }

                if (fullListLength == 0)
                {
                    MessageBox.Show("No files to modify blobset", "No Supported Files Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return true;
                }

                string blobsetFilename = Path.GetFileName(blobsetFile);

                fileMapping_br = new(Path.Combine(basePath, "data", blobsetFilename + ".mapping"));

                FileMapping fileMapping = new();
                fileMapping.Read(fileMapping_br);

                if (fileMapping == null)
                {
                    MessageBox.Show("Looks like the file mapping data is corrupted. Might need to run 'Update File Mapping Data' in Settings or Validate Steam Files", "File Mapping Data Error", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return true;
                }

                List<uint> mainFinalOffSet = [];
                List<int> mainCompressedSize = [];
                List<int> mainUncompressedSize = [];
                List<uint> vramFinalOffSet = [];
                List<int> vramCompressedSize = [];
                List<int> vramUncompressedSize = [];
                List<int> headerIndex = [];

                string filePathRemove = modsFolder + "\\";
                string backupFilePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, "backup");

                string filePath = blobsetFile.Replace("-0", "-1");
                _filePath = filePath;

                byte[] blobsetHeader = Utilities.ReadBlobsetHeader(blobsetFilename);

                if (blobsetHeader == null)
                {
                    string headerFilePath = Path.Combine(basePath, "backup", $"{blobsetFilename}.header");
                    string message = $"Can't find blobset header - {headerFilePath}";
                    string caption = "Blobset Header File Not Found";

                    MessageBox.Show(message, caption, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return true;
                }

                // Restoring original blobset header 
                Writer? bw = new(blobsetFile, Endian.Little);
                bw.Write(blobsetHeader);
                if (bw != null) { bw.Close(); bw = null; }

                if (File.Exists(filePath))
                    File.Delete(filePath);

                writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                for (int i = 0; i < fileLists["dds"].Length; i++)
                {
                    FileMapping fm = Utilities.GetFileMappingIndex(fileLists["dds"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["dds"][i]} - fileIndex can't be found, make sure the dds file name or location is correct. Example: Don't have this in the file name (1).", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    Mini_TXPK mini_TXPK = new();

                    if (Global.platforms == Enums.Platforms.PS3)
                    {
                        PS3_DDS_Header header = IO.XmlDeserialize<PS3_DDS_Header>(Path.Combine(ddsHeaderData, fileLists["dds"][i].Replace(filePathRemove, string.Empty).Replace(".dds", ".xml")));
                        mini_TXPK.BufferSize = header.Entries[0].BufferSize;
                        mini_TXPK.DDSHeight2 = header.Entries[0].DDSHeight;
                        mini_TXPK.DDSWidth2 = header.Entries[0].DDSWidth;
                        mini_TXPK.DDSImageType2 = header.Entries[0].DDSImageType;
                        mini_TXPK.DDSMipMaps = header.Entries[0].DDSMipMaps;
                        mini_TXPK.DDSType = header.Entries[0].DDSType;
                        mini_TXPK.Unknown1 = header.Entries[0].Unknown1;
                        mini_TXPK.Unknown2 = header.Entries[0].Unknown2;
                        mini_TXPK.Unknown3 = header.Entries[0].Unknown3;
                        mini_TXPK.Unknown4 = header.Entries[0].Unknown4;
                        mini_TXPK.Unknown5 = header.Entries[0].Unknown5;
                        mini_TXPK.Reserved = header.Entries[0].Reserved;
                    }

                    byte[] mini_TXPK_Header = mini_TXPK.WriteAndRead(fileLists["dds"][i], fileLists["dds"][i].Replace(filePathRemove, string.Empty));

                    int mainUnCompSize = mini_TXPK_Header.Length;
                    int vramUncompSize = Global.isBigendian == true ? (int)Utilities.FileInfo(fileLists["dds"][i]) - 128 : (int)Utilities.FileInfo(fileLists["dds"][i]);

                    int _mainCompressedSize = LZMA_IO.CompressAndWrite(mini_TXPK_Header, writer, mainUnCompSize);

                    mainCompressedSize.Add(_mainCompressedSize);
                    mainUncompressedSize.Add(mainUnCompSize);

                    Reader? br = new(fileLists["dds"][i]);

                    int ddsDataSize = (int)br.Length;
                    int ddsHeaderSize = 128;

                    if (Global.isBigendian) // If PS3 or Xbox 360
                    {
                        br.Position = ddsHeaderSize; // Position after dds header
                        ddsDataSize = (int)(br.Length - ddsHeaderSize); // Size without dds header
                    }

                    byte[] ddsChunkData = br.ReadBytes(ddsDataSize, Endian.Little);
                    int _vramCompressedSize = LZMA_IO.CompressAndWrite(ddsChunkData, writer, chunkSize);

                    vramCompressedSize.Add(_vramCompressedSize);
                    vramCompressedSize.Add(vramUncompSize);
                    vramUncompressedSize.Add(vramUncompSize);

                    headerIndex.Add(Convert.ToInt32(fm.Entries[0].Index));

                    progress = fileLists["dds"][i].Replace(filePathRemove, string.Empty);
                    int percentProgress = (i + 1) * 100 / fileLists["dds"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                }

                for (int i = 0; i < fileLists["txpk"].Length; i++)
                {
                    if (!File.Exists(fileLists["txpk"][i].Replace(".txpk", ".xml")))
                    {
                        MessageBox.Show($"Can't find TXPK xml info - {fileLists["txpk"][i].Replace(".txpk", ".xml")}", "XML File Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    ModifyFileInfo txpkXmlInfo = IO.XmlDeserialize<ModifyFileInfo>(fileLists["txpk"][i].Replace(".txpk", ".xml"));

                    if (txpkXmlInfo == null)
                    {
                        MessageBox.Show($"{fileLists["txpk"][i].Replace(".txpk", ".xml")} - XmlDeserialize failed.", "Modify XML Info Was Null", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    FileMapping fm = Utilities.GetFileMappingIndex(txpkXmlInfo.Index, fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["txpk"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    Reader? br = new(fileLists["txpk"][i]);

                    byte[] main = br.ReadBytes(txpkXmlInfo.MainUnCompressedSize);
                    byte[] vram = br.ReadBytes(txpkXmlInfo.VramUnCompressedSize);

                    int txpkMainCompressedSize = LZMA_IO.CompressAndWrite(main, writer, chunkSize);

                    mainCompressedSize.Add(txpkMainCompressedSize);
                    mainUncompressedSize.Add(txpkXmlInfo.MainUnCompressedSize);

                    int txpkVramCompressedSize = LZMA_IO.CompressAndWrite(vram, writer, chunkSize);

                    vramCompressedSize.Add(txpkVramCompressedSize);
                    vramUncompressedSize.Add(txpkXmlInfo.VramUnCompressedSize);

                    progress = fileLists["txpk"][i].Replace(filePathRemove, string.Empty);

                    headerIndex.Add(txpkXmlInfo.Index);

                    int percentProgress = (i + 1) * 100 / fileLists["txpk"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                }

                for (int i = 0; i < fileLists["m3mp"].Length; i++)
                {
                    Reader? br = new(fileLists["m3mp"][i]);

                    ModifyFileInfo m3mpFileInfo = IO.XmlDeserialize<ModifyFileInfo>(fileLists["m3mp"][i].Replace(".m3mp", ".xml"));

                    if (m3mpFileInfo == null)
                    {
                        MessageBox.Show($"{fileLists["m3mp"][i].Replace(".m3mp", ".xml")} - XmlDeserialize failed.", "Modify XML Info Was Null", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    FileMapping fm = Utilities.GetFileMappingIndex(m3mpFileInfo.Index, fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["m3mp"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    int mainUncompSize = m3mpFileInfo.MainUnCompressedSize;
                    int mainChunkCount = Utilities.ChunkAmount(mainUncompSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainUncompSize, mainChunkCount);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] m3mpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(m3mpChunkData, writer, (int)mainChunkSizes[j]);
                    }
                    mainCompressedSize.Add(mainUncompSize);

                    mainUncompressedSize.Add(mainUncompSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    progress = fileLists["m3mp"][i].Replace(filePathRemove, string.Empty);

                    headerIndex.Add(m3mpFileInfo.Index);

                    int percentProgress = (i + 1) * 100 / fileLists["m3mp"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                }

                for (int i = 0; i < fileLists["bsb"].Length; i++)
                {
                    FileMapping fm = Utilities.GetFileMappingIndex(fileLists["bsb"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["bsb"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    int mainSize = (int)Utilities.FileInfo(fileLists["bsb"][i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(fileLists["bsb"][i]);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fm.Entries[0].Index);

                    progress = fileLists["bsb"][i].Replace(filePathRemove, string.Empty);

                    int percentProgress = (i + 1) * 100 / fileLists["bsb"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                }

                for (int i = 0; i < fileLists["bmf"].Length; i++)
                {
                    FileMapping fm = Utilities.GetFileMappingIndex(fileLists["bmf"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["bmf"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    int mainSize = (int)Utilities.FileInfo(fileLists["bmf"][i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(fileLists["bmf"][i]);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fm.Entries[0].Index);

                    progress = fileLists["bmf"][i].Replace(filePathRemove, string.Empty);

                    int percentProgress = (i + 1) * 100 / fileLists["bmf"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                }

                for (int i = 0; i < fileLists["bank"].Length; i++)
                {
                    FileMapping fm = Utilities.GetFileMappingIndex(fileLists["bank"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["bank"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    int mainSize = (int)Utilities.FileInfo(fileLists["bank"][i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(fileLists["bank"][i]);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fm.Entries[0].Index);

                    progress = fileLists["bank"][i].Replace(filePathRemove, string.Empty);

                    int percentProgress = (i + 1) * 100 / fileLists["bank"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                }

                for (int i = 0; i < fileLists["fsb"].Length; i++)
                {
                    FileMapping fm = Utilities.GetFileMappingIndex(fileLists["fsb"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["fsb"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    int mainSize = (int)Utilities.FileInfo(fileLists["fsb"][i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(fileLists["fsb"][i]);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fm.Entries[0].Index);

                    progress = fileLists["fsb"][i].Replace(filePathRemove, string.Empty);

                    int percentProgress = (i + 1) * 100 / fileLists["fsb"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                }

                for (int i = 0; i < fileLists["fev"].Length; i++)
                {
                    FileMapping fm = Utilities.GetFileMappingIndex(fileLists["fev"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["fev"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    int mainSize = (int)Utilities.FileInfo(fileLists["fev"][i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(fileLists["fev"][i]);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fm.Entries[0].Index);

                    progress = fileLists["fev"][i].Replace(filePathRemove, string.Empty);

                    int percentProgress = (i + 1) * 100 / fileLists["fev"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                }

                for (int i = 0; i < fileLists["wav"].Length; i++)
                {
                    FileMapping fm = Utilities.GetFileMappingIndex(fileLists["wav"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["wav"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    int mainSize = (int)Utilities.FileInfo(fileLists["wav"][i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(fileLists["wav"][i]);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fm.Entries[0].Index);

                    progress = fileLists["wav"][i].Replace(filePathRemove, string.Empty);

                    int percentProgress = (i + 1) * 100 / fileLists["wav"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                }

                for (int i = 0; i < fileLists["dat"].Length; i++)
                {
                    string datFilePath = fileLists["dat"][i];
                    int index = Convert.ToInt32(Path.GetFileNameWithoutExtension(datFilePath));
                    var blobsetHeaderData = Global.blobsetHeaderData.Entries[index];


                    mainUncompressedSize.Add((int)blobsetHeaderData.MainUnCompressedSize);
                    vramUncompressedSize.Add((int)blobsetHeaderData.VramUnCompressedSize);

                    int mainChunkCount = Utilities.ChunkAmount((int)blobsetHeaderData.MainUnCompressedSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes((int)blobsetHeaderData.MainUnCompressedSize, mainChunkCount);

                    int vramChunkCount = Utilities.ChunkAmount((int)blobsetHeaderData.VramUnCompressedSize);
                    long[] vramChunkSizes = Utilities.ChunkSizes((int)blobsetHeaderData.VramUnCompressedSize, vramChunkCount);

                    Reader? br = new(datFilePath);

                    if (blobsetHeaderData.MainCompressedSize == blobsetHeaderData.MainUnCompressedSize)
                    {
                        if (blobsetHeaderData.VramCompressedSize == blobsetHeaderData.VramUnCompressedSize)
                        {
                            for (int j = 0; j < mainChunkCount; j++)
                            {
                                byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                                IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                            }

                            if ((int)blobsetHeaderData.VramCompressedSize != 0)
                            {
                                for (int j = 0; j < vramChunkCount; j++)
                                {
                                    byte[] tmpChunkData = br.ReadBytes((int)vramChunkSizes[j]);
                                    IO.ReadWriteData(tmpChunkData, writer, (int)vramChunkSizes[j]);
                                }
                            }
                            mainCompressedSize.Add((int)blobsetHeaderData.MainCompressedSize);
                            vramCompressedSize.Add((int)blobsetHeaderData.VramCompressedSize);
                        }
                        else
                        {
                            for (int j = 0; j < mainChunkCount; j++)
                            {
                                byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                                IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                            }

                            byte[] vram = br.ReadBytes((int)blobsetHeaderData.VramUnCompressedSize);

                            int datVramCompressedSize = LZMA_IO.CompressAndWrite(vram, writer, chunkSize);
                            mainCompressedSize.Add((int)blobsetHeaderData.MainCompressedSize);
                            vramCompressedSize.Add(datVramCompressedSize);
                        }
                    }
                    else
                    {
                        if (blobsetHeaderData.VramCompressedSize == blobsetHeaderData.VramUnCompressedSize)
                        {
                            byte[] main = br.ReadBytes((int)blobsetHeaderData.VramUnCompressedSize);

                            int datMainCompressedSize = LZMA_IO.CompressAndWrite(main, writer, chunkSize);
                            mainCompressedSize.Add(datMainCompressedSize);

                            if ((int)blobsetHeaderData.VramCompressedSize != 0)
                            {
                                for (int j = 0; j < vramChunkCount; j++)
                                {
                                    byte[] tmpChunkData = br.ReadBytes((int)vramChunkSizes[j]);
                                    IO.ReadWriteData(tmpChunkData, writer, (int)vramChunkSizes[j]);
                                }
                            }

                            vramCompressedSize.Add((int)blobsetHeaderData.VramCompressedSize);
                        }
                        else
                        {
                            byte[] main = br.ReadBytes((int)blobsetHeaderData.VramUnCompressedSize);

                            int datMainCompressedSize = LZMA_IO.CompressAndWrite(main, writer, chunkSize);
                            mainCompressedSize.Add(datMainCompressedSize);

                            byte[] vram = br.ReadBytes((int)blobsetHeaderData.VramUnCompressedSize);

                            int datVramCompressedSize = LZMA_IO.CompressAndWrite(vram, writer, chunkSize);
                            vramCompressedSize.Add(datVramCompressedSize);
                        }
                    }
                    if (br != null) { br.Close(); br = null; }
                }

                mainFinalOffSet.Add(0);
                vramFinalOffSet.Add((uint)mainCompressedSize[0]);
                mainFinalOffSet.Add((uint)(vramFinalOffSet[0] + vramCompressedSize[0]));

                for (int i = 1; i < fullListLength; i++)
                {
                    vramFinalOffSet.Add((uint)(mainFinalOffSet[i] + mainCompressedSize[i]));
                    mainFinalOffSet.Add((uint)(vramFinalOffSet[i] + vramCompressedSize[i]));
                }

                blobsetHeader_bw = new Writer(blobsetFile, Global.isBigendian ? Endian.Big : Endian.Little);

                const int HeaderSize = 12;
                const int DataSize = 32;
                const int DefaultHashSize = 20;
                const int NoHashSize = 0;
                const int SkipValue = 4;
                const int BlobSetNumber = 1; // Constant for blobSetNumber

                int hashSize = DefaultHashSize;

                switch (Global.gameInfo.GameId)
                {
                    case 0: // AFLL has no 20 byte hash
                    case 5 when Global.isBigendian: // TableTop Cricket has no hash if big-endian
                        hashSize = NoHashSize;
                        break;
                }

                // Set the initial position for writing the blobset count
                blobsetHeader_bw.Position = hashSize + SkipValue;
                blobsetHeader_bw.WriteInt32(2); // Write blobset count

                // Write data for each entry in the full list
                for (int i = 0; i < fullListLength; i++)
                {
                    // Calculate the position based on headerIndex
                    int positionOffset = (headerIndex[i] == 0)
                        ? hashSize + HeaderSize
                        : (DataSize * headerIndex[i]) + (hashSize + HeaderSize);

                    blobsetHeader_bw.Position = positionOffset;

                    // Write the various sizes and offsets
                    blobsetHeader_bw.WriteUInt32(mainFinalOffSet[i]); // mainFinalOffSet
                    blobsetHeader_bw.WriteInt32(mainCompressedSize[i]); // mainCompressedSize
                    blobsetHeader_bw.WriteInt32(mainUncompressedSize[i]); // mainUncompressedSize
                    blobsetHeader_bw.WriteUInt32(vramFinalOffSet[i]); // vramFinalOffSet
                    blobsetHeader_bw.WriteInt32(vramCompressedSize[i]); // vramCompressedSize
                    blobsetHeader_bw.WriteInt32(vramUncompressedSize[i]); // vramUncompressedSize
                    blobsetHeader_bw.Position += SkipValue; // pathHash
                    blobsetHeader_bw.WriteInt32(BlobSetNumber); // blobSetNumber
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : \n\nFile: {_filePath} \n\n {ex}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return true;
            }
            finally
            {
                if (fileMapping_br != null) { fileMapping_br.Close(); fileMapping_br = null; }
                if (blobsetHeader_bw != null) { blobsetHeader_bw.Close(); blobsetHeader_bw = null; }
                if (writer != null) { writer.Dispose(); writer = null; }
                UI.BlobsetHeaderData();
            }
            return false;
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
        public static bool BlobsetV2(string blobsetfile, BackgroundWorker Modify_bgw)
        {
            bool error = true;
            return error;
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
        public static bool BlobsetV3(string blobsetfile, BackgroundWorker Modify_bgw)
        {
            bool error = true;
            return error;
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
            Reader? fileMapping_br = null;
            Writer? blobsetHeader_bw = null;
            FileStream? writer = null;

            string progress = string.Empty;
            string _filePath = string.Empty;
            int chunkSize = 262144;

            try
            {
                if (Global.blobsetHeaderData == null)
                {
                    MessageBox.Show("Blobset header data is null", "Blobset Header Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return true;
                }

                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];

                int blobsetFileCount = (int)Global.blobsetHeaderData.FilesCount;
                string basePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt);
                string modsFolder = Path.Combine(basePath, "mods");

                // Define an array of file extensions to search for
                string[] fileExtensions = { "dds", "txpk", "m3mp", "wem", "bnk" };

                // Initialize a dictionary to hold file lists and their lengths
                var fileLists = new Dictionary<string, string[]>();
                int fullListLength = 0;

                // Iterate over each file extension, populate the dictionary, and calculate total length
                foreach (var extension in fileExtensions)
                {
                    string[] fileList = Utilities.DirectoryInfo(modsFolder, $"*.{extension}");
                    fileLists[extension] = fileList;
                    fullListLength += fileList.Length;
                }

                if (fullListLength == 0)
                {
                    MessageBox.Show("No files to modify blobset", "No Supported Files Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return true;
                }

                string blobsetFilename = Path.GetFileName(blobsetFile);

                fileMapping_br = new(Path.Combine(basePath, "data", blobsetFilename + ".mapping"));

                FileMapping fileMapping = new();
                fileMapping.Read(fileMapping_br);

                if (fileMapping == null)
                {
                    MessageBox.Show("Looks like the file mapping data is corrupted. Might need to run 'Update File Mapping Data' in Settings or Validate Steam Files", "File Mapping Data Error", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return true;
                }

                List<int> mainCompressedSize = [];
                List<int> mainUncompressedSize = [];
                List<int> vramCompressedSize = [];
                List<int> vramUncompressedSize = [];
                List<int> headerIndex = [];

                string filePathRemove = modsFolder + "\\";
                string backupFilePath = Path.Combine(basePath, "backup");

                string gameLocation = Path.GetDirectoryName(blobsetFile);

                for (int i = 0; i < fileLists["dds"].Length; i++)
                {
                    FileMapping fm = Utilities.GetFileMappingIndex(fileLists["dds"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["dds"][i]} - fileIndex can't be found, make sure the dds file name or location is correct. Example: Don't have this in the file name (1).", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    string folderName = fm.Entries[0].FolderHash;
                    string fileName = fm.Entries[0].FileNameHash;
                    string filePath = Path.Combine(gameLocation, folderName, fileName);
                    _filePath = filePath;

                    BackupFilesV3V4(filePath, folderName, fileName, backupFilePath);

                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    Mini_TXPK mini_TXPK = new();
                    mini_TXPK.Serialize(fileLists["dds"][i], fileLists["dds"][i].Replace(filePathRemove, string.Empty), writer);

                    int mainUnCompSize = (int)Utilities.FileInfo(filePath);
                    int vramUncompSize = (int)Utilities.FileInfo(fileLists["dds"][i]);

                    mainCompressedSize.Add(mainUnCompSize);
                    mainUncompressedSize.Add(mainUnCompSize);

                    int chunkCount = Utilities.ChunkAmount(vramUncompSize);
                    long[] chunkSizes = Utilities.ChunkSizes(vramUncompSize, chunkCount);

                    Reader? br = new(fileLists["dds"][i]);

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

                    progress = fileLists["dds"][i].Replace(filePathRemove, string.Empty);
                    int percentProgress = (i + 1) * 100 / fileLists["dds"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                for (int i = 0; i < fileLists["txpk"].Length; i++)
                {
                    if (!File.Exists(fileLists["txpk"][i].Replace(".txpk", ".xml")))
                    {
                        MessageBox.Show($"Can't find TXPK xml info - {fileLists["txpk"][i].Replace(".txpk", ".xml")}", "XML File Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    ModifyFileInfo txpkXmlInfo = IO.XmlDeserialize<ModifyFileInfo>(fileLists["txpk"][i].Replace(".txpk", ".xml"));

                    if (txpkXmlInfo == null)
                    {
                        MessageBox.Show($"{fileLists["txpk"][i].Replace(".txpk", ".xml")} - XmlDeserialize failed.", "Modify XML Info Was Null", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    FileMapping fm = Utilities.GetFileMappingIndex(txpkXmlInfo.Index, fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["txpk"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    string folderName = fm.Entries[0].FolderHash;
                    string fileName = fm.Entries[0].FileNameHash;
                    string filePath = Path.Combine(gameLocation, folderName, fileName);
                    _filePath = filePath;

                    BackupFilesV3V4(filePath, folderName, fileName, backupFilePath);

                    Reader? br = new(fileLists["txpk"][i]);

                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    int mainChunkCount = Utilities.ChunkAmount(txpkXmlInfo.MainUnCompressedSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(txpkXmlInfo.MainUnCompressedSize, mainChunkCount);

                    int vramChunkCount = Utilities.ChunkAmount(txpkXmlInfo.VramUnCompressedSize);
                    long[] vramChunkSizes = Utilities.ChunkSizes(txpkXmlInfo.VramUnCompressedSize, vramChunkCount);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] txpkMainChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        ZSTD_IO.CompressAndWrite(txpkMainChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    int txpkMainCompressedSize = (int)writer.Position;
                    mainCompressedSize.Add(txpkMainCompressedSize);
                    mainUncompressedSize.Add(txpkXmlInfo.MainUnCompressedSize);

                    for (int j = 0; j < vramChunkCount; j++)
                    {
                        byte[] txpkVramChunkData = br.ReadBytes((int)vramChunkSizes[j]);

                        if ((int)vramChunkSizes[j] == chunkSize)
                            ZSTD_IO.CompressAndWrite(txpkVramChunkData, writer, (int)vramChunkSizes[j]);
                        else
                            IO.ReadWriteModifyData(txpkVramChunkData, writer, (int)vramChunkSizes[j]);
                    }

                    vramCompressedSize.Add((int)Utilities.FileInfo(filePath) - txpkMainCompressedSize);
                    vramUncompressedSize.Add(txpkXmlInfo.VramUnCompressedSize);

                    progress = fileLists["txpk"][i].Replace(filePathRemove, string.Empty);

                    headerIndex.Add(txpkXmlInfo.Index);

                    int percentProgress = (i + 1) * 100 / fileLists["txpk"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                for (int i = 0; i < fileLists["m3mp"].Length; i++)
                {
                    Reader? br = new(fileLists["m3mp"][i]);

                    ModifyFileInfo m3mpFileInfo = IO.XmlDeserialize<ModifyFileInfo>(fileLists["m3mp"][i].Replace(".m3mp", ".xml"));

                    if (m3mpFileInfo == null)
                    {
                        MessageBox.Show($"{fileLists["m3mp"][i].Replace(".m3mp", ".xml")} - XmlDeserialize failed.", "Modify XML Info Was Null", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    FileMapping fm = Utilities.GetFileMappingIndex(m3mpFileInfo.Index, fileMapping);

                    if (fm == null)
                    {
                        MessageBox.Show($"{fileLists["m3mp"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    string folderName = fm.Entries[0].FolderHash;
                    string fileName = fm.Entries[0].FileNameHash;
                    string filePath = Path.Combine(gameLocation, folderName, fileName);
                    _filePath = filePath;

                    BackupFilesV3V4(filePath, folderName, fileName, backupFilePath);

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

                    progress = fileLists["m3mp"][i].Replace(filePathRemove, string.Empty);

                    headerIndex.Add(m3mpFileInfo.Index);

                    int percentProgress = (i + 1) * 100 / fileLists["m3mp"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                for (int i = 0; i < fileLists["wem"].Length; i++)
                {
                    FileMapping fmIndex = Utilities.GetFileMappingIndex(fileLists["wem"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fmIndex == null)
                    {
                        MessageBox.Show($"{fileLists["wem"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    string folderName = fmIndex.Entries[0].FolderHash;
                    string fileName = fmIndex.Entries[0].FileNameHash;
                    string filePath = Path.Combine(gameLocation, folderName, fileName);
                    _filePath = filePath;

                    BackupFilesV3V4(filePath, folderName, fileName, backupFilePath);

                    int mainSize = (int)Utilities.FileInfo(fileLists["wem"][i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(fileLists["wem"][i]);
                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fmIndex.Entries[0].Index);

                    progress = fileLists["wem"][i].Replace(filePathRemove, string.Empty);

                    int percentProgress = (i + 1) * 100 / fileLists["wem"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                for (int i = 0; i < fileLists["bnk"].Length; i++)
                {
                    FileMapping fmIndex = Utilities.GetFileMappingIndex(fileLists["bnk"][i].Replace(filePathRemove, string.Empty), fileMapping);

                    if (fmIndex == null)
                    {
                        MessageBox.Show($"{fileLists["bnk"][i]} - fileIndex can't be found, make sure it's in the correct location.", "File Index Not Found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return true;
                    }

                    string folderName = fmIndex.Entries[0].FolderHash;
                    string fileName = fmIndex.Entries[0].FileNameHash;
                    string filePath = Path.Combine(gameLocation, folderName, fileName);
                    _filePath = filePath;

                    BackupFilesV3V4(filePath, folderName, fileName, backupFilePath);

                    int mainSize = (int)Utilities.FileInfo(fileLists["bnk"][i]);

                    mainCompressedSize.Add(mainSize);
                    mainUncompressedSize.Add(mainSize);
                    vramCompressedSize.Add(0);
                    vramUncompressedSize.Add(0);

                    int mainChunkCount = Utilities.ChunkAmount(mainSize);
                    long[] mainChunkSizes = Utilities.ChunkSizes(mainSize, mainChunkCount);

                    Reader? br = new(fileLists["bnk"][i]);
                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    for (int j = 0; j < mainChunkCount; j++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)mainChunkSizes[j]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)mainChunkSizes[j]);
                    }

                    headerIndex.Add(fmIndex.Entries[0].Index);

                    progress = fileLists["bnk"][i].Replace(filePathRemove, string.Empty);

                    int percentProgress = (i + 1) * 100 / fileLists["bnk"].Length;
                    Modify_bgw.ReportProgress(percentProgress, progress);

                    if (br != null) { br.Close(); br = null; }
                    if (writer != null) { writer.Dispose(); writer = null; }
                }

                string backupBlobsetPath = Path.Combine(backupFilePath, "data-0.blobset.pc");

                if (!File.Exists(backupBlobsetPath))
                {
                    File.Copy(blobsetFile, backupBlobsetPath);
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
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : \n\nFile: {_filePath} \n\n {ex.Message}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return true;
            }
            finally
            {
                if (fileMapping_br != null) { fileMapping_br.Close(); fileMapping_br = null; }
                if (blobsetHeader_bw != null) { blobsetHeader_bw.Close(); blobsetHeader_bw = null; }
                UI.BlobsetHeaderData();
            }
            return false;
        }
        #endregion

        #region Backup Files
        private static void BackupFilesV3V4(string filePath, string folderName, string fileName, string backupFilePath)
        {
            string backupFile = Path.Combine(backupFilePath, folderName, fileName);

            if (!File.Exists(backupFile))
            {
                string dir = Path.Combine(backupFilePath, folderName);
                Directory.CreateDirectory(dir);
                File.Move(filePath, backupFile);
            }

            if (File.Exists(filePath))
                File.Delete(filePath);
        }
        #endregion
    }
}

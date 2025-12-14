using BlobsetIO;
using Gameloop.Vdf;
using Gameloop.Vdf.JsonConverter;
using Gameloop.Vdf.Linq;
using Microsoft.Win32;
using PackageIO;
using Pfim;
using System.Buffers;
using System.Diagnostics;
using System.Runtime.InteropServices;
using static Blobset_Tools.Structs;

namespace Blobset_Tools
{
    /// <summary>
    /// UI functions.
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
    /// [Wouldubeinta]	   02/07/2025	Created
    /// </history>
    public class UI
    {
        public static void BlobsetHeaderData()
        {
            Reader? br = null;
            BlobsetFile? blobsetFile = null;

            try
            {
                if (!File.Exists(Global.gameInfo.GameLocation))
                    return;

                br = new(Global.gameInfo.GameLocation);

                blobsetFile = new BlobsetFile();
                blobsetFile.Deserialize(br, (Enums.BlobsetVersion)Global.gameInfo.BlobsetVersion);
                Global.blobsetHeaderData = blobsetFile;
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
            }
        }

        public static void FilesList(TreeView folder_treeView)
        {
            Reader? br = null;
            FileMapping? fileMapping = null;

            try
            {
                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];

                string blobsetFilename = Path.GetFileName(Global.gameInfo.GameLocation);

                br = new Reader(Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "data", blobsetFilename + ".mapping"));

                fileMapping = new();
                fileMapping.Read(br);

                if (fileMapping == null | fileMapping.FilesCount == 0)
                    return;

                ImageList myImageList = new();
                myImageList.Images.Add(Properties.Resources.folder_32);

                folder_treeView.ImageList = myImageList;

                folder_treeView.Nodes.Add(MakeTreeFromPaths(fileMapping, blobsetFilename));

                if (folder_treeView.Nodes.Count > 0)
                {
                    folder_treeView.Nodes[0].Expand();
                    folder_treeView.SelectedNode = folder_treeView.Nodes[0];
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (fileMapping != null) { fileMapping = null; }
            }
        }
        public static int getLVSelectedIndex(ListView file_listView)
        {
            int Index = -1;

            for (int i = 0; i < file_listView.Items.Count; i++)
            {
                if (file_listView.Items[i].Selected == true)
                {
                    Index = i;
                    break;
                }
            }
            return Index;
        }

        public static Bitmap DDStoBitmap(byte[] ddsData, bool hasAlpha, ref DDSInfo ddsInfo)
        {
            MemoryStream? ms = null;
            IImage? image = null;
            ArrayPoolAllocator? allocator = null;
            Bitmap? bitmap = null;
            ddsInfo.IFormat = ImageFormat.Rgba32;

            try
            {
                ddsInfo.Size = ddsData.Length;

                if (ddsData.Length > 124)
                {
                    ms = new(ddsData);

                    allocator = new();
                    var config = new PfimConfig(allocator: allocator);
                    image = Pfimage.FromStream(ms, config);

                    if (image == null)
                        return bitmap;

                    ddsInfo.CompressionAlgorithm = image.DDSHeader.PixelFormat.FourCC;

                    if (image.DDSHeader10 != null)
                    {
                        ddsInfo.dxgiFormat = image.DDSHeader10.DxgiFormat;
                        ddsInfo.isDX10 = true;
                    }
                    else
                        ddsInfo.isDX10 = false;

                    System.Drawing.Imaging.PixelFormat format = System.Drawing.Imaging.PixelFormat.Format32bppArgb;
                    ddsInfo.Width = image.Width;
                    ddsInfo.Height = image.Height;
                    ddsInfo.IFormat = image.Format;
                    ddsInfo.MipMap = (int)image.DDSHeader.MipMapCount + 1;

                    // Convert from Pfim's backend agnostic image format into GDI+'s image format
                    switch (image.Format)
                    {
                        case ImageFormat.Rgb24:
                            format = System.Drawing.Imaging.PixelFormat.Format24bppRgb;
                            break;

                        case ImageFormat.Rgba32:
                            if (hasAlpha)
                                format = System.Drawing.Imaging.PixelFormat.Format32bppArgb;
                            else
                                format = System.Drawing.Imaging.PixelFormat.Format32bppRgb;
                            break;

                        case ImageFormat.R5g5b5:
                            format = System.Drawing.Imaging.PixelFormat.Format16bppRgb555;
                            break;

                        case ImageFormat.R5g6b5:
                            format = System.Drawing.Imaging.PixelFormat.Format16bppRgb565;
                            break;

                        case ImageFormat.R5g5b5a1:
                            format = System.Drawing.Imaging.PixelFormat.Format16bppArgb1555;
                            break;

                        case ImageFormat.Rgb8:
                            format = System.Drawing.Imaging.PixelFormat.Format8bppIndexed;
                            break;
                    }

                    // Pin pfim's data array so that it doesn't get reaped by GC, unnecessary
                    // in this snippet but useful technique if the data was going to be used in
                    // control like a picture box
                    var handle = GCHandle.Alloc(image.Data, GCHandleType.Normal);
                    try
                    {
                        var data = Marshal.UnsafeAddrOfPinnedArrayElement(image.Data, 0);
                        bitmap = new(image.Width, image.Height, image.Stride, format, data);
                    }
                    finally
                    {
                        handle.Free();
                    }
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (ms != null) { ms.Dispose(); ms = null; }
                if (image != null) { image.Dispose(); image = null; }
                if (allocator != null) { allocator = null; }
            }
            return bitmap;
        }

        public static byte[] GetDDSData_V1_V2(List<FileIndexInfo> list)
        {
            Reader? br = null;
            byte[]? ddsData = null;

            try
            {
                br = new(Global.gameInfo.GameLocation.Replace("-0", "-" + Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].BlobSetNumber));

                Endian endian = Endian.Little;

                if (Global.isBigendian)
                    endian = Endian.Big;

                br.CurrentEndian = endian;

                uint VramFinalOffset = Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].VramFinalOffSet;
                uint VramCompressedSize = Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].VramCompressedSize;
                uint VramUnCompressedSize = Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].VramUnCompressedSize;

                ddsData = new byte[VramUnCompressedSize];

                br.Position = VramFinalOffset;

                if (VramCompressedSize != VramUnCompressedSize)
                {
                    int size = 0;
                    int chunkCount = br.ReadInt32();
                    int[] chunkCompressedSize = new int[chunkCount];

                    for (int j = 0; j < chunkCount; j++)
                    {
                        chunkCompressedSize[j] = br.ReadInt32();
                        chunkCompressedSize[j] = chunkCompressedSize[j] -= 4;
                    }

                    for (int j = 0; j < chunkCount; j++)
                    {
                        int chunkUnCompressedSize = br.ReadInt32();

                        if (chunkCompressedSize[j] == chunkUnCompressedSize)
                        {
                            byte[] tmpddsData = br.ReadBytes(chunkUnCompressedSize, Endian.Little);
                            Buffer.BlockCopy(tmpddsData, 0, ddsData, size, tmpddsData.Length);
                            size += tmpddsData.Length;
                        }
                        else
                        {
                            byte[] compressedTmpddsData = br.ReadBytes(chunkCompressedSize[j], Endian.Little);
                            byte[] tmpddsData = LZMA_IO.DecompressAndRead(compressedTmpddsData, chunkUnCompressedSize);
                            Buffer.BlockCopy(tmpddsData, 0, ddsData, size, tmpddsData.Length);
                            size += tmpddsData.Length;
                        }
                    }
                }
                else
                {
                    ddsData = br.ReadBytes(Convert.ToInt32(VramUnCompressedSize), Endian.Little);
                }

                if (Global.isBigendian)
                {
                    Mini_TXPK miniTxpk = LZMA_IO.ReadMiniTXPKInfo(list);
                    PS3_DDS consoleDDS = new(miniTxpk.DDSHeight, miniTxpk.DDSWidth, miniTxpk.DDSMipMaps, miniTxpk.DDSType, ddsData.Length, ddsData);
                    ddsData = consoleDDS.WriteDDS();
                }

                if (br != null) { br.Close(); br = null; }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
            }
            return ddsData;
        }

        public static byte[] GetDDSData_V3_V4(List<FileIndexInfo> list)
        {
            Reader? br = null;
            byte[]? ddsData = null;

            try
            {
                br = new(Path.Combine(Global.gameInfo.GameLocation.Replace("data-0.blobset.pc", string.Empty), list[Global.fileIndex].FolderHash, list[Global.fileIndex].FileHash));

                int MainCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                int MainUnCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;
                int VramCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].VramCompressedSize;
                int VramUnCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].VramUnCompressedSize;

                ddsData = new byte[VramUnCompressedSize];

                if (MainCompressedSize == MainUnCompressedSize)
                    br.Position = MainUnCompressedSize;
                else
                    br.Position = MainCompressedSize;

                if (VramCompressedSize != VramUnCompressedSize)
                {
                    int size = 0;

                    while (br.Position < br.Length)
                    {
                        int CompressedSize = br.ReadInt32();
                        int Tmp = CompressedSize -= 4;
                        CompressedSize = Tmp;

                        bool isCompressed = true;

                        byte[] Data = br.ReadBytes(CompressedSize);

                        byte[] ZstdMagicArray = [Data[0], Data[1], Data[2], Data[3]];
                        uint ZstdMagic = BitConverter.ToUInt32(ZstdMagicArray);

                        if (ZstdMagic != 4247762216)
                            isCompressed = false;

                        byte[] tmpddsData = ZSTD_IO.DecompressAndRead(Data, isCompressed);
                        Buffer.BlockCopy(tmpddsData, 0, ddsData, size, tmpddsData.Length);
                        size += tmpddsData.Length;
                    }
                    if (br != null) { br.Close(); br = null; }
                }
                else
                {
                    ddsData = br.ReadBytes(VramUnCompressedSize);
                    if (br != null) { br.Close(); br = null; }
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
            }
            return ddsData;
        }

        public class ArrayPoolAllocator : IImageAllocator
        {
            public byte[] Rent(int minLength)
            {
                return ArrayPool<byte>.Shared.Rent(minLength);
            }

            public void Return(byte[] array)
            {
                ArrayPool<byte>.Shared.Return(array);
            }
        }

        public static TreeNode MakeTreeFromPaths(FileMapping fileMapping, string rootNodeName = "", char separator = '\\')
        {
            TreeNode rootNode = new(rootNodeName) { ImageIndex = 0 };
            int index = 0;

            for (int i = 0; i < fileMapping.Entries.Count; i++)
            {
                string path = fileMapping.Entries[i].FilePath;

                TreeNode currentNode = rootNode;
                string[] pathItems = path.Split(separator);

                foreach (string item in pathItems)
                {
                    IEnumerable<TreeNode> tmp = currentNode.Nodes.Cast<TreeNode>().Where(x => x.Text.Equals(item));

                    if (item == pathItems[pathItems.Length - 1])
                    {
                        List<FileIndexInfo> fileInfo = new();
                        FileIndexInfo file = new()
                        {
                            FileName = item,
                            FilePath = fileMapping.Entries[i].FilePath,
                            MappingIndex = index,
                            BlobsetIndex = fileMapping.Entries[i].Index,
                            FileHash = fileMapping.Entries[i].FileNameHash,
                            FolderHash = fileMapping.Entries[i].FolderHash,
                        };

                        fileInfo.Add(file);

                        if (currentNode.Tag == null)
                            currentNode.Tag = fileInfo;
                        else
                        {
                            List<FileIndexInfo> getfileInfos = (List<FileIndexInfo>)currentNode.Tag;
                            getfileInfos.Add(file);
                            currentNode.Tag = getfileInfos;
                        }
                    }
                    else
                        currentNode = tmp.Count() > 0 ? tmp.Single() : currentNode.Nodes.Add(item);
                }
                index++;
            }
            return rootNode;
        }

        public static TreeNode MakeTreeFromPaths(string[] filePath, string rootNodeName = "", char separator = '\\')
        {
            TreeNode? rootNode = new(rootNodeName) { ImageIndex = 0 };
            int index = 0;

            for (int i = 0; i < filePath.Length; i++)
            {
                string path = filePath[i].Replace(@"/", @"\");

                TreeNode currentNode = rootNode;
                string[] pathItems = path.Split(separator);

                foreach (string item in pathItems)
                {
                    IEnumerable<TreeNode> tmp = currentNode.Nodes.Cast<TreeNode>().Where(x => x.Text.Equals(item));

                    if (item == pathItems[pathItems.Length - 1])
                    {
                        List<FileIndexInfo> fileInfo = new();
                        FileIndexInfo file = new()
                        {
                            FileName = item,
                            MappingIndex = index,
                        };

                        fileInfo.Add(file);

                        if (currentNode.Tag == null)
                            currentNode.Tag = fileInfo;
                        else
                        {
                            List<FileIndexInfo> getfileInfos = (List<FileIndexInfo>)currentNode.Tag;
                            getfileInfos.Add(file);
                            currentNode.Tag = getfileInfos;
                        }
                    }
                    else
                        currentNode = tmp.Count() > 0 ? tmp.Single() : currentNode.Nodes.Add(item);
                }
                index++;
            }
            return rootNode;
        }

        public static string getSteamLocation()
        {
            RegistryKey? key = null;
            string value = string.Empty;

            try
            {
                key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Wow6432Node\Valve\Steam", false);

                if (key != null)
                {
                    object? steamPath = key.GetValue("InstallPath", null);

                    if (steamPath != null)
                    {
                        string libararyFoldersPath = Path.Combine(steamPath.ToString(), "steamapps", "libraryfolders.vdf");

                        if (!File.Exists(libararyFoldersPath))
                            libararyFoldersPath = Path.Combine(steamPath.ToString(), "config", "libraryfolders.vdf");

                        if (File.Exists(libararyFoldersPath))
                        {
                            VProperty libraryfolders_vdf = VdfConvert.Deserialize(File.ReadAllText(libararyFoldersPath));
                            string slf = libraryfolders_vdf.ToJson().ToString();
                            SteamLibraryFolders? libraryFolders = System.Text.Json.JsonSerializer.Deserialize<SteamLibraryFolders>("{" + slf + "}");

                            bool hasGameId = false;

                            foreach (var libraryFolder in libraryFolders.LibraryFolders)
                            {
                                foreach (var app in libraryFolder.Value.Apps)
                                {
                                    if (app.Key.ToString() == Global.gameInfo.SteamGameId.ToString())
                                    {
                                        hasGameId = true;
                                        break;
                                    }
                                }

                                if (hasGameId)
                                {
                                    value = libraryFolder.Value.Path;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            catch (Exception arg)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + arg, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (key != null)
                {
                    key.Close();
                    key = null;
                }
            }
            return value;
        }

        public static void ValidateSteamGame()
        {
            uint steamID = Global.gameInfo.SteamGameId;

            if (steamID == 0)
            {
                MessageBox.Show("This is not a Steam game", "SteamID = 0", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            string steamLocation = getSteamLocation();

            if (steamLocation != string.Empty)
            {
                string gameAppID = steamID.ToString();

                Process? p = null;
                ProcessStartInfo? ps = null;

                try
                {
                    p = new();
                    ps = new();

                    ps.FileName = steamLocation + @"\Steam.exe";
                    string arg = " steam://validate/" + gameAppID;
                    ps.WorkingDirectory = steamLocation;
                    ps.Arguments = arg;
                    p.StartInfo = ps;
                    p.Start();
                }
                catch (Exception arg)
                {
                    MessageBox.Show("Error occurred, report it to Wouldy : " + arg, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                }
                finally
                {
                    if (p != null) { p.Dispose(); p = null; }
                }
            }
        }

        public static readonly string[] LoadingText =
        {
            "BigAnt for the support.",
            Environment.NewLine,
            "Luigi Auriemma for his expertise over the year's.",
            Environment.NewLine,
            "FeudalNate for PackageIO class.",
            Environment.NewLine,
            "Facebook & oleg-st for ZstdSharp - ",
            "https://github.com/oleg-st/ZstdSharp",
            Environment.NewLine,
            "nickbabcock for Pfim - ",
            "https://github.com/nickbabcock/Pfim",
            Environment.NewLine,
            "shravan2x for Gameloop.Vdf - ",
            "https://github.com/shravan2x/Gameloop.Vdf",
            Environment.NewLine,
            "JamesNK for Newtonsoft.Json - ",
            "https://github.com/JamesNK/Newtonsoft.Json",
            Environment.NewLine,
            "Crauzer for WEMSharp - ",
            "https://github.com/Crauzer/WEMSharp",
            Environment.NewLine,
            "knot3 for OggSharp - ",
            "https://github.com/knot3/OggSharp",
            Environment.NewLine,
            "lostromb for concentus.oggfile - ",
            "https://github.com/lostromb/concentus.oggfile",
            Environment.NewLine,
            "bartlomiejduda for PS3 and Xbox 360 image swizzling code - ",
            "https://github.com/bartlomiejduda",
            Environment.NewLine,
            Environment.NewLine,
        };
    }
}

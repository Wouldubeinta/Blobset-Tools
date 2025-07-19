using BlobsetIO;
using Microsoft.Win32;
using PackageIO;
using Pfim;
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
                if (!File.Exists(Properties.Settings.Default.GameLocation))
                    return;

                br = new(Properties.Settings.Default.GameLocation);

                blobsetFile = new BlobsetFile();
                blobsetFile.Deserialize(br, (Enums.BlobsetVersion)Properties.Settings.Default.BlobsetVersion);
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
                br = new Reader(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\data\BlobsetFileMapping.bin");

                fileMapping = new ();
                fileMapping.Read(br);

                if (fileMapping == null | fileMapping.FilesCount == 0)
                    return;

                ImageList myImageList = new();
                myImageList.Images.Add(Properties.Resources.folder_32);

                folder_treeView.ImageList = myImageList;

                folder_treeView.Nodes.Add(MakeTreeFromPaths(fileMapping, "data-0.blobset.pc"));

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

        public static Bitmap DDStoBitmap(byte[] ddsData, ref DDSInfo ddsInfo)
        {
            MemoryStream? ms = null;
            IImage? image = null;
            Bitmap? bitmap = null;
            ddsInfo.MipMap = 1;
            ddsInfo.IFormat = ImageFormat.Rgba32;

            try
            {
                ddsInfo.Size = ddsData.Length;

                if (ddsData.Length > 124)
                {
                    ms = new (ddsData);

                    byte[] tmp = new byte[4];
                    ms.Position = 84;
                    ms.Read(tmp, 0, 4);
                    ddsInfo.PFormat = (PixelFormat)BitConverter.ToInt32(tmp);

                    ms.Position = 0;

                    switch (ddsInfo.PFormat)
                    {
                        case PixelFormat.A16B16G16R16:
                        case PixelFormat.A16B16G16R16F:
                        case PixelFormat.A32B32G32R32F:
                        case PixelFormat.R16F:
                        case PixelFormat.R32F:
                        case PixelFormat.G16R16F:
                        case PixelFormat.G32R32F:
                            break;
                        default:
                            image = Pfimage.FromStream(ms);

                            System.Drawing.Imaging.PixelFormat format = System.Drawing.Imaging.PixelFormat.Format32bppArgb;
                            ddsInfo.IFormat = image.Format;
                            ddsInfo.MipMap = image.MipMaps.Length + 1;

                            // Convert from Pfim's backend agnostic image format into GDI+'s image format
                            switch (image.Format)
                            {
                                case ImageFormat.Rgb24:
                                    format = System.Drawing.Imaging.PixelFormat.Format24bppRgb;
                                    break;

                                case ImageFormat.Rgba32:
                                    format = System.Drawing.Imaging.PixelFormat.Format32bppArgb;
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
                            var handle = GCHandle.Alloc(image.Data, GCHandleType.Pinned);
                            try
                            {
                                var data = Marshal.UnsafeAddrOfPinnedArrayElement(image.Data, 0);
                                bitmap = new Bitmap(image.Width, image.Height, image.Stride, format, data);
                                if (ms != null) { ms.Close(); ms = null; }
                                if (image != null) { image.Dispose(); image = null; }
                            }
                            finally
                            {
                                handle.Free();
                            }
                            break;
                    }
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (ms != null) { ms.Close(); ms = null; }
                if (image != null) { image.Dispose(); image = null; }
                GC.Collect();
            }
            return bitmap;
        }

        public static byte[] GetDDSData(List<Structs.FileIndexInfo> list)
        {
            Reader? br = null;
            byte[]? ddsData = null;

            try
            {
                br = new (Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + list[Global.fileIndex].FolderHash + @"\" + list[Global.fileIndex].FileHash);

                int MainCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                int MainUnCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;
                int VramCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].VramCompressedSize;
                int VramUnCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].VramUnCompressedSize;

                if (MainCompressedSize != MainUnCompressedSize) 
                {
                    br.Position = MainCompressedSize;
                }
                else
                    br.Position = MainUnCompressedSize;

                ddsData = new byte[VramUnCompressedSize];

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

        public static Bitmap TXPK_DDStoBitmap(byte[] txpkData, int ddsSize, long ddsOffset, ref int mipmapCount, ref ImageFormat fmt, ref PixelFormat ddsFormat)
        {
            MemoryStream? ms = null;
            IImage? image = null;
            Bitmap? bitmap = null;
            mipmapCount = 1;
            fmt = ImageFormat.Rgba32;

            try
            {
                if (txpkData != null)
                {
                    byte[] ddsData = new byte[ddsSize];
                    Buffer.BlockCopy(txpkData, (int)ddsOffset, ddsData, 0, ddsSize);

                    if (ddsData.Length > 124)
                    {
                        ms = new (ddsData);

                        byte[] tmp = new byte[4];
                        ms.Position = 84;
                        ms.Read(tmp, 0, 4);
                        ddsFormat = (PixelFormat)BitConverter.ToInt32(tmp);

                        ms.Position = 0;

                        switch (ddsFormat)
                        {
                            case PixelFormat.A16B16G16R16:
                            case PixelFormat.A16B16G16R16F:
                            case PixelFormat.A32B32G32R32F:
                            case PixelFormat.R16F:
                            case PixelFormat.R32F:
                            case PixelFormat.G16R16F:
                            case PixelFormat.G32R32F:
                                break;
                            default:
                                image = Pfimage.FromStream(ms);

                                System.Drawing.Imaging.PixelFormat format = System.Drawing.Imaging.PixelFormat.Format32bppArgb;
                                fmt = image.Format;
                                mipmapCount = image.MipMaps.Length + 1;

                                // Convert from Pfim's backend agnostic image format into GDI+'s image format
                                switch (image.Format)
                                {
                                    case ImageFormat.Rgb24:
                                        format = System.Drawing.Imaging.PixelFormat.Format24bppRgb;
                                        break;

                                    case ImageFormat.Rgba32:
                                        format = System.Drawing.Imaging.PixelFormat.Format32bppArgb;
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
                                var handle = GCHandle.Alloc(image.Data, GCHandleType.Pinned);
                                try
                                {
                                    var data = Marshal.UnsafeAddrOfPinnedArrayElement(image.Data, 0);
                                    bitmap = new Bitmap(image.Width, image.Height, image.Stride, format, data);
                                }
                                finally
                                {
                                    handle.Free();
                                }
                                break;
                        }
                    }
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (ms != null) { ms.Close(); ms = null; }
                if (image != null) { image.Dispose(); image = null; }
                GC.Collect();
            }
            return bitmap;
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
                        List<Structs.FileIndexInfo> fileInfo = new();
                        Structs.FileIndexInfo file = new()
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
                            List<Structs.FileIndexInfo> getfileInfos = (List<Structs.FileIndexInfo>)currentNode.Tag;
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
                        List<Structs.FileIndexInfo> fileInfo = new();
                        Structs.FileIndexInfo file = new()
                        {
                            FileName = item,
                            MappingIndex = index,
                        };

                        fileInfo.Add(file);

                        if (currentNode.Tag == null)
                            currentNode.Tag = fileInfo;
                        else
                        {
                            List<Structs.FileIndexInfo> getfileInfos = (List<Structs.FileIndexInfo>)currentNode.Tag;
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
            object value = string.Empty;

            try
            {
                key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Wow6432Node\Valve\Steam", false);

                if (key != null)
                {
                    object tmp = key.GetValue("InstallPath", null);

                    if (tmp != null)
                        value = tmp;
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
            return value.ToString();
        }

        public static void ValidateSteamGame()
        {
            int steamID = Properties.Settings.Default.GameID;

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
                ProcessStartInfo ps = null;

                try
                {
                    p = new ();
                    ps = new ();

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

        public static string[] LoadingText =
        {
            "*** " + "Blobset Tools - v" + Global.version + " ***",
            Environment.NewLine,
            "Description: Extract and Modify BigAnt games blobset files",
            Environment.NewLine,
            Environment.NewLine,
            "Author: Wouldubeinta",
            Environment.NewLine,
            "Discord ID: Wouldubeinta",
            Environment.NewLine,
            Environment.NewLine,
            "*** Special Thanks To ***",
            Environment.NewLine,
            "BigAnt for the support",
            Environment.NewLine,
            "Luigi Auriemma for he's expertise over the year's",
            Environment.NewLine,
            "FeudalNate for PackageIO class",
            Environment.NewLine,
            "Facebook & oleg-st for ZstdSharp - ",
            "https://github.com/oleg-st/ZstdSharp",
            Environment.NewLine,
            "nickbabcock for Pfim - ",
            "https://github.com/nickbabcock/Pfim",
            Environment.NewLine,
            Environment.NewLine,
        };
    }
}

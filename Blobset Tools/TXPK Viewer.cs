using BlobsetIO;
using PackageIO;
using System.ComponentModel;

namespace Blobset_Tools
{
    public partial class TXPK_Viewer : Form
    {
        private readonly string filename;
        private int fileIndex = 0;
        private readonly TXPK txpkData;
        private readonly List<Structs.FileIndexInfo> list;
        private BackgroundWorker? TXPKDecompress_bgw = null;
        private BackgroundWorker? TXPKExtract_bgw = null;
        private ImageList? myImageList = null;
        private readonly bool isAlpha = true;
        private bool isFlipped = false;

        private uint MainFinalOffset = 0;
        private uint MainCompressedSize = 0;
        private uint MainUnCompressedSize = 0;
        private uint VramFinalOffset = 0;
        private uint VramCompressedSize = 0;
        private uint VramUnCompressedSize = 0;

        public TXPK_Viewer(string _filename, TXPK _txpkData, List<Structs.FileIndexInfo> _list)
        {
            InitializeComponent();
            filename = _filename;
            txpkData = _txpkData;
            list = _list;
        }

        private void TXPK_Viewer_Load(object sender, EventArgs e)
        {
            if (txpkData != null)
            {
                var blobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];

                MainFinalOffset = blobsetHeaderData.MainFinalOffSet;
                MainCompressedSize = blobsetHeaderData.MainCompressedSize;
                MainUnCompressedSize = blobsetHeaderData.MainUnCompressedSize;
                VramFinalOffset = blobsetHeaderData.VramFinalOffSet;
                VramCompressedSize = blobsetHeaderData.VramCompressedSize;
                VramUnCompressedSize = blobsetHeaderData.VramUnCompressedSize;

                Text = Text + " - " + filename;
                myImageList = new ImageList();
                myImageList.Images.Add(Properties.Resources.folder_32);
                myImageList.Images.Add(Properties.Resources.dds_32);

                folder_treeView.ImageList = myImageList;

                string[] ddsPaths = new string[txpkData.FilesCount];

                for (int i = 0; i < txpkData.FilesCount; i++)
                {
                    ddsPaths[i] = $"{txpkData.Entries[i].DDSFilePath}.dds";
                }

                folder_treeView.Nodes.Add(UI.MakeTreeFromPaths(ddsPaths, Path.GetFileName(filename)));

                if (folder_treeView.Nodes.Count > 0)
                {
                    folder_treeView.Nodes[0].Expand();
                    folder_treeView.SelectedNode = folder_treeView.Nodes[0];
                }

                string txpkName = Path.GetFileName(filename);

                foreach (string f in Directory.EnumerateFiles(Global.currentPath + @"\temp\", "*.*"))
                    File.Delete(f);

                TXPKDecompressBgw();
            }
        }

        private void folder_treeView_AfterSelect(object sender, TreeViewEventArgs e)
        {
            files_listView.Refresh();
            files_listView.Clear();
            status_Label.Text = string.Empty;

            if (folder_treeView.SelectedNode == null)
                return;

            files_listView.SmallImageList = myImageList;

            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            if (list == null)
                return;

            ListViewItem lvi;

            for (int i = 0; i < list.Count; i++)
            {
                int icon = 1;

                lvi = files_listView.Items.Add(new ListViewItem { ImageIndex = icon, Text = list[i].FileName });
            }

            status_Label.Text = $"{list.Count} items in {folder_treeView.SelectedNode.Text} folder";
        }

        private void files_listView_SelectedIndexChanged(object sender, EventArgs e)
        {
            fileIndex = UI.getLVSelectedIndex(files_listView);
            LoadImage(alphaToolStripMenuItem.Checked);
            pictureBox1.Refresh();
        }

        private void LoadImage(bool hasAlpha)
        {
            Reader? br = null;

            try
            {
                string txpkName = Path.Combine(Global.currentPath, "temp", Path.GetFileName(filename));
                br = new Reader(txpkName);

                if (fileIndex == -1) return;

                TXPK txpk = new();
                txpk.Deserialize(br);

                if (txpk == null) return;

                int index = Get_DDS_Index(files_listView.Items[fileIndex].Text, txpk);

                if (index == -1) return;

                br.Position = br.Position + txpk.Entries[index].DDSDataOffset;
                byte[] ddsData = br.ReadBytes((int)txpk.Entries[index].DDSDataSize, Endian.Little);

                if (Global.isBigendian)
                {
                    PS3_DDS consoleDDS = new(txpk.Entries[index].DDSHeight, txpk.Entries[index].DDSWidth, txpk.Entries[index].DDSMipMaps, txpk.Entries[index].DDSType, ddsData.Length, ddsData);
                    ddsData = consoleDDS.WriteDDS();
                }

                Structs.DDSInfo ddsInfo = new();

                Bitmap bitmap = UI.DDStoBitmap(ddsData, hasAlpha, ref ddsInfo);

                string ddsFormat = ddsInfo.isDX10 ? ddsInfo.dxgiFormat.ToString() + " - DX11+" : ddsInfo.CompressionAlgorithm.ToString();

                if (bitmap != null)
                {
                    pictureBox1.Image = bitmap;

                    if (isFlipped)
                    {
                        pictureBox1.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
                        pictureBox1.Refresh();
                    }

                    DDSInfo_SSLabel.Text = $"Format: {ddsFormat} | Height: {bitmap.Height.ToString()} | Width: {bitmap.Width.ToString()} | MipMaps: 1/{ddsInfo.MipMap.ToString()}";
                }
                if (br != null) { br.Close(); br = null; }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : {ex.Message}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
            }
        }

        private void extractTXPKToolStripMenuItem_Click(object sender, EventArgs e)
        {
            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            string platformExt = platformDetails["PlatformExt"];

            extractTXPK_fbd.SelectedPath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "txpk");

            if (extractTXPK_fbd.ShowDialog() == DialogResult.OK)
            {
                TXPKExtractBgw();
            }
        }

        private void TXPKDecompressBgw()
        {
            TXPKDecompress_bgw = new BackgroundWorker();
            TXPKDecompress_bgw.DoWork += new DoWorkEventHandler(TXPKDecompress_bgw_DoWork);
            TXPKDecompress_bgw.ProgressChanged += new ProgressChangedEventHandler(TXPKDecompress_bgw_ProgressChanged);
            TXPKDecompress_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(TXPKDecompress_bgw_RunWorkerCompleted);
            TXPKDecompress_bgw.WorkerReportsProgress = true;
            TXPKDecompress_bgw.RunWorkerAsync();

            files_listView.Enabled = false;
            toolStripProgressBar.MarqueeAnimationSpeed = 10;
            toolStripProgressBar.Style = ProgressBarStyle.Marquee;
        }

        private void TXPKDecompress_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            bool errorCheck = false;
            int blobsetVersion = Global.gameInfo.BlobsetVersion;

            if (MainCompressedSize != MainUnCompressedSize && VramCompressedSize != VramUnCompressedSize)
                errorCheck = blobsetVersion > 2 ? TXPX_DecompressZSTD(false) : TXPX_DecompressLZMA();
            else
                errorCheck = blobsetVersion > 2 ? TXPX_DecompressZSTD(true) : TXPX_DecompressLZMA();

            if (errorCheck)
                e.Cancel = true;
        }

        private void TXPKDecompress_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            status_Label.Text = e.UserState.ToString() + e.ProgressPercentage;
        }

        private void TXPKDecompress_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.Text = "DDS TXPK has finished decompressing to a temp file for reading";
            }

            files_listView.Enabled = true;
            toolStripProgressBar.MarqueeAnimationSpeed = 100;
            toolStripProgressBar.Style = ProgressBarStyle.Blocks;
            if (TXPKDecompress_bgw != null) { TXPKDecompress_bgw.Dispose(); }
        }

        private bool TXPX_DecompressZSTD(bool isMainUnCompressed)
        {
            Reader? br = null;
            FileStream? fsWriter = null;

            try
            {
                br = new Reader(Path.Combine(Global.gameInfo.GameLocation.Replace("data-0.blobset.pc", string.Empty), list[Global.fileIndex].FolderHash, list[Global.fileIndex].FileHash));

                string txpkName = Path.GetFileName(filename);
                fsWriter = new FileStream(Path.Combine(Global.currentPath, "temp", txpkName), FileMode.OpenOrCreate, FileAccess.ReadWrite);

                if (isMainUnCompressed)
                {
                    byte[] txpkHeader = br.ReadBytes((int)MainUnCompressedSize);
                    fsWriter.Write(txpkHeader, 0, txpkHeader.Length);
                }

                int i = 0;

                while (br.Position < br.Length)
                {
                    int txpkCompressedSize = br.ReadInt32();
                    int txpkTmp = txpkCompressedSize -= 4;
                    txpkCompressedSize = txpkTmp;

                    bool isCompressed = true;

                    byte[] txpkData = br.ReadBytes(txpkCompressedSize);

                    byte[] ZstdMagicArray = [txpkData[0], txpkData[1], txpkData[2], txpkData[3]];
                    uint ZstdMagic = BitConverter.ToUInt32(ZstdMagicArray);

                    if (ZstdMagic != 4247762216)
                        isCompressed = false;

                    ZSTD_IO.DecompressAndWrite(txpkData, fsWriter, isCompressed);

                    TXPKDecompress_bgw.ReportProgress(i, "DDS TXPK is decompressing to temp file. Chunk: ");
                    i++;
                }
                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : {ex.Message}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return true;
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
            }
            return false;
        }

        private bool TXPX_DecompressLZMA()
        {
            Reader? br = null;
            FileStream? fsWriter = null;

            try
            {
                Endian endian = Endian.Little;

                if (Global.isBigendian)
                    endian = Endian.Big;

                br = new Reader(Global.gameInfo.GameLocation.Replace("-0", "-" + Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].BlobSetNumber), endian);

                string txpkName = Path.GetFileName(filename);
                fsWriter = new FileStream(Path.Combine(Global.currentPath, "temp", txpkName), FileMode.OpenOrCreate, FileAccess.ReadWrite);

                br.Position = MainFinalOffset;

                if (MainCompressedSize != MainUnCompressedSize)
                {
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
                            byte[] txpkHeader = br.ReadBytes(chunkUnCompressedSize, Endian.Little);
                            fsWriter.Write(txpkHeader, 0, txpkHeader.Length);
                        }
                        else
                        {
                            byte[] compressedTmptxpkData = br.ReadBytes(chunkCompressedSize[j], Endian.Little);
                            byte[] txpkHeader = LZMA_IO.DecompressAndRead(compressedTmptxpkData, chunkUnCompressedSize);
                            fsWriter.Write(txpkHeader, 0, txpkHeader.Length);
                        }
                    }
                }
                else
                {
                    byte[] txpkHeader = br.ReadBytes((int)MainUnCompressedSize, Endian.Little);
                    fsWriter.Write(txpkHeader, 0, txpkHeader.Length);
                }

                br.Position = VramFinalOffset;

                if (VramCompressedSize != VramUnCompressedSize)
                {
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
                            byte[] txpkData = br.ReadBytes(chunkUnCompressedSize, Endian.Little);
                            fsWriter.Write(txpkData, 0, txpkData.Length);
                        }
                        else
                        {
                            byte[] compressedTmptxpkData = br.ReadBytes(chunkCompressedSize[j], Endian.Little);
                            byte[] txpkData = LZMA_IO.DecompressAndRead(compressedTmptxpkData, chunkUnCompressedSize);
                            fsWriter.Write(txpkData, 0, txpkData.Length);
                        }
                    }
                }
                else
                {
                    byte[] txpkData = br.ReadBytes((int)VramUnCompressedSize, Endian.Little);
                    fsWriter.Write(txpkData, 0, txpkData.Length);
                }

                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : {ex.Message}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return true;
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
            }
            return false;
        }

        private void TXPKExtractBgw()
        {
            TXPKExtract_bgw = new BackgroundWorker();
            TXPKExtract_bgw.DoWork += new DoWorkEventHandler(TXPKExtract_bgw_DoWork);
            TXPKExtract_bgw.ProgressChanged += new ProgressChangedEventHandler(TXPKExtract_bgw_ProgressChanged);
            TXPKExtract_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(TXPKExtract_bgw_RunWorkerCompleted);
            TXPKExtract_bgw.WorkerReportsProgress = true;
            TXPKExtract_bgw.WorkerSupportsCancellation = true;
            TXPKExtract_bgw.RunWorkerAsync();

            files_listView.Enabled = false;
        }

        private void TXPKExtract_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            bool errorCheck = ExtractTXPK();

            if (errorCheck)
                e.Cancel = true;
        }

        private void TXPKExtract_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            int progressPercentage = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            progressStripStatusLabel.Text = $"{progressPercentage} %";
            status_Label.ForeColor = Color.Black;
            status_Label.Text = $"Extracting TXPK: {e.UserState}";
            toolStripProgressBar.Value = progressPercentage;
        }

        private void TXPKExtract_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                status_Label.Text = "DDS TXPK has finished extracting.";
                MessageBox.Show("TXPK DDS Files Extraction has finished", "All Done", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }

            files_listView.Enabled = true;
            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;

            if (TXPKExtract_bgw != null) { TXPKExtract_bgw.Dispose(); }
            if (extractTXPK_fbd != null) { extractTXPK_fbd.Dispose(); }
        }

        private bool ExtractTXPK()
        {
            Reader? br = null;
            FileStream? writer = null;

            try
            {
                string txpkName = Path.Combine(Global.currentPath, "temp", Path.GetFileName(filename));

                if (!File.Exists(txpkName))
                    return false;

                br = new Reader(txpkName);

                TXPK txpk = new();
                txpk.Deserialize(br);

                ExtractFileInfo? txpkInfo = null;
                PS3_DDS_Header? txpkPS3Info = null;

                if (Global.isBigendian)
                {
                    txpkPS3Info = new();
                    txpkPS3Info.Entries = new PS3_DDS_Header.Entry[txpk.FilesCount];
                }
                else
                {
                    txpkInfo = new();
                    txpkInfo.Entries = new ExtractFileInfo.Entry[txpk.FilesCount];
                }

                int MainUnCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;
                int index = 0;

                foreach (var entry in txpk.Entries)
                {
                    string ddsFilePath = entry.DDSFilePath.Replace("/", @"\") + ".dds";
                    string filePath = extractTXPK_fbd.SelectedPath + @"\" + ddsFilePath;
                    Directory.CreateDirectory(Path.GetDirectoryName(filePath));

                    br.Position = MainUnCompressedSize + entry.DDSDataOffset;

                    int chunkCount = Utilities.ChunkAmount((int)entry.DDSDataSize);
                    long[] chunkSizes = Utilities.ChunkSizes((int)entry.DDSDataSize, chunkCount);

                    writer = new FileStream(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    byte[]? ddsHeader = null;

                    if (Global.isBigendian)
                    {
                        PS3_DDS consoleDDS = new(txpk.Entries[index].DDSHeight, txpk.Entries[index].DDSWidth, txpk.Entries[index].DDSMipMaps, txpk.Entries[index].DDSType, 0);
                        ddsHeader = consoleDDS.WriteDDS();
                        writer.Write(ddsHeader, 0, ddsHeader.Length);

                        txpkPS3Info.Entries[index] = new();
                        txpkPS3Info.Entries[index].FilePath = entry.DDSFilePath.Replace(@"/", @"\") + ".dds";
                        txpkPS3Info.Entries[index].BufferSize = txpk.Entries[index].BufferSize;
                        txpkPS3Info.Entries[index].DDSType = txpk.Entries[index].DDSType;
                        txpkPS3Info.Entries[index].DDSMipMaps = txpk.Entries[index].DDSMipMaps;
                        txpkPS3Info.Entries[index].Unknown1 = txpk.Entries[index].Unknown1;
                        txpkPS3Info.Entries[index].Unknown2 = txpk.Entries[index].Unknown2;
                        txpkPS3Info.Entries[index].DDSWidth = txpk.Entries[index].DDSWidth2;
                        txpkPS3Info.Entries[index].DDSHeight = txpk.Entries[index].DDSHeight2;
                        txpkPS3Info.Entries[index].DDSImageType = txpk.Entries[index].DDSImageType2;
                        txpkPS3Info.Entries[index].Unknown3 = txpk.Entries[index].Unknown3;
                        txpkPS3Info.Entries[index].Unknown4 = txpk.Entries[index].Unknown4;
                        txpkPS3Info.Entries[index].Unknown5 = txpk.Entries[index].Unknown5;
                        txpkPS3Info.Entries[index].Reserved = txpk.Entries[index].Reserved;

                        byte[] ddsData = br.ReadBytes((int)entry.DDSDataSize, Endian.Little);

                        if (txpk.Entries[index].DDSType == 133 || txpk.Entries[index].DDSType == 154)
                            ddsData = PS3_DDS.UnswizzleMorton(ddsData, (int)txpk.Entries[index].DDSWidth, (int)txpk.Entries[index].DDSHeight, 32, 1, 1);

                        IO.ReadWriteData(ddsData, writer, (int)entry.DDSDataSize);
                    }
                    else
                    {
                        txpkInfo.Entries[index] = new();
                        txpkInfo.Entries[index].FilePath = entry.DDSFilePath.Replace(@"/", @"\") + ".dds";

                        for (int i = 0; i < chunkCount; i++)
                        {
                            byte[] tmpChunkData = br.ReadBytes((int)chunkSizes[i], Endian.Little);
                            IO.ReadWriteData(tmpChunkData, writer, (int)chunkSizes[i]);
                        }
                    }

                    int percentProgress = (index + 1) * 100 / (int)txpk.FilesCount;
                    TXPKExtract_bgw.ReportProgress(percentProgress, ddsFilePath);

                    if (writer != null) { writer.Dispose(); writer = null; }

                    index++;
                }

                if (Global.isBigendian)
                {
                    txpkPS3Info.Index = list[Global.fileIndex].BlobsetIndex;
                    IO.XmlSerialize(extractTXPK_fbd.SelectedPath + @"\TXPK_List.xml", txpkPS3Info);
                }
                else
                {
                    txpkInfo.Index = list[Global.fileIndex].BlobsetIndex;
                    IO.XmlSerialize(extractTXPK_fbd.SelectedPath + @"\TXPK_List.xml", txpkInfo);
                }

                if (br != null) { br.Close(); br = null; }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : {ex.Message}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return true;
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (writer != null) { writer.Dispose(); writer = null; }
            }
            return false;
        }

        private int Get_DDS_Index(string ddsFileName, TXPK txpk)
        {
            int index = -1;

            int i = 0;

            foreach (var item in txpk.Entries)
            {
                if (ddsFileName == Path.GetFileName(item.DDSFilePath + ".dds"))
                {
                    index = i;
                    break;
                }
                i++;
            }
            return index;
        }

        private void pictureBox1_MouseClick(object sender, MouseEventArgs e)
        {
            if (pictureBox1.Image != null)
            {
                if (e.Button == MouseButtons.Right)
                    flipImage_contextMenuStrip.Show(Cursor.Position);
            }
        }

        private void flipImageToolStripMenuItem_Click(object sender, EventArgs e)
        {
            isFlipped = !isFlipped;
            pictureBox1.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
            pictureBox1.Refresh();
        }

        private void alphaToolStripMenuItem_Click(object sender, EventArgs e)
        {
            LoadImage(alphaToolStripMenuItem.Checked);
        }

        private void files_listView_MouseClick(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
            {
                extractDDS_contextMenuStrip.Show(Cursor.Position);
            }
        }

        private void extractToPNGToolStripMenuItem_Click(object sender, EventArgs e)
        {
            string path = files_listView.SelectedItems[0].Text;

            saveFileDialog.Title = "Save PNG File";
            saveFileDialog.Filter = "PNG" + " File|*.png";
            saveFileDialog.DefaultExt = "png";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                pictureBox1.Image.Save(saveFileDialog.FileName);

                MessageBox.Show($"PNG File has been saved to - {saveFileDialog.FileName}", "PNG File Extracted :)", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void extractDDSToolStripMenuItem_Click(object sender, EventArgs e)
        {

        }
    }
}

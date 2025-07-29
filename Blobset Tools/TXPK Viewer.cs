using BlobsetIO;
using PackageIO;
using Pfim;
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
        private ImageList myImageList = null;

        private uint MainCompressedSize = 0;
        private uint MainUnCompressedSize = 0;
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
                MainCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                MainUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;
                VramCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].VramCompressedSize;
                VramUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].VramUnCompressedSize;

                Text = Text + " - " + filename;
                myImageList = new ImageList();
                myImageList.Images.Add(Properties.Resources.folder_32);
                myImageList.Images.Add(Properties.Resources.dds_32);

                folder_treeView.ImageList = myImageList;

                string[] ddsPaths = new string[txpkData.FilesCount];

                for (int i = 0; i < txpkData.FilesCount; i++)
                {
                    ddsPaths[i] = txpkData.Entries[i].DDSFilePath + ".dds";
                }

                folder_treeView.Nodes.Add(UI.MakeTreeFromPaths(ddsPaths, Path.GetFileName(filename)));

                if (folder_treeView.Nodes.Count > 0)
                {
                    folder_treeView.Nodes[0].Expand();
                    folder_treeView.SelectedNode = folder_treeView.Nodes[0];
                }

                string txpkName = Path.GetFileName(filename);

                if (File.Exists(Global.currentPath + @"\temp\" + txpkName))
                    File.Delete(Global.currentPath + @"\temp" + txpkName);

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

            status_Label.Text = list.Count + " items in " + folder_treeView.SelectedNode.Text + " folder";
        }

        private void files_listView_SelectedIndexChanged(object sender, EventArgs e)
        {
            Reader? br = null;

            try
            {
                string txpkName = Path.GetFileName(filename);
                br = new Reader(Global.currentPath + @"\temp\" + txpkName);

                fileIndex = UI.getLVSelectedIndex(files_listView);

                if (fileIndex == -1) return;

                TXPK txpk = new();
                txpk.Deserialize(br);

                if (txpk == null) return;

                int index = Get_DDS_Index(files_listView.Items[fileIndex].Text, txpk);

                if (index == -1) return;

                br.Position = br.Position + txpk.Entries[index].DDSDataOffset;
                byte[] ddsData = br.ReadBytes((int)txpk.Entries[index].DDSDataSize1);

                int mipmapCount = 1;
                ImageFormat? fmt = ImageFormat.Rgba32;
                PixelFormat? ddsFormat = PixelFormat.UnCompressed;

                Structs.DDSInfo ddsInfo = new();

                fmt = ddsInfo.IFormat;
                ddsFormat = ddsInfo.PFormat;
                mipmapCount = ddsInfo.MipMap + 1;

                Bitmap bitmap = UI.DDStoBitmap(ddsData, ref ddsInfo);

                if (bitmap != null)
                {
                    pictureBox1.Image = bitmap;
                    DDSInfo_SSLabel.Text = "Format: " + ddsFormat.ToString() + " - " + fmt.ToString() + "    Height: " + bitmap.Height.ToString() + "     Width: " + bitmap.Width.ToString() + "     MipMaps: 1/" + mipmapCount.ToString(); ;
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
        }

        private void extractTXPKToolStripMenuItem_Click(object sender, EventArgs e)
        {
            extractTXPK_fbd.SelectedPath = Global.currentPath + @"\txpk\";

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

            if (MainCompressedSize != MainUnCompressedSize && VramCompressedSize != VramUnCompressedSize)
                errorCheck = TXPX_Decompress(false);
            else
                errorCheck = TXPX_Decompress(true);

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

        private bool TXPX_Decompress(bool isVramCompressed)
        {
            Reader? br = null;
            FileStream? fsWriter = null;
            bool error = false;

            try
            {
                br = new Reader(Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + list[Global.fileIndex].FolderHash + @"\" + list[Global.fileIndex].FileHash);

                string txpkName = Path.GetFileName(filename);
                fsWriter = new FileStream(Global.currentPath + @"\temp\" + txpkName, FileMode.OpenOrCreate, FileAccess.ReadWrite);

                if (isVramCompressed)
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
                error = true;
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
            }
            return error;
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
            toolStripProgressBar.Value = e.ProgressPercentage;
            progressStripStatusLabel.Text = string.Format("{0} %", e.ProgressPercentage);
            status_Label.ForeColor = Color.Black;
            status_Label.Text = "Extracting TXPK: " + e.UserState.ToString();
            statusStrip1.Refresh();
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

            bool errorCheck = true;

            try
            {
                string txpkName = Global.currentPath + @"\temp\" + Path.GetFileName(filename);

                if (!File.Exists(txpkName))
                    return false;

                br = new Reader(txpkName);

                TXPK txpk = new();
                txpk.Deserialize(br);

                ExtractFileInfo txpkInfo = new();

                int MainUnCompressedSize = (int)Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;

                int index = 0;

                txpkInfo.Entries = new ExtractFileInfo.Entry[txpk.FilesCount];

                foreach (var entry in txpk.Entries)
                {
                    string ddsFilePath = entry.DDSFilePath.Replace("/", @"\") + ".dds";
                    string filePath = extractTXPK_fbd.SelectedPath + @"\" + ddsFilePath;
                    Directory.CreateDirectory(Path.GetDirectoryName(filePath));

                    br.Position = entry.DDSDataOffset + MainUnCompressedSize;

                    int chunkCount = Utilities.ChunkAmount((int)entry.DDSDataSize1);
                    long[] chunkSizes = Utilities.ChunkSizes((int)entry.DDSDataSize1, chunkCount);

                    writer = new FileStream(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    for (int i = 0; i < chunkCount; i++)
                    {
                        byte[] tmpChunkData = br.ReadBytes((int)chunkSizes[i]);
                        IO.ReadWriteData(tmpChunkData, writer, (int)chunkSizes[i]);
                    }

                    txpkInfo.Entries[index] = new();
                    txpkInfo.Entries[index].FilePath = entry.DDSFilePath.Replace(@"/", @"\") + ".dds";

                    int percentProgress = 100 * index / (int)txpk.FilesCount;
                    TXPKExtract_bgw.ReportProgress(percentProgress, ddsFilePath);

                    if (writer != null) { writer.Dispose(); writer = null; }

                    index++;
                }

                txpkInfo.Index = list[Global.fileIndex].BlobsetIndex;

                IO.XmlSerialize(extractTXPK_fbd.SelectedPath + @"\TXPK_List.xml", txpkInfo);

                if (br != null) { br.Close(); br = null; }
            }
            catch (Exception error)
            {
                errorCheck = true;
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (writer != null) { writer.Dispose(); writer = null; }
                errorCheck = false;
            }
            return errorCheck;
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
            pictureBox1.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
            pictureBox1.Refresh();
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
            string path = files_listView.SelectedItems[fileIndex].Text;

            saveFileDialog1.Title = "Save PNG File";
            saveFileDialog1.Filter = "PNG" + " File|*.png";
            saveFileDialog1.DefaultExt = "png";
            saveFileDialog1.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog1.ShowDialog() == DialogResult.OK)
            {
                pictureBox1.Image.Save(saveFileDialog1.FileName);

                MessageBox.Show("PNG File has been saved to - " + saveFileDialog1.FileName, "PNG File Extracted :)", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog1.Dispose();
        }

        private void extractDDSToolStripMenuItem_Click(object sender, EventArgs e)
        {

        }

        private void TXPK_Viewer_FormClosing(object sender, FormClosingEventArgs e)
        {
            string txpkName = Path.GetFileName(filename);

            if (File.Exists(Global.currentPath + @"\temp\" + txpkName))
                File.Delete(Global.currentPath + @"\temp\" + txpkName);
        }
    }
}

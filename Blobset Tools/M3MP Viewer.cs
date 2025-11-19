using BlobsetIO;
using PackageIO;
using System.ComponentModel;

namespace Blobset_Tools
{
    public partial class M3MP_Viewer : Form
    {
        private readonly string? filename;
        private readonly M3MP? m3mpData;
        private ExtractFileInfo? m3mpXmlIn = null;
        private readonly List<Structs.FileIndexInfo>? list;
        private BackgroundWorker? M3MPDecompress_bgw = null;
        private BackgroundWorker? M3MPExtract_bgw = null;
        private ImageList? myImageList = null;
        private bool isCompressed = false;
        private int blobsetVersion = 4;
        private uint MainFinalOffset = 0;
        private uint MainCompressedSize = 0;
        private uint MainUnCompressedSize = 0;

        public M3MP_Viewer(string _filename, M3MP _m3mpData, List<Structs.FileIndexInfo> _list)
        {
            InitializeComponent();
            filename = _filename;
            m3mpData = _m3mpData;
            list = _list;
        }

        private void M3MP_Viewer_Load(object sender, EventArgs e)
        {
            try
            {
                if (m3mpData != null)
                {
                    MainFinalOffset = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainFinalOffSet;
                    MainCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                    MainUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;

                    blobsetVersion = Properties.Settings.Default.BlobsetVersion;

                    Text = Text + " - " + filename;
                    myImageList = new ImageList();
                    myImageList.Images.Add(Properties.Resources.folder_32);
                    myImageList.Images.Add(Properties.Resources.file_32);

                    folder_treeView.ImageList = myImageList;

                    m3mpXmlIn = new ExtractFileInfo();
                    m3mpXmlIn.Index = Convert.ToInt32(Path.GetFileNameWithoutExtension(filename));

                    string[] filePaths = new string[m3mpData.FilesCount];

                    m3mpXmlIn.Entries = new ExtractFileInfo.Entry[filePaths.Length];

                    for (int i = 0; i < m3mpData.FilesCount; i++)
                    {
                        ExtractFileInfo.Entry entry = new();
                        entry.FilePath = m3mpData.UnCompressedEntries[i].FilePath;
                        m3mpXmlIn.Entries[i] = entry;
                        filePaths[i] = m3mpData.UnCompressedEntries[i].FilePath;
                    }

                    folder_treeView.Nodes.Add(UI.MakeTreeFromPaths(filePaths, Path.GetFileName(filename)));

                    if (folder_treeView.Nodes.Count > 0)
                    {
                        folder_treeView.Nodes[0].Expand();
                        folder_treeView.SelectedNode = folder_treeView.Nodes[0];
                    }

                    string m3mpName = Path.GetFileName(filename);

                    if (isCompressed || blobsetVersion <= 1)
                    {
                        if (File.Exists(Global.currentPath + @"\temp\" + m3mpName))
                            File.Delete(Global.currentPath + @"\temp" + m3mpName);

                        M3MPDecompressBgw();
                    }
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
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
                lvi.Tag = list[i].MappingIndex;
            }

            status_Label.Text = list.Count + " items in " + folder_treeView.SelectedNode.Text + " folder";
        }

        private void M3MPDecompressBgw()
        {
            M3MPDecompress_bgw = new BackgroundWorker();
            M3MPDecompress_bgw.DoWork += new DoWorkEventHandler(M3MPDecompress_bgw_DoWork);
            M3MPDecompress_bgw.ProgressChanged += new ProgressChangedEventHandler(M3MPDecompress_bgw_ProgressChanged);
            M3MPDecompress_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(M3MPDecompress_bgw_RunWorkerCompleted);
            M3MPDecompress_bgw.WorkerSupportsCancellation = true;
            M3MPDecompress_bgw.WorkerReportsProgress = true;
            M3MPDecompress_bgw.RunWorkerAsync();

            files_listView.Enabled = false;
            toolStripProgressBar.MarqueeAnimationSpeed = 10;
            toolStripProgressBar.Style = ProgressBarStyle.Marquee;
        }

        private void M3MPDecompress_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            bool errorCheck = blobsetVersion >= 3 ? M3MP_DecompressZSTD() : M3MP_DecompressLZMA();

            if (errorCheck)
                e.Cancel = true;
        }

        private bool M3MP_DecompressZSTD()
        {
            Reader? br = null;
            FileStream? fsWriter = null;

            try
            {
                string m3mpName = Global.currentPath + @"\temp\" + Path.GetFileName(filename);

                br = new(Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + list[Global.fileIndex].FolderHash + @"\" + list[Global.fileIndex].FileHash);

                fsWriter = new(m3mpName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                int i = 0;

                while (br.Position < br.Length)
                {
                    int m3mpCompressedSize = br.ReadInt32();
                    int m3mpTmp = m3mpCompressedSize -= 4;
                    m3mpCompressedSize = m3mpTmp;

                    bool isCompressed = true;

                    byte[] m3mpData = br.ReadBytes(m3mpCompressedSize);

                    byte[] ZstdMagicArray = [m3mpData[0], m3mpData[1], m3mpData[2], m3mpData[3]];
                    uint ZstdMagic = BitConverter.ToUInt32(ZstdMagicArray);

                    if (ZstdMagic != 4247762216)
                        isCompressed = false;

                    ZSTD_IO.DecompressAndWrite(m3mpData, fsWriter, isCompressed);

                    M3MPDecompress_bgw.ReportProgress(i, "M3MP is decompressing to temp file. Chunk: ");
                    i++;
                }
                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return true;
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
            }
            return false;
        }

        private bool M3MP_DecompressLZMA() 
        {
            Reader? br = null;
            FileStream? fsWriter = null;

            try 
            {
                br = new Reader(Properties.Settings.Default.GameLocation.Replace("-0", "-" + Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].BlobSetNumber));
                string m3mpName = Global.currentPath + @"\temp\" + Path.GetFileName(filename);
                fsWriter = new(m3mpName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                br.Position = MainFinalOffset;

                if (MainCompressedSize != MainUnCompressedSize) 
                {
                    int m3mpChunkCount = br.ReadInt32();
                    int[] m3mpChunkCompressedSize = new int[m3mpChunkCount];

                    for (int j = 0; j < m3mpChunkCount; j++)
                    {
                        m3mpChunkCompressedSize[j] = br.ReadInt32();
                        m3mpChunkCompressedSize[j] = m3mpChunkCompressedSize[j] -= 4;
                    }

                    for (int j = 0; j < m3mpChunkCount; j++)
                    {
                        int m3mpChunkUnCompressedSize = br.ReadInt32();

                        if (m3mpChunkCompressedSize[j] == m3mpChunkUnCompressedSize) 
                        {
                            byte[] m3mpTempData = br.ReadBytes(m3mpChunkUnCompressedSize, Endian.Little);
                            fsWriter.Write(m3mpTempData, 0, m3mpChunkUnCompressedSize);
                        }    
                        else
                        {
                            byte[] m3mpCompressedData = br.ReadBytes(m3mpChunkCompressedSize[j], Endian.Little);
                            byte[] m3mpData = LZMA_IO.DecompressAndRead(m3mpCompressedData, m3mpChunkCompressedSize[j]);
                            fsWriter.Write(m3mpData, 0, m3mpData.Length);
                        }
                    }
                    fsWriter.Flush();
                }
                else
                {
                    byte[] m3mpData = br.ReadBytes((int)MainUnCompressedSize);
                    fsWriter.Write(m3mpData, 0, m3mpData.Length);
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return true;
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
            }
            return false;
        }

        private void M3MPDecompress_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            status_Label.Text = e.UserState.ToString() + e.ProgressPercentage;
        }

        private void M3MPDecompress_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            toolStripProgressBar.MarqueeAnimationSpeed = 100;
            toolStripProgressBar.Style = ProgressBarStyle.Blocks;
            files_listView.Enabled = true;
            status_Label.Text = "M3MP has finished decompressing to a temp file for reading";
        }

        private void extractM3MPToolStripMenuItem_Click(object sender, EventArgs e)
        {
            extractM3MP_fbd.SelectedPath = Global.currentPath + @"\m3mp\";

            if (extractM3MP_fbd.ShowDialog() == DialogResult.OK)
                M3MPExtractBGW();
        }

        private void M3MPExtractBGW()
        {
            M3MPExtract_bgw = new BackgroundWorker();
            M3MPExtract_bgw.DoWork += new DoWorkEventHandler(M3MPExtract_bgw_DoWork);
            M3MPExtract_bgw.ProgressChanged += new ProgressChangedEventHandler(M3MPExtract_bgw_ProgressChanged);
            M3MPExtract_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(M3MPExtract_bgw_RunWorkerCompleted);
            M3MPExtract_bgw.WorkerSupportsCancellation = true;
            M3MPExtract_bgw.WorkerReportsProgress = true;
            M3MPExtract_bgw.RunWorkerAsync();

            files_listView.Enabled = false;
            status_Label.Text = "Starting the extraction now.....";
        }

        private void M3MPExtract_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            string m3mpName = Global.currentPath + @"\temp\" + Path.GetFileName(filename);

            if (isCompressed)
            {
                if (!File.Exists(m3mpName))
                    return;

                if (blobsetVersion > 1)
                    ZSTD_IO.M3MPDecompressAndWrite(m3mpName, extractM3MP_fbd.SelectedPath + @"\", M3MPExtract_bgw);
                else
                    LZMA_IO.M3MPDecompressAndWrite(m3mpName, extractM3MP_fbd.SelectedPath + @"\", M3MPExtract_bgw);
            }
            else
            {
                string filePath = Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + list[Global.fileIndex].FolderHash + @"\" + list[Global.fileIndex].FileHash;

                if (blobsetVersion > 1)
                    ZSTD_IO.M3MPDecompressAndWrite(filePath, extractM3MP_fbd.SelectedPath + @"\", M3MPExtract_bgw);
                else
                    LZMA_IO.M3MPDecompressAndWrite(m3mpName, extractM3MP_fbd.SelectedPath + @"\", M3MPExtract_bgw);
            }

            IO.XmlSerialize(extractM3MP_fbd.SelectedPath + @"\M3MP_List.xml", m3mpXmlIn);
        }

        private void M3MPExtract_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            toolStripProgressBar.Value = e.ProgressPercentage;
            progressStripStatusLabel.Text = string.Format("{0} %", e.ProgressPercentage);
            status_Label.ForeColor = Color.Black;
            statusStrip1.Refresh();
            status_Label.Text = "Extracting File: " + e.UserState.ToString();
        }

        private void M3MPExtract_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                status_Label.Text = "M3MP has finished extracting all files.....";
                MessageBox.Show("M3MP has finished extracting.", "M3MP Extraction", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }

            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;

            files_listView.Enabled = true;
            extractM3MP_fbd.Dispose();

            if (M3MPExtract_bgw != null) { M3MPExtract_bgw.Dispose(); M3MPExtract_bgw = null; }
        }

        private void M3MP_Viewer_FormClosing(object sender, FormClosingEventArgs e)
        {
            string m3mpName = Path.GetFileName(filename);

            if (File.Exists(Global.currentPath + @"\temp\" + m3mpName))
                File.Delete(Global.currentPath + @"\temp\" + m3mpName);
        }
    }
}

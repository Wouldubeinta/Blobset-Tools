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
        private int blobsetVersion = 3;
        private uint MainFinalOffset = 0;
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
                    var BlobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];
                    MainFinalOffset = BlobsetHeaderData.MainFinalOffSet;
                    MainUnCompressedSize = BlobsetHeaderData.MainUnCompressedSize;
                    blobsetVersion = Global.gameInfo.BlobsetVersion;

                    isCompressed = BlobsetHeaderData.MainCompressedSize != MainUnCompressedSize;

                    string m3mpName = Path.GetFileName(filename);

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

                    folder_treeView.Nodes.Add(UI.MakeTreeFromPaths(filePaths, m3mpName));

                    if (folder_treeView.Nodes.Count > 0)
                    {
                        folder_treeView.Nodes[0].Expand();
                        folder_treeView.SelectedNode = folder_treeView.Nodes[0];
                    }

                    string m3mpNameTmp = Path.Combine(Global.currentPath, "temp", m3mpName);

                    if (File.Exists(m3mpNameTmp))
                        File.Delete(m3mpNameTmp);

                    M3MPDecompressBgw();
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
            M3MPDecompress_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(M3MPDecompress_bgw_RunWorkerCompleted);
            M3MPDecompress_bgw.WorkerSupportsCancellation = true;
            M3MPDecompress_bgw.WorkerReportsProgress = false;
            M3MPDecompress_bgw.RunWorkerAsync();

            files_listView.Enabled = false;
            toolStripProgressBar.MarqueeAnimationSpeed = 10;
            toolStripProgressBar.Style = ProgressBarStyle.Marquee;
        }

        private void M3MPDecompress_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            bool errorCheck = blobsetVersion > 2 ? M3MP_DecompressZSTD() : M3MP_DecompressLZMA();

            if (errorCheck)
                e.Cancel = true;
        }

        private bool M3MP_DecompressZSTD()
        {
            Reader? br = null;
            FileStream? writer = null;

            try
            {
                string m3mpName = Path.Combine(Global.currentPath, "temp", Path.GetFileName(filename));

                br = new(Path.Combine(Global.gameInfo.GameLocation.Replace("data-0.blobset.pc", string.Empty), list[Global.fileIndex].FolderHash, list[Global.fileIndex].FileHash));

                writer = new(m3mpName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                if (isCompressed)
                    ZSTD_IO.DecompressChunk(br, writer);
                else
                {
                    if (writer != null) { writer.Dispose(); writer = null; }
                    IO.ReadWriteData(br, m3mpName);
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
                if (writer != null) { writer.Dispose(); writer = null; }
            }
            return false;
        }

        private bool M3MP_DecompressLZMA()
        {
            Reader? br = null;
            FileStream? writer = null;

            try
            {
                Endian endian = Endian.Little;

                if (Global.isBigendian)
                    endian = Endian.Big;

                br = new Reader(Global.gameInfo.GameLocation.Replace("-0", "-" + Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].BlobSetNumber), endian);
                string m3mpName = Path.Combine(Global.currentPath, "temp", Path.GetFileName(filename));
                writer = new(m3mpName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                br.Position = MainFinalOffset;

                if (isCompressed)
                    LZMA_IO.DecompressChunkAndWrite(br, writer);
                else
                {
                    if (writer != null) { writer.Dispose(); writer = null; }
                    IO.ReadWriteCunkData(br, m3mpName, (int)MainUnCompressedSize);
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
                if (writer != null) { writer.Dispose(); writer = null; }
            }
            return false;
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
            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            string platformExt = platformDetails["PlatformExt"];

            extractM3MP_fbd.SelectedPath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "m3mp");

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
            string m3mpName = Path.Combine(Global.currentPath, "temp", Path.GetFileName(filename));

            if (blobsetVersion > 2)
                ZSTD_IO.M3MPDecompressAndWrite(m3mpName, extractM3MP_fbd.SelectedPath + @"\", M3MPExtract_bgw);
            else
                LZMA_IO.M3MPDecompressAndWrite(m3mpName, extractM3MP_fbd.SelectedPath + @"\", M3MPExtract_bgw);

            IO.XmlSerialize(extractM3MP_fbd.SelectedPath + @"\M3MP_List.xml", m3mpXmlIn);
        }

        private void M3MPExtract_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            int progressPercentage = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            progressStripStatusLabel.Text = $"{progressPercentage} %";
            status_Label.ForeColor = Color.Black;
            status_Label.Text = $"Extracting File: {e.UserState}";
            toolStripProgressBar.Value = progressPercentage;
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
            try
            {
                string m3mpName = Path.Combine(Global.currentPath, "temp", Path.GetFileName(filename));

                if (File.Exists(m3mpName))
                    File.Delete(m3mpName);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }
    }
}

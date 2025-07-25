using BlobsetIO;
using System.ComponentModel;
using System.Reflection;
using WEMSharp;

namespace Blobset_Tools
{
    public partial class MainForm : Form
    {
        private BackgroundWorker? Extract_bgw = null;
        private BackgroundWorker? Modify_bgw = null;
        private BackgroundWorker? FileMapping_bgw = null;

        public MainForm()
        {
            InitializeComponent();

            Global.version = Assembly.GetEntryAssembly().GetName().Version.ToString();
            Text = "Blobset Tools - v" + Global.version;
            Global.currentPath = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            string gameName = "* Reading and Writing " + Properties.Settings.Default.GameName + " Blobset file *";
            gameName_toolStripTextBox.Text = gameName;

            dds_pictureBox.SizeMode = PictureBoxSizeMode.Zoom;
            dds_pictureBox.Image = Properties.Resources.Blobset_Tools;

            UI.BlobsetHeaderData();
        }

        private void MainForm_Load(object sender, EventArgs e)
        {
            foreach (string line in UI.LoadingText)
                fileInfo_richTextBox.AppendText(line);

            string gameVersion = string.Empty;
            int gameID = Properties.Settings.Default.GameID;

            switch (gameID)
            {
                case (int)Enums.Game.AFLL:
                case (int)Enums.Game.RLL2:
                    break;
                case (int)Enums.Game.DBC14:
                case (int)Enums.Game.RLL3:
                case (int)Enums.Game.AC:
                    string gv1 = Properties.Settings.Default.GameLocation.Replace(@"data-0.blobset.pc", string.Empty) + "version.txt";
                    if (File.Exists(gv1))
                        gameVersion = File.ReadAllText(gv1);
                    break;
                default:
                    string gv2 = Properties.Settings.Default.GameLocation.Replace(@"data\data-0.blobset.pc", string.Empty) + "version.txt";
                    if (File.Exists(gv2))
                        gameVersion = File.ReadAllText(gv2);
                    break;
            }

            string fileMappingVersion = File.ReadAllText(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt");

            if (fileMappingVersion != gameVersion || gameVersion == "0")
            {
                if (gameID == (int)Enums.Game.AFLL || gameID == (int)Enums.Game.RLL2)
                {
                    updateFileMappingDataToolStripMenuItem_Click(sender, new EventArgs());
                    File.Delete(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt");
                    File.WriteAllText(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt", "1");
                }
                else
                {
                    updateFileMappingDataToolStripMenuItem_Click(sender, new EventArgs());
                    File.Delete(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt");
                    File.WriteAllText(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt", gameVersion);
                }
            }
            else
            {
                folder_treeView.Nodes.Clear();
                UI.FilesList(folder_treeView);
            }

            blobsetCompressionToolStripMenuItem.Checked = Properties.Settings.Default.Compression;
            loadGameToolStripMenuItem.Checked = Properties.Settings.Default.LoadGame;
            skipUnknownFilesToolStripMenuItem.Checked = Properties.Settings.Default.SkipUnknown;
        }

        private void folder_treeView_AfterSelect(object sender, TreeViewEventArgs e)
        {
            files_listView.Refresh();
            files_listView.Clear();
            status_Label.Text = string.Empty;

            if (folder_treeView.SelectedNode == null)
                return;

            ImageList myImageList = new();
            myImageList.Images.Add(Properties.Resources.dds_32);
            myImageList.Images.Add(Properties.Resources.txpk_32);
            myImageList.Images.Add(Properties.Resources.m3mp_32);
            myImageList.Images.Add(Properties.Resources.wem_32);
            myImageList.Images.Add(Properties.Resources.bnk_32);
            myImageList.Images.Add(Properties.Resources.dat_32);

            files_listView.SmallImageList = myImageList;

            Global.filelist = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            if (Global.filelist == null)
                return;

            ListViewItem[] lvi = new ListViewItem[Global.filelist.Count];

            for (int i = 0; i < Global.filelist.Count; i++)
            {
                int icon = 0;
                string ext = Path.GetExtension(Global.filelist[i].FileName);

                if (ext == ".txpk")
                    icon = 1;
                if (ext == ".m3mp")
                    icon = 2;
                else if (ext == ".wem")
                    icon = 3;
                else if (ext == ".bnk")
                    icon = 4;
                else if (ext == ".dat")
                    icon = 5;

                lvi[i] = new ListViewItem { ImageIndex = icon, Text = Global.filelist[i].FileName };
            }

            AddItems(lvi);
            status_Label.Text = Global.filelist.Count + " items in " + folder_treeView.SelectedNode.Text + " folder";
        }

        private void AddItems(ListViewItem[] lvi)
        {
            if (InvokeRequired)
            {
                Invoke((System.Windows.Forms.MethodInvoker)delegate { AddItems(lvi); });
                return;
            }
            files_listView.Items.AddRange(lvi);
        }

        private void files_listView_MouseClick(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
            {
                if (Global.filelist == null)
                    return;

                if (Global.fileIndex == -1)
                    return;

                string ext = Path.GetExtension(Global.filelist[Global.fileIndex].FilePath);

                if (ext == ".dds")
                    extractImage_contextMenuStrip.Show(Cursor.Position);
                else if (ext == ".wem")
                    extractFile_contextMenuStrip.Show(Cursor.Position);
                else if (ext == ".bnk")
                    extractFile_contextMenuStrip.Show(Cursor.Position);
                else if (ext == ".dat")
                    extractFile_contextMenuStrip.Show(Cursor.Position);
            }
        }

        private void files_listView_SelectedIndexChanged(object sender, EventArgs e)
        {
            try
            {
                dds_pictureBox.Image = Properties.Resources.Blobset_Tools;

                Global.fileIndex = UI.getLVSelectedIndex(files_listView);
                fileInfo_richTextBox.Clear();

                if (Global.filelist == null)
                    return;

                if (Global.fileIndex == -1)
                    return;

                uint MainCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                uint MainUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;
                uint VramCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].VramCompressedSize;
                uint VramUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].VramUnCompressedSize;

                string filePath = Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + Global.filelist[Global.fileIndex].FolderHash + @"\" + Global.filelist[Global.fileIndex].FileHash;

                string type = Path.GetExtension(Global.filelist[Global.fileIndex].FilePath);

                if (type == ".dds")
                {
                    Structs.DDSInfo ddsInfo = new();
                    byte[] ddsData = UI.GetDDSData(Global.filelist);
                    Bitmap bitmap = UI.DDStoBitmap(ddsData, ref ddsInfo);

                    fileInfo_richTextBox.AppendText("*** DDS Location ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    fileInfo_richTextBox.AppendText("*** Blobset Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    if (bitmap == null)
                        return;

                    dds_pictureBox.Image = bitmap;

                    fileInfo_richTextBox.AppendText("*** DDS Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("Format: " + ddsInfo.PFormat.ToString() + " " + ddsInfo.IFormat.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("Height: " + ddsInfo.Height.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("Width: " + ddsInfo.Width.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MipMaps: 1 / " + ddsInfo.MipMap.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("Size: " + Utilities.FormatSize((ulong)ddsInfo.Size).ToString());
                }
                else if (type == ".txpk")
                {
                    fileInfo_richTextBox.AppendText("*** TXPK Location ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    fileInfo_richTextBox.AppendText("*** Blobset Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    uint txpkSize = MainUnCompressedSize + VramUnCompressedSize;

                    TXPK txpk = ZSTD_IO.ReadTXPKInfo(filePath);

                    if (txpk == null)
                        return;

                    fileInfo_richTextBox.AppendText("*** TXPK Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("DDS File Count: " + txpk.FilesCount.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("TXPK File Size: " + Utilities.FormatSize(txpkSize));
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("*** TXPK DDS File List ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    int i = 0;

                    foreach (var entry in txpk.Entries)
                    {
                        fileInfo_richTextBox.AppendText(i.ToString() + " - " + entry.DDSFilePath.Replace("/", @"\") + ".dds");
                        fileInfo_richTextBox.AppendText(Environment.NewLine);
                        i++;
                    }
                }
                else if (type == ".m3mp")
                {
                    fileInfo_richTextBox.AppendText("*** M3MP Location ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    fileInfo_richTextBox.AppendText("*** Blobset Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    bool isCompressed = false;

                    if (MainCompressedSize != MainUnCompressedSize)
                        isCompressed = true;

                    M3MP m3mp = ZSTD_IO.ReadM3MPInfo(filePath, isCompressed);

                    if (m3mp == null)
                        return;

                    fileInfo_richTextBox.AppendText("*** M3MP Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("M3MP File Count: " + m3mp.FilesCount.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("M3MP File Size: " + Utilities.FormatSize(MainUnCompressedSize));
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("*** M3MP File List ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    int i = 0;

                    foreach (var entry in m3mp.UnCompressedEntries)
                    {
                        fileInfo_richTextBox.AppendText(i.ToString() + " - " + entry.FilePath.Replace("/", @"\"));
                        fileInfo_richTextBox.AppendText(Environment.NewLine);
                        i++;
                    }
                }
                else if (type == ".wem")
                {
                    fileInfo_richTextBox.AppendText("*** Wise Audio WEM Location ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    fileInfo_richTextBox.AppendText("*** Blobset Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    WEMFile wem = new(filePath, WEMForcePacketFormat.NoForcePacketFormat);
                    //wem.GenerateOGG(@"D:\test.ogg", @"D:\packed_codebooks_aoTuV_603.bin", false, false);
                    //VorbisFile worbisFile = new(@"D:\test.ogg");
                    //IO.ReadWritePCMData(@"D:\test.ogg", @"D:\test.pcm");
                    //int b = 0;

                    fileInfo_richTextBox.AppendText("*** Wise Audio WEM Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("WEM Channel Count: " + wem.Channels);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("WEM Sample Rate: " + wem.SampleRate + " Hz");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("WEM Average Bytes Per Second: " + Utilities.FormatSize(wem.AverageBytesPerSecond));
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("WEM File Size: " + Utilities.FormatSize(MainUnCompressedSize));
                }
                else if (type == ".bnk")
                {
                    fileInfo_richTextBox.AppendText("*** Wise Audio BNK Location ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    fileInfo_richTextBox.AppendText("*** Blobset Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    fileInfo_richTextBox.AppendText("*** Wise Audio BNK Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("BNK File Size: " + Utilities.FormatSize(MainUnCompressedSize));
                }
                else if (type == ".dat")
                {
                    fileInfo_richTextBox.AppendText("*** Unknown File DAT Location ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    fileInfo_richTextBox.AppendText("*** Blobset Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString());
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText(Environment.NewLine);

                    fileInfo_richTextBox.AppendText("*** Unknown File DAT Info ***");
                    fileInfo_richTextBox.AppendText(Environment.NewLine);
                    fileInfo_richTextBox.AppendText("DAT File Size: " + Utilities.FormatSize(MainUnCompressedSize));
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }

        private void files_listView_MouseDoubleClick(object sender, MouseEventArgs e)
        {
            if (Global.fileIndex != -1)
            {
                if (Global.filelist == null)
                    return;

                string filePath = Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + Global.filelist[Global.fileIndex].FolderHash + @"\" + Global.filelist[Global.fileIndex].FileHash;
                string ext = Path.GetExtension(Global.filelist[Global.fileIndex].FilePath);

                if (ext == ".txpk")
                {
                    TXPK txpk = ZSTD_IO.ReadTXPKInfo(filePath);

                    TXPK_Viewer form = new(Global.filelist[Global.fileIndex].FilePath, txpk, Global.filelist);
                    bool IsOpen = false;

                    foreach (Form f in Application.OpenForms)
                    {
                        if (f.Text == "TXPK Viewer - " + Global.filelist[Global.fileIndex].FilePath)
                        {
                            IsOpen = true;
                            f.Focus();
                            break;
                        }
                    }

                    if (!IsOpen)
                        form.Show();
                }
                if (ext == ".m3mp")
                {
                    uint MainCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                    uint MainUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;

                    bool isCompressed = false;

                    if (MainCompressedSize != MainUnCompressedSize)
                        isCompressed = true;

                    M3MP? m3mp = ZSTD_IO.ReadM3MPInfo(filePath, isCompressed);

                    M3MP_Viewer form = new(Global.filelist[Global.fileIndex].FilePath, m3mp, Global.filelist);
                    bool IsOpen = false;

                    foreach (Form f in Application.OpenForms)
                    {
                        if (f.Text == "M3MP Viewer - " + Global.filelist[Global.fileIndex].FilePath)
                        {
                            IsOpen = true;
                            f.Focus();
                            break;
                        }
                    }

                    if (!IsOpen)
                        form.Show();
                }
                else if (ext == ".dds")
                {
                    DDS_Viewer form = new(Global.filelist[Global.fileIndex].FilePath, Global.filelist);
                    bool IsOpen = false;

                    foreach (Form f in Application.OpenForms)
                    {
                        if (f.Text == "DDS Viewer - " + Global.filelist[Global.fileIndex].FilePath)
                        {
                            IsOpen = true;
                            f.Focus();
                            break;
                        }
                    }

                    if (!IsOpen)
                        form.Show();
                }
                else if (ext == ".dat")
                {
                    Hex_Viewer form = new(Global.filelist[Global.fileIndex].FilePath, Global.filelist);
                    bool IsOpen = false;

                    foreach (Form f in Application.OpenForms)
                    {
                        if (f.Text == "Hex Viewer - " + Global.filelist[Global.fileIndex].FilePath)
                        {
                            IsOpen = true;
                            f.Focus();
                            break;
                        }
                    }

                    if (!IsOpen)
                        form.Show();
                }
            }
        }

        private void extractBlobsetToolStripMenuItem_Click(object sender, EventArgs e)
        {
            fileInfo_richTextBox.Clear();
            fileInfo_richTextBox.AppendText("Extracting " + Properties.Settings.Default.GameName + " Blobset, please wait..........");
            fileInfo_richTextBox.AppendText(Environment.NewLine);
            fileInfo_richTextBox.AppendText("This may take a while, just depends on the size of the game.");
            ExtractMain();
        }

        private void ExtractMain()
        {
            Extract_bgw = new BackgroundWorker();
            Extract_bgw.DoWork += new DoWorkEventHandler(Extract_bgw_DoWork);
            Extract_bgw.ProgressChanged += new ProgressChangedEventHandler(Extract_bgw_ProgressChanged);
            Extract_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(Extract_bgw_RunWorkerCompleted);
            Extract_bgw.WorkerReportsProgress = true;
            Extract_bgw.WorkerSupportsCancellation = true;
            Extract_bgw.RunWorkerAsync();

            createBlobsetToolStripMenuItem.Enabled = false;
            extractBlobsetToolStripMenuItem.Enabled = false;
            validateSteamGameFilesToolStripMenuItem.Enabled = false;
            updateFileMappingDataToolStripMenuItem.Enabled = false;
            files_listView.Enabled = false;
        }

        private void Extract_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            string blobsetFile = Properties.Settings.Default.GameLocation;
            bool errorCheck = Extract.BlobsetV4(blobsetFile, Extract_bgw);

            if (errorCheck)
                e.Cancel = true;
        }

        private void Extract_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            toolStripProgressBar.Value = e.ProgressPercentage;
            progressStripStatusLabel.Text = string.Format("{0} %", e.ProgressPercentage);
            status_Label.ForeColor = Color.Black;
            statusStrip1.Refresh();
            status_Label.Text = "Extracting File: " + e.UserState.ToString();
        }

        private void Extract_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                fileInfo_richTextBox.Clear();
                status_Label.Text = "Blobset file has finished extracting....";
                fileInfo_richTextBox.AppendText("Blobset file has finished extracting....");
                MessageBox.Show(Properties.Settings.Default.GameName + " Blobset file has finished extracting", "Blobset Extraction", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            else
                fileInfo_richTextBox.Clear();

            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;

            createBlobsetToolStripMenuItem.Enabled = true;
            extractBlobsetToolStripMenuItem.Enabled = true;
            validateSteamGameFilesToolStripMenuItem.Enabled = true;
            updateFileMappingDataToolStripMenuItem.Enabled = true;
            files_listView.Enabled = true;
            status_Label.ForeColor = Color.Black;

            if (Extract_bgw != null) { Extract_bgw.Dispose(); Extract_bgw = null; }
        }

        private void updateFileMappingDataToolStripMenuItem_Click(object sender, EventArgs e)
        {
            fileInfo_richTextBox.Clear();
            fileInfo_richTextBox.AppendText("Mapping files, please wait..........");
            FileMappingMain();
        }

        private void FileMappingMain()
        {
            FileMapping_bgw = new BackgroundWorker();
            FileMapping_bgw.DoWork += new DoWorkEventHandler(FileMapping_bgw_DoWork);
            FileMapping_bgw.ProgressChanged += new ProgressChangedEventHandler(FileMapping_bgw_ProgressChanged);
            FileMapping_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(FileMapping_bgw_RunWorkerCompleted);
            FileMapping_bgw.WorkerReportsProgress = true;
            FileMapping_bgw.WorkerSupportsCancellation = true;
            FileMapping_bgw.RunWorkerAsync();

            createBlobsetToolStripMenuItem.Enabled = false;
            extractBlobsetToolStripMenuItem.Enabled = false;
            validateSteamGameFilesToolStripMenuItem.Enabled = false;
            updateFileMappingDataToolStripMenuItem.Enabled = false;
            files_listView.Enabled = false;
        }

        private void FileMapping_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            string blobsetFile = Properties.Settings.Default.GameLocation;
            bool errorCheck = FileMapping.WriteV4(blobsetFile, FileMapping_bgw);

            if (errorCheck)
                e.Cancel = true;
        }

        private void FileMapping_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            toolStripProgressBar.Value = e.ProgressPercentage;
            progressStripStatusLabel.Text = string.Format("{0} %", e.ProgressPercentage);
            status_Label.ForeColor = Color.Black;
            status_Label.Text = "Creating New File Mapping: " + e.UserState.ToString();
            statusStrip1.Refresh();
        }

        private void FileMapping_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                fileInfo_richTextBox.Clear();
                status_Label.Text = "Creating New File Mapping has finished....";
                fileInfo_richTextBox.AppendText("Creating New File Mapping has finished....");
                MessageBox.Show("Creating New File Mapping has finished", "Creating New File Mapping", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            else
                fileInfo_richTextBox.Clear();

            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;

            createBlobsetToolStripMenuItem.Enabled = true;
            extractBlobsetToolStripMenuItem.Enabled = true;
            validateSteamGameFilesToolStripMenuItem.Enabled = true;
            updateFileMappingDataToolStripMenuItem.Enabled = true;
            files_listView.Enabled = true;
            status_Label.ForeColor = Color.Black;

            if (FileMapping_bgw != null)
            {
                FileMapping_bgw.Dispose();
                FileMapping_bgw = null;
            }

            folder_treeView.Nodes.Clear();
            UI.FilesList(folder_treeView);
        }

        private void createBlobsetToolStripMenuItem_Click(object sender, EventArgs e)
        {
            fileInfo_richTextBox.Clear();
            fileInfo_richTextBox.AppendText("Modifing Blobset with mods, please wait..........");
            ModifyMain();
        }

        private void ModifyMain()
        {
            Modify_bgw = new BackgroundWorker();
            Modify_bgw.DoWork += new DoWorkEventHandler(Modify_bgw_DoWork);
            Modify_bgw.ProgressChanged += new ProgressChangedEventHandler(Modify_bgw_ProgressChanged);
            Modify_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(Modify_bgw_RunWorkerCompleted);
            Modify_bgw.WorkerReportsProgress = true;
            Modify_bgw.WorkerSupportsCancellation = true;
            Modify_bgw.RunWorkerAsync();

            createBlobsetToolStripMenuItem.Enabled = false;
            extractBlobsetToolStripMenuItem.Enabled = false;
            validateSteamGameFilesToolStripMenuItem.Enabled = false;
            updateFileMappingDataToolStripMenuItem.Enabled = false;
            files_listView.Enabled = false;
        }

        private void Modify_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            string blobsetFile = Properties.Settings.Default.GameLocation;
            bool errorCheck = Modify.BlobsetV4(blobsetFile, Modify_bgw);

            if (errorCheck)
                e.Cancel = true;
        }

        private void Modify_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            toolStripProgressBar.Value = e.ProgressPercentage;
            progressStripStatusLabel.Text = string.Format("{0} %", e.ProgressPercentage);
            status_Label.ForeColor = Color.Black;
            status_Label.Text = "Modifing Blobset: " + e.UserState.ToString();
            statusStrip1.Refresh();
        }

        private void Modify_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                fileInfo_richTextBox.Clear();
                status_Label.Text = "Mods have been added to the blobset....";
                fileInfo_richTextBox.AppendText("Mods have been added to the blobset....");
                MessageBox.Show("Mods have been added to the blobset....", "Blobset Modify", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            else
                fileInfo_richTextBox.Clear();

            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;

            createBlobsetToolStripMenuItem.Enabled = true;
            extractBlobsetToolStripMenuItem.Enabled = true;
            validateSteamGameFilesToolStripMenuItem.Enabled = true;
            updateFileMappingDataToolStripMenuItem.Enabled = true;
            files_listView.Enabled = true;
            status_Label.ForeColor = Color.Black;

            if (Modify_bgw != null) { Modify_bgw.Dispose(); Modify_bgw = null; }
        }

        private void blobsetCompressionToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (blobsetCompressionToolStripMenuItem.Checked)
            {
                Properties.Settings.Default.Compression = true;
                Properties.Settings.Default.Save();
            }
            else
            {
                Properties.Settings.Default.Compression = false;
                Properties.Settings.Default.Save();
            }
        }

        private void loadGameToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (loadGameToolStripMenuItem.Checked)
            {
                Properties.Settings.Default.LoadGame = true;
                Properties.Settings.Default.Save();
            }
            else
            {
                Properties.Settings.Default.LoadGame = false;
                Properties.Settings.Default.Save();
            }
        }

        private void validateSteamGameFilesToolStripMenuItem_Click(object sender, EventArgs e)
        {
            status_Label.Text = "Validating " + Properties.Settings.Default.GameName + " files";
            UI.ValidateSteamGame();
        }

        private void skipUnknownFilesToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (skipUnknownFilesToolStripMenuItem.Checked)
            {
                Properties.Settings.Default.SkipUnknown = true;
                Properties.Settings.Default.Save();
            }
            else
            {
                Properties.Settings.Default.SkipUnknown = false;
                Properties.Settings.Default.Save();
            }
        }

        private void txpkCreatorToolStripMenuItem_Click(object sender, EventArgs e)
        {
            TXPK_Creator form = new();
            bool IsOpen = false;

            foreach (Form f in Application.OpenForms)
            {
                if (f.Text == "TXPK Creator")
                {
                    IsOpen = true;
                    f.Focus();
                    break;
                }
            }

            if (!IsOpen)
                form.Show();
        }

        private void m3mpCreatorToolStripMenuItem_Click(object sender, EventArgs e)
        {
            M3MP_Creator form = new();
            bool IsOpen = false;

            foreach (Form f in Application.OpenForms)
            {
                if (f.Text == "M3MP Creator")
                {
                    IsOpen = true;
                    f.Focus();
                    break;
                }
            }

            if (!IsOpen)
                form.Show();
        }

        private void ddsFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;
            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save DDS File";
            saveFileDialog.Filter = "DDS" + " File|*.dds";
            saveFileDialog.DefaultExt = "dds";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                File.WriteAllBytes(saveFileDialog.FileName, UI.GetDDSData(list));

                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText("DDS File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show("DDS File has been saved to - " + saveFileDialog.FileName, "Save DDS File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void extractToolStripMenuItem_Click(object sender, EventArgs e)
        {
            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            string path = list[Global.fileIndex].FilePath;
            string ext = Path.GetExtension(path).Replace(".", "");

            saveFileDialog.Title = "Save " + ext.ToUpper() + " File";
            saveFileDialog.Filter = ext.ToUpper() + " File|*" + "." + ext;
            saveFileDialog.DefaultExt = ext;
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                ExtractData(list, ext, path);

                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText(ext.ToUpper() + " File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show(ext.ToUpper() + " File has been saved to - " + saveFileDialog.FileName, "Save " + ext.ToUpper() + " File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void ExtractData(List<Structs.FileIndexInfo> list, string ext, string path)
        {
            string blobsetFilePath = Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + list[Global.fileIndex].FolderHash + @"\" + list[Global.fileIndex].FileHash;

            if (ext == "dat")
            {
                if (Path.GetDirectoryName(path) == @"unknown\uncompressed_no_header")
                {
                    IO.ReadWriteData(blobsetFilePath, saveFileDialog.FileName);
                }
            }
            else
                IO.ReadWriteData(blobsetFilePath, saveFileDialog.FileName);
        }

        private void dds_pictureBox_MouseClick(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
                flipImage_contextMenuStrip.Show(Cursor.Position);
        }

        private void flipImageToolStripMenuItem_Click(object sender, EventArgs e)
        {
            dds_pictureBox.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
            dds_pictureBox.Refresh();
        }

        private void fileInfo_richTextBox_SizeChanged(object sender, EventArgs e)
        {
            gameName_toolStripTextBox.Size = new Size(fileInfo_richTextBox.Size.Width, 21);
        }

        private void fileInfo_richTextBox_MouseClick(object sender, MouseEventArgs e)
        {
            SaveLog_contextMenuStrip.Show(Cursor.Position);
        }

        private void saveLogTotxtToolStripMenuItem_Click(object sender, EventArgs e)
        {
            File.WriteAllText(Global.currentPath + @"\FileLog.txt", fileInfo_richTextBox.Text);
            fileInfo_richTextBox.Clear();
            fileInfo_richTextBox.AppendText("File log saved to - " + Global.currentPath + @"\FileLog.txt");
            status_Label.Text = "File log saved to - " + Global.currentPath + @"\FileLog.txt";
        }

        private void Search()
        {
            bool valueResult = true;

            try
            {
                int filesCount = files_listView.Items.Count;
                int fileIndex = UI.getLVSelectedIndex(files_listView);
                int index = fileIndex;

                if (fileIndex == -1)
                    index = 0;

                if (index == filesCount)
                    index = 0;

                for (int i = index; i < filesCount; i++)
                {
                    if (files_listView.Items[i].Text.Contains(searchToolStripTextBox.Text))
                    {
                        files_listView.Focus();
                        files_listView.Items[i].Focused = true;
                        files_listView.Items[i].Selected = true;
                        files_listView.EnsureVisible(i);
                        files_listView.Refresh();
                        valueResult = false;
                        break;
                    }
                }
                if (valueResult)
                {
                    files_listView.Focus();
                    files_listView.Items[0].Focused = true;
                    files_listView.Items[0].Selected = true;
                    files_listView.Refresh();
                    MessageBox.Show("Could not find the item", "No Results Found", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return;
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }

        private void searchToolStripTextBox_KeyDown(object sender, KeyEventArgs e)
        {
            if (files_listView.Items.Count > 0)
            {
                if (e.KeyValue == (char)Keys.Enter)
                    Search();
            }
        }

        private void searchToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (files_listView.Items.Count > 0)
                Search();
        }

        private void MainForm_FormClosing(object sender, FormClosingEventArgs e)
        {
            Environment.Exit(Environment.ExitCode);
        }

        private void aboutToolStripMenuItem_Click(object sender, EventArgs e)
        {
            About about = new();
            about.ShowDialog();
        }

        private void exitToolStripMenuItem_Click(object sender, EventArgs e)
        {
            Application.Exit();
        }
    }
}

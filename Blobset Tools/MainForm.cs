using BlobsetIO;
using System.ComponentModel;
using System.Reflection;
using WEMSharp;
using static Blobset_Tools.Enums;

namespace Blobset_Tools
{
    public partial class MainForm : Form
    {
        private BackgroundWorker? Extract_bgw = null;
        private BackgroundWorker? Modify_bgw = null;
        private BackgroundWorker? FileMapping_bgw = null;
        private System.Media.SoundPlayer? player = null;
        private byte[] oggData = null;
        private byte[] wavData = null;
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
            string gameVersion = Utilities.GetGameVersion();
            int gameID = Properties.Settings.Default.GameID;
            BlobsetVersion blobsetVersion = (BlobsetVersion)Properties.Settings.Default.BlobsetVersion;
            string fileMappingVersion = File.ReadAllText(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt");

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** " + "Blobset Tools - v" + Global.version + " ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText("Description: Extract and Modify BigAnt games blobset files." + Environment.NewLine + Environment.NewLine);

            if (gameVersion == fileMappingVersion)
                fileInfo_richTextBox.SelectionColor = Color.Green;
            else
                fileInfo_richTextBox.SelectionColor = Color.Red;

            fileInfo_richTextBox.AppendText("Game Version: " + gameVersion + Environment.NewLine);

            if (gameVersion == fileMappingVersion)
                fileInfo_richTextBox.SelectionColor = Color.Green;
            else
                fileInfo_richTextBox.SelectionColor = Color.Red;

            fileInfo_richTextBox.AppendText("File Mapping Version: " + fileMappingVersion + Environment.NewLine + Environment.NewLine);

            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;

            foreach (string line in UI.LoadingText)
                fileInfo_richTextBox.AppendText(line);

            folder_treeView.Nodes.Clear();
            UI.FilesList(folder_treeView);

            if (Global.blobsetHeaderData == null)
            {
                MessageBox.Show("There was a problem reading the blobset file, restart Blobset Tools.", "Blobset Reading Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                folder_treeView.Nodes.Clear();
                return;
            }

            loadGameToolStripMenuItem.Checked = Properties.Settings.Default.LoadGame;
            skipUnknownFilesToolStripMenuItem.Checked = Properties.Settings.Default.SkipUnknown;

            if (blobsetVersion == BlobsetVersion.v1)
            {
                loadGameToolStripMenuItem.Visible = false;
                validateSteamGameFilesToolStripMenuItem.Visible = false;
                restoreBackupFilesToolStripMenuItem.Visible = false;
            }
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
                    extractSoundFile_contextMenuStrip.Show(Cursor.Position);
                else if (ext == ".dat")
                    extractDatFilecontextMenuStrip.Show(Cursor.Position);
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

                uint MainFinalOffSet = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainFinalOffSet;
                uint MainCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                uint MainUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;
                uint VramFinalOffSet = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].VramFinalOffSet;
                uint VramCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].VramCompressedSize;
                uint VramUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].VramUnCompressedSize;
                uint blobsetNumber = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].BlobSetNumber;

                string filePath = Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + Global.filelist[Global.fileIndex].FolderHash + @"\" + Global.filelist[Global.fileIndex].FileHash;
                int blobsetVersion = Properties.Settings.Default.BlobsetVersion;

                if (Properties.Settings.Default.BlobsetVersion != (int)BlobsetVersion.v4)
                    filePath = Properties.Settings.Default.GameLocation;

                if (!File.Exists(filePath))
                {
                    MessageBox.Show("Can't find the file - " + Global.filelist[Global.fileIndex].FilePath + ". You might need to run the File Mapping Data in the Options.", "File Not Found !!!", MessageBoxButtons.OK, MessageBoxIcon.Exclamation);
                    return;
                }

                string type = Path.GetExtension(Global.filelist[Global.fileIndex].FilePath);

                if (type == ".dds")
                {
                    Structs.DDSInfo ddsInfo = new();
                    byte[] ddsData = blobsetVersion >= 2 ? UI.GetDDSData_V3_V4(Global.filelist) : UI.GetDDSData_V1_V2(Global.filelist);

                    if (ddsData == null)
                        return;

                    Bitmap bitmap = UI.DDStoBitmap(ddsData, ref ddsInfo);

                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** DDS Location ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);

                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex + Environment.NewLine);

                    if (Properties.Settings.Default.BlobsetVersion == (int)BlobsetVersion.v4)
                        fileInfo_richTextBox.AppendText("FileName: " + filePath + Environment.NewLine);
                    else
                    {
                        fileInfo_richTextBox.AppendText("FolderHash: " + Global.filelist[Global.fileIndex].FolderHash + Environment.NewLine);
                        fileInfo_richTextBox.AppendText("FileHash: " + Global.filelist[Global.fileIndex].FileHash + Environment.NewLine);
                        fileInfo_richTextBox.AppendText("Blobset Number: " + "data-" + blobsetNumber + ".blobset.pc" + Environment.NewLine);
                    }

                    if (blobsetVersion != (int)BlobsetVersion.v4)
                        fileInfo_richTextBox.AppendText("MainFinalOffset: " + MainFinalOffSet.ToString() + Environment.NewLine);

                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString() + Environment.NewLine);

                    if (blobsetVersion != (int)BlobsetVersion.v4)
                        fileInfo_richTextBox.AppendText("VramFinalOffset: " + VramFinalOffSet.ToString() + Environment.NewLine);

                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString() + Environment.NewLine + Environment.NewLine);

                    if (bitmap == null)
                        return;

                    dds_pictureBox.Image = bitmap;

                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** DDS Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    string ddsFormat = ddsInfo.isDX10 ? ddsInfo.dxgiFormat.ToString() + " - DX11+" : ddsInfo.CompressionAlgorithm.ToString();
                    fileInfo_richTextBox.AppendText("Format: " + ddsFormat + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("Height: " + ddsInfo.Height.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("Width: " + ddsInfo.Width.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MipMaps: 1 / " + ddsInfo.MipMap.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("Size: " + Utilities.FormatSize((ulong)ddsInfo.Size).ToString());
                }
                else if (type == ".txpk")
                {
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** TXPK Location ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);

                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString() + Environment.NewLine + Environment.NewLine);

                    uint txpkSize = MainUnCompressedSize + VramUnCompressedSize;

                    TXPK txpk = blobsetVersion >= 2 ? ZSTD_IO.ReadTXPKInfo(filePath) : LZMA_IO.ReadTXPKInfo(Global.filelist);

                    if (txpk == null)
                        return;

                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** TXPK Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;

                    fileInfo_richTextBox.AppendText("DDS File Count: " + txpk.FilesCount.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("TXPK File Size: " + Utilities.FormatSize(txpkSize) + Environment.NewLine + Environment.NewLine);

                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** TXPK DDS File List ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;

                    if (txpk.Entries == null)
                        return;

                    int i = 0;

                    foreach (var entry in txpk.Entries)
                    {
                        fileInfo_richTextBox.AppendText(i.ToString() + " - " + entry.DDSFilePath.Replace("/", @"\") + ".dds" + Environment.NewLine);
                        i++;
                    }
                }
                else if (type == ".m3mp")
                {
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** M3MP Location ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString() + Environment.NewLine + Environment.NewLine);

                    bool isCompressed = false;

                    if (MainCompressedSize != MainUnCompressedSize)
                        isCompressed = true;

                    M3MP m3mp = ZSTD_IO.ReadM3MPInfo(filePath, isCompressed);

                    if (m3mp == null)
                        return;

                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** M3MP Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText("M3MP File Count: " + m3mp.FilesCount.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("M3MP File Size: " + Utilities.FormatSize(MainUnCompressedSize) + Environment.NewLine + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** M3MP File List ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;

                    if (m3mp.UnCompressedEntries == null)
                        return;

                    int i = 0;

                    foreach (var entry in m3mp.UnCompressedEntries)
                    {
                        fileInfo_richTextBox.AppendText(i.ToString() + " - " + entry.FilePath.Replace("/", @"\") + Environment.NewLine);
                        i++;
                    }
                }
                else if (type == ".wem")
                {
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Wise Audio WEM Location ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString() + Environment.NewLine + Environment.NewLine);

                    if (player != null) { player.Stop(); player.Stream.Dispose(); player.Dispose(); player = null; }
                    WEMFile? wem = new(filePath, WEMForcePacketFormat.NoForcePacketFormat);
                    MemoryStream wem_Ms = new();
                    wem.GenerateOGG(wem_Ms, Global.currentPath + @"\packed_codebooks_aoTuV_603.bin", false, false);
                    oggData = wem_Ms.ToArray();
                    MemoryStream wav_ms = IO.WriteVorbisOggWAVData(wem_Ms, wem.SampleRate, wem.Channels, wem.SampleCount);
                    wavData = wav_ms.ToArray();
                    player = new System.Media.SoundPlayer(wav_ms);
                    player.Play();

                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Wise Audio WEM Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText("WEM Channel Count: " + wem.Channels + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("WEM Sample Rate: " + wem.SampleRate + " Hz" + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("WEM Average Bytes Per Second: " + Utilities.FormatSize(wem.AverageBytesPerSecond) + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("WEM File Size: " + Utilities.FormatSize((ulong)Utilities.FileInfo(filePath)));

                    if (wem._wemFile != null) { wem._wemFile.Dispose(); wem = null; }
                    if (wem_Ms != null) { wem_Ms.Dispose(); wem = null; }
                }
                else if (type == ".bnk")
                {
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Wise Audio BNK Location ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString() + Environment.NewLine + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Wise Audio BNK Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText("BNK File Size: " + Utilities.FormatSize(MainUnCompressedSize));
                }
                else if (type == ".dat")
                {
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Unknown File DAT Location ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                    fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("FileName: " + filePath + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainCompressedSize: " + MainCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("MainUnCompressedSize: " + MainUnCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramCompressedSize: " + VramCompressedSize.ToString() + Environment.NewLine);
                    fileInfo_richTextBox.AppendText("VramUnCompressedSize: " + VramUnCompressedSize.ToString() + Environment.NewLine + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.White;
                    fileInfo_richTextBox.AppendText("*** Unknown File DAT Info ***" + Environment.NewLine);
                    fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
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
                int blobsetVersion = Properties.Settings.Default.BlobsetVersion;

                if (Properties.Settings.Default.BlobsetVersion != (int)BlobsetVersion.v4)
                    filePath = Properties.Settings.Default.GameLocation;

                if (!File.Exists(filePath))
                {
                    MessageBox.Show("Can't find the file - " + Global.filelist[Global.fileIndex].FilePath + ". You might need to run the File Mapping Data in the Options.", "File Not Found !!!", MessageBoxButtons.OK, MessageBoxIcon.Exclamation);
                    return;
                }

                uint MainCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                uint MainUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;

                if (ext == ".txpk")
                {

                    TXPK txpk = blobsetVersion >= 2 ? ZSTD_IO.ReadTXPKInfo(filePath) : LZMA_IO.ReadTXPKInfo(Global.filelist);

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
            BlobsetVersion blobsetVersion = (BlobsetVersion)Properties.Settings.Default.BlobsetVersion;

            bool errorCheck = true;

            switch (blobsetVersion)
            {
                case BlobsetVersion.v1:
                    errorCheck = Extract.BlobsetV1(blobsetFile, Extract_bgw);
                    break;
                case BlobsetVersion.v2:
                    errorCheck = Extract.BlobsetV2(blobsetFile, Extract_bgw);
                    break;
                case BlobsetVersion.v3:
                    errorCheck = Extract.BlobsetV3(blobsetFile, Extract_bgw);
                    break;
                case BlobsetVersion.v4:
                    errorCheck = Extract.BlobsetV4(blobsetFile, Extract_bgw);
                    break;
            }

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
            BlobsetVersion blobsetVersion = (BlobsetVersion)Properties.Settings.Default.BlobsetVersion;
            bool errorCheck = true;

            switch (blobsetVersion)
            {
                case BlobsetVersion.v1:
                    errorCheck = FileMapping.WriteV1(blobsetFile, FileMapping_bgw);
                    break;
                case BlobsetVersion.v2:
                    errorCheck = FileMapping.WriteV2(blobsetFile, FileMapping_bgw);
                    break;
                case BlobsetVersion.v3:
                    errorCheck = FileMapping.WriteV3(blobsetFile, FileMapping_bgw);
                    break;
                case BlobsetVersion.v4:
                    errorCheck = FileMapping.WriteV4(blobsetFile, FileMapping_bgw);
                    break;
            }

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

                string gameVersion = Utilities.GetGameVersion();
                int gameID = Properties.Settings.Default.GameID;

                if (gameID == (int)Enums.Game.AFLL || gameID == (int)Enums.Game.RLL2)
                {
                    File.Delete(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt");
                    File.WriteAllText(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt", "1");
                }
                else
                {
                    File.Delete(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt");
                    File.WriteAllText(Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\version.txt", gameVersion);
                }

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

            if (FileMapping_bgw != null) { FileMapping_bgw.Dispose(); FileMapping_bgw = null; }

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
            BlobsetVersion blobsetVersion = (BlobsetVersion)Properties.Settings.Default.BlobsetVersion;

            bool errorCheck = true;

            switch (blobsetVersion)
            {
                case BlobsetVersion.v1:
                    errorCheck = Modify.BlobsetV1(blobsetFile, Modify_bgw);
                    break;
                case BlobsetVersion.v2:
                    errorCheck = Modify.BlobsetV2(blobsetFile, Modify_bgw);
                    break;
                case BlobsetVersion.v3:
                    errorCheck = Modify.BlobsetV3(blobsetFile, Modify_bgw);
                    break;
                case BlobsetVersion.v4:
                    errorCheck = Modify.BlobsetV4(blobsetFile, Modify_bgw);
                    break;
            }

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
                int blobsetVersion = Properties.Settings.Default.BlobsetVersion;
                byte[] ddsData = blobsetVersion >= 2 ? UI.GetDDSData_V3_V4(Global.filelist) : UI.GetDDSData_V1_V2(Global.filelist);

                File.WriteAllBytes(saveFileDialog.FileName, ddsData);

                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText("DDS File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show("DDS File has been saved to - " + saveFileDialog.FileName, "Save DDS File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void pngFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;
            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save PNG File";
            saveFileDialog.Filter = "PNG" + " File|*.png";
            saveFileDialog.DefaultExt = "png";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                dds_pictureBox.Image.Save(saveFileDialog.FileName);

                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText("PNG File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show("PNG File has been saved to - " + saveFileDialog.FileName, "Save PNG File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void wemFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (player != null) { player.Stop(); player.Stream.Dispose(); player.Dispose(); player = null; }

            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save WEM File";
            saveFileDialog.Filter = "WEM File|*" + ".wem";
            saveFileDialog.DefaultExt = "wem";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                string blobsetFilePath = Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + list[Global.fileIndex].FolderHash + @"\" + list[Global.fileIndex].FileHash;
                IO.ReadWriteData(blobsetFilePath, saveFileDialog.FileName);
                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText("WEM File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show("WEM File has been saved to - " + saveFileDialog.FileName, "Save WEM File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void oggFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (player != null) { player.Stop(); player.Stream.Dispose(); player.Dispose(); player = null; }

            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save OGG File";
            saveFileDialog.Filter = "OGG File|*" + ".ogg";
            saveFileDialog.DefaultExt = "ogg";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                if (oggData != null)
                {
                    File.WriteAllBytes(saveFileDialog.FileName, oggData);
                    fileInfo_richTextBox.Clear();
                    fileInfo_richTextBox.AppendText("OGG File has been saved to - " + saveFileDialog.FileName);
                    MessageBox.Show("OGG File has been saved to - " + saveFileDialog.FileName, "Save OGG File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
                }
            }
            saveFileDialog.Dispose();
        }

        private void wavFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (player != null) { player.Stop(); player.Stream.Dispose(); player.Dispose(); player = null; }

            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save WAV File";
            saveFileDialog.Filter = "WAV File|*" + ".wav";
            saveFileDialog.DefaultExt = "wav";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                if (oggData != null)
                {
                    File.WriteAllBytes(saveFileDialog.FileName, wavData);
                    fileInfo_richTextBox.Clear();
                    fileInfo_richTextBox.AppendText("WAV File has been saved to - " + saveFileDialog.FileName);
                    MessageBox.Show("WAV File has been saved to - " + saveFileDialog.FileName, "Save WAV File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
                }
            }
            saveFileDialog.Dispose();
        }

        private void datFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save DAT File";
            saveFileDialog.Filter = "DAT File|*" + ".dat";
            saveFileDialog.DefaultExt = "dat";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                if (Path.GetDirectoryName(path) == @"unknown\mainuncompressed")
                {
                    int blobsetVersion = Properties.Settings.Default.BlobsetVersion;

                    if (blobsetVersion == (int)BlobsetVersion.v4)
                    {
                        string blobsetFilePath = Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + list[Global.fileIndex].FolderHash + @"\" + list[Global.fileIndex].FileHash;
                        IO.ReadWriteData(blobsetFilePath, saveFileDialog.FileName);
                    }
                    else
                    {

                    }
                }

                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText("DAT File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show("DAT File has been saved to - " + saveFileDialog.FileName, "Save DAT File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
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
                int index = fileIndex + 1;

                if (fileIndex == 0)
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

        private void restoreBackupFilesToolStripMenuItem_Click(object sender, EventArgs e)
        {
            string backupFilePath = Global.currentPath + "\\games\\" + Properties.Settings.Default.GameName + "\\backup\\";
            string gameLocation = Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", "");

            string[] files = Utilities.DirectoryInfo(backupFilePath, "*");

            if (files.Length != 0)
            {
                foreach (string file in files)
                {
                    string? folder = Path.GetDirectoryName(file.Replace(backupFilePath, string.Empty));
                    string filePath = gameLocation + folder + @"\" + Path.GetFileName(file);

                    if (folder == string.Empty)
                        filePath = gameLocation + folder + Path.GetFileName(file);

                    if (File.Exists(filePath))
                    {
                        File.Delete(filePath);
                        File.Move(file, filePath);
                    }
                }

                UI.BlobsetHeaderData();
                MessageBox.Show("Backup files have been restored.", "Restore Backup Files", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            else
                MessageBox.Show("No files to restore.", "Restore Backup Files", MessageBoxButtons.OK, MessageBoxIcon.Warning);
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

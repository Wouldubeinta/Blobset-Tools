using BlobsetIO;
using PackageIO;
using System.ComponentModel;


namespace Blobset_Tools
{
    public partial class TXPK_Creator : Form
    {
        private BackgroundWorker? TXPK_Create_bgw = null;
        private ExtractFileInfo? TXPK_Xml_In = null;
        private PS3_DDS_Header? TXPK_Console_Xml_In = null;

        public TXPK_Creator()
        {
            InitializeComponent();
        }

        private void TXPK_Creator_Load(object sender, EventArgs e)
        {
            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            string platformExt = platformDetails["PlatformExt"];

            string TXPK_List = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "txpk", "TXPK_List.xml");

            if (File.Exists(TXPK_List))
            {
                txpk_richTextBox.AppendText("TXPK_List.xml found, ready for TXPK creation");
                txpk_richTextBox.AppendText(Environment.NewLine);
                txpk_richTextBox.AppendText("Click on Create TXPK.");
                txpk_richTextBox.AppendText(Environment.NewLine);
                txpk_richTextBox.AppendText(Environment.NewLine);
            }
            else
                txpk_richTextBox.AppendText("Could not find TXPK_List.xml. Make sure you place the files and TXPK_List.xml in the txpk folder");
        }

        private void createTXPKToolStripMenuItem_Click(object sender, EventArgs e)
        {
            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            string platformExt = platformDetails["PlatformExt"];

            string txpkListXML = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "txpk", "TXPK_List.xml");

            if (File.Exists(txpkListXML))
            {
                if (Global.isBigendian)
                {
                    TXPK_Console_Xml_In = IO.XmlDeserialize<PS3_DDS_Header>(txpkListXML);

                    if (TXPK_Console_Xml_In == null)
                    {
                        MessageBox.Show("There is a problem with the xml file - TXPK_List.xml", "XML Format Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                        return;
                    }

                    saveFileDialog1.FileName = TXPK_Console_Xml_In.Index.ToString();
                }
                else
                {
                    TXPK_Xml_In = IO.XmlDeserialize<ExtractFileInfo>(txpkListXML);

                    if (TXPK_Xml_In == null)
                    {
                        MessageBox.Show("There is a problem with the xml file - TXPK_List.xml", "XML Format Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                        return;
                    }

                    saveFileDialog1.FileName = TXPK_Xml_In.Index.ToString();
                }

                saveFileDialog1.InitialDirectory = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "mods", "dds_txpk");

                if (saveFileDialog1.ShowDialog() == DialogResult.OK)
                    TXPK_Create();
            }
            else
                txpk_richTextBox.AppendText("Could not find TXPK_List.xml. Make sure you place the files and TXPK_List.xml in the txpk folder");
        }

        private void TXPK_Create()
        {
            TXPK_Create_bgw = new BackgroundWorker();
            TXPK_Create_bgw.DoWork += new DoWorkEventHandler(TXPK_Create_bgw_DoWork);
            TXPK_Create_bgw.ProgressChanged += new ProgressChangedEventHandler(TXPK_Create_bgw_ProgressChanged);
            TXPK_Create_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(TXPK_Create_bgw_RunWorkerCompleted);
            TXPK_Create_bgw.WorkerReportsProgress = true;
            TXPK_Create_bgw.WorkerSupportsCancellation = true;
            TXPK_Create_bgw.RunWorkerAsync();
        }

        private void TXPK_Create_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            bool errorCheck = CreateTXPK();

            if (errorCheck)
                e.Cancel = true;
        }

        private void TXPK_Create_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            int progressPercentage = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            progressStripStatusLabel.Text = $"{progressPercentage} %";
            txpk_richTextBox.AppendText(e.UserState.ToString());
            txpk_richTextBox.AppendText(Environment.NewLine);
            toolStripProgressBar.Value = progressPercentage;
        }

        private void TXPK_Create_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                status_Label.Text = "TXPK file has finished creating....";
                txpk_richTextBox.AppendText(Environment.NewLine);
                txpk_richTextBox.AppendText(Environment.NewLine);
                txpk_richTextBox.AppendText("TXPK file has finished creating....");
                MessageBox.Show("TXPK file has finished creating", "TXPK Creating", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }

            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;
            saveFileDialog1.Dispose();
            if (TXPK_Create_bgw != null) { TXPK_Create_bgw.Dispose(); TXPK_Create_bgw = null; }
        }

        private bool CreateTXPK()
        {
            Reader? br = null;
            FileStream? writer = null;
            TXPK? txpk = new();

            try
            {
                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];

                int txpkCount = Global.isBigendian ? TXPK_Console_Xml_In.Entries.Length : TXPK_Xml_In.Entries.Length;

                txpk.Entries = new TXPK.Entry[txpkCount];
                txpk.FilesCount = (uint)txpkCount;

                int dataOffset = 0;

                for (int i = 0; i < txpkCount; i++)
                {
                    txpk.Entries[i] = new();
                    string filePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "txpk", Global.isBigendian ? TXPK_Console_Xml_In.Entries[i].FilePath : TXPK_Xml_In.Entries[i].FilePath);
                    br = new Reader(filePath);

                    br.Position = 12;
                    txpk.Entries[i].DDSHeight = br.ReadUInt32();
                    txpk.Entries[i].DDSWidth = br.ReadUInt32();
                    txpk.Entries[i].DDSHeight2 = (ushort)txpk.Entries[i].DDSHeight;
                    txpk.Entries[i].DDSWidth2 = (ushort)txpk.Entries[i].DDSWidth;
                    br.Position = 28;
                    txpk.Entries[i].DDSMipMaps = (byte)br.ReadUInt32();

                    byte imageType = 1;

                    if (Path.GetFileNameWithoutExtension(filePath).Contains("mat.pc"))
                        imageType = 2;

                    txpk.Entries[i].DDSImageType = imageType;
                    txpk.Entries[i].DDSImageType2 = imageType;
                    txpk.Entries[i].DDSFilePathOffset = 20; // always 0x14

                    txpk.Entries[i].DDSDataOffset = (uint)dataOffset;

                    if (Global.isBigendian)
                    {
                        txpk.Entries[i].DDSDataSize = (uint)Utilities.FileInfo(filePath) - 128;
                        txpk.Entries[i].DDSFilePath = TXPK_Console_Xml_In.Entries[i].FilePath.Replace(@"\", "/").Replace(".dds", string.Empty);
                        txpk.Entries[i].BufferSize = TXPK_Console_Xml_In.Entries[i].BufferSize;
                        txpk.Entries[i].ConsoleDDSHeaderOffset = (uint)txpk.Entries[i].DDSFilePath.Length + txpk.Entries[i].DDSFilePathOffset + txpk.Entries[i].BufferSize + 1;
                        txpk.Entries[i].DDSPathSize = txpk.Entries[i].ConsoleDDSHeaderOffset + 24;
                        txpk.Entries[i].DDSType = TXPK_Console_Xml_In.Entries[i].DDSType;
                        //txpk.Entries[i].DDSMipMaps = TXPK_Console_Xml_In.Entries[i].DDSMipMaps;
                        txpk.Entries[i].Unknown1 = TXPK_Console_Xml_In.Entries[i].Unknown1;
                        txpk.Entries[i].Unknown2 = TXPK_Console_Xml_In.Entries[i].Unknown2;
                        txpk.Entries[i].Unknown3 = TXPK_Console_Xml_In.Entries[i].Unknown3;
                        txpk.Entries[i].Unknown4 = TXPK_Console_Xml_In.Entries[i].Unknown4;
                        txpk.Entries[i].Unknown5 = TXPK_Console_Xml_In.Entries[i].Unknown5;
                        txpk.Entries[i].Reserved = TXPK_Console_Xml_In.Entries[i].Reserved;
                    }
                    else
                    {
                        txpk.Entries[i].DDSFilePath = TXPK_Xml_In.Entries[i].FilePath.Replace(@"\", "/").Replace(".dds", string.Empty);
                        txpk.Entries[i].DDSDataSize = (uint)Utilities.FileInfo(filePath);
                        txpk.Entries[i].DDSPathSize = (uint)txpk.Entries[i].DDSFilePath.Length + 21;
                        txpk.Entries[i].ConsoleDDSHeaderOffset = txpk.Entries[i].DDSDataSize;
                    }
                    dataOffset += (int)txpk.Entries[i].DDSDataSize;
                }

                if (br != null) { br.Close(); br = null; }

                if (File.Exists(saveFileDialog1.FileName))
                    File.Delete(saveFileDialog1.FileName);

                writer = new FileStream(saveFileDialog1.FileName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                txpk.Serialize(writer);

                if (writer != null) { writer.Dispose(); writer = null; }

                int HeaderSize = (int)Utilities.FileInfo(saveFileDialog1.FileName);

                if (File.Exists(saveFileDialog1.FileName))
                    File.Delete(saveFileDialog1.FileName);

                writer = new FileStream(saveFileDialog1.FileName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                txpk.Serialize(writer);

                if (writer != null) { writer.Dispose(); writer = null; }

                writer = new FileStream(saveFileDialog1.FileName, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                for (int i = 0; i < txpkCount; i++)
                {
                    string filePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "txpk", Global.isBigendian ? TXPK_Console_Xml_In.Entries[i].FilePath : TXPK_Xml_In.Entries[i].FilePath);
                    br = new Reader(filePath);

                    int ddsSize = (int)txpk.Entries[i].DDSDataSize;

                    int tmpChunkCount = Utilities.ChunkAmount(ddsSize);
                    long[] tmpChunkSizes = Utilities.ChunkSizes(ddsSize, tmpChunkCount);

                    br.Position = Global.isBigendian ? 128 : 0;

                    byte ddsType = txpk.Entries[i].DDSType;

                    if (ddsType == 133 || ddsType == 156)
                    {
                        byte[] tmpData = br.ReadBytes(ddsSize);
                        byte[] ddsDataSwizzled = PS3_DDS.SwizzleMorton(tmpData,
                            (int)txpk.Entries[i].DDSWidth,
                            (int)txpk.Entries[i].DDSHeight,
                            32, 1, 1);
                        IO.ReadWriteData(ddsDataSwizzled, writer, ddsDataSwizzled.Length);
                    }
                    else
                    {
                        for (int j = 0; j < tmpChunkCount; j++)
                        {
                            byte[] chunkData = br.ReadBytes((int)tmpChunkSizes[j]);
                            IO.ReadWriteData(chunkData, writer, (int)tmpChunkSizes[j]);
                        }
                    }

                    if (br != null) { br.Close(); br = null; }
                }

                if (writer != null) { writer.Dispose(); writer = null; }

                int FileSize = (int)Utilities.FileInfo(saveFileDialog1.FileName);

                ModifyFileInfo txpkXmlOut = new();
                txpkXmlOut.Index = Global.isBigendian ? TXPK_Console_Xml_In.Index : TXPK_Xml_In.Index;
                txpkXmlOut.IsCompressed = Global.isBigendian ? TXPK_Console_Xml_In.IsCompressed : TXPK_Xml_In.IsCompressed;
                txpkXmlOut.MainCompressedSize = 0;
                txpkXmlOut.MainUnCompressedSize = HeaderSize;
                txpkXmlOut.VramCompressedSize = 0;
                txpkXmlOut.VramUnCompressedSize = FileSize - HeaderSize;

                string directory = Path.GetDirectoryName(saveFileDialog1.FileName);
                string fileNameWithoutExtension = Path.GetFileNameWithoutExtension(saveFileDialog1.FileName);
                string xmlFilePath = Path.Combine(directory, $"{fileNameWithoutExtension}.xml");

                IO.XmlSerialize(xmlFilePath, txpkXmlOut);
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
    }
}

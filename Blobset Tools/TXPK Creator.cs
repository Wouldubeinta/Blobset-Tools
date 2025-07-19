using BlobsetIO;
using PackageIO;
using System.ComponentModel;


namespace Blobset_Tools
{
    public partial class TXPK_Creator : Form
    {
        private BackgroundWorker? TXPK_Create_bgw = null;
        private ExtractFileInfo? TXPK_Xml_In = null;

        public TXPK_Creator()
        {
            InitializeComponent();
        }

        private void TXPK_Creator_Load(object sender, EventArgs e)
        {
            if (File.Exists(Global.currentPath + @"\txpk\TXPK_List.xml"))
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
            if (File.Exists(Global.currentPath + @"\txpk\TXPK_List.xml")) 
            {
                TXPK_Xml_In = IO.XmlDeserialize<ExtractFileInfo>(Global.currentPath + @"\txpk\TXPK_List.xml");

                if (TXPK_Xml_In == null)
                {
                    MessageBox.Show("There is a problem with the xml file - TXPK_List.xml", "XML Format Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }

                saveFileDialog1.FileName = TXPK_Xml_In.Index.ToString();

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
            toolStripProgressBar1.Value = e.ProgressPercentage;
            progressStripStatusLabel.Text = string.Format("{0} %", e.ProgressPercentage);
            txpk_richTextBox.AppendText(e.UserState.ToString());
            txpk_richTextBox.AppendText(Environment.NewLine);
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

            toolStripProgressBar1.Value = 0;
            progressStripStatusLabel.Text = string.Empty;
            saveFileDialog1.Dispose();
            if (TXPK_Create_bgw != null) { TXPK_Create_bgw.Dispose(); TXPK_Create_bgw = null; }
        }

        private bool CreateTXPK() 
        {
            Reader? br = null;
            FileStream? writer = null;
            TXPK? txpk = new();
            bool result = false;


            try
            {
                int totalFileSize = 0;
                long[]? chunkSizes = null;

                txpk.Entries = new TXPK.Entry[TXPK_Xml_In.Entries.Length];
                txpk.FilesCount = (uint)TXPK_Xml_In.Entries.Length;

                int dataOffset = 0;

                for (int i = 0; i < TXPK_Xml_In.Entries.Length; i++) 
                {
                    txpk.Entries[i] = new();
                    string filePath = Global.currentPath + @"\txpk\" + TXPK_Xml_In.Entries[i].FilePath;
                    br = new Reader(filePath);

                    br.Position = 12;
                    txpk.Entries[i].DDSWidth = br.ReadUInt32();
                    txpk.Entries[i].DDSHeight = br.ReadUInt32();

                    byte imageType = 1;

                    if (Path.GetFileNameWithoutExtension(filePath).Contains("mat.pc"))
                        imageType = 2;

                    txpk.Entries[i].DDSImageType = imageType;
                    txpk.Entries[i].HeaderSize = 20; // always 0x14
                    txpk.Entries[i].DDSDataSize1 = (uint)Utilities.FileInfo(filePath);
                    txpk.Entries[i].DDSDataSize2 = txpk.Entries[i].DDSDataSize1;
                    txpk.Entries[i].DDSFilePath = TXPK_Xml_In.Entries[i].FilePath.Replace(@"\", "/").Replace(".dds", string.Empty);
                    txpk.Entries[i].DDSPathSize = (uint)txpk.Entries[i].DDSFilePath.Length;
                    txpk.Entries[i].DDSDataOffset = (uint)dataOffset;

                    dataOffset += (int)txpk.Entries[i].DDSDataSize1;
                }

                if (br != null) { br.Close(); br = null; }

                writer = new FileStream(saveFileDialog1.FileName, FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                txpk.Serialize(writer);

                int HeaderSize = (int)Utilities.FileInfo(saveFileDialog1.FileName);

                writer.Position = 0;
                txpk.Serialize(writer);

                if (writer != null) { writer.Dispose(); writer = null; }

                writer = new FileStream(saveFileDialog1.FileName, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                for (int i = 0; i < TXPK_Xml_In.Entries.Length; i++) 
                {
                    string filePath = Global.currentPath + @"\txpk\" + TXPK_Xml_In.Entries[i].FilePath;
                    br = new Reader(filePath);

                    int tmpChunkCount = Utilities.ChunkAmount((int)txpk.Entries[i].DDSDataSize1);
                    long[] tmpChunkSizes = Utilities.ChunkSizes((int)txpk.Entries[i].DDSDataSize1, tmpChunkCount);

                    for (int j = 0; j < tmpChunkCount; j++)
                    {
                        byte[] tmpData = br.ReadBytes((int)tmpChunkSizes[j]);
                        IO.ReadWriteData(tmpData, writer, (int)tmpChunkSizes[j]);
                    }
                }

                if (br != null) { br.Close(); br = null; }
                if (writer != null) { writer.Dispose(); writer = null; }

                int FileSize = (int)Utilities.FileInfo(saveFileDialog1.FileName);

                ModifyFileInfo txpkXmlOut = new();
                txpkXmlOut.Index = TXPK_Xml_In.Index;
                txpkXmlOut.IsCompressed = TXPK_Xml_In.IsCompressed;
                txpkXmlOut.MainCompressedSize = 0;
                txpkXmlOut.MainUnCompressedSize = HeaderSize;
                txpkXmlOut.VramCompressedSize = 0;
                txpkXmlOut.VramUnCompressedSize = FileSize;
                IO.XmlSerialize(Path.GetDirectoryName(saveFileDialog1.FileName) + @"\" + Path.GetFileNameWithoutExtension(saveFileDialog1.FileName) + ".xml", txpkXmlOut);
            }
            catch (Exception error)
            {
                result = true;
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally 
            {
                if (br != null) { br.Close(); br = null; }
                if (writer != null) { writer.Dispose(); writer = null; }
            }
            return result;
        }
    }
}

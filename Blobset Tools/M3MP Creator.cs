using BlobsetIO;
using PackageIO;
using System.ComponentModel;
using static BlobsetIO.M3MP;


namespace Blobset_Tools
{
    public partial class M3MP_Creator : Form
    {
        private BackgroundWorker? M3MP_Create_bgw = null;
        private ExtractFileInfo? M3MP_Xml_In = null;

        public M3MP_Creator()
        {
            InitializeComponent();
        }

        private void M3MP_Creator_Load(object sender, EventArgs e)
        {
            DeleteTempFiles();

            if (File.Exists(Global.currentPath + @"\m3mp\M3MP_List.xml"))
            {
                m3mp_richTextBox.AppendText("M3MP_List.xml found, ready for M3MP creation");
                m3mp_richTextBox.AppendText(Environment.NewLine);
                m3mp_richTextBox.AppendText("Click on Create M3MP.");
                m3mp_richTextBox.AppendText(Environment.NewLine);
                m3mp_richTextBox.AppendText(Environment.NewLine);
            }
            else
                m3mp_richTextBox.AppendText("Could not find M3MP_List.xml. Make sure you place the files and M3MP_List.xml in the m3mp folder");
        }

        private void createToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (File.Exists(Global.currentPath + @"\m3mp\M3MP_List.xml"))
            {
                M3MP_Xml_In = IO.XmlDeserialize<ExtractFileInfo>(Global.currentPath + @"\m3mp\M3MP_List.xml");

                if (M3MP_Xml_In == null)
                {
                    MessageBox.Show("There is a problem with the xml file - M3MP_List.xml", "XML Format Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }

                saveFileDialog1.FileName = M3MP_Xml_In.Index.ToString();

                string compCheck = M3MP_Xml_In.IsCompressed ? "compressed" : "uncompressed";

                saveFileDialog1.InitialDirectory = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\mods\m3mp\" + compCheck;

                if (saveFileDialog1.ShowDialog() == DialogResult.OK)
                    M3MP_Create();
            }
            else
                m3mp_richTextBox.AppendText("Could not find M3MP_List.xml. Make sure you place the files and M3MP_List.xml in the m3mp folder");
        }

        private void M3MP_Create()
        {
            M3MP_Create_bgw = new BackgroundWorker();
            M3MP_Create_bgw.DoWork += new DoWorkEventHandler(M3MP_Create_bgw_DoWork);
            M3MP_Create_bgw.ProgressChanged += new ProgressChangedEventHandler(M3MP_Create_bgw_ProgressChanged);
            M3MP_Create_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(M3MP_Create_bgw_RunWorkerCompleted);
            M3MP_Create_bgw.WorkerReportsProgress = true;
            M3MP_Create_bgw.WorkerSupportsCancellation = true;
            M3MP_Create_bgw.RunWorkerAsync();
        }

        private void M3MP_Create_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            bool errorCheck = CreateM3MP();

            if (errorCheck)
                e.Cancel = true;
        }

        private void M3MP_Create_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            toolStripProgressBar1.Value = e.ProgressPercentage;
            progressStripStatusLabel.Text = string.Format("{0} %", e.ProgressPercentage);
            m3mp_richTextBox.AppendText(e.UserState.ToString());
            m3mp_richTextBox.AppendText(Environment.NewLine);
        }

        private void M3MP_Create_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                status_Label.Text = "M3MP file has finished creating....";
                m3mp_richTextBox.AppendText(Environment.NewLine);
                m3mp_richTextBox.AppendText(Environment.NewLine);
                m3mp_richTextBox.AppendText("M3MP file has finished creating....");
                MessageBox.Show("M3MP file has finished creating", "M3MP Creating", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }

            toolStripProgressBar1.Value = 0;
            progressStripStatusLabel.Text = string.Empty;
            saveFileDialog1.Dispose();
            if (M3MP_Create_bgw != null) { M3MP_Create_bgw.Dispose(); M3MP_Create_bgw = null; }
        }

        private bool CreateM3MP()
        {
            Reader? br = null;
            Writer? bw = null;
            FileStream? writer = null;
            bool result = false;
            M3MP? m3mp = new();

            try
            {
                int chunkSize = 32768;
                int compressedOffset = 16;
                int totalFileSize = 0;
                long[]? chunkSizes = null;

                m3mp.UnCompressedEntries = new UnCompressedEntry[M3MP_Xml_In.Entries.Length];

                for (int i = 0; i < M3MP_Xml_In.Entries.Length; i++)
                {
                    string filePath = Global.currentPath + @"\m3mp\" + M3MP_Xml_In.Entries[i].FilePath.Replace("/", @"\");

                    m3mp.UnCompressedEntries[i] = new();
                    m3mp.UnCompressedEntries[i].UncompressedDataInfo = new UncompressedDataInfo();
                    m3mp.UnCompressedEntries[i].UncompressedDataInfo.Size = (uint)Utilities.FileInfo(filePath);

                    totalFileSize += (int)m3mp.UnCompressedEntries[i].UncompressedDataInfo.Size;
                    m3mp.UnCompressedEntries[i].UncompressedDataInfo.Reserved = 0;
                    m3mp.UnCompressedEntries[i].FilePath = M3MP_Xml_In.Entries[i].FilePath.Replace(@"\", @"/");

                    int filePathLength = M3MP_Xml_In.Entries[i].FilePath.Length + 1;
                    compressedOffset += filePathLength;
                    compressedOffset += 16;

                    int percentProgress = 100 * i / M3MP_Xml_In.Entries.Length;
                    M3MP_Create_bgw.ReportProgress(percentProgress, "Getting header data ready: " + M3MP_Xml_In.Entries[i].FilePath);
                }

                m3mp.CompressedDataOffset = (uint)compressedOffset;
                m3mp.ChunksCount = (uint)Utilities.ChunkAmount(totalFileSize, chunkSize);
                chunkSizes = Utilities.ChunkSizes(totalFileSize, (int)m3mp.ChunksCount, chunkSize);

                M3MP_Create_bgw.ReportProgress(0, Environment.NewLine);

                m3mp.CompressedEntries = new CompressedEntry[m3mp.ChunksCount];

                for (int i = 0; i < m3mp.ChunksCount; i++)
                {
                    CompressedEntry entry = new();
                    entry.CompressedDataInfo = new();
                    int percentProgress = 100 * i / (int)m3mp.ChunksCount;
                    M3MP_Create_bgw.ReportProgress(percentProgress, "Initializing chunk compressed data info array chunk " + (i + 1).ToString());
                    m3mp.CompressedEntries[i] = entry;
                }

                m3mp.FilesCount = (uint)M3MP_Xml_In.Entries.Length;

                bw = new Writer(Global.currentPath + @"\temp\m3mp_header.tmp");

                m3mp.Serialize(bw);

                m3mp.UnCompressedEntries[0].UncompressedDataInfo.Offset = 0;

                writer = new FileStream(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp", FileMode.OpenOrCreate, FileAccess.Write, FileShare.ReadWrite);

                M3MP_Create_bgw.ReportProgress(0, Environment.NewLine);

                uint uncompressedOffset = 0;

                for (int i = 0; i < M3MP_Xml_In.Entries.Length; i++)
                {
                    string filePath = Global.currentPath + @"\m3mp\" + M3MP_Xml_In.Entries[i].FilePath.Replace("/", @"\");
                    br = new Reader(filePath);

                    int tmpChunkCount = Utilities.ChunkAmount((int)Utilities.FileInfo(filePath));
                    long[] tmpChunkSizes = Utilities.ChunkSizes((int)Utilities.FileInfo(filePath), tmpChunkCount);

                    for (int j = 0; j < tmpChunkCount; j++)
                    {
                        byte[] tmpData = br.ReadBytes((int)tmpChunkSizes[j]);
                        IO.ReadWriteData(tmpData, writer, (int)tmpChunkSizes[j]);
                    }

                    if (br != null) { br.Close(); br = null; }

                    m3mp.UnCompressedEntries[i].UncompressedDataInfo.Offset = uncompressedOffset;

                    uncompressedOffset += m3mp.UnCompressedEntries[i].UncompressedDataInfo.Size;

                    int percentProgress = 100 * i / M3MP_Xml_In.Entries.Length;
                    M3MP_Create_bgw.ReportProgress(percentProgress, "Writing temp data : " + M3MP_Xml_In.Entries[i].FilePath);
                }

                if (writer != null) { writer.Dispose(); writer = null; }
                if (bw != null) { bw.Close(); bw = null; }

                br = new(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp");
                writer = new(Global.currentPath + @"\temp\m3mp_compressed_data.tmp", FileMode.OpenOrCreate, FileAccess.Write);

                M3MP_Create_bgw.ReportProgress(0, Environment.NewLine);

                long m3mpHeaderSize = Utilities.FileInfo(Global.currentPath + @"\temp\m3mp_header.tmp");

                for (int i = 0; i < m3mp.ChunksCount; i++)
                {
                    byte[] buffer2 = br.ReadBytes((int)chunkSizes[i]);
                    int chunkCOffset = 0;
                    int chunkCSize = ZSTD_IO.CompressAndWrite(buffer2, writer, ref chunkCOffset, (int)chunkSizes[i]);
                    m3mp.CompressedEntries[i].CompressedDataInfo.Offset = (uint)chunkCOffset + (uint)m3mpHeaderSize;
                    m3mp.CompressedEntries[i].CompressedDataInfo.CompressedSize = (uint)chunkCSize;
                    m3mp.CompressedEntries[i].CompressedDataInfo.UnCompressedSize = (uint)buffer2.Length;

                    int percentProgress = 100 * i / (int)m3mp.ChunksCount;
                    M3MP_Create_bgw.ReportProgress(percentProgress, "Writing M3MP compressed data chunk " + (i + 1).ToString());
                }

                if (writer != null) { writer.Dispose(); writer = null; }
                if (br != null) { br.Close(); br = null; }

                bw = new(Global.currentPath + @"\temp\m3mp_header.tmp");

                bw.Position = 0;
                m3mp.Serialize(bw);
                bw.Flush();
                if (bw != null) { bw.Close(); bw = null; }

                writer = new FileStream(saveFileDialog1.FileName, FileMode.OpenOrCreate, FileAccess.Write);

                IO.ReadWriteData(Global.currentPath + @"\temp\m3mp_header.tmp", writer);
                IO.ReadWriteData(Global.currentPath + @"\temp\m3mp_compressed_data.tmp", writer);

                ModifyFileInfo m3mpXmlOut = new();
                m3mpXmlOut.Index = M3MP_Xml_In.Index;
                m3mpXmlOut.IsCompressed = M3MP_Xml_In.IsCompressed;
                m3mpXmlOut.MainCompressedSize = 0;
                m3mpXmlOut.MainUnCompressedSize = (int)Utilities.FileInfo(Global.currentPath + @"\temp\m3mp_header.tmp") + (int)Utilities.FileInfo(Global.currentPath + @"\temp\m3mp_compressed_data.tmp");
                m3mpXmlOut.VramCompressedSize = 0;
                m3mpXmlOut.VramUnCompressedSize = 0;
                IO.XmlSerialize(Path.GetDirectoryName(saveFileDialog1.FileName) + @"\" + Path.GetFileNameWithoutExtension(saveFileDialog1.FileName) + ".xml", m3mpXmlOut);

                if (writer != null) { writer.Dispose(); writer = null; }

                DeleteTempFiles();
            }
            catch (Exception error)
            {
                result = true;
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (bw != null) { bw.Close(); bw = null; }
                if (writer != null) { writer.Dispose(); writer = null; }
            }

            return result;
        }

        private void DeleteTempFiles()
        {
            if (File.Exists(Global.currentPath + @"\temp\m3mp_header.tmp"))
                File.Delete(Global.currentPath + @"\temp\m3mp_header.tmp");

            if (File.Exists(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp"))
                File.Delete(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp");

            if (File.Exists(Global.currentPath + @"\temp\m3mp_compressed_data.tmp"))
                File.Delete(Global.currentPath + @"\temp\m3mp_compressed_data.tmp");
        }

        private void M3MP_Creator_FormClosing(object sender, FormClosingEventArgs e)
        {
            DeleteTempFiles();
        }
    }
}

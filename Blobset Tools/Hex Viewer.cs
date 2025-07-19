using PackageIO;
using System.ComponentModel;

namespace Blobset_Tools
{
    public partial class Hex_Viewer : Form
    {
        private readonly string filename;
        //private int fileIndex = 0;
        private readonly List<Structs.FileIndexInfo> list;
        private BackgroundWorker? HexViewer_bgw = null;
        public Hex_Viewer(string _filename, List<Structs.FileIndexInfo> _list)
        {
            InitializeComponent();
            filename = _filename;
            list = _list;
        }

        private void Hex_Viewer_Load(object sender, EventArgs e)
        {
            HexViewerLoad();
        }

        private void HexViewerLoad()
        {
            HexViewer_bgw = new BackgroundWorker();
            HexViewer_bgw.DoWork += new DoWorkEventHandler(HexViewer_bgw_DoWork);
            HexViewer_bgw.ProgressChanged += new ProgressChangedEventHandler(HexViewer_bgw_ProgressChanged);
            HexViewer_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(HexViewer_bgw_RunWorkerCompleted);
            HexViewer_bgw.WorkerReportsProgress = true;
            HexViewer_bgw.WorkerSupportsCancellation = true;
            HexViewer_bgw.RunWorkerAsync();
        }

        private void HexViewer_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            Reader? br = null;

            try
            {
                HexViewer_bgw.ReportProgress(100, "Loading data into Hex Viewer, please wait......");

                string filePath = Properties.Settings.Default.GameLocation.Replace("data-0.blobset.pc", string.Empty) + list[Global.fileIndex].FolderHash + @"\" + list[Global.fileIndex].FileHash;
                br = new Reader(filePath);

                byte[] data = br.ReadBytes((int)br.Length);
                srcHexViewer1.LoadData(data);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : \n\nFile: " + filename + "\n\n" + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
            }
        }

        private void HexViewer_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            status_Label.ForeColor = Color.Black;
            statusStrip1.Refresh();
            status_Label.Text = e.UserState.ToString();
        }

        private void HexViewer_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                status_Label.Text = "Data has loaded into Hex Viewer....";
            }

            if (HexViewer_bgw != null)
            {
                HexViewer_bgw.Dispose();
                HexViewer_bgw = null;
            }
        }

        private void AddItems(byte[] data)
        {
            if (InvokeRequired)
            {
                Invoke((System.Windows.Forms.MethodInvoker)delegate { AddItems(data); });
                return;
            }
            srcHexViewer1.LoadData(data);
        }

        private void firstToolStripMenuItem_Click(object sender, EventArgs e)
        {

        }

        private void nextToolStripMenuItem_Click(object sender, EventArgs e)
        {

        }

        private void previousToolStripMenuItem_Click(object sender, EventArgs e)
        {

        }
    }
}

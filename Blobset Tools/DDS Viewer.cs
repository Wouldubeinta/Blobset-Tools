namespace Blobset_Tools
{
    public partial class DDS_Viewer : Form
    {
        private readonly string ddsfile = string.Empty;
        private readonly List<Structs.FileIndexInfo> list;
        public DDS_Viewer(string _ddsfile, List<Structs.FileIndexInfo> _list)
        {
            InitializeComponent();
            ddsfile = _ddsfile;
            list = _list;
        }

        private void TexturePreview_Load(object sender, EventArgs e)
        {
            toolStripComboBox.SelectedIndex = 4;
            Text = "DDS Viewer - " + ddsfile;
            LoadImage(alphaToolStripMenuItem.Checked);

            if (pictureBox1.Image != null)
            {
                if (flipImageToolStripMenuItem.Checked)
                {
                    pictureBox1.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
                    pictureBox1.Refresh();
                }
            }
        }

        private unsafe void LoadImage(bool hasAlpha)
        {
            Structs.DDSInfo ddsInfo = new();

            int blobsetVersion = Global.gameInfo.BlobsetVersion;
            byte[] ddsData = blobsetVersion >= 2 ? UI.GetDDSData_V3_V4(Global.filelist) : UI.GetDDSData_V1_V2(Global.filelist);
            Bitmap bitmap = UI.DDStoBitmap(ddsData, hasAlpha, ref ddsInfo);

            string ddsFormat = ddsInfo.isDX10 ? $"{ddsInfo.dxgiFormat.ToString()} - DX11+" : ddsInfo.CompressionAlgorithm.ToString();

            pictureBox1.Image = null;

            if (bitmap != null)
            {
                pictureBox1.Width = bitmap.Width;
                pictureBox1.Height = bitmap.Height;
                pictureBox1.Image = bitmap;

                toolStripStatusLabel1.Text = $"Format: {ddsFormat} | Height: {bitmap.Height.ToString()} | Width: {bitmap.Width.ToString()} | MipMaps: 1/{ddsInfo.MipMap.ToString()}";
            }
        }

        private void flipToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (pictureBox1.Image != null)
            {
                if (flipImageToolStripMenuItem.Checked)
                {
                    pictureBox1.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
                    pictureBox1.Refresh();
                }
            }
        }

        private void alphaToolStripMenuItem_Click(object sender, EventArgs e)
        {
            LoadImage(alphaToolStripMenuItem.Checked);
            pictureBox1.Refresh();
        }

        private void toolStripComboBox_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (ddsfile != string.Empty && pictureBox1.Image != null)
            {
                int index = toolStripComboBox.SelectedIndex;
                PictureBoxSizeMode pbs = (PictureBoxSizeMode)Enum.ToObject(typeof(PictureBoxSizeMode), index);

                pictureBox1.SizeMode = pbs;

                if (flipImageToolStripMenuItem.Checked)
                {
                    pictureBox1.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
                    pictureBox1.Refresh();
                }
            }
        }

        private void pictureBox1_MouseClick(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
                contextMenuStrip1.Show(Cursor.Position);
        }

        private void gridToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (gridToolStripMenuItem.Checked)
            {
                gridToolStripMenuItem.Image = Properties.Resources.grid_dark;
                pictureBox1.BackgroundImage = Properties.Resources.grid_dark;
            }
            else
            {
                gridToolStripMenuItem.Image = Properties.Resources.grid_light;
                pictureBox1.BackgroundImage = Properties.Resources.grid_light;
            }
        }

        private void flipImageToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (pictureBox1.Image != null)
            {
                pictureBox1.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
                pictureBox1.Refresh();
            }
        }

        private void pngFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save PNG File";
            saveFileDialog.Filter = "PNG" + " File|*.png";
            saveFileDialog.DefaultExt = "png";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                pictureBox1.Image.Save(saveFileDialog.FileName);
                MessageBox.Show($"PNG File has been saved to - {saveFileDialog.FileName}", "Save PNG File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }
    }
}

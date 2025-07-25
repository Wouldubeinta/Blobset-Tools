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

        private void TexturePreview_Load(object sender, System.EventArgs e)
        {
            toolStripComboBox.SelectedIndex = 4;
            Text = "DDS Viewer - " + ddsfile;
            LoadImage();

            if (pictureBox1.Image != null)
            {
                if (flipImageToolStripMenuItem.Checked)
                {
                    pictureBox1.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
                    pictureBox1.Refresh();
                }
            }
        }

        private unsafe void LoadImage()
        {
            int mipmapCount = 1;
            Pfim.ImageFormat fmt = Pfim.ImageFormat.Rgba32;
            Pfim.PixelFormat ddsFormat = Pfim.PixelFormat.UnCompressed;

            Structs.DDSInfo ddsInfo = new();

            Bitmap bitmap = UI.DDStoBitmap(UI.GetDDSData(list), ref ddsInfo);

            pictureBox1.Image = null;

            if (bitmap != null)
            {
                pictureBox1.Width = bitmap.Width;
                pictureBox1.Height = bitmap.Height;
                pictureBox1.Image = bitmap;

                if (mipmapCount == 0)
                    mipmapCount = 1;

                toolStripStatusLabel1.Text = "Format: " + ddsFormat.ToString() + " - " + fmt.ToString() + "    Height: " + bitmap.Height.ToString() + "     Width: " + bitmap.Width.ToString() + "     MipMaps: 1/" + mipmapCount.ToString();
            }
        }

        private void alphaEnabledToolStripMenuItem_Click(object sender, EventArgs e)
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
                MessageBox.Show("PNG File has been saved to - " + saveFileDialog.FileName, "Save PNG File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }
    }
}

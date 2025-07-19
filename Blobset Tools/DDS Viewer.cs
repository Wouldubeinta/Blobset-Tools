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
            toolStripComboBox1.SelectedIndex = 4;
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

        private void toolStripComboBox1_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (ddsfile != string.Empty && pictureBox1.Image != null)
            {
                int index = toolStripComboBox1.SelectedIndex;
                PictureBoxSizeMode pbs = (PictureBoxSizeMode)Enum.ToObject(typeof(PictureBoxSizeMode), index);

                pictureBox1.SizeMode = pbs;

                if (flipImageToolStripMenuItem.Checked)
                {
                    pictureBox1.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
                    pictureBox1.Refresh();
                }
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

        private void gridLightDarkToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (gridLightDarkToolStripMenuItem.Checked)
                pictureBox1.BackgroundImage = Properties.Resources.grid_dark;
            else
                pictureBox1.BackgroundImage = Properties.Resources.grid_light;
        }
    }
}

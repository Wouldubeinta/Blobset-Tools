using PackageIO;
using System.ComponentModel.Design;

namespace Blobset_Tools
{
    public partial class Hex_Viewer : Form
    {
        private readonly string filePath;
        private readonly string filename;
        private readonly int size;
        private readonly uint offset;
        public Hex_Viewer(string _filePath, string _filename, int _size, uint _offset)
        {
            InitializeComponent();
            filePath = _filePath;
            filename = _filename;
            size = _size;
            offset = _offset;

            Text = "Hex Viewer - " + filename;
        }

        private void Hex_Viewer_Load(object sender, EventArgs e)
        {
            Reader? br = null;

            try
            {
                br = new(filePath);
                br.Position = offset;
                ByteViewer bv = new();
                bv.SetBytes(br.ReadBytes(size, Endian.Little)); // or SetBytes
                bv.ForeColor = Color.DarkBlue;
                bv.BackColor = Color.LightGray;
                bv.Dock = DockStyle.Fill;
                Controls.Add(bv);
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
    }
}

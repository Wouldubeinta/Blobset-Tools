using BlobsetIO;
using PackageIO;
using System.Data;

namespace Blobset_Tools
{
    public partial class File_Mapping_Editor : Form
    {
        public File_Mapping_Editor()
        {
            InitializeComponent();
        }

        private string fileMappingFile = string.Empty;

        private void File_Mapping_Editor_Load(object sender, EventArgs e)
        {
            DataTable? dt = null;
            Reader? br = null;
            FileMapping? fileMapping = new FileMapping();

            try
            {
                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];
                string basePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt);
                string dataFolder = Path.Combine(basePath, "data");
                fileMappingFile = dataFolder + @"\data-0.blobset." + platformExt + ".mapping";

                br = new(fileMappingFile, Endian.Little);
                fileMapping.Read(br);

                if (fileMapping == null)
                    return;

                dt = new DataTable();

                dt.Columns.Add("Index", typeof(int));
                dt.Columns.Add("FilePath", typeof(string));
                dt.Columns.Add("FolderHash", typeof(string));
                dt.Columns.Add("FileNameHash", typeof(string));

                for (int i = 0; i < fileMapping.FilesCount; i++)
                {
                    dt.Rows.Add();
                    dt.Rows[dt.Rows.Count - 1]["Index"] = fileMapping.Entries[i].Index;
                    dt.Rows[dt.Rows.Count - 1]["FilePath"] = fileMapping.Entries[i].FilePath;
                    dt.Rows[dt.Rows.Count - 1]["FolderHash"] = fileMapping.Entries[i].FolderHash;
                    dt.Rows[dt.Rows.Count - 1]["FileNameHash"] = fileMapping.Entries[i].FileNameHash;
                }

                dataGridView1.DataSource = dt;

                dataGridView1.Columns[0].Width = 70;
                dataGridView1.Columns[1].Width = 500;
                dataGridView1.Columns[2].Width = 90;
                dataGridView1.Columns[3].Width = 110;
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : {ex.Message}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null)
                    br.Close();
            }
        }

        private void Save_toolStripSplitButton_ButtonClick(object sender, EventArgs e)
        {
            if (fileMappingFile == null)
                return;

            dataGridView1.Rows[0].Cells[0].Selected = true;
            Writer? bw = null;

            try
            {
                bw = new Writer(fileMappingFile, Endian.Little);

                bw.Position = 0;
                bw.WriteInt32(dataGridView1.RowCount, Endian.Little); //Count

                for (int i = 0; i < dataGridView1.RowCount; i++)
                {
                    bw.WriteInt32(Convert.ToInt32(dataGridView1.Rows[i].Cells[0].Value), Endian.Little); // Index
                    bw.WriteUInt8(Convert.ToByte(dataGridView1.Rows[i].Cells[1].Value.ToString().Length), Endian.Little); // Path Length
                    bw.WriteString(dataGridView1.Rows[i].Cells[1].Value.ToString()); // Path
                    bw.WriteUInt8(Convert.ToByte(dataGridView1.Rows[i].Cells[2].Value.ToString().Length), Endian.Little); // FolderHash Length
                    bw.WriteString(dataGridView1.Rows[i].Cells[2].Value.ToString()); // FolderHash
                    bw.WriteUInt8(Convert.ToByte(dataGridView1.Rows[i].Cells[3].Value.ToString().Length), Endian.Little); // FileHash Length
                    bw.WriteString(dataGridView1.Rows[i].Cells[3].Value.ToString()); // FileHash
                    bw.Flush();
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : {ex.Message}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (bw != null)
                    bw.Close();

                MessageBox.Show("Changers have been saved to the File Mapping", "Save Changers Is Complete :)", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
        }

        private void searchToolStripMenuItem_Click(object sender, EventArgs e)
        {
            Search();
        }

        private void Search()
        {
            if (string.IsNullOrWhiteSpace(Search_toolStripTextBox.Text)) // Handles empty or whitespace-only input
            {
                return;
            }

            try
            {
                bool found = false;

                // Determine the starting index for the search
                int startIndex = 0;
                if (dataGridView1.CurrentCell != null) // Check if a cell is selected
                {
                    startIndex = dataGridView1.CurrentCell.RowIndex + 1;
                    if (startIndex >= dataGridView1.Rows.Count)
                    {
                        startIndex = 0; // Wrap around to the beginning
                    }
                }

                // Iterate through the rows
                for (int i = startIndex; i < dataGridView1.Rows.Count; i++)
                {
                    DataGridViewRow row = dataGridView1.Rows[i]; // Cache the row for efficiency

                    // Build the path string
                    string path = $"{row.Cells[1].Value?.ToString() ?? string.Empty}"; // Null-conditional operator and null-coalescing operator

                    // Perform the search (case-insensitive)
                    if (path.Contains(Search_toolStripTextBox.Text, StringComparison.OrdinalIgnoreCase)) // Case-insensitive comparison
                    {
                        // Found a match!
                        dataGridView1.Focus();
                        row.Visible = true; // Ensure the row is visible (if filtering is in place)
                        row.Selected = true;
                        dataGridView1.CurrentCell = row.Cells[0];
                        dataGridView1.Refresh(); // Consider if Refresh() is truly needed here.
                        found = true;
                        break; // Exit the loop once a match is found
                    }
                }

                // Handle the case where no match was found
                if (!found)
                {
                    dataGridView1.Focus();
                    if (dataGridView1.Rows.Count > 0) // Check if there are any rows to select
                    {
                        dataGridView1.Rows[0].Visible = true;
                        dataGridView1.Rows[0].Selected = true;
                        dataGridView1.CurrentCell = dataGridView1.Rows[0].Cells[0];
                    }
                    dataGridView1.Refresh(); // Consider if Refresh() is truly needed here.
                    MessageBox.Show("Could not find player name.", "No Results Found", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy : {ex}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }

        private void Search_toolStripTextBox_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.KeyValue == (char)Keys.Enter)
            {
                Search();
            }
        }
    }
}

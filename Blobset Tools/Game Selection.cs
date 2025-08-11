namespace Blobset_Tools
{
    public partial class GameSelection : Form
    {
        public GameSelection()
        {
            InitializeComponent();
        }

        private void affl_button_Click(object sender, EventArgs e)
        {
            //MessageBox.Show("Not Implemented yet!!!", "AFL Live", MessageBoxButtons.OK, MessageBoxIcon.Information);

            Properties.Settings.Default.GameID = (int)Enums.Game.AFLL;
            Properties.Settings.Default.GameName = "AFL Live";
            Properties.Settings.Default.SteamGameID = 0;
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v1;

            if (File.Exists(@"C:\Program Files (x86)\Tru Blu Games\AFL Live\data-0.blobset.pc"))
                Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Tru Blu Games\AFL Live\data-0.blobset.pc";
            else
            {
                if (ofd.ShowDialog() == DialogResult.OK)
                    Properties.Settings.Default.GameLocation = ofd.FileName;
                else
                    Application.Exit();
                ofd.Dispose();
            }

            Properties.Settings.Default.Save();

            HideAndCloseForm();
        }

        private void rll2_button_Click(object sender, EventArgs e)
        {
            MessageBox.Show("Not Implemented yet!!!", "Rugby League Live 2 PS3", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            Properties.Settings.Default.GameID = (int)Enums.Game.RLL2;
            Properties.Settings.Default.GameName = "Rugby League Live 2 PS3";

            if (File.Exists(@"D:\data-0.blobset.pc"))
                Properties.Settings.Default.GameLocation = @"D:\data-0.blobset.pc";

            Properties.Settings.Default.SteamGameID = 0;
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v1;

            if (ofd.ShowDialog() == DialogResult.OK)
                Properties.Settings.Default.GameLocation = ofd.FileName;
            else
                Application.Exit();

            ofd.Dispose();
            Properties.Settings.Default.Save();
            HideAndCloseForm();
            */
        }

        private void dbc14_button_Click(object sender, EventArgs e)
        {
            MessageBox.Show("Not Implemented yet!!!", "Don Bradman Cricket 14", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            Properties.Settings.Default.GameID = (int)Enums.Game.DBC14;
            Properties.Settings.Default.GameName = "Don Bradman Cricket 14";
            Properties.Settings.Default.SteamGameID = 216260;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Don Bradman Cricket 14\data-0.blobset.pc"
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v2;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data-0.blobset.pc");
            }

            HideAndCloseForm();
            */
        }

        private void rll3_button_Click(object sender, EventArgs e)
        {
            MessageBox.Show("Not Implemented yet!!!", "Rugby League 3", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            Properties.Settings.Default.GameID = (int)Enums.Game.RLL3;
            Properties.Settings.Default.GameName = "Rugby League 3";
            Properties.Settings.Default.SteamGameID = 312920;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Rugby League 3\data-0.blobset.pc"
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v2;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data-0.blobset.pc");
            }

            HideAndCloseForm();
            */
        }

        private void ac_button_Click(object sender, EventArgs e)
        {
            MessageBox.Show("Not Implemented yet!!!", "Ashes Cricket", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            Properties.Settings.Default.GameID = (int)Enums.Game.AC;
            Properties.Settings.Default.GameName = "Ashes Cricket";
            Properties.Settings.Default.SteamGameID = 649640;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Ashes Cricket\data-0.blobset.pc"
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v2;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data-0.blobset.pc");
            }

            HideAndCloseForm();
            */
        }

        private void rll4_button_Click(object sender, EventArgs e)
        {
            MessageBox.Show("Not Implemented yet!!!", "Rugby League Live 4", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            Properties.Settings.Default.GameID = (int)Enums.Game.RLL4;
            Properties.Settings.Default.GameName = "Rugby League Live 4";
            Properties.Settings.Default.SteamGameID = 556480;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Rugby League Live 4\data-0.blobset.pc"
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v2;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data-0.blobset.pc");
            }

            HideAndCloseForm();
            */
        }

        private void aoit_button_Click(object sender, EventArgs e)
        {
            MessageBox.Show("Not Implemented yet!!!", "AO International Tennis", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            Properties.Settings.Default.GameID = (int)Enums.Game.AOIT;
            Properties.Settings.Default.GameName = "AO International Tennis";
            Properties.Settings.Default.SteamGameID = 758410;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\AO International Tennis\data\data-0.blobset.pc"
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v3;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
            */
        }

        private void c19_button_Click(object sender, EventArgs e)
        {
            MessageBox.Show("Not Implemented yet!!!", "Cricket 19", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            Properties.Settings.Default.GameID = (int)Enums.Game.C19;
            Properties.Settings.Default.GameName = "Cricket 19";
            Properties.Settings.Default.SteamGameID = 1028630;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Cricket 19\data\data-0.blobset.pc";
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v3;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
            */
        }

        private void ao2_button_Click(object sender, EventArgs e)
        {
            Properties.Settings.Default.GameID = (int)Enums.Game.AOT2;
            Properties.Settings.Default.GameName = "AO Tennis 2";
            Properties.Settings.Default.SteamGameID = 1072500;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\AO Tennis 2\data\data-0.blobset.pc";
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v4;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
        }

        private void c22_button_Click(object sender, EventArgs e)
        {
            Properties.Settings.Default.GameID = (int)Enums.Game.C22;
            Properties.Settings.Default.GameName = "Cricket 22";
            Properties.Settings.Default.SteamGameID = 1701380;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Cricket 22\data\data-0.blobset.pc";
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v4;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
        }

        private void afl23_button_Click(object sender, EventArgs e)
        {
            Properties.Settings.Default.GameID = (int)Enums.Game.AFL23;
            Properties.Settings.Default.GameName = "AFL 23";
            Properties.Settings.Default.SteamGameID = 2337630;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\AFL 23\data\data-0.blobset.pc";
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v4;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
        }

        private void c24_button_Click(object sender, EventArgs e)
        {
            Properties.Settings.Default.GameID = (int)Enums.Game.C24;
            Properties.Settings.Default.GameName = "Cricket 24";
            Properties.Settings.Default.SteamGameID = 2358260;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Cricket 24\data\data-0.blobset.pc";
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v4;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
        }

        private void tb_button_Click(object sender, EventArgs e)
        {
            Properties.Settings.Default.GameID = (int)Enums.Game.TB;
            Properties.Settings.Default.GameName = "Tiebreak";
            Properties.Settings.Default.SteamGameID = 2264340;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Tiebreak\data\data-0.blobset.pc";
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v4;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
        }

        private void r25_Click(object sender, EventArgs e)
        {
            Properties.Settings.Default.GameID = (int)Enums.Game.R25;
            Properties.Settings.Default.GameName = "Rugby 25";
            Properties.Settings.Default.SteamGameID = 2340870;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Rugby 25\data\data-0.blobset.pc";
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v4;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
        }

        private void afl26_button_Click(object sender, EventArgs e)
        {
            Properties.Settings.Default.GameID = (int)Enums.Game.AFL26;
            Properties.Settings.Default.GameName = "AFL 26";
            Properties.Settings.Default.SteamGameID = 3468640;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\AFL 26\data\data-0.blobset.pc";
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v4;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(Properties.Settings.Default.GameName + @"\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
        }

        private void rl26_button_Click(object sender, EventArgs e)
        {
            Properties.Settings.Default.GameID = (int)Enums.Game.RL26;
            Properties.Settings.Default.GameName = "Rugby League 26";
            Properties.Settings.Default.SteamGameID = 3468660;
            Properties.Settings.Default.GameLocation = @"C:\Program Files (x86)\Steam\steamapps\common\Rugby League\data\data-0.blobset.pc";
            Properties.Settings.Default.BlobsetVersion = (int)Enums.BlobsetVersion.v4;
            Properties.Settings.Default.Save();

            if (!File.Exists(Properties.Settings.Default.GameLocation))
            {
                GetGameLocation(@"Rugby League\data\data-0.blobset.pc");
            }

            HideAndCloseForm();
        }

        private void GetGameLocation(string game)
        {
            string steamLocation = UI.getSteamLocation();

            if (steamLocation != string.Empty)
            {
                string gameLocation = steamLocation + @"\steamapps\common\" + game;

                if (File.Exists(gameLocation))
                {
                    Properties.Settings.Default.GameLocation = gameLocation;
                    Properties.Settings.Default.Save();
                }
                else
                {
                    ofd.InitialDirectory = steamLocation + @"\steamapps\common";

                    if (ofd.ShowDialog() == DialogResult.OK)
                    {
                        Properties.Settings.Default.GameLocation = ofd.FileName;
                        Properties.Settings.Default.Save();
                    }
                    else
                    {
                        Application.Exit();
                    }
                    ofd.Dispose();
                }
            }
            else
            {
                if (ofd.ShowDialog() == DialogResult.OK)
                {
                    Properties.Settings.Default.GameLocation = ofd.FileName;
                    Properties.Settings.Default.Save();
                }
                else
                {
                    Application.Exit();
                }
                ofd.Dispose();
            }
        }

        private void HideAndCloseForm()
        {
            Hide();
            MainForm mainForm = new();
            mainForm.ShowDialog();
            Close();
        }
    }
}

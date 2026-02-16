using Blobset_Tools.Properties;
using BlobsetIO;
using System.Reflection;

namespace Blobset_Tools
{
    public partial class GameSelection : Form
    {
        public GameSelection()
        {
            InitializeComponent();
        }

        private IniFile? settingsIni = null;
        private int platform = 0;
        private int defaultGame = 0;

        private void GameSelection_Load(object sender, EventArgs e)
        {
            try 
            {
                Global.currentPath = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
                GameSelection_ComboBox.Items.AddRange(GameList);
                settingsIni = new IniFile(Path.Combine(Global.currentPath, "Settings.ini"));
                defaultGame = Convert.ToInt32(settingsIni.Read("DefaultGame", "Settings"));
                GameSelection_ComboBox.SelectedIndex = defaultGame;
                platform = Convert.ToInt32(settingsIni.Read("Platform", "Settings"));
                Platform_ComboBox.SelectedIndex = 0;
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }

        private void load_button_Click(object sender, EventArgs e)
        {
            ofd.Filter = BlobsetPlatformExt();
            settingsIni.Write("Platform", Platform_ComboBox.SelectedIndex.ToString(), "Settings");
            settingsIni.Write("DefaultGame", GameSelection_ComboBox.SelectedIndex.ToString(), "Settings");
            Global.platforms = GetPlatforms();
            Global.isBigendian = Global.platforms > Enums.Platforms.Windows;

            switch (GameSelection_ComboBox.SelectedIndex)
            {
                case 0:
                    AFFL();
                    break;
                case 1:
                    RLL2();
                    break;
                case 2:
                    RLL2WCE();
                    break;
                case 3:
                    DBC14();
                    break;
                case 4:
                    RLL3();
                    break;
                case 5:
                    TCC();
                    break;
                case 6:
                    CPL16();
                    break;
                case 7:
                    DB17();
                    break;
                case 8:
                    MTBOD();
                    break;
                case 9:
                    AC();
                    break;
                case 10:
                    RLL4();
                    break;
                case 11:
                    AOIT();
                    break;
                case 12:
                    CPL18();
                    break;
                case 13:
                    C19();
                    break;
                case 14:
                    AOT2();
                    break;
                case 15:
                    TWT2();
                    break;
                case 16:
                    C22();
                    break;
                case 17:
                    AFL23();
                    break;
                case 18:
                    C24();
                    break;
                case 19:
                    TB();
                    break;
                case 20:
                    R25();
                    break;
                case 21:
                    AFL26();
                    break;
                case 22:
                    RL26();
                    break;
                case 23:
                    C26();
                    break;
            }
        }

        private void AFFL()
        {
            string iniPath = GameInfo("AFL Live");

            if (!File.Exists(Global.gameInfo.GameLocation))
            {
                if (ofd.ShowDialog() == DialogResult.OK)
                {
                    Global.gameInfo.GameLocation = ofd.FileName;
                    IniFile gameInfoIni = new(iniPath);
                    gameInfoIni.Write("GameLocation", ofd.FileName, "GameInfo");
                }
                else
                {
                    ofd.Dispose();
                    return;
                }

                ofd.Dispose();
            }

            HideAndCloseForm();
        }

        private void RLL2()
        {
            string iniPath = GameInfo("Rugby League Live 2");

            if (!File.Exists(Global.gameInfo.GameLocation))
            {
                if (ofd.ShowDialog() == DialogResult.OK)
                {
                    Global.gameInfo.GameLocation = ofd.FileName;
                    IniFile gameInfoIni = new(iniPath);
                    gameInfoIni.Write("GameLocation", ofd.FileName, "GameInfo");
                }
                else
                {
                    ofd.Dispose();
                    return;
                }

                ofd.Dispose();
            }

            HideAndCloseForm();
        }

        private void RLL2WCE()
        {
            string iniPath = GameInfo("Rugby League Live 2 - World Cup Edition");

            if (!File.Exists(Global.gameInfo.GameLocation))
            {
                if (ofd.ShowDialog() == DialogResult.OK)
                {
                    Global.gameInfo.GameLocation = ofd.FileName;
                    IniFile gameInfoIni = new(iniPath);
                    gameInfoIni.Write("GameLocation", ofd.FileName, "GameInfo");
                }
                else
                {
                    ofd.Dispose();
                    return;
                }

                ofd.Dispose();
            }

            HideAndCloseForm();
        }

        private void DBC14()
        {
            string iniPath = GameInfo("Don Bradman Cricket 14");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void RLL3()
        {
            string iniPath = GameInfo("Rugby League Live 3");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void TCC()
        {
            string iniPath = GameInfo("TableTop Cricket");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void CPL16()
        {
            string iniPath = GameInfo("Casey Powell Lacrosse 16");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void MTBOD()
        {
            string iniPath = GameInfo("Masquerade - The Baubles of Doom");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void DB17()
        {
            string iniPath = GameInfo("Don Bradman Cricket 17");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void AC()
        {
            MessageBox.Show("Not Implemented yet!!!", "Ashes Cricket", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            string iniPath = GameInfo("Ashes Cricket");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
            */
        }

        private void RLL4()
        {
            MessageBox.Show("Not Implemented yet!!!", "Rugby League Live 4", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            string iniPath = GameInfo("Rugby League Live 4");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
            */
        }

        private void AOIT()
        {
            MessageBox.Show("Not Implemented yet!!!", "AO International Tennis", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            string iniPath = GameInfo("AO International Tennis");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
            */
        }

        private void CPL18()
        {
            MessageBox.Show("Not Implemented yet!!!", "Casey Powell Lacrosse 18", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            string iniPath = GameInfo("Casey Powell Lacrosse 18");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
            */
        }

        private void C19()
        {
            MessageBox.Show("Not Implemented yet!!!", "Cricket 19", MessageBoxButtons.OK, MessageBoxIcon.Information);

            /*
            string iniPath = GameInfo("Cricket 19");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
            */
        }

        private void AOT2()
        {
            string iniPath = GameInfo("AO Tennis 2");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void TWT2()
        {
            string iniPath = GameInfo("Tennis World Tour 2");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void C22()
        {
            string iniPath = GameInfo("Cricket 22");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void AFL23()
        {
            string iniPath = GameInfo("AFL 23");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void C24()
        {
            string iniPath = GameInfo("Cricket 24");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void TB()
        {
            string iniPath = GameInfo("Tiebreak");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void R25()
        {
            string iniPath = GameInfo("Rugby 25");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void AFL26()
        {
            string iniPath = GameInfo("AFL 26");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void RL26()
        {
            string iniPath = GameInfo("Rugby League 26");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine("Rugby League", "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private void C26()
        {
            string iniPath = GameInfo("Cricket 26");
            bool error = false;

            if (!File.Exists(Global.gameInfo.GameLocation))
                error = GetGameLocation(Path.Combine(Global.gameInfo.GameName, "data", "data-0.blobset.pc"), iniPath);

            if (error)
                return;

            HideAndCloseForm();
        }

        private bool GetGameLocation(string game, string iniPath)
        {
            if (!File.Exists(iniPath)) 
                throw new Exception("GameInfo.ini not found");

            string steamLocation = @"C:\Program Files (x86)\Steam";
            IniFile iniFile = new(iniPath);

            try 
            {
                steamLocation = UI.getSteamLocation();
                string gameLocation = Path.Combine(steamLocation, "steamapps", "common", game);

                // Check if the Steam location is valid
                if (!string.IsNullOrEmpty(steamLocation) && File.Exists(gameLocation))
                {
                    SetGameLocation(gameLocation, iniFile);
                    return false; // Game location found
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }

            // If the game location is not found, prompt the user to select a file
            return PromptUserForGameLocation(iniFile, steamLocation);
        }

        private void SetGameLocation(string location, IniFile iniFile)
        {
            Global.gameInfo.GameLocation = location;
            iniFile.Write("GameLocation", location, "GameInfo");
        }

        private bool PromptUserForGameLocation(IniFile iniFile, string steamLocation)
        {
            if (!string.IsNullOrEmpty(steamLocation))
                ofd.InitialDirectory = Path.Combine(steamLocation, "steamapps", "common");

            if (ofd.ShowDialog() == DialogResult.OK)
            {
                SetGameLocation(ofd.FileName, iniFile);
                return false; // User selected a game location
            }
            ofd.Dispose();
            return true; // User canceled the dialog
        }

        private void GameSelection_comboBox_SelectedIndexChanged(object sender, EventArgs e)
        {
            switch (GameSelection_ComboBox.SelectedIndex)
            {
                case 0:
                    load_button.BackgroundImage = Resources.AFLL;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.AddRange(AllPlatforms);
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 1:
                    load_button.BackgroundImage = Resources.RLL2;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.AddRange(ConsolePlatforms);
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 2:
                    load_button.BackgroundImage = Resources.RLL2_WCE;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.AddRange(ConsolePlatforms);
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 3:
                    load_button.BackgroundImage = Resources.Don_Bradman_Cricket_14;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.AddRange(AllPlatforms);
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 4:
                    load_button.BackgroundImage = Resources.RLL3;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.AddRange(AllPlatforms);
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 5:
                    load_button.BackgroundImage = Resources.TTC;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.AddRange(AllPlatforms);
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 6:
                    load_button.BackgroundImage = Resources.CPL16;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 7:
                    load_button.BackgroundImage = Resources.Don_Bradman_Cricket_17;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 8:
                    load_button.BackgroundImage = Resources.MTBOD;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.AddRange(AllPlatforms);
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 9:
                    load_button.BackgroundImage = Resources.Ashes_Cricket;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 10:
                    load_button.BackgroundImage = Resources.RLL4;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 11:
                    load_button.BackgroundImage = Resources.AO_International_Tennis;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 12:
                    load_button.BackgroundImage = Resources.CPL18;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 13:
                    load_button.BackgroundImage = Resources.Cricket_19;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 14:
                    load_button.BackgroundImage = Resources.AO_Tennis_2;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 15:
                    load_button.BackgroundImage = Resources.TWT2;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 16:
                    load_button.BackgroundImage = Resources.Cricket_22;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 17:
                    load_button.BackgroundImage = Resources.AFL23;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 18:
                    load_button.BackgroundImage = Resources.Cricket_24;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 19:
                    load_button.BackgroundImage = Resources.Tiebreak;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 20:
                    load_button.BackgroundImage = Resources.Rugby_25;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 21:
                    load_button.BackgroundImage = Resources.AFL26;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 22:
                    load_button.BackgroundImage = Resources.RL26;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
                case 23:
                    load_button.BackgroundImage = Resources.Cricket_26;
                    Platform_ComboBox.Items.Clear();
                    Platform_ComboBox.Items.Add("Windows");
                    Platform_ComboBox.SelectedIndex = 0;
                    break;
            }
        }

        private string GameInfo(string gameName)
        {
            string iniPath = string.Empty;

            try 
            {
                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];

                GameInfo gameInfo = new();
                iniPath = Path.Combine(Global.currentPath, "games", gameName, platformExt, "GameInfo.ini");
                gameInfo.Deserialize(iniPath);
                Global.gameInfo = gameInfo;
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            return iniPath;
        }

        private void Platform_ComboBox_SelectedIndexChanged(object sender, EventArgs e)
        {
            switch (Platform_ComboBox.Items[Platform_ComboBox.SelectedIndex].ToString())
            {
                case "Windows":
                    pictureBox1.Image = Resources.Windows;
                    break;
                case "Playstation 3":
                    pictureBox1.Image = Resources.PS3;
                    break;
                case "Xbox 360":
                    pictureBox1.Image = Resources.Xbox_360;
                    break;
            }
        }

        private void Platform_ComboBox_DrawItem(object sender, DrawItemEventArgs e)
        {
            var items = Platform_ComboBox.Items;

            string[] platforms = new string[items.Count];

            int i = 0;

            foreach (var item in items)
            {
                platforms[i] = item.ToString();
                i++;
            }

            DrawItem(platforms, platform, e);
        }

        private void GameSelection_comboBox_DrawItem(object sender, DrawItemEventArgs e)
        {
            DrawItem(GameList, defaultGame, e);
        }

        private void DrawItem(string[] list, int index, DrawItemEventArgs e)
        {
            Color backgroundColor = (e.State & DrawItemState.Selected) == DrawItemState.Selected
                ? Color.Gold// Or any other color you prefer for selected item
                : Color.Crimson; // Default background color

            // Draw the background
            using (SolidBrush backgroundBrush = new(backgroundColor))
            {
                e.Graphics.FillRectangle(backgroundBrush, e.Bounds);
            }

            // Draw each string in the array, using a different size, color,
            // and font for each item.
            Font myFont = new("RLFont", 14F, FontStyle.Regular, GraphicsUnit.Point, 0);
            e.Graphics.DrawString(e.Index == -1 ? list[index] : list[e.Index], myFont, Brushes.Black, new RectangleF(e.Bounds.X, e.Bounds.Y + 2, e.Bounds.Width, e.Bounds.Height));
        }

        private readonly string[] GameList =
        {
            "AFL Live (PC)(PS3)(360)",
            "Rugby League Live 2 (PS3)(360)",
            "Rugby League Live 2 - World Cup Edition (PS3)(360)",
            "Don Bradman Cricket 14 (PC)(PS3)(360)",
            "Rugby League Live 3 (PC)(PS3)(360)",
            "TableTop Cricket (PC)(PS3)(360)",
            "Casey Powell Lacrosse 16 (PC)",
            "Don Bradman Cricket 17 (PC)",
            "Masquerade - The Baubles of Doom (PC)(PS3)(360)",
            "Ashes Cricket (PC)",
            "Rugby League Live 4 (PC)",
            "AO International Tennis (PC)",
            "Casey Powell Lacrosse 18 (PC)",
            "Cricket 19 (PC)",
            "AO Tennis 2 (PC)",
            "Tennis World Tour 2 (PC)",
            "Cricket 22 (PC)",
            "AFL 23 (PC)",
            "Cricket 24 (PC)",
            "Tiebreak (PC)",
            "Rugby 25 (PC)",
            "AFL 26 (PC)",
            "Rugby League 26 (PC)",
            "Cricket 26 (PC)"
        };

        private readonly string[] AllPlatforms =
        {
            "Windows",
            "Playstation 3",
            "Xbox 360"
        };

        private readonly string[] ConsolePlatforms = { "Playstation 3", "Xbox 360" };

        private string BlobsetPlatformExt()
        {
            string ext = "Blobset File | *.pc";
            string platform = Platform_ComboBox.Items[Platform_ComboBox.SelectedIndex].ToString();

            switch (platform)
            {
                case "Playstation 3":
                    ext = "Blobset File | *.ps3";
                    break;
                case "Xbox 360":
                    ext = "Blobset File | *.xbox360";
                    break;
                default:
                    ext = "Blobset File | *.pc";
                    break;
            }
            return ext;
        }

        private Enums.Platforms GetPlatforms()
        {
            Enums.Platforms platforms = Enums.Platforms.Windows;
            string platform = "Windows";

            if (Platform_ComboBox.Items != null && Platform_ComboBox.SelectedIndex != -1)
                platform = Platform_ComboBox.Items[Platform_ComboBox.SelectedIndex].ToString();

            switch (platform)
            {
                case "Playstation 3":
                    platforms = Enums.Platforms.PS3;
                    break;
                case "Xbox 360":
                    platforms = Enums.Platforms.Xbox360;
                    break;
                default:
                    platforms = Enums.Platforms.Windows;
                    break;
            }
            return platforms;
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

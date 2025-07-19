namespace Blobset_Tools
{
    partial class MainForm
    {
        /// <summary>
        ///  Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        ///  Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        ///  Required method for Designer support - do not modify
        ///  the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            components = new System.ComponentModel.Container();
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(MainForm));
            statusStrip1 = new StatusStrip();
            status_Label = new ToolStripStatusLabel();
            progressStripStatusLabel = new ToolStripStatusLabel();
            toolStripProgressBar = new ToolStripProgressBar();
            menuStrip1 = new MenuStrip();
            fileToolStripMenuItem = new ToolStripMenuItem();
            exitToolStripMenuItem = new ToolStripMenuItem();
            extractBlobsetToolStripMenuItem = new ToolStripMenuItem();
            createBlobsetToolStripMenuItem = new ToolStripMenuItem();
            gameName_toolStripTextBox = new ToolStripTextBox();
            toolsToolStripMenuItem = new ToolStripMenuItem();
            tXPKCreatorToolStripMenuItem = new ToolStripMenuItem();
            m3MPCreatorToolStripMenuItem = new ToolStripMenuItem();
            optionsToolStripMenuItem = new ToolStripMenuItem();
            blobsetCompressionToolStripMenuItem = new ToolStripMenuItem();
            updateFileMappingDataToolStripMenuItem = new ToolStripMenuItem();
            loadGameToolStripMenuItem = new ToolStripMenuItem();
            validateSteamGameFilesToolStripMenuItem = new ToolStripMenuItem();
            skipUnknownFilesToolStripMenuItem = new ToolStripMenuItem();
            aboutToolStripMenuItem = new ToolStripMenuItem();
            ofd = new OpenFileDialog();
            splitContainer1 = new SplitContainer();
            splitContainer2 = new SplitContainer();
            folder_treeView = new TreeView();
            files_listView = new ListView();
            splitContainer3 = new SplitContainer();
            fileInfo_richTextBox = new RichTextBox();
            dds_pictureBox = new PictureBox();
            extractImage_contextMenuStrip = new ContextMenuStrip(components);
            extractToolStripMenuItem = new ToolStripMenuItem();
            dDSFileToolStripMenuItem = new ToolStripMenuItem();
            pNGFileToolStripMenuItem = new ToolStripMenuItem();
            saveFileDialog = new SaveFileDialog();
            extractFile_contextMenuStrip = new ContextMenuStrip(components);
            extractToolStripMenuItem1 = new ToolStripMenuItem();
            SaveLog_contextMenuStrip = new ContextMenuStrip(components);
            saveLogTotxtToolStripMenuItem = new ToolStripMenuItem();
            flipImage_contextMenuStrip = new ContextMenuStrip(components);
            flipImageToolStripMenuItem = new ToolStripMenuItem();
            statusStrip1.SuspendLayout();
            menuStrip1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)splitContainer1).BeginInit();
            splitContainer1.Panel1.SuspendLayout();
            splitContainer1.Panel2.SuspendLayout();
            splitContainer1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)splitContainer2).BeginInit();
            splitContainer2.Panel1.SuspendLayout();
            splitContainer2.Panel2.SuspendLayout();
            splitContainer2.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)splitContainer3).BeginInit();
            splitContainer3.Panel1.SuspendLayout();
            splitContainer3.Panel2.SuspendLayout();
            splitContainer3.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)dds_pictureBox).BeginInit();
            extractImage_contextMenuStrip.SuspendLayout();
            extractFile_contextMenuStrip.SuspendLayout();
            SaveLog_contextMenuStrip.SuspendLayout();
            flipImage_contextMenuStrip.SuspendLayout();
            SuspendLayout();
            // 
            // statusStrip1
            // 
            statusStrip1.ImageScalingSize = new Size(20, 20);
            statusStrip1.Items.AddRange(new ToolStripItem[] { status_Label, progressStripStatusLabel, toolStripProgressBar });
            statusStrip1.Location = new Point(0, 702);
            statusStrip1.Name = "statusStrip1";
            statusStrip1.Size = new Size(1271, 23);
            statusStrip1.TabIndex = 1;
            statusStrip1.Text = "statusStrip1";
            // 
            // status_Label
            // 
            status_Label.Name = "status_Label";
            status_Label.Size = new Size(963, 18);
            status_Label.Spring = true;
            status_Label.TextAlign = ContentAlignment.MiddleLeft;
            // 
            // progressStripStatusLabel
            // 
            progressStripStatusLabel.Name = "progressStripStatusLabel";
            progressStripStatusLabel.Size = new Size(0, 18);
            // 
            // toolStripProgressBar
            // 
            toolStripProgressBar.Name = "toolStripProgressBar";
            toolStripProgressBar.Size = new Size(291, 17);
            // 
            // menuStrip1
            // 
            menuStrip1.ImageScalingSize = new Size(20, 20);
            menuStrip1.Items.AddRange(new ToolStripItem[] { fileToolStripMenuItem, extractBlobsetToolStripMenuItem, createBlobsetToolStripMenuItem, gameName_toolStripTextBox, toolsToolStripMenuItem, optionsToolStripMenuItem, aboutToolStripMenuItem });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Size = new Size(1271, 25);
            menuStrip1.TabIndex = 2;
            menuStrip1.Text = "menuStrip1";
            // 
            // fileToolStripMenuItem
            // 
            fileToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { exitToolStripMenuItem });
            fileToolStripMenuItem.Name = "fileToolStripMenuItem";
            fileToolStripMenuItem.Size = new Size(37, 21);
            fileToolStripMenuItem.Text = "File";
            // 
            // exitToolStripMenuItem
            // 
            exitToolStripMenuItem.Image = Properties.Resources.close_32;
            exitToolStripMenuItem.Name = "exitToolStripMenuItem";
            exitToolStripMenuItem.Size = new Size(92, 22);
            exitToolStripMenuItem.Text = "Exit";
            exitToolStripMenuItem.Click += exitToolStripMenuItem_Click;
            // 
            // extractBlobsetToolStripMenuItem
            // 
            extractBlobsetToolStripMenuItem.Name = "extractBlobsetToolStripMenuItem";
            extractBlobsetToolStripMenuItem.Size = new Size(96, 21);
            extractBlobsetToolStripMenuItem.Text = "Extract Blobset";
            extractBlobsetToolStripMenuItem.ToolTipText = "Extract all blobset files.";
            extractBlobsetToolStripMenuItem.Click += extractBlobsetToolStripMenuItem_Click;
            // 
            // createBlobsetToolStripMenuItem
            // 
            createBlobsetToolStripMenuItem.Name = "createBlobsetToolStripMenuItem";
            createBlobsetToolStripMenuItem.Size = new Size(99, 21);
            createBlobsetToolStripMenuItem.Text = "Modify Blobset";
            createBlobsetToolStripMenuItem.ToolTipText = "Modify blobset with mods.";
            createBlobsetToolStripMenuItem.Click += createBlobsetToolStripMenuItem_Click;
            // 
            // gameName_toolStripTextBox
            // 
            gameName_toolStripTextBox.Alignment = ToolStripItemAlignment.Right;
            gameName_toolStripTextBox.BackColor = SystemColors.Control;
            gameName_toolStripTextBox.Font = new Font("RLFont", 9F);
            gameName_toolStripTextBox.ForeColor = Color.Blue;
            gameName_toolStripTextBox.Name = "gameName_toolStripTextBox";
            gameName_toolStripTextBox.ReadOnly = true;
            gameName_toolStripTextBox.ShortcutsEnabled = false;
            gameName_toolStripTextBox.Size = new Size(391, 21);
            gameName_toolStripTextBox.Text = "* Rugby League 26 *";
            gameName_toolStripTextBox.TextBoxTextAlign = HorizontalAlignment.Center;
            // 
            // toolsToolStripMenuItem
            // 
            toolsToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { tXPKCreatorToolStripMenuItem, m3MPCreatorToolStripMenuItem });
            toolsToolStripMenuItem.Name = "toolsToolStripMenuItem";
            toolsToolStripMenuItem.Size = new Size(47, 21);
            toolsToolStripMenuItem.Text = "Tools";
            // 
            // tXPKCreatorToolStripMenuItem
            // 
            tXPKCreatorToolStripMenuItem.Image = (Image)resources.GetObject("tXPKCreatorToolStripMenuItem.Image");
            tXPKCreatorToolStripMenuItem.Name = "tXPKCreatorToolStripMenuItem";
            tXPKCreatorToolStripMenuItem.Size = new Size(151, 22);
            tXPKCreatorToolStripMenuItem.Text = "TXPK Creator";
            tXPKCreatorToolStripMenuItem.ToolTipText = "To create DDS TXPK files.";
            tXPKCreatorToolStripMenuItem.Click += txpkCreatorToolStripMenuItem_Click;
            // 
            // m3MPCreatorToolStripMenuItem
            // 
            m3MPCreatorToolStripMenuItem.Image = (Image)resources.GetObject("m3MPCreatorToolStripMenuItem.Image");
            m3MPCreatorToolStripMenuItem.Name = "m3MPCreatorToolStripMenuItem";
            m3MPCreatorToolStripMenuItem.Size = new Size(151, 22);
            m3MPCreatorToolStripMenuItem.Text = "M3MP Creator";
            m3MPCreatorToolStripMenuItem.ToolTipText = "To Create M3MP files.";
            m3MPCreatorToolStripMenuItem.Click += m3mpCreatorToolStripMenuItem_Click;
            // 
            // optionsToolStripMenuItem
            // 
            optionsToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { blobsetCompressionToolStripMenuItem, updateFileMappingDataToolStripMenuItem, loadGameToolStripMenuItem, validateSteamGameFilesToolStripMenuItem, skipUnknownFilesToolStripMenuItem });
            optionsToolStripMenuItem.Name = "optionsToolStripMenuItem";
            optionsToolStripMenuItem.Size = new Size(61, 21);
            optionsToolStripMenuItem.Text = "Options";
            // 
            // blobsetCompressionToolStripMenuItem
            // 
            blobsetCompressionToolStripMenuItem.Checked = true;
            blobsetCompressionToolStripMenuItem.CheckOnClick = true;
            blobsetCompressionToolStripMenuItem.CheckState = CheckState.Checked;
            blobsetCompressionToolStripMenuItem.Image = Properties.Resources.compression_32;
            blobsetCompressionToolStripMenuItem.Name = "blobsetCompressionToolStripMenuItem";
            blobsetCompressionToolStripMenuItem.Size = new Size(211, 22);
            blobsetCompressionToolStripMenuItem.Text = "Blobset Compression";
            blobsetCompressionToolStripMenuItem.ToolTipText = "If ticked, this will compress the blobset files.";
            blobsetCompressionToolStripMenuItem.Click += blobsetCompressionToolStripMenuItem_Click;
            // 
            // updateFileMappingDataToolStripMenuItem
            // 
            updateFileMappingDataToolStripMenuItem.Image = Properties.Resources.mapping_32;
            updateFileMappingDataToolStripMenuItem.Name = "updateFileMappingDataToolStripMenuItem";
            updateFileMappingDataToolStripMenuItem.Size = new Size(211, 22);
            updateFileMappingDataToolStripMenuItem.Text = "Update File Mapping Data";
            updateFileMappingDataToolStripMenuItem.ToolTipText = "This update's the file mapping files in the data folder.";
            updateFileMappingDataToolStripMenuItem.Click += updateFileMappingDataToolStripMenuItem_Click;
            // 
            // loadGameToolStripMenuItem
            // 
            loadGameToolStripMenuItem.CheckOnClick = true;
            loadGameToolStripMenuItem.Image = Properties.Resources.loading_32;
            loadGameToolStripMenuItem.Name = "loadGameToolStripMenuItem";
            loadGameToolStripMenuItem.Size = new Size(211, 22);
            loadGameToolStripMenuItem.Text = "Load Game";
            loadGameToolStripMenuItem.ToolTipText = "If ticked, this will load the game after creating the blobset.";
            loadGameToolStripMenuItem.Click += loadGameToolStripMenuItem_Click;
            // 
            // validateSteamGameFilesToolStripMenuItem
            // 
            validateSteamGameFilesToolStripMenuItem.Image = (Image)resources.GetObject("validateSteamGameFilesToolStripMenuItem.Image");
            validateSteamGameFilesToolStripMenuItem.Name = "validateSteamGameFilesToolStripMenuItem";
            validateSteamGameFilesToolStripMenuItem.Size = new Size(211, 22);
            validateSteamGameFilesToolStripMenuItem.Text = "Validate Steam Game Files";
            validateSteamGameFilesToolStripMenuItem.ToolTipText = "This will repair game files, If something stuffs up.";
            validateSteamGameFilesToolStripMenuItem.Click += validateSteamGameFilesToolStripMenuItem_Click;
            // 
            // skipUnknownFilesToolStripMenuItem
            // 
            skipUnknownFilesToolStripMenuItem.CheckOnClick = true;
            skipUnknownFilesToolStripMenuItem.Image = Properties.Resources.skip_32;
            skipUnknownFilesToolStripMenuItem.Name = "skipUnknownFilesToolStripMenuItem";
            skipUnknownFilesToolStripMenuItem.Size = new Size(211, 22);
            skipUnknownFilesToolStripMenuItem.Text = "Skip Unknown Files";
            skipUnknownFilesToolStripMenuItem.ToolTipText = "When ticked, this will skip extracting unknown file types, like meshes and animation files ect...";
            skipUnknownFilesToolStripMenuItem.Click += skipUnknownFilesToolStripMenuItem_Click;
            // 
            // aboutToolStripMenuItem
            // 
            aboutToolStripMenuItem.Name = "aboutToolStripMenuItem";
            aboutToolStripMenuItem.Size = new Size(52, 21);
            aboutToolStripMenuItem.Text = "About";
            aboutToolStripMenuItem.Click += aboutToolStripMenuItem_Click;
            // 
            // ofd
            // 
            ofd.DefaultExt = "pc";
            ofd.Filter = "Blobset File|*.pc";
            ofd.Title = "Select Blobset File";
            // 
            // splitContainer1
            // 
            splitContainer1.Dock = DockStyle.Fill;
            splitContainer1.Location = new Point(0, 25);
            splitContainer1.Name = "splitContainer1";
            // 
            // splitContainer1.Panel1
            // 
            splitContainer1.Panel1.Controls.Add(splitContainer2);
            // 
            // splitContainer1.Panel2
            // 
            splitContainer1.Panel2.Controls.Add(splitContainer3);
            splitContainer1.Size = new Size(1271, 677);
            splitContainer1.SplitterDistance = 744;
            splitContainer1.TabIndex = 3;
            // 
            // splitContainer2
            // 
            splitContainer2.Dock = DockStyle.Fill;
            splitContainer2.Location = new Point(0, 0);
            splitContainer2.Name = "splitContainer2";
            // 
            // splitContainer2.Panel1
            // 
            splitContainer2.Panel1.Controls.Add(folder_treeView);
            // 
            // splitContainer2.Panel2
            // 
            splitContainer2.Panel2.Controls.Add(files_listView);
            splitContainer2.Size = new Size(744, 677);
            splitContainer2.SplitterDistance = 233;
            splitContainer2.TabIndex = 0;
            // 
            // folder_treeView
            // 
            folder_treeView.BackColor = Color.WhiteSmoke;
            folder_treeView.Dock = DockStyle.Fill;
            folder_treeView.Font = new Font("Segoe UI", 10F);
            folder_treeView.ItemHeight = 22;
            folder_treeView.Location = new Point(0, 0);
            folder_treeView.Name = "folder_treeView";
            folder_treeView.Size = new Size(233, 677);
            folder_treeView.TabIndex = 0;
            folder_treeView.AfterSelect += folder_treeView_AfterSelect;
            // 
            // files_listView
            // 
            files_listView.Alignment = ListViewAlignment.Left;
            files_listView.BackColor = Color.WhiteSmoke;
            files_listView.Dock = DockStyle.Fill;
            files_listView.FullRowSelect = true;
            files_listView.HeaderStyle = ColumnHeaderStyle.None;
            files_listView.LabelWrap = false;
            files_listView.Location = new Point(0, 0);
            files_listView.MultiSelect = false;
            files_listView.Name = "files_listView";
            files_listView.ShowGroups = false;
            files_listView.Size = new Size(507, 677);
            files_listView.TabIndex = 0;
            files_listView.UseCompatibleStateImageBehavior = false;
            files_listView.View = View.List;
            files_listView.SelectedIndexChanged += files_listView_SelectedIndexChanged;
            files_listView.MouseClick += files_listView_MouseClick;
            files_listView.MouseDoubleClick += files_listView_MouseDoubleClick;
            // 
            // splitContainer3
            // 
            splitContainer3.Dock = DockStyle.Fill;
            splitContainer3.Location = new Point(0, 0);
            splitContainer3.Name = "splitContainer3";
            splitContainer3.Orientation = Orientation.Horizontal;
            // 
            // splitContainer3.Panel1
            // 
            splitContainer3.Panel1.Controls.Add(fileInfo_richTextBox);
            // 
            // splitContainer3.Panel2
            // 
            splitContainer3.Panel2.Controls.Add(dds_pictureBox);
            splitContainer3.Size = new Size(523, 677);
            splitContainer3.SplitterDistance = 281;
            splitContainer3.TabIndex = 0;
            // 
            // fileInfo_richTextBox
            // 
            fileInfo_richTextBox.BackColor = SystemColors.InfoText;
            fileInfo_richTextBox.Dock = DockStyle.Fill;
            fileInfo_richTextBox.Font = new Font("Segoe UI", 11F);
            fileInfo_richTextBox.ForeColor = Color.DodgerBlue;
            fileInfo_richTextBox.Location = new Point(0, 0);
            fileInfo_richTextBox.Name = "fileInfo_richTextBox";
            fileInfo_richTextBox.ReadOnly = true;
            fileInfo_richTextBox.Size = new Size(523, 281);
            fileInfo_richTextBox.TabIndex = 0;
            fileInfo_richTextBox.Text = "";
            fileInfo_richTextBox.MouseClick += fileInfo_richTextBox_MouseClick;
            fileInfo_richTextBox.SizeChanged += fileInfo_richTextBox_SizeChanged;
            // 
            // dds_pictureBox
            // 
            dds_pictureBox.Dock = DockStyle.Fill;
            dds_pictureBox.Location = new Point(0, 0);
            dds_pictureBox.Name = "dds_pictureBox";
            dds_pictureBox.Size = new Size(523, 392);
            dds_pictureBox.SizeMode = PictureBoxSizeMode.Zoom;
            dds_pictureBox.TabIndex = 0;
            dds_pictureBox.TabStop = false;
            dds_pictureBox.MouseClick += dds_pictureBox_MouseClick;
            // 
            // extractImage_contextMenuStrip
            // 
            extractImage_contextMenuStrip.Items.AddRange(new ToolStripItem[] { extractToolStripMenuItem });
            extractImage_contextMenuStrip.Name = "extract_contextMenuStrip";
            extractImage_contextMenuStrip.Size = new Size(110, 26);
            // 
            // extractToolStripMenuItem
            // 
            extractToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { dDSFileToolStripMenuItem, pNGFileToolStripMenuItem });
            extractToolStripMenuItem.Image = Properties.Resources.extract_32;
            extractToolStripMenuItem.Name = "extractToolStripMenuItem";
            extractToolStripMenuItem.Size = new Size(109, 22);
            extractToolStripMenuItem.Text = "Extract";
            // 
            // dDSFileToolStripMenuItem
            // 
            dDSFileToolStripMenuItem.Image = Properties.Resources.dds_32;
            dDSFileToolStripMenuItem.Name = "dDSFileToolStripMenuItem";
            dDSFileToolStripMenuItem.Size = new Size(119, 22);
            dDSFileToolStripMenuItem.Text = "DDS File";
            dDSFileToolStripMenuItem.Click += ddsFileToolStripMenuItem_Click;
            // 
            // pNGFileToolStripMenuItem
            // 
            pNGFileToolStripMenuItem.Image = Properties.Resources.png_32;
            pNGFileToolStripMenuItem.Name = "pNGFileToolStripMenuItem";
            pNGFileToolStripMenuItem.Size = new Size(119, 22);
            pNGFileToolStripMenuItem.Text = "PNG File";
            pNGFileToolStripMenuItem.Click += pngFileToolStripMenuItem_Click;
            // 
            // extractFile_contextMenuStrip
            // 
            extractFile_contextMenuStrip.Items.AddRange(new ToolStripItem[] { extractToolStripMenuItem1 });
            extractFile_contextMenuStrip.Name = "extractFile_contextMenuStrip";
            extractFile_contextMenuStrip.Size = new Size(110, 26);
            // 
            // extractToolStripMenuItem1
            // 
            extractToolStripMenuItem1.Image = Properties.Resources.extract_32;
            extractToolStripMenuItem1.Name = "extractToolStripMenuItem1";
            extractToolStripMenuItem1.Size = new Size(109, 22);
            extractToolStripMenuItem1.Text = "Extract";
            extractToolStripMenuItem1.Click += extractToolStripMenuItem1_Click;
            // 
            // SaveLog_contextMenuStrip
            // 
            SaveLog_contextMenuStrip.Items.AddRange(new ToolStripItem[] { saveLogTotxtToolStripMenuItem });
            SaveLog_contextMenuStrip.Name = "SaveLog_contextMenuStrip";
            SaveLog_contextMenuStrip.Size = new Size(157, 26);
            // 
            // saveLogTotxtToolStripMenuItem
            // 
            saveLogTotxtToolStripMenuItem.Image = Properties.Resources.save_32;
            saveLogTotxtToolStripMenuItem.Name = "saveLogTotxtToolStripMenuItem";
            saveLogTotxtToolStripMenuItem.Size = new Size(156, 22);
            saveLogTotxtToolStripMenuItem.Text = "Save Log To .txt";
            saveLogTotxtToolStripMenuItem.Click += saveLogTotxtToolStripMenuItem_Click;
            // 
            // flipImage_contextMenuStrip
            // 
            flipImage_contextMenuStrip.Items.AddRange(new ToolStripItem[] { flipImageToolStripMenuItem });
            flipImage_contextMenuStrip.Name = "flipImage_contextMenuStrip";
            flipImage_contextMenuStrip.Size = new Size(130, 26);
            // 
            // flipImageToolStripMenuItem
            // 
            flipImageToolStripMenuItem.Image = Properties.Resources.flip_image_32;
            flipImageToolStripMenuItem.Name = "flipImageToolStripMenuItem";
            flipImageToolStripMenuItem.Size = new Size(129, 22);
            flipImageToolStripMenuItem.Text = "Flip Image";
            flipImageToolStripMenuItem.Click += flipImageToolStripMenuItem_Click;
            // 
            // MainForm
            // 
            AutoScaleDimensions = new SizeF(7F, 15F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(1271, 725);
            Controls.Add(splitContainer1);
            Controls.Add(statusStrip1);
            Controls.Add(menuStrip1);
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            Name = "MainForm";
            Text = "Blobset Tools";
            FormClosing += MainForm_FormClosing;
            Load += MainForm_Load;
            statusStrip1.ResumeLayout(false);
            statusStrip1.PerformLayout();
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            splitContainer1.Panel1.ResumeLayout(false);
            splitContainer1.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)splitContainer1).EndInit();
            splitContainer1.ResumeLayout(false);
            splitContainer2.Panel1.ResumeLayout(false);
            splitContainer2.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)splitContainer2).EndInit();
            splitContainer2.ResumeLayout(false);
            splitContainer3.Panel1.ResumeLayout(false);
            splitContainer3.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)splitContainer3).EndInit();
            splitContainer3.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)dds_pictureBox).EndInit();
            extractImage_contextMenuStrip.ResumeLayout(false);
            extractFile_contextMenuStrip.ResumeLayout(false);
            SaveLog_contextMenuStrip.ResumeLayout(false);
            flipImage_contextMenuStrip.ResumeLayout(false);
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion
        private StatusStrip statusStrip1;
        private MenuStrip menuStrip1;
        private ToolStripMenuItem fileToolStripMenuItem;
        private ToolStripMenuItem exitToolStripMenuItem;
        private ToolStripMenuItem createBlobsetToolStripMenuItem;
        private ToolStripMenuItem extractBlobsetToolStripMenuItem;
        private ToolStripMenuItem optionsToolStripMenuItem;
        private ToolStripMenuItem blobsetCompressionToolStripMenuItem;
        private ToolStripMenuItem updateFileMappingDataToolStripMenuItem;
        private ToolStripMenuItem loadGameToolStripMenuItem;
        private ToolStripMenuItem validateSteamGameFilesToolStripMenuItem;
        private ToolStripMenuItem aboutToolStripMenuItem;
        private ToolStripStatusLabel status_Label;
        private ToolStripProgressBar toolStripProgressBar;
        private OpenFileDialog ofd;
        private ToolStripStatusLabel progressStripStatusLabel;
        private ToolStripMenuItem skipUnknownFilesToolStripMenuItem;
        private SplitContainer splitContainer1;
        private SplitContainer splitContainer2;
        private TreeView folder_treeView;
        private SplitContainer splitContainer3;
        private ListView files_listView;
        private RichTextBox fileInfo_richTextBox;
        private PictureBox dds_pictureBox;
        private ContextMenuStrip extractImage_contextMenuStrip;
        private ToolStripMenuItem extractToolStripMenuItem;
        private ToolStripMenuItem dDSFileToolStripMenuItem;
        private ToolStripMenuItem pNGFileToolStripMenuItem;
        private SaveFileDialog saveFileDialog;
        private ContextMenuStrip extractFile_contextMenuStrip;
        private ToolStripMenuItem extractToolStripMenuItem1;
        private ContextMenuStrip SaveLog_contextMenuStrip;
        private ToolStripMenuItem saveLogTotxtToolStripMenuItem;
        private ContextMenuStrip flipImage_contextMenuStrip;
        private ToolStripMenuItem flipImageToolStripMenuItem;
        private ToolStripTextBox gameName_toolStripTextBox;
        private ToolStripMenuItem toolsToolStripMenuItem;
        private ToolStripMenuItem tXPKCreatorToolStripMenuItem;
        private ToolStripMenuItem m3MPCreatorToolStripMenuItem;
    }
}

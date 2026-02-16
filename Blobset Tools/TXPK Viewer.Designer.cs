namespace Blobset_Tools
{
    partial class TXPK_Viewer
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
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
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            components = new System.ComponentModel.Container();
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(TXPK_Viewer));
            flipImage_contextMenuStrip = new ContextMenuStrip(components);
            flipImageToolStripMenuItem = new ToolStripMenuItem();
            alphaToolStripMenuItem = new ToolStripMenuItem();
            extractToPNGToolStripMenuItem = new ToolStripMenuItem();
            extractDDS_contextMenuStrip = new ContextMenuStrip(components);
            extractDDSToolStripMenuItem = new ToolStripMenuItem();
            extractTXPK_fbd = new FolderBrowserDialog();
            saveFileDialog = new SaveFileDialog();
            menuStrip1 = new MenuStrip();
            extractTXPKToolStripMenuItem = new ToolStripMenuItem();
            statusStrip1 = new StatusStrip();
            status_Label = new ToolStripStatusLabel();
            DDSInfo_SSLabel = new ToolStripStatusLabel();
            progressStripStatusLabel = new ToolStripStatusLabel();
            toolStripProgressBar = new ToolStripProgressBar();
            splitContainer1 = new SplitContainer();
            splitContainer2 = new SplitContainer();
            folder_treeView = new TreeView();
            files_listView = new ListView();
            pictureBox1 = new PictureBox();
            flipImage_contextMenuStrip.SuspendLayout();
            extractDDS_contextMenuStrip.SuspendLayout();
            menuStrip1.SuspendLayout();
            statusStrip1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)splitContainer1).BeginInit();
            splitContainer1.Panel1.SuspendLayout();
            splitContainer1.Panel2.SuspendLayout();
            splitContainer1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)splitContainer2).BeginInit();
            splitContainer2.Panel1.SuspendLayout();
            splitContainer2.Panel2.SuspendLayout();
            splitContainer2.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)pictureBox1).BeginInit();
            SuspendLayout();
            // 
            // flipImage_contextMenuStrip
            // 
            flipImage_contextMenuStrip.ImageScalingSize = new Size(20, 20);
            flipImage_contextMenuStrip.Items.AddRange(new ToolStripItem[] { flipImageToolStripMenuItem, alphaToolStripMenuItem, extractToPNGToolStripMenuItem });
            flipImage_contextMenuStrip.Name = "flipImage_contextMenuStrip";
            flipImage_contextMenuStrip.Size = new Size(181, 82);
            // 
            // flipImageToolStripMenuItem
            // 
            flipImageToolStripMenuItem.Image = Properties.Resources.flip_image_32;
            flipImageToolStripMenuItem.Name = "flipImageToolStripMenuItem";
            flipImageToolStripMenuItem.Size = new Size(180, 26);
            flipImageToolStripMenuItem.Text = "Flip Image";
            flipImageToolStripMenuItem.Click += flipImageToolStripMenuItem_Click;
            // 
            // alphaToolStripMenuItem
            // 
            alphaToolStripMenuItem.Checked = true;
            alphaToolStripMenuItem.CheckOnClick = true;
            alphaToolStripMenuItem.CheckState = CheckState.Checked;
            alphaToolStripMenuItem.Image = Properties.Resources.alpha_32;
            alphaToolStripMenuItem.Name = "alphaToolStripMenuItem";
            alphaToolStripMenuItem.Size = new Size(180, 26);
            alphaToolStripMenuItem.Text = "Alpha";
            alphaToolStripMenuItem.Click += alphaToolStripMenuItem_Click;
            // 
            // extractToPNGToolStripMenuItem
            // 
            extractToPNGToolStripMenuItem.Image = Properties.Resources.png_32;
            extractToPNGToolStripMenuItem.Name = "extractToPNGToolStripMenuItem";
            extractToPNGToolStripMenuItem.Size = new Size(180, 26);
            extractToPNGToolStripMenuItem.Text = "Extract To PNG";
            extractToPNGToolStripMenuItem.Click += extractToPNGToolStripMenuItem_Click;
            // 
            // extractDDS_contextMenuStrip
            // 
            extractDDS_contextMenuStrip.ImageScalingSize = new Size(20, 20);
            extractDDS_contextMenuStrip.Items.AddRange(new ToolStripItem[] { extractDDSToolStripMenuItem });
            extractDDS_contextMenuStrip.Name = "extractDDS_contextMenuStrip";
            extractDDS_contextMenuStrip.Size = new Size(162, 30);
            // 
            // extractDDSToolStripMenuItem
            // 
            extractDDSToolStripMenuItem.Image = Properties.Resources.extract_32;
            extractDDSToolStripMenuItem.Name = "extractDDSToolStripMenuItem";
            extractDDSToolStripMenuItem.Size = new Size(161, 26);
            extractDDSToolStripMenuItem.Text = "Extract DDS";
            extractDDSToolStripMenuItem.Click += extractDDSToolStripMenuItem_Click;
            // 
            // extractTXPK_fbd
            // 
            extractTXPK_fbd.Description = "Select your txpk folder in your games folder of the Blobset Tools.";
            // 
            // saveFileDialog
            // 
            saveFileDialog.Title = "Select your txpk folder in your games folder of the Blobset Tools.";
            // 
            // menuStrip1
            // 
            menuStrip1.ImageScalingSize = new Size(20, 20);
            menuStrip1.Items.AddRange(new ToolStripItem[] { extractTXPKToolStripMenuItem });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Padding = new Padding(7, 3, 0, 3);
            menuStrip1.Size = new Size(914, 30);
            menuStrip1.TabIndex = 0;
            menuStrip1.Text = "menuStrip1";
            // 
            // extractTXPKToolStripMenuItem
            // 
            extractTXPKToolStripMenuItem.Name = "extractTXPKToolStripMenuItem";
            extractTXPKToolStripMenuItem.Size = new Size(106, 24);
            extractTXPKToolStripMenuItem.Text = "Extract TXPK";
            extractTXPKToolStripMenuItem.Click += extractTXPKToolStripMenuItem_Click;
            // 
            // statusStrip1
            // 
            statusStrip1.ImageScalingSize = new Size(20, 20);
            statusStrip1.Items.AddRange(new ToolStripItem[] { status_Label, DDSInfo_SSLabel, progressStripStatusLabel, toolStripProgressBar });
            statusStrip1.Location = new Point(0, 573);
            statusStrip1.Name = "statusStrip1";
            statusStrip1.Padding = new Padding(1, 0, 16, 0);
            statusStrip1.Size = new Size(914, 27);
            statusStrip1.TabIndex = 2;
            statusStrip1.Text = "statusStrip1";
            // 
            // status_Label
            // 
            status_Label.Name = "status_Label";
            status_Label.Size = new Size(0, 21);
            // 
            // DDSInfo_SSLabel
            // 
            DDSInfo_SSLabel.ForeColor = Color.Blue;
            DDSInfo_SSLabel.Name = "DDSInfo_SSLabel";
            DDSInfo_SSLabel.Size = new Size(666, 21);
            DDSInfo_SSLabel.Spring = true;
            DDSInfo_SSLabel.TextAlign = ContentAlignment.MiddleRight;
            // 
            // progressStripStatusLabel
            // 
            progressStripStatusLabel.Name = "progressStripStatusLabel";
            progressStripStatusLabel.Size = new Size(0, 21);
            // 
            // toolStripProgressBar
            // 
            toolStripProgressBar.Name = "toolStripProgressBar";
            toolStripProgressBar.Size = new Size(229, 19);
            // 
            // splitContainer1
            // 
            splitContainer1.Dock = DockStyle.Fill;
            splitContainer1.Location = new Point(0, 30);
            splitContainer1.Margin = new Padding(3, 4, 3, 4);
            splitContainer1.Name = "splitContainer1";
            // 
            // splitContainer1.Panel1
            // 
            splitContainer1.Panel1.Controls.Add(splitContainer2);
            // 
            // splitContainer1.Panel2
            // 
            splitContainer1.Panel2.Controls.Add(pictureBox1);
            splitContainer1.Size = new Size(914, 543);
            splitContainer1.SplitterDistance = 512;
            splitContainer1.SplitterWidth = 5;
            splitContainer1.TabIndex = 3;
            // 
            // splitContainer2
            // 
            splitContainer2.Dock = DockStyle.Fill;
            splitContainer2.Location = new Point(0, 0);
            splitContainer2.Margin = new Padding(3, 4, 3, 4);
            splitContainer2.Name = "splitContainer2";
            // 
            // splitContainer2.Panel1
            // 
            splitContainer2.Panel1.Controls.Add(folder_treeView);
            // 
            // splitContainer2.Panel2
            // 
            splitContainer2.Panel2.Controls.Add(files_listView);
            splitContainer2.Size = new Size(512, 543);
            splitContainer2.SplitterDistance = 204;
            splitContainer2.SplitterWidth = 5;
            splitContainer2.TabIndex = 0;
            // 
            // folder_treeView
            // 
            folder_treeView.BackColor = Color.WhiteSmoke;
            folder_treeView.Dock = DockStyle.Fill;
            folder_treeView.Font = new Font("Segoe UI", 10F);
            folder_treeView.Location = new Point(0, 0);
            folder_treeView.Margin = new Padding(3, 4, 3, 4);
            folder_treeView.Name = "folder_treeView";
            folder_treeView.Size = new Size(204, 543);
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
            files_listView.Margin = new Padding(3, 4, 3, 4);
            files_listView.MultiSelect = false;
            files_listView.Name = "files_listView";
            files_listView.Size = new Size(303, 543);
            files_listView.TabIndex = 0;
            files_listView.UseCompatibleStateImageBehavior = false;
            files_listView.View = View.List;
            files_listView.SelectedIndexChanged += files_listView_SelectedIndexChanged;
            // 
            // pictureBox1
            // 
            pictureBox1.BackgroundImageLayout = ImageLayout.Center;
            pictureBox1.Dock = DockStyle.Fill;
            pictureBox1.Location = new Point(0, 0);
            pictureBox1.Margin = new Padding(3, 4, 3, 4);
            pictureBox1.Name = "pictureBox1";
            pictureBox1.Size = new Size(397, 543);
            pictureBox1.SizeMode = PictureBoxSizeMode.Zoom;
            pictureBox1.TabIndex = 0;
            pictureBox1.TabStop = false;
            pictureBox1.MouseClick += pictureBox1_MouseClick;
            // 
            // TXPK_Viewer
            // 
            AutoScaleDimensions = new SizeF(120F, 120F);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(914, 600);
            Controls.Add(splitContainer1);
            Controls.Add(statusStrip1);
            Controls.Add(menuStrip1);
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            Margin = new Padding(3, 4, 3, 4);
            Name = "TXPK_Viewer";
            Text = "TXPK Viewer";
            Load += TXPK_Viewer_Load;
            flipImage_contextMenuStrip.ResumeLayout(false);
            extractDDS_contextMenuStrip.ResumeLayout(false);
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            statusStrip1.ResumeLayout(false);
            statusStrip1.PerformLayout();
            splitContainer1.Panel1.ResumeLayout(false);
            splitContainer1.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)splitContainer1).EndInit();
            splitContainer1.ResumeLayout(false);
            splitContainer2.Panel1.ResumeLayout(false);
            splitContainer2.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)splitContainer2).EndInit();
            splitContainer2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)pictureBox1).EndInit();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion
        private ContextMenuStrip flipImage_contextMenuStrip;
        private ToolStripMenuItem flipImageToolStripMenuItem;
        private ContextMenuStrip extractDDS_contextMenuStrip;
        private ToolStripMenuItem extractDDSToolStripMenuItem;
        private FolderBrowserDialog extractTXPK_fbd;
        private ToolStripMenuItem extractToPNGToolStripMenuItem;
        private SaveFileDialog saveFileDialog;
        private MenuStrip menuStrip1;
        private StatusStrip statusStrip1;
        private ToolStripStatusLabel status_Label;
        private ToolStripStatusLabel DDSInfo_SSLabel;
        private ToolStripMenuItem extractTXPKToolStripMenuItem;
        private SplitContainer splitContainer1;
        private SplitContainer splitContainer2;
        private TreeView folder_treeView;
        private ListView files_listView;
        private PictureBox pictureBox1;
        private ToolStripStatusLabel progressStripStatusLabel;
        private ToolStripProgressBar toolStripProgressBar;
        private ToolStripMenuItem alphaToolStripMenuItem;
    }
}
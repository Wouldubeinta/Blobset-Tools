namespace Blobset_Tools
{
    partial class M3MP_Viewer
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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(M3MP_Viewer));
            menuStrip1 = new MenuStrip();
            extractM3MPToolStripMenuItem = new ToolStripMenuItem();
            statusStrip1 = new StatusStrip();
            status_Label = new ToolStripStatusLabel();
            progressStripStatusLabel = new ToolStripStatusLabel();
            toolStripProgressBar = new ToolStripProgressBar();
            splitContainer1 = new SplitContainer();
            folder_treeView = new TreeView();
            files_listView = new ListView();
            extractM3MP_fbd = new FolderBrowserDialog();
            menuStrip1.SuspendLayout();
            statusStrip1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)splitContainer1).BeginInit();
            splitContainer1.Panel1.SuspendLayout();
            splitContainer1.Panel2.SuspendLayout();
            splitContainer1.SuspendLayout();
            SuspendLayout();
            // 
            // menuStrip1
            // 
            menuStrip1.Items.AddRange(new ToolStripItem[] { extractM3MPToolStripMenuItem });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Size = new Size(800, 24);
            menuStrip1.TabIndex = 0;
            menuStrip1.Text = "menuStrip1";
            // 
            // extractM3MPToolStripMenuItem
            // 
            extractM3MPToolStripMenuItem.Name = "extractM3MPToolStripMenuItem";
            extractM3MPToolStripMenuItem.Size = new Size(92, 20);
            extractM3MPToolStripMenuItem.Text = "Extract M3MP";
            extractM3MPToolStripMenuItem.Click += extractM3MPToolStripMenuItem_Click;
            // 
            // statusStrip1
            // 
            statusStrip1.Items.AddRange(new ToolStripItem[] { status_Label, progressStripStatusLabel, toolStripProgressBar });
            statusStrip1.Location = new Point(0, 428);
            statusStrip1.Name = "statusStrip1";
            statusStrip1.Size = new Size(800, 22);
            statusStrip1.TabIndex = 1;
            statusStrip1.Text = "statusStrip1";
            // 
            // status_Label
            // 
            status_Label.Name = "status_Label";
            status_Label.Size = new Size(583, 17);
            status_Label.Spring = true;
            status_Label.TextAlign = ContentAlignment.MiddleLeft;
            // 
            // progressStripStatusLabel
            // 
            progressStripStatusLabel.Name = "progressStripStatusLabel";
            progressStripStatusLabel.Size = new Size(0, 17);
            // 
            // toolStripProgressBar
            // 
            toolStripProgressBar.Name = "toolStripProgressBar";
            toolStripProgressBar.Size = new Size(200, 16);
            // 
            // splitContainer1
            // 
            splitContainer1.Dock = DockStyle.Fill;
            splitContainer1.Location = new Point(0, 24);
            splitContainer1.Name = "splitContainer1";
            // 
            // splitContainer1.Panel1
            // 
            splitContainer1.Panel1.Controls.Add(folder_treeView);
            // 
            // splitContainer1.Panel2
            // 
            splitContainer1.Panel2.Controls.Add(files_listView);
            splitContainer1.Size = new Size(800, 404);
            splitContainer1.SplitterDistance = 266;
            splitContainer1.TabIndex = 2;
            // 
            // folder_treeView
            // 
            folder_treeView.Dock = DockStyle.Fill;
            folder_treeView.Location = new Point(0, 0);
            folder_treeView.Name = "folder_treeView";
            folder_treeView.Size = new Size(266, 404);
            folder_treeView.TabIndex = 0;
            folder_treeView.AfterSelect += folder_treeView_AfterSelect;
            // 
            // files_listView
            // 
            files_listView.Dock = DockStyle.Fill;
            files_listView.HeaderStyle = ColumnHeaderStyle.None;
            files_listView.Location = new Point(0, 0);
            files_listView.MultiSelect = false;
            files_listView.Name = "files_listView";
            files_listView.Size = new Size(530, 404);
            files_listView.TabIndex = 0;
            files_listView.UseCompatibleStateImageBehavior = false;
            files_listView.View = View.List;
            // 
            // extractM3MP_fbd
            // 
            extractM3MP_fbd.Description = "Select your m3mp folder in your games folder of the Blobset Tools.";
            // 
            // M3MP_Viewer
            // 
            AutoScaleDimensions = new SizeF(7F, 15F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(800, 450);
            Controls.Add(splitContainer1);
            Controls.Add(statusStrip1);
            Controls.Add(menuStrip1);
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            Name = "M3MP_Viewer";
            Text = "M3MP Viewer";
            FormClosing += M3MP_Viewer_FormClosing;
            Load += M3MP_Viewer_Load;
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            statusStrip1.ResumeLayout(false);
            statusStrip1.PerformLayout();
            splitContainer1.Panel1.ResumeLayout(false);
            splitContainer1.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)splitContainer1).EndInit();
            splitContainer1.ResumeLayout(false);
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private MenuStrip menuStrip1;
        private StatusStrip statusStrip1;
        private SplitContainer splitContainer1;
        private ToolStripMenuItem extractM3MPToolStripMenuItem;
        private TreeView folder_treeView;
        private ListView files_listView;
        private ToolStripStatusLabel status_Label;
        private ToolStripStatusLabel progressStripStatusLabel;
        private ToolStripProgressBar toolStripProgressBar;
        private FolderBrowserDialog extractM3MP_fbd;
    }
}
namespace Blobset_Tools
{
    partial class DDS_Viewer
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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(DDS_Viewer));
            statusStrip1 = new StatusStrip();
            toolStripStatusLabel1 = new ToolStripStatusLabel();
            pictureBox1 = new PictureBox();
            menuStrip1 = new MenuStrip();
            gridToolStripMenuItem = new ToolStripMenuItem();
            toolStripComboBox = new ToolStripComboBox();
            contextMenuStrip1 = new ContextMenuStrip(components);
            flipImageToolStripMenuItem = new ToolStripMenuItem();
            alphaToolStripMenuItem = new ToolStripMenuItem();
            extractToolStripMenuItem = new ToolStripMenuItem();
            pngFileToolStripMenuItem = new ToolStripMenuItem();
            saveFileDialog = new SaveFileDialog();
            statusStrip1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)pictureBox1).BeginInit();
            menuStrip1.SuspendLayout();
            contextMenuStrip1.SuspendLayout();
            SuspendLayout();
            // 
            // statusStrip1
            // 
            statusStrip1.ImageScalingSize = new Size(20, 20);
            statusStrip1.Items.AddRange(new ToolStripItem[] { toolStripStatusLabel1 });
            statusStrip1.Location = new Point(0, 445);
            statusStrip1.Name = "statusStrip1";
            statusStrip1.Padding = new Padding(1, 0, 17, 0);
            statusStrip1.Size = new Size(676, 22);
            statusStrip1.TabIndex = 0;
            statusStrip1.Text = "statusStrip1";
            // 
            // toolStripStatusLabel1
            // 
            toolStripStatusLabel1.ForeColor = Color.MediumBlue;
            toolStripStatusLabel1.Name = "toolStripStatusLabel1";
            toolStripStatusLabel1.Size = new Size(0, 17);
            // 
            // pictureBox1
            // 
            pictureBox1.BackgroundImage = Properties.Resources.grid_light;
            pictureBox1.Dock = DockStyle.Fill;
            pictureBox1.Location = new Point(0, 28);
            pictureBox1.Margin = new Padding(4);
            pictureBox1.Name = "pictureBox1";
            pictureBox1.Size = new Size(676, 417);
            pictureBox1.SizeMode = PictureBoxSizeMode.Zoom;
            pictureBox1.TabIndex = 1;
            pictureBox1.TabStop = false;
            pictureBox1.MouseClick += pictureBox1_MouseClick;
            // 
            // menuStrip1
            // 
            menuStrip1.ImageScalingSize = new Size(20, 20);
            menuStrip1.Items.AddRange(new ToolStripItem[] { gridToolStripMenuItem, toolStripComboBox });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Padding = new Padding(7, 2, 0, 2);
            menuStrip1.Size = new Size(676, 28);
            menuStrip1.TabIndex = 2;
            menuStrip1.Text = "menuStrip1";
            // 
            // gridToolStripMenuItem
            // 
            gridToolStripMenuItem.CheckOnClick = true;
            gridToolStripMenuItem.Image = Properties.Resources.grid_light;
            gridToolStripMenuItem.Name = "gridToolStripMenuItem";
            gridToolStripMenuItem.Size = new Size(124, 24);
            gridToolStripMenuItem.Text = "Grid (Light/Dark";
            gridToolStripMenuItem.Click += gridToolStripMenuItem_Click;
            // 
            // toolStripComboBox
            // 
            toolStripComboBox.DropDownStyle = ComboBoxStyle.DropDownList;
            toolStripComboBox.DropDownWidth = 100;
            toolStripComboBox.Items.AddRange(new object[] { "Normal", "StretchImage", "AutoSize", "CenterImage", "Zoom" });
            toolStripComboBox.Name = "toolStripComboBox";
            toolStripComboBox.Size = new Size(116, 24);
            toolStripComboBox.SelectedIndexChanged += toolStripComboBox_SelectedIndexChanged;
            // 
            // contextMenuStrip1
            // 
            contextMenuStrip1.Items.AddRange(new ToolStripItem[] { flipImageToolStripMenuItem, alphaToolStripMenuItem, extractToolStripMenuItem });
            contextMenuStrip1.Name = "contextMenuStrip1";
            contextMenuStrip1.Size = new Size(130, 70);
            // 
            // flipImageToolStripMenuItem
            // 
            flipImageToolStripMenuItem.Image = Properties.Resources.flip_image_32;
            flipImageToolStripMenuItem.Name = "flipImageToolStripMenuItem";
            flipImageToolStripMenuItem.Size = new Size(129, 22);
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
            alphaToolStripMenuItem.Size = new Size(129, 22);
            alphaToolStripMenuItem.Text = "Alpha";
            alphaToolStripMenuItem.Click += alphaToolStripMenuItem_Click;
            // 
            // extractToolStripMenuItem
            // 
            extractToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { pngFileToolStripMenuItem });
            extractToolStripMenuItem.Image = Properties.Resources.extract_32;
            extractToolStripMenuItem.Name = "extractToolStripMenuItem";
            extractToolStripMenuItem.Size = new Size(129, 22);
            extractToolStripMenuItem.Text = "Extract";
            // 
            // pngFileToolStripMenuItem
            // 
            pngFileToolStripMenuItem.Image = Properties.Resources.png_32;
            pngFileToolStripMenuItem.Name = "pngFileToolStripMenuItem";
            pngFileToolStripMenuItem.Size = new Size(119, 22);
            pngFileToolStripMenuItem.Text = "PNG File";
            pngFileToolStripMenuItem.Click += pngFileToolStripMenuItem_Click;
            // 
            // DDS_Viewer
            // 
            AutoScaleDimensions = new SizeF(7F, 15F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(676, 467);
            Controls.Add(pictureBox1);
            Controls.Add(statusStrip1);
            Controls.Add(menuStrip1);
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            Margin = new Padding(4);
            Name = "DDS_Viewer";
            Text = "DDS Viewer";
            Load += TexturePreview_Load;
            statusStrip1.ResumeLayout(false);
            statusStrip1.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)pictureBox1).EndInit();
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            contextMenuStrip1.ResumeLayout(false);
            ResumeLayout(false);
            PerformLayout();

        }

        #endregion

        private System.Windows.Forms.StatusStrip statusStrip1;
        private System.Windows.Forms.PictureBox pictureBox1;
        private System.Windows.Forms.ToolStripStatusLabel toolStripStatusLabel1;
        private System.Windows.Forms.MenuStrip menuStrip1;
        private System.Windows.Forms.ToolStripMenuItem gridToolStripMenuItem;
        private System.Windows.Forms.ToolStripComboBox toolStripComboBox;
        private ContextMenuStrip contextMenuStrip1;
        private ToolStripMenuItem flipImageToolStripMenuItem;
        private ToolStripMenuItem extractToolStripMenuItem;
        private ToolStripMenuItem pngFileToolStripMenuItem;
        private SaveFileDialog saveFileDialog;
        private ToolStripMenuItem alphaToolStripMenuItem;
    }
}
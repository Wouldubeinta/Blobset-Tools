namespace Blobset_Tools
{
    partial class File_Mapping_Editor
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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(File_Mapping_Editor));
            statusStrip1 = new StatusStrip();
            Save_toolStripSplitButton = new ToolStripSplitButton();
            menuStrip1 = new MenuStrip();
            searchToolStripMenuItem = new ToolStripMenuItem();
            Search_toolStripTextBox = new ToolStripTextBox();
            dataGridView1 = new DataGridView();
            statusStrip1.SuspendLayout();
            menuStrip1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)dataGridView1).BeginInit();
            SuspendLayout();
            // 
            // statusStrip1
            // 
            statusStrip1.Items.AddRange(new ToolStripItem[] { Save_toolStripSplitButton });
            statusStrip1.Location = new Point(0, 524);
            statusStrip1.Name = "statusStrip1";
            statusStrip1.Size = new Size(1173, 22);
            statusStrip1.TabIndex = 0;
            statusStrip1.Text = "statusStrip1";
            // 
            // Save_toolStripSplitButton
            // 
            Save_toolStripSplitButton.DisplayStyle = ToolStripItemDisplayStyle.Text;
            Save_toolStripSplitButton.Image = (Image)resources.GetObject("Save_toolStripSplitButton.Image");
            Save_toolStripSplitButton.ImageTransparentColor = Color.Magenta;
            Save_toolStripSplitButton.Name = "Save_toolStripSplitButton";
            Save_toolStripSplitButton.Size = new Size(100, 20);
            Save_toolStripSplitButton.Text = "Save Changers";
            Save_toolStripSplitButton.ButtonClick += Save_toolStripSplitButton_ButtonClick;
            // 
            // menuStrip1
            // 
            menuStrip1.Items.AddRange(new ToolStripItem[] { searchToolStripMenuItem, Search_toolStripTextBox });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Size = new Size(1173, 27);
            menuStrip1.TabIndex = 1;
            menuStrip1.Text = "menuStrip1";
            // 
            // searchToolStripMenuItem
            // 
            searchToolStripMenuItem.Name = "searchToolStripMenuItem";
            searchToolStripMenuItem.Size = new Size(54, 23);
            searchToolStripMenuItem.Text = "Search";
            searchToolStripMenuItem.Click += searchToolStripMenuItem_Click;
            // 
            // Search_toolStripTextBox
            // 
            Search_toolStripTextBox.Name = "Search_toolStripTextBox";
            Search_toolStripTextBox.Size = new Size(500, 23);
            Search_toolStripTextBox.KeyDown += Search_toolStripTextBox_KeyDown;
            // 
            // dataGridView1
            // 
            dataGridView1.AllowUserToAddRows = false;
            dataGridView1.AllowUserToDeleteRows = false;
            dataGridView1.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
            dataGridView1.Dock = DockStyle.Fill;
            dataGridView1.Location = new Point(0, 27);
            dataGridView1.Name = "dataGridView1";
            dataGridView1.Size = new Size(1173, 497);
            dataGridView1.TabIndex = 2;
            // 
            // File_Mapping_Editor
            // 
            AutoScaleDimensions = new SizeF(7F, 15F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(1173, 546);
            Controls.Add(dataGridView1);
            Controls.Add(statusStrip1);
            Controls.Add(menuStrip1);
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            Name = "File_Mapping_Editor";
            Text = "File Mapping Editor";
            Load += File_Mapping_Editor_Load;
            statusStrip1.ResumeLayout(false);
            statusStrip1.PerformLayout();
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)dataGridView1).EndInit();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private StatusStrip statusStrip1;
        private ToolStripSplitButton Save_toolStripSplitButton;
        private MenuStrip menuStrip1;
        private ToolStripMenuItem searchToolStripMenuItem;
        private ToolStripTextBox Search_toolStripTextBox;
        private DataGridView dataGridView1;
    }
}
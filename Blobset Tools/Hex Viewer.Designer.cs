namespace Blobset_Tools
{
    partial class Hex_Viewer
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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(Hex_Viewer));
            menuStrip1 = new MenuStrip();
            searchToolStripMenuItem = new ToolStripMenuItem();
            firstToolStripMenuItem = new ToolStripMenuItem();
            nextToolStripMenuItem = new ToolStripMenuItem();
            previousToolStripMenuItem = new ToolStripMenuItem();
            toolStripTextBox1 = new ToolStripTextBox();
            statusStrip1 = new StatusStrip();
            status_Label = new ToolStripStatusLabel();
            srcHexViewer1 = new HexViewer.SRCHexViewer();
            menuStrip1.SuspendLayout();
            statusStrip1.SuspendLayout();
            SuspendLayout();
            // 
            // menuStrip1
            // 
            menuStrip1.Items.AddRange(new ToolStripItem[] { searchToolStripMenuItem, toolStripTextBox1 });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Size = new Size(800, 27);
            menuStrip1.TabIndex = 0;
            menuStrip1.Text = "menuStrip1";
            // 
            // searchToolStripMenuItem
            // 
            searchToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { firstToolStripMenuItem, nextToolStripMenuItem, previousToolStripMenuItem });
            searchToolStripMenuItem.Name = "searchToolStripMenuItem";
            searchToolStripMenuItem.Size = new Size(54, 23);
            searchToolStripMenuItem.Text = "Search";
            // 
            // firstToolStripMenuItem
            // 
            firstToolStripMenuItem.Name = "firstToolStripMenuItem";
            firstToolStripMenuItem.Size = new Size(180, 22);
            firstToolStripMenuItem.Text = "First";
            firstToolStripMenuItem.Click += firstToolStripMenuItem_Click;
            // 
            // nextToolStripMenuItem
            // 
            nextToolStripMenuItem.Name = "nextToolStripMenuItem";
            nextToolStripMenuItem.Size = new Size(180, 22);
            nextToolStripMenuItem.Text = "Next";
            nextToolStripMenuItem.Click += nextToolStripMenuItem_Click;
            // 
            // previousToolStripMenuItem
            // 
            previousToolStripMenuItem.Name = "previousToolStripMenuItem";
            previousToolStripMenuItem.Size = new Size(180, 22);
            previousToolStripMenuItem.Text = "Previous";
            previousToolStripMenuItem.Click += previousToolStripMenuItem_Click;
            // 
            // toolStripTextBox1
            // 
            toolStripTextBox1.Name = "toolStripTextBox1";
            toolStripTextBox1.Size = new Size(300, 23);
            // 
            // statusStrip1
            // 
            statusStrip1.Items.AddRange(new ToolStripItem[] { status_Label });
            statusStrip1.Location = new Point(0, 428);
            statusStrip1.Name = "statusStrip1";
            statusStrip1.Size = new Size(800, 22);
            statusStrip1.TabIndex = 2;
            statusStrip1.Text = "statusStrip1";
            // 
            // status_Label
            // 
            status_Label.Name = "status_Label";
            status_Label.Size = new Size(118, 17);
            status_Label.Text = "toolStripStatusLabel1";
            // 
            // srcHexViewer1
            // 
            srcHexViewer1.AlternateBackColor1 = Color.FromArgb(100, 100, 100);
            srcHexViewer1.AlternateBackColor2 = Color.FromArgb(64, 64, 64);
            srcHexViewer1.AlternateTextColor1 = Color.White;
            srcHexViewer1.AlternateTextColor2 = Color.White;
            srcHexViewer1.AutoSelectCharacter = true;
            srcHexViewer1.AutoSize = true;
            srcHexViewer1.BackColor = Color.FromArgb(64, 64, 64);
            srcHexViewer1.BytesPerLine = 16;
            srcHexViewer1.Dock = DockStyle.Fill;
            srcHexViewer1.HexCenterSeparator = '-';
            srcHexViewer1.HexDataSeparator = ' ';
            srcHexViewer1.HexLineNumbers = true;
            srcHexViewer1.HexViewBackColor = Color.FromArgb(64, 64, 64);
            srcHexViewer1.HexViewFont = new Font("Lucida Console", 9F, FontStyle.Regular, GraphicsUnit.Point, 0);
            srcHexViewer1.HexViewForeColor = Color.White;
            srcHexViewer1.IsAlternateLineColor = true;
            srcHexViewer1.LineNumberBackColor = Color.Black;
            srcHexViewer1.LineNumberForeColor = Color.White;
            srcHexViewer1.LoadingDelay = 0;
            srcHexViewer1.Location = new Point(0, 27);
            srcHexViewer1.Margin = new Padding(4, 3, 4, 3);
            srcHexViewer1.Name = "srcHexViewer1";
            srcHexViewer1.NotifyIntegralProgressOnly = true;
            srcHexViewer1.Padding = new Padding(2);
            srcHexViewer1.ShowByteIndexInsteadOfLineNumbers = false;
            srcHexViewer1.ShowHorizontalScrollbars = false;
            srcHexViewer1.ShowLineNumber = true;
            srcHexViewer1.ShowProgressBar = false;
            srcHexViewer1.Size = new Size(800, 401);
            srcHexViewer1.SynchronizeScrolling = false;
            srcHexViewer1.TabIndex = 3;
            srcHexViewer1.TextViewBackColor = Color.FromArgb(64, 64, 64);
            srcHexViewer1.TextViewFont = new Font("Lucida Console", 9F, FontStyle.Regular, GraphicsUnit.Point, 0);
            srcHexViewer1.TextViewForeColor = Color.White;
            // 
            // Hex_Viewer
            // 
            AutoScaleDimensions = new SizeF(7F, 15F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(800, 450);
            Controls.Add(srcHexViewer1);
            Controls.Add(statusStrip1);
            Controls.Add(menuStrip1);
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            Name = "Hex_Viewer";
            Text = "Hex Viewer";
            Load += Hex_Viewer_Load;
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            statusStrip1.ResumeLayout(false);
            statusStrip1.PerformLayout();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private MenuStrip menuStrip1;
        private ToolStripMenuItem searchToolStripMenuItem;
        private ToolStripTextBox toolStripTextBox1;
        private StatusStrip statusStrip1;
        private ToolStripMenuItem firstToolStripMenuItem;
        private ToolStripMenuItem nextToolStripMenuItem;
        private ToolStripMenuItem previousToolStripMenuItem;
        private HexViewer.SRCHexViewer srcHexViewer1;
        private ToolStripStatusLabel status_Label;
    }
}
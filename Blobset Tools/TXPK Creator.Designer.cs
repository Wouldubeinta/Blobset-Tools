namespace Blobset_Tools
{
    partial class TXPK_Creator
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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(TXPK_Creator));
            menuStrip1 = new MenuStrip();
            createTXPKToolStripMenuItem = new ToolStripMenuItem();
            statusStrip1 = new StatusStrip();
            status_Label = new ToolStripStatusLabel();
            progressStripStatusLabel = new ToolStripStatusLabel();
            toolStripProgressBar = new ToolStripProgressBar();
            txpk_richTextBox = new RichTextBox();
            saveFileDialog1 = new SaveFileDialog();
            menuStrip1.SuspendLayout();
            statusStrip1.SuspendLayout();
            SuspendLayout();
            // 
            // menuStrip1
            // 
            menuStrip1.ImageScalingSize = new Size(20, 20);
            menuStrip1.Items.AddRange(new ToolStripItem[] { createTXPKToolStripMenuItem });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Padding = new Padding(7, 3, 0, 3);
            menuStrip1.Size = new Size(914, 30);
            menuStrip1.TabIndex = 0;
            menuStrip1.Text = "menuStrip1";
            // 
            // createTXPKToolStripMenuItem
            // 
            createTXPKToolStripMenuItem.Name = "createTXPKToolStripMenuItem";
            createTXPKToolStripMenuItem.Size = new Size(104, 24);
            createTXPKToolStripMenuItem.Text = "Create TXPK";
            createTXPKToolStripMenuItem.Click += createTXPKToolStripMenuItem_Click;
            // 
            // statusStrip1
            // 
            statusStrip1.ImageScalingSize = new Size(20, 20);
            statusStrip1.Items.AddRange(new ToolStripItem[] { status_Label, progressStripStatusLabel, toolStripProgressBar });
            statusStrip1.Location = new Point(0, 573);
            statusStrip1.Name = "statusStrip1";
            statusStrip1.Padding = new Padding(1, 0, 16, 0);
            statusStrip1.Size = new Size(914, 27);
            statusStrip1.TabIndex = 1;
            statusStrip1.Text = "statusStrip1";
            // 
            // status_Label
            // 
            status_Label.Name = "status_Label";
            status_Label.Size = new Size(666, 21);
            status_Label.Spring = true;
            status_Label.TextAlign = ContentAlignment.MiddleLeft;
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
            // txpk_richTextBox
            // 
            txpk_richTextBox.BackColor = Color.Black;
            txpk_richTextBox.Dock = DockStyle.Fill;
            txpk_richTextBox.ForeColor = Color.Gold;
            txpk_richTextBox.Location = new Point(0, 30);
            txpk_richTextBox.Margin = new Padding(3, 4, 3, 4);
            txpk_richTextBox.Name = "txpk_richTextBox";
            txpk_richTextBox.Size = new Size(914, 543);
            txpk_richTextBox.TabIndex = 2;
            txpk_richTextBox.Text = "";
            // 
            // saveFileDialog1
            // 
            saveFileDialog1.DefaultExt = "txpk";
            saveFileDialog1.Filter = "TXPK File|*.txpk";
            // 
            // TXPK_Creator
            // 
            AutoScaleDimensions = new SizeF(120F, 120F);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(914, 600);
            Controls.Add(txpk_richTextBox);
            Controls.Add(statusStrip1);
            Controls.Add(menuStrip1);
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            Margin = new Padding(3, 4, 3, 4);
            Name = "TXPK_Creator";
            Text = "TXPK Creator";
            Load += TXPK_Creator_Load;
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            statusStrip1.ResumeLayout(false);
            statusStrip1.PerformLayout();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private MenuStrip menuStrip1;
        private ToolStripMenuItem createTXPKToolStripMenuItem;
        private StatusStrip statusStrip1;
        private ToolStripStatusLabel status_Label;
        private ToolStripStatusLabel progressStripStatusLabel;
        private ToolStripProgressBar toolStripProgressBar;
        private RichTextBox txpk_richTextBox;
        private SaveFileDialog saveFileDialog1;
    }
}
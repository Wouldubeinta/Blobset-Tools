namespace HexViewer
{
    partial class SRCHexViewer
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

        #region Component Designer generated code

        /// <summary> 
        /// Required method for Designer support - do not modify 
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.pgbStatus = new System.Windows.Forms.ProgressBar();
            this.splitContainerMain = new System.Windows.Forms.SplitContainer();
            this.rtbHex = new HexViewer.SynchronizedScrollRichTextBox();
            this.rtbASCII = new HexViewer.SynchronizedScrollRichTextBox();
            ((System.ComponentModel.ISupportInitialize)(this.splitContainerMain)).BeginInit();
            this.splitContainerMain.Panel1.SuspendLayout();
            this.splitContainerMain.Panel2.SuspendLayout();
            this.splitContainerMain.SuspendLayout();
            this.SuspendLayout();
            // 
            // pgbStatus
            // 
            this.pgbStatus.Dock = System.Windows.Forms.DockStyle.Bottom;
            this.pgbStatus.Location = new System.Drawing.Point(2, 369);
            this.pgbStatus.Name = "pgbStatus";
            this.pgbStatus.Size = new System.Drawing.Size(599, 10);
            this.pgbStatus.Style = System.Windows.Forms.ProgressBarStyle.Continuous;
            this.pgbStatus.TabIndex = 0;
            this.pgbStatus.Visible = false;
            // 
            // splitContainerMain
            // 
            this.splitContainerMain.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(64)))), ((int)(((byte)(64)))), ((int)(((byte)(64)))));
            this.splitContainerMain.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.splitContainerMain.Dock = System.Windows.Forms.DockStyle.Fill;
            this.splitContainerMain.FixedPanel = System.Windows.Forms.FixedPanel.Panel1;
            this.splitContainerMain.IsSplitterFixed = true;
            this.splitContainerMain.Location = new System.Drawing.Point(2, 2);
            this.splitContainerMain.Name = "splitContainerMain";
            // 
            // splitContainerMain.Panel1
            // 
            this.splitContainerMain.Panel1.Controls.Add(this.rtbHex);
            this.splitContainerMain.Panel1.Padding = new System.Windows.Forms.Padding(3);
            // 
            // splitContainerMain.Panel2
            // 
            this.splitContainerMain.Panel2.Controls.Add(this.rtbASCII);
            this.splitContainerMain.Panel2.Padding = new System.Windows.Forms.Padding(3);
            this.splitContainerMain.Size = new System.Drawing.Size(599, 367);
            this.splitContainerMain.SplitterDistance = 105;
            this.splitContainerMain.SplitterWidth = 2;
            this.splitContainerMain.TabIndex = 6;
            // 
            // rtbHex
            // 
            this.rtbHex.AutoWordSelection = true;
            this.rtbHex.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(64)))), ((int)(((byte)(64)))), ((int)(((byte)(64)))));
            this.rtbHex.BorderStyle = System.Windows.Forms.BorderStyle.None;
            this.rtbHex.DetectUrls = false;
            this.rtbHex.Dock = System.Windows.Forms.DockStyle.Fill;
            this.rtbHex.Font = new System.Drawing.Font("Lucida Console", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.rtbHex.ForeColor = System.Drawing.Color.White;
            this.rtbHex.HideSelection = false;
            this.rtbHex.Location = new System.Drawing.Point(3, 3);
            this.rtbHex.Name = "rtbHex";
            this.rtbHex.ReadOnly = true;
            this.rtbHex.ScrollBars = System.Windows.Forms.RichTextBoxScrollBars.ForcedVertical;
            this.rtbHex.Size = new System.Drawing.Size(97, 359);
            this.rtbHex.TabIndex = 7;
            this.rtbHex.Text = "";
            this.rtbHex.WordWrap = false;
            this.rtbHex.vScroll += new HexViewer.SynchronizedScrollRichTextBox.vScrollEventHandler(this.rtbHex_vScroll);
            this.rtbHex.SelectionChanged += new System.EventHandler(this.rtb_SelectionChanged);
            // 
            // rtbASCII
            // 
            this.rtbASCII.AutoWordSelection = true;
            this.rtbASCII.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(64)))), ((int)(((byte)(64)))), ((int)(((byte)(64)))));
            this.rtbASCII.BorderStyle = System.Windows.Forms.BorderStyle.None;
            this.rtbASCII.DetectUrls = false;
            this.rtbASCII.Dock = System.Windows.Forms.DockStyle.Fill;
            this.rtbASCII.Font = new System.Drawing.Font("Lucida Console", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.rtbASCII.ForeColor = System.Drawing.Color.White;
            this.rtbASCII.HideSelection = false;
            this.rtbASCII.Location = new System.Drawing.Point(3, 3);
            this.rtbASCII.Name = "rtbASCII";
            this.rtbASCII.ReadOnly = true;
            this.rtbASCII.ScrollBars = System.Windows.Forms.RichTextBoxScrollBars.ForcedVertical;
            this.rtbASCII.Size = new System.Drawing.Size(484, 359);
            this.rtbASCII.TabIndex = 5;
            this.rtbASCII.Text = "";
            this.rtbASCII.WordWrap = false;
            this.rtbASCII.vScroll += new HexViewer.SynchronizedScrollRichTextBox.vScrollEventHandler(this.rtbASCII_vScroll);
            this.rtbASCII.SelectionChanged += new System.EventHandler(this.rtb_SelectionChanged);
            // 
            // SRCHexViewer
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.AutoSize = true;
            this.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(64)))), ((int)(((byte)(64)))), ((int)(((byte)(64)))));
            this.Controls.Add(this.splitContainerMain);
            this.Controls.Add(this.pgbStatus);
            this.Name = "SRCHexViewer";
            this.Padding = new System.Windows.Forms.Padding(2);
            this.Size = new System.Drawing.Size(603, 381);
            this.splitContainerMain.Panel1.ResumeLayout(false);
            this.splitContainerMain.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.splitContainerMain)).EndInit();
            this.splitContainerMain.ResumeLayout(false);
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.ProgressBar pgbStatus;
        private SynchronizedScrollRichTextBox rtbASCII;
        private SynchronizedScrollRichTextBox rtbHex;
        private System.Windows.Forms.SplitContainer splitContainerMain;

    }
}

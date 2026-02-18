namespace Blobset_Tools
{
    partial class About
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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(About));
            changelog_tabControl = new TabControl();
            credits_tabPage = new TabPage();
            credits_richTextBox = new RichTextBox();
            info_tabPage = new TabPage();
            info_richTextBox = new RichTextBox();
            license_tabPage = new TabPage();
            licence_richTextBox = new RichTextBox();
            changelog_tabPage = new TabPage();
            changelog_richTextBox = new RichTextBox();
            title_pictureBox = new PictureBox();
            close_button = new Button();
            title_label = new Label();
            version_label = new Label();
            copyright_label = new Label();
            changelog_tabControl.SuspendLayout();
            credits_tabPage.SuspendLayout();
            info_tabPage.SuspendLayout();
            license_tabPage.SuspendLayout();
            changelog_tabPage.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)title_pictureBox).BeginInit();
            SuspendLayout();
            // 
            // changelog_tabControl
            // 
            changelog_tabControl.Controls.Add(credits_tabPage);
            changelog_tabControl.Controls.Add(info_tabPage);
            changelog_tabControl.Controls.Add(license_tabPage);
            changelog_tabControl.Controls.Add(changelog_tabPage);
            changelog_tabControl.Font = new Font("Segoe UI", 10F);
            changelog_tabControl.Location = new Point(11, 112);
            changelog_tabControl.Margin = new Padding(2, 3, 2, 3);
            changelog_tabControl.Name = "changelog_tabControl";
            changelog_tabControl.SelectedIndex = 0;
            changelog_tabControl.Size = new Size(550, 297);
            changelog_tabControl.TabIndex = 0;
            // 
            // credits_tabPage
            // 
            credits_tabPage.Controls.Add(credits_richTextBox);
            credits_tabPage.Location = new Point(4, 26);
            credits_tabPage.Margin = new Padding(2, 3, 2, 3);
            credits_tabPage.Name = "credits_tabPage";
            credits_tabPage.Padding = new Padding(2, 3, 2, 3);
            credits_tabPage.Size = new Size(542, 267);
            credits_tabPage.TabIndex = 0;
            credits_tabPage.Text = "Credits";
            credits_tabPage.UseVisualStyleBackColor = true;
            // 
            // credits_richTextBox
            // 
            credits_richTextBox.BackColor = SystemColors.Window;
            credits_richTextBox.Dock = DockStyle.Fill;
            credits_richTextBox.Font = new Font("Segoe UI", 10F);
            credits_richTextBox.Location = new Point(2, 3);
            credits_richTextBox.Margin = new Padding(2, 3, 2, 3);
            credits_richTextBox.Name = "credits_richTextBox";
            credits_richTextBox.ReadOnly = true;
            credits_richTextBox.Size = new Size(538, 261);
            credits_richTextBox.TabIndex = 0;
            credits_richTextBox.Text = resources.GetString("credits_richTextBox.Text");
            credits_richTextBox.WordWrap = false;
            // 
            // info_tabPage
            // 
            info_tabPage.Controls.Add(info_richTextBox);
            info_tabPage.Location = new Point(4, 26);
            info_tabPage.Margin = new Padding(2, 3, 2, 3);
            info_tabPage.Name = "info_tabPage";
            info_tabPage.Padding = new Padding(2, 3, 2, 3);
            info_tabPage.Size = new Size(542, 267);
            info_tabPage.TabIndex = 1;
            info_tabPage.Text = "Info";
            info_tabPage.UseVisualStyleBackColor = true;
            // 
            // info_richTextBox
            // 
            info_richTextBox.BackColor = SystemColors.Window;
            info_richTextBox.Dock = DockStyle.Fill;
            info_richTextBox.Location = new Point(2, 3);
            info_richTextBox.Margin = new Padding(2, 3, 2, 3);
            info_richTextBox.Name = "info_richTextBox";
            info_richTextBox.ReadOnly = true;
            info_richTextBox.Size = new Size(538, 261);
            info_richTextBox.TabIndex = 0;
            info_richTextBox.Text = "Description: Extract and Modify BigAnt games blobset files\n\nAuthor: Wouldubeinta\nDiscord ID: Wouldubeinta";
            // 
            // license_tabPage
            // 
            license_tabPage.Controls.Add(licence_richTextBox);
            license_tabPage.Location = new Point(4, 26);
            license_tabPage.Margin = new Padding(2, 3, 2, 3);
            license_tabPage.Name = "license_tabPage";
            license_tabPage.Padding = new Padding(2, 3, 2, 3);
            license_tabPage.Size = new Size(542, 267);
            license_tabPage.TabIndex = 2;
            license_tabPage.Text = "License";
            license_tabPage.UseVisualStyleBackColor = true;
            // 
            // licence_richTextBox
            // 
            licence_richTextBox.BackColor = SystemColors.Window;
            licence_richTextBox.Dock = DockStyle.Fill;
            licence_richTextBox.Location = new Point(2, 3);
            licence_richTextBox.Margin = new Padding(2, 3, 2, 3);
            licence_richTextBox.Name = "licence_richTextBox";
            licence_richTextBox.ReadOnly = true;
            licence_richTextBox.Size = new Size(538, 261);
            licence_richTextBox.TabIndex = 0;
            licence_richTextBox.Text = resources.GetString("licence_richTextBox.Text");
            // 
            // changelog_tabPage
            // 
            changelog_tabPage.Controls.Add(changelog_richTextBox);
            changelog_tabPage.Location = new Point(4, 26);
            changelog_tabPage.Margin = new Padding(2, 3, 2, 3);
            changelog_tabPage.Name = "changelog_tabPage";
            changelog_tabPage.Padding = new Padding(2, 3, 2, 3);
            changelog_tabPage.Size = new Size(542, 267);
            changelog_tabPage.TabIndex = 3;
            changelog_tabPage.Text = "Changelog";
            changelog_tabPage.UseVisualStyleBackColor = true;
            // 
            // changelog_richTextBox
            // 
            changelog_richTextBox.BackColor = SystemColors.Window;
            changelog_richTextBox.Dock = DockStyle.Fill;
            changelog_richTextBox.Location = new Point(2, 3);
            changelog_richTextBox.Margin = new Padding(2, 3, 2, 3);
            changelog_richTextBox.Name = "changelog_richTextBox";
            changelog_richTextBox.ReadOnly = true;
            changelog_richTextBox.Size = new Size(538, 261);
            changelog_richTextBox.TabIndex = 0;
            changelog_richTextBox.Text = resources.GetString("changelog_richTextBox.Text");
            changelog_richTextBox.WordWrap = false;
            // 
            // title_pictureBox
            // 
            title_pictureBox.Location = new Point(13, 13);
            title_pictureBox.Margin = new Padding(2, 3, 2, 3);
            title_pictureBox.Name = "title_pictureBox";
            title_pictureBox.Size = new Size(91, 93);
            title_pictureBox.SizeMode = PictureBoxSizeMode.Zoom;
            title_pictureBox.TabIndex = 1;
            title_pictureBox.TabStop = false;
            // 
            // close_button
            // 
            close_button.Location = new Point(488, 442);
            close_button.Margin = new Padding(2, 3, 2, 3);
            close_button.Name = "close_button";
            close_button.Size = new Size(69, 25);
            close_button.TabIndex = 2;
            close_button.Text = "Close";
            close_button.UseVisualStyleBackColor = true;
            close_button.Click += close_Click;
            // 
            // title_label
            // 
            title_label.AutoSize = true;
            title_label.Font = new Font("Arial Rounded MT Bold", 18F, FontStyle.Regular, GraphicsUnit.Point, 0);
            title_label.Location = new Point(201, 31);
            title_label.Margin = new Padding(2, 0, 2, 0);
            title_label.Name = "title_label";
            title_label.Size = new Size(169, 28);
            title_label.TabIndex = 3;
            title_label.Text = "Blobset Tools";
            // 
            // version_label
            // 
            version_label.AutoSize = true;
            version_label.Font = new Font("Segoe UI", 10F);
            version_label.Location = new Point(254, 70);
            version_label.Margin = new Padding(2, 0, 2, 0);
            version_label.Name = "version_label";
            version_label.Size = new Size(50, 19);
            version_label.TabIndex = 4;
            version_label.Text = "1.0.0.0";
            // 
            // copyright_label
            // 
            copyright_label.AutoSize = true;
            copyright_label.Location = new Point(388, 418);
            copyright_label.Margin = new Padding(2, 0, 2, 0);
            copyright_label.Name = "copyright_label";
            copyright_label.Size = new Size(167, 15);
            copyright_label.TabIndex = 5;
            copyright_label.Text = "Copyright: Wouldy Mods 2026";
            // 
            // About
            // 
            AutoScaleDimensions = new SizeF(96F, 96F);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(571, 478);
            Controls.Add(copyright_label);
            Controls.Add(version_label);
            Controls.Add(title_label);
            Controls.Add(close_button);
            Controls.Add(title_pictureBox);
            Controls.Add(changelog_tabControl);
            Icon = (Icon)resources.GetObject("$this.Icon");
            Margin = new Padding(2, 3, 2, 3);
            MaximizeBox = false;
            MaximumSize = new Size(587, 517);
            MinimizeBox = false;
            MinimumSize = new Size(587, 517);
            Name = "About";
            Text = "About";
            Load += About_Load;
            changelog_tabControl.ResumeLayout(false);
            credits_tabPage.ResumeLayout(false);
            info_tabPage.ResumeLayout(false);
            license_tabPage.ResumeLayout(false);
            changelog_tabPage.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)title_pictureBox).EndInit();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private TabControl changelog_tabControl;
        private TabPage credits_tabPage;
        private TabPage info_tabPage;
        private TabPage license_tabPage;
        private TabPage changelog_tabPage;
        private PictureBox title_pictureBox;
        private Button close_button;
        private Label title_label;
        private Label version_label;
        private Label copyright_label;
        private RichTextBox credits_richTextBox;
        private RichTextBox info_richTextBox;
        private RichTextBox licence_richTextBox;
        private RichTextBox changelog_richTextBox;
    }
}
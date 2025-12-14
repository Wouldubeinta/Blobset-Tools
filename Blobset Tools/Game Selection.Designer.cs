namespace Blobset_Tools
{
    partial class GameSelection
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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(GameSelection));
            GameSelection_ComboBox = new ComboBox();
            load_button = new Button();
            Platform_ComboBox = new ComboBox();
            label1 = new Label();
            label2 = new Label();
            pictureBox1 = new PictureBox();
            ofd = new OpenFileDialog();
            ((System.ComponentModel.ISupportInitialize)pictureBox1).BeginInit();
            SuspendLayout();
            // 
            // GameSelection_ComboBox
            // 
            GameSelection_ComboBox.BackColor = Color.Crimson;
            GameSelection_ComboBox.DrawMode = DrawMode.OwnerDrawVariable;
            GameSelection_ComboBox.DropDownStyle = ComboBoxStyle.DropDownList;
            GameSelection_ComboBox.Font = new Font("Calibri", 14.25F, FontStyle.Bold, GraphicsUnit.Point, 0);
            GameSelection_ComboBox.IntegralHeight = false;
            GameSelection_ComboBox.Location = new Point(174, 407);
            GameSelection_ComboBox.Name = "GameSelection_ComboBox";
            GameSelection_ComboBox.Size = new Size(463, 32);
            GameSelection_ComboBox.TabIndex = 2;
            GameSelection_ComboBox.DrawItem += GameSelection_comboBox_DrawItem;
            GameSelection_ComboBox.SelectedIndexChanged += GameSelection_comboBox_SelectedIndexChanged;
            // 
            // load_button
            // 
            load_button.BackColor = Color.Black;
            load_button.Location = new Point(275, 86);
            load_button.Name = "load_button";
            load_button.Size = new Size(256, 256);
            load_button.TabIndex = 0;
            load_button.UseVisualStyleBackColor = false;
            load_button.Click += load_button_Click;
            // 
            // Platform_ComboBox
            // 
            Platform_ComboBox.BackColor = Color.Crimson;
            Platform_ComboBox.DrawMode = DrawMode.OwnerDrawFixed;
            Platform_ComboBox.DropDownStyle = ComboBoxStyle.DropDownList;
            Platform_ComboBox.Font = new Font("RLFont", 14.2499981F, FontStyle.Bold, GraphicsUnit.Point, 0);
            Platform_ComboBox.IntegralHeight = false;
            Platform_ComboBox.Items.AddRange(new object[] { "Windows" });
            Platform_ComboBox.Location = new Point(671, 12);
            Platform_ComboBox.Name = "Platform_ComboBox";
            Platform_ComboBox.Size = new Size(147, 30);
            Platform_ComboBox.TabIndex = 1;
            Platform_ComboBox.DrawItem += Platform_ComboBox_DrawItem;
            Platform_ComboBox.SelectedIndexChanged += Platform_ComboBox_SelectedIndexChanged;
            // 
            // label1
            // 
            label1.AutoSize = true;
            label1.BackColor = Color.Transparent;
            label1.Font = new Font("RLFont Black", 12.749999F, FontStyle.Bold);
            label1.ForeColor = Color.White;
            label1.Location = new Point(582, 17);
            label1.Name = "label1";
            label1.Size = new Size(86, 20);
            label1.TabIndex = 3;
            label1.Text = "Platform :";
            // 
            // label2
            // 
            label2.AutoSize = true;
            label2.BackColor = Color.Transparent;
            label2.Font = new Font("RLFont Black", 12.749999F, FontStyle.Bold);
            label2.ForeColor = Color.White;
            label2.Location = new Point(110, 412);
            label2.Name = "label2";
            label2.Size = new Size(61, 20);
            label2.TabIndex = 4;
            label2.Text = "Game :";
            // 
            // pictureBox1
            // 
            pictureBox1.BackColor = Color.Transparent;
            pictureBox1.Image = Properties.Resources.Windows;
            pictureBox1.Location = new Point(12, 12);
            pictureBox1.Name = "pictureBox1";
            pictureBox1.Size = new Size(128, 128);
            pictureBox1.SizeMode = PictureBoxSizeMode.StretchImage;
            pictureBox1.TabIndex = 5;
            pictureBox1.TabStop = false;
            // 
            // ofd
            // 
            ofd.FileName = "data-0.blobset";
            ofd.Filter = "Blobset File|*.pc";
            ofd.Title = "Select Blobset File";
            // 
            // GameSelection
            // 
            AutoScaleDimensions = new SizeF(7F, 15F);
            AutoScaleMode = AutoScaleMode.Font;
            AutoScroll = true;
            BackgroundImage = (Image)resources.GetObject("$this.BackgroundImage");
            ClientSize = new Size(840, 498);
            Controls.Add(pictureBox1);
            Controls.Add(label2);
            Controls.Add(label1);
            Controls.Add(Platform_ComboBox);
            Controls.Add(load_button);
            Controls.Add(GameSelection_ComboBox);
            DoubleBuffered = true;
            Icon = (Icon)resources.GetObject("$this.Icon");
            Margin = new Padding(3, 2, 3, 2);
            MaximizeBox = false;
            MaximumSize = new Size(856, 537);
            Name = "GameSelection";
            Text = "Blobset Tools - Select Your Game";
            Load += GameSelection_Load;
            ((System.ComponentModel.ISupportInitialize)pictureBox1).EndInit();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private ComboBox GameSelection_ComboBox;
        private Button load_button;
        private ComboBox Platform_ComboBox;
        private Label label1;
        private Label label2;
        private PictureBox pictureBox1;
        private OpenFileDialog ofd;
    }
}
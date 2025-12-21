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
            FilePath_label = new Label();
            FileIndex_label = new Label();
            SuspendLayout();
            // 
            // FilePath_label
            // 
            FilePath_label.AutoSize = true;
            FilePath_label.Location = new Point(24, 53);
            FilePath_label.Name = "FilePath_label";
            FilePath_label.Size = new Size(55, 15);
            FilePath_label.TabIndex = 0;
            FilePath_label.Text = "File Path:";
            // 
            // FileIndex_label
            // 
            FileIndex_label.AutoSize = true;
            FileIndex_label.Location = new Point(24, 27);
            FileIndex_label.Name = "FileIndex_label";
            FileIndex_label.Size = new Size(59, 15);
            FileIndex_label.TabIndex = 1;
            FileIndex_label.Text = "File Index:";
            // 
            // File_Mapping_Editor
            // 
            AutoScaleDimensions = new SizeF(7F, 15F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(800, 450);
            Controls.Add(FileIndex_label);
            Controls.Add(FilePath_label);
            Name = "File_Mapping_Editor";
            Text = "File_Mapping_Editor";
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private Label FilePath_label;
        private Label FileIndex_label;
    }
}
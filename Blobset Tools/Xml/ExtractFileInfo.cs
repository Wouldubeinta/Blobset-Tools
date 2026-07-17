using System.Xml;
using System.Xml.Serialization;

namespace Blobset_Tools
{
    [Serializable()]
    public partial class ExtractFileInfo
    {
        #region Fields
        private int index = 0;
        private string folderHash = string.Empty;
        private string fileNameHash = string.Empty;
        private bool isCompressed = false;
        private Entry[]? entries = null;
        #endregion

        #region Properties
        [XmlAttribute()]
        public int Index
        {
            get { return index; }
            set { index = value; }
        }

        [XmlAttribute()]
        public string FolderHash 
        {
            get { return folderHash; }
            set { folderHash = value; }
        }

        [XmlAttribute()]
        public string FileNameHash
        {
            get { return fileNameHash; }
            set { fileNameHash = value; }
        }

        [XmlAttribute()]
        public bool IsCompressed
        {
            get { return isCompressed; }
            set { isCompressed = value; }
        }

        [XmlArrayItem("Entry", IsNullable = true)]
        public Entry[]? Entries
        {
            get { return entries; }
            set { entries = value; }
        }
        #endregion

        [Serializable()]
        public partial class Entry
        {
            [XmlAttribute()]
            public string? FilePath { get; set; }
        }
    }
}

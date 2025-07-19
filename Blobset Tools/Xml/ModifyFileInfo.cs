using System.Xml.Serialization;

namespace Blobset_Tools
{
    [Serializable()]
    public partial class ModifyFileInfo
    {
        #region Fields
        private int index = 0;
        private bool isCompressed = true;
        private int mainCompressedSize = 0;
        private int mainUnCompressedSize = 0;
        private int vramCompressedSize = 0;
        private int vramUnCompressedSize = 0;
        #endregion

        #region Properties
        [XmlAttribute()]
        public int Index
        {
            get { return index; }
            set { index = value; }
        }

        [XmlAttribute()]
        public bool IsCompressed
        {
            get { return isCompressed; }
            set { isCompressed = value; }
        }

        [XmlAttribute()]
        public int MainCompressedSize
        {
            get { return mainCompressedSize; }
            set { mainCompressedSize = value; }
        }

        [XmlAttribute()]
        public int MainUnCompressedSize
        {
            get { return mainUnCompressedSize; }
            set { mainUnCompressedSize = value; }
        }

        [XmlAttribute()]
        public int VramCompressedSize
        {
            get { return vramCompressedSize; }
            set { vramCompressedSize = value; }
        }

        [XmlAttribute()]
        public int VramUnCompressedSize
        {
            get { return vramUnCompressedSize; }
            set { vramUnCompressedSize = value; }
        }
        #endregion
    }
}

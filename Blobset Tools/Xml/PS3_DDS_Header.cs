using System.Xml.Serialization;

namespace Blobset_Tools
{
    [Serializable()]
    public partial class PS3_DDS_Header
    {
        #region Fields
        private int index = 0;
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
            public uint BufferSize { get; set; }
            public byte DDSType { get; set; }
            public byte DDSMipMaps { get; set; }
            public uint Unknown1 { get; set; }
            public ushort Unknown2 { get; set; }
            public ushort DDSWidth { get; set; }
            public ushort DDSHeight { get; set; }
            public ushort DDSImageType { get; set; }
            public ushort Unknown3 { get; set; }
            public ushort Unknown4 { get; set; }
            public ushort Unknown5 { get; set; }
            public uint Reserved { get; set; }
        }
    }
}

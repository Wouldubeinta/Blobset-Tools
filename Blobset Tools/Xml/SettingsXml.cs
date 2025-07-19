using System.Xml;
using System.Xml.Serialization;

namespace Blobset_Tools
{
    [Serializable()]
    public partial class SettingsXml
    {
        #region Fields
        private bool compression;
        private bool loadGame;
        private bool skipUnknown;
        private string? gameLocation;
        private int gameID;
        private string? gameName;
        private int blobsetVersion;
        private int steamGameID;
        #endregion

        #region Properties
        [XmlAttribute()]
        public bool Compression
        {
            get { return compression; }
            set { compression = value; }
        }

        [XmlAttribute()]
        public bool LoadGame
        {
            get { return loadGame; }
            set { loadGame = value; }
        }

        [XmlAttribute()]
        public bool SkipUnknown
        {
            get { return skipUnknown; }
            set { skipUnknown = value; }
        }

        [XmlAttribute()]
        public string GameLocation
        {
            get { return gameLocation; }
            set { gameLocation = value; }
        }

        [XmlAttribute()]
        public int GameID
        {
            get { return gameID; }
            set { gameID = value; }
        }

        [XmlAttribute()]
        public string GameName
        {
            get { return gameName; }
            set { gameName = value; }
        }

        [XmlAttribute()]
        public int BlobsetVersion
        {
            get { return blobsetVersion; }
            set { blobsetVersion = value; }
        }

        [XmlAttribute()]
        public int SteamGameID
        {
            get { return steamGameID; }
            set { steamGameID = value; }
        }
        #endregion
    }
}

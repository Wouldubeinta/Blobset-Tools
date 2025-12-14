namespace Blobset_Tools
{
    internal class GameInfo
    {
        #region Fields
        private string version = string.Empty;
        private int gameId = 0;
        private string gameName = string.Empty;
        private uint steamGameId = 0;
        private int blobsetVersion = 0;
        private string gameLocation = string.Empty;
        #endregion

        #region Properties
        public string Version
        {
            get { return version; }
            set { version = value; }
        }

        public int GameId
        {
            get { return gameId; }
            set { gameId = value; }
        }

        public string GameName
        {
            get { return gameName; }
            set { gameName = value; }
        }

        public uint SteamGameId
        {
            get { return steamGameId; }
            set { steamGameId = value; }
        }

        public int BlobsetVersion
        {
            get { return blobsetVersion; }
            set { blobsetVersion = value; }
        }

        public string GameLocation
        {
            get { return gameLocation; }
            set { gameLocation = value; }
        }
        #endregion

        #region "Deserialize"
        /// <summary>
        /// Deserialize GameInfo.
        /// </summary>
        /// <param name="location">GameInfo Ini location.</param>
        public void Deserialize(string location)
        {
            IniFile gameInfo = new(location);

            Version = gameInfo.Read("Version", "GameInfo");
            GameId = Convert.ToInt32(gameInfo.Read("GameId", "GameInfo"));
            GameName = gameInfo.Read("GameName", "GameInfo");
            SteamGameId = GameId > 2 ? Convert.ToUInt32(gameInfo.Read("SteamGameId", "GameInfo")) : 0;
            BlobsetVersion = Convert.ToInt32(gameInfo.Read("BlobsetVersion", "GameInfo"));
            GameLocation = gameInfo.Read("GameLocation", "GameInfo");
        }
        #endregion
    }
}

using System.Text.Json.Serialization;

namespace Blobset_Tools
{
    public partial class SteamLibraryFolders
    {
        #region Fields
        private Dictionary<string, LibraryFolder> libraryFolders = new();
        #endregion

        #region Properties
        [JsonPropertyName("libraryfolders")]
        public Dictionary<string, LibraryFolder> LibraryFolders
        {
            get { return libraryFolders; }
            set { libraryFolders = value; }
        }
        #endregion
    }

    public partial class LibraryFolder
    {
        #region Fields
        private string path = string.Empty;
        private string label = string.Empty;
        private string contentId = string.Empty;
        private string totalSize = string.Empty;
        private string updateCleanBytesTally = string.Empty;
        private string timeLastUpdateVerified = string.Empty;
        private Dictionary<string, string>? apps = new();
        #endregion

        #region Properties
        [JsonPropertyName("time_last_update_verified")]
        public string TimeLastUpdateVerified
        {
            get { return timeLastUpdateVerified; }
            set { timeLastUpdateVerified = value; }
        }

        [JsonPropertyName("update_clean_bytes_tally")]
        public string UpdateCleanBytesTally
        {
            get { return updateCleanBytesTally; }
            set { updateCleanBytesTally = value; }
        }

        [JsonPropertyName("totalsize")]
        public string TotalSize
        {
            get { return totalSize; }
            set { totalSize = value; }
        }

        [JsonPropertyName("contentid")]
        public string ContentId
        {
            get { return contentId; }
            set { contentId = value; }
        }

        [JsonPropertyName("label")]
        public string Label
        {
            get { return label; }
            set { label = value; }
        }

        [JsonPropertyName("path")]
        public string Path
        {
            get { return path; }
            set { path = value; }
        }

        [JsonPropertyName("apps")]
        public Dictionary<string, string>? Apps
        {
            get { return apps; }
            set { apps = value; }
        }
        #endregion
    }
}

using System.ComponentModel;

namespace HexViewer
{
    // event arguments
    public class ProgressChangedEventArgs : EventArgs
    {
        public float Percentage = 0f;
        public int ByteIndex = -1;
        public int BytesTotal = -1;
    }
    public class LoadCompletedEventArgs : EventArgs
    {
        public string Tag = null;
        public bool IsCancelled = false;
    }
    public class SelectedByteChangedEventArgs : EventArgs
    {
        public int Index = -1;
        public byte SelectedByte = 0;
        public bool IsInvalidSelection = true;
    }
    public class SearchCompletedEventArgs : EventArgs
    {
        public int TotalMatches = 0;
    }

    // delegates
    public delegate void ProgressChangedEventHandler(object sender, ProgressChangedEventArgs e);
    public delegate void LoadCompletedEventHandler(object sender, LoadCompletedEventArgs e);
    public delegate void SelectedByteIndexChangedEventHandler(object sender, SelectedByteChangedEventArgs e);
    public delegate void SearchCompletedEventHandler(object sender, SearchCompletedEventArgs e);

    public partial class SRCHexViewer : UserControl
    {
        // events
        [Description("Occurs when data loading's progress has changed.")]
        public event ProgressChangedEventHandler ProgressChanged;
        [Description("Occurs when a load process has been completed.")]
        public event LoadCompletedEventHandler LoadCompleted;
        [Description("Occurs when selected byte index has changed.")]
        public event SelectedByteIndexChangedEventHandler SelectedByteIndexChanged;
        [Description("Occurs when a search indexing has been completed.")]
        public event SearchCompletedEventHandler SearchCompleted;

        // mappers
        protected virtual void OnProgressChanged(float progressPercentage, int byteIndex, int bytesTotal)
        {
            ProgressChangedEventArgs args = new();
            args.Percentage = progressPercentage;
            args.ByteIndex = byteIndex;
            args.BytesTotal = bytesTotal;
            if (ProgressChanged != null) ProgressChanged(this, args);
        }
        protected virtual void OnLoadCompleted(string processTag, bool isCancelled)
        {
            LoadCompletedEventArgs args = new();
            args.Tag = processTag;
            args.IsCancelled = isCancelled;
            if (LoadCompleted != null) LoadCompleted(this, args);
        }
        protected virtual void OnSelectedByteIndexChanged(int selByteIndex, byte selByte,
            bool isInvalidSelection = false)
        {
            SelectedByteChangedEventArgs args = new();
            args.Index = selByteIndex;
            args.SelectedByte = selByte;
            args.IsInvalidSelection = isInvalidSelection;
            if (SelectedByteIndexChanged != null) SelectedByteIndexChanged(this, args);
        }
        protected virtual void OnSearchCompleted(int totalMatches)
        {
            SearchCompletedEventArgs args = new();
            args.TotalMatches = totalMatches;
            if (SearchCompleted != null) SearchCompleted(this, args);
        }
    }
}

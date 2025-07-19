namespace HexViewer
{
    public static class Tools
    {
        public static Color InvertColor(Color color)
        {
            return Color.FromArgb(color.ToArgb() ^ 0xFFFFFF);
        }
    }
}

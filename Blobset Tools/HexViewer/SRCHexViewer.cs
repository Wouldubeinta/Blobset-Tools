namespace HexViewer
{
    public partial class SRCHexViewer : UserControl
    {
        // for hex display
        private const int HEX_CHAR_PER_BYTE = 3;
        private const int LINE_NUMBER_LENGTH = 5;
        private const int MAX_LINE_NUMBER = 9999;
        private const int MAX_LINE_NUMBER_HEX = 0xFFFF;
        private const int PHYSICAL_LINE_LENGTH = 42;
        private const int PHYSICAL_NUMBERED_LINE_LENGTH = PHYSICAL_LINE_LENGTH + 50;
        private const int PHYSICAL_LINE_VALUE_LENGTH = 21;

        private int bytesPerLine, charPerLine;
        private bool isLoading = false, isCancelled = false, isScrolling = false;
        private float loadingPercentage = 0f;
        private readonly List<byte> loadedData = new();
        private List<byte> allData = new();
        private char hexDataSeparator = ' ', hexCenterSeparator = ' ';
        private int selectedByteIndex = 0;
        // search routers
        private byte? searchByte = null;
        private int searchIndex = 0;
        private readonly List<int> searchMatchIndices = new();

        public SRCHexViewer()
        {
            InitializeComponent();

            // default properties
            HexDataSeparator = ' ';
            HexCenterSeparator = '-';
            IsAlternateLineColor = true;
            AlternateBackColor1 = Color.FromArgb(100, 100, 100);
            AlternateBackColor2 = Color.FromArgb(64, 64, 64);
            AlternateTextColor1 = Color.White;
            AlternateTextColor2 = Color.White;
            LoadingDelay = 0;
            ShowLineNumber = true;
            BytesPerLine = 16;
            LineNumberBackColor = Color.Black;
            LineNumberForeColor = Color.White;
            AutoSelectCharacter = true;
            HexLineNumbers = true;
            ShowByteIndexInsteadOfLineNumbers = false;
            NotifyIntegralProgressOnly = true;

            Disposed += SRCHexViewer_Disposed;
        }

        private int getASCIIEquivalentIndex(int hexIndex)
        {
            int charPerLine = bytesPerLine * HEX_CHAR_PER_BYTE; // including newline character
            if (ShowLineNumber) charPerLine += (LINE_NUMBER_LENGTH + 2);
            int hexIndexInLine = hexIndex % charPerLine; // 0-based
            if (ShowLineNumber)
            {
                hexIndexInLine -= (LINE_NUMBER_LENGTH + 2); // truncate line number bytes count
                if (hexIndexInLine < 0) return -1; // user selects beyond line numbers display
            }
            int hexLineIndex = hexIndex / charPerLine; // 0-based

            int asciiIndexInLine = hexIndexInLine / HEX_CHAR_PER_BYTE; // 0-based
            if (bytesPerLine > 1 && asciiIndexInLine >= bytesPerLine / 2) asciiIndexInLine++;
            // chars from previous rows, add middle separator if required
            int asciiIndex = (bytesPerLine + ((bytesPerLine > 1) ? 1 : 0)) * hexLineIndex;
            if (hexLineIndex > 0) asciiIndex += hexLineIndex; // add newline characters count
            asciiIndex += asciiIndexInLine;

            return asciiIndex;
        }

        private int getHexEquivalentIndex(int asciiIndex)
        {
            int charPerLine = bytesPerLine + 1; // plus newline character
            if (bytesPerLine > 1) charPerLine++; // plus middle separator
            int asciiIndexInLine = asciiIndex % charPerLine;
            int asciiLineIndex = asciiIndex / charPerLine; // 0-based
            if (bytesPerLine > 1 && asciiIndexInLine >= bytesPerLine / 2) asciiIndexInLine--; // remove middle separator in count

            int hexIndexInLine = asciiIndexInLine * HEX_CHAR_PER_BYTE;
            if (ShowLineNumber) hexIndexInLine += (LINE_NUMBER_LENGTH + 2);
            int hexCharPerLine = bytesPerLine * HEX_CHAR_PER_BYTE; // including newline
            if (ShowLineNumber) hexCharPerLine += (LINE_NUMBER_LENGTH + 2);
            int hexIndex = hexIndexInLine + (hexCharPerLine * asciiLineIndex);

            // experimental fix of 'beyond line number auto-selection' error when end of line was selected :D
            if (hexIndex != 0 && hexIndexInLine != 0 && (hexIndex % hexCharPerLine) == 0) hexIndex -= 3;

            return hexIndex;
        }

        private int getASCIIIndexOfByteAt(int byteIndex)
        {
            int lineIndex = byteIndex / bytesPerLine; // 0-based
            int indexInLine = byteIndex % bytesPerLine;

            int charPerLine = bytesPerLine + 1; // plus newline character
            if (bytesPerLine > 1) charPerLine++; // plus newline character
            int asciiIndex = (charPerLine * lineIndex) + indexInLine;
            if (bytesPerLine > 1 && indexInLine >= bytesPerLine / 2) asciiIndex++; // add middle separator
            return asciiIndex;
        }

        private int getHexIndexOfByteAt(int byteIndex)
        {
            return getHexEquivalentIndex(getASCIIIndexOfByteAt(byteIndex));
        }

        private void setSelectedByteIndexFromASCIIView()
        {
            int asciiIndex = rtbASCII.SelectionStart;
            if (asciiIndex < 0)
            {
                selectedByteIndex = asciiIndex;
                return;
            }

            int charPerLine = bytesPerLine + 1; // plus newline character
            if (bytesPerLine > 1) charPerLine++; // plus middle separator
            int asciiIndexInLine = asciiIndex % charPerLine;
            int asciiLineIndex = asciiIndex / charPerLine; // 0-based
            if (bytesPerLine > 1 && asciiIndexInLine >= bytesPerLine / 2) asciiIndexInLine--; // remove middle separator in count

            int byteIndex = asciiIndexInLine + (asciiLineIndex * bytesPerLine);
            // check if the last index of line was selected
            // must select last byte in line, not the first byte on next line.
            if (asciiIndexInLine == (charPerLine - 2)) // minus newline and middle separators
                byteIndex--;
            if (byteIndex != selectedByteIndex)
            {
                selectedByteIndex = byteIndex;
                if (selectedByteIndex < loadedData.Count && selectedByteIndex >= 0)
                    OnSelectedByteIndexChanged(byteIndex, loadedData[byteIndex]);
                else OnSelectedByteIndexChanged(-1, 0, true);
            }
        }

        private void SRCHexViewer_Disposed(object sender, EventArgs e)
        {
            CancelLoading();
        }

        private void rtbHex_vScroll(Message message)
        {
            if (!SynchronizeScrolling) return;
            message.HWnd = rtbASCII.Handle;
            isScrolling = true;
            rtbASCII.PubWndProc(ref message);
            isScrolling = false;
        }

        private void rtbASCII_vScroll(Message message)
        {
            if (!SynchronizeScrolling) return;
            message.HWnd = rtbHex.Handle;
            isScrolling = true;
            rtbHex.PubWndProc(ref message);
            isScrolling = false;
        }

        private void rtb_SelectionChanged(object sender, EventArgs e)
        {
            if (isLoading || isScrolling) return;

            int selLineHex =
                rtbHex.GetLineFromCharIndex(rtbHex.GetCharIndexFromPosition(new Point(0, 0)));
            int selLineASCII =
                rtbASCII.GetLineFromCharIndex(rtbASCII.GetCharIndexFromPosition(new Point(0, 0)));
            if (selLineHex != selLineASCII)
            {
                if (rtbHex.Focused)
                {
                    rtbASCII.SelectionChanged -= rtb_SelectionChanged;
                    rtbASCII.SelectionStart = rtbASCII.GetFirstCharIndexFromLine(selLineHex);
                    rtbASCII.ScrollToCaret();
                    setSelectedByteIndexFromASCIIView();
                    rtbASCII.SelectionChanged += rtb_SelectionChanged;
                }
                else if (rtbASCII.Focused)
                {
                    rtbHex.SelectionChanged -= rtb_SelectionChanged;
                    rtbHex.SelectionStart = rtbHex.GetFirstCharIndexFromLine(selLineASCII);
                    rtbHex.ScrollToCaret();
                    setSelectedByteIndexFromASCIIView();
                    rtbHex.SelectionChanged += rtb_SelectionChanged;
                }
            }
            else if (AutoSelectCharacter)
            { // highlight corresponding hex/ascii character
                if (rtbHex.Focused)
                {
                    int asciiIndex = getASCIIEquivalentIndex(rtbHex.SelectionStart);
                    rtbASCII.SelectionChanged -= rtb_SelectionChanged;
                    if (asciiIndex == -1)
                    { // invalid selection in hex view
                        rtbASCII.SelectionLength = 0;
                    }
                    else
                    {
                        rtbASCII.SelectionStart = asciiIndex;
                        rtbASCII.SelectionLength = 1;
                    }
                    setSelectedByteIndexFromASCIIView();
                    rtbASCII.SelectionChanged += rtb_SelectionChanged;
                }
                else if (rtbASCII.Focused)
                {
                    int hexIndex = getHexEquivalentIndex(rtbASCII.SelectionStart);
                    rtbHex.SelectionChanged -= rtb_SelectionChanged;
                    if (hexIndex == -1)
                    { // invalid selection in ASCII view
                        rtbHex.SelectionLength = 0;
                    }
                    else
                    {
                        rtbHex.SelectionStart = hexIndex;
                        rtbHex.SelectionLength = 2;
                    }
                    setSelectedByteIndexFromASCIIView();
                    rtbHex.SelectionChanged += rtb_SelectionChanged;
                }
            }
        }

    }
}

using System.Text;

namespace HexViewer
{
    public partial class SRCHexViewer : UserControl
    {
        public void LoadData(byte[] data, string processTag = null)
        {
            Invoke((MethodInvoker)delegate
            {
                rtbHex.Clear();
                rtbASCII.Clear();
                loadedData.Clear();
                searchMatchIndices.Clear();
                allData.Clear();
                allData = data.ToList();

                loadingPercentage = 0f;
                if (data.Length == 0) return;

                isLoading = true;
                isCancelled = false;
                if (ShowProgressBar) pgbStatus.Visible = true;
                pgbStatus.Maximum = data.Length;
                pgbStatus.Value = 0;

                int lineCount = 1;
                bool firstHighlight = false;
                int startHighlightIndexASCII = 0, startHighlightIndexHex = 0;

                #region Line Number at First Line
                if (ShowLineNumber)
                {
                    if (ShowByteIndexInsteadOfLineNumbers)
                        rtbHex.AppendText((HexLineNumbers ? "0000" : "0").PadLeft(LINE_NUMBER_LENGTH, ' '));
                    else
                        rtbHex.AppendText(lineCount.ToString(HexLineNumbers ? "X4" : "0").PadLeft(LINE_NUMBER_LENGTH, ' '));
                    rtbHex.SelectionStart = startHighlightIndexHex;
                    rtbHex.SelectionLength = LINE_NUMBER_LENGTH;
                    rtbHex.SelectionColor = LineNumberForeColor;
                    rtbHex.SelectionBackColor = LineNumberBackColor;
                    rtbHex.AppendText(" ");
                    startHighlightIndexHex = rtbHex.SelectionStart;
                    rtbHex.AppendText(" ");
                }
                #endregion

                for (int i = 0; i < data.Length; ++i)
                {
                    if (isCancelled) break;

                    float currLoadingPercentage = (float)((i / (float)data.Length) * 100.0);
                    if (NotifyIntegralProgressOnly)
                    {
                        if (Convert.ToInt32(Math.Floor(loadingPercentage)) !=
                            Convert.ToInt32(Math.Floor(currLoadingPercentage)))
                            OnProgressChanged((float)Math.Floor(currLoadingPercentage), i, data.Length);
                    }
                    else
                    {
                        OnProgressChanged(currLoadingPercentage, i, data.Length);
                    }
                    loadingPercentage = currLoadingPercentage;
                    byte b = data[i];
                    rtbHex.AppendText(b.ToString("X2"));
                    if (char.IsControl((char)b) || b == 0) rtbASCII.AppendText(".");
                    else rtbASCII.AppendText(((char)b).ToString());

                    int byteInCurrentLine = (i + 1) % bytesPerLine;
                    if (byteInCurrentLine == 0)
                    { // apply line color and insert newline
                        #region Highlight Current Line in Hex and ASCII view
                        // hex view highlighting
                        rtbHex.SelectionStart = startHighlightIndexHex;
                        rtbHex.SelectionLength = charPerLine;
                        if (IsAlternateLineColor)
                        {
                            rtbHex.SelectionBackColor = firstHighlight ? AlternateBackColor1 : AlternateBackColor2;
                            rtbHex.SelectionColor = firstHighlight ? AlternateTextColor1 : AlternateTextColor2;
                        }
                        rtbHex.AppendText(Environment.NewLine);
                        startHighlightIndexHex = rtbHex.SelectionStart;

                        // ASCII view highlighting
                        rtbASCII.SelectionStart = startHighlightIndexASCII;
                        rtbASCII.SelectionLength = charPerLine;
                        if (IsAlternateLineColor)
                        {
                            rtbASCII.SelectionBackColor = firstHighlight ? AlternateBackColor1 : AlternateBackColor2;
                            rtbASCII.SelectionColor = firstHighlight ? AlternateTextColor1 : AlternateTextColor2;
                        }
                        rtbASCII.AppendText(Environment.NewLine);
                        startHighlightIndexASCII = rtbASCII.SelectionStart;

                        firstHighlight = !firstHighlight;

                        ++lineCount;
                        if (lineCount > (HexLineNumbers ? MAX_LINE_NUMBER_HEX : MAX_LINE_NUMBER)) lineCount = 1;

                        #region Insert Line Number in Next Line
                        if (ShowLineNumber)
                        {
                            if (ShowByteIndexInsteadOfLineNumbers && i < data.Length)
                                rtbHex.AppendText(i.ToString(HexLineNumbers ? "X4" : "0").PadLeft(LINE_NUMBER_LENGTH, ' '));
                            else
                                rtbHex.AppendText(lineCount.ToString(HexLineNumbers ? "X4" : "0").PadLeft(LINE_NUMBER_LENGTH, ' '));
                            rtbHex.SelectionStart = startHighlightIndexHex;
                            rtbHex.SelectionLength = LINE_NUMBER_LENGTH;
                            rtbHex.SelectionColor = LineNumberForeColor;
                            rtbHex.SelectionBackColor = LineNumberBackColor;
                            rtbHex.AppendText(" ");
                            startHighlightIndexHex = rtbHex.SelectionStart;
                            rtbHex.AppendText(" ");
                        }
                        #endregion
                        #endregion
                    }
                    else if (i != data.Length - 1)
                    { // separator
                        if (byteInCurrentLine == bytesPerLine / 2)
                        {
                            rtbHex.AppendText(HexCenterSeparator.ToString());
                            rtbASCII.AppendText(" ");
                        }
                        else rtbHex.AppendText(HexDataSeparator.ToString());
                    }
                    loadedData.Add(b);

                    pgbStatus.Increment(1);
                    System.Threading.Thread.Sleep(LoadingDelay);
                    Application.DoEvents();
                }

                if (IsDisposed) return;

                if (rtbHex.SelectionStart != startHighlightIndexHex)
                { // final highlighting, pad trailing spaces if required
                    #region Highlight and Pad Remaining Line of Bytes in Hex and ASCII view
                    // hex view
                    rtbHex.AppendText(new string(' ', charPerLine -
                        (rtbHex.SelectionStart - startHighlightIndexHex)));
                    rtbHex.SelectionStart = startHighlightIndexHex;
                    rtbHex.SelectionLength = charPerLine;
                    if (IsAlternateLineColor)
                    {
                        rtbHex.SelectionBackColor = firstHighlight ? AlternateBackColor1 : AlternateBackColor2;
                        rtbHex.SelectionColor = firstHighlight ? AlternateTextColor1 : AlternateTextColor2;
                    }
                    // ASCII view
                    rtbASCII.AppendText(new string(' ', (bytesPerLine + 1) -
                        (rtbASCII.SelectionStart - startHighlightIndexASCII)));
                    rtbASCII.SelectionStart = startHighlightIndexASCII;
                    rtbASCII.SelectionLength = bytesPerLine + 1;
                    if (IsAlternateLineColor)
                    {
                        rtbASCII.SelectionBackColor = firstHighlight ? AlternateBackColor1 : AlternateBackColor2;
                        rtbASCII.SelectionColor = firstHighlight ? AlternateTextColor1 : AlternateTextColor2;
                    }
                    #endregion
                }

                rtbHex.SelectionStart = 0;
                rtbHex.SelectionLength = 0;
                rtbASCII.SelectionStart = 0;
                rtbASCII.SelectionLength = 0;
                pgbStatus.Visible = false;
                rtbASCII.ScrollToCaret();

                isLoading = false;
                OnLoadCompleted(processTag, isCancelled);
            });
        }

        public void LoadString(string data, Encoding encoding = null)
        {
            LoadData(Encoding.UTF8.GetBytes(data));
        }

        public void ClearContent()
        {
            rtbHex.Clear();
            rtbASCII.Clear();
            loadedData.Clear();
            allData.Clear();
            isCancelled = false;
            isLoading = false;
        }

        public void CancelLoading()
        {
            isCancelled = true;
        }

        public void ReloadData(bool loadedDataOnly = false, string processTag = null)
        {
            LoadData(loadedDataOnly ? loadedData.ToArray() : allData.ToArray(), processTag);
        }

        public void SelectByteAtIndex(int byteIndex)
        {
            if (selectedByteIndex == byteIndex) return;
            if (byteIndex >= loadedData.Count || byteIndex < 0)
                throw new ArgumentOutOfRangeException("byteIndex");

            rtbASCII.SelectionChanged -= rtb_SelectionChanged;
            rtbASCII.SelectionStart = getASCIIIndexOfByteAt(byteIndex);
            rtbASCII.SelectionLength = 1;
            rtbASCII.SelectionChanged += rtb_SelectionChanged;
            rtbHex.SelectionChanged -= rtb_SelectionChanged;
            rtbHex.SelectionStart = getHexIndexOfByteAt(byteIndex);
            rtbHex.SelectionLength = 2;
            rtbHex.SelectionChanged += rtb_SelectionChanged;
            selectedByteIndex = byteIndex;
            OnSelectedByteIndexChanged(byteIndex, loadedData[byteIndex]);
        }

        public int GetSelectedByteIndex()
        { // work in ASCII view
            return selectedByteIndex;
        }

        public int SearchFirst(string hexString)
        {
            if (hexString.Length > 2)
                hexString = hexString.Substring(0, 1); // truncate, only 1 byte is needed
            return SearchFirst(byte.Parse(hexString, System.Globalization.NumberStyles.HexNumber));
        }

        public int SearchFirst(char ch)
        {
            return SearchFirst((byte)ch); // search on ASCII view
        }

        public int SearchFirst(byte b)
        {
            searchMatchIndices.Clear();

            for (int i = 0; i < loadedData.Count; ++i)
            { // collect matching indices
                if (loadedData[i] == b)
                    searchMatchIndices.Add(i);
                Application.DoEvents();
            }

            OnSearchCompleted(searchMatchIndices.Count);
            searchIndex = 0;
            searchByte = b;
            if (searchMatchIndices.Count > 0)
            {
                SelectByteAtIndex(searchMatchIndices[searchIndex]);
                return 0;
            }
            else return -1;
        }

        public int SearchNext(bool moveToFirst = false)
        {
            if (searchByte == null) throw new InvalidOperationException("Search has not yet initialized.");
            if (searchIndex == searchMatchIndices.Count) return -1;
            searchIndex++;
            if (searchIndex == searchMatchIndices.Count)
            {
                if (moveToFirst) searchIndex = 0;
                else return -1;
            }
            SelectByteAtIndex(searchMatchIndices[searchIndex]);

            return searchIndex;
        }

        public int SearchPrevious(bool moveToLast = false)
        {
            if (searchByte == null) throw new InvalidOperationException("Search has not yet initialized.");
            if (searchIndex == -1) return -1;

            searchIndex--;
            if (searchIndex == -1)
            {
                if (moveToLast) searchIndex = (searchMatchIndices.Count - 1);
                else return -1;
            }
            SelectByteAtIndex(searchMatchIndices[searchIndex]);

            return searchIndex;
        }

        public int GetSearchIndex()
        {
            if (searchByte == null) return -1;
            else return searchIndex;
        }

        public void EditByteAtIndex(int byteIndex, byte newValue)
        {
            if (byteIndex >= loadedData.Count || byteIndex < 0)
                throw new ArgumentOutOfRangeException("byteIndex");

            loadedData[byteIndex] = newValue;

            // change byte at ASCII view
            int prevSelIndex = rtbASCII.SelectionStart;
            int prevSelLength = rtbASCII.SelectionLength;
            rtbASCII.SelectionChanged -= rtb_SelectionChanged;
            rtbASCII.SelectionStart = getASCIIIndexOfByteAt(byteIndex);
            rtbASCII.SelectionLength = 1;
            if (char.IsControl((char)newValue) || newValue == 0) rtbASCII.SelectedText = ".";
            else rtbASCII.SelectedText = ((char)newValue).ToString();
            rtbASCII.SelectionStart = prevSelIndex;
            rtbASCII.SelectionLength = prevSelLength;
            rtbASCII.SelectionChanged += rtb_SelectionChanged;

            // change byte at Hex view
            prevSelIndex = rtbHex.SelectionStart;
            prevSelLength = rtbHex.SelectionLength;
            rtbHex.SelectionChanged -= rtb_SelectionChanged;
            rtbHex.SelectionStart = getHexIndexOfByteAt(byteIndex);
            rtbHex.SelectionLength = 2;
            rtbHex.SelectedText = newValue.ToString("X2");
            rtbHex.SelectionStart = prevSelIndex;
            rtbHex.SelectionLength = prevSelLength;
            rtbHex.SelectionChanged += rtb_SelectionChanged;
        }

        #region Getter Methods
        public int GetDataLineCount() { return rtbHex.Lines.Count(); }
        public string GetHexContent() { return rtbHex.Text.Replace("\n", "\r\n"); }
        public string GetTextContent() { return rtbASCII.Text.Replace("\n", "\r\n"); }
        public byte[] GetAllData() { return allData.ToArray(); }
        public int GetAllDataCount() { return allData.Count; }
        public bool IsCancelled() { return isCancelled; }
        public byte[] GetLoadedData() { return loadedData.ToArray(); }
        public int GetLoadedBytesCount() { return loadedData.Count; }
        public byte GetByteAtIndex(int index) { return loadedData[index]; }
        public bool IsLoading() { return isLoading; }
        public float GetLoadingPercentage() { return loadingPercentage; }
        #endregion
    }
}

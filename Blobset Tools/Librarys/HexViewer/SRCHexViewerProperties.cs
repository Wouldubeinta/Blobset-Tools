using System.ComponentModel;

namespace HexViewer
{
    public partial class SRCHexViewer : UserControl
    {
        #region Added Properties

        [Browsable(true)]
        [Description("Number of bytes to display per line. (Must be set after setting ShowLineNumber property.)")]
        public int BytesPerLine
        {
            get { return bytesPerLine; }
            set
            {
                if (value < 1)
                    throw new InvalidOperationException("Invalid value set to 'BytesPerLine' property");

                int splitDistance = (ShowLineNumber ? PHYSICAL_NUMBERED_LINE_LENGTH : PHYSICAL_LINE_LENGTH)
                    + ((value - 1) * PHYSICAL_LINE_VALUE_LENGTH);
                splitContainerMain.IsSplitterFixed = false;
                splitContainerMain.SplitterDistance = splitDistance;
                splitContainerMain.IsSplitterFixed = true;
                bytesPerLine = value;
                charPerLine = (bytesPerLine * HEX_CHAR_PER_BYTE);
            }
        }

        [Browsable(true)]
        [Description("Auto-select corresponding hex/ascii character in opposite view.")]
        public bool AutoSelectCharacter
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Indicates whether to synchronize scrolling of each view.")]
        public bool SynchronizeScrolling
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Indicates whether to display horizontal scrollbars.")]
        public bool ShowHorizontalScrollbars
        {
            get
            {
                return (rtbHex.ScrollBars == RichTextBoxScrollBars.ForcedBoth);
            }
            set
            {
                rtbHex.ScrollBars =
                    value ? RichTextBoxScrollBars.ForcedBoth : RichTextBoxScrollBars.ForcedVertical;
                rtbASCII.ScrollBars =
                    value ? RichTextBoxScrollBars.ForcedBoth : RichTextBoxScrollBars.ForcedVertical;
            }
        }

        [Browsable(true)]
        [Description("Delay in milliseconds for every byte of data to load.")]
        public int LoadingDelay
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Hide/Show control's progressbar when loading data.")]
        public bool ShowProgressBar
        {
            get { return pgbStatus.Visible; }
            set { pgbStatus.Visible = value; }
        }

        [Browsable(true)]
        [Description("Character that separates each byte in Hex view.")]
        public char HexDataSeparator
        {
            get { return hexDataSeparator; }
            set
            {
                if (char.IsControl(value))
                    throw new ArgumentOutOfRangeException();
                hexDataSeparator = value;
            }
        }

        [Browsable(true)]
        [Description("Character that divides a line in Hex view.")]
        public char HexCenterSeparator
        {
            get { return hexCenterSeparator; }
            set
            {
                if (char.IsControl(value))
                    throw new ArgumentOutOfRangeException();
                hexCenterSeparator = value;
            }
        }

        [Browsable(true)]
        [Description("Specifies whether to display an alternate background color for each line.")]
        public bool IsAlternateLineColor
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("First alternating line color.")]
        public Color AlternateBackColor1
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Second alternating line color.")]
        public Color AlternateBackColor2
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("First alternating line textcolor.")]
        public Color AlternateTextColor1
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Second alternating line textcolor.")]
        public Color AlternateTextColor2
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Hex view background color.")]
        public Color HexViewBackColor
        {
            get { return rtbHex.BackColor; }
            set { rtbHex.BackColor = value; }
        }

        [Browsable(true)]
        [Description("ASCII view background color.")]
        public Color TextViewBackColor
        {
            get { return rtbASCII.BackColor; }
            set { rtbASCII.BackColor = value; }
        }

        [Browsable(true)]
        [Description("Hex view text color.")]
        public Color HexViewForeColor
        {
            get { return rtbHex.ForeColor; }
            set { rtbHex.ForeColor = value; }
        }

        [Browsable(true)]
        [Description("ASCII view text color.")]
        public Color TextViewForeColor
        {
            get { return rtbASCII.ForeColor; }
            set { rtbASCII.ForeColor = value; }
        }

        [Browsable(true)]
        [Description("Hex view font.")]
        public Font HexViewFont
        {
            get { return rtbHex.Font; }
            set { rtbHex.Font = value; }
        }

        [Browsable(true)]
        [Description("ASCII view font.")]
        public Font TextViewFont
        {
            get { return rtbASCII.Font; }
            set { rtbASCII.Font = value; }
        }

        [Browsable(true)]
        [Description("Hide/Display line numbers.")]
        public bool ShowLineNumber
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Line number text color.")]
        public Color LineNumberForeColor
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Line number background color.")]
        public Color LineNumberBackColor
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Indicates whether to show line numbers in hex.")]
        public bool HexLineNumbers
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Indicates whether to display byte index instead of line numbers.")]
        public bool ShowByteIndexInsteadOfLineNumbers
        {
            get;
            set;
        }

        [Browsable(true)]
        [Description("Indicates whether to return accurate loading progress percentage(false) or integral percentage values only(true).")]
        public bool NotifyIntegralProgressOnly
        {
            get;
            set;
        }

        #endregion

        #region Overriden Properties

        public override Color ForeColor
        {
            get { return base.ForeColor; }
            set
            {
                base.ForeColor = value;
                splitContainerMain.ForeColor = value;
            }
        }

        public override Color BackColor
        {
            get { return base.BackColor; }
            set
            {
                base.BackColor = value;
                splitContainerMain.BackColor = value;
            }
        }

        #endregion
    }
}

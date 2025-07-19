namespace HexViewer
{
    public class SynchronizedScrollRichTextBox : System.Windows.Forms.RichTextBox
    { // source: http://stackoverflow.com/questions/1827323/c-synchronize-scroll-position-of-two-richtextboxes
        public event vScrollEventHandler vScroll;
        public delegate void vScrollEventHandler(System.Windows.Forms.Message message);

        public const int WM_VSCROLL = 0x115;
        public const int WM_MOUSEWHEEL = 0x20A;
        public const int WM_MOUSEMOVE = 0x200;

        protected override void WndProc(ref System.Windows.Forms.Message msg)
        {
            if (msg.Msg == WM_VSCROLL || msg.Msg == WM_MOUSEWHEEL)
                if (vScroll != null) vScroll(msg);

            base.WndProc(ref msg);
        }

        public void PubWndProc(ref System.Windows.Forms.Message msg)
        {
            base.WndProc(ref msg);
        }
    }
}

using SevenZip.Compression.LZMA;

namespace SevenZip.Compression.Lzma
{
    public static class SevenZipHelper
    {

        private static readonly int dictionary = 1 << 23;
        private static readonly int posStateBits = 2;
        private static readonly int litContextBits = 3; // for normal files
        private static readonly int litPosBits = 0;
        private static readonly int algorithm = 2;
        private static readonly int numFastBytes = 128;
        private static readonly string matchFinder = "bt4";
        private static readonly bool eos = false;

        private static readonly CoderPropID[] propIDs =
        {
            CoderPropID.DictionarySize,
            CoderPropID.PosStateBits,
            CoderPropID.LitContextBits,
            CoderPropID.LitPosBits,
            CoderPropID.Algorithm,
            CoderPropID.NumFastBytes,
            CoderPropID.MatchFinder,
            CoderPropID.EndMarker
        };

        // these are the default properties, keeping it simple for now:
        private static readonly object[] properties =
        {
            dictionary,
            posStateBits,
            litContextBits,
            litPosBits,
            algorithm,
            numFastBytes,
            matchFinder,
            eos
        };

        public static byte[] Compress(byte[] inputBytes)
        {
            MemoryStream? inStream = null;
            MemoryStream? outStream = null;
            MemoryStream? finalStream = null;

            byte[]? buffer = null;

            try
            {
                inStream = new MemoryStream(inputBytes);
                outStream = new MemoryStream();
                Encoder encoder = new();
                encoder.Code(inStream, outStream, -1, -1, null);
                buffer = new byte[outStream.Length];
                finalStream = new MemoryStream(buffer);
                outStream.WriteTo(finalStream);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (inStream != null)
                    inStream.Close();

                if (outStream != null)
                    outStream.Close();

                if (finalStream != null)
                    finalStream.Close();
            }
            return buffer;
        }

        public static byte[] Decompress(byte[] inputBytes, long outSize)
        {
            MemoryStream? newInStream = null;
            MemoryStream? newOutStream = null;

            byte[]? buffer = null;

            try
            {
                newInStream = new MemoryStream(inputBytes);

                Decoder decoder = new();

                newInStream.Seek(0, 0);
                buffer = new byte[outSize];
                newOutStream = new MemoryStream(buffer);

                byte[] properties2 = { 93, 0, 32, 0, 0 };

                decoder.SetDecoderProperties(properties2);
                long compressedSize = inputBytes.Length;
                decoder.Code(newInStream, newOutStream, compressedSize, outSize, null);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (newInStream != null)
                    newInStream.Close();

                if (newOutStream != null)
                    newOutStream.Close();
            }
            return buffer;
        }
    }
}

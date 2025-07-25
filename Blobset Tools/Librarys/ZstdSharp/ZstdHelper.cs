namespace ZstdSharp
{
    public static class ZstdHelper
    {
        public static byte[] Decompress(byte[] inputBytes, long outSize)
        {
            MemoryStream? newInStream = null;
            DecompressionStream? decompression = null;

            byte[]? buffer = null;

            try
            {
                newInStream = new MemoryStream(inputBytes);
                buffer = new byte[outSize];
                decompression = new DecompressionStream(newInStream, (int)outSize, true, false);
                decompression.Read(buffer, 0, (int)outSize);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (newInStream != null)
                {
                    newInStream.Close();
                    newInStream = null;
                }

                if (decompression != null)
                {
                    decompression.Close();
                    decompression = null;
                }
            }
            return buffer;
        }

        public static byte[] Compress(byte[] inputBytes)
        {
            Compressor? compressor = null;

            byte[]? buffer = null;

            try
            {
                compressor = new Compressor(1);
                var compressed = compressor.Wrap(inputBytes);

                buffer = compressed.ToArray();
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (compressor != null)
                {
                    compressor.Dispose();
                    compressor = null;
                }
            }
            return buffer;
        }
    }
}

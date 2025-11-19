using System;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;
using static System.Windows.Forms.VisualStyles.VisualStyleElement.ProgressBar;


namespace SevenZip.Compression.LZMA
{
    public static class SevenZipHelper
    {

        static int dictionary = 1 << 23;
        static int posStateBits = 2;
        static int litContextBits = 3; // for normal files
        static int litPosBits = 0;
        static int algorithm = 2;
        static int numFastBytes = 128;
        static string matchFinder = "bt4";
        static bool eos = false;

        static CoderPropID[] propIDs =
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
        static object[] properties =
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
                Encoder encoder = new Encoder();
                //encoder.SetCoderProperties(propIDs, properties);
                encoder.WriteCoderProperties(outStream);
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

        public static void CompressFileLZMA(string inFile, string outFile)
        {
            FileStream? input = null;
            FileStream? output = null;

            try
            {
                Encoder coder = new Encoder();
                input = new FileStream(inFile, FileMode.Open);
                output = new FileStream(outFile, FileMode.Create);

                // Write the encoder properties
                coder.WriteCoderProperties(output);

                // Encode the file.
                coder.Code(input, output, input.Length, -1, null);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (input != null)
                    input.Close();

                if (output != null)
                    output.Close();
            }
        }

        public static byte[] Decompress(byte[] inputBytes, long outSize)
        {
            MemoryStream? newInStream = null;
            MemoryStream? newOutStream = null;
            Decoder? decoder = null;

            byte[]? buffer = null;

            try
            {
                newInStream = new MemoryStream(inputBytes);

                decoder = new Decoder();

                newInStream.Seek(0, 0);
                buffer = new byte[outSize];
                newOutStream = new MemoryStream(buffer);

                byte[] properties2 = new byte[5];
                if (newInStream.Read(properties2, 0, 5) != 5)
                    throw (new Exception("input .lzma is too short"));

                decoder.SetDecoderProperties(properties2);
                long compressedSize = newInStream.Length - newInStream.Position;
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

                if (decoder != null)
                    decoder = null;
            }
            return buffer;
        }

        public static void DecompressFileLZMA(string inFile, string outFile)
        {
            FileStream? input = null;
            FileStream? output = null;

            try
            {
                Decoder coder = new();
                input = new(inFile, FileMode.Open);
                output = new(outFile, FileMode.Create);

                // Read the decoder properties
                byte[] properties = new byte[5];
                input.Read(properties, 0, 5);

                // Read in the decompress file size.
                byte[] fileLengthBytes = new byte[8];
                input.Read(fileLengthBytes, 0, 8);
                long fileLength = BitConverter.ToInt64(fileLengthBytes, 0);

                coder.SetDecoderProperties(properties);
                coder.Code(input, output, input.Length, fileLength, null);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (input != null)
                    input.Close();

                if (output != null)
                    output.Close();
            }
        }
    }
}

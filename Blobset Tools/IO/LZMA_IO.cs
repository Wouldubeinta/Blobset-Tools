using BlobsetIO;
using PackageIO;
using SevenZip.Compression.LZMA;
using System.ComponentModel;

namespace Blobset_Tools
{
    /// <summary>
    /// LZMA Compression IO Operations Class
    /// </summary>
    /// <remarks>
    ///   Blobset Tools. Written by Wouldubeinta
    ///   Copyright (C) 2025 Wouldy Mods.
    ///   
    ///   This program is free software; you can redistribute it and/or
    ///   modify it under the terms of the GNU General Public License
    ///   as published by the Free Software Foundation; either version 2
    ///   of the License, or (at your option) any later version.
    ///   
    ///   This program is distributed in the hope that it will be useful,
    ///   but WITHOUT ANY WARRANTY; without even the implied warranty of
    ///   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    ///   GNU General Public License for more details.
    /// 
    ///   The author may be contacted at:
    ///   Discord: Wouldubeinta
    /// </remarks>
    /// <history>
    /// [Wouldubeinta]	   16/07/2025	Created
    /// </history>
    public class LZMA_IO
    {
        /// <summary>
        /// Decompresses data from a byte array and returns it.
        /// </summary>
        /// <param name="input">Input Byte array.</param>
        /// <param name="outSize">Uncompressed Size.</param>
        /// <returns>Returns uncompressed byte array.</returns>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static byte[] DecompressAndRead(byte[] input, int outSize)
        {
            Decoder? decoder = null;
            MemoryStream? inStream = null;
            MemoryStream? outStream = null;
            byte[]? buffer = null;

            try
            {
                inStream = new(input);
                outStream = new();
                decoder = new();

                buffer = new byte[outSize];

                byte[] properties = new byte[5];
                if (inStream.Read(properties, 0, 5) != 5)
                    throw (new Exception("input .lzma is too short"));

                decoder.SetDecoderProperties(properties);

                long inSize = inStream.Length - inStream.Position;
                decoder.Code(inStream, outStream, inSize, outSize, null);
                outStream.Position = 0;
                outStream.Read(buffer, 0, outSize);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (inStream != null) { inStream.Dispose(); inStream = null; }
                if (outStream != null) { outStream.Dispose(); outStream = null; }
                if (decoder != null) { decoder = null; }
            }
            return buffer;
        }

        /// <summary>
        /// Compresses data from a byte array and returns it.
        /// </summary>
        /// <param name="input">Input Byte array.</param>
        /// <returns>Returns compressed byte array.</returns>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static byte[] CompressAndRead(byte[] input)
        {
            MemoryStream? inStream = null;
            MemoryStream? outStream = null;
            Encoder? encoder = null;
            byte[]? buffer = null;

            try
            {
                encoder = new();
                outStream = new();
                inStream = new MemoryStream(input);
                encoder.Code(inStream, outStream, input.Length, -1, null);
                buffer = new byte[outStream.Length];

                if (outStream != null)
                    outStream.Write(buffer, 0, buffer.Length);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (inStream != null) { inStream.Dispose(); inStream = null; }
                if (outStream != null) { outStream.Dispose(); outStream = null; }
                if (encoder != null) { encoder = null; }
            }
            return buffer;
        }

        /// <summary>
        /// Decompresses the compressed chunk data, if compressed. Then writes appending chunk data to a file.
        /// </summary>
        /// <param name="input">byte[] array chunk data.</param>
        /// <param name="writer">FileStream writer.</param>
        /// <param name="outSize">Uncompressed Size.</param>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static void DecompressAndWrite(byte[] input, FileStream writer, int outSize)
        {
            Decoder? decoder = null;
            MemoryStream? inStream = null;
            MemoryStream? outStream = null;

            try
            {
                inStream = new(input);
                outStream = new();
                decoder = new();

                byte[] buffer = new byte[outSize];

                byte[] properties = new byte[5];
                if (inStream.Read(properties, 0, 5) != 5)
                    throw (new Exception("input .lzma is too short"));

                decoder.SetDecoderProperties(properties);

                long inSize = inStream.Length - inStream.Position;
                decoder.Code(inStream, outStream, inSize, outSize, null);
                outStream.Position = 0;
                outStream.Read(buffer, 0, outSize);
                writer.Write(buffer, 0, buffer.Length);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (inStream != null) { inStream.Dispose(); inStream = null; }
                if (outStream != null) { outStream.Dispose(); outStream = null; }
                if (decoder != null) { decoder = null; }
            }
        }

        /// <summary>
        /// Compresses chunk data and appends to output file.
        /// </summary>
        /// <param name="input">Byte array to be compressed.</param>
        /// <param name="writer">FileStream output writer.</param>
        /// <param name="offset">Compressed chunk offset.</param>
        /// <param name="chunkSize">Data chunk size.</param>
        /// <returns>Returns compressed chunk size.</returns>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static int CompressAndWrite(byte[] input, FileStream writer, ref int offset, int chunkSize = 32768)
        {
            MemoryStream? inStream = null;
            MemoryStream? outStream = null;
            Encoder? encoder = null;
            int compressedSize = 0;

            try
            {
                inStream = new(input);
                outStream = new();
                encoder = new();

                int readCount = 0;
                byte[]? buffer = new byte[chunkSize];

                while ((readCount = inStream.Read(buffer, 0, chunkSize)) != 0)
                {
                    offset = (int)writer.Position;
                    encoder.Code(inStream, outStream, input.Length, -1, null);
                    byte[]? output = new byte[outStream.Length];
                    outStream.Write(output, 0, output.Length);
                    writer.Write(output, 0, output.Length);
                    writer.Flush();
                    compressedSize = (int)outStream.Length;
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (inStream != null) { inStream.Dispose(); inStream = null; }
                if (outStream != null) { outStream.Dispose(); outStream = null; }
                if (encoder != null) { encoder = null; }
            }
            return compressedSize;
        }

        /// <summary>
        /// Compresses chunk data and appends to output file.
        /// </summary>
        /// <param name="input">Byte array to be compressed.</param>
        /// <param name="writer">FileStream output writer.</param>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static void CompressAndWrite(byte[] input, FileStream writer)
        {
            MemoryStream? inStream = null;
            MemoryStream? outStream = null;
            Encoder? encoder = null;

            try
            {
                inStream = new(input);
                outStream = new();
                encoder = new();
                encoder.Code(inStream, outStream, input.Length, -1, null);

                byte[]? output = new byte[outStream.Length];
                outStream.Write(output, 0, output.Length);
                int compressedSize = output.Length + 4;
                writer.Write(BitConverter.GetBytes(compressedSize), 0, 4);
                writer.Write(output, 0, output.Length);
                writer.Flush();
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (inStream != null) { inStream.Dispose(); inStream = null; }
                if (outStream != null) { outStream.Dispose(); outStream = null; }
                if (encoder != null) { encoder = null; }
            }
        }

        /// <summary>
        /// Decompresses the compressed chunk data, if compressed. Then read's the headers magic to determine the file type.
        /// </summary>
        /// <param name="input">byte[] array chunk data.</param>
        /// <param name="outSize">Uncompressed size.</param>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static int DecompressAndReadMagic(byte[] input, int outSize)
        {
            Decoder? decoder = null;
            MemoryStream? inStream = null;
            MemoryStream? outStream = null;
            int magic = 0;
            bool isMiniTXPK = false;
            int _outSize = outSize;
            byte[]? buffer = null;

            try
            {
                inStream = new(input);
                outStream = new();
                decoder = new();

                buffer = new byte[outSize];

                byte[] properties = new byte[5];
                if (inStream.Read(properties, 0, 5) != 5)
                    throw (new Exception("input .lzma is too short"));

                decoder.SetDecoderProperties(properties);

                long inSize = inStream.Length - inStream.Position;
                decoder.Code(inStream, outStream, inSize, outSize, null);
                outStream.Position = 0;
                outStream.Read(buffer, 0, outSize);

                if (buffer[20] == 99 && buffer[21] == 111 && buffer[22] == 110 && buffer[23] == 118)  // checking for "conv" string
                    isMiniTXPK = true;

                byte[] tmp = new byte[4];

                if (isMiniTXPK)
                {
                    tmp[0] = 77; tmp[1] = 73; tmp[2] = 78; tmp[3] = 73; // setting the magic to "MINI"
                }
                else
                {
                    tmp[0] = buffer[0]; tmp[1] = buffer[1]; tmp[2] = buffer[2]; tmp[3] = buffer[3];
                }

                magic = BitConverter.ToInt32(tmp);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (inStream != null) { inStream.Dispose(); inStream = null; }
                if (outStream != null) { outStream.Dispose(); outStream = null; }
                if (decoder != null) { decoder = null; }
            }
            return magic;
        }

        /// <summary>
        /// M3MP Decompress extracts all the files from a decompressed tmp m3mp file.
        /// </summary>
        /// <param name="m3mpTempFile">Temporary m3mp file path.</param>
        /// <param name="folderPath">Output folder location.</param>
        /// <returns>Returns the extracted data file path progress.</returns>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static string M3MPDecompressAndWrite(string m3mpTempFile, string folderPath)
        {
            FileStream? fsReader = null;
            FileStream? fsWriter = null;
            Reader? br = null;
            string progressFile = string.Empty;

            try
            {
                fsReader = new(m3mpTempFile, FileMode.Open, FileAccess.Read);
                br = new(fsReader);

                M3MP m3mp = new();
                m3mp.Deserialize(br);

                foreach (var entry in m3mp.UnCompressedEntries)
                {
                    string filePath = entry.FilePath.Replace("/", @"\");
                    string outputFilePath = folderPath + filePath;
                    Directory.CreateDirectory(Path.GetDirectoryName(outputFilePath));
                    fsWriter = new(outputFilePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    for (int i = 0; i < m3mp.ChunksCount; i++)
                    {
                        br.Position = m3mp.CompressedEntries[i].CompressedDataInfo.Offset;
                        byte[] compressedChunkData = br.ReadBytes((int)m3mp.CompressedEntries[i].CompressedDataInfo.CompressedSize);
                        DecompressAndWrite(compressedChunkData, fsWriter, (int)m3mp.CompressedEntries[i].CompressedDataInfo.UnCompressedSize);
                    }

                    if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
                    progressFile = filePath;
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
            }
            return progressFile;
        }

        /// <summary>
        /// M3MP Decompress extracts all the files from a decompressed tmp m3mp file.
        /// </summary>
        /// <param name="m3mpTempFile">Temporary m3mp file path.</param>
        /// <param name="folderPath">Output folder location.</param>
        /// <param name="bgw">Background Worker</param>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static void M3MPDecompressAndWrite(string m3mpTempFile, string folderPath, BackgroundWorker? bgw = null)
        {
            FileStream? fsReader = null;
            FileStream? fsWriter = null;
            Reader? br = null;

            try
            {
                if (File.Exists(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp"))
                    File.Delete(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp");

                fsReader = new(m3mpTempFile, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                br = new(fsReader);

                M3MP m3mp = new();
                m3mp.Deserialize(br);

                fsWriter = new(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp", FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                for (int i = 0; i < m3mp.ChunksCount; i++)
                {
                    br.Position = m3mp.CompressedEntries[i].CompressedDataInfo.Offset;

                    byte[] compressedChunkData = br.ReadBytes((int)m3mp.CompressedEntries[i].CompressedDataInfo.CompressedSize);
                    DecompressAndWrite(compressedChunkData, fsWriter, (int)m3mp.CompressedEntries[i].CompressedDataInfo.UnCompressedSize);

                    int percentProgress = 100 * i / (int)m3mp.ChunksCount;
                    bgw.ReportProgress(percentProgress, "Extracting Chunk Data - " + (i + 1).ToString());
                }

                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
                if (br != null) { br.Close(); br = null; }

                int index = 0;
                br = new(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp");

                foreach (var entry in m3mp.UnCompressedEntries)
                {
                    string filePath = entry.FilePath.Replace("/", @"\");
                    string outputFilePath = folderPath + filePath;
                    Directory.CreateDirectory(Path.GetDirectoryName(outputFilePath));

                    int chunkCount = Utilities.ChunkAmount((int)m3mp.UnCompressedEntries[index].UncompressedDataInfo.Size);
                    long[] chunkSizes = Utilities.ChunkSizes((int)m3mp.UnCompressedEntries[index].UncompressedDataInfo.Size, chunkCount);

                    fsWriter = new(outputFilePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    for (int i = 0; i < chunkCount; i++)
                    {
                        byte[] tmpData = br.ReadBytes((int)chunkSizes[i]);
                        IO.ReadWriteData(tmpData, fsWriter, (int)chunkSizes[i]);
                    }

                    int percentProgress = 100 * index / (int)m3mp.FilesCount;
                    bgw.ReportProgress(percentProgress, filePath);
                    index++;
                    if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
                }

                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }

                if (File.Exists(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp"))
                    File.Delete(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp");
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
                if (fsReader != null) { fsReader.Dispose(); fsReader = null; }
            }
        }

        /// <summary>
        /// M3MP Decompress extracts all the files from a decompressed tmp m3mp file.
        /// </summary>
        /// <param name="br">Binary Reader stream.</param>
        /// <returns>Returns the extracted data file path progress.</returns>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static string M3MPDecompressAndWrite(Reader br)
        {
            FileStream? fsWriter = null;
            Reader? tmpBr = null;
            string progressFile = string.Empty;

            try
            {
                if (File.Exists(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp"))
                    File.Delete(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp");

                M3MP m3mp = new();
                m3mp.Deserialize(br);

                fsWriter = new(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp", FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                for (int i = 0; i < m3mp.ChunksCount; i++)
                {
                    br.Position = m3mp.CompressedEntries[i].CompressedDataInfo.Offset;

                    byte[] compressedChunkData = br.ReadBytes((int)m3mp.CompressedEntries[i].CompressedDataInfo.CompressedSize);
                    DecompressAndWrite(compressedChunkData, fsWriter, (int)m3mp.CompressedEntries[i].CompressedDataInfo.UnCompressedSize);
                }

                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }

                int index = 0;
                tmpBr = new(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp");

                string outputFilePath = Global.currentPath + @"\games\" + Properties.Settings.Default.GameName + @"\data-0.blobset\";

                foreach (var entry in m3mp.UnCompressedEntries)
                {
                    string filePath = entry.FilePath.Replace("/", @"\");
                    Directory.CreateDirectory(Path.GetDirectoryName(outputFilePath + filePath));

                    int chunkCount = Utilities.ChunkAmount((int)m3mp.UnCompressedEntries[index].UncompressedDataInfo.Size);
                    long[] chunkSizes = Utilities.ChunkSizes((int)m3mp.UnCompressedEntries[index].UncompressedDataInfo.Size, chunkCount);

                    for (int i = 0; i < chunkCount; i++)
                    {
                        byte[] tmpData = tmpBr.ReadBytes((int)chunkSizes[i]);
                        fsWriter = new(outputFilePath + filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                        IO.ReadWriteData(tmpData, fsWriter);
                    }

                    progressFile = filePath;

                    if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
                }

                if (tmpBr != null) { tmpBr.Close(); tmpBr = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }

                if (File.Exists(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp"))
                    File.Delete(Global.currentPath + @"\temp\m3mp_uncompressed_data.tmp");
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (tmpBr != null) { tmpBr.Close(); tmpBr = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
            }
            return progressFile;
        }

        /// <summary>
        /// Get's TXPK header info.
        /// </summary>
        /// <param name="fileIn">File path to txpk.</param>
        /// <returns>Returns the txpk header info.</returns>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static TXPK ReadTXPKInfo(string fileIn)
        {
            Reader? input = null;
            Reader? txpk_br = null;
            TXPK? txpk = null;

            try
            {
                input = new(fileIn);
                int txpkCompressedChunkSize = input.ReadInt32();
                int txpkTmp = txpkCompressedChunkSize -= 4;
                txpkCompressedChunkSize = txpkTmp;

                byte[] txpkData = input.ReadBytes(txpkCompressedChunkSize);

                byte[] txpkHeader = DecompressAndRead(txpkData, txpkData.Length);

                txpk_br = new(txpkHeader);

                txpk = new();
                txpk.Deserialize(txpk_br);
                return txpk;
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (input != null) { input.Close(); input = null; }
                if (txpk_br != null) { txpk_br.Close(); txpk_br = null; }
            }
            return txpk;
        }

        /// <summary>
        /// Get's M3MP header info.
        /// </summary>
        /// <param name="fileIn">File path to m3mp.</param>
        /// <param name="isCompressed">Is the M3MP compressed.</param>
        /// <returns>Returns the m3mp header info.</returns>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static M3MP ReadM3MPInfo(string fileIn, bool isCompressed)
        {
            Reader? input = null;
            Reader? m3mp_br = null;
            M3MP? m3mp = null;

            try
            {
                if (!File.Exists(fileIn))
                    return m3mp;

                input = new(fileIn);
                m3mp = new();

                if (isCompressed)
                {
                    List<byte[]> m3mpHeaderChunk = new(3);
                    int m3mpHeaderSize = 0;

                    for (int j = 0; j < 3; j++)
                    {
                        int m3mpCompressedSize = input.ReadInt32();
                        int m3mpTmp = m3mpCompressedSize -= 4;
                        m3mpCompressedSize = m3mpTmp;

                        byte[] m3mpData = input.ReadBytes(m3mpCompressedSize);

                        m3mpHeaderChunk.Add(DecompressAndRead(m3mpData, m3mpData.Length));
                        m3mpHeaderSize += 262144;
                    }

                    byte[] m3mpHeader = IO.CombineDataChunks(m3mpHeaderChunk, m3mpHeaderSize);
                    m3mp_br = new Reader(m3mpHeader);
                    m3mp.Deserialize(m3mp_br);
                }
                else
                    m3mp.Deserialize(input);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (input != null) { input.Close(); input = null; }
                if (m3mp_br != null) { m3mp_br.Close(); m3mp_br = null; }
            }
            return m3mp;
        }
    }
}

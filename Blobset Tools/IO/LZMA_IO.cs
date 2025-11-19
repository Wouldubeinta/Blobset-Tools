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
        /// Decompresses chunk and writes the data.
        /// </summary>
        /// <param name="br">Binary Reader input.</param>
        /// <param name="writer">Filestream writer</param>
        /// <history>
        /// [Wouldubeinta]	26/07/2025	Created
        /// </history>
        public static void DecompressChunkAndWrite(Reader br, FileStream writer)
        {
            int chunkCount = br.ReadInt32();
            int[] chunkCompressedSize = new int[chunkCount];

            for (int j = 0; j < chunkCount; j++)
            {
                chunkCompressedSize[j] = br.ReadInt32();
                chunkCompressedSize[j] = chunkCompressedSize[j] -= 4;
            }

            for (int j = 0; j < chunkCount; j++)
            {
                int chunkUnCompressedSize = br.ReadInt32();

                if (chunkCompressedSize[j] == chunkUnCompressedSize) 
                {
                    byte[] chunkUnCompressedData = br.ReadBytes(chunkUnCompressedSize, Endian.Little);
                    DecompressAndWrite(chunkUnCompressedData, writer, chunkUnCompressedSize, false);
                }
                else
                {
                    byte[] chunkCompressedData = br.ReadBytes(chunkCompressedSize[j], Endian.Little);
                    DecompressAndWrite(chunkCompressedData, writer, chunkUnCompressedSize, true);
                }
            }
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
        public static void DecompressAndWrite(byte[] input, FileStream writer, int outSize, bool isCompressed)
        {
            Decoder? decoder = null;
            MemoryStream? inStream = null;
            MemoryStream? outStream = null;

            try
            {
                if (isCompressed)
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
                    writer.Flush();
                }
                else 
                {
                    writer.Write(input, 0, input.Length);
                    writer.Flush();
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

                    byte[] compressedChunkData = br.ReadBytes((int)m3mp.CompressedEntries[i].CompressedDataInfo.CompressedSize, Endian.Little);
                    DecompressAndWrite(compressedChunkData, fsWriter, (int)m3mp.CompressedEntries[i].CompressedDataInfo.UnCompressedSize, true);

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
                        byte[] tmpData = br.ReadBytes((int)chunkSizes[i], Endian.Little);
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
        /// Get's TXPK header info.
        /// </summary>
        /// <param name="fileIn">File path to txpk.</param>
        /// <returns>Returns the txpk header info.</returns>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static TXPK ReadTXPKInfo(List<Structs.FileIndexInfo> list)
        {
            Reader? br = null;
            Reader? txpk_br = null;
            TXPK? txpk = null;

            try
            {
                br = new(Properties.Settings.Default.GameLocation.Replace("-0", "-" + Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].BlobSetNumber));

                uint MainFinalOffset = Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainFinalOffSet;
                uint MainCompressedSize = Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                uint MainUnCompressedSize = Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;

                byte[] txpkData = new byte[MainUnCompressedSize];

                br.Position = MainFinalOffset;

                if (MainCompressedSize != MainUnCompressedSize)
                {
                    int chunkCount = br.ReadInt32();
                    int[] chunkCompressedSize = new int[chunkCount];
                    int size = 0;

                    for (int j = 0; j < chunkCount; j++)
                    {
                        chunkCompressedSize[j] = br.ReadInt32();
                        chunkCompressedSize[j] = chunkCompressedSize[j] -= 4;
                    }

                    for (int j = 0; j < chunkCount; j++)
                    {
                        int chunkUnCompressedSize = br.ReadInt32();

                        if (chunkCompressedSize[j] == chunkUnCompressedSize)
                        {
                            byte[] tmptxpkData = br.ReadBytes(chunkUnCompressedSize);
                            Buffer.BlockCopy(tmptxpkData, 0, txpkData, size, tmptxpkData.Length);
                            size += tmptxpkData.Length;
                        }
                        else
                        {
                            byte[] compressedTmptxpkData = br.ReadBytes(chunkCompressedSize[j]);
                            byte[] tmptxpkData = DecompressAndRead(compressedTmptxpkData, chunkUnCompressedSize);
                            Buffer.BlockCopy(tmptxpkData, 0, txpkData, size, tmptxpkData.Length);
                            size += tmptxpkData.Length;
                        }
                    }
                }
                else
                {
                    txpkData = br.ReadBytes(Convert.ToInt32(MainUnCompressedSize));
                }

                txpk_br = new(txpkData);

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
                if (br != null) { br.Close(); br = null; }
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
        public static M3MP ReadM3MPInfo(List<Structs.FileIndexInfo> list)
        {
            Reader? br = null;
            Reader? m3mp_br = null;
            M3MP? m3mp = null;

            try
            {
                br = new(Properties.Settings.Default.GameLocation.Replace("-0", "-" + Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].BlobSetNumber));

                uint MainFinalOffset = Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainFinalOffSet;
                uint MainCompressedSize = Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                uint MainUnCompressedSize = Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;

                br.Position = MainFinalOffset;

                m3mp = new();

                if (MainCompressedSize != MainUnCompressedSize)
                {
                    int chunkCount = br.ReadInt32();
                    int[] chunkCompressedSize = new int[chunkCount];

                    for (int j = 0; j < chunkCount; j++)
                    {
                        chunkCompressedSize[j] = br.ReadInt32();
                        chunkCompressedSize[j] = chunkCompressedSize[j] -= 4;
                    }

                    List<byte[]> m3mpHeaderChunk = new(chunkCount);
                    int m3mpHeaderSize = 0;

                    for (int j = 0; j < chunkCount; j++)
                    {
                        int chunkUnCompressedSize = br.ReadInt32();

                        if (chunkCompressedSize[j] == chunkUnCompressedSize)
                        {
                            byte[] m3mpData = br.ReadBytes(chunkUnCompressedSize);
                            m3mpHeaderChunk.Add(m3mpData);
                            m3mpHeaderSize += m3mpData.Length;
                        }
                        else
                        {
                            byte[] m3mpData = br.ReadBytes(chunkCompressedSize[j]);
                            m3mpHeaderChunk.Add(DecompressAndRead(m3mpData, chunkUnCompressedSize));
                            m3mpHeaderSize += chunkUnCompressedSize;
                        }
                    }

                    byte[] m3mpHeader = IO.CombineDataChunks(m3mpHeaderChunk, m3mpHeaderSize);
                    m3mp_br = new Reader(m3mpHeader);
                    m3mp.Deserialize(m3mp_br);
                }
                else 
                {
                    m3mp.Deserialize(br);
                }
                    
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (m3mp_br != null) { m3mp_br.Close(); m3mp_br = null; }
            }
            return m3mp;
        }
    }
}

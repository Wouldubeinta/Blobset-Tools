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
    ///   as published by the Free Software Foundation; either version 3
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
        /// Decompresses chunks and returns the data.
        /// </summary>
        /// <param name="br">Binary Reader input.</param>
        /// <param name="dataSize">Uncompressed data size.</param>
        /// <returns>Returns uncompressed byte array data.</returns>
        /// <history>
        /// [Wouldubeinta]	03/12/2025	Created
        /// </history>
        public static byte[] DecompressChunkAndRead(Reader br, int dataSize)
        {
            int size = 0;
            int chunkCount = br.ReadInt32();
            int[] chunkCompressedSize = new int[chunkCount];

            byte[]? ddsData = new byte[dataSize];

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
                    byte[] tmpddsData = br.ReadBytes(chunkUnCompressedSize, Endian.Little);
                    Buffer.BlockCopy(tmpddsData, 0, ddsData, size, tmpddsData.Length);
                    size += tmpddsData.Length;
                }
                else
                {
                    byte[] compressedTmpddsData = br.ReadBytes(chunkCompressedSize[j], Endian.Little);
                    byte[] tmpddsData = DecompressAndRead(compressedTmpddsData, chunkUnCompressedSize);
                    Buffer.BlockCopy(tmpddsData, 0, ddsData, size, tmpddsData.Length);
                    size += tmpddsData.Length;
                }
            }
            return ddsData;
        }

        /// <summary>
        /// Decompresses chunk and writes console dds data + header.
        /// </summary>
        /// <param name="br">Binary Reader input.</param>
        /// <param name="writer">Filestream writer</param>
        /// <history>
        /// [Wouldubeinta]	26/07/2025	Created
        /// </history>
        public static void DecompressChunkAndWriteDDS(Reader br, FileStream writer, Mini_TXPK miniTxpk, int VramUnCompressedSize)
        {
            byte[]? ddsData = DecompressChunkAndRead(br, VramUnCompressedSize);

            PS3_DDS consoleDDS = new(miniTxpk.DDSHeight, miniTxpk.DDSWidth, miniTxpk.DDSMipMaps, miniTxpk.DDSType, ddsData.Length, ddsData);
            ddsData = consoleDDS.WriteDDS();

            if (ddsData != null)
                writer.Write(ddsData, 0, ddsData.Length);
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
                }
                else
                    writer.Write(input, 0, input.Length);

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
        public static int CompressAndWrite(byte[] input, FileStream writer, int chunkSize = 32768)
        {
            Reader? br = null;
            MemoryStream? inStream = null;
            MemoryStream? outStream = null;
            Encoder? encoder = null;
            int totalCompressedSize = 0;

            try
            {
                long currentPos = writer.Position; // Get writer's current position

                br = new(input, Endian.Little);

                int chunkCount = Utilities.ChunkAmount(input.Length, chunkSize);
                long[] chunkSizes = Utilities.ChunkSizes(input.Length, chunkCount, chunkSize);

                // Initial size for the header
                int headerSize = 4 + (chunkCount * 8);
                int buffer = 4 + (chunkCount * 4);
                writer.Write(new byte[buffer], 0, buffer); // Preallocate header space

                long[] compressedSizes = new long[chunkCount];
                totalCompressedSize = headerSize;

                for (int i = 0; i < chunkCount; i++)
                {
                    long currentChunkSize = chunkSizes[i];

                    byte[] _chunkSizes = BitConverter.GetBytes((uint)currentChunkSize);
                    writer.Write(Global.isBigendian ? Functions.SwapSex(_chunkSizes) : _chunkSizes, 0, 4);

                    byte[] chunk = br.ReadBytes((int)currentChunkSize);
                    inStream = new(chunk);

                    outStream = new();
                    encoder = new();
                    // Compress the chunk
                    encoder.WriteCoderProperties(outStream);
                    encoder.Code(inStream, outStream, currentChunkSize, -1, null);
                    byte[] output = outStream.ToArray(); // Get compressed data
                    writer.Write(output, 0, output.Length);
                    compressedSizes[i] = output.Length;
                    if (outStream != null) { outStream.Dispose(); outStream = null; }
                    if (encoder != null) { encoder = null; }

                    totalCompressedSize += (int)compressedSizes[i];
                }

                long endPos = writer.Position;

                // Write the header information
                writer.Position = currentPos;
                byte[] _chunkCount = BitConverter.GetBytes(chunkCount);
                writer.Write(Global.isBigendian ? Functions.SwapSex(_chunkCount) : _chunkCount, 0, 4);

                for (int i = 0; i < chunkCount; i++)
                {
                    byte[] _compressedSizes = BitConverter.GetBytes((uint)(compressedSizes[i] += 4));
                    writer.Write(Global.isBigendian ? Functions.SwapSex(_compressedSizes) : _compressedSizes, 0, 4);
                }

                writer.Position = endPos;
                writer.Flush();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error occurred, report it to Wouldy: {ex}", "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (inStream != null) { inStream.Dispose(); inStream = null; }
                if (outStream != null) { outStream.Dispose(); outStream = null; }
                if (encoder != null) { encoder = null; }
            }
            return totalCompressedSize;
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
            FileStream? fsWriter = null;
            Reader? br = null;

            try
            {
                string m3mpUncompressedDataTmp = Path.Combine(Global.currentPath, "temp", "m3mp_uncompressed_data.tmp");

                if (File.Exists(m3mpUncompressedDataTmp))
                    File.Delete(m3mpUncompressedDataTmp);

                br = new(m3mpTempFile);

                M3MP m3mp = new();
                m3mp.Deserialize(br);

                if (m3mp == null)
                    return;

                fsWriter = new(m3mpUncompressedDataTmp, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                for (int i = 0; i < m3mp.ChunksCount; i++)
                {
                    br.Position = m3mp.CompressedEntries[i].CompressedDataInfo.Offset;

                    byte[] compressedChunkData = br.ReadBytes((int)m3mp.CompressedEntries[i].CompressedDataInfo.CompressedSize, Endian.Little);
                    DecompressAndWrite(compressedChunkData, fsWriter, (int)m3mp.CompressedEntries[i].CompressedDataInfo.UnCompressedSize, true);

                    if (bgw != null)
                    {
                        int percentProgress = (i + 1) * 100 / (int)m3mp.ChunksCount;
                        bgw.ReportProgress(percentProgress, "Extracting Chunk Data - " + (i + 1).ToString());
                    }
                }

                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
                if (br != null) { br.Close(); br = null; }

                int index = 0;
                br = new(m3mpUncompressedDataTmp);

                foreach (var entry in m3mp.UnCompressedEntries)
                {
                    string filePath = entry.FilePath.Replace("/", @"\");
                    string outputFilePath = Path.Combine(folderPath, filePath);
                    Directory.CreateDirectory(Path.GetDirectoryName(outputFilePath));

                    int chunkCount = Utilities.ChunkAmount((int)m3mp.UnCompressedEntries[index].UncompressedDataInfo.Size);
                    long[] chunkSizes = Utilities.ChunkSizes((int)m3mp.UnCompressedEntries[index].UncompressedDataInfo.Size, chunkCount);

                    fsWriter = new(outputFilePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    for (int i = 0; i < chunkCount; i++)
                    {
                        byte[] tmpData = br.ReadBytes((int)chunkSizes[i], Endian.Little);
                        IO.ReadWriteData(tmpData, fsWriter, (int)chunkSizes[i]);
                    }

                    if (bgw != null)
                    {
                        int percentProgress = (index + 1) * 100 / (int)m3mp.FilesCount;
                        bgw.ReportProgress(percentProgress, filePath);
                    }

                    index++;
                    if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
                }

                if (br != null) { br.Close(); br = null; }
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }

                if (File.Exists(m3mpUncompressedDataTmp))
                    File.Delete(m3mpUncompressedDataTmp);
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
        }


        /// <summary>
        /// Get's MiniTXPK header info.
        /// </summary>
        /// <param name="fileIn">File path to mini txpk.</param>
        /// <returns>Returns the mini txpk header info.</returns>
        /// <history>
        /// [Wouldubeinta]		16/07/2025	Created
        /// </history>
        public static Mini_TXPK ReadMiniTXPKInfo(List<Structs.FileIndexInfo> list)
        {
            Reader? br = null;
            Reader? mini_txpk_br = null;
            Mini_TXPK? mini_txpk = null;

            try
            {
                Endian endian = Endian.Little;

                if (Global.isBigendian)
                    endian = Endian.Big;

                br = new(Global.gameInfo.GameLocation.Replace("-0", "-" + Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].BlobSetNumber), endian);

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
                            byte[] tmptxpkData = br.ReadBytes(chunkUnCompressedSize, Endian.Little);
                            Buffer.BlockCopy(tmptxpkData, 0, txpkData, size, tmptxpkData.Length);
                            size += tmptxpkData.Length;
                        }
                        else
                        {
                            byte[] compressedTmptxpkData = br.ReadBytes(chunkCompressedSize[j], Endian.Little);
                            byte[] tmpMiniTxpkData = DecompressAndRead(compressedTmptxpkData, chunkUnCompressedSize);
                            Buffer.BlockCopy(tmpMiniTxpkData, 0, txpkData, size, tmpMiniTxpkData.Length);
                            size += tmpMiniTxpkData.Length;
                        }
                    }
                }
                else
                {
                    txpkData = br.ReadBytes(Convert.ToInt32(MainUnCompressedSize), Endian.Little);
                }

                mini_txpk_br = new(txpkData);

                mini_txpk = new();
                mini_txpk.Deserialize(mini_txpk_br);
                return mini_txpk;
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (mini_txpk_br != null) { mini_txpk_br.Close(); mini_txpk_br = null; }
            }
            return mini_txpk;
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
                Endian endian = Endian.Little;

                if (Global.isBigendian)
                    endian = Endian.Big;

                br = new(Global.gameInfo.GameLocation.Replace("-0", "-" + Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].BlobSetNumber), endian);

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
                            byte[] tmptxpkData = br.ReadBytes(chunkUnCompressedSize, Endian.Little);
                            Buffer.BlockCopy(tmptxpkData, 0, txpkData, size, tmptxpkData.Length);
                            size += tmptxpkData.Length;
                        }
                        else
                        {
                            byte[] compressedTmptxpkData = br.ReadBytes(chunkCompressedSize[j], Endian.Little);
                            byte[] tmptxpkData = DecompressAndRead(compressedTmptxpkData, chunkUnCompressedSize);
                            Buffer.BlockCopy(tmptxpkData, 0, txpkData, size, tmptxpkData.Length);
                            size += tmptxpkData.Length;
                        }
                    }
                }
                else
                {
                    txpkData = br.ReadBytes(Convert.ToInt32(MainUnCompressedSize), Endian.Little);
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
                Endian endian = Endian.Little;

                if (Global.isBigendian)
                    endian = Endian.Big;

                br = new(Global.gameInfo.GameLocation.Replace("-0", "-" + Global.blobsetHeaderData.Entries[list[Global.fileIndex].BlobsetIndex].BlobSetNumber), endian);

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
                            byte[] m3mpData = br.ReadBytes(chunkUnCompressedSize, Endian.Little);
                            m3mpHeaderChunk.Add(m3mpData);
                            m3mpHeaderSize += m3mpData.Length;
                        }
                        else
                        {
                            byte[] m3mpData = br.ReadBytes(chunkCompressedSize[j], Endian.Little);
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

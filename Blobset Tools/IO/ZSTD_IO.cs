using BlobsetIO;
using PackageIO;
using System.ComponentModel;
using ZstdSharp;

namespace Blobset_Tools
{
    /// <summary>
    /// Z Standard Compression IO Operations Class
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
    /// [Wouldubeinta]	   01/07/2025	Created
    /// </history>
    public class ZSTD_IO
    {
        /// <summary>
        /// Decompresses data from a byte array and returns it.
        /// </summary>
        /// <param name="input">Input Byte array.</param>
        /// <param name="isCompressed">Is the data compressed.</param>
        /// <returns>Returns uncompressed byte array.</returns>
        /// <history>
        /// [Wouldubeinta]		08/07/2025	Created
        /// </history>
        public static byte[] DecompressAndRead(byte[] input, bool isCompressed)
        {
            Decompressor? decompressor = null;
            byte[]? buffer = null;

            try
            {
                if (isCompressed)
                {
                    decompressor = new();
                    buffer = decompressor.Unwrap(input).ToArray();
                }
                else
                {
                    buffer = input;
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (decompressor != null) { decompressor.Dispose(); decompressor = null; }
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
        public static void DecompressChunk(Reader br, FileStream writer)
        {
            while (br.Position < br.Length)
            {
                int compressedSize = br.ReadInt32();
                int tmp = compressedSize -= 4;
                compressedSize = tmp;

                bool isCompressed = true;

                byte[] dataChunk = br.ReadBytes(compressedSize);

                byte[] ZstdMagicArray = [dataChunk[0], dataChunk[1], dataChunk[2], dataChunk[3]];
                uint ZstdMagic = BitConverter.ToUInt32(ZstdMagicArray);

                if (ZstdMagic != 4247762216)
                    isCompressed = false;

                DecompressAndWrite(dataChunk, writer, isCompressed);
            }
        }

        /// <summary>
        /// Compresses data from a byte array and returns it.
        /// </summary>
        /// <param name="input">Input Byte array.</param>
        /// <returns>Returns compressed byte array.</returns>
        /// <history>
        /// [Wouldubeinta]		08/07/2025	Created
        /// </history>
        public static byte[] CompressAndRead(byte[] input)
        {
            byte[]? buffer = null;
            Compressor? compressor = null;

            try
            {
                compressor = new();
                compressor.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_dictIDFlag, 1);
                compressor.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_contentSizeFlag, 0);
                compressor.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_compressionLevel, 3);
                buffer = compressor.Wrap(input).ToArray();
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            return buffer;
        }

        /// <summary>
        /// Decompresses the compressed chunk data, if compressed. Then writes appending chunk data to a file.
        /// </summary>
        /// <param name="input">byte[] array chunk data.</param>
        /// <param name="writer">FileStream writer.</param>
        /// <param name="isCompressed">Is the data compressed</param>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static void DecompressAndWrite(byte[] input, FileStream writer, bool isCompressed)
        {
            Decompressor? decompressor = null;

            try
            {
                if (isCompressed)
                {
                    decompressor = new();
                    byte[] buffer = decompressor.Unwrap(input).ToArray();
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
                if (decompressor != null) { decompressor.Dispose(); decompressor = null; }
            }
        }

        /// <summary>
        /// Compresses chunk data and appends to output file.
        /// </summary>
        /// <param name="data">Byte array to be compressed.</param>
        /// <param name="writer">FileStream output writer.</param>
        /// <param name="offset">Compressed chunk offset.</param>
        /// <param name="chunkSize">Data chunk size.</param>
        /// <returns>Returns compressed chunk size.</returns>
        /// <history>
        /// [Wouldubeinta]		13/07/2025	Created
        /// </history>
        public static int CompressAndWrite(byte[] data, FileStream writer, ref int offset, int chunkSize = 262144)
        {
            MemoryStream? stream = null;
            Compressor? compressor = null;
            int compressedSize = 0;

            try
            {
                stream = new(data);
                compressor = new();
                compressor.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_dictIDFlag, 1);
                compressor.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_contentSizeFlag, 0);
                compressor.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_compressionLevel, 4);

                int readCount = 0;
                byte[]? buffer = new byte[chunkSize];

                while ((readCount = stream.Read(buffer, 0, chunkSize)) != 0)
                {
                    offset = (int)writer.Position;
                    byte[]? output = compressor.Wrap(buffer).ToArray();
                    writer.Write(output, 0, output.Length);
                    writer.Flush();
                    compressedSize = output.Length;
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (stream != null) { stream.Dispose(); stream = null; }
                if (compressor != null) { compressor.Dispose(); compressor = null; }
            }
            return compressedSize;
        }

        /// <summary>
        /// Compresses chunk data and appends to output file.
        /// </summary>
        /// <param name="data">Byte array to be compressed.</param>
        /// <param name="writer">FileStream output writer.</param>
        /// <param name="chunkSize">Data chunk size.</param>
        /// <history>
        /// [Wouldubeinta]		14/07/2025	Created
        /// </history>
        public static void CompressAndWrite(byte[] data, FileStream writer, int chunkSize = 262144)
        {
            Compressor? compressor = null;

            try
            {
                compressor = new();
                compressor.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_dictIDFlag, 1);
                compressor.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_contentSizeFlag, 0);
                compressor.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_compressionLevel, 4);

                byte[]? output = compressor.Wrap(data).ToArray();

                if (output.Length < chunkSize)
                {
                    writer.Write(BitConverter.GetBytes(output.Length + 4), 0, 4);
                    writer.Write(output, 0, output.Length);
                    writer.Flush();
                }
                else
                {
                    writer.Write(BitConverter.GetBytes(data.Length + 4), 0, 4);
                    writer.Write(data, 0, data.Length);
                    writer.Flush();
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (compressor != null) { compressor.Dispose(); compressor = null; }
            }
        }

        /// <summary>
        /// Decompresses the compressed chunk data, if compressed. Then read's the headers magic to determine the file type.
        /// </summary>
        /// <param name="input">byte[] array chunk data.</param>
        /// <param name="isCompressed">Is the data compressed</param>
        /// <history>
        /// [Wouldubeinta]		01/07/2025	Created
        /// </history>
        public static int DecompressAndReadMagic(byte[] input, bool isCompressed)
        {
            Decompressor? decompressor = null;
            int magic = 0;
            byte[]? buffer = null;
            bool isMiniTXPK = false;

            try
            {
                if (isCompressed)
                {
                    decompressor = new();
                    buffer = decompressor.Unwrap(input).ToArray();

                    if (buffer[20] == 99 && buffer[21] == 111 && buffer[22] == 110 && buffer[23] == 118)  // checking for "conv" string
                        isMiniTXPK = true;
                }
                else
                    buffer = input;

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
                if (decompressor != null) { decompressor.Dispose(); decompressor = null; }
            }
            return magic;
        }

        /// <summary>
        /// M3MP Decompress extracts all the files from a decompressed tmp m3mp file.
        /// </summary>
        /// <param name="m3mpTempFile">Temporary m3mp file path.</param>
        /// <param name="folderPath">Output folder location.</param>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
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
                    DecompressAndWrite(compressedChunkData, fsWriter, true);

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
        /// Get's TXPK header info.
        /// </summary>
        /// <param name="fileIn">File path to txpk.</param>
        /// <returns>Returns the txpk header info.</returns>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static TXPK ReadTXPKInfo(string fileIn)
        {
            Reader? input = null;
            Reader? txpk_br = null;
            TXPK? txpk = null;

            try
            {
                uint MainCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainCompressedSize;
                uint MainUnCompressedSize = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex].MainUnCompressedSize;

                input = new(fileIn);

                if (MainCompressedSize != MainUnCompressedSize)
                {
                    int txpkCompressedChunkSize = input.ReadInt32();
                    int txpkTmp = txpkCompressedChunkSize -= 4;
                    txpkCompressedChunkSize = txpkTmp;

                    bool isTxpkCompressed = true;

                    byte[] txpkData = input.ReadBytes(txpkCompressedChunkSize);

                    byte[] ZstdMagicArray = [txpkData[0], txpkData[1], txpkData[2], txpkData[3]];
                    uint ZstdMagic = BitConverter.ToUInt32(ZstdMagicArray);

                    if (ZstdMagic != 4247762216)
                        isTxpkCompressed = false;

                    byte[] txpkHeader = DecompressAndRead(txpkData, isTxpkCompressed);

                    txpk_br = new(txpkHeader);

                    txpk = new();
                    txpk.Deserialize(txpk_br);
                }
                else
                {
                    txpk = new();
                    txpk.Deserialize(input);
                }
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
        /// [Wouldubeinta]		08/07/2025	Created
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

                        bool isM3mpCompressed = true;

                        byte[] m3mpData = input.ReadBytes(m3mpCompressedSize);

                        byte[] ZstdMagicArray = [m3mpData[0], m3mpData[1], m3mpData[2], m3mpData[3]];
                        uint ZstdMagic = BitConverter.ToUInt32(ZstdMagicArray);

                        if (ZstdMagic != 4247762216)
                            isM3mpCompressed = false;

                        m3mpHeaderChunk.Add(DecompressAndRead(m3mpData, isM3mpCompressed));
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

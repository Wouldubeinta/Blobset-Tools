using BlobsetIO;
using Concentus;
using Concentus.Oggfile;
using PackageIO;
using System.Xml;
using System.Xml.Serialization;

namespace Blobset_Tools
{
    /// <summary>
    /// IO Operations Class
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
    /// [Wouldubeinta]	   02/06/2025	Created
    /// </history>
    public class IO
    {
        /// <summary>
        /// Read data from a Binary Reader and writes to file in chunks.
        /// </summary>
        /// <param name="reader">Input Binary Reader stream.</param>
        /// <param name="fileOut">Output file path.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static void ReadWriteData(Reader reader, string fileOut, int chunkSize = 262144)
        {
            FileStream? writer = null;

            try
            {
                writer = new(fileOut, FileMode.OpenOrCreate, FileAccess.Write);

                int readCount = 0;
                byte[] buffer = new byte[chunkSize];

                while ((readCount = reader.Read(buffer, 0, chunkSize)) != 0)
                {
                    writer.Write(buffer, 0, readCount);
                    writer.Flush();
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (writer != null) { writer.Dispose(); writer = null; }
            }
        }

        /// <summary>
        /// Read data from a Binary Reader and writes to file in chunks.
        /// </summary>
        /// <param name="reader">Input Binary Reader stream.</param>
        /// <param name="writer">Output FileStream writer.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static void ReadWriteData(Reader reader, FileStream writer, int chunkSize = 262144)
        {
            try
            {
                int readCount = 0;
                byte[] buffer = new byte[chunkSize];

                while ((readCount = reader.Read(buffer, 0, chunkSize)) != 0)
                {
                    writer.Write(buffer, 0, readCount);
                    writer.Flush();
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }

        /// <summary>
        /// Read data from a Binary Reader and writes to file in chunks.
        /// </summary>
        /// <param name="fileIn">Input file path.</param>
        /// <param name="writer">Output FileStream writer.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static void ReadWriteData(string fileIn, FileStream writer, int chunkSize = 262144)
        {
            Reader? reader = null;

            try
            {
                reader = new Reader(fileIn);

                int readCount = 0;
                byte[] buffer = new byte[chunkSize];

                while ((readCount = reader.Read(buffer, 0, chunkSize)) != 0)
                {
                    writer.Write(buffer, 0, readCount);
                    writer.Flush();
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (reader != null) { reader.Close(); reader = null; }
            }
        }

        /// <summary>
        /// Read data from a Byte[] array and writes chunk to file.
        /// </summary>
        /// <param name="data">Input byte[] array.</param>
        /// <param name="writer">Output FileStream writer.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static void ReadWriteData(byte[] data, FileStream writer, int chunkSize = 262144)
        {
            try
            {
                int tmpSize = chunkSize;

                if (chunkSize > data.Length)
                    tmpSize = data.Length;

                writer.Write(data, 0, tmpSize);
                writer.Flush();
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }

        /// <summary>
        /// Read data from a Binary Reader and writes to file in chunks.
        /// </summary>
        /// <param name="reader">Input Binary Reader stream.</param>
        /// <param name="fileOut">Output file path.</param>
        /// <param name="size">File size.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <history>
        /// [Wouldubeinta]		18/11/2025	Created
        /// </history>
        public static void ReadWriteCunkData(Reader reader, string fileOut, int size, int chunkSize = 262144)
        {
            FileStream? writer = null;

            try
            {
                writer = new(fileOut, FileMode.OpenOrCreate, FileAccess.Write);

                int chunkCount = Utilities.ChunkAmount(size);
                long[] chunkSizes = Utilities.ChunkSizes(size, chunkCount);

                for (int j = 0; j < chunkCount; j++)
                {
                    byte[] tmpData = reader.ReadBytes((int)chunkSizes[j], Endian.Little);
                    ReadWriteData(tmpData, writer, (int)chunkSizes[j]);
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (writer != null) { writer.Dispose(); writer = null; }
            }
        }

        /// <summary>
        /// Read data from a Binary Reader and writes to dds file in chunks + header for consoles.
        /// </summary>
        /// <param name="reader">Input Binary Reader stream.</param>
        /// <param name="fileOut">Output file path.</param>
        /// <param name="size">File size.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <history>
        /// [Wouldubeinta]		1/12/2025	Created
        /// </history>
        public static void ReadWriteCunkDDSData(Reader reader, string fileOut, int size, Mini_TXPK mini_txpk, int chunkSize = 262144)
        {
            FileStream? writer = null;

            try
            {
                writer = new(fileOut, FileMode.OpenOrCreate, FileAccess.Write);

                byte[] tmpData = reader.ReadBytes(size, Endian.Little);
                PS3_DDS consoleDDS = new(mini_txpk.DDSHeight, mini_txpk.DDSWidth, mini_txpk.DDSMipMaps, mini_txpk.DDSType, size, tmpData);
                byte[] ddsData = consoleDDS.WriteDDS();
                ReadWriteData(ddsData, writer, ddsData.Length);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (writer != null) { writer.Dispose(); writer = null; }
            }
        }

        /// <summary>
        /// Read byte[] data from a Binary Reader and return.
        /// </summary>
        /// <param name="blobsetFile">Blobset file location.</param>
        /// <param name="size">File size.</param>
        /// <returns>Returns read data.</returns>
        /// <history>
        /// [Wouldubeinta]		25/11/2025	Created
        /// </history>
        public static byte[] ReadData(string blobsetFile, uint offset, int size)
        {
            Reader? reader = null;
            byte[]? buffer = null;

            try
            {
                reader = new(blobsetFile, Endian.Little);
                reader.Position = offset;
                buffer = new byte[size];
                reader.Read(buffer, 0, size);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (reader != null) { reader.Close(); reader = null; }
            }
            return buffer;
        }

        /// <summary>
        /// Read data from a Byte[] array and writes modify uncompressed data.
        /// </summary>
        /// <param name="data">Input byte[] array.</param>
        /// <param name="writer">Output FileStream writer.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static void ReadWriteModifyData(byte[] data, FileStream writer, int chunkSize = 262144)
        {
            try
            {
                int tmpSize = chunkSize;

                if (chunkSize > data.Length)
                    tmpSize = data.Length;

                writer.Write(BitConverter.GetBytes(tmpSize + 4), 0, 4);
                writer.Write(data, 0, tmpSize);
                writer.Flush();
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }

        /// <summary>
        /// Read data from a file path and writes to file in chunks.
        /// </summary>
        /// <param name="reader">Input Binary Reader stream.</param>
        /// <param name="fileOut">Output file path.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static void ReadWriteData(string fileIn, string fileOut, int chunkSize = 262144)
        {
            Reader? reader = null;
            FileStream? writer = null;

            try
            {
                reader = new(fileIn);
                writer = new(fileOut, FileMode.OpenOrCreate, FileAccess.Write);

                int readCount = 0;
                byte[] buffer = new byte[chunkSize];

                while ((readCount = reader.Read(buffer, 0, chunkSize)) != 0)
                {
                    writer.Write(buffer, 0, readCount);
                    writer.Flush();
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (reader != null) { reader.Close(); reader = null; }
                if (writer != null) { writer.Dispose(); writer = null; }
            }
        }

        /// <summary>
        /// Extracts all the dds files from a decompressed tmp txpk file.
        /// </summary>
        /// <param name="tmpFilePath">Temporary txpk file path.</param>
        /// <param name="folderPath">Output folder location.</param>
        /// <param name="MainUncompressedSize">TXPK header Size.</param>
        /// <returns>Returns the extracted dds file path progress.</returns>
        /// <history>
        /// [Wouldubeinta]		30/06/2025	Created
        /// </history>
        public static string TXPK_DDS_Extractor(string tmpFilePath, string folderPath, int MainUncompressedSize)
        {
            FileStream? writer = null;
            Reader? br = null;
            string progressFile = string.Empty;

            try
            {
                br = new(tmpFilePath);

                TXPK txpk = new();
                txpk.Deserialize(br);

                foreach (var entry in txpk.Entries)
                {
                    string ddsFilePath = entry.DDSFilePath.Replace("/", @"\") + ".dds";
                    string filePath = Path.Combine(folderPath, ddsFilePath);
                    Directory.CreateDirectory(Path.GetDirectoryName(filePath));
                    writer = new(filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    br.Position = entry.DDSDataOffset + MainUncompressedSize;

                    if (Global.isBigendian)
                    {
                        byte[] tempData = br.ReadBytes((int)entry.DDSDataSize, Endian.Little);
                        PS3_DDS consoleDDS = new(entry.DDSHeight, entry.DDSWidth, entry.DDSMipMaps, entry.DDSType, (int)entry.DDSDataSize, tempData);
                        byte[] ddsData = consoleDDS.WriteDDS();
                        writer.Write(ddsData, 0, ddsData.Length);
                        writer.Flush();
                    }
                    else
                    {
                        int chunkCount = Utilities.ChunkAmount((int)entry.DDSDataSize);
                        long[] chunkSizes = Utilities.ChunkSizes((int)entry.DDSDataSize, chunkCount);

                        for (int i = 0; i < chunkCount; i++)
                        {
                            byte[] chunkData = br.ReadBytes((int)chunkSizes[i], Endian.Little);
                            writer.Write(chunkData, 0, (int)chunkSizes[i]);
                            writer.Flush();
                        }
                    }

                    if (writer != null) { writer.Dispose(); writer = null; }
                    progressFile = ddsFilePath;
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (br != null) { br.Close(); br = null; }
                if (writer != null) { writer.Dispose(); writer = null; }
            }
            return progressFile;
        }

        /// <summary>
        /// Combines a List of byte arrays.
        /// </summary>
        /// <param name="data">List of byte arrays.</param>
        /// <param name="dataSize">Size of combined data</param>
        /// <returns>Returns combined data.</returns>
        /// <history>
        /// [Wouldubeinta]		15/06/2025	Created
        /// </history>
        public static byte[] CombineDataChunks(List<byte[]> data, int dataSize)
        {
            MemoryStream? memoryStream = null;
            byte[] buffer = new byte[dataSize];

            try
            {
                memoryStream = new(buffer);

                for (int i = 0; i < data.Count; i++)
                    memoryStream.Write(data[i], 0, data[i].Length);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (memoryStream != null) { memoryStream.Close(); memoryStream = null; }
            }
            return buffer;
        }

        /// <summary>
        /// Read pcm data from a memory stream and writes wav data to memory stream.
        /// </summary>
        /// <param name="ms">Input memory stream.</param>
        /// <param name="sampleRate">PCM sample rate.</param>
        /// <param name="numChannels">Number of channels in pcm data.</param>
        /// <param name="sampleCount">How many samples in pcm data.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <returns>Returns wav data.</returns>
        /// <history>
        /// [Wouldubeinta]		28/07/2025	Created
        /// </history>
        public static MemoryStream WriteVorbisOggWAVData(MemoryStream? ms, uint sampleRate, int numChannels, uint sampleCount, int chunkSize = 256)
        {
            MemoryStream? wavStream = null;
            OggSharp.VorbisFile? vorbFile = null;
            int _chunkSize = chunkSize;

            try
            {
                if (numChannels == 1)
                    _chunkSize = 64;

                wavStream = new MemoryStream();
                ms.Position = 0;
                vorbFile = new OggSharp.VorbisFile(ms);

                double duration = (double)sampleCount / sampleRate;
                int pcmSize = (int)(sampleRate * 2 * 2 * duration);
                WavHeader wavHeader = new((int)sampleRate, numChannels);
                wavHeader.headerSize = pcmSize + 38;
                wavHeader.pcmDataSize = pcmSize;
                wavHeader.Serialize(wavStream);

                int chunkCount = Utilities.ChunkAmount(pcmSize, _chunkSize);
                long[] chunkSizes = Utilities.ChunkSizes(pcmSize, chunkCount, _chunkSize);

                int readCount = 0;

                for (int i = 0; i < chunkCount; i++)
                {
                    byte[] buffer = new byte[chunkSizes[i]];

                    readCount = vorbFile.read(buffer, buffer.Length, 0, 2, 1, [0]);
                    wavStream.Write(buffer, 0, buffer.Length);
                    wavStream.Flush();
                }
                wavStream.Position = 0;
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (ms != null) { ms.Dispose(); ms = null; }
                if (vorbFile != null) { vorbFile.Dispose(); vorbFile = null; }
            }
            return wavStream;
        }

        /// <summary>
        /// Read pcm data from a memory stream and writes wav data to memory stream.
        /// </summary>
        /// <param name="ms">Input memory stream.</param>
        /// <param name="sampleRate">PCM sample rate.</param>
        /// <param name="numChannels">Number of channels in pcm data.</param>
        /// <param name="chunkSize">Chunk size to write to file.</param>
        /// <returns>Returns wav data.</returns>
        /// <history>
        /// [Wouldubeinta]		29/07/2025	Created
        /// </history>
        public static MemoryStream WriteOpusOggWAVData(MemoryStream? ms, int sampleRate, int numChannels, int chunkSize = 512)
        {
            MemoryStream? pcmStream = null;
            MemoryStream? wavStream = null;

            try
            {
                pcmStream = new MemoryStream();
                wavStream = new MemoryStream();

                IOpusDecoder decoder = OpusCodecFactory.CreateDecoder(sampleRate, numChannels);
                OpusOggReadStream oggIn = new(decoder, ms);
                while (oggIn.HasNextPacket)
                {
                    short[] packet = oggIn.DecodeNextPacket();
                    if (packet != null)
                    {
                        for (int i = 0; i < packet.Length; i++)
                        {
                            var bytes = BitConverter.GetBytes(packet[i]);
                            pcmStream.Write(bytes, 0, bytes.Length);
                        }
                    }
                }
                pcmStream.Position = 0;

                WavHeader wavHeader = new(sampleRate, numChannels);
                wavHeader.headerSize = (int)pcmStream.Length + 38;
                wavHeader.pcmDataSize = (int)pcmStream.Length;
                wavHeader.Serialize(wavStream);

                pcmStream.CopyTo(wavStream);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (ms != null) { ms.Dispose(); ms = null; }
                if (pcmStream != null) { pcmStream.Dispose(); pcmStream = null; }
            }
            return wavStream;
        }

        public static byte[] WriteDDS(DDS.HEADER header, int ddsDataSize, byte[]? bdata)
        {
            MemoryStream? ms = null;
            Writer? bw = null;
            byte[]? data = new byte[ddsDataSize + 128];

            try
            {
                ms = new(data);
                bw = new(ms);
                bw.WriteUInt32(0x20534444, Endian.Little);
                bw.WriteUInt32(header.size, Endian.Little);
                bw.WriteUInt32((uint)header.flags, Endian.Little);
                bw.WriteUInt32(header.height, Endian.Little);
                bw.WriteUInt32(header.width, Endian.Little);
                bw.WriteUInt32(header.pitchOrLinearSize, Endian.Little);
                bw.WriteUInt32(header.depth, Endian.Little);
                bw.WriteUInt32(header.mipMapCount, Endian.Little);
                foreach (uint u in header.reserved1)
                {
                    bw.WriteUInt32(u, Endian.Little);
                }
                bw.WriteUInt32(header.ddspf.size, Endian.Little);
                bw.WriteUInt32((uint)header.ddspf.flags, Endian.Little);
                bw.WriteUInt32(header.ddspf.fourCC, Endian.Little);
                bw.WriteUInt32(header.ddspf.rGBBitCount, Endian.Little);
                bw.WriteUInt32(header.ddspf.rBitMask, Endian.Little);
                bw.WriteUInt32(header.ddspf.gBitMask, Endian.Little);
                bw.WriteUInt32(header.ddspf.bBitMask, Endian.Little);
                bw.WriteUInt32(header.ddspf.aBitMask, Endian.Little);
                bw.WriteUInt32((uint)header.caps, Endian.Little);
                bw.WriteUInt32((uint)header.caps2, Endian.Little);
                bw.WriteUInt32(header.caps3, Endian.Little);
                bw.WriteUInt32(header.caps4, Endian.Little);
                bw.WriteUInt32(header.reserved2, Endian.Little);

                if (bdata != null)
                    bw.Write(bdata, Endian.Little);
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
            finally
            {
                if (ms != null) { ms.Dispose(); }
                if (bw != null) { bw.Close(); }
            }
            return data;
        }

        public static void Create_PS3_XML(Mini_TXPK mini_TXPK, string filePath, int index)
        {
            string dir = Path.GetDirectoryName(filePath);
            Directory.CreateDirectory(dir);
            PS3_DDS_Header header = new();

            header.Entries = new PS3_DDS_Header.Entry[1];
            header.Index = index;
            header.Entries[0] = new PS3_DDS_Header.Entry();
            header.Entries[0].BufferSize = mini_TXPK.BufferSize;
            header.Entries[0].DDSType = mini_TXPK.DDSType;
            header.Entries[0].DDSHeight = mini_TXPK.DDSHeight2;
            header.Entries[0].DDSWidth = mini_TXPK.DDSWidth2;
            header.Entries[0].DDSImageType = mini_TXPK.DDSImageType2;
            header.Entries[0].DDSMipMaps = mini_TXPK.DDSMipMaps;
            header.Entries[0].FilePath = mini_TXPK.DDSFilePath;
            header.Entries[0].Unknown1 = mini_TXPK.Unknown1;
            header.Entries[0].Unknown2 = mini_TXPK.Unknown2;
            header.Entries[0].Unknown3 = mini_TXPK.Unknown3;
            header.Entries[0].Unknown4 = mini_TXPK.Unknown4;
            header.Entries[0].Unknown5 = mini_TXPK.Unknown5;
            header.Entries[0].Reserved = mini_TXPK.Reserved;

            XmlSerialize(filePath, header);
        }

        /// <summary>
        /// Deserialize XML data.
        /// </summary>
        /// <param name="fileIn">Xml file in.</param>
        /// <returns>Returns generic type class data.</returns>
        /// <history>
        /// [Wouldubeinta]		14/07/2025	Created
        /// </history>
        public static T XmlDeserialize<T>(string fileIn)
        {
            StreamReader? reader = null;
            T? xmlData = default;

            try
            {
                XmlSerializer serializer = new(typeof(T));
                reader = new(fileIn);
                xmlData = (T)serializer.Deserialize(reader);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                if (reader != null) { reader.Dispose(); reader = null; }
            }
            return xmlData;
        }

        /// <summary>
        /// Serialize XML data.
        /// </summary>
        /// <param name="fileout">Xml file out.</param>
        /// <param name="data">Generic Type data.</param>
        /// <history>
        /// [Wouldubeinta]		14/07/2025	Created
        /// </history>
        public static void XmlSerialize<T>(string fileout, T data)
        {
            StringWriter? stringWriter = null;

            try
            {
                XmlWriterSettings settings = new();
                settings.Indent = true;
                settings.OmitXmlDeclaration = true;
                settings.NewLineHandling = NewLineHandling.None;
                stringWriter = new();
                XmlWriter writer = XmlWriter.Create(stringWriter, settings);
                XmlSerializer serializer = new(typeof(T));
                XmlSerializerNamespaces ns = new();
                ns.Add(string.Empty, string.Empty);
                serializer.Serialize(writer, data, ns);
                string startDocuments = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
                File.WriteAllText(fileout, startDocuments + "\n" + stringWriter.ToString());
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                if (stringWriter != null) { stringWriter.Dispose(); stringWriter = null; }
            }
        }
    }
}

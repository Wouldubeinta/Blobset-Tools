using BlobsetIO;
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
                writer = new (fileOut, FileMode.OpenOrCreate, FileAccess.Write);

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
        /// Read data from a Binary Reader and writes to file in chunks.
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
                MessageBox.Show("Error occurred, report it to Wouldy : " + Extract.debugTest + " - " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
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
                reader = new (fileIn);
                writer = new (fileOut, FileMode.OpenOrCreate, FileAccess.Write);

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
        public static string TXPKDDSExtractor(string tmpFilePath, string folderPath, int MainUncompressedSize)
        {
            FileStream? fsWriter = null;
            Reader? br = null;
            string progressFile = string.Empty;

            try
            {
                br = new (tmpFilePath);

                TXPK txpk = new();
                txpk.Deserialize(br);

                foreach (var entry in txpk.Entries)
                {
                    string ddsFilePath = entry.DDSFilePath.Replace("/", @"\") + ".dds";
                    string filePath = folderPath + ddsFilePath;
                    Directory.CreateDirectory(Path.GetDirectoryName(filePath));
                    fsWriter = new (filePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);

                    br.Position = entry.DDSDataOffset + MainUncompressedSize;

                    int chunkCount = Utilities.ChunkAmount((int)entry.DDSDataSize1);
                    long[] chunkSizes = Utilities.ChunkSizes((int)entry.DDSDataSize1, chunkCount);

                    for (int i = 0; i < chunkCount; i++)
                    {
                        byte[] chunkData = br.ReadBytes((int)chunkSizes[i]);
                        fsWriter.Write(chunkData, 0, (int)chunkSizes[i]);
                        fsWriter.Flush();
                    }

                    if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
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
                if (fsWriter != null) { fsWriter.Dispose(); fsWriter = null; }
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
                memoryStream = new (buffer);

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
                reader = new (fileIn);
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
                stringWriter = new ();
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

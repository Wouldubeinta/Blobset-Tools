using BlobsetIO;
using System.Media;
using WEMSharp;
using static Blobset_Tools.Enums;

namespace Blobset_Tools
{
    internal class UiFileTypes
    {
        #region "DDS"
        public static void DDS(RichTextBox fileInfo_richTextBox, string filePath, PictureBox dds_pictureBox, int blobsetVersion, bool isAlpha, bool isFlipped)
        {
            var blobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];

            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            //string platform = platformDetails["Platform"];
            string platformExt = platformDetails["PlatformExt"];

            Structs.DDSInfo ddsInfo = new();
            byte[] ddsData = blobsetVersion > 1 ? UI.GetDDSData_V3_V4(Global.filelist) : UI.GetDDSData_V1_V2(Global.filelist);

            if (ddsData == null) return;

            Bitmap bitmap = UI.DDStoBitmap(ddsData, isAlpha, ref ddsInfo);

            if (bitmap != null)
            {
                dds_pictureBox.Image = bitmap;

                if (isFlipped)
                {
                    dds_pictureBox.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
                    dds_pictureBox.Refresh();
                }
            }

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** DDS Location ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"{Global.filelist[Global.fileIndex].FilePath}{Environment.NewLine}{Environment.NewLine}");

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"FileIndex: {Global.filelist[Global.fileIndex].BlobsetIndex}{Environment.NewLine}");

            if (blobsetVersion == (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"FileName: {filePath}{Environment.NewLine}");
            else
            {
                fileInfo_richTextBox.AppendText($"FolderHash: {Global.filelist[Global.fileIndex].FolderHash}{Environment.NewLine}");
                fileInfo_richTextBox.AppendText($"FileHash: {Global.filelist[Global.fileIndex].FileHash}{Environment.NewLine}");
                fileInfo_richTextBox.AppendText($"Blobset Number: data-{blobsetHeaderData.BlobSetNumber}.blobset.{platformExt}{Environment.NewLine}");
            }

            if (blobsetVersion != (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"MainFinalOffset: {blobsetHeaderData.MainFinalOffSet}{Environment.NewLine}");

            fileInfo_richTextBox.AppendText($"MainCompressedSize: {blobsetHeaderData.MainCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"MainUnCompressedSize: {blobsetHeaderData.MainUnCompressedSize}{Environment.NewLine}");

            if (blobsetVersion != (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"VramFinalOffset: {blobsetHeaderData.VramFinalOffSet}{Environment.NewLine}");

            fileInfo_richTextBox.AppendText($"VramCompressedSize: {blobsetHeaderData.VramCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramUnCompressedSize: {blobsetHeaderData.VramUnCompressedSize}{Environment.NewLine}{Environment.NewLine}");

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** DDS Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;

            string ddsFormat = ddsInfo.isDX10 ? $"{ddsInfo.dxgiFormat} - DX11+" : ddsInfo.CompressionAlgorithm.ToString();

            fileInfo_richTextBox.AppendText($"Format: {ddsFormat}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"Height: {ddsInfo.Height}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"Width: {ddsInfo.Width}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"MipMaps: 1 / {ddsInfo.MipMap}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"Size: {Utilities.FormatSize((ulong)ddsInfo.Size)}");
        }
        #endregion

        #region "TXPK"
        public static void TXPK(RichTextBox fileInfo_richTextBox, string filePath, PictureBox dds_pictureBox, int blobsetVersion)
        {
            var blobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];
            uint txpkSize = blobsetHeaderData.MainUnCompressedSize + blobsetHeaderData.VramUnCompressedSize;

            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            //string platform = platformDetails["Platform"];
            string platformExt = platformDetails["PlatformExt"];

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** TXPK Location ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"{Global.filelist[Global.fileIndex].FilePath}{Environment.NewLine}{Environment.NewLine}");

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"FileIndex: {Global.filelist[Global.fileIndex].BlobsetIndex}{Environment.NewLine}");

            if (blobsetVersion == (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"FileName: {filePath}{Environment.NewLine}");
            else
            {
                fileInfo_richTextBox.AppendText($"FolderHash: {Global.filelist[Global.fileIndex].FolderHash}{Environment.NewLine}");
                fileInfo_richTextBox.AppendText($"FileHash: {Global.filelist[Global.fileIndex].FileHash}{Environment.NewLine}");
                fileInfo_richTextBox.AppendText($"Blobset Number: data-{blobsetHeaderData.BlobSetNumber}.blobset.{platformExt}{Environment.NewLine}");
            }

            if (blobsetVersion != (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"MainFinalOffset: {blobsetHeaderData.MainFinalOffSet}{Environment.NewLine}");

            fileInfo_richTextBox.AppendText($"MainCompressedSize: {blobsetHeaderData.MainCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"MainUnCompressedSize: {blobsetHeaderData.MainUnCompressedSize}{Environment.NewLine}");

            if (blobsetVersion != (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"VramFinalOffset: {blobsetHeaderData.VramFinalOffSet}{Environment.NewLine}");

            fileInfo_richTextBox.AppendText($"VramCompressedSize: {blobsetHeaderData.VramCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramUnCompressedSize: {blobsetHeaderData.VramUnCompressedSize}{Environment.NewLine}{Environment.NewLine}");

            // Read TXPK information based on blobset version
            TXPK txpk = blobsetVersion > 1 ? ZSTD_IO.ReadTXPKInfo(filePath) : LZMA_IO.ReadTXPKInfo(Global.filelist);
            if (txpk == null) return;

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** TXPK Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"DDS File Count: {txpk.FilesCount}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"TXPK File Size: {Utilities.FormatSize(txpkSize)}{Environment.NewLine}{Environment.NewLine}");

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** TXPK DDS File List ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;

            if (txpk.Entries == null) return;

            for (int i = 0; i < txpk.Entries.Length; i++)
            {
                var entry = txpk.Entries[i];
                fileInfo_richTextBox.AppendText($"{i} - {entry.DDSFilePath.Replace("/", @"\")}.dds{Environment.NewLine}");
            }
        }
        #endregion

        #region "M3MP"
        public static void M3MP(RichTextBox fileInfo_richTextBox, string filePath, PictureBox dds_pictureBox, int blobsetVersion)
        {
            // Retrieve blobset metadata
            var blobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];

            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            //string platform = platformDetails["Platform"];
            string platformExt = platformDetails["PlatformExt"];

            // Display blobset information
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** M3MP Location ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"FileIndex: {Global.filelist[Global.fileIndex].BlobsetIndex}" + Environment.NewLine);

            if (blobsetVersion == (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"FileName: {filePath}{Environment.NewLine}");
            else
            {
                fileInfo_richTextBox.AppendText($"FolderHash: {Global.filelist[Global.fileIndex].FolderHash}{Environment.NewLine}");
                fileInfo_richTextBox.AppendText($"FileHash: {Global.filelist[Global.fileIndex].FileHash}{Environment.NewLine}");
                fileInfo_richTextBox.AppendText($"Blobset Number: data-{blobsetHeaderData.BlobSetNumber}.blobset.{platformExt}{Environment.NewLine}");
            }

            if (blobsetVersion != (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"MainFinalOffset: {blobsetHeaderData.MainFinalOffSet}{Environment.NewLine}");

            fileInfo_richTextBox.AppendText($"MainCompressedSize: {blobsetHeaderData.MainCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"MainUnCompressedSize: {blobsetHeaderData.MainUnCompressedSize}{Environment.NewLine}");

            if (blobsetVersion != (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"VramFinalOffset: {blobsetHeaderData.VramFinalOffSet}{Environment.NewLine}");

            fileInfo_richTextBox.AppendText($"VramCompressedSize: {blobsetHeaderData.VramCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramUnCompressedSize: {blobsetHeaderData.VramUnCompressedSize}{Environment.NewLine}{Environment.NewLine}");

            // Determine if the m3mp data is compressed
            bool isCompressed = blobsetHeaderData.MainCompressedSize != blobsetHeaderData.MainUnCompressedSize;

            // Read M3MP information based on blobset version
            M3MP m3mp = blobsetVersion > 2 ? ZSTD_IO.ReadM3MPInfo(filePath, isCompressed) : LZMA_IO.ReadM3MPInfo(Global.filelist);
            if (m3mp == null) return;

            // Display M3MP information
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** M3MP Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"M3MP File Count: {m3mp.FilesCount}" + Environment.NewLine);
            fileInfo_richTextBox.AppendText($"M3MP File Size: {Utilities.FormatSize(blobsetHeaderData.MainUnCompressedSize)}" + Environment.NewLine + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** M3MP File List ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;

            if (m3mp.UnCompressedEntries == null) return;

            int i = 0;
            foreach (var entry in m3mp.UnCompressedEntries)
            {
                fileInfo_richTextBox.AppendText($"{i} - {entry.FilePath.Replace("/", @"\")}" + Environment.NewLine);
                i++;
            }
        }
        #endregion

        #region "WEM"
        public static void Wise_WEM(RichTextBox fileInfo_richTextBox, string filePath, PictureBox dds_pictureBox, int blobsetVersion, SoundPlayer player, byte[] oggData, byte[] wavData)
        {
            // Retrieve blobset metadata
            var blobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];

            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            //string platform = platformDetails["Platform"];
            string platformExt = platformDetails["PlatformExt"];

            // Display blobset information
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Wise Audio WEM Location ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"FileIndex: {Global.filelist[Global.fileIndex].BlobsetIndex}" + Environment.NewLine);
            fileInfo_richTextBox.AppendText($"FileName: {filePath}" + Environment.NewLine);

            fileInfo_richTextBox.AppendText($"MainCompressedSize: {blobsetHeaderData.MainCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"MainUnCompressedSize: {blobsetHeaderData.MainUnCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramCompressedSize: {blobsetHeaderData.VramCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramUnCompressedSize: {blobsetHeaderData.VramUnCompressedSize}{Environment.NewLine}{Environment.NewLine}");

            // Stop and dispose of the current player if it exists
            StopAndDisposePlayer(ref player);

            // Process the WEM file
            WEMFile? wem = new(filePath, WEMForcePacketFormat.NoForcePacketFormat);
            using MemoryStream wem_Ms = new();

            // Generate OGG data
            wem.GenerateOGG(wem_Ms, Global.currentPath + @"\packed_codebooks_aoTuV_603.bin", false, false);
            oggData = wem_Ms.ToArray();

            // Generate WAV data
            using MemoryStream wav_ms = IO.WriteVorbisOggWAVData(wem_Ms, wem.SampleRate, wem.Channels, wem.SampleCount);
            wavData = wav_ms.ToArray();

            // Play the WAV audio
            player = new SoundPlayer(wav_ms);
            player.Play();

            // Display WEM information
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Wise Audio WEM Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"WEM Channel Count: {wem.Channels}" + Environment.NewLine);
            fileInfo_richTextBox.AppendText($"WEM Sample Rate: {wem.SampleRate} Hz" + Environment.NewLine);
            fileInfo_richTextBox.AppendText($"WEM Average Bytes Per Second: {Utilities.FormatSize(wem.AverageBytesPerSecond)}" + Environment.NewLine);
            fileInfo_richTextBox.AppendText($"WEM File Size: {Utilities.FormatSize((ulong)Utilities.FileInfo(filePath))}");

            // Dispose of WEM resources
            DisposeWEMResources(wem);
        }

        private static void DisposeWEMResources(WEMFile? wem)
        {
            if (wem?._wemFile != null)
            {
                wem._wemFile.Dispose();
                wem = null;
            }
        }
        #endregion

        #region "BNK"
        public static void Wise_BNK(RichTextBox fileInfo_richTextBox, string filePath, PictureBox dds_pictureBox, int blobsetVersion)
        {
            // Retrieve blobset metadata
            var blobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];

            // Display blobset information
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Wise Audio BNK Location ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"FileIndex: {Global.filelist[Global.fileIndex].BlobsetIndex}" + Environment.NewLine);
            fileInfo_richTextBox.AppendText($"FileName: {filePath}" + Environment.NewLine);

            fileInfo_richTextBox.AppendText($"MainCompressedSize: {blobsetHeaderData.MainCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"MainUnCompressedSize: {blobsetHeaderData.MainUnCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramCompressedSize: {blobsetHeaderData.VramCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramUnCompressedSize: {blobsetHeaderData.VramUnCompressedSize}{Environment.NewLine}{Environment.NewLine}");

            // Display BNK information
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Wise Audio BNK Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"BNK File Size: {Utilities.FormatSize(blobsetHeaderData.MainUnCompressedSize)}");
        }
        #endregion

        #region "DAT"
        public static void DAT(RichTextBox fileInfo_richTextBox, string filePath, PictureBox dds_pictureBox, int blobsetVersion)
        {
            var blobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];

            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            //string platform = platformDetails["Platform"];
            string platformExt = platformDetails["PlatformExt"];

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Unknown File DAT Location ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText("FileIndex: " + Global.filelist[Global.fileIndex].BlobsetIndex + Environment.NewLine);

            if (blobsetVersion == (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"FileName: {filePath}{Environment.NewLine}");
            else
            {
                fileInfo_richTextBox.AppendText($"FolderHash: {Global.filelist[Global.fileIndex].FolderHash}{Environment.NewLine}");
                fileInfo_richTextBox.AppendText($"FileHash: {Global.filelist[Global.fileIndex].FileHash}{Environment.NewLine}");
                fileInfo_richTextBox.AppendText($"Blobset Number: data-{blobsetHeaderData.BlobSetNumber}.blobset.{platformExt}{Environment.NewLine}");
            }

            if (blobsetVersion != (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"MainFinalOffset: {blobsetHeaderData.MainFinalOffSet}{Environment.NewLine}");

            fileInfo_richTextBox.AppendText($"MainCompressedSize: {blobsetHeaderData.MainCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"MainUnCompressedSize: {blobsetHeaderData.MainUnCompressedSize}{Environment.NewLine}");

            if (blobsetVersion != (int)BlobsetVersion.v4)
                fileInfo_richTextBox.AppendText($"VramFinalOffset: {blobsetHeaderData.VramFinalOffSet}{Environment.NewLine}");

            fileInfo_richTextBox.AppendText($"VramCompressedSize: {blobsetHeaderData.VramCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramUnCompressedSize: {blobsetHeaderData.VramUnCompressedSize}{Environment.NewLine}{Environment.NewLine}");

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Unknown File DAT Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText("DAT File Size: " + Utilities.FormatSize(blobsetHeaderData.MainUnCompressedSize));
        }
        #endregion

        #region "BANK"
        public static void FMOD_BANK()
        {

        }
        #endregion

        #region "FSB"
        public static void FSB()
        {

        }
        #endregion

        #region "WAV"
        public static void WAV(RichTextBox fileInfo_richTextBox, string filePath, PictureBox dds_pictureBox, int blobsetVersion, SoundPlayer player, byte[] wavData)
        {
            // Retrieve blobset metadata
            var blobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];

            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            //string platform = platformDetails["Platform"];
            string platformExt = platformDetails["PlatformExt"];

            // Display blobset information
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** WAV Audio Location ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText(Global.filelist[Global.fileIndex].FilePath + Environment.NewLine + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Blobset Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"FileIndex: {Global.filelist[Global.fileIndex].BlobsetIndex}" + Environment.NewLine);

            fileInfo_richTextBox.AppendText($"FolderHash: {Global.filelist[Global.fileIndex].FolderHash}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"FileHash: {Global.filelist[Global.fileIndex].FileHash}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"Blobset Number: data-{blobsetHeaderData.BlobSetNumber}.blobset.{platformExt}{Environment.NewLine}");

            fileInfo_richTextBox.AppendText($"MainFinalOffSet: {blobsetHeaderData.MainFinalOffSet}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"MainCompressedSize: {blobsetHeaderData.MainCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"MainUnCompressedSize: {blobsetHeaderData.MainUnCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramFinalOffSet: {blobsetHeaderData.VramFinalOffSet}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramCompressedSize: {blobsetHeaderData.VramCompressedSize}{Environment.NewLine}");
            fileInfo_richTextBox.AppendText($"VramUnCompressedSize: {blobsetHeaderData.VramUnCompressedSize}{Environment.NewLine}{Environment.NewLine}");

            // Stop and dispose of the current player if it exists before playing a new file.
            StopAndDisposePlayer(ref player);

            using MemoryStream wav_ms = new(IO.ReadData(filePath, blobsetHeaderData.MainFinalOffSet, (int)blobsetHeaderData.MainUnCompressedSize));

            WavHeader wav_header = new(0, 0);
            wav_header.Deserialize(wav_ms);
            wav_ms.Position = 0;

            // Play the WAV audio
            player = new SoundPlayer(wav_ms);
            player.Play();

            // Display WEM information
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** WAV Audio Info ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText($"WAV Channel Count: {wav_header.numChannels}" + Environment.NewLine);
            fileInfo_richTextBox.AppendText($"WAV Sample Rate: {wav_header.sampleRate} Hz" + Environment.NewLine);
            fileInfo_richTextBox.AppendText($"WAV Average Bytes Per Second: {Utilities.FormatSize((ulong)wav_header.bytesPerSecond)}" + Environment.NewLine);
            fileInfo_richTextBox.AppendText($"WAV File Size: {Utilities.FormatSize(blobsetHeaderData.MainUnCompressedSize)}");
        }
        #endregion

        private static void StopAndDisposePlayer(ref SoundPlayer player)
        {
            if (player != null)
            {
                player.Stop();
                player.Stream.Dispose();
                player.Dispose();
            }
        }
    }
}

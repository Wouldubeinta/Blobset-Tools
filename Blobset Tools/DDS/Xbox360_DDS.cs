namespace Blobset_Tools
{
    /// <summary>
    /// Xbox360 DDS Class
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
    /// [Wouldubeinta]	   04/12/2025	Created
    /// </history>

    public class Xbox360_DDS
    {
        private DDS.HEADER header;
        private readonly byte[]? bdata;
        private readonly int ddsDataSize;

        public Xbox360_DDS(uint height, uint width, uint mipmap, uint type, int ddsSize, byte[]? ddsData = null)
        {
            header.size = 124;
            header.flags |= DDS.HEADER.Flags.DDSD_CAPS | DDS.HEADER.Flags.DDSD_HEIGHT | DDS.HEADER.Flags.DDSD_WIDTH | DDS.HEADER.Flags.DDSD_PIXELFORMAT;
            header.height = height;
            header.width = width;
            bdata = ddsData;
            ddsDataSize = ddsSize;

            switch (type)
            {
                // DXT1
                case 134:
                case 166:
                    header.flags |= DDS.HEADER.Flags.DDSD_LINEARSIZE;
                    header.pitchOrLinearSize = Math.Max(1, (width + 3) / 4) * Math.Max(1, (height + 3) / 4) * 8;
                    header.ddspf.flags |= DDS.PIXELFORMAT.Flags.DDPF_FOURCC;
                    header.ddspf.fourCC = 827611204; // DXT1 string
                    break;
                // DXT5
                case 136:
                case 168:
                    header.flags |= DDS.HEADER.Flags.DDSD_LINEARSIZE;
                    header.pitchOrLinearSize = Math.Max(1, (width + 3) / 4) * Math.Max(1, (height + 3) / 4) * 16;
                    header.ddspf.flags |= DDS.PIXELFORMAT.Flags.DDPF_FOURCC;
                    header.ddspf.fourCC = 894720068; // DXT5 string
                    break;
                // Uncompressed 8.8.8.8 ARGB 32bit
                case 133:
                case 165:
                    header.flags |= DDS.HEADER.Flags.DDSD_LINEARSIZE;
                    header.pitchOrLinearSize = (uint)ddsSize;
                    header.ddspf.flags |= DDS.PIXELFORMAT.Flags.DDPF_ALPHAPIXELS | DDS.PIXELFORMAT.Flags.DDPF_RGB;
                    header.ddspf.fourCC = 0;
                    header.ddspf.rGBBitCount = 32;
                    header.ddspf.rBitMask = 0xFF0000;
                    header.ddspf.gBitMask = 0xFF00;
                    header.ddspf.bBitMask = 0xFF;
                    header.ddspf.aBitMask = 0xFF000000;
                    break;
                // 16.16f GR 32bit floating point
                case 154:
                    header.pitchOrLinearSize = (uint)ddsSize;
                    header.ddspf.flags |= DDS.PIXELFORMAT.Flags.DDPF_RGB | DDS.PIXELFORMAT.Flags.DDPF_FLOAT;
                    header.ddspf.fourCC = 0;
                    header.ddspf.rGBBitCount = 32;
                    header.ddspf.rBitMask = 0xFFFF0000;
                    header.ddspf.gBitMask = 0x0000FFFF;
                    header.ddspf.bBitMask = 0x00000000;
                    header.ddspf.aBitMask = 0x00000000;
                    break;
            }

            if (mipmap > 1)
            {
                header.flags |= DDS.HEADER.Flags.DDSD_MIPMAPCOUNT;
                header.mipMapCount = mipmap;
                header.caps |= DDS.HEADER.Caps.DDSCAPS_MIPMAP | DDS.HEADER.Caps.DDSCAPS_COMPLEX;
            }

            header.reserved1 = new uint[11];
            header.ddspf.size = 32;
            header.caps |= DDS.HEADER.Caps.DDSCAPS_TEXTURE;
        }

        public byte[] WriteDDS()
        {
            return IO.WriteDDS(header, ddsDataSize, bdata);
        }

        private static byte[] SwapByteOrderX360(byte[] imageData)
        {
            if (imageData.Length % 2 != 0)
            {
                throw new Exception("Data size must be a multiple of 2 bytes!");
            }

            byte[] swappedData = new byte[imageData.Length];

            for (int i = 0; i < imageData.Length; i += 2)
            {
                swappedData[i] = imageData[i + 1];
                swappedData[i + 1] = imageData[i];
            }
            return swappedData;
        }

        private static int XgAddress2DTiledX(int blockOffset, int widthInBlocks, int texelBytePitch)
        {
            int alignedWidth = (widthInBlocks + 31) & ~31;
            int logBpp = (texelBytePitch >> 2) + ((texelBytePitch >> 1) >> (texelBytePitch >> 2));
            int offsetByte = blockOffset << logBpp;
            int offsetTile = (((offsetByte & ~0xFFF) >> 3) + ((offsetByte & 0x700) >> 2) + (offsetByte & 0x3F));
            int offsetMacro = offsetTile >> (7 + logBpp);

            int macroX = (offsetMacro % (alignedWidth >> 5)) << 2;
            int tile = (((offsetTile >> (5 + logBpp)) & 2) + (offsetByte >> 6)) & 3;
            int macro = (macroX + tile) << 3;
            int micro = ((((offsetTile >> 1) & ~0xF) + (offsetTile & 0xF)) & ((texelBytePitch << 3) - 1)) >> logBpp;

            return macro + micro;
        }

        private static int XgAddress2DTiledY(int blockOffset, int widthInBlocks, int texelBytePitch)
        {
            int alignedWidth = (widthInBlocks + 31) & ~31;
            int logBpp = (texelBytePitch >> 2) + ((texelBytePitch >> 1) >> (texelBytePitch >> 2));
            int offsetByte = blockOffset << logBpp;
            int offsetTile = (((offsetByte & ~0xFFF) >> 3) + ((offsetByte & 0x700) >> 2) + (offsetByte & 0x3F));
            int offsetMacro = offsetTile >> (7 + logBpp);

            int macroY = (offsetMacro / (alignedWidth >> 5)) << 2;
            int tile = ((offsetTile >> (6 + logBpp)) & 1) + ((offsetByte & 0x800) >> 10);
            int macro = (macroY + tile) << 3;
            int micro = (((offsetTile & ((texelBytePitch << 6) - 1 & ~0x1F)) + ((offsetTile & 0xF) << 1)) >> (3 + logBpp)) & ~1;

            return macro + micro + ((offsetTile & 0x10) >> 4);
        }

        private static byte[] ConvertX360ImageData(byte[] imageData, int imageWidth, int imageHeight, int blockPixelSize, int texelBytePitch, bool swizzleFlag)
        {
            int widthInBlocks = imageWidth / blockPixelSize;
            int heightInBlocks = imageHeight / blockPixelSize;

            int paddedWidthInBlocks = (widthInBlocks + 31) & ~31;
            int paddedHeightInBlocks = (heightInBlocks + 31) & ~31;
            int totalPaddedBlocks = paddedWidthInBlocks * paddedHeightInBlocks;

            byte[] convertedData;

            if (!swizzleFlag)
            {
                convertedData = new byte[widthInBlocks * heightInBlocks * texelBytePitch];
            }
            else
            {
                convertedData = new byte[totalPaddedBlocks * texelBytePitch];
            }

            for (int blockOffset = 0; blockOffset < totalPaddedBlocks; blockOffset++)
            {
                int x = XgAddress2DTiledX(blockOffset, paddedWidthInBlocks, texelBytePitch);
                int y = XgAddress2DTiledY(blockOffset, paddedWidthInBlocks, texelBytePitch);

                if (x < widthInBlocks && y < heightInBlocks)
                {
                    if (!swizzleFlag)
                    {
                        int srcByteOffset = blockOffset * texelBytePitch;
                        int destByteOffset = (y * widthInBlocks + x) * texelBytePitch;
                        if (srcByteOffset + texelBytePitch <= imageData.Length)
                        {
                            Array.Copy(imageData, srcByteOffset, convertedData, destByteOffset, texelBytePitch);
                        }
                    }
                    else
                    {
                        int srcByteOffset = (y * widthInBlocks + x) * texelBytePitch;
                        int destByteOffset = blockOffset * texelBytePitch;
                        if (srcByteOffset + texelBytePitch <= imageData.Length)
                        {
                            Array.Copy(imageData, srcByteOffset, convertedData, destByteOffset, texelBytePitch);
                        }
                    }
                }
            }

            return convertedData;
        }

        public static byte[] UnswizzleX360(byte[] imageData, int imgWidth, int imgHeight, int blockPixelSize = 4, int texelBytePitch = 8)
        {
            byte[] swappedData = SwapByteOrderX360(imageData);
            byte[] unswizzledData = ConvertX360ImageData(swappedData, imgWidth, imgHeight, blockPixelSize, texelBytePitch, false);
            return unswizzledData;
        }

        public static byte[] SwizzleX360(byte[] imageData, int imgWidth, int imgHeight, int blockPixelSize = 4, int texelBytePitch = 8)
        {
            byte[] swappedData = SwapByteOrderX360(imageData);
            byte[] swizzledData = ConvertX360ImageData(swappedData, imgWidth, imgHeight, blockPixelSize, texelBytePitch, true);
            return swizzledData;
        }
    }
}

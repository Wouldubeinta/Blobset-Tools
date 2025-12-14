namespace Blobset_Tools
{
    /// <summary>
    /// PS3 DDS Class
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
    /// [Wouldubeinta]	   30/11/2025	Created
    /// </history>

    public class PS3_DDS
    {
        private DDS.HEADER header;
        private readonly byte[]? bdata;
        private readonly int ddsDataSize;

        public PS3_DDS(uint height, uint width, uint mipmap, uint type, int ddsSize, byte[]? ddsData = null)
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
                    if (type == 133)
                    {
                        if (ddsData != null)
                            bdata = UnswizzleMorton(ddsData, (int)width, (int)height, 32, 1, 1);
                    }

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
                    if (ddsData != null)
                        bdata = UnswizzleMorton(ddsData, (int)width, (int)height, 32, 1, 1);
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

        private static int CalculateMortonIndex(int t, int width, int height)
        {
            int num1 = 1, num2 = 1;
            int num3 = t;
            int tWidth = width;
            int tHeight = height;
            int num6 = 0, num7 = 0;

            while (tWidth > 1 || tHeight > 1)
            {
                if (tWidth > 1)
                {
                    num6 += num2 * (num3 & 1);
                    num3 >>= 1;
                    num2 *= 2;
                    tWidth >>= 1;
                }
                if (tHeight > 1)
                {
                    num7 += num1 * (num3 & 1);
                    num3 >>= 1;
                    num1 *= 2;
                    tHeight >>= 1;
                }
            }

            return num7 * width + num6;
        }

        private static int ConvertBppToBytesPerPixel(int bpp)
        {
            if (bpp <= 0)
                throw new ArgumentException("Invalid bpp value!");
            else if (bpp <= 8)
                return 1;
            else if (bpp <= 16)
                return 2;
            else if (bpp <= 24)
                return 3;
            else if (bpp <= 32)
                return 4;
            else
                throw new NotSupportedException("Not supported bpp value!");
        }

        private static byte[] ConvertMorton(byte[] pixelData, int imgWidth, int imgHeight, int bpp, int blockWidth, int blockHeight, bool swizzleFlag)
        {
            int blockDataSize;

            if (bpp == 1)
                blockDataSize = (blockWidth * blockHeight) / 8;
            else if (bpp == 2)
                blockDataSize = (blockWidth * blockHeight) / 4;
            else if (bpp == 4)
            {
                blockDataSize = (blockWidth * blockHeight) / 2;
            }
            else
            {
                int bytesPerPixel = ConvertBppToBytesPerPixel(bpp);
                blockDataSize = bytesPerPixel * blockWidth * blockHeight;
            }

            byte[] convertedData = new byte[pixelData.Length];
            imgWidth /= blockWidth;
            imgHeight /= blockHeight;
            int sourceIndex = 0;

            for (int t = 0; t < imgWidth * imgHeight; t++)
            {
                int index = CalculateMortonIndex(t, imgWidth, imgHeight);
                int destinationIndex = blockDataSize * index;

                if (!swizzleFlag)
                    Array.Copy(pixelData, sourceIndex, convertedData, destinationIndex, blockDataSize);
                else
                    Array.Copy(pixelData, destinationIndex, convertedData, sourceIndex, blockDataSize);

                sourceIndex += blockDataSize;
            }
            return convertedData;
        }

        public static byte[] UnswizzleMorton(byte[] pixelData, int imgWidth, int imgHeight, int bpp, int blockWidth = 1, int blockHeight = 1)
        {
            return ConvertMorton(pixelData, imgWidth, imgHeight, bpp, blockWidth, blockHeight, false);
        }

        public static byte[] SwizzleMorton(byte[] pixelData, int imgWidth, int imgHeight, int bpp, int blockWidth = 1, int blockHeight = 1)
        {
            return ConvertMorton(pixelData, imgWidth, imgHeight, bpp, blockWidth, blockHeight, true);
        }
    }
}

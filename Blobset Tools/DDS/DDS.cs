namespace Blobset_Tools.DDS
{
    /// <summary>
    /// DDS Header Class
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

    #region DDS Header
    public struct HEADER
    {
        public uint size;
        public Flags flags;
        public uint height;
        public uint width;
        public uint pitchOrLinearSize;
        public uint depth;
        public uint mipMapCount;
        public uint[] reserved1; //  = new uint[11]
        public PIXELFORMAT ddspf;
        public Caps caps;
        public Caps2 caps2;
        public uint caps3;
        public uint caps4;
        public uint reserved2;

        public enum Flags
        {
            DDSD_CAPS = 0x1,
            DDSD_HEIGHT = 0x2,
            DDSD_WIDTH = 0x4,
            DDSD_PITCH = 0x8,
            DDSD_PIXELFORMAT = 0x1000,
            DDSD_MIPMAPCOUNT = 0x20000,
            DDSD_LINEARSIZE = 0x80000,
            DDSD_DEPTH = 0x800000
        }

        public enum Caps
        {
            DDSCAPS_COMPLEX = 0x8,
            DDSCAPS_MIPMAP = 0x400000,
            DDSCAPS_TEXTURE = 0x1000
        }

        public enum Caps2
        {
            DDSCAPS2_CUBEMAP = 0x200,
            DDSCAPS2_CUBEMAP_POSITIVEX = 0x400,
            DDSCAPS2_CUBEMAP_NEGATIVEX = 0x800,
            DDSCAPS2_CUBEMAP_POSITIVEY = 0x1000,
            DDSCAPS2_CUBEMAP_NEGATIVEY = 0x2000,
            DDSCAPS2_CUBEMAP_POSITIVEZ = 0x4000,
            DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x8000,
            DDSCAPS2_VOLUME = 0x200000
        }
    }
    #endregion

    public struct PIXELFORMAT
    {
        public uint size;
        public Flags flags;
        public uint fourCC;
        public uint rGBBitCount;
        public uint rBitMask;
        public uint gBitMask;
        public uint bBitMask;
        public uint aBitMask;

        public enum Flags
        {
            DDPF_ALPHAPIXELS = 0x1,
            DDPF_ALPHA = 0x2,
            DDPF_FOURCC = 0x4,
            DDPF_RGB = 0x40,
            DDPF_FLOAT = 0x200,
            DDPF_LUMINANCE = 0x20000
        }
    }
}

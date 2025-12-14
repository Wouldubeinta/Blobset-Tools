namespace Blobset_Tools
{
    /// <summary>
    /// Holds all the enums.
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
    /// [Wouldubeinta]	   25/06/2025	Created
    /// </history>
    public class Enums
    {
        public enum FileType
        {
            BMf = 543575362, // Used in AFLL and RLL2.
            BSB_BE = 541217602,
            BSB = 1112752672, // Used in AFLL and RLL2.
            BANK = 1179011410,
            FEV1_BE = 1178949169,
            FEV1 = 827737414, // Used in AFLL and RLL2.
            FSB = 1179861557,
            PM3M = 1295207760,
            M3MP = 1347236685,
            MINI = 1229867341,
            MOB = 557993805, //Used in RLL4 and some cricket games.
            PNG = -1991225785,
            KPXT = 1263556692,
            TXPK = 1415073867,
            WiseBNK = 1145588546,
            WiseWEM = 1179011410,
            WAV = 1380533830 // RLL2 - bigendian
        }

        public enum Game
        {
            AFLL, // AFL Live (PC)(PS3)(360)
            RLL2, // Rugby League Live 2 (PS3)(360)
            RLL2WCE, // Rugby League Live 2 - World Cup Edition (PS3)(360)
            DBC14, // Don Bradman Cricket 14 (PC)(PS3)(360)
            RLL3, // Rugby League Live 3 (PC)(PS3)(360)
            TTC, // TableTop Cricket (PC)(PS3)(360)
            CPL16, // Casey Powell Lacrosse 16 (PC)
            DBC17, // Don Bradman Cricket 17 (PC)
            MTBOD, // Masquerade - The Baubles of Doom (PC)(PS3)(360)
            AC, // Ashes Cricket (PC)
            RLL4, // Rugby League Live 4 (PC)
            AOIT, // AO International Tennis (PC)
            CPL18, // Casey Powell Lacrosse 18 (PC)
            C19, // Cricket 19 (PC)
            AOT2, // AO Tennis 2 (PC)
            TWT2, // Tennis World Tour 2 (PC)
            C22, // Cricket 22 (PC)
            AFL23, // AFL 23 (PC)
            C24, // Cricket 24 (PC)
            TB, // Tiebreak (PC)
            R25, // Rugby 25 (PC)
            AFL26, // AFL 26 (PC)
            RL26, // Rugby League 26 (PC)
            C26 // Cricket 26 (PC)
        }

        public enum BlobsetVersion
        {
            v1, v2, v3, v4
        }

        public enum Platforms
        {
            Windows,
            PS3,
            Xbox360
        }
    }
}

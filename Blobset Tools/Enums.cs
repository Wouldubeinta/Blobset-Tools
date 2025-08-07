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
    /// [Wouldubeinta]	   25/06/2025	Created
    /// </history>
    public class Enums
    {
        public enum FileType
        {
            BMf = 543575362, // Used in AFLL.
            BSB = 1112752672, // Used in AFLL.
            FEV1 = 827737414, // Used in AFLL.
            FSB = 876761926,
            M3MP = 1347236685,
            MINI = 1229867341,
            MOB = 557993805, //Used in RLL4 and some cricket games.
            TXPK = 1415073867,
            WiseBNK = 1145588546,
            WiseWEM = 1179011410,
        }

        public enum Game
        {
            AFLL, // AFL Live
            RLL2, // Rugby League Live 2 PS3
            DBC14, // Don Bradman Cricket 14
            RLL3, // Rugby League Live 3
            DBC17, // Don Bradman Cricket 17
            AC, // Ashes Cricket
            RLL4, // Rugby League Live 4
            AOIT, // AO International Tennis
            C19, // Cricket 19
            AOT2, // AO Tennis 2
            C22, // Cricket 22
            AFL23, // AFL 23
            C24, // Cricket 24
            TB, // Tiebreak
            R25, // Rugby 25
            AFL26, // AFL 26
            RL26 // Rugby League 26
        }

        public enum BlobsetVersion
        {
            v1, v2, v3, v4
        }
    }
}

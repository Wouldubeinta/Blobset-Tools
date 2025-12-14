/* OggSharp
* Copyright (C) 2000 ymnk, JCraft,Inc.
*
* Written by: 2000 ymnk<ymnk@jcraft.com>
* Ported to C# from JOrbis by: Mark Crichton <crichton@gimp.org>
*
* Thanks go to the JOrbis team, for licencing the code under the
* LGPL, making my job a lot easier.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public License
* as published by the Free Software Foundation; either version 3 of
* the License, or (at your option) any later version.

* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

namespace OggSharp
{
    // psychoacoustic setup
    internal class PsyInfo
    {
        //int    athp;
        //int    decayp;
        //int    smoothp;
        //int    noisefitp;
        //int    noisefit_subblock;
        //float noisefit_threshdB;

        //float ath_att;

        //int tonemaskp;
        private readonly float[] toneatt_125Hz = new float[5];
        private readonly float[] toneatt_250Hz = new float[5];
        private readonly float[] toneatt_500Hz = new float[5];
        private readonly float[] toneatt_1000Hz = new float[5];
        private readonly float[] toneatt_2000Hz = new float[5];
        private readonly float[] toneatt_4000Hz = new float[5];
        private readonly float[] toneatt_8000Hz = new float[5];

        //int peakattp;
        private readonly float[] peakatt_125Hz = new float[5];
        private readonly float[] peakatt_250Hz = new float[5];
        private readonly float[] peakatt_500Hz = new float[5];
        private readonly float[] peakatt_1000Hz = new float[5];
        private readonly float[] peakatt_2000Hz = new float[5];
        private readonly float[] peakatt_4000Hz = new float[5];
        private readonly float[] peakatt_8000Hz = new float[5];

        //int noisemaskp;
        private readonly float[] noiseatt_125Hz = new float[5];
        private readonly float[] noiseatt_250Hz = new float[5];
        private readonly float[] noiseatt_500Hz = new float[5];
        private readonly float[] noiseatt_1000Hz = new float[5];
        private readonly float[] noiseatt_2000Hz = new float[5];
        private readonly float[] noiseatt_4000Hz = new float[5];
        private readonly float[] noiseatt_8000Hz = new float[5];

        //float max_curve_dB;

        //float attack_coeff;
        //float decay_coeff;

        internal void free() { }
    }
}
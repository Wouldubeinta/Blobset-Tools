/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2011 Xiph.Org Foundation
   Originally written by Jean-Marc Valin, Gregory Maxwell, Koen Vos,
   Timothy B. Terriberry, and the Opus open-source contributors
   Ported to C# by Logan Stromberg

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   - Neither the name of Internet Society, IETF or IETF Trust, nor the
   names of specific contributors, may be used to endorse or promote
   products derived from this software without specific prior written
   permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

using Concentus.Celt;
using Concentus.Celt.Structs;
using Concentus.Common;
using Concentus.Common.CPlusPlus;
using Concentus.Enums;
using Concentus.Silk;
using Concentus.Silk.Structs;

namespace Concentus.Structs
{
    /// <summary>
    /// The Opus encoder structure
    /// </summary>
    public class OpusEncoder : IOpusEncoder
    {
        #region Encoder state

        internal readonly EncControlState silk_mode = new();
        internal OpusApplication application;
        internal int channels;
        internal int delay_compensation;
        internal int force_channels;
        internal OpusSignal signal_type;
        internal OpusBandwidth user_bandwidth;
        internal OpusBandwidth max_bandwidth;
        internal OpusMode user_forced_mode;
        internal int voice_ratio;
        internal int Fs;
        internal int use_vbr;
        internal int vbr_constraint;
        internal OpusFramesize variable_duration;
        internal int bitrate_bps;
        internal int user_bitrate_bps;
        internal int lsb_depth;
        internal int encoder_buffer;
        internal int lfe;
        internal readonly TonalityAnalysisState analysis = new();

        // partial reset happens below this line
        internal int stream_channels;
        internal short hybrid_stereo_width_Q14;
        internal int variable_HP_smth2_Q15;
        internal int prev_HB_gain;
        internal readonly int[] hp_mem = new int[4];
        internal OpusMode mode;
        internal OpusMode prev_mode;
        internal int prev_channels;
        internal int prev_framesize;
        internal OpusBandwidth bandwidth;
        internal int silk_bw_switch;
        /* Sampling rate (at the API level) */
        internal int first;
        internal int[] energy_masking;
        internal readonly StereoWidthState width_mem = new();
        internal readonly short[] delay_buffer = new short[OpusConstants.MAX_ENCODER_BUFFER * 2];
        internal OpusBandwidth detected_bandwidth;
        internal uint rangeFinal;

        // Constant quality encoding parameter (new feature not in the main libopus)
        private int? _vqLevel;

        // [Porting Note] There were originally "cabooses" that were tacked onto the end
        // of the struct without being explicitly included (since they have a variable size).
        // Here they are just included as an intrinsic variable.
        internal readonly SilkEncoder SilkEncoder = new();
        internal readonly CeltEncoder Celt_Encoder = new();

        // used by multichannel structs
        internal OpusEncoder() { }

        internal void Reset()
        {
            silk_mode.Reset();
            application = 0;
            channels = 0;
            delay_compensation = 0;
            force_channels = 0;
            signal_type = 0;
            user_bandwidth = 0;
            max_bandwidth = 0;
            user_forced_mode = 0;
            voice_ratio = 0;
            Fs = 0;
            use_vbr = 0;
            vbr_constraint = 0;
            variable_duration = 0;
            bitrate_bps = 0;
            user_bitrate_bps = 0;
            lsb_depth = 0;
            encoder_buffer = 0;
            lfe = 0;
            analysis.Reset();
            PartialReset();
        }

        /// <summary>
        /// OPUS_ENCODER_RESET_START
        /// </summary>
        internal void PartialReset()
        {
            stream_channels = 0;
            hybrid_stereo_width_Q14 = 0;
            variable_HP_smth2_Q15 = 0;
            prev_HB_gain = 0;
            Arrays.MemSetInt(hp_mem, 0, 4);
            mode = 0;
            prev_mode = 0;
            prev_channels = 0;
            prev_framesize = 0;
            bandwidth = 0;
            silk_bw_switch = 0;
            first = 0;
            energy_masking = null;
            width_mem.Reset();
            Arrays.MemSetShort(delay_buffer, 0, OpusConstants.MAX_ENCODER_BUFFER * 2);
            detected_bandwidth = 0;
            rangeFinal = 0;
            //SilkEncoder.Reset();
            //CeltEncoder.Reset();
        }

        /// <inheritdoc />
        public void ResetState()
        {
            EncControlState dummy = new();
            analysis.Reset();
            PartialReset();

            Celt_Encoder.ResetState();
            EncodeAPI.silk_InitEncoder(SilkEncoder, dummy);
            stream_channels = channels;
            hybrid_stereo_width_Q14 = 1 << 14;
            prev_HB_gain = CeltConstants.Q15ONE;
            first = 1;
            mode = OpusMode.MODE_HYBRID;
            bandwidth = OpusBandwidth.OPUS_BANDWIDTH_FULLBAND;
            variable_HP_smth2_Q15 = Inlines.silk_LSHIFT(Inlines.silk_lin2log(TuningParameters.VARIABLE_HP_MIN_CUTOFF_HZ), 8);
        }

        #endregion

        #region Encoder API functions

        /// <summary>
        /// Allocates and initializes an encoder state.
        /// Note that regardless of the sampling rate and number channels selected, the Opus encoder
        /// can switch to a lower audio bandwidth or number of channels if the bitrate
        /// selected is too low. This also means that it is safe to always use 48 kHz stereo input
        /// and let the encoder optimize the encoding. The decoder will not be constrained later on
        /// by the mode that you select here for the encoder.
        /// </summary>
        /// <param name="Fs">Sampling rate of input signal (Hz). This must be one of 8000, 12000, 16000, 24000, or 48000.</param>
        /// <param name="channels">Number of channels (1 or 2) in input signal</param>
        /// <param name="application">There are three coding modes:
        /// <para>
        /// OPUS_APPLICATION_VOIP gives best quality at a given bitrate for voice
        /// signals. It enhances the input signal by high-pass filtering and
        /// emphasizing formants and harmonics. Optionally it includes in-band
        /// forward error correction to protect against packet loss. Use this
        /// mode for typical VoIP applications.Because of the enhancement,
        /// even at high bitrates the output may sound different from the input.
        /// </para>
        /// <para>
        /// OPUS_APPLICATION_AUDIO gives best quality at a given bitrate for most
        /// non-voice signals like music. Use this mode for music and mixed
        /// (music/voice) content, broadcast, and applications requiring less
        /// than 15 ms of coding delay.
        /// </para>
        /// <para>
        /// OPUS_APPLICATION_RESTRICTED_LOWDELAY configures low-delay mode that
        /// disables the speech-optimized mode in exchange for slightly reduced delay.
        /// This mode can only be set on an newly initialized or freshly reset encoder
        /// because it changes the codec delay.
        /// </para></param>
        /// <returns>The created encoder</returns>
        [Obsolete("Use OpusCodecFactory methods which can give you native code if supported by your platform")]
        public OpusEncoder(int Fs, int channels, OpusApplication application)
        {
            int ret;
            if ((Fs != 48000 && Fs != 24000 && Fs != 16000 && Fs != 12000 && Fs != 8000))
            {
                throw new ArgumentException("Sample rate is invalid (must be 8/12/16/24/48 Khz)");
            }
            if (channels != 1 && channels != 2)
            {
                throw new ArgumentException("Number of channels must be 1 or 2");
            }

            ret = opus_init_encoder(Fs, channels, application);
            if (ret != OpusError.OPUS_OK)
            {
                if (ret == OpusError.OPUS_BAD_ARG)
                    throw new ArgumentException("OPUS_BAD_ARG when creating encoder");
                throw new OpusException("Error while initializing encoder: " + CodecHelpers.opus_strerror(ret), ret);
            }
        }

        internal int opus_init_encoder(int Fs, int channels, OpusApplication application)
        {
            SilkEncoder silk_enc;
            CeltEncoder celt_enc;
            int err;
            int ret;

            if ((Fs != 48000 && Fs != 24000 && Fs != 16000 && Fs != 12000 && Fs != 8000) || (channels != 1 && channels != 2) ||
                application == OpusApplication.OPUS_APPLICATION_UNIMPLEMENTED)
                return OpusError.OPUS_BAD_ARG;

            Reset();
            /* Create SILK encoder */
            silk_enc = SilkEncoder;
            celt_enc = Celt_Encoder;

            stream_channels = this.channels = channels;

            this.Fs = Fs;

            ret = EncodeAPI.silk_InitEncoder(silk_enc, silk_mode);
            if (ret != 0) return OpusError.OPUS_INTERNAL_ERROR;

            /* default SILK parameters */
            silk_mode.nChannelsAPI = channels;
            silk_mode.nChannelsInternal = channels;
            silk_mode.API_sampleRate = this.Fs;
            silk_mode.maxInternalSampleRate = 16000;
            silk_mode.minInternalSampleRate = 8000;
            silk_mode.desiredInternalSampleRate = 16000;
            silk_mode.payloadSize_ms = 20;
            silk_mode.bitRate = 25000;
            silk_mode.packetLossPercentage = 0;
            silk_mode.complexity = 9;
            silk_mode.useInBandFEC = 0;
            silk_mode.useDTX = 0;
            silk_mode.useCBR = 0;
            silk_mode.reducedDependency = 0;

            /* Create CELT encoder */
            /* Initialize CELT encoder */
            err = celt_enc.celt_encoder_init(Fs, channels);
            if (err != OpusError.OPUS_OK) return OpusError.OPUS_INTERNAL_ERROR;

            celt_enc.SetSignalling(0);
            celt_enc.SetComplexity(silk_mode.complexity);

            use_vbr = 1;
            /* Makes constrained VBR the default (safer for real-time use) */
            vbr_constraint = 1;
            user_bitrate_bps = OpusConstants.OPUS_AUTO;
            bitrate_bps = 3000 + Fs * channels;
            this.application = application;
            signal_type = OpusSignal.OPUS_SIGNAL_AUTO;
            user_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_AUTO;
            max_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_FULLBAND;
            force_channels = OpusConstants.OPUS_AUTO;
            user_forced_mode = OpusMode.MODE_AUTO;
            voice_ratio = -1;
            encoder_buffer = this.Fs / 100;
            lsb_depth = 24;
            variable_duration = OpusFramesize.OPUS_FRAMESIZE_ARG;

            /* Delay compensation of 4 ms (2.5 ms for SILK's extra look-ahead
               + 1.5 ms for SILK resamplers and stereo prediction) */
            delay_compensation = this.Fs / 250;

            hybrid_stereo_width_Q14 = 1 << 14;
            prev_HB_gain = CeltConstants.Q15ONE;
            variable_HP_smth2_Q15 = Inlines.silk_LSHIFT(Inlines.silk_lin2log(TuningParameters.VARIABLE_HP_MIN_CUTOFF_HZ), 8);
            first = 1;
            mode = OpusMode.MODE_HYBRID;
            bandwidth = OpusBandwidth.OPUS_BANDWIDTH_FULLBAND;

            Analysis.tonality_analysis_init(analysis);

            return OpusError.OPUS_OK;
        }


        internal int user_bitrate_to_bitrate(int user_bitrate, int frame_size, int max_data_bytes)
        {
            if (frame_size == 0)
            {
                frame_size = Fs / 400;
            }
            if (user_bitrate == OpusConstants.OPUS_AUTO)
                return 60 * Fs / frame_size + Fs * channels;
            else if (user_bitrate == OpusConstants.OPUS_BITRATE_MAX)
                return max_data_bytes * 8 * Fs / frame_size;
            else
                return user_bitrate;
        }

        /// <summary>
        /// 
        /// </summary>
        /// <typeparam name="T">The storage type of analysis_pcm, either short or float</typeparam>
        /// <param name="pcm"></param>
        /// <param name="frame_size"></param>
        /// <param name="data"></param>
        /// <param name="out_data_bytes"></param>
        /// <param name="lsb_depth"></param>
        /// <param name="analysis_pcm"></param>
        /// <param name="analysis_size"></param>
        /// <param name="c1"></param>
        /// <param name="c2"></param>
        /// <param name="analysis_channels"></param>
        /// <param name="downmix"></param>
        /// <param name="float_api"></param>
        /// <returns></returns>
        internal int opus_encode_native<T>(ReadOnlySpan<short> pcm, int pcm_ptr, int frame_size,
                        Span<byte> data, int data_ptr, int out_data_bytes, int lsb_depth,
                        ReadOnlySpan<T> analysis_pcm, int analysis_size, int c1, int c2,
                        int analysis_channels, Downmix.downmix_func<T> downmix, int float_api)
        {
            SilkEncoder silk_enc;
            CeltEncoder celt_enc;
            int i;
            int ret = 0;
            int nBytes;
            EntropyCoder enc = new(); // porting note: stack var
            int bytes_target;
            int prefill = 0;
            int start_band = 0;
            int redundancy = 0;
            int redundancy_bytes = 0; /* Number of bytes to use for redundancy frame */
            int celt_to_silk = 0;
            short[] pcm_buf;
            int nb_compr_bytes;
            int to_celt = 0;
            uint redundant_rng = 0;
            int cutoff_Hz, hp_freq_smth1;
            int voice_est; /* Probability of voice in Q7 */
            int equiv_rate;
            int delay_compensation;
            int frame_rate;
            int max_rate; /* Max bitrate we're allowed to use */
            OpusBandwidth curr_bandwidth;
            int HB_gain;
            int max_data_bytes; /* Max number of bytes we're allowed to use */
            int total_buffer;
            int stereo_width;
            CeltMode celt_mode;
            AnalysisInfo analysis_info = new(); // porting note: stack var
            int analysis_read_pos_bak = -1;
            int analysis_read_subframe_bak = -1;
            short[] tmp_prefill;

            max_data_bytes = Inlines.IMIN(1276, out_data_bytes);

            rangeFinal = 0;
            if ((variable_duration == 0 && 400 * frame_size != Fs && 200 * frame_size != Fs && 100 * frame_size != Fs &&
                 50 * frame_size != Fs && 25 * frame_size != Fs && 50 * frame_size != 3 * Fs)
                 || (400 * frame_size < Fs)
                 || max_data_bytes <= 0
                 )
            {
                return OpusError.OPUS_BAD_ARG;
            }

            silk_enc = SilkEncoder;
            celt_enc = Celt_Encoder;
            if (application == OpusApplication.OPUS_APPLICATION_RESTRICTED_LOWDELAY)
                delay_compensation = 0;
            else
                delay_compensation = this.delay_compensation;

            lsb_depth = Inlines.IMIN(lsb_depth, this.lsb_depth);
            celt_mode = celt_enc.GetMode();
            voice_ratio = -1;

            if (analysis.enabled)
            {
                analysis_info.valid = 0;
                if ((_vqLevel.HasValue || silk_mode.complexity >= 7) && Fs == 48000)
                {
                    analysis_read_pos_bak = analysis.read_pos;
                    analysis_read_subframe_bak = analysis.read_subframe;
                    Analysis.run_analysis(analysis,
                        celt_mode,
                        analysis_pcm,
                        analysis_size,
                        frame_size,
                        c1,
                        c2,
                        analysis_channels,
                        Fs,
                        lsb_depth,
                        downmix,
                        analysis_info);
                }

                detected_bandwidth = 0;
                if (analysis_info.valid != 0)
                {
                    int analysis_bandwidth;
                    if (signal_type == OpusSignal.OPUS_SIGNAL_AUTO)
                        voice_ratio = (int)Math.Floor(.5f + 100 * (1 - analysis_info.music_prob));

                    analysis_bandwidth = analysis_info.bandwidth;
                    if (analysis_bandwidth <= 12)
                        detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND;
                    else if (analysis_bandwidth <= 14)
                        detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND;
                    else if (analysis_bandwidth <= 16)
                        detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND;
                    else if (analysis_bandwidth <= 18)
                        detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND;
                    else
                        detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_FULLBAND;

                    // If we are in constant-quality encode mode, use the analysis results to set the variable bitrate
                    if (_vqLevel.HasValue)
                    {
                        user_bitrate_bps = GetVariableQualityBitrate(_vqLevel.Value, analysis.music_prob);
                    }
                }
            }

            if (channels == 2 && force_channels != 1)
                stereo_width = CodecHelpers.compute_stereo_width(pcm, pcm_ptr, frame_size, Fs, width_mem);
            else
                stereo_width = 0;
            total_buffer = delay_compensation;
            bitrate_bps = user_bitrate_to_bitrate(user_bitrate_bps, frame_size, max_data_bytes);

            frame_rate = Fs / frame_size;
            if (use_vbr == 0)
            {
                int cbrBytes;
                /* Multiply by 3 to make sure the division is exact. */
                int frame_rate3 = 3 * Fs / frame_size;
                /* We need to make sure that "int" values always fit in 16 bits. */
                cbrBytes = Inlines.IMIN((3 * bitrate_bps / 8 + frame_rate3 / 2) / frame_rate3, max_data_bytes);
                bitrate_bps = cbrBytes * frame_rate3 * 8 / 3;
                max_data_bytes = cbrBytes;
            }
            if (max_data_bytes < 3 || bitrate_bps < 3 * frame_rate * 8
               || (frame_rate < 50 && (max_data_bytes * frame_rate < 300 || bitrate_bps < 2400)))
            {
                /*If the space is too low to do something useful, emit 'PLC' frames.*/
                OpusMode tocmode = mode;
                OpusBandwidth bw = bandwidth == 0 ? OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND : bandwidth;
                if (tocmode == 0)
                    tocmode = OpusMode.MODE_SILK_ONLY;
                if (frame_rate > 100)
                    tocmode = OpusMode.MODE_CELT_ONLY;
                if (frame_rate < 50)
                    tocmode = OpusMode.MODE_SILK_ONLY;
                if (tocmode == OpusMode.MODE_SILK_ONLY && bw > OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND)
                    bw = OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND;
                else if (tocmode == OpusMode.MODE_CELT_ONLY && bw == OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND)
                    bw = OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND;
                else if (tocmode == OpusMode.MODE_HYBRID && bw <= OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND)
                    bw = OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND;
                data[data_ptr] = CodecHelpers.gen_toc(tocmode, frame_rate, bw, stream_channels);
                ret = 1;
                if (use_vbr == 0)
                {
                    ret = OpusRepacketizer.PadPacket(data, data_ptr, ret, max_data_bytes);
                    if (ret == OpusError.OPUS_OK)
                        ret = max_data_bytes;
                }
                return ret;
            }
            max_rate = frame_rate * max_data_bytes * 8;

            /* Equivalent 20-ms rate for mode/channel/bandwidth decisions */
            equiv_rate = bitrate_bps - (40 * channels + 20) * (Fs / frame_size - 50);

            if (signal_type == OpusSignal.OPUS_SIGNAL_VOICE)
                voice_est = 127;
            else if (signal_type == OpusSignal.OPUS_SIGNAL_MUSIC)
                voice_est = 0;
            else if (voice_ratio >= 0)
            {
                voice_est = voice_ratio * 327 >> 8;
                /* For AUDIO, never be more than 90% confident of having speech */
                if (application == OpusApplication.OPUS_APPLICATION_AUDIO)
                    voice_est = Inlines.IMIN(voice_est, 115);
            }
            else if (application == OpusApplication.OPUS_APPLICATION_VOIP)
                voice_est = 115;
            else
                voice_est = 48;

            if (force_channels != OpusConstants.OPUS_AUTO && channels == 2)
            {
                stream_channels = force_channels;
            }
            else
            {
#if FUZZING
        /* Random mono/stereo decision */
        if (st.channels == 2 && (new Random().Next() & 0x1F) == 0)
            st.stream_channels = 3 - st.stream_channels;
#else
                /* Rate-dependent mono-stereo decision */
                if (channels == 2)
                {
                    int stereo_threshold;
                    stereo_threshold = Tables.stereo_music_threshold + ((voice_est * voice_est * (Tables.stereo_voice_threshold - Tables.stereo_music_threshold)) >> 14);
                    if (stream_channels == 2)
                        stereo_threshold -= 1000;
                    else
                        stereo_threshold += 1000;
                    stream_channels = (equiv_rate > stereo_threshold) ? 2 : 1;
                }
                else
                {
                    stream_channels = channels;
                }
#endif
            }
            equiv_rate = bitrate_bps - (40 * stream_channels + 20) * (Fs / frame_size - 50);

            /* Mode selection depending on application and signal type */
            if (application == OpusApplication.OPUS_APPLICATION_RESTRICTED_LOWDELAY)
            {
                mode = OpusMode.MODE_CELT_ONLY;
            }
            else if (user_forced_mode == OpusMode.MODE_AUTO)
            {
#if FUZZING
        /* Random mode switching */
        if ((new Random().Next() & 0xF) == 0)
        {
            if ((new Random().Next() & 0x1) == 0)
                st.mode = OpusMode.MODE_CELT_ONLY;
            else
                st.mode = OpusMode.MODE_SILK_ONLY;
        }
        else {
            if (st.prev_mode == OpusMode.MODE_CELT_ONLY)
                st.mode = OpusMode.MODE_CELT_ONLY;
            else
                st.mode = OpusMode.MODE_SILK_ONLY;
        }
#else
                int mode_voice, mode_music;
                int threshold;

                /* Interpolate based on stereo width */
                mode_voice = Inlines.MULT16_32_Q15(CeltConstants.Q15ONE - stereo_width, Tables.mode_thresholds[0][0])
                      + Inlines.MULT16_32_Q15(stereo_width, Tables.mode_thresholds[1][0]);
                mode_music = Inlines.MULT16_32_Q15(CeltConstants.Q15ONE - stereo_width, Tables.mode_thresholds[1][1])
                      + Inlines.MULT16_32_Q15(stereo_width, Tables.mode_thresholds[1][1]);
                /* Interpolate based on speech/music probability */
                threshold = mode_music + ((voice_est * voice_est * (mode_voice - mode_music)) >> 14);
                /* Bias towards SILK for VoIP because of some useful features */
                if (application == OpusApplication.OPUS_APPLICATION_VOIP)
                    threshold += 8000;

                /*printf("%f %d\n", stereo_width/(float)1.0f, threshold);*/
                /* Hysteresis */
                if (prev_mode == OpusMode.MODE_CELT_ONLY)
                    threshold -= 4000;
                else if (prev_mode > 0)
                    threshold += 4000;

                mode = (equiv_rate >= threshold) ? OpusMode.MODE_CELT_ONLY : OpusMode.MODE_SILK_ONLY;

                /* When FEC is enabled and there's enough packet loss, use SILK */
                if (silk_mode.useInBandFEC != 0 && silk_mode.packetLossPercentage > (128 - voice_est) >> 4)
                    mode = OpusMode.MODE_SILK_ONLY;
                /* When encoding voice and DTX is enabled, set the encoder to SILK mode (at least for now) */
                if (silk_mode.useDTX != 0 && voice_est > 100)
                    mode = OpusMode.MODE_SILK_ONLY;
#endif
            }
            else
            {
                mode = user_forced_mode;
            }

            /* Override the chosen mode to make sure we meet the requested frame size */
            if (mode != OpusMode.MODE_CELT_ONLY && frame_size < Fs / 100)
                mode = OpusMode.MODE_CELT_ONLY;
            if (lfe != 0)
                mode = OpusMode.MODE_CELT_ONLY;
            /* If max_data_bytes represents less than 8 kb/s, switch to CELT-only mode */
            if (max_data_bytes < (frame_rate > 50 ? 12000 : 8000) * frame_size / (Fs * 8))
                mode = OpusMode.MODE_CELT_ONLY;

            if (stream_channels == 1 && prev_channels == 2 && silk_mode.toMono == 0
                  && mode != OpusMode.MODE_CELT_ONLY && prev_mode != OpusMode.MODE_CELT_ONLY)
            {
                /* Delay stereo.mono transition by two frames so that SILK can do a smooth downmix */
                silk_mode.toMono = 1;
                stream_channels = 2;
            }
            else
            {
                silk_mode.toMono = 0;
            }

            if (prev_mode > 0 &&
                ((mode != OpusMode.MODE_CELT_ONLY && prev_mode == OpusMode.MODE_CELT_ONLY) ||
            (mode == OpusMode.MODE_CELT_ONLY && prev_mode != OpusMode.MODE_CELT_ONLY)))
            {
                redundancy = 1;
                celt_to_silk = (mode != OpusMode.MODE_CELT_ONLY) ? 1 : 0;
                if (celt_to_silk == 0)
                {
                    /* Switch to SILK/hybrid if frame size is 10 ms or more*/
                    if (frame_size >= Fs / 100)
                    {
                        mode = prev_mode;
                        to_celt = 1;
                    }
                    else
                    {
                        redundancy = 0;
                    }
                }
            }
            /* For the first frame at a new SILK bandwidth */
            if (silk_bw_switch != 0)
            {
                redundancy = 1;
                celt_to_silk = 1;
                silk_bw_switch = 0;
                prefill = 1;
            }

            if (redundancy != 0)
            {
                /* Fair share of the max size allowed */
                redundancy_bytes = Inlines.IMIN(257, max_data_bytes * (Fs / 200) / (frame_size + Fs / 200));
                /* For VBR, target the actual bitrate (subject to the limit above) */
                if (use_vbr != 0)
                    redundancy_bytes = Inlines.IMIN(redundancy_bytes, bitrate_bps / 1600);
            }

            if (mode != OpusMode.MODE_CELT_ONLY && prev_mode == OpusMode.MODE_CELT_ONLY)
            {
                EncControlState dummy = new();
                EncodeAPI.silk_InitEncoder(silk_enc, dummy);
                prefill = 1;
            }

            /* Automatic (rate-dependent) bandwidth selection */
            if (mode == OpusMode.MODE_CELT_ONLY || first != 0 || silk_mode.allowBandwidthSwitch != 0)
            {
                int[] voice_bandwidth_thresholds;
                int[] music_bandwidth_thresholds;
                int[] bandwidth_thresholds = new int[8];
                OpusBandwidth bandwidth = OpusBandwidth.OPUS_BANDWIDTH_FULLBAND;
                int equiv_rate2;

                equiv_rate2 = equiv_rate;
                if (mode != OpusMode.MODE_CELT_ONLY)
                {
                    /* Adjust the threshold +/- 10% depending on complexity */
                    equiv_rate2 = equiv_rate2 * (45 + silk_mode.complexity) / 50;
                    /* CBR is less efficient by ~1 kb/s */
                    if (use_vbr == 0)
                        equiv_rate2 -= 1000;
                }
                if (channels == 2 && force_channels != 1)
                {
                    voice_bandwidth_thresholds = Tables.stereo_voice_bandwidth_thresholds;
                    music_bandwidth_thresholds = Tables.stereo_music_bandwidth_thresholds;
                }
                else
                {
                    voice_bandwidth_thresholds = Tables.mono_voice_bandwidth_thresholds;
                    music_bandwidth_thresholds = Tables.mono_music_bandwidth_thresholds;
                }
                /* Interpolate bandwidth thresholds depending on voice estimation */
                for (i = 0; i < 8; i++)
                {
                    bandwidth_thresholds[i] = music_bandwidth_thresholds[i]
                             + ((voice_est * voice_est * (voice_bandwidth_thresholds[i] - music_bandwidth_thresholds[i])) >> 14);
                }
                do
                {
                    int threshold, hysteresis;
                    threshold = bandwidth_thresholds[2 * (bandwidth - OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND)];
                    hysteresis = bandwidth_thresholds[2 * (bandwidth - OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND) + 1];
                    if (first == 0)
                    {
                        if (this.bandwidth >= bandwidth)
                            threshold -= hysteresis;
                        else
                            threshold += hysteresis;
                    }
                    if (equiv_rate2 >= threshold)
                        break;
                } while (--bandwidth > OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND);
                this.bandwidth = bandwidth;
                /* Prevents any transition to SWB/FB until the SILK layer has fully
                   switched to WB mode and turned the variable LP filter off */
                if (first == 0 && mode != OpusMode.MODE_CELT_ONLY && silk_mode.inWBmodeWithoutVariableLP == 0 && this.bandwidth > OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND)
                    this.bandwidth = OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND;
            }

            if (bandwidth > max_bandwidth)
                bandwidth = max_bandwidth;

            if (user_bandwidth != OpusBandwidth.OPUS_BANDWIDTH_AUTO)
                bandwidth = user_bandwidth;

            /* This prevents us from using hybrid at unsafe CBR/max rates */
            if (mode != OpusMode.MODE_CELT_ONLY && max_rate < 15000)
            {
                bandwidth = OpusBandwidthHelpers.MIN(bandwidth, OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND);
            }

            /* Prevents Opus from wasting bits on frequencies that are above
               the Nyquist rate of the input signal */
            if (Fs <= 24000 && bandwidth > OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND)
                bandwidth = OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND;
            if (Fs <= 16000 && bandwidth > OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND)
                bandwidth = OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND;
            if (Fs <= 12000 && bandwidth > OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND)
                bandwidth = OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND;
            if (Fs <= 8000 && bandwidth > OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND)
                bandwidth = OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND;
            /* Use detected bandwidth to reduce the encoded bandwidth. */
            if (detected_bandwidth != 0 && user_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_AUTO)
            {
                OpusBandwidth min_detected_bandwidth;
                /* Makes bandwidth detection more conservative just in case the detector
                   gets it wrong when we could have coded a high bandwidth transparently.
                   When operating in SILK/hybrid mode, we don't go below wideband to avoid
                   more complicated switches that require redundancy. */
                if (equiv_rate <= 18000 * stream_channels && mode == OpusMode.MODE_CELT_ONLY)
                    min_detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND;
                else if (equiv_rate <= 24000 * stream_channels && mode == OpusMode.MODE_CELT_ONLY)
                    min_detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND;
                else if (equiv_rate <= 30000 * stream_channels)
                    min_detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND;
                else if (equiv_rate <= 44000 * stream_channels)
                    min_detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND;
                else
                    min_detected_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_FULLBAND;

                detected_bandwidth = OpusBandwidthHelpers.MAX(detected_bandwidth, min_detected_bandwidth);
                bandwidth = OpusBandwidthHelpers.MIN(bandwidth, detected_bandwidth);
            }
            celt_enc.SetLSBDepth(lsb_depth);

            /* CELT mode doesn't support mediumband, use wideband instead */
            if (mode == OpusMode.MODE_CELT_ONLY && bandwidth == OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND)
                bandwidth = OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND;
            if (lfe != 0)
                bandwidth = OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND;

            /* Can't support higher than wideband for >20 ms frames */
            if (frame_size > Fs / 50 && (mode == OpusMode.MODE_CELT_ONLY || bandwidth > OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND))
            {
                byte[] tmp_data;
                int nb_frames;
                OpusBandwidth bak_bandwidth;
                int bak_channels, bak_to_mono;
                OpusMode bak_mode;
                OpusRepacketizer rp;
                int bytes_per_frame;
                int repacketize_len;

                if (analysis.enabled && analysis_read_pos_bak != -1)
                {
                    analysis.read_pos = analysis_read_pos_bak;
                    analysis.read_subframe = analysis_read_subframe_bak;
                }

                nb_frames = frame_size > Fs / 25 ? 3 : 2;
                bytes_per_frame = Inlines.IMIN(1276, (out_data_bytes - 3) / nb_frames);

                tmp_data = new byte[nb_frames * bytes_per_frame];

                rp = new OpusRepacketizer();

                bak_mode = user_forced_mode;
                bak_bandwidth = user_bandwidth;
                bak_channels = force_channels;

                user_forced_mode = mode;
                user_bandwidth = bandwidth;
                force_channels = stream_channels;
                bak_to_mono = silk_mode.toMono;

                if (bak_to_mono != 0)
                    force_channels = 1;
                else
                    prev_channels = stream_channels;
                for (i = 0; i < nb_frames; i++)
                {
                    int tmp_len;
                    silk_mode.toMono = 0;
                    /* When switching from SILK/Hybrid to CELT, only ask for a switch at the last frame */
                    if (to_celt != 0 && i == nb_frames - 1)
                        user_forced_mode = OpusMode.MODE_CELT_ONLY;
                    tmp_len = opus_encode_native(pcm, pcm_ptr + (i * (channels * Fs / 50)), Fs / 50,
                          tmp_data, i * bytes_per_frame, bytes_per_frame, lsb_depth,
                          null, 0, c1, c2, analysis_channels, downmix, float_api);
                    if (tmp_len < 0)
                    {

                        return OpusError.OPUS_INTERNAL_ERROR;
                    }
                    ret = rp.AddPacket(tmp_data, i * bytes_per_frame, tmp_len);
                    if (ret < 0)
                    {

                        return OpusError.OPUS_INTERNAL_ERROR;
                    }
                }
                if (use_vbr != 0)
                    repacketize_len = out_data_bytes;
                else
                    repacketize_len = Inlines.IMIN(3 * bitrate_bps / (3 * 8 * 50 / nb_frames), out_data_bytes);
                ret = rp.opus_repacketizer_out_range_impl(0, nb_frames, data, data_ptr, repacketize_len, 0, (use_vbr == 0) ? 1 : 0);
                if (ret < 0)
                {
                    return OpusError.OPUS_INTERNAL_ERROR;
                }
                user_forced_mode = bak_mode;
                user_bandwidth = bak_bandwidth;
                force_channels = bak_channels;
                silk_mode.toMono = bak_to_mono;

                return ret;
            }
            curr_bandwidth = bandwidth;

            /* Chooses the appropriate mode for speech
               *NEVER* switch to/from CELT-only mode here as this will invalidate some assumptions */
            if (mode == OpusMode.MODE_SILK_ONLY && curr_bandwidth > OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND)
                mode = OpusMode.MODE_HYBRID;
            if (mode == OpusMode.MODE_HYBRID && curr_bandwidth <= OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND)
                mode = OpusMode.MODE_SILK_ONLY;

            /* printf("%d %d %d %d\n", st.bitrate_bps, st.stream_channels, st.mode, curr_bandwidth); */
            bytes_target = Inlines.IMIN(max_data_bytes - redundancy_bytes, bitrate_bps * frame_size / (Fs * 8)) - 1;

            data_ptr += 1;

            enc.enc_init((uint)(max_data_bytes - 1));

            pcm_buf = new short[(total_buffer + frame_size) * channels];
            Arrays.MemCopy(delay_buffer, ((encoder_buffer - total_buffer) * channels), pcm_buf, 0, total_buffer * channels);

            if (mode == OpusMode.MODE_CELT_ONLY)
                hp_freq_smth1 = Inlines.silk_LSHIFT(Inlines.silk_lin2log(TuningParameters.VARIABLE_HP_MIN_CUTOFF_HZ), 8);
            else
                hp_freq_smth1 = silk_enc.state_Fxx[0].variable_HP_smth1_Q15;

            variable_HP_smth2_Q15 = Inlines.silk_SMLAWB(variable_HP_smth2_Q15,
                  hp_freq_smth1 - variable_HP_smth2_Q15, ((int)((TuningParameters.VARIABLE_HP_SMTH_COEF2) * ((long)1 << (16)) + 0.5))/*Inlines.SILK_CONST(TuningParameters.VARIABLE_HP_SMTH_COEF2, 16)*/);

            /* convert from log scale to Hertz */
            cutoff_Hz = Inlines.silk_log2lin(Inlines.silk_RSHIFT(variable_HP_smth2_Q15, 8));

            if (application == OpusApplication.OPUS_APPLICATION_VOIP)
            {
                CodecHelpers.hp_cutoff(pcm, pcm_ptr, cutoff_Hz, pcm_buf, (total_buffer * channels), hp_mem, frame_size, channels, Fs);
            }
            else
            {
                CodecHelpers.dc_reject(pcm, pcm_ptr, 3, pcm_buf, total_buffer * channels, hp_mem, frame_size, channels, Fs);
            }

            /* SILK processing */
            HB_gain = CeltConstants.Q15ONE;
            if (mode != OpusMode.MODE_CELT_ONLY)
            {
                int total_bitRate, celt_rate;
                short[] pcm_silk = new short[channels * frame_size];

                /* Distribute bits between SILK and CELT */
                total_bitRate = 8 * bytes_target * frame_rate;
                if (mode == OpusMode.MODE_HYBRID)
                {
                    int HB_gain_ref;
                    /* Base rate for SILK */
                    silk_mode.bitRate = stream_channels * (5000 + ((Fs == 100 * frame_size) ? 1000 : 0));
                    if (curr_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND)
                    {
                        /* SILK gets 2/3 of the remaining bits */
                        silk_mode.bitRate += (total_bitRate - silk_mode.bitRate) * 2 / 3;
                    }
                    else
                    { /* FULLBAND */
                        /* SILK gets 3/5 of the remaining bits */
                        silk_mode.bitRate += (total_bitRate - silk_mode.bitRate) * 3 / 5;
                    }
                    /* Don't let SILK use more than 80% */
                    if (silk_mode.bitRate > total_bitRate * 4 / 5)
                    {
                        silk_mode.bitRate = total_bitRate * 4 / 5;
                    }
                    if (energy_masking == null)
                    {
                        /* Increasingly attenuate high band when it gets allocated fewer bits */
                        celt_rate = total_bitRate - silk_mode.bitRate;
                        HB_gain_ref = (curr_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND) ? 3000 : 3600;
                        HB_gain = Inlines.SHL32(celt_rate, 9) / Inlines.SHR32(celt_rate + stream_channels * HB_gain_ref, 6);
                        HB_gain = HB_gain < CeltConstants.Q15ONE * 6 / 7 ? HB_gain + CeltConstants.Q15ONE / 7 : CeltConstants.Q15ONE;
                    }
                }
                else
                {
                    /* SILK gets all bits */
                    silk_mode.bitRate = total_bitRate;
                }

                /* Surround masking for SILK */
                if (energy_masking != null && use_vbr != 0 && lfe == 0)
                {
                    int mask_sum = 0;
                    int masking_depth;
                    int rate_offset;
                    int c;
                    int end = 17;
                    short srate = 16000;
                    if (bandwidth == OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND)
                    {
                        end = 13;
                        srate = 8000;
                    }
                    else if (bandwidth == OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND)
                    {
                        end = 15;
                        srate = 12000;
                    }
                    for (c = 0; c < channels; c++)
                    {
                        for (i = 0; i < end; i++)
                        {
                            int mask;
                            mask = Inlines.MAX16(Inlines.MIN16(energy_masking[21 * c + i],
                                   ((short)(0.5 + (.5f) * (1 << (10))))/*Inlines.QCONST16(.5f, 10)*/), -((short)(0.5 + (2.0f) * (1 << (10))))/*Inlines.QCONST16(2.0f, 10)*/);
                            if (mask > 0)
                                mask = Inlines.HALF16(mask);
                            mask_sum += mask;
                        }
                    }
                    /* Conservative rate reduction, we cut the masking in half */
                    masking_depth = mask_sum / end * channels;
                    masking_depth += ((short)(0.5 + (.2f) * (1 << (10))))/*Inlines.QCONST16(.2f, 10)*/;
                    rate_offset = Inlines.PSHR32(Inlines.MULT16_16(srate, masking_depth), 10);
                    rate_offset = Inlines.MAX32(rate_offset, -2 * silk_mode.bitRate / 3);
                    /* Split the rate change between the SILK and CELT part for hybrid. */
                    if (bandwidth == OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND || bandwidth == OpusBandwidth.OPUS_BANDWIDTH_FULLBAND)
                        silk_mode.bitRate += 3 * rate_offset / 5;
                    else
                        silk_mode.bitRate += rate_offset;
                    bytes_target += rate_offset * frame_size / (8 * Fs);
                }

                silk_mode.payloadSize_ms = 1000 * frame_size / Fs;
                silk_mode.nChannelsAPI = channels;
                silk_mode.nChannelsInternal = stream_channels;
                if (curr_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND)
                {
                    silk_mode.desiredInternalSampleRate = 8000;
                }
                else if (curr_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND)
                {
                    silk_mode.desiredInternalSampleRate = 12000;
                }
                else
                {
                    Inlines.OpusAssert(mode == OpusMode.MODE_HYBRID || curr_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND);
                    silk_mode.desiredInternalSampleRate = 16000;
                }
                if (mode == OpusMode.MODE_HYBRID)
                {
                    /* Don't allow bandwidth reduction at lowest bitrates in hybrid mode */
                    silk_mode.minInternalSampleRate = 16000;
                }
                else
                {
                    silk_mode.minInternalSampleRate = 8000;
                }

                if (mode == OpusMode.MODE_SILK_ONLY)
                {
                    int effective_max_rate = max_rate;
                    silk_mode.maxInternalSampleRate = 16000;
                    if (frame_rate > 50)
                        effective_max_rate = effective_max_rate * 2 / 3;
                    if (effective_max_rate < 13000)
                    {
                        silk_mode.maxInternalSampleRate = 12000;
                        silk_mode.desiredInternalSampleRate = Inlines.IMIN(12000, silk_mode.desiredInternalSampleRate);
                    }
                    if (effective_max_rate < 9600)
                    {
                        silk_mode.maxInternalSampleRate = 8000;
                        silk_mode.desiredInternalSampleRate = Inlines.IMIN(8000, silk_mode.desiredInternalSampleRate);
                    }
                }
                else
                {
                    silk_mode.maxInternalSampleRate = 16000;
                }

                silk_mode.useCBR = use_vbr == 0 ? 1 : 0;

                /* Call SILK encoder for the low band */
                nBytes = Inlines.IMIN(1275, max_data_bytes - 1 - redundancy_bytes);

                silk_mode.maxBits = nBytes * 8;
                /* Only allow up to 90% of the bits for hybrid mode*/
                if (mode == OpusMode.MODE_HYBRID)
                    silk_mode.maxBits = silk_mode.maxBits * 9 / 10;
                if (silk_mode.useCBR != 0)
                {
                    silk_mode.maxBits = (silk_mode.bitRate * frame_size / (Fs * 8)) * 8;
                    /* Reduce the initial target to make it easier to reach the CBR rate */
                    silk_mode.bitRate = Inlines.IMAX(1, silk_mode.bitRate - 2000);
                }

                if (prefill != 0)
                {
                    BoxedValueInt zero = new(0);
                    int prefill_offset;

                    /* Use a smooth onset for the SILK prefill to avoid the encoder trying to encode
                       a discontinuity. The exact location is what we need to avoid leaving any "gap"
                       in the audio when mixing with the redundant CELT frame. Here we can afford to
                       overwrite st.delay_buffer because the only thing that uses it before it gets
                       rewritten is tmp_prefill[] and even then only the part after the ramp really
                       gets used (rather than sent to the encoder and discarded) */
                    prefill_offset = channels * (encoder_buffer - this.delay_compensation - Fs / 400);
                    CodecHelpers.gain_fade(delay_buffer, prefill_offset,
                          0, CeltConstants.Q15ONE, celt_mode.overlap, Fs / 400, channels, celt_mode.window, Fs);
                    Arrays.MemSetShort(delay_buffer, 0, prefill_offset);
                    Arrays.MemCopy(delay_buffer, 0, pcm_silk, 0, encoder_buffer * channels);

                    EncodeAPI.silk_Encode(silk_enc, silk_mode, pcm_silk, encoder_buffer, null, data.Slice(data_ptr), zero, 1);
                }

                Arrays.MemCopy(pcm_buf, total_buffer * channels, pcm_silk, 0, frame_size * channels);

                BoxedValueInt boxed_silkBytes = new(nBytes);
                ret = EncodeAPI.silk_Encode(silk_enc, silk_mode, pcm_silk, frame_size, enc, data.Slice(data_ptr), boxed_silkBytes, 0);
                nBytes = boxed_silkBytes.Val;

                if (ret != 0)
                {
                    /*fprintf (stderr, "SILK encode error: %d\n", ret);*/
                    /* Handle error */

                    return OpusError.OPUS_INTERNAL_ERROR;
                }
                if (nBytes == 0)
                {
                    rangeFinal = 0;
                    data[data_ptr - 1] = CodecHelpers.gen_toc(mode, Fs / frame_size, curr_bandwidth, stream_channels);

                    return 1;
                }
                /* Extract SILK internal bandwidth for signaling in first byte */
                if (mode == OpusMode.MODE_SILK_ONLY)
                {
                    if (silk_mode.internalSampleRate == 8000)
                    {
                        curr_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND;
                    }
                    else if (silk_mode.internalSampleRate == 12000)
                    {
                        curr_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND;
                    }
                    else if (silk_mode.internalSampleRate == 16000)
                    {
                        curr_bandwidth = OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND;
                    }
                }
                else
                {
                    Inlines.OpusAssert(silk_mode.internalSampleRate == 16000);
                }

                silk_mode.opusCanSwitch = silk_mode.switchReady;
                if (silk_mode.opusCanSwitch != 0)
                {
                    redundancy = 1;
                    celt_to_silk = 0;
                    silk_bw_switch = 1;
                }
            }

            /* CELT processing */
            {
                int endband = 21;

                switch (curr_bandwidth)
                {
                    case OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND:
                        endband = 13;
                        break;
                    case OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND:
                    case OpusBandwidth.OPUS_BANDWIDTH_WIDEBAND:
                        endband = 17;
                        break;
                    case OpusBandwidth.OPUS_BANDWIDTH_SUPERWIDEBAND:
                        endband = 19;
                        break;
                    case OpusBandwidth.OPUS_BANDWIDTH_FULLBAND:
                        endband = 21;
                        break;
                }
                celt_enc.SetEndBand(endband);
                celt_enc.SetChannels(stream_channels);
            }
            celt_enc.SetBitrate(OpusConstants.OPUS_BITRATE_MAX);
            if (mode != OpusMode.MODE_SILK_ONLY)
            {
                int celt_pred = 2;
                celt_enc.SetVBR(false);
                /* We may still decide to disable prediction later */
                if (silk_mode.reducedDependency != 0)
                    celt_pred = 0;
                celt_enc.SetPrediction(celt_pred);

                if (mode == OpusMode.MODE_HYBRID)
                {
                    int len;

                    len = (enc.tell() + 7) >> 3;
                    if (redundancy != 0)
                        len += mode == OpusMode.MODE_HYBRID ? 3 : 1;
                    if (use_vbr != 0)
                    {
                        nb_compr_bytes = len + bytes_target - (silk_mode.bitRate * frame_size) / (8 * Fs);
                    }
                    else
                    {
                        /* check if SILK used up too much */
                        nb_compr_bytes = len > bytes_target ? len : bytes_target;
                    }
                }
                else
                {
                    if (use_vbr != 0)
                    {
                        int bonus = 0;
                        if (analysis.enabled && variable_duration == OpusFramesize.OPUS_FRAMESIZE_VARIABLE && frame_size != Fs / 50)
                        {
                            bonus = (60 * stream_channels + 40) * (Fs / frame_size - 50);
                            if (analysis_info.valid != 0)
                                bonus = (int)(bonus * (1.0f + .5f * analysis_info.tonality));
                        }
                        celt_enc.SetVBR(true);
                        celt_enc.SetVBRConstraint(vbr_constraint != 0);
                        celt_enc.SetBitrate(bitrate_bps + bonus);
                        nb_compr_bytes = max_data_bytes - 1 - redundancy_bytes;
                    }
                    else
                    {
                        nb_compr_bytes = bytes_target;
                    }
                }

            }
            else
            {
                nb_compr_bytes = 0;
            }


            tmp_prefill = new short[channels * Fs / 400];
            if (mode != OpusMode.MODE_SILK_ONLY && mode != prev_mode && prev_mode > 0)
            {
                Arrays.MemCopy(delay_buffer, ((encoder_buffer - total_buffer - Fs / 400) * channels), tmp_prefill, 0, channels * Fs / 400);
            }

            if (channels * (encoder_buffer - (frame_size + total_buffer)) > 0)
            {
                Arrays.MemMoveShort(delay_buffer, channels * frame_size, 0, channels * (encoder_buffer - frame_size - total_buffer));
                Arrays.MemCopy(pcm_buf, 0, delay_buffer, (channels * (encoder_buffer - frame_size - total_buffer)), (frame_size + total_buffer) * channels);
            }
            else
            {
                Arrays.MemCopy(pcm_buf, (frame_size + total_buffer - encoder_buffer) * channels, delay_buffer, 0, encoder_buffer * channels);
            }

            /* gain_fade() and stereo_fade() need to be after the buffer copying
               because we don't want any of this to affect the SILK part */
            if (prev_HB_gain < CeltConstants.Q15ONE || HB_gain < CeltConstants.Q15ONE)
            {
                CodecHelpers.gain_fade(pcm_buf, 0,
                      prev_HB_gain, HB_gain, celt_mode.overlap, frame_size, channels, celt_mode.window, Fs);
            }

            prev_HB_gain = HB_gain;
            if (mode != OpusMode.MODE_HYBRID || stream_channels == 1)
                silk_mode.stereoWidth_Q14 = Inlines.IMIN((1 << 14), 2 * Inlines.IMAX(0, equiv_rate - 30000));
            if (energy_masking == null && channels == 2)
            {
                /* Apply stereo width reduction (at low bitrates) */
                if (hybrid_stereo_width_Q14 < (1 << 14) || silk_mode.stereoWidth_Q14 < (1 << 14))
                {
                    int g1, g2;
                    g1 = hybrid_stereo_width_Q14;
                    g2 = silk_mode.stereoWidth_Q14;
                    g1 = g1 == 16384 ? CeltConstants.Q15ONE : Inlines.SHL16(g1, 1);
                    g2 = g2 == 16384 ? CeltConstants.Q15ONE : Inlines.SHL16(g2, 1);
                    CodecHelpers.stereo_fade(pcm_buf, g1, g2, celt_mode.overlap,
                          frame_size, channels, celt_mode.window, Fs);
                    hybrid_stereo_width_Q14 = (short)(silk_mode.stereoWidth_Q14);
                }
            }

            if (mode != OpusMode.MODE_CELT_ONLY && enc.tell() + 17 + 20 * ((mode == OpusMode.MODE_HYBRID) ? 1 : 0) <= 8 * (max_data_bytes - 1))
            {
                /* For SILK mode, the redundancy is inferred from the length */
                if (mode == OpusMode.MODE_HYBRID && (redundancy != 0 || enc.tell() + 37 <= 8 * nb_compr_bytes))
                    enc.enc_bit_logp(data.Slice(data_ptr), redundancy, 12);
                if (redundancy != 0)
                {
                    int max_redundancy;
                    enc.enc_bit_logp(data.Slice(data_ptr), celt_to_silk, 1);
                    if (mode == OpusMode.MODE_HYBRID)
                        max_redundancy = (max_data_bytes - 1) - nb_compr_bytes;
                    else
                        max_redundancy = (max_data_bytes - 1) - ((enc.tell() + 7) >> 3);
                    /* Target the same bit-rate for redundancy as for the rest,
                       up to a max of 257 bytes */
                    redundancy_bytes = Inlines.IMIN(max_redundancy, bitrate_bps / 1600);
                    redundancy_bytes = Inlines.IMIN(257, Inlines.IMAX(2, redundancy_bytes));
                    if (mode == OpusMode.MODE_HYBRID)
                        enc.enc_uint(data.Slice(data_ptr), (uint)(redundancy_bytes - 2), 256);
                }
            }
            else
            {
                redundancy = 0;
            }

            if (redundancy == 0)
            {
                silk_bw_switch = 0;
                redundancy_bytes = 0;
            }
            if (mode != OpusMode.MODE_CELT_ONLY) start_band = 17;

            if (mode == OpusMode.MODE_SILK_ONLY)
            {
                ret = (enc.tell() + 7) >> 3;
                enc.enc_done(data.Slice(data_ptr));
                nb_compr_bytes = ret;
            }
            else
            {
                nb_compr_bytes = Inlines.IMIN((max_data_bytes - 1) - redundancy_bytes, nb_compr_bytes);
                enc.enc_shrink(data.Slice(data_ptr), (uint)nb_compr_bytes);
            }

            if (analysis.enabled && redundancy != 0 || mode != OpusMode.MODE_SILK_ONLY)
            {
                celt_enc.SetAnalysis(analysis_info);
            }
            /* 5 ms redundant frame for CELT->SILK */
            if (redundancy != 0 && celt_to_silk != 0)
            {
                int err;
                celt_enc.SetStartBand(0);
                celt_enc.SetVBR(false);
                err = celt_enc.celt_encode_with_ec(pcm_buf, 0, Fs / 200, data, data_ptr + nb_compr_bytes, redundancy_bytes, null);
                if (err < 0)
                {
                    return OpusError.OPUS_INTERNAL_ERROR;
                }
                redundant_rng = celt_enc.GetFinalRange();
                celt_enc.ResetState();
            }

            celt_enc.SetStartBand(start_band);

            if (mode != OpusMode.MODE_SILK_ONLY)
            {
                if (mode != prev_mode && prev_mode > 0)
                {
                    Span<byte> dummy = stackalloc byte[2];
                    celt_enc.ResetState();

                    /* Prefilling */
                    celt_enc.celt_encode_with_ec(tmp_prefill, 0, Fs / 400, dummy, 0, 2, null);
                    celt_enc.SetPrediction(0);
                }
                /* If false, we already busted the budget and we'll end up with a "PLC packet" */
                if (enc.tell() <= 8 * nb_compr_bytes)
                {
                    ret = celt_enc.celt_encode_with_ec(pcm_buf, 0, frame_size, data.Slice(data_ptr), 0, nb_compr_bytes, enc);
                    if (ret < 0)
                    {
                        return OpusError.OPUS_INTERNAL_ERROR;
                    }
                }
            }

            /* 5 ms redundant frame for SILK->CELT */
            if (redundancy != 0 && celt_to_silk == 0)
            {
                int err;
                Span<byte> dummy = stackalloc byte[2];
                int N2, N4;
                N2 = Fs / 200;
                N4 = Fs / 400;

                celt_enc.ResetState();
                celt_enc.SetStartBand(0);
                celt_enc.SetPrediction(0);

                /* NOTE: We could speed this up slightly (at the expense of code size) by just adding a function that prefills the buffer */
                celt_enc.celt_encode_with_ec(pcm_buf, (channels * (frame_size - N2 - N4)), N4, dummy, 0, 2, null);

                err = celt_enc.celt_encode_with_ec(pcm_buf, (channels * (frame_size - N2)), N2, data, data_ptr + nb_compr_bytes, redundancy_bytes, null);
                if (err < 0)
                {
                    return OpusError.OPUS_INTERNAL_ERROR;
                }
                redundant_rng = celt_enc.GetFinalRange();
            }

            /* Signalling the mode in the first byte */
            data_ptr -= 1;
            data[data_ptr] = CodecHelpers.gen_toc(mode, Fs / frame_size, curr_bandwidth, stream_channels);

            rangeFinal = enc.rng ^ redundant_rng;

            if (to_celt != 0)
                prev_mode = OpusMode.MODE_CELT_ONLY;
            else
                prev_mode = mode;
            prev_channels = stream_channels;
            prev_framesize = frame_size;

            first = 0;

            /* In the unlikely case that the SILK encoder busted its target, tell
               the decoder to call the PLC */
            if (enc.tell() > (max_data_bytes - 1) * 8)
            {
                if (max_data_bytes < 2)
                {
                    return OpusError.OPUS_BUFFER_TOO_SMALL;
                }
                data[data_ptr + 1] = 0;
                ret = 1;
                rangeFinal = 0;
            }
            else if (mode == OpusMode.MODE_SILK_ONLY && redundancy == 0)
            {
                /*When in LPC only mode it's perfectly
                  reasonable to strip off trailing zero bytes as
                  the required range decoder behavior is to
                  fill these in. This can't be done when the MDCT
                  modes are used because the decoder needs to know
                  the actual length for allocation purposes.*/
                while (ret > 2 && data[data_ptr + ret] == 0) ret--;
            }
            /* Count ToC and redundancy */
            ret += 1 + redundancy_bytes;
            if (use_vbr == 0)
            {
                if (OpusRepacketizer.PadPacket(data, data_ptr, ret, max_data_bytes) != OpusError.OPUS_OK)
                {
                    return OpusError.OPUS_INTERNAL_ERROR;
                }
                ret = max_data_bytes;
            }

            return ret;
        }

        // Bitrate table used for constant quality encoding
        // Column 1 is bitrate for 100% speech, column 2 is 100% music
        private static readonly int[][] vqTable = new int[][]
        {
            new int[] { 7000, 16000 }, // VQ 0
            new int[] { 10000, 24000 }, // VQ 1
            new int[] { 13000, 32000 }, // VQ 2
            new int[] { 17000, 48000 }, // VQ 3
            new int[] { 20000, 64000 }, // VQ 4
            new int[] { 24000, 80000 }, // VQ 5
            new int[] { 28000, 96000 }, // VQ 6
            new int[] { 32000, 112000 }, // VQ 7
            new int[] { 38000, 128000 }, // VQ 8
            new int[] { 48000, 192000 }, // VQ 9
            new int[] { 64000, 256000 }, // VQ 10
        };

        private static int GetVariableQualityBitrate(int vqLevel, float music_prob)
        {
            float low = vqTable[vqLevel][0];
            float high = vqTable[vqLevel][1];
            // Use a simple square root curve to cause the VQ to favor higher bitrates unless speech is very confident
            float curvedQual = (float)Math.Sqrt(music_prob);
            return (int)((high * curvedQual) + (low * (1 - curvedQual)));
        }

        /// <summary>
        /// Encodes an Opus frame.
        /// </summary>
        /// <param name="in_pcm">Input signal (Interleaved if stereo). Length should be at least frame_size * channels</param>
        /// <param name="pcm_offset">Offset to use when reading the in_pcm buffer</param>
        /// <param name="frame_size">The number of samples per channel in the inpus signal.
        /// The frame size must be a valid Opus framesize for the given sample rate.
        /// For example, at 48Khz the permitted values are 120, 240, 480, 960, 1920, and 2880. Passing in a duration of less than 10ms
        /// (480 samples at 48Khz) will prevent the encoder from using FEC, DTX, or hybrid modes.</param>
        /// <param name="out_data">Destination buffer for the output payload. This must contain at least max_data_bytes</param>
        /// <param name="out_data_offset">The offset to use when writing to the output data buffer</param>
        /// <param name="max_data_bytes">The maximum amount of space allocated for the output payload. This may be used to impose
        /// an upper limit on the instant bitrate, but should not be used as the only bitrate control (use the Bitrate parameter for that)</param>
        /// <returns>The length of the encoded packet, in bytes. This value will always be less than or equal to 1275, the maximum Opus packet size.</returns>
        [Obsolete("Use Span<> overrides if possible")]
        public int Encode(short[] in_pcm, int pcm_offset, int frame_size,
              byte[] out_data, int out_data_offset, int max_data_bytes)
        {
            return Encode(in_pcm.AsSpan(pcm_offset), frame_size, out_data.AsSpan(out_data_offset), max_data_bytes);
        }

        /// <summary>
        /// Encodes an Opus frame.
        /// </summary>
        /// <param name="in_pcm">Input signal (Interleaved if stereo). Length should be at least frame_size * channels</param>
        /// <param name="frame_size">The number of samples per channel in the inpus signal.
        /// The frame size must be a valid Opus framesize for the given sample rate.
        /// For example, at 48Khz the permitted values are 120, 240, 480, 960, 1920, and 2880. Passing in a duration of less than 10ms
        /// (480 samples at 48Khz) will prevent the encoder from using FEC, DTX, or hybrid modes.</param>
        /// <param name="out_data">Destination buffer for the output payload. This must contain at least max_data_bytes</param>
        /// <param name="max_data_bytes">The maximum amount of space allocated for the output payload. This may be used to impose
        /// an upper limit on the instant bitrate, but should not be used as the only bitrate control (use the Bitrate parameter for that)</param>
        /// <returns>The length of the encoded packet, in bytes. This value will always be less than or equal to 1275, the maximum Opus packet size.</returns>
        public int Encode(ReadOnlySpan<short> in_pcm, int frame_size, Span<byte> out_data, int max_data_bytes)
        {
            // Check that the caller is telling the truth about its input buffers
            if (max_data_bytes > out_data.Length)
            {
                throw new ArgumentException(string.Format(
                    "Output buffer is too small: Stated size is {0} bytes, actual size is {1} bytes",
                    max_data_bytes, out_data.Length));
            }

            int delay_compensation;
            if (application == OpusApplication.OPUS_APPLICATION_RESTRICTED_LOWDELAY)
                delay_compensation = 0;
            else
                delay_compensation = this.delay_compensation;

            int internal_frame_size = CodecHelpers.compute_frame_size(in_pcm, frame_size,
                  variable_duration, channels, Fs, bitrate_bps,
                  delay_compensation, Downmix.downmix_int, analysis.subframe_mem, analysis.enabled);

            // Check that input pcm length is >= frame_size
            if (internal_frame_size > in_pcm.Length)
            {
                throw new ArgumentException(string.Format(
                    "Not enough samples provided in input signal: Expected {0} samples, found {1}",
                    internal_frame_size, in_pcm.Length));
            }

            try
            {
                int ret = opus_encode_native(in_pcm, 0, internal_frame_size, out_data, 0, max_data_bytes, 16,
                                         in_pcm, frame_size, 0, -2, channels, Downmix.downmix_int, 0);

                if (ret < 0)
                {
                    // An error happened; report it
                    if (ret == OpusError.OPUS_BAD_ARG)
                        throw new ArgumentException("OPUS_BAD_ARG while encoding");
                    throw new OpusException("An error occurred during encoding: " + CodecHelpers.opus_strerror(ret), ret);
                }

                return ret;
            }
            catch (ArgumentException e)
            {
                throw new OpusException("public error during encoding: " + e.Message, OpusError.OPUS_BAD_ARG);
            }
        }

        /// <summary>
        /// Encodes an Opus frame using floating point input.
        /// </summary>
        /// <param name="in_pcm">Input signal in float format (Interleaved if stereo). Length should be at least frame_size * channels.
        /// Value should be normalized to the +/- 1.0 range. Samples with a range beyond +/-1.0 will be clipped.</param>
        /// <param name="in_pcm_offset">Offset to use when reading from in_pcm buffer</param>
        /// <param name="frame_size">The number of samples per channel in the inpus signal.
        /// The frame size must be a valid Opus framesize for the given sample rate.
        /// For example, at 48Khz the permitted values are 120, 240, 480, 960, 1920, and 2880. Passing in a duration of less than 10ms
        /// (480 samples at 48Khz) will prevent the encoder from using FEC, DTX, or hybrid modes.</param>
        /// <param name="out_data">Destination buffer for the output payload. This must contain at least max_data_bytes</param>
        /// <param name="out_data_offset">Offset to use when writing into output data buffer</param>
        /// <param name="max_data_bytes">The maximum amount of space allocated for the output payload. This may be used to impose
        /// an upper limit on the instant bitrate, but should not be used as the only bitrate control (use the Bitrate parameter for that)</param>
        /// <returns>The length of the encoded packet, in bytes. This value will always be less than or equal to 1275, the maximum Opus packet size.</returns>
        [Obsolete("Use Span<> overrides if possible")]
        public int Encode(float[] in_pcm, int in_pcm_offset, int frame_size, byte[] out_data, int out_data_offset, int max_data_bytes)
        {
            return Encode(in_pcm.AsSpan(in_pcm_offset), frame_size, out_data.AsSpan(out_data_offset), max_data_bytes);
        }

        /// <summary>
        /// Encodes an Opus frame using floating point input.
        /// </summary>
        /// <param name="in_pcm">Input signal in float format (Interleaved if stereo). Length should be at least frame_size * channels.
        /// Value should be normalized to the +/- 1.0 range. Samples with a range beyond +/-1.0 will be clipped.</param>
        /// <param name="frame_size">The number of samples per channel in the inpus signal.
        /// The frame size must be a valid Opus framesize for the given sample rate.
        /// For example, at 48Khz the permitted values are 120, 240, 480, 960, 1920, and 2880. Passing in a duration of less than 10ms
        /// (480 samples at 48Khz) will prevent the encoder from using FEC, DTX, or hybrid modes.</param>
        /// <param name="out_data">Destination buffer for the output payload. This must contain at least max_data_bytes</param>
        /// <param name="max_data_bytes">The maximum amount of space allocated for the output payload. This may be used to impose
        /// an upper limit on the instant bitrate, but should not be used as the only bitrate control (use the Bitrate parameter for that)</param>
        /// <returns>The length of the encoded packet, in bytes. This value will always be less than or equal to 1275, the maximum Opus packet size.</returns>
        public int Encode(ReadOnlySpan<float> in_pcm, int frame_size, Span<byte> out_data, int max_data_bytes)
        {
            // Check that the caller is telling the truth about its input buffers
            if (max_data_bytes > out_data.Length)
            {
                throw new ArgumentException(string.Format(
                    "Output buffer is too small: Stated size is {0} bytes, actual size is {1} bytes",
                    max_data_bytes, out_data.Length));
            }

            int i, ret;
            int internal_frame_size;
            int delay_compensation;
            short[] input;

            if (application == OpusApplication.OPUS_APPLICATION_RESTRICTED_LOWDELAY)
                delay_compensation = 0;
            else
                delay_compensation = this.delay_compensation;

            internal_frame_size = CodecHelpers.compute_frame_size(in_pcm, frame_size,
                  variable_duration, channels, Fs, bitrate_bps,
                  delay_compensation, Downmix.downmix_float, analysis.subframe_mem, analysis.enabled);

            // Check that input pcm length is >= frame_size
            if (internal_frame_size > in_pcm.Length)
            {
                throw new ArgumentException(string.Format(
                    "Not enough samples provided in input signal: Expected {0} samples, found {1}",
                    internal_frame_size, in_pcm.Length));
            }

            input = new short[internal_frame_size * channels];

            for (i = 0; i < internal_frame_size * channels; i++)
                input[i] = Inlines.FLOAT2INT16(in_pcm[i]);

            try
            {
                ret = opus_encode_native(input, 0, internal_frame_size, out_data, 0, max_data_bytes, 16,
                                     in_pcm, frame_size, 0, -2, channels, Downmix.downmix_float, 1);

                if (ret < 0)
                {
                    // An error happened; report it
                    if (ret == OpusError.OPUS_BAD_ARG)
                        throw new ArgumentException("OPUS_BAD_ARG while decoding");
                    throw new OpusException("An error occurred during encoding: " + CodecHelpers.opus_strerror(ret), ret);
                }

                return ret;
            }
            catch (ArgumentException e)
            {
                throw new OpusException("public error during encoding: " + e.Message, OpusError.OPUS_BAD_ARG);
            }
        }

        #endregion

        #region Getters and Setters

        /// <summary>
        /// Gets or sets the application (or signal type) of the input signal. This hints
        /// to the encoder what type of details we want to preserve in the encoding.
        /// This cannot be changed after the encoder has started
        /// </summary>
        public OpusApplication Application
        {
            get
            {
                return application;
            }
            set
            {
                if (first == 0 && application != value)
                {
                    throw new ArgumentException("Application cannot be changed after encoding has started");
                }

                application = value;
            }
        }

        /// <summary>
        /// Gets or sets the bitrate for encoder, in bits per second. Valid bitrates are between 6K (6144) and 510K (522240)
        /// </summary>
        public int Bitrate
        {
            get
            {
                return user_bitrate_to_bitrate(user_bitrate_bps, prev_framesize, 1276);
            }
            set
            {
                if (ConstantQuality.HasValue)
                {
                    throw new ArgumentException("Bitrate is read-only while the ConstantQuality parameter is set");
                }

                if (value != OpusConstants.OPUS_AUTO && value != OpusConstants.OPUS_BITRATE_MAX)
                {
                    if (value <= 0)
                        throw new ArgumentException("Bitrate must be positive");
                    else if (value <= 500)
                        value = 500;
                    else if (value > 300000 * channels)
                        value = 300000 * channels;
                }

                user_bitrate_bps = value;
            }
        }

        /// <summary>
        /// Gets or sets the maximum number of channels to be encoded. This can be used to force a downmix from stereo to mono if stereo
        /// separation is not important
        /// </summary>
        public int ForceChannels
        {
            get
            {
                return force_channels;
            }
            set
            {
                if ((value < 1 || value > channels) && value != OpusConstants.OPUS_AUTO)
                {
                    throw new ArgumentException("Force channels must be <= num. of channels");
                }

                force_channels = value;
            }
        }

        /// <summary>
        /// Gets or sets the maximum bandwidth to be used by the encoder. This can be used if
        /// high-frequency audio is not important to your application (e.g. telephony)
        /// </summary>
        public OpusBandwidth MaxBandwidth
        {
            get
            {
                return max_bandwidth;
            }
            set
            {
                max_bandwidth = value;
                if (max_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND)
                {
                    silk_mode.maxInternalSampleRate = 8000;
                }
                else if (max_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND)
                {
                    silk_mode.maxInternalSampleRate = 12000;
                }
                else
                {
                    silk_mode.maxInternalSampleRate = 16000;
                }
            }
        }

        /// <summary>
        /// Gets or sets the "preferred" encoded bandwidth. This does not affect the sample rate of the input audio,
        /// only the encoding cutoffs
        /// </summary>
        public OpusBandwidth Bandwidth
        {
            get
            {
                return bandwidth;
            }
            set
            {
                user_bandwidth = value;
                if (user_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_NARROWBAND)
                {
                    silk_mode.maxInternalSampleRate = 8000;
                }
                else if (user_bandwidth == OpusBandwidth.OPUS_BANDWIDTH_MEDIUMBAND)
                {
                    silk_mode.maxInternalSampleRate = 12000;
                }
                else
                {
                    silk_mode.maxInternalSampleRate = 16000;
                }
            }
        }

        /// <summary>
        /// Gets or sets a flag to enable Discontinuous Transmission mode. This mode is only available in the SILK encoder
        /// (Bitrate &lt; 40Kbit/s and/or ForceMode == SILK). When enabled, the encoder detects silence and background noise
        /// and reduces the number of output packets, with up to 600ms in between separate packet transmissions.
        /// </summary>
        public bool UseDTX
        {
            get
            {
                return silk_mode.useDTX != 0;
            }
            set
            {
                silk_mode.useDTX = value ? 1 : 0;
            }
        }

        /// <summary>
        /// Gets or sets the encoder complexity, between 0 and 10
        /// </summary>
        public int Complexity
        {
            get
            {
                return silk_mode.complexity;
            }
            set
            {
                if (value < 0 || value > 10)
                {
                    throw new ArgumentException("Complexity must be between 0 and 10");
                }
                silk_mode.complexity = value;
                Celt_Encoder.SetComplexity(value);
            }
        }

        /// <summary>
        /// Gets or sets a flag to enable Forward Error Correction. This mode is only available in the SILK encoder
        /// (Bitrate &lt; 40Kbit/s and/or ForceMode == SILK). When enabled, lost packets can be partially recovered
        /// by decoding data stored in the following packet.
        /// </summary>
        public bool UseInbandFEC
        {
            get
            {
                return silk_mode.useInBandFEC != 0;
            }
            set
            {
                silk_mode.useInBandFEC = value ? 1 : 0;
            }
        }

        /// <summary>
        /// Gets or sets the expected amount of packet loss in the transmission medium, from 0 to 100.
        /// Only applies if UseInbandFEC is also enabled, and the encoder is in SILK mode.
        /// </summary>
        public int PacketLossPercent
        {
            get
            {
                return silk_mode.packetLossPercentage;
            }
            set
            {
                if (value < 0 || value > 100)
                {
                    throw new ArgumentException("Packet loss must be between 0 and 100");
                }
                silk_mode.packetLossPercentage = value;
                Celt_Encoder.SetPacketLossPercent(value);
            }
        }

        /// <summary>
        /// Gets or sets a flag to enable Variable Bitrate encoding. This is recommended as it generally improves audio quality
        /// with little impact on average bitrate
        /// </summary>
        public bool UseVBR
        {
            get
            {
                return use_vbr != 0;
            }
            set
            {
                use_vbr = value ? 1 : 0;
                silk_mode.useCBR = value ? 0 : 1;
            }
        }

        // <summary>
        // Gets or sets the "voice ratio". This is not implemented, but the idea is to hint the amount of voice vs. music in the input signal
        // </summary>
        //public int VoiceRatio
        //{
        //    get
        //    {
        //        return voice_ratio;
        //    }
        //    set
        //    {
        //        if (value < -1 || value > 100)
        //        {
        //            throw new ArgumentException("Voice ratio must be between -1 and 100");
        //        }

        //        voice_ratio = value;
        //    }
        //}

        /// <summary>
        /// Gets or sets a flag to enable constrained VBR. This only applies when the encoder is in CELT mode (i.e. high bitrates)
        /// </summary>
        public bool UseConstrainedVBR
        {
            get
            {
                return vbr_constraint != 0;
            }
            set
            {
                vbr_constraint = value ? 1 : 0;
            }
        }

        /// <summary>
        /// Gets or sets a hint to the encoder for what type of audio is being processed, voice or music 
        /// </summary>
        public OpusSignal SignalType
        {
            get
            {
                return signal_type;
            }
            set
            {
                signal_type = value;
            }
        }

        /// <summary>
        /// Gets the number of samples of audio that are being stored in a buffer and are therefore contributing to latency.
        /// </summary>
        public int Lookahead
        {
            get
            {
                int returnVal = Fs / 400;
                if (application != OpusApplication.OPUS_APPLICATION_RESTRICTED_LOWDELAY)
                    returnVal += delay_compensation;

                return returnVal;
            }
        }

        /// <summary>
        /// Gets the encoder's input sample rate. This is fixed for the lifetime of the encoder.
        /// </summary>
        public int SampleRate
        {
            get
            {
                return Fs;
            }
        }

        /// <summary>
        /// Gets the number of channels that this encoder expects in its input. Always constant for the lifetime of the decoder.
        /// </summary>
        public int NumChannels
        {
            get
            {
                return channels;
            }
        }

        /// <summary>
        /// Returns the final range of the entropy coder
        /// </summary>
        public uint FinalRange
        {
            get
            {
                return rangeFinal;
            }
        }

        /// <summary>
        /// Gets or sets the bit resolution of the input audio signal. Though the encoder always uses 16-bit internally, this can help
        /// it make better decisions about bandwidth and cutoff values
        /// </summary>
        public int LSBDepth
        {
            get
            {
                return lsb_depth;
            }
            set
            {
                if (value < 8 || value > 24)
                {
                    throw new ArgumentException("LSB depth must be between 8 and 24");
                }

                lsb_depth = value;
            }
        }

        /// <summary>
        /// Gets or sets a fixed length for each encoded frame. Typically, the encoder just chooses a frame duration based on the input length
        /// and the current public mode. This can be used to enforce an exact length if it is required by your application (e.g. monotonous transmission)
        /// </summary>
        public OpusFramesize ExpertFrameDuration
        {
            get
            {
                return variable_duration;

            }
            set
            {
                variable_duration = value;
                Celt_Encoder.SetExpertFrameDuration(value);
            }
        }

        /// <summary>
        /// Gets or sets a user-forced mode for the encoder. There are three modes, SILK, HYBRID, and CELT. Silk can only encode below 40Kbit/s and is best suited
        /// for speech. Silk also has modes such as FEC which may be desirable. Celt sounds better at higher bandwidth and is comparable to AAC. It also performs somewhat faster.
        /// Hybrid is used to create a smooth transition between the two modes. Note that this value may not always be honored due to other factors such
        /// as frame size and bitrate.
        /// </summary>
        public OpusMode ForceMode
        {
            get
            {
                return user_forced_mode;
            }
            set
            {
                user_forced_mode = value;
            }
        }

        /// <summary>
        /// Gets or sets a value indicating that this stream is a low-frequency channel. This is used when encoding 5.1 surround audio.
        /// </summary>
        public bool IsLFE
        {
            get
            {
                return lfe != 0;
            }
            set
            {
                lfe = value ? 1 : 0;
                Celt_Encoder.SetLFE(value ? 1 : 0);
            }
        }

        /// <summary>
        /// Gets or sets a flag to disable prediction, which does... something with the SILK codec
        /// </summary>
        public bool PredictionDisabled
        {
            get
            {
                return silk_mode.reducedDependency != 0;
            }
            set
            {
                silk_mode.reducedDependency = value ? 1 : 0;
            }
        }

        /// <summary>
        /// Gets or sets a value indicating whether neural net analysis functions should be enabled, increasing encode quality
        /// at the expense of speed.
        /// </summary>
        public bool EnableAnalysis
        {
            get
            {
                return analysis.enabled;
            }
            set
            {
                if (!value && _vqLevel.HasValue)
                {
                    throw new ArgumentException("You cannot disable analysis while also specifying a ConstantQuality parameter");
                }
                if (value && Fs != 48000)
                {
                    throw new ArgumentException("EnableAnalysis only works if the encoder is in 48000Khz mode");
                }

                analysis.enabled = value;
            }
        }

        /// <summary>
        /// EXPERIMENTAL!!! Gets or sets the constant quality encoding parameter. This is a new feature intended to approximate
        /// "Constant Quality VBR" that other codecs such as MP3Lame provide, to let you encode mixed speech and music
        /// (such as a podcast) in the same Opus stream without changing encoder params.
        /// The quality is range from 0 (lowest) to 10 (highest). A setting of "null" means to use the regular Opus bitrate modes.
        /// </summary>
        public int? ConstantQuality
        {
            get
            {
                return _vqLevel;
            }

            set
            {
                if (value.HasValue && (value.Value < 0 || value.Value > 10))
                {
                    throw new ArgumentException("Constant quality VBR level must be either null (disabled) or between 0 and 10, inclusive.");
                }
                if (value.HasValue && Fs != 48000)
                {
                    throw new ArgumentException("ConstantQuality only works if the encoder is in 48000Khz mode");
                }

                EnableAnalysis = true;
                _vqLevel = value;
            }
        }

        /// <summary>
        /// EXPERIMENTAL. Returns the probability that the current signal is music, according to the built-in analysis.
        /// Only meaningful if EnableAnalysis is true and quality is above 7 or so
        /// </summary>
        public float MusicProbability
        {
            get
            {
                return analysis.music_prob;
            }
        }

        internal void SetEnergyMask(int[] value)
        {
            energy_masking = value;
            Celt_Encoder.SetEnergyMask(value);
        }

        internal CeltMode GetCeltMode()
        {
            return Celt_Encoder.GetMode();
        }

        /// <inheritdoc/>
        public string GetVersionString()
        {
            return CodecHelpers.GetVersionString();
        }

        public void Dispose() { }

        #endregion
    }
}

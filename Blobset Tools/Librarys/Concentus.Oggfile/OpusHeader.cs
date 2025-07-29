#pragma warning disable 0169

namespace Concentus.Oggfile
{
    internal class OpusHeader
    {
        private readonly byte version;
        private readonly byte channel_count;
        private readonly ushort pre_skip;
        private readonly uint input_sample_rate;
        private readonly short output_gain;
        private readonly byte mapping_family;
        private readonly byte stream_count;
        private readonly byte coupled_count;
    }
}

#pragma warning restore 0169
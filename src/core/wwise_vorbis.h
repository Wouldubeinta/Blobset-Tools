#ifndef WWISE_VORBIS_H
#define WWISE_VORBIS_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QByteArray>
#include <QString>

// Encodes audio as Wwise Vorbis WEM data, the way Rugby League 26's own sounds are (Wwise 2023.1, bank v150):
//
//   libvorbis (1.3.7) at quality 0.4 with an 18 kHz lowpass, 44100 Hz, mono or stereo. That is exactly the game's
//   setup: its commentary (mono) and stereo sounds decode with the same codebooks, floors, residues and modes.
//   The standard Vorbis setup is then stored the Wwise way (codebooks as indexes into Wwise's aoTuV 6.03 codebook
//   library, the fields Wwise always has left out), and each audio packet without its packet type / window flag
//   bits, with a u16 size before it.
//
//   "fmt " (0x42 bytes): 0xFFFF, channels, 44100, average bytes a second, 0, 0, cbSize 0x30, 0, channel layout,
//   then the samples, the first audio packet's and the data's end offsets (after the seek table), 0, the samples
//   the last packet decodes past the end (twice), the seek table size, the first audio packet's offset, the biggest
//   packet, the decoder's memory sizes (the game's for its setup), the setup's FNV-1 32 hash and the block sizes
//   (2^8 / 2^11).
//   "data": the seek table (u16 samples, u16 bytes since the last entry, one each 16384 samples or more), the setup
//   (u16 size + the setup), then the packets.
//
// All 53 game WEMs checked follow these rules exactly.
namespace WwiseVorbis {

struct Stream {
    QByteArray fmt;     // The "fmt " chunk's body.
    QByteArray data;    // The "data" chunk's body.
    quint32 samples = 0;
    int channels = 0;
};

// `pcm`: interleaved 16 bit samples, `channels` of them a frame, at `sampleRate`; encoded as `targetChannels` (1 or 2,
// downmixed / doubled) at 44100 Hz (resampled). `layout`: the "fmt " channel layout to store.
bool encode(const QByteArray& pcm, int channels, quint32 sampleRate, int targetChannels, quint32 layout, Stream* out,
            QString* error);

} // namespace WwiseVorbis

#endif // WWISE_VORBIS_H

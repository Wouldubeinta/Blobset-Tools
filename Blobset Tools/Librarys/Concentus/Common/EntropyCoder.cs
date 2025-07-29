/* Copyright (c) 2001-2011 Timothy B. Terriberry
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

namespace Concentus.Common
{
    using Concentus.Common.CPlusPlus;
    using System;

    /*A range decoder.
  This is an entropy decoder based upon \cite{Mar79}, which is itself a
   rediscovery of the FIFO arithmetic code introduced by \cite{Pas76}.
  It is very similar to arithmetic encoding, except that encoding is done with
   digits in any base, instead of with bits, and so it is faster when using
   larger bases (i.e.: a byte).
  The author claims an average waste of $\frac{1}{2}\log_b(2b)$ bits, where $b$
   is the base, longer than the theoretical optimum, but to my knowledge there
   is no published justification for this claim.
  This only seems true when using near-infinite precision arithmetic so that
   the process is carried out with no rounding errors.

  An excellent description of implementation details is available at
   http://www.arturocampos.com/ac_range.html
  A recent work \cite{MNW98} which proposes several changes to arithmetic
   encoding for efficiency actually re-discovers many of the principles
   behind range encoding, and presents a good theoretical analysis of them.

  End of stream is handled by writing out the smallest number of bits that
   ensures that the stream will be correctly decoded regardless of the value of
   any subsequent bits.
  ec_tell() can be used to determine how many bits were needed to decode
   all the symbols thus far; other data can be packed in the remaining bits of
   the input buffer.
  @PHDTHESIS{Pas76,
    author="Richard Clark Pasco",
    title="Source coding algorithms for fast data compression",
    school="Dept. of Electrical Engineering, Stanford University",
    address="Stanford, CA",
    month=May,
    year=1976
  }
  @INPROCEEDINGS{Mar79,
   author="Martin, G.N.N.",
   title="Range encoding: an algorithm for removing redundancy from a digitised
    message",
   booktitle="Video & Data Recording Conference",
   year=1979,
   address="Southampton",
   month=Jul
  }
  @ARTICLE{MNW98,
   author="Alistair Moffat and Radford Neal and Ian H. Witten",
   title="Arithmetic Coding Revisited",
   journal="{ACM} Transactions on Information Systems",
   year=1998,
   volume=16,
   number=3,
   pages="256--294",
   month=Jul,
   URL="http://www.stanford.edu/class/ee398a/handouts/papers/Moffat98ArithmCoding.pdf"
  }*/
    internal class EntropyCoder
    {
        private const int EC_WINDOW_SIZE = (sizeof(uint) * 8);

        ///*The number of bits to use for the range-coded part of uint integers.*/
        private const int EC_UINT_BITS = 8;

        ///*The resolution of fractional-precision bit usage measurements, i.e.,
        //   3 => 1/8th bits.*/
        internal const int BITRES = 3;

        /*The number of bits to output at a time.*/
        private const int EC_SYM_BITS = (8);

        /*The total number of bits in each of the state registers.*/
        private const int EC_CODE_BITS = (32);

        /*The maximum symbol value.*/
        private const uint EC_SYM_MAX = ((1U << EC_SYM_BITS) - 1);

        /*Bits to shift by to move a symbol into the high-order position.*/
        private const uint EC_CODE_SHIFT = (EC_CODE_BITS - EC_SYM_BITS - 1);

        /*Carry bit of the high-order range symbol.*/
        private const uint EC_CODE_TOP = ((1U) << (EC_CODE_BITS - 1));

        /*Low-order bit of the high-order range symbol.*/
        private const uint EC_CODE_BOT = (EC_CODE_TOP >> EC_SYM_BITS);

        /*The number of bits available for the last, partial symbol in the code field.*/
        private const int EC_CODE_EXTRA = ((EC_CODE_BITS - 2) % EC_SYM_BITS + 1);

        //////////////// Coder State //////////////////// 

        /*The size of the buffer.*/
        internal uint storage;

        /*The offset at which the last byte containing raw bits was read/written.*/
        internal uint end_offs;

        /*Bits that will be read from/written at the end.*/
        internal uint end_window;

        /*Number of valid bits in end_window.*/
        internal int nend_bits;

        /*The total number of whole bits read/written.
          This does not include partial bits currently in the range coder.*/
        internal int nbits_total;

        /*The offset at which the next range coder byte will be read/written.*/
        internal uint offs;

        /*The number of values in the current range.*/
        internal uint rng;

        /*In the decoder: the difference between the top of the current range and
           the input value, minus one.
          In the encoder: the low end of the current range.*/
        internal uint val;

        /*In the decoder: the saved normalization factor from ec_decode().
          In the encoder: the number of oustanding carry propagating symbols.*/
        internal uint ext;

        /*A buffered input/output symbol, awaiting carry propagation.*/
        internal int rem;

        /*Nonzero if an error occurred.*/
        internal int error;

        internal EntropyCoder()
        {
            Reset();
        }

        internal void Reset()
        {
            storage = 0;
            end_offs = 0;
            end_window = 0;
            nend_bits = 0;
            offs = 0;
            rng = 0;
            val = 0;
            ext = 0;
            rem = 0;
            error = 0;
        }

        internal void Assign(EntropyCoder other)
        {
            storage = other.storage;
            end_offs = other.end_offs;
            end_window = other.end_window;
            nend_bits = other.nend_bits;
            nbits_total = other.nbits_total;
            offs = other.offs;
            rng = other.rng;
            val = other.val;
            ext = other.ext;
            rem = other.rem;
            error = other.error;
        }

        internal int read_byte(ReadOnlySpan<byte> buf)
        {
            return offs < storage ? buf[(int)(offs++)] : 0;
        }

        internal int read_byte_from_end(ReadOnlySpan<byte> buf)
        {
            return end_offs < storage ?
             buf[(int)((storage - ++(end_offs)))] : 0;
        }

        internal int write_byte(Span<byte> buf, uint _value)
        {
            if (offs + end_offs >= storage)
            {
                return -1;
            }
            buf[(int)(offs++)] = (byte)_value;
            return 0;
        }

        internal int write_byte_at_end(Span<byte> buf, uint _value)
        {
            if (offs + end_offs >= storage)
            {
                return -1;
            }

            buf[(int)((storage - ++(end_offs)))] = (byte)_value;
            return 0;
        }

        /// <summary>
        /// Normalizes the contents of val and rng so that rng lies entirely in the high-order symbol.
        /// </summary>
        internal void dec_normalize(ReadOnlySpan<byte> buf)
        {
            /*If the range is too small, rescale it and input some bits.*/
            while (rng <= EC_CODE_BOT)
            {
                int sym;
                nbits_total += EC_SYM_BITS;
                rng <<= EC_SYM_BITS;

                /*Use up the remaining bits from our last symbol.*/
                sym = rem;

                /*Read the next value from the input.*/
                rem = read_byte(buf);

                /*Take the rest of the bits we need from this new symbol.*/
                sym = (sym << EC_SYM_BITS | rem) >> (EC_SYM_BITS - EC_CODE_EXTRA);

                /*And subtract them from val, capped to be less than EC_CODE_TOP.*/
                val = (uint)((val << EC_SYM_BITS) + (EC_SYM_MAX & ~sym)) & (EC_CODE_TOP - 1);
            }
        }

        internal void dec_init(ReadOnlySpan<byte> buf, uint _storage)
        {
            storage = _storage;
            end_offs = 0;
            end_window = 0;
            nend_bits = 0;
            /*This is the offset from which ec_tell() will subtract partial bits.
              The final value after the ec_dec_normalize() call will be the same as in
               the encoder, but we have to compensate for the bits that are added there.*/
            nbits_total = EC_CODE_BITS + 1
            - ((EC_CODE_BITS - EC_CODE_EXTRA) / EC_SYM_BITS) * EC_SYM_BITS;
            offs = 0;
            rng = 1U << EC_CODE_EXTRA;
            rem = read_byte(buf);
            val = rng - 1 - (uint)(rem >> (EC_SYM_BITS - EC_CODE_EXTRA));
            error = 0;
            /*Normalize the interval.*/
            dec_normalize(buf);
        }

        internal uint decode(uint _ft)
        {
            uint s;
            ext = rng / _ft;
            s = val / ext;
            return _ft - Inlines.EC_MINI(s + 1, _ft);
        }

        internal uint decode_bin(uint _bits)
        {
            uint s;
            ext = rng >> (int)_bits;
            s = val / ext;
            return (1U << (int)_bits) - Inlines.EC_MINI(s + 1U, 1U << (int)_bits);
        }

        internal void dec_update(ReadOnlySpan<byte> buf, uint _fl, uint _fh, uint _ft)
        {
            uint s;
            s = ext * (_ft - _fh);
            val -= s;
            rng = _fl > 0 ? ext * (_fh - _fl) : rng - s;
            dec_normalize(buf);
        }

        /// <summary>
        /// The probability of having a "one" is 1/(1&lt;&lt;_logp).
        /// </summary>
        /// <param name="_logp"></param>
        /// <returns></returns>
        internal int dec_bit_logp(ReadOnlySpan<byte> buf, uint _logp)
        {
            uint r;
            uint d;
            uint s;
            int ret;
            r = rng;
            d = val;
            s = r >> (int)_logp;
            ret = d < s ? 1 : 0;
            if (ret == 0) val = d - s;
            rng = ret != 0 ? s : r - s;
            dec_normalize(buf);
            return ret;
        }

        internal int dec_icdf(ReadOnlySpan<byte> buf, byte[] _icdf, uint _ftb)
        {
            uint r;
            uint d;
            uint s;
            uint t;
            int ret;
            s = rng;
            d = val;
            r = s >> (int)_ftb;
            ret = -1;
            do
            {
                t = s;
                s = r * _icdf[++ret];
            }
            while (d < s);
            val = d - s;
            rng = t - s;
            dec_normalize(buf);
            return ret;
        }

        internal int dec_icdf(ReadOnlySpan<byte> buf, byte[] _icdf, int _icdf_offset, uint _ftb)
        {
            uint r;
            uint d;
            uint s;
            uint t;
            int ret;
            s = rng;
            d = val;
            r = s >> (int)_ftb;
            ret = _icdf_offset - 1;
            do
            {
                t = s;
                s = r * _icdf[++ret];
            }
            while (d < s);
            val = d - s;
            rng = t - s;
            dec_normalize(buf);
            return ret - _icdf_offset;
        }

        internal uint dec_uint(ReadOnlySpan<byte> buf, uint _ft)
        {
            uint ft;
            uint s;
            int ftb;
            /*In order to optimize EC_ILOG(), it is undefined for the value 0.*/
            Inlines.OpusAssert(_ft > 1);
            _ft--;
            ftb = Inlines.EC_ILOG(_ft);
            if (ftb > EC_UINT_BITS)
            {
                uint t;
                ftb -= EC_UINT_BITS;
                ft = (_ft >> ftb) + 1;
                s = decode(ft);
                dec_update(buf, s, s + 1, ft);
                t = s << ftb | dec_bits(buf, (uint)ftb);
                if (t <= _ft) return t;
                error = 1;
                return _ft;
            }
            else
            {
                _ft++;
                s = decode(_ft);
                dec_update(buf, s, s + 1, _ft);
                return s;
            }
        }

        internal uint dec_bits(ReadOnlySpan<byte> buf, uint _bits)
        {
            uint window;
            int available;
            uint ret;
            window = end_window;
            available = nend_bits;
            if ((uint)available < _bits)
            {
                do
                {
                    window |= (uint)read_byte_from_end(buf) << available;
                    available += EC_SYM_BITS;
                }
                while (available <= EC_WINDOW_SIZE - EC_SYM_BITS);
            }
            ret = window & (((uint)1 << (int)_bits) - 1U);
            window = window >> (int)_bits;
            available = available - (int)_bits;
            end_window = window;
            nend_bits = available;
            nbits_total = nbits_total + (int)_bits;
            return ret;
        }

        /// <summary>
        /// Outputs a symbol, with a carry bit.
        /// If there is a potential to propagate a carry over several symbols, they are
        /// buffered until it can be determined whether or not an actual carry will
        /// occur.
        /// If the counter for the buffered symbols overflows, then the stream becomes
        /// undecodable.
        /// This gives a theoretical limit of a few billion symbols in a single packet on
        /// 32-bit systems.
        /// The alternative is to truncate the range in order to force a carry, but
        /// requires similar carry tracking in the decoder, needlessly slowing it down.
        /// </summary>
        /// <param name="_c"></param>
        internal void enc_carry_out(Span<byte> buf, int _c)
        {
            if (_c != EC_SYM_MAX)
            {
                /*No further carry propagation possible, flush buffer.*/
                int carry;
                carry = _c >> EC_SYM_BITS;

                /*Don't output a byte on the first write.
                  This compare should be taken care of by branch-prediction thereafter.*/
                if (rem >= 0)
                {
                    error |= write_byte(buf, (uint)(rem + carry));
                }

                if (ext > 0)
                {
                    uint sym;
                    sym = (EC_SYM_MAX + (uint)carry) & EC_SYM_MAX;
                    do error |= write_byte(buf, sym);
                    while (--(ext) > 0);
                }

                rem = (int)((uint)_c & EC_SYM_MAX);
            }
            else
            {
                ext++;
            }
        }

        internal void enc_normalize(Span<byte> buf)
        {
            /*If the range is too small, output some bits and rescale it.*/
            while (rng <= EC_CODE_BOT)
            {
                enc_carry_out(buf, (int)(val >> (int)EC_CODE_SHIFT));
                /*Move the next-to-high-order symbol into the high-order position.*/
                val = (val << EC_SYM_BITS) & (EC_CODE_TOP - 1);
                rng = rng << EC_SYM_BITS;
                nbits_total += EC_SYM_BITS;
            }
        }

        internal void enc_init(uint _size)
        {
            end_offs = 0;
            end_window = 0;
            nend_bits = 0;
            /*This is the offset from which ec_tell() will subtract partial bits.*/
            nbits_total = EC_CODE_BITS + 1;
            offs = 0;
            rng = EC_CODE_TOP;
            rem = -1;
            val = 0;
            ext = 0;
            storage = _size;
            error = 0;
        }

        internal void encode(Span<byte> buf, uint _fl, uint _fh, uint _ft)
        {
            uint r;
            r = rng / _ft;
            if (_fl > 0)
            {
                val += rng - (r * (_ft - _fl));
                rng = (r * (_fh - _fl));
            }
            else
            {
                rng -= (r * (_ft - _fh));
            }

            enc_normalize(buf);
        }

        internal void encode_bin(Span<byte> buf, uint _fl, uint _fh, uint _bits)
        {
            uint r;
            r = rng >> (int)_bits;
            if (_fl > 0)
            {
                val += rng - (r * ((1U << (int)_bits) - _fl));
                rng = (r * (_fh - _fl));
            }
            else rng -= (r * ((1U << (int)_bits) - _fh));
            enc_normalize(buf);
        }

        /*The probability of having a "one" is 1/(1<<_logp).*/
        internal void enc_bit_logp(Span<byte> buf, int _val, uint _logp)
        {
            uint r;
            uint s;
            uint l;
            r = rng;
            l = val;
            s = r >> (int)_logp;
            r -= s;
            if (_val != 0)
            {
                val = l + r;
            }

            rng = _val != 0 ? s : r;
            enc_normalize(buf);
        }

        internal void enc_icdf(Span<byte> buf, int _s, byte[] _icdf, uint _ftb)
        {
            uint r;
            r = rng >> (int)_ftb;
            if (_s > 0)
            {
                val += rng - (r * _icdf[_s - 1]);
                rng = (r * (uint)(_icdf[_s - 1] - _icdf[_s]));
            }
            else
            {
                rng -= (r * _icdf[_s]);
            }
            enc_normalize(buf);
        }

        internal void enc_icdf(Span<byte> buf, int _s, byte[] _icdf, int icdf_ptr, uint _ftb)
        {
            uint r;
            r = rng >> (int)_ftb;
            if (_s > 0)
            {
                val += rng - (r * _icdf[icdf_ptr + _s - 1]);
                rng = (r * (uint)(_icdf[icdf_ptr + _s - 1] - _icdf[icdf_ptr + _s]));
            }
            else
            {
                rng -= (r * _icdf[icdf_ptr + _s]);
            }
            enc_normalize(buf);
        }

        internal void enc_uint(Span<byte> buf, uint _fl, uint _ft)
        {
            uint ft;
            uint fl;
            int ftb;
            /*In order to optimize EC_ILOG(), it is undefined for the value 0.*/
            Inlines.OpusAssert(_ft > 1);
            _ft--;
            ftb = Inlines.EC_ILOG(_ft);
            if (ftb > EC_UINT_BITS)
            {
                ftb -= EC_UINT_BITS;
                ft = (_ft >> ftb) + 1;
                fl = _fl >> ftb;
                encode(buf, fl, fl + 1, ft);
                enc_bits(buf, _fl & (((uint)1 << ftb) - 1U), (uint)ftb);
            }
            else encode(buf, _fl, _fl + 1, _ft + 1);
        }

        internal void enc_bits(Span<byte> buf, uint _fl, uint _bits)
        {
            uint window;
            int used;
            window = end_window;
            used = nend_bits;
            Inlines.OpusAssert(_bits > 0);

            if (used + _bits > EC_WINDOW_SIZE)
            {
                do
                {
                    error |= write_byte_at_end(buf, window & EC_SYM_MAX);
                    window >>= EC_SYM_BITS;
                    used -= EC_SYM_BITS;
                }
                while (used >= EC_SYM_BITS);
            }

            window |= _fl << used;
            used += (int)_bits;
            end_window = window;
            nend_bits = used;
            nbits_total += (int)_bits;
        }

        internal void enc_patch_initial_bits(Span<byte> buf, uint _val, uint _nbits)
        {
            int shift;
            uint mask;
            Inlines.OpusAssert(_nbits <= EC_SYM_BITS);
            shift = EC_SYM_BITS - (int)_nbits;
            mask = ((1U << (int)_nbits) - 1) << shift;

            if (offs > 0)
            {
                /*The first byte has been finalized.*/
                buf[0] = (byte)((buf[0] & ~mask) | _val << shift);
            }
            else if (rem >= 0)
            {
                /*The first byte is still awaiting carry propagation.*/
                rem = (int)(((uint)rem & ~mask) | _val) << shift;
            }
            else if (rng <= (EC_CODE_TOP >> (int)_nbits))
            {
                /*The renormalization loop has never been run.*/
                val = (val & ~(mask << (int)EC_CODE_SHIFT)) |
                 _val << (int)(EC_CODE_SHIFT + shift);
            }
            else
            {
                /*The encoder hasn't even encoded _nbits of data yet.*/
                error = -1;
            }
        }

        internal void enc_shrink(Span<byte> buf, uint _size)
        {
            Inlines.OpusAssert(offs + end_offs <= _size);
            //(memmove(buf + _size - this.end_offs, buf + this.storage - this.end_offs, this.end_offs * sizeof(*(dst))))
            Arrays.MemMoveByte(buf, (int)_size - (int)end_offs, (int)storage - (int)end_offs, (int)end_offs);
            storage = _size;
        }

        internal uint range_bytes()
        {
            return offs;
        }

        internal int get_error()
        {
            return error;
        }

        /// <summary>
        /// Returns the number of bits "used" by the encoded or decoded symbols so far.
        /// This same number can be computed in either the encoder or the decoder, and is
        /// suitable for making coding decisions.
        /// This will always be slightly larger than the exact value (e.g., all
        /// rounding error is in the positive direction).
        /// </summary>
        /// <returns>The number of bits.</returns>
        internal int tell()
        {
            int returnVal = nbits_total - Inlines.EC_ILOG(rng);
            return returnVal;
        }

        private static readonly uint[] correction = { 35733, 38967, 42495, 46340, 50535, 55109, 60097, 65535 };

        /// <summary>
        /// This is a faster version of ec_tell_frac() that takes advantage
        /// of the low(1/8 bit) resolution to use just a linear function
        /// followed by a lookup to determine the exact transition thresholds.
        /// </summary>
        /// <returns></returns>
        internal uint tell_frac()
        {
            int nbits;
            int r;
            int l;
            uint b;
            nbits = nbits_total << EntropyCoder.BITRES;
            l = Inlines.EC_ILOG(rng);
            r = (int)(rng >> (l - 16));
            b = (uint)((r >> 12) - 8);
            b += (r > correction[b] ? 1u : 0);
            l = (int)((l << 3) + b);
            return (uint)(nbits - l);
        }

        internal void enc_done(Span<byte> buf)
        {
            uint window;
            int used;
            uint msk;
            uint end;
            int l;
            /*We output the minimum number of bits that ensures that the symbols encoded
               thus far will be decoded correctly regardless of the bits that follow.*/
            l = EC_CODE_BITS - Inlines.EC_ILOG(rng);
            msk = (EC_CODE_TOP - 1) >> l;
            end = (val + msk) & ~msk;

            if ((end | msk) >= val + rng)
            {
                l++;
                msk >>= 1;
                end = (val + msk) & ~msk;
            }

            while (l > 0)
            {
                enc_carry_out(buf, (int)(end >> (int)EC_CODE_SHIFT));
                end = (end << EC_SYM_BITS) & (EC_CODE_TOP - 1);
                l -= EC_SYM_BITS;
            }

            /*If we have a buffered byte flush it into the output buffer.*/
            if (rem >= 0 || ext > 0)
            {
                enc_carry_out(buf, 0);
            }

            /*If we have buffered extra bits, flush them as well.*/
            window = end_window;
            used = nend_bits;

            while (used >= EC_SYM_BITS)
            {
                error |= write_byte_at_end(buf, window & EC_SYM_MAX);
                window >>= EC_SYM_BITS;
                used -= EC_SYM_BITS;
            }

            /*Clear any excess space and add any remaining extra bits to the last byte.*/
            if (error == 0)
            {
                Arrays.MemSetWithOffset<byte>(buf, 0, (int)offs, (int)storage - (int)offs - (int)end_offs);
                if (used > 0)
                {
                    /*If there's no range coder data at all, give up.*/
                    if (end_offs >= storage)
                    {
                        error = -1;
                    }
                    else
                    {
                        l = -l;
                        /*If we've busted, don't add too many extra bits to the last byte; it
                           would corrupt the range coder data, and that's more important.*/
                        if (offs + end_offs >= storage && l < used)
                        {
                            window = window & ((1U << l) - 1);
                            error = -1;
                        }

                        buf[(int)(storage - end_offs - 1)] |= (byte)window;
                    }
                }
            }
        }
    }
}

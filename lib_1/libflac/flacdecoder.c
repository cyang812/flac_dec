// -*- tab-width: 4 -*-
// FLAC (Free Lossless Audio Codec) decoder
//
// Copyright (c) 2003, Alex Beregszaszi
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include <inttypes.h>

#include "bitstreamf.h"
#include "flacdecoder.h"
#include "subframe_decoders.h"
#include "crc8.h"
#define ENABLE_CRC_CHECK

const int sample_rate_table[] =
{ 0,
  88200, 176400, 192000,
  8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000,
  0, 0, 0, 0 };

const int sample_size_table[] =
{ 0, 8, 12, 0, 16, 20, 24, 0 };

const int blocksize_table[] =
{
     0,    192, 576<<0, 576<<1, 576<<2, 576<<3,      0,      0,
256<<0, 256<<1, 256<<2, 256<<3, 256<<4, 256<<5, 256<<6, 256<<7
};

int decode_frame(FLACContext *s, int32_t* decoded);
//static int decode_frame(FLACContext *s);
int decode_subframe(FLACContext *s, int channel, int32_t* decoded);
int64_t get_utf8(GetBitContext *gb);

int flac_decode_frame(FLACContext *s, int32_t* decoded, uint8_t *buf, int buf_size)
{
    int tmp;
    int result;

    init_get_bits(&s->gb, buf, buf_size*8);

    tmp = get_bits(&s->gb, 16);
    if ((tmp & 0xFFFE) != 0xFFF8)
	{
        return -41;
    }

	result = decode_frame(s, decoded);
    if(result < 0)
	{
       s->bitstream_size = 0;
       s->bitstream_index = 0;
       return result;
    }

    s->framesize = (get_bits_count(&s->gb)+7)>>3;

    return 0;
}




int decode_frame(FLACContext *s, int32_t* decoded)
{
    int blocksize_code, sample_rate_code, sample_size_code, assignment;
    int decorrelation, bps, blocksize, samplerate;
    int res;
    int i, mid, side;
	int32_t* output;
#ifdef ENABLE_CRC_CHECK
	int crc8;
#endif

    blocksize_code = get_bits(&s->gb, 4);
    sample_rate_code = get_bits(&s->gb, 4);

    assignment = get_bits(&s->gb, 4); /* channel assignment */
    if (assignment < 8 && s->channels == assignment+1)
    {
	    decorrelation = INDEPENDENT;
	}
    else if (assignment >=8 && assignment < 11 && s->channels == 2)
    {
	    decorrelation = LEFT_SIDE + assignment - 8;
	}
    else
    {
        return -13;
    }

    sample_size_code = get_bits(&s->gb, 3);
    if(sample_size_code == 0)
    {
	    bps = s->bps;
	}
    else if((sample_size_code != 3) && (sample_size_code != 7))
	{
        bps = sample_size_table[sample_size_code];
	}
    else
    {
        return -14;
    }

    if(get_bits1(&s->gb))
    {
        return -15;
    }

    // Get the samplenumber of the first sample in this block
    s->samplenumber = get_utf8(&s->gb);

    /* samplenumber actually contains the frame number for streams
       with a constant block size - so we multiply by blocksize to
       get the actual sample number */
    if (s->min_blocksize == s->max_blocksize)
	{
        s->samplenumber *= s->min_blocksize;
    }

	switch(blocksize_code)
	{
		case 0:
	        blocksize = s->min_blocksize;
			break;

		case 6:
	        blocksize = get_bits(&s->gb, 8)+1;
			break;

		case 7:
	        blocksize = get_bits(&s->gb, 16)+1;
			break;

		default:
	        blocksize = blocksize_table[blocksize_code];
	}

    if(blocksize > s->max_blocksize)
	{
        return -16;
    }

    if (sample_rate_code == 0)
	{
        samplerate= s->samplerate;
    }
	else if (sample_rate_code < 12)
        samplerate = sample_rate_table[sample_rate_code];
    else if (sample_rate_code == 12)
        samplerate = get_bits(&s->gb, 8) * 1000;
    else if (sample_rate_code == 13)
        samplerate = get_bits(&s->gb, 16);
    else if (sample_rate_code == 14)
        samplerate = get_bits(&s->gb, 16) * 10;
    else
	{
        return -17;
    }

    skip_bits(&s->gb, 8);

	// for speed, let's disable this...
#ifdef ENABLE_CRC_CHECK
	crc8 = get_crc8(s->gb.buffer, get_bits_count(&s->gb) / 8);
    if(crc8)
	{
        return -18;
    }
#endif

    s->blocksize    = blocksize;
    s->samplerate   = samplerate;
    s->bps          = bps;
    //s->decorrelation= decorrelation;
    s->decorrelation= (decorrelation_type)decorrelation;

    // decode subframes
    if ((res = decode_subframe(s, 0, decoded)) < 0)
	{
        return res-100;
	}

    if (s->channels == 2)
	{
    	if ((res = decode_subframe(s, 1, decoded + 1)) < 0)
          	return res-200;
    }


    switch(s->decorrelation)
    {
        case INDEPENDENT:
            break;

        case LEFT_SIDE:
			output = decoded;
			i = s->blocksize;
			while(i--)
			{
				output[1] = output[0] - output[1];
				output += s->channels;
			}

            break;

        case RIGHT_SIDE:
			output = decoded;
			i = s->blocksize;
			while(i--)
			{
				output[0] = output[0] + output[1];
				output += s->channels;
			}
            break;

        case MID_SIDE:
			output = decoded;
			i = s->blocksize;
			while(i--)
			{
                mid = output[0];
                side = output[1];

                mid -= side>>1;
                output[0] = mid + side;
                output[1] = mid;
				output += s->channels;
			}
            break;
    }

    align_get_bits(&s->gb);

    // frame footer
    skip_bits(&s->gb, 16); /* data crc */

    return 0;
}



int decode_subframe(FLACContext *s, int channel, int32_t* decoded)
{
    int type, wasted = 0;
    int i, tmp;
 	int32_t* output;
    s->curr_bps = s->bps;
    if(channel == 0)
	{
        if(s->decorrelation == RIGHT_SIDE)
            s->curr_bps++;
    }
	else
	{
        if(s->decorrelation == LEFT_SIDE || s->decorrelation == MID_SIDE)
            s->curr_bps++;
    }

    if (get_bits1(&s->gb))
    {
//        rprintf("invalid subframe padding\n");
        return -9;
    }
    type = get_bits(&s->gb, 6);

    if (get_bits1(&s->gb))
    {
        wasted = 1;
        while (!get_bits1(&s->gb))
            wasted++;
        s->curr_bps -= wasted;
//        rprintf("%d wasted bits\n", wasted);
    }


    if (type == 0)
    {
        //rprintf("constant\n");

        tmp = get_sbits(&s->gb, s->curr_bps);
		output = decoded;
		i = s->blocksize;
        while(i--)
		{
            *output = tmp;
			output += s->channels;
		}
    }
    else if (type == 1)
    {
        //rprintf("verbatim\n");
		output = decoded;
		i = s->blocksize;
        while(i--)
		{
            *output = get_sbits(&s->gb, s->curr_bps);
			output += s->channels;
		}
    }
    else if ((type >= 8) && (type <= 12))
    {
        //rprintf("fixed\n");
        if (decode_subframe_fixed(s, decoded, type & ~0x8) < 0)
            return -10;
    }
    else if (type >= 32)
    {
        //rprintf("lpc\n");
        if (decode_subframe_lpc(s, decoded, (type & ~0x20)+1) < 0)
            return -11;
    }
    else
    {
//        rprintf("Unknown coding type: %d\n", type);
        return -12;
    }

    if (wasted)
    {
		output = decoded;
		i = s->blocksize;
        while(i--)
		{
            *output <<= wasted;
			output += s->channels;
		}
    }

    return 0;
}

int64_t get_utf8(GetBitContext *gb)
{
    uint64_t val;
    int ones=0, bytes;

    while(get_bits1(gb))
	{
        ones++;
	}

    if(ones == 0)
	{
		bytes = 0;
	}
    else if(ones == 1)
	{
		return -1;
	}
    else
	{
		bytes = ones - 1;
	}

    val = get_bits(gb, 7-ones);
    while(bytes--)
	{
        const int tmp = get_bits(gb, 8);

        if((tmp >> 6) != 2)
            return -2;

        val <<= 6;
        val |= tmp&0x3F;
    }
    return val;
}

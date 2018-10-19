/*
 * Common bit i/o utils
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * alternative bitstream reader & writer by Michael Niedermayer <michaelni@gmx.at>
 */

/**
 * @file bitstream.c
 * bitstream api.
 */

#include "bitstreamf.h"

/* bit input functions */

/**
 * init GetBitContext.
 * @param buffer bitstream buffer, must be FF_INPUT_BUFFER_PADDING_SIZE bytes larger then the actual read bits
 * because some optimized bitstream readers read 32 or 64 bit at once and could read over the end
 * @param bit_size the size of the buffer in bits
 */
void init_get_bits(GetBitContext *s, const uint8_t *buffer, int bit_size)
{
    int buffer_size = (bit_size+7)>>3;
    if(buffer_size < 0 || bit_size < 0) 
    {
        buffer_size = bit_size = 0;
        buffer = 0;
    }

    s->buffer = buffer;
    s->size_in_bits = bit_size;
    s->buffer_end = buffer + buffer_size;
    s->index=0;
    {
        OPEN_READER(re, s)
        UPDATE_CACHE(re, s)
        UPDATE_CACHE(re, s)
        CLOSE_READER(re, s)
    }
}

/**
 * reads 0-17 bits.
 * Note, the alt bitstream reader can read up to 25 bits, but the libmpeg2 reader can't
 */
unsigned int get_bits(GetBitContext *s, int n)
{
    register int tmp;
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    tmp= SHOW_UBITS(re, s, n);
    LAST_SKIP_BITS(re, s, n)
    CLOSE_READER(re, s)
    return tmp;
}

int get_bits_count(GetBitContext *s)
{
    return s->index;
}

int get_sbits(GetBitContext *s, int n)
{
    register int tmp;
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    tmp= SHOW_SBITS(re, s, n);
    LAST_SKIP_BITS(re, s, n)
    CLOSE_READER(re, s)
    return tmp;
}

/** 
 * reads 0-32 bits.
 */
unsigned int get_bits_long(GetBitContext *s, int n)
{
    if(n <= 17)
    {
		return get_bits(s, n);
    }
    else
    {
        int ret = get_bits(s, 16) << (n-16);
        return ret | get_bits(s, n-16);
    }
}

unsigned int get_bits1(GetBitContext *s)
{
    int index = s->index;
    uint8_t result = s->buffer[ index>>3 ];
    result <<= (index&0x07);
    result >>= 8 - 1;
    index++;
    s->index = index;

    return result;
}

/**
 * shows 0-17 bits.
 * Note, the alt bitstream reader can read up to 25 bits, but the libmpeg2 reader can't
 */
unsigned int show_bits(GetBitContext *s, int n)
{
    register int tmp;
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    tmp= SHOW_UBITS(re, s, n);
//    CLOSE_READER(re, s)
    return tmp;
}

void skip_bits(GetBitContext *s, int n)
{
 //Note gcc seems to optimize this to s->index+=n for the ALT_READER :))
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    LAST_SKIP_BITS(re, s, n)
    CLOSE_READER(re, s)
}

/**
 * I think this might be skipping bits to align the bit stream to an 8-bit (byte) boundary.
 */
void align_get_bits(GetBitContext *s)
{
    int n = (-s->index) & 7;
    if(n) 
    {
		skip_bits(s, n);
    }
}

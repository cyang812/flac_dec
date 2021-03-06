/*
 * exp golomb vlc stuff
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Alex Beregszaszi
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
 */

#ifndef GOLOMB_H
#define GOLOMB_H

/**
 * @file golomb.h
 * @brief 
 *     exp golomb vlc stuff
 * @author Michael Niedermayer <michaelni@gmx.at> and Alex Beregszaszi
 */

#include "bitstreamf.h"

extern const unsigned char ff_log2_tab[];

//int av_log2(unsigned int v);
//int get_sr_golomb_flac(GetBitContext *gb, int k, int limit, int esc_len);

static __inline int av_log2(unsigned int v)
{
    int n = 0;

    if (v & 0xFFFF0000) 
    {
        v >>= 16;
        n += 16;
    }

    if (v & 0xFF00) 
    {
        v >>= 8;
        n += 8;
    }
    n += ff_log2_tab[v];

    return n;
}

/**
 * read signed golomb rice code (flac).
 */
static __inline int get_sr_golomb_flac(GetBitContext *gb, int k, int limit, int esc_len)
{
    unsigned int buf;
    int log;
    
    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf = GET_CACHE(re, gb);

    log = av_log2(buf);
    
    if(log > 31-11)
    {
        buf >>= log - k;
        buf += (30-log) << k;
        LAST_SKIP_BITS(re, gb, 32 + k - log);
        CLOSE_READER(re, gb);
    
        return (buf >> 1) ^ -(buf & 1);;
    }
    else
    {
        int i;
        for(i = 0; SHOW_UBITS(re, gb, 1) == 0; i++)
		{
            LAST_SKIP_BITS(re, gb, 1);
            UPDATE_CACHE(re, gb);
        }
        SKIP_BITS(re, gb, 1);

        if(i < limit - 1)
		{
            if(k)
	    	{
                buf = SHOW_UBITS(re, gb, k);
                LAST_SKIP_BITS(re, gb, k);
            }
	    	else
	    	{
                buf = 0;
            }

            CLOSE_READER(re, gb);
			buf += (i << k);
            return (buf >> 1) ^ -(buf & 1);;
        }
		else if(i == limit - 1)
		{
            buf = SHOW_UBITS(re, gb, esc_len);
            LAST_SKIP_BITS(re, gb, esc_len);
            CLOSE_READER(re, gb);

    		buf++;
            return (buf >> 1) ^ -(buf & 1);;
        }
		else
		{
            return -1;
		}
    }
}


#endif


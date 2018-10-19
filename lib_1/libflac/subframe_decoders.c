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
#include <limits.h>

#include "flacdecoder.h"
#include "golomb.h"

int decode_residuals(FLACContext *s, int32_t* decoded, int pred_order);

int decode_subframe_fixed(FLACContext *s, int32_t* decoded, int pred_order)
{
    const int blocksize = s->blocksize;
    int a, b, c, d, i, n2, n3, n4;
    int32_t* output;
    
    // warm up samples
	output = decoded;
	i = pred_order;
	while(i--)
	{
		*output = get_sbits(&s->gb, s->curr_bps);
		output += s->channels;
	}

    if (decode_residuals(s, decoded, pred_order) < 0)
	{
        return -4;
	}

	output = &decoded[pred_order * s->channels];
    a  = output[-1 * s->channels];
	n2 = output[-2 * s->channels];
	n3 = output[-3 * s->channels];
	n4 = output[-4 * s->channels];
    b = a - n2;
    c = b - n2 + n3;
    d = c - n2 + 2*n3 - n4;

    switch(pred_order)
    {
        case 0:
            break;

        case 1:
			output = &decoded[pred_order * s->channels];
			i = blocksize - pred_order;
			while(i--)
			{
				a += *output;
                *output = a;
				output += s->channels;
			}
            break;

        case 2:
			output = &decoded[pred_order * s->channels];
			i = blocksize - pred_order;
			while(i--)
			{
				b += *output;
				a += b;
                *output = a;
				output += s->channels;
			}
            break;

        case 3:
			output = &decoded[pred_order * s->channels];
			i = blocksize - pred_order;
			while(i--)
			{
				c += *output;
				b += c;
				a += b;
                *output = a;
				output += s->channels;
			}
            break;

        case 4:
			output = &decoded[pred_order * s->channels];
			i = blocksize - pred_order;
			while(i--)
			{
				d += *output;
				c += d;
				b += c;
				a += b;
                *output = a;
				output += s->channels;
			}
            break;

        default:
			//LCD_Printf("illegal pred order: %d\n", pred_order);
            return -5;
    }

    return 0;
}

int decode_subframe_lpc(FLACContext *s, int32_t* decoded, int pred_order)
{
    int sum, i, j;
    int64_t wsum;
    int coeff_prec, qlevel;
    int coeffs[2048];
    int32_t* output;
	int32_t* reader;
	int* pcoeffs;

    // warm up samples
	output = decoded;
	i = pred_order;
	while(i--)
	{
		*output = get_sbits(&s->gb, s->curr_bps);
		output += s->channels;
	}
    
    coeff_prec = get_bits(&s->gb, 4) + 1;
    if (coeff_prec == 16)
    {
        //fprintf(stderr,"invalid coeff precision\n");
        return -6;
    }
    qlevel = get_sbits(&s->gb, 5);
    if (qlevel < 0) 
    {
        //fprintf(stderr,"qlevel %d not supported, maybe buggy stream\n", qlevel);
        return -7;
    }

    for (i = 0; i < pred_order; i++)
    {
        coeffs[i] = get_sbits(&s->gb, coeff_prec);
    }
    
    if (decode_residuals(s, decoded, pred_order) < 0)
        return -8;

    if ((s->bps + coeff_prec + av_log2(pred_order)) <= 32) 
	{
		output = &decoded[pred_order * s->channels];
		i = s->blocksize - pred_order;
		while(i--)
		{
            sum = 0;

			pcoeffs = coeffs;
			reader = output;
			j = pred_order;
            while(j--)
			{
				reader -= s->channels;
                sum += (*pcoeffs) * (*reader);
				pcoeffs++;
			}

            *output += sum >> qlevel;
			output += s->channels;
		}
    } 
	else 
	{
		output = &decoded[pred_order * s->channels];
		i = s->blocksize - pred_order;
		while(i--)
		{
            wsum = 0;
			pcoeffs = coeffs;
			reader = output;
			j = pred_order;
            while(j--)
			{
				reader -= s->channels;
                wsum += (int64_t)(*pcoeffs) * (int64_t)(*reader);
				pcoeffs++;
			}

			wsum >>= qlevel;
            *output += wsum;
			output += s->channels;
		}

		//LCD_Printf("w\n");
    }
    
    return 0;
}

int decode_residuals(FLACContext *s, int32_t* decoded, int pred_order)
{
    int i, tmp, partition, method_type, rice_order;
    int samples;
	int32_t* output;

    method_type = get_bits(&s->gb, 2);
    if (method_type > 1)
	{
        //LCD_Printf("illegal residual coding method %d\n", method_type);
        return -3;
    }
    
    rice_order = get_bits(&s->gb, 4);
    samples = s->blocksize >> rice_order;
	i = samples - pred_order;
	output = &decoded[pred_order * s->channels];
    for (partition = 0; partition < (1 << rice_order); partition++)
    {
        tmp = get_bits(&s->gb, method_type == 0 ? 4 : 5);
        if (tmp == (method_type == 0 ? 15 : 31))
        {
            //LCD_Printf("fixed len partition\n");
            tmp = get_bits(&s->gb, 5);
			while(i--)
			{
				*output = get_sbits(&s->gb, tmp);
				output += s->channels;
			}
			i = samples;
        }
        else
        {
			//LCD_Printf("intmax: %d\n", INT16_MAX);
			while(i--)
			{
				*output = get_sr_golomb_flac(&s->gb, tmp, INT_MAX, 0);
				output += s->channels;
			}
			i = samples;
        }
    }

    return 0;
}    

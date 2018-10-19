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

#ifndef DECODER_H
#define DECODER_H
 
#include "bitstreamf.h"

enum decorrelation_type 
{
    INDEPENDENT,
    LEFT_SIDE,
    RIGHT_SIDE,
    MID_SIDE,
};

typedef struct FLACContext 
{
    GetBitContext gb;

    int min_blocksize, max_blocksize;
    int min_framesize, max_framesize;
    int samplerate, channels;
    int blocksize/*, last_blocksize*/;
    int bps, curr_bps;
    unsigned long samplenumber;
    unsigned long totalsamples;
    enum decorrelation_type decorrelation;

    int filesize;
    int bitrate;
    int metadatalength;
    
    int bitstream_size;
    int bitstream_index;

    int sample_skip;
    int framesize;
} FLACContext;

int flac_decode_frame(FLACContext *s, int32_t* decoded, uint8_t *buf, int buf_size);

#endif

/*
* @Author: cyang
* @Date:   2018-10-19 12:02:06
* @Last Modified by:   cyang
* @Last Modified time: 2018-10-19 14:12:18
*/

#include "flacdecoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN_FILE_NAME   "2.flac"
#define OUT_FILE_NAME  "2_out.pcm"
FILE *in_file, *out_file;
int in_file_size = 0;

int eof, bytesleft;

unsigned int decoded_buf_sz;

#define MAX_BLOCKSIZE	(4680)
#define MAX_FRAMESIZE	(15*1024)

unsigned char filebuf[MAX_FRAMESIZE]; //15kB  /* The input buffer */

int32_t decoded[2*MAX_BLOCKSIZE]; //37440 B
short PCM_buf[2*MAX_BLOCKSIZE];  //18720 B  

/*buffer total: 70K */

int filesize(FILE*stream)
{
	int curpos,length;
	curpos = ftell(stream);
	fseek(stream, 0L, SEEK_END);
	length = ftell(stream);
	fseek(stream, curpos, SEEK_SET);
	return length;
}

int flac_init(FLACContext* fc)
{
    int found_streaminfo = 0;
    int endofmetadata = 0;
    int blocklength;
    unsigned int br;
    unsigned char buf[256];
    uint32_t* p;
    uint32_t seekpoint_lo, seekpoint_hi;
    uint32_t offset_lo, offset_hi;

    br = fread(buf, 1, 4, in_file);

    if(br < 4)
    {
    	printf("read file err\n");
        return 0;
    }

	if (memcmp(buf, "fLaC", 4) != 0)
	{
		printf("flac file err\n");
		return 0;
	}

	fc->metadatalength = 4;

	while (!endofmetadata)
	{
		br = fread(buf, 1, 4, in_file);
		if (br < 4)
		{
			return 0;
		}

		endofmetadata      = (buf[0] & 0x80);
		blocklength        = (buf[1] << 16) | (buf[2] << 8) | buf[3];
		fc->metadatalength += blocklength + 4;

		/* 0 is the STREAMINFO block */
    	if ((buf[0] & 0x7f) == 0)
    	{
			br = fread(buf, 1, blocklength, in_file);
			if(br == 0)
			{
				return 0;
			}

			fc->filesize = in_file_size;
			fc->min_blocksize = (buf[0] << 8) | buf[1];
			fc->max_blocksize = (buf[2] << 8) | buf[3];
			fc->min_framesize = (buf[4] << 16) | (buf[5] << 8) | buf[6];
			fc->max_framesize = (buf[7] << 16) | (buf[8] << 8) | buf[9];
			fc->samplerate = (buf[10] << 12) | (buf[11] << 4)
								     	    | ((buf[12] & 0xf0) >> 4);
			fc->channels = ((buf[12]&0x0e)>>1) + 1;
			fc->bps = (((buf[12]&0x01) << 4) | ((buf[13]&0xf0)>>4) ) + 1;

			/* totalsamples is a 36-bit field, but we assume <= 32 bits are
				 used */
			fc->totalsamples = (buf[14] << 24) | (buf[15] << 16)
											  | (buf[16] << 8) | buf[17];

			found_streaminfo = 1;
    	}
		else if((buf[0] & 0x7f) == 3)
		{
			/*3 is the SEEKTABLE block*/
      		while (blocklength >= 18)
			{
				br = fread(buf, 1, 18, in_file);
				if(br < 18)
				{
					return 0;
				}
				blocklength -= br;

				p = (uint32_t*)buf;
				seekpoint_hi = betoh32(*(p++));
				seekpoint_lo = betoh32(*(p++));
				offset_hi = betoh32(*(p++));
				offset_lo = betoh32(*(p++));
      		}

			fseek(in_file, blocklength, SEEK_CUR);
    	}
		else
		{
      		// Skip to next metadata block
			if(fseek(in_file, blocklength, SEEK_CUR) != 0)
            {
				return 0;
			}
    	}
	}

	if (found_streaminfo)
	{
		fc->bitrate = ((fc->filesize - fc->metadatalength) * 8) / ((fc->totalsamples / fc->samplerate) * 1000);
		return 1;
  	}
	else
	{
    	return 0;
	}
}

int Get_One_Block(FLACContext * fc)
{
	int consumed;
	int i;
	uint16_t k;
	unsigned int br;

	i = flac_decode_frame(fc, decoded, filebuf, bytesleft);

	if(i < 0)
	{
		eof = 1;
		bytesleft = 0;
		return 1;
	}

	decoded_buf_sz = fc->blocksize*2;
	consumed = fc->gb.index / 8;
	memcpy(filebuf, &filebuf[consumed], bytesleft - consumed);
	bytesleft = bytesleft - consumed;

	if(eof == 0)
	{
		br = fread(filebuf+bytesleft, 1, consumed, in_file);
		bytesleft += br;

		if(br == 0)
			eof = 1;
	}

	//解码器输出为32位，但是音源为44100，16bit，需要重新对齐
	for(k=0; k<decoded_buf_sz; k++)
	{
		PCM_buf[k] = decoded[k];
	}

	//for debug
	static uint32_t max_write_size = 0;
	if(max_write_size != decoded_buf_sz * sizeof(short))
	{
		max_write_size = decoded_buf_sz * sizeof(short);
		printf("write size = %d B\n", max_write_size);
	}
	fwrite(PCM_buf, sizeof(short), decoded_buf_sz, out_file);
}

int playFLAC()
{
	FLACContext fc;
	unsigned int br;
	unsigned int freq;

	in_file = fopen(IN_FILE_NAME, "rb");
	out_file = fopen(OUT_FILE_NAME,"wb");

	in_file_size = filesize(in_file);

	if(in_file == NULL || out_file == NULL)
	{
		printf("open file err\n");
		return 1;
	}

	flac_init(&fc);
	
	freq = fc.samplerate;
	printf("flac freq = %d\n", freq);
	printf("flac chanels = %d\n", fc.channels);
	printf("flac bps = %d\n", fc.bps);
	printf("flac max_blocksize = %d\n", fc.max_blocksize);
	printf("flac max_framesize = %d\n", fc.max_framesize);

	if(fc.max_blocksize > MAX_BLOCKSIZE)
	{
		goto end;
	}

	if(fc.max_framesize > MAX_FRAMESIZE)
	{
		goto end;
	}
	
	br = fread(filebuf, 1, fc.max_framesize, in_file);
	bytesleft = br;
	eof = 0;

	while(1)
	{
		Get_One_Block(&fc);

		if(eof == 1)
			break;
	}

end:
	fclose(in_file);
	fclose(out_file);
}

int main(int argc, char const *argv[])
{
	playFLAC();

	return 0;
}

/********************************************************************************
* File Name          : flacplay.c
* Author             : Sharpufo.Huang.Rong.Bin
* Date               : 01/02/2011
* Description        : STM32F103ZE FLAC AUDIO FILE PLAY PROGRAM BODY
*******************************************************************************/
/********************************************************************************
Copyright (c) 2011, SHARPUFO.Huang.Rong.Bin
保留所有原创部分之权利,只允许非商业性、非赢利性使用!
本软件代码使用到的各个其他代码模块归原作者所有，请遵循其原版权声明.
Largely based on flacplay.c from TRAXMOD Digital Audio Player
Below is the Orginal TRAXMOD CopyRight Body.
*******************************************************************************/
// -*- tab-width: 4 -*-
//TRAXMOD Digital Audio Player
// 
//Copyright (c) 2009, K9spud LLC.
// http://www.k9spud.com/traxmod/
//
// Largely based on main.c from the Rockbox FLAC test decoder program, which is
// Copyright (c) 2005, Dave Chapman
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
/*******************************************************************************/

#include "stm32f4xx.h"
#include "flacdecoder.h"
#include "flacplay.h"
#include "main.h"
#include <stdlib.h>
#include <string.h>

int eof,bytesleft;

unsigned int decoded_buf_pt;
unsigned int decoded_buf_sz;

unsigned char filebuf[MAX_FRAMESIZE]; /* The input buffer */
int32_t decoded[2*MAX_BLOCKSIZE];//解码器支持24位解码，所以用int32_t

short PCM_buf0[2*MAX_BLOCKSIZE];//PCM数据buffer
short PCM_buf1[2*MAX_BLOCKSIZE];


extern __IO uint32_t XferCplt;
extern FATFS fatfs;
extern FIL file;
extern FIL fileR;
extern DIR dir;
extern FILINFO fno;
extern USB_OTG_CORE_HANDLE USB_OTG_Core;

u8 bufferswitch = 1;

int flac_init(FIL* file, FLACContext* fc)
{
  int found_streaminfo = 0;
  int endofmetadata = 0;
  int blocklength;
  unsigned int br;
  unsigned char buf[256];
  uint32_t* p;
  uint32_t seekpoint_lo, seekpoint_hi;
  uint32_t offset_lo, offset_hi;

	f_read(file,buf,4,&br);
	
	if(br<4)
  {
    return 0;
  }

	if (memcmp(buf, "fLaC", 4) != 0) {return 0;}
	
	fc->metadatalength = 4;

	while (!endofmetadata) 
	{
		f_read(file, buf, 4,&br);
		if (br < 4){return 0;}

		endofmetadata=(buf[0]&0x80);
		blocklength = (buf[1] << 16) | (buf[2] << 8) | buf[3];
		fc->metadatalength+=blocklength+4;

    if ((buf[0] & 0x7f) == 0)       /* 0 is the STREAMINFO block */
    {
      /* FIXME: Don't trust the value of blocklength */
			f_read(file, buf, blocklength,&br); 
			if(br==0)
			{
					return 0;
			}
		
			fc->filesize = file->fsize;
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
				f_read(file, buf, 18,&br);
				if(br< 18) 
				{
					return 0;
				}
				blocklength -= br;

				p = (uint32_t*)buf;
				seekpoint_hi=betoh32(*(p++));
				seekpoint_lo=betoh32(*(p++));
				offset_hi=betoh32(*(p++));
				offset_lo=betoh32(*(p++));
      }
			
			f_lseek(file,file->fptr +blocklength);
    } 
		else 
		{
      // Skip to next metadata block
			f_lseek(file,file->fptr +blocklength);
			if(file->fptr >= file->fsize)
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

void dump_headers(FLACContext *s)
{

}


/*********************************************************************************
从文件中读取一个BLOCK并解码
解码后的数据放到pcmbuf中
为解码后的数据长度(u32长度)
get one decoded block from file.
********************************************************************************/
int Get_One_Block(FIL * file,FLACContext * fc)
{
	int consumed;
	int i;
	uint16_t k;
	FRESULT fres;
	UINT br;
	//i = flac_decode_frame(fc, decoded_buf, filebuf, bytesleft);
	
	i = flac_decode_frame(fc, decoded, filebuf, bytesleft);//该函数为24位取样设计，因此输出buf为32位，实际若输出16位音频，需要对decoded_buf做处理
	if(i < 0){eof=1;bytesleft=0;return 1;}
	decoded_buf_sz=fc->blocksize*2;
	decoded_buf_pt=0;
	consumed = fc->gb.index / 8;
	memcpy(filebuf, &filebuf[consumed], bytesleft - consumed);
	bytesleft=bytesleft-consumed;
	if(eof==0)
	{
		fres=f_read(file, filebuf+bytesleft, consumed, &br);
		bytesleft+=br;
		if(fres!=FR_OK)eof=1;
		if(br<consumed)eof=1;
	}
/***************************************/
//解码器输出为32位，但是音源为44100，16bit，需要重新对齐
	for(k=0;k<decoded_buf_sz;k++)
	{
		if(bufferswitch == 1)
		{
			PCM_buf1[k] = decoded[k]; 
			
		}
		else
		{
			PCM_buf0[k] = decoded[k];
		}
	}
/******************************************/
	if (eof == 1) return 2;
	return 0;
}




const int samplerate_table[12]={0,88200,176400,192000,8000,16000,22050,24000,32000,44100,48000,96000};
//0000 : get from STREAMINFO metadata block 
//0001 : 88.2kHz 
//0010 : 176.4kHz 
//0011 : 192kHz 
//0100 : 8kHz 
//0101 : 16kHz 
//0110 : 22.05kHz 
//0111 : 24kHz 
//1000 : 32kHz 
//1001 : 44.1kHz 
//1010 : 48kHz 
//1011 : 96kHz 

const int samplesize_table[8]={0,8,12,0,16,20,24,0};
//000 : get from STREAMINFO metadata block 
//001 : 8 bits per sample 
//010 : 12 bits per sample 
//011 : reserved 
//100 : 16 bits per sample 
//101 : 20 bits per sample 
//110 : 24 bits per sample 
//111 : reserved 



/*********************************************************************************
decoded and play a flac coded audio file by the given filename
return:
0, normally finish or exit
NOT 0, ERROR occured
********************************************************************************/
int playFLAC(char * filename)
{
	FIL file;
	UINT br;
	FLACContext fc;

	u32 freq;
	
	
	
	if(f_open(&fileR, filename, FA_READ)!=FR_OK){f_close(&fileR);return 1;}

	flac_init(&fileR, &fc);
	dump_headers(&fc);
	freq=fc.samplerate;

	WavePlayerInit(freq);//初始化CS43L22

	if(freq!=I2S_AudioFreq_48k && freq!=I2S_AudioFreq_44k && freq!=I2S_AudioFreq_22k && freq!=I2S_AudioFreq_16k && freq!=I2S_AudioFreq_8k)
	{f_close(&file);return 2;}

	if(fc.channels!=2 ){f_close(&fileR);return 2;}

	if(fc.bps!=16){f_close(&fileR);return 2;}

	if(fc.max_blocksize > MAX_BLOCKSIZE)
	{
		f_close(&fileR);
		return 2;
	}

	if(fc.max_framesize > MAX_FRAMESIZE)
	{
		f_close(&fileR);
		return 2;
	}


	eof=0;	
	f_read(&fileR, filebuf,fc.max_framesize,&br);
	bytesleft=br;
	XferCplt = 1;
	while(1)
	{
		if( Get_One_Block(&fileR,&fc) == 0)
		{
			while(XferCplt == 0)
			{}
			if(bufferswitch == 1)
			{
				Audio_MAL_Play((u32)PCM_buf1,decoded_buf_sz*2);
				bufferswitch = 0;
			}
			else
			{
				Audio_MAL_Play((u32)PCM_buf0,decoded_buf_sz*2);
				bufferswitch = 1;
			}
			XferCplt = 0;
		}
		else
		{
			break;
		}
	}
}



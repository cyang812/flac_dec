#include "stdint.h"
#include "string.h"
#include "ff.h"
#include "diskio.h"
#include "decoder.h"
#include "Flac.h"
#include "fsl_debug_console.h"
#include "Audio.h"

//This is the size of the memory block that the decoders store all their work in
//It should be set to the largest value it ever needs to be (currently FLAC defined)
#define decoderScatchSize MAX_FRAMESIZE + MAX_BLOCKSIZE*8
// Buffer for all decoders
static unsigned char g_decoderScratch[decoderScatchSize];

//converts string to all uppercase letters
static void strToUppercase(char * string)
{
	unsigned long i = 0;
	
	while(string[i] != '\0')
	{
		if(string[i] >= 97 && string[i] <= 122)
		{
			string[i] -= 32;
		}
		i++;

	}
}

//This function creates the FLACContext the file at filePath
//Called by main FLAC decoder
//See http://flac.sourceforge.net/format.html for FLAC format details
//0 context is valid; 1 context is not valid
static int Flac_ParceMetadata(const TCHAR filePath[], FLACContext* context)
{
	UINT s1 = 0;
	FIL FLACfile;
	int metaDataFlag = 1;
	char metaDataChunk[128];	
	unsigned long metaDataBlockLength = 0;
	char* tagContents;

	if(f_open(&FLACfile, filePath, FA_READ) != FR_OK) {
		PRINTF("Could not open: %s\r\n", filePath);
		return 1;
	}
	f_read(&FLACfile, metaDataChunk, 4, &s1);
	if(s1 != 4) {
		PRINTF("Read failure\r\n");
		f_close(&FLACfile);
		return 1;
	}

	if(memcmp(metaDataChunk, "fLaC", 4) != 0) {
		PRINTF("Not a FLAC file\r\n");
		f_close(&FLACfile);
		return 1;
	}
	
	// Now we are at the stream block
	// Each block has metadata header of 4 bytes
	do {
		f_read(&FLACfile, metaDataChunk, 4, &s1);
		if(s1 != 4) {
			PRINTF("Read failure\r\n");
			f_close(&FLACfile);
			return 1;
		}

		//Check if last chunk
		if(metaDataChunk[0] & 0x80) metaDataFlag = 0;
		metaDataBlockLength = (metaDataChunk[1] << 16) | (metaDataChunk[2] << 8) | metaDataChunk[3];
		//STREAMINFO block
		if((metaDataChunk[0] & 0x7F) == 0) {						
			if(metaDataBlockLength > 128) {
				PRINTF("Metadata buffer too small\r\n");
				f_close(&FLACfile);
				return 1;
			}

			f_read(&FLACfile, metaDataChunk, metaDataBlockLength, &s1);
			if(s1 != metaDataBlockLength) {
				PRINTF("Read failure\r\n");
				f_close(&FLACfile);
				return 1;
			}
			/* 
			<bits> Field in STEAMINFO
			<16> min block size (samples)
			<16> max block size (samples)
			<24> min frams size (bytes)
			<24> max frams size (bytes)
			<20> Sample rate (Hz)
			<3> (number of channels)-1
			<5> (bits per sample)-1. 
			<36> Total samples in stream. 
			<128> MD5 signature of the unencoded audio data.
			*/		
			context->min_blocksize = (metaDataChunk[0] << 8) | metaDataChunk[1];
			context->max_blocksize = (metaDataChunk[2] << 8) | metaDataChunk[3];
			context->min_framesize = (metaDataChunk[4] << 16) | (metaDataChunk[5] << 8) | metaDataChunk[6];
			context->max_framesize = (metaDataChunk[7] << 16) | (metaDataChunk[8] << 8) | metaDataChunk[9];
			context->samplerate = (metaDataChunk[10] << 12) | (metaDataChunk[11] << 4) | ((metaDataChunk[12] & 0xf0) >> 4);
			context->channels = ((metaDataChunk[12] & 0x0e) >> 1) + 1;
			context->bps = (((metaDataChunk[12] & 0x01) << 4) | ((metaDataChunk[13] & 0xf0)>>4) ) + 1;			
			//This field in FLAC context is limited to 32-bits
			context->totalsamples = (metaDataChunk[14] << 24) | (metaDataChunk[15] << 16) | (metaDataChunk[16] << 8) | metaDataChunk[17];
		} else if((metaDataChunk[0] & 0x7F) == 4) {
			unsigned long fieldLength, commentListLength;			
			unsigned long readCount;
			unsigned long totalReadCount = 0;
			unsigned long currentCommentNumber = 0;
			int readAmount;

			f_read(&FLACfile, &fieldLength, 4, &s1);
			totalReadCount +=s1;
			//Read vendor info
			readCount = 0;
			readAmount = 128;
			while(readCount < fieldLength) {
				if(fieldLength-readCount < readAmount) readAmount = fieldLength-readCount;
				if(readAmount> metaDataBlockLength-totalReadCount) {
					PRINTF("Malformed metadata aborting\r\n");
					f_close(&FLACfile);
					return 1;
				
				}
				f_read(&FLACfile, metaDataChunk, readAmount, &s1);
				readCount += s1;
				totalReadCount +=s1;
				//terminate the string								
				metaDataChunk[s1-1] = '\0';
			}

			f_read(&FLACfile, &commentListLength, 4, &s1);
			totalReadCount +=s1;
			while(currentCommentNumber < commentListLength) {
				f_read(&FLACfile, &fieldLength, 4, &s1);
				totalReadCount +=s1;
				readCount = 0;
				readAmount = 128;
				while(readCount < fieldLength) {
					if(fieldLength-readCount < readAmount) readAmount = fieldLength-readCount;
					if(readAmount> metaDataBlockLength-totalReadCount) {
						PRINTF("Malformed metadata aborting\r\n");
						f_close(&FLACfile);
						return 1;
					
					}
					f_read(&FLACfile, metaDataChunk, readAmount, &s1);
					readCount += s1;
					totalReadCount +=s1;
					//terminate the string
					metaDataChunk[s1-1] = '\0';
					
					//Make another with just contents
					tagContents = strchr(metaDataChunk, '=');
					if (!tagContents) {
						continue;
					}					
					tagContents[0] = '\0';
					tagContents = &tagContents[1];
					strToUppercase(metaDataChunk);
					if(strcmp(metaDataChunk, "ARTIST") == 0) {		
						PRINTF("Artist: %s\r\n", tagContents);
					} else if(strcmp(metaDataChunk, "TITLE") == 0) {	
						PRINTF("Title: %s\r\n", tagContents);
					} else if(strcmp(metaDataChunk, "ALBUM") == 0) {	
						PRINTF("Album: %s\r\n", tagContents);
					}					
				}
				currentCommentNumber++;
			}
			if(f_lseek(&FLACfile, FLACfile.fptr + metaDataBlockLength-totalReadCount) != FR_OK) {
				PRINTF("File Seek Faile\r\n");
				f_close(&FLACfile);
				return 1;
			}
		} else {		
			if(f_lseek(&FLACfile, FLACfile.fptr + metaDataBlockLength) != FR_OK) {
				PRINTF("File Seek Failed\r\n");
				f_close(&FLACfile);
				return 1;
			}
		}		
	} while(metaDataFlag);

	// track length in ms
	context->length = (context->totalsamples / context->samplerate) * 1000; 
	// file size in bytes
	context->filesize = f_size(&FLACfile);					
	// current offset is end of metadata in bytes
	context->metadatalength = FLACfile.fptr;
	// bitrate of file				
	context->bitrate = ((context->filesize - context->metadatalength) * 8) / context->length;
	f_close(&FLACfile);
	return 0;	
}

//Just a dummy function for the flac_decode_frame
static void yield() 
{
	//Do nothing
}

int Flac_Play(const char filePath[])
{
	FIL FLACfile;
	UINT bytesLeft, bytesUsed, s1;
	int i;
	uint8_t WriteIndex;
	int Index;

	FLACContext context;
	int sampleShift;
	int16_t samplePair[2];

	//Pointers to memory chuncks in scratchMemory for decode
	//fileChunk currently can't be in EPI as it needs byte access
	unsigned char* bytePointer;
	unsigned char* fileChunk;
	int32_t* decodedSamplesLeft;
	int32_t* decodedSamplesRight;

	//Setup the pointers, the defines are in decoder.h
	bytePointer = (unsigned char*)g_decoderScratch;
	fileChunk = bytePointer;
	decodedSamplesLeft = (int32_t*)&bytePointer[MAX_FRAMESIZE];
	decodedSamplesRight = (int32_t*)&bytePointer[MAX_FRAMESIZE+4*MAX_BLOCKSIZE];

	//Get the metadata we need to play the file
	if(Flac_ParceMetadata(filePath, &context) != 0) {
		PRINTF("Failed to get FLAC context\r\n");
		return 1;
	}

	if(f_open(&FLACfile, filePath ,FA_READ) != FR_OK) {
		PRINTF("Cannot open: %s\r\n", filePath);
		return 1;
	}

	//Goto start of stream
	if(f_lseek(&FLACfile, context.metadatalength) != FR_OK) {
		f_close(&FLACfile);
		return 1;
	}

	//The decoder has sample size defined by FLAC_OUTPUT_DEPTH (currently 29 bit)
	//Shift for lower bitrate to align MSB correctly
	sampleShift = FLAC_OUTPUT_DEPTH-context.bps;
	//Fill up fileChunk completely (MAX_FRAMSIZE = valid size of memory fileChunk points to)
	f_read(&FLACfile, fileChunk, MAX_FRAMESIZE, &bytesLeft);
	
	PRINTF("Playing %s\r\n", filePath);
	PRINTF("Mode: %s\r\n", context.channels==1?"Mono":"Stereo");
	PRINTF("Samplerate: %d Hz\r\n", context.samplerate);
	PRINTF("SampleBits: %d bit\r\n", context.bps);
	PRINTF("Samples: %d\r\n", context.totalsamples);
	
	I2S_SetSamplerate(context.samplerate);
	I2S_TxStart();
	WriteIndex = I2SState.TxWriteIndex + 1;
	Index = 0;

	while (bytesLeft) {
		if(flac_decode_frame(&context, decodedSamplesLeft, decodedSamplesRight, fileChunk, bytesLeft, yield) < 0) {
			PRINTF("FLAC Decode Failed\r\n");
			break;
		}		

		//Dump the block to the waveOut
		i = 0;
		while(i < context.blocksize) {
			//Left Channel
			samplePair[0] = (uint16_t) (decodedSamplesLeft[i]>>sampleShift);
			if (context.channels==2) {
				//Right Channel
				samplePair[1] = (uint16_t) (decodedSamplesRight[i]>>sampleShift);
			} else {
				//Repeat Left channel if mono
				samplePair[1] = (uint16_t) (decodedSamplesLeft[i]>>sampleShift);
			}
			
			while (WriteIndex == I2SState.TxReadIndex) {
				
			}
			//Sample pair is 4 bytes, 16-bit mode
			if (WriteIndex != I2SState.TxReadIndex) {
				I2SState.TxBuffer[I2SState.TxWriteIndex][Index] = (samplePair[0]&0xffff) | (samplePair[1]<<16);
				Index++;
				if (Index >= AUDIO_FRAME_SIZE) {
					Index = 0;
					I2SState.TxWriteIndex = WriteIndex;
					if (WriteIndex >= AUDIO_NUM_BUFFERS-1) {
						WriteIndex = 0;
					} else {
						WriteIndex++;
					}					
				}
			}
			i++;
		}

		//calculate the number of valid bytes left in the fileChunk buffer
		bytesUsed = context.gb.index/8;
		bytesLeft -= bytesUsed;
		//shift the unused stuff to the front of the fileChunk buffer
		memmove(fileChunk, &fileChunk[bytesUsed], bytesLeft);
		//Refill the fileChunk buffer
		f_read(&FLACfile, &fileChunk[bytesLeft], MAX_FRAMESIZE - bytesLeft, &s1);
		//add however many were read
		bytesLeft += s1;
	}
	
	I2S_TxStop();
	PRINTF("Play over\r\n");
	f_close(&FLACfile);

	return 0;	
}

﻿//*****************************************************************************************//
//**                                                                                     **//
//**                              PNGLoader                                              **//
//**                                                                                     **//
//*****************************************************************************************//

#ifndef Class_PNGLoader_Header
#define Class_PNGLoader_Header

#include <stdio.h>
#include <string.h>

class PNGLoader {

private:
	const unsigned int imageNumChannel = 4;
	const unsigned char pivotBit = 8;

	class bytePointer {
	private:
		unsigned char* byte = nullptr;
		unsigned int index = 0;
		unsigned int Size = 0;
	public:
		void setPointer(unsigned int size, unsigned char* byt) {
			byte = new unsigned char[size];
			Size = size;
			memcpy(byte, byt, size);
		}
		~bytePointer() {
			delete[] byte;
			byte = nullptr;
		}
		//ビックエンディアン
		unsigned int convertUCHARtoUINT() {
			unsigned int ret = ((unsigned int)byte[index + 0] << 24) | ((unsigned int)byte[index + 1] << 16) |
				((unsigned int)byte[index + 2] << 8) | ((unsigned int)byte[index + 3]);
			index += 4;
			return ret;
		}
		unsigned char getChar() {
			unsigned char ret = byte[index];
			index++;
			return ret;
		}
		unsigned char* getPointer(unsigned int addPointer) {
			unsigned char* ret = &byte[index];
			index += addPointer;
			return ret;
		}
		unsigned int getIndex() {
			return index;
		}
		void setIndex(unsigned int ind) {
			index = ind;
		}
		bool checkEOF() {
			if (index >= Size)return false;
			return true;
		}
	};

	unsigned int width = 0;
	unsigned int height = 0;

	void bitdepthSift(unsigned char* siftImage, unsigned char* decom, unsigned int decomW, unsigned int decomH,
		unsigned int compNumChannel, unsigned char BitDepth);

	void bindThePalette(unsigned char* outByte, unsigned char* index, unsigned char* Palette,
		unsigned int wid, unsigned int hei, unsigned int numChannel);

	void unfiltering(unsigned char* dstImage, unsigned char* srcImage,
		unsigned int widByteSize, unsigned int numChannel, unsigned int heiByteSize);

	void resize(unsigned char colorType, unsigned char* dstImage, unsigned char* srcImage,
		unsigned int dstWid, unsigned int dstHei,
		unsigned int srcWid, unsigned int srcNumChannel, unsigned int srcHei);

public:
	unsigned char* loadPNG(char* pass, unsigned int outWid, unsigned int outHei,
		char* errorMessage = nullptr);

	unsigned char* loadPngInByteArray(unsigned char* byteArray, unsigned int size,
		unsigned int outWid, unsigned int outHei, char* errorMessage = nullptr);

	unsigned int getSrcWidth() { return width; }
	unsigned int getSrcHeight() { return height; }
};

#endif


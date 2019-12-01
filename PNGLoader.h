//*****************************************************************************************//
//**                                                                                     **//
//**                              PNGLoader                                              **//
//**                                                                                     **//
//*****************************************************************************************//

#ifndef Class_PNGLoader_Header
#define Class_PNGLoader_Header

#include <stdio.h>

class PNGLoader {

private:
	unsigned char* image = nullptr;
	unsigned int imageNumChannel = 4;

	class bytePointer {
	private:
		unsigned char* byte = nullptr;
		unsigned int index = 0;
		unsigned int Size = 0;
		bytePointer() {}
	public:
		bytePointer(unsigned int size, FILE* fp) {
			byte = new unsigned char[size];
			fread(byte, sizeof(unsigned char), size, fp);
			Size = size;
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

	void unfiltering(unsigned char* dstImage, unsigned char* srcImage,
		unsigned int widByteSize, unsigned int numChannel, unsigned int heiByteSize);

	void resize(unsigned char colorType, unsigned char* dstImage, unsigned char* srcImage,
		unsigned int dstWid, unsigned int dstHei,
		unsigned int srcWid, unsigned int srcNumChannel, unsigned int srcHei);

public:
	~PNGLoader();
	bool loadPNG(char* pass, unsigned int outWid, unsigned int outHei);
	unsigned char* getImage();
};

#endif


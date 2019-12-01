//*****************************************************************************************//
//**                                                                                     **//
//**                              PNGLoader                                              **//
//**                                                                                     **//
//*****************************************************************************************//

#define _CRT_SECURE_NO_WARNINGS
#include "PNGLoader.h"
#include "../DecompressDeflate/DecompressDeflate.h"
#include <string.h>
#include <stdlib.h>

PNGLoader::~PNGLoader() {
	if (image) {
		delete[] image;
		image = nullptr;
	}
}

bool PNGLoader::loadPNG(char* pass, unsigned int outWid, unsigned int outHei) {
	FILE* fp = fopen(pass, "rb");
	if (fp == NULL) {
		return false;
	}
	unsigned int count = 0;
	while (fgetc(fp) != EOF) {
		count++;
	}
	count++;//EOFの分
	fseek(fp, 0, SEEK_SET);//最初に戻す
	bytePointer* bp = new bytePointer(count, fp);
	fclose(fp);

	const unsigned int numPixel = outWid * imageNumChannel * outHei;
	image = new unsigned char[numPixel];
	unsigned char* IDATByte = nullptr;
	unsigned char* decompressImage = nullptr;
	unsigned char* UnfilteringImage = nullptr;

	static const unsigned char numSignature = 8;
	static const unsigned char signature[numSignature] = { 137,80,78,71,13,10,26,10 };

	for (int i = 0; i < numSignature; i++) {
		if (bp->getChar() != signature[i])return false;
	}

	//IDATサイズカウント
	unsigned int IDAT_Size = 0;
	unsigned int index = bp->getIndex();
	do {
		const unsigned int chunkDataLenght = bp->convertUCHARtoUINT();
		char chunkType[5] = {};
		memcpy(chunkType, bp->getPointer(4), sizeof(char) * 4);
		chunkType[4] = '\0';
		if (!strcmp("IDAT", chunkType)) {
			if (chunkDataLenght == 0) {
				bp->getPointer(4);//CRC分スキップ
				continue;
			}
			IDAT_Size += chunkDataLenght;
			bp->getPointer(chunkDataLenght + 4);
			continue;
		}
		bp->getPointer(chunkDataLenght + 4);//IDAT以外スキップ
	} while (bp->checkEOF());
	bp->setIndex(index);
	IDATByte = new unsigned char[IDAT_Size];

	//IHDRヘッダChunkData
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int compNumChannel = 0;
	unsigned int numChannel = 0;
	unsigned char bitDepth = 0;
	unsigned char colorType = 0;
	unsigned char compressionMethod = 0;
	unsigned char filterMethod = 0;
	unsigned char interlaceMethod = 0;

	//チャンク
	unsigned int idatIndex = 0;
	do {
		const unsigned int chunkDataLenght = bp->convertUCHARtoUINT();
		char chunkType[5] = {};
		memcpy(chunkType, bp->getPointer(4), sizeof(char) * 4);
		chunkType[4] = '\0';

		//各ChunkData読み込み
		if (!strcmp("IHDR", chunkType)) {
			width = bp->convertUCHARtoUINT();
			height = bp->convertUCHARtoUINT();
			bitDepth = bp->getChar();//ビット深度1,2,4,8,16まである
			colorType = bp->getChar();
			//全3bit, αチャンネル有無,カラーorグレー,パレット使用有無 

			switch (colorType) {
			case 0://(000) 1, 2, 4, 8, 16 グレースケール
				compNumChannel = 1;
				numChannel = 1;
				break;
			case 2://(010) 8, 16          RGBカラー
				compNumChannel = 3;
				numChannel = 3;
				break;
			case 3://(011) 1, 2, 4, 8    インデックスカラー(要PLTEチャンク)
				compNumChannel = 1;
				numChannel = 3;
				break;
			case 4://(100) 8, 16         グレースケール ＋ αチャンネル
				compNumChannel = 2;
				numChannel = 2;
				break;
			case 6://(110) 8, 16         RGBカラー ＋ αチャンネル
				compNumChannel = 4;
				numChannel = 4;
				break;
			}

			compressionMethod = bp->getChar();//0のみ(圧縮有のみ)
			filterMethod = bp->getChar();//0のみ(フィルタリング有のみ)
			interlaceMethod = bp->getChar();
			const unsigned int CRC = bp->convertUCHARtoUINT();
			//ピクセルの水平ライン先頭にフィルタタイプ情報1byteが付加する(+height)
			decompressImage = new unsigned char[(long long)width * compNumChannel * height + height];
			UnfilteringImage = new unsigned char[(long long)width * numChannel * height];
			continue;
		}
		if (!strcmp("IDAT", chunkType)) {
			if (chunkDataLenght == 0) {
				bp->getPointer(4);//CRC分スキップ
				continue;
			}
			memcpy(&IDATByte[idatIndex], bp->getPointer(chunkDataLenght), sizeof(char) * chunkDataLenght);
			idatIndex += chunkDataLenght;
			const unsigned int CRC = bp->convertUCHARtoUINT();
			continue;
		}
		if (!strcmp("IEND", chunkType)) {
			break;
		}
		bp->getPointer(chunkDataLenght + 4);//次のチャンクへスキップ +4 はCRC分
	} while (bp->checkEOF());
	if (!bp->checkEOF())return false;

	//zlib フォーマット
	//Compression method / flags code : 1 byte
	//Additional flags / check bits : 1 byte
	//Compressed data blocks : n bytes
	//Check value : 4 bytes
	unsigned char CompressionMethod = IDATByte[0] & 0x0f;//圧縮方式8のみ
	unsigned char CompressionInfo = (IDATByte[0] >> 4) & 0x0f;//圧縮情報
	unsigned char FCHECK = IDATByte[1] & 0x1f;//CMF と FLG のチェックビット
	unsigned char FDICT = (IDATByte[1] >> 5) & 0x01;//プリセット辞書
	unsigned char FLEVEL = (IDATByte[1] >> 6) & 0x03;//圧縮レベル
	DecompressDeflate de;
	de.getDecompressArray(&IDATByte[2], IDAT_Size - 6, decompressImage);

	unfiltering(UnfilteringImage, decompressImage, width, numChannel, height);

	//memcpy(image, UnfilteringImage, (long long)width * numChannel * height);
	resize(colorType, image, UnfilteringImage,
		outWid, outHei,
		width, numChannel, height);

	delete[] UnfilteringImage;
	UnfilteringImage = nullptr;
	delete[] IDATByte;
	IDATByte = nullptr;
	delete[] decompressImage;
	decompressImage = nullptr;
	delete bp;
	bp = nullptr;
	return true;
}

//paethアルゴリズム  
//左,上,左上のピクセル値から,現ピクセル値を予測するらしい・・計算はそのまま拝借しました・・
static unsigned int paeth(int left, int up, int upLeft) {
	int total = left + up - upLeft;
	int pa = abs(total - left);  //x方向変化量
	int pb = abs(total - up);    //y方向変化量
	int pc = abs(total - upLeft);//合計

	//x方向変化量少ない場合 → 左ピクセル出力
	if (pa <= pb && pa <= pc)
		return left;

	//y方向変化量少ない場合 → 上ピクセル出力
	if (pb <= pc)
		return up;

	//どちらでもない場合 → 左上ピクセル出力        
	return upLeft;
}

void PNGLoader::unfiltering(unsigned char* dstImage, unsigned char* srcImage,
	unsigned int widByteSize, unsigned int numChannel, unsigned int heiByteSize) {
	for (unsigned int h = 0; h < heiByteSize; h++) {
		unsigned int wid = widByteSize * numChannel;
		for (unsigned int w = 0; w < wid; w += numChannel) {
			for (unsigned int c = 0; c < numChannel; c++) {
				unsigned int tmpW = 0;
				unsigned int tmpH = 0;
				unsigned int tmpWH = 0;
				unsigned int ave = 0;
				switch (srcImage[(wid + 1) * h]) {
				case 0://フィルタ無し
					dstImage[wid * h + w + c] = srcImage[(wid + 1) * h + w + 1 + c];
					break;
				case 1://左差分
					if (w == 0) {
						dstImage[wid * h + w + c] = srcImage[(wid + 1) * h + w + 1 + c];
					}
					else {
						dstImage[wid * h + w + c] =
							(unsigned char)(((unsigned int)dstImage[wid * h + w - numChannel + c] +
							(unsigned int)srcImage[(wid + 1) * h + w + 1 + c]) % 256);
					}
					break;
				case 2://上差分
					if (h == 0) {
						dstImage[wid * h + w + c] = srcImage[(wid + 1) * h + w + 1 + c];
					}
					else {
						dstImage[wid * h + w + c] =
							(unsigned char)(((unsigned int)dstImage[wid * (h - 1) + w + c] +
							(unsigned int)srcImage[(wid + 1) * h + w + 1 + c]) % 256);
					}
					break;
				case 3://上左の平均との差分
					if (w != 0) {
						tmpW = (unsigned int)dstImage[wid * h + w - numChannel + c];
					}
					if (h != 0) {
						tmpH = (unsigned int)dstImage[wid * (h - 1) + w + c];
					}
					ave = (unsigned int)((tmpW + tmpH) * 0.5);
					dstImage[wid * h + w + c] =
						(unsigned char)((ave + (unsigned int)srcImage[(wid + 1) * h + w + 1 + c]) % 256);
					break;
				case 4://paethアルゴリズム
					if (w != 0) {
						tmpW = (unsigned int)dstImage[wid * h + w - numChannel + c];
					}
					if (h != 0) {
						tmpH = (unsigned int)dstImage[wid * (h - 1) + w + c];
					}
					if (w != 0 && h != 0) {
						tmpWH = (unsigned int)dstImage[wid * (h - 1) + w - numChannel + c];
					}
					dstImage[wid * h + w + c] =
						(unsigned char)((paeth(tmpW, tmpH, tmpWH) +
						(unsigned int)srcImage[(wid + 1) * h + w + 1 + c]) % 256);
					break;
				}
			}
		}
	}
}

void PNGLoader::resize(unsigned char colorType, unsigned char* dstImage, unsigned char* srcImage,
	unsigned int dstWid, unsigned int dstHei,
	unsigned int srcWid, unsigned int srcNumChannel, unsigned int srcHei) {

	unsigned int dWid = dstWid * imageNumChannel;
	unsigned int sWid = srcWid * srcNumChannel;

	for (unsigned int dh = 0; dh < dstHei; dh++) {
		unsigned int sh = (unsigned int)((float)srcHei / (float)dstHei * dh);
		for (unsigned int dw = 0; dw < dstWid; dw++) {
			unsigned int dstInd0 = dWid * dh + dw * imageNumChannel;
			unsigned int sw = (unsigned int)((float)srcWid / (float)dstWid * dw) * srcNumChannel;
			unsigned int srcInd0 = sWid * sh + sw;

			switch (colorType) {
			case 0://(000) 1, 2, 4, 8, 16 グレースケール
				dstImage[dstInd0 + 0] = srcImage[srcInd0];
				dstImage[dstInd0 + 1] = srcImage[srcInd0];
				dstImage[dstInd0 + 2] = srcImage[srcInd0];
				dstImage[dstInd0 + 3] = 255;
				break;
			case 2://(010) 8, 16          RGBカラー
			case 3://(011) 1, 2, 4, 8    インデックスカラー(要PLTEチャンク)
				dstImage[dstInd0 + 0] = srcImage[srcInd0];
				dstImage[dstInd0 + 1] = srcImage[srcInd0 + 1];
				dstImage[dstInd0 + 2] = srcImage[srcInd0 + 2];
				dstImage[dstInd0 + 3] = 255;
				break;
			case 4://(100) 8, 16         グレースケール ＋ αチャンネル
				dstImage[dstInd0 + 0] = srcImage[srcInd0];
				dstImage[dstInd0 + 1] = srcImage[srcInd0];
				dstImage[dstInd0 + 2] = srcImage[srcInd0];
				dstImage[dstInd0 + 3] = srcImage[srcInd0 + 1];
				break;
			case 6://(110) 8, 16         RGBカラー ＋ αチャンネル
				dstImage[dstInd0 + 0] = srcImage[srcInd0];
				dstImage[dstInd0 + 1] = srcImage[srcInd0 + 1];
				dstImage[dstInd0 + 2] = srcImage[srcInd0 + 2];
				dstImage[dstInd0 + 3] = srcImage[srcInd0 + 3];
				break;
			}
		}
	}
}

unsigned char* PNGLoader::getImage() {
	return image;
}
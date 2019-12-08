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

unsigned char* PNGLoader::loadPNG(char* pass, unsigned int outWid, unsigned int outHei) {
	FILE* fp = fopen(pass, "rb");
	if (fp == NULL) {
		return nullptr;
	}
	unsigned int count = 0;
	while (fgetc(fp) != EOF) {
		count++;
	}
	fseek(fp, 0, SEEK_SET);//最初に戻す
	bytePointer* bp = new bytePointer(count, fp);
	fclose(fp);

	const unsigned int numPixel = outWid * imageNumChannel * outHei;
	unsigned char* image = new unsigned char[numPixel];
	unsigned char* IDATByte = nullptr;
	unsigned char* PLTEByte = nullptr;
	unsigned int numPLTEByte = 0;
	unsigned char* tRNSByte = nullptr;
	unsigned int numtRNSByte = 0;
	unsigned char* decompressImage = nullptr;
	unsigned char* deployedImage = nullptr;

	static const unsigned char numSignature = 8;
	static const unsigned char signature[numSignature] = { 137,80,78,71,13,10,26,10 };

	for (int i = 0; i < numSignature; i++) {
		if (bp->getChar() != signature[i])return nullptr;
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
			//ピクセルの水平ライン先頭にフィルタタイプ情報1byte付加する
			decompressImage = new unsigned char[((long long)width * compNumChannel + 1) * height];
			deployedImage = new unsigned char[(long long)width * numChannel * height];
			continue;
		}
		if (!strcmp("PLTE", chunkType)) {
			if (chunkDataLenght == 0) {
				bp->getPointer(4);//CRC分スキップ
				continue;
			}
			PLTEByte = new unsigned char[chunkDataLenght];
			memcpy(PLTEByte, bp->getPointer(chunkDataLenght), sizeof(unsigned char) * chunkDataLenght);
			const unsigned int CRC = bp->convertUCHARtoUINT();
			numPLTEByte = chunkDataLenght;
			continue;
		}
		if (!strcmp("tRNS", chunkType)) {
			if (chunkDataLenght == 0) {
				bp->getPointer(4);//CRC分スキップ
				continue;
			}
			tRNSByte = new unsigned char[chunkDataLenght];
			memcpy(tRNSByte, bp->getPointer(chunkDataLenght), sizeof(unsigned char) * chunkDataLenght);
			const unsigned int CRC = bp->convertUCHARtoUINT();
			numtRNSByte = chunkDataLenght;
			continue;
		}
		if (!strcmp("IDAT", chunkType)) {
			if (chunkDataLenght == 0) {
				bp->getPointer(4);//CRC分スキップ
				continue;
			}
			memcpy(&IDATByte[idatIndex], bp->getPointer(chunkDataLenght), sizeof(unsigned char) * chunkDataLenght);
			idatIndex += chunkDataLenght;
			const unsigned int CRC = bp->convertUCHARtoUINT();
			continue;
		}
		if (!strcmp("IEND", chunkType)) {
			break;
		}
		bp->getPointer(chunkDataLenght + 4);//次のチャンクへスキップ +4 はCRC分
	} while (bp->checkEOF());
	if (!bp->checkEOF())return nullptr;

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

	unfiltering(deployedImage, decompressImage, width, compNumChannel, height);

	if (colorType == 3) {
		numChannel++;
		unsigned char* palette = new unsigned char[numPLTEByte + (numPLTEByte / 3)];
		unsigned int cnt = 0;
		for (unsigned int i = 0; i < numPLTEByte; i += 3) {
			palette[i] = PLTEByte[i];
			palette[i + 1] = PLTEByte[i + 1];
			palette[i + 2] = PLTEByte[i + 2];
			palette[i + 3] = 255;
			if (cnt < numtRNSByte) {
				palette[i + 3] = tRNSByte[cnt++];
			}
		}
		unsigned char* color = new unsigned char[(long long)width * numChannel * height];
		bindThePalette(color, deployedImage, palette, width, height, numChannel);
		delete[] palette;
		palette = nullptr;
		resize(colorType, image, color,
			outWid, outHei,
			width, numChannel, height);
		delete[] color;
		color = nullptr;
	}
	else {
		resize(colorType, image, deployedImage,
			outWid, outHei,
			width, numChannel, height);
	}

	delete[] deployedImage;
	deployedImage = nullptr;
	delete[] IDATByte;
	IDATByte = nullptr;
	delete[] decompressImage;
	decompressImage = nullptr;
	delete[] PLTEByte;
	PLTEByte = nullptr;
	delete[] tRNSByte;
	tRNSByte = nullptr;
	delete bp;
	bp = nullptr;

	return image;
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

void PNGLoader::bindThePalette(unsigned char* outByte, unsigned char* index, unsigned char* Palette,
	unsigned int wid, unsigned int hei, unsigned int numChannel) {

	unsigned int widByte = wid * numChannel;
	for (unsigned int h = 0; h < hei; h++) {
		for (unsigned int w = 0; w < wid; w++) {
			for (unsigned int c = 0; c < numChannel; c++) {
				outByte[widByte * h + w * numChannel + c] =
					Palette[index[wid * h + w] * numChannel + c];
			}
		}
	}
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
				unsigned int add = 0;
				switch (srcImage[(wid + 1) * h]) {
				case 1://左差分
					if (w != 0) {
						add = (unsigned int)dstImage[wid * h + w - numChannel + c];
					}
					break;
				case 2://上差分
					if (h != 0) {
						add = (unsigned int)dstImage[wid * (h - 1) + w + c];
					}
					break;
				case 3://上左の平均との差分
					if (w != 0) {
						tmpW = (unsigned int)dstImage[wid * h + w - numChannel + c];
					}
					if (h != 0) {
						tmpH = (unsigned int)dstImage[wid * (h - 1) + w + c];
					}
					add = (unsigned int)((tmpW + tmpH) * 0.5);
					break;
				case 4://paethアルゴリズムで差分計算
					if (w != 0) {
						tmpW = (unsigned int)dstImage[wid * h + w - numChannel + c];
					}
					if (h != 0) {
						tmpH = (unsigned int)dstImage[wid * (h - 1) + w + c];
					}
					if (w != 0 && h != 0) {
						tmpWH = (unsigned int)dstImage[wid * (h - 1) + w - numChannel + c];
					}
					add = paeth(tmpW, tmpH, tmpWH);
					break;
				}
				//差分を足す
				dstImage[wid * h + w + c] =
					(unsigned char)((add + (unsigned int)srcImage[(wid + 1) * h + w + 1 + c]) % 256);
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
				dstImage[dstInd0 + 0] = srcImage[srcInd0];
				dstImage[dstInd0 + 1] = srcImage[srcInd0 + 1];
				dstImage[dstInd0 + 2] = srcImage[srcInd0 + 2];
				dstImage[dstInd0 + 3] = 255;
				break;
			case 3://(011) 1, 2, 4, 8    インデックスカラー(要PLTEチャンク)
				dstImage[dstInd0 + 0] = srcImage[srcInd0];
				dstImage[dstInd0 + 1] = srcImage[srcInd0 + 1];
				dstImage[dstInd0 + 2] = srcImage[srcInd0 + 2];
				dstImage[dstInd0 + 3] = srcImage[srcInd0 + 3];
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

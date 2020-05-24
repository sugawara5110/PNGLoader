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
#include <memory>

static void setEr(char* errorMessage, char* inMessage) {
	if (errorMessage) {
		int size = (int)strlen(inMessage) + 1;
		memcpy(errorMessage, inMessage, size);
	}
}

unsigned char* PNGLoader::loadPNG(char* pass, unsigned int outWid, unsigned int outHei, char* errorMessage) {
	FILE* fp = fopen(pass, "rb");
	if (fp == NULL) {
		setEr(errorMessage, "File read error");
		return nullptr;
	}
	unsigned int count = 0;
	while (fgetc(fp) != EOF) {
		count++;
	}
	fseek(fp, 0, SEEK_SET);//最初に戻す
	std::unique_ptr<unsigned char[]> byte = std::make_unique<unsigned char[]>(count);
	fread(byte.get(), sizeof(unsigned char), count, fp);
	fclose(fp);
	unsigned char* ret = loadPngInByteArray(byte.get(), count, outWid, outHei, errorMessage);

	return ret;
}

unsigned char* PNGLoader::loadPngInByteArray(unsigned char* byteArray, unsigned int size,
	unsigned int outWid, unsigned int outHei, char* errorMessage) {

	std::unique_ptr<bytePointer> bp = std::make_unique<bytePointer>();
	bp->setPointer(size, byteArray);

	static const unsigned char numSignature = 8;
	static const unsigned char signature[numSignature] = { 137,80,78,71,13,10,26,10 };

	for (int i = 0; i < numSignature; i++) {
		if (bp->getChar() != signature[i]) {
			setEr(errorMessage, "This file is not a png");
			return nullptr;
		}
	}

	std::unique_ptr<unsigned char[]> IDATByte = nullptr;
	std::unique_ptr<unsigned char[]> PLTEByte = nullptr;
	unsigned int numPLTEByte = 0;
	std::unique_ptr<unsigned char[]> tRNSByte = nullptr;
	unsigned int numtRNSByte = 0;
	std::unique_ptr<unsigned char[]> decompressImage = nullptr;
	std::unique_ptr<unsigned char[]> deployedImage = nullptr;

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
	IDATByte = std::make_unique<unsigned char[]>(IDAT_Size);

	//IHDRヘッダChunkData
	unsigned int compNumChannel = 0;
	unsigned int numChannel = 0;
	unsigned char bitDepth = 0;
	unsigned char colorType = 0;
	unsigned char compressionMethod = 0;
	unsigned char filterMethod = 0;
	unsigned char interlaceMethod = 0;
	struct vec2 {
		unsigned int w = 0;
		unsigned int h = 0;
	};
	vec2 passSize[7] = {};
	int allPassSize = 0;

	struct InterlaceScan {
		int xFactor, yFactor, xOffset, yOffset;
	};
	const InterlaceScan iScan[] = {
		8, 8, 0, 0,
		8, 8, 4, 0,
		4, 8, 0, 4,
		4, 4, 2, 0,
		2, 4, 0, 2,
		2, 2, 1, 0,
		1, 2, 0, 1
	};

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
			if (interlaceMethod) {
				for (int i = 0; i < 7; i++) {
					passSize[i].w = width / iScan[i].xFactor +
						((int)width % iScan[i].xFactor > iScan[i].xOffset ? 1 : 0);
					passSize[i].h = height / iScan[i].yFactor +
						((int)height % iScan[i].yFactor > iScan[i].yOffset ? 1 : 0);
					allPassSize += (passSize[i].w * compNumChannel * bitDepth / pivotBit + 1) * passSize[i].h;
				}
				decompressImage = std::make_unique<unsigned char[]>(allPassSize);
			}
			else {
				decompressImage =
					std::make_unique<unsigned char[]>(((long long)width * compNumChannel * bitDepth / pivotBit + 1) * height);
			}

			deployedImage = std::make_unique<unsigned char[]>((long long)width * compNumChannel * height);
			continue;
		}
		if (!strcmp("PLTE", chunkType)) {
			if (chunkDataLenght == 0) {
				bp->getPointer(4);//CRC分スキップ
				continue;
			}
			PLTEByte = std::make_unique<unsigned char[]>(chunkDataLenght);
			memcpy(PLTEByte.get(), bp->getPointer(chunkDataLenght), sizeof(unsigned char) * chunkDataLenght);
			const unsigned int CRC = bp->convertUCHARtoUINT();
			numPLTEByte = chunkDataLenght;
			continue;
		}
		if (!strcmp("tRNS", chunkType)) {
			if (chunkDataLenght == 0) {
				bp->getPointer(4);//CRC分スキップ
				continue;
			}
			tRNSByte = std::make_unique<unsigned char[]>(chunkDataLenght);
			memcpy(tRNSByte.get(), bp->getPointer(chunkDataLenght), sizeof(unsigned char) * chunkDataLenght);
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
	if (!bp->checkEOF()) {
		setEr(errorMessage, "chunk read error");
		return nullptr;
	}

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
	de.getDecompressArray(&IDATByte[2], IDAT_Size - 6, decompressImage.get());

	std::unique_ptr<unsigned char[]> bitdepthSiftImage = nullptr;

	if (interlaceMethod) {
		std::unique_ptr<unsigned char[]> p = std::make_unique <unsigned char[]>(compNumChannel);
		int decIndex = 0;
		for (int i = 0; i < 7; i++) {

			bitdepthSiftImage =
				std::make_unique <unsigned char[]>((passSize[i].w * compNumChannel + 1) * passSize[i].h);

			bitdepthSift(bitdepthSiftImage.get(), &decompressImage[decIndex], passSize[i].w, passSize[i].h,
				compNumChannel, bitDepth);
			decIndex += (passSize[i].w * compNumChannel * bitDepth / pivotBit + 1) * passSize[i].h;

			std::unique_ptr<unsigned char[]> depImage =
				std::make_unique <unsigned char[]>(passSize[i].w * compNumChannel * passSize[i].h);

			unfiltering(depImage.get(), bitdepthSiftImage.get(), passSize[i].w, compNumChannel, passSize[i].h);
			bitdepthSiftImage.reset();
			int pIndex = 0;
			for (unsigned int y = iScan[i].yOffset; y < height; y += iScan[i].yFactor) {
				for (unsigned int x = iScan[i].xOffset * compNumChannel;
					x < width * compNumChannel;
					x += (iScan[i].xFactor * compNumChannel)) {

					memcpy(p.get(), &depImage[pIndex], sizeof(unsigned char) * compNumChannel);
					pIndex += compNumChannel;
					unsigned char* pd = &deployedImage[width * compNumChannel * y + x];
					memcpy(pd, p.get(), sizeof(unsigned char) * compNumChannel);
				}
			}
		}
	}
	else {
		bitdepthSiftImage =
			std::make_unique <unsigned char[]>((width * compNumChannel + 1) * height);

		bitdepthSift(bitdepthSiftImage.get(), decompressImage.get(), width, height, compNumChannel, bitDepth);

		unfiltering(deployedImage.get(), bitdepthSiftImage.get(), width, compNumChannel, height);
	}
	if (outWid <= 0 || outHei <= 0) {
		outWid = width;
		outHei = height;
	}
	const unsigned int numPixel = outWid * imageNumChannel * outHei;
	unsigned char* image = new unsigned char[numPixel];//外部で開放

	if (colorType == 3) {
		numChannel++;
		std::unique_ptr<unsigned char[]> palette = std::make_unique <unsigned char[]>(numPLTEByte + (numPLTEByte / 3));
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
		std::unique_ptr<unsigned char[]> color = std::make_unique <unsigned char[]>((long long)width * numChannel * height);
		bindThePalette(color.get(), deployedImage.get(), palette.get(), width, height, numChannel);
		resize(colorType, image, color.get(),
			outWid, outHei,
			width, numChannel, height);
	}
	else {
		resize(colorType, image, deployedImage.get(),
			outWid, outHei,
			width, numChannel, height);
	}

	setEr(errorMessage, "OK");

	return image;
}

static const int bitMask[]{
	0b00000000,
	0b00000001,
	0b00000011,
	0b00000111,
	0b00001111,
	0b00011111,
	0b00111111,
	0b01111111,
	0b11111111
};
void PNGLoader::bitdepthSift(unsigned char* siftImage, unsigned char* decom, unsigned int decomW, unsigned int decomH,
	unsigned int compNumChannel, unsigned char BitDepth) {

	const int wid = decomW * compNumChannel * BitDepth / pivotBit + 1;
	int siftImageIndex = 0;
	for (unsigned int y = 0; y < decomH; y++) {
		for (int x = 0; x < wid; x++) {
			if (x == 0) { siftImage[siftImageIndex++] = decom[wid * y + x]; continue; }
			for (int p = pivotBit - BitDepth; p >= 0; p -= BitDepth) {
				siftImage[siftImageIndex++] = (decom[wid * y + x] >> p)& bitMask[BitDepth];
			}
		}
	}
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

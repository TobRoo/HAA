// JpegDecode.cpp: implementation of the CJpegDecode class.
//
//////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
#include "JpegDecode.h"
#include <memory.h>
#include <stdlib.h>  // for abs
#include "JpegDecodeHuffMan.h"
#include "DctQuant.h"
CHuffManDecoder m_decode;
CDctQuant       m_dctquant;

#define CENTERJSAMPLE	128

static unsigned char m_uMPGYVal[144][176];
static unsigned char m_uMPGUVVal[144][176];

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CJpegDecode::CJpegDecode()
{
}

CJpegDecode::~CJpegDecode()
{
	m_decode.Huff_Decode_Free();
}

bool CJpegDecode::DecodeInit(int m_nQuality)
{
	m_dctquant.j_uvy_quant_Init(m_nQuality);
	if( m_decode.Huff_Decode_Init() == 0 ) return false;
	return true;
}

void CJpegDecode::DecodeDel()
{
	m_decode.Huff_Decode_Free();
}



void CJpegDecode::SetQuality(int m_nQuality)
{
	m_dctquant.j_uvy_quant_Init(m_nQuality);
}


// added by eliu, most was copied from DecodeImage
bool CJpegDecode::Decode(const char *lpbuffer, int nSize, unsigned char *YData, unsigned char *UVData, int nRow, int nCol)
{
		int i,j,m,n;
	if( m_decode.Huff_Decode_Buffer(lpbuffer,nSize) == 0 )  return false;  // eliu changed


	// reset 
	int nImageSize = nRow * nCol;
	memset(YData,0x00,nImageSize);
	memset(UVData,0x00,nImageSize);


	// Compute Y
	// 18 X 22 blocks
	short block[64];
	int offset = 0;

	int nWidth = ( nCol >> 3 );
	int nHeigth = ( nRow >> 3 );
	for( i=0; i< nRow/8; i++)
	{
		for( j=0; j<nCol/8; j++)
		{
			// Y block
			memset(block,0,64*sizeof(short));
			m_decode.Huff_Decode_One_Block(block, 0);
			
			//zigzag
			m_dctquant.zigzag_i_trans(block);

			//quantization
			m_dctquant.j_i_y_quant(block);

			//DCT
			m_dctquant.j_rev_dct(block);
			
			for( m=0; m<8; m++)
			{
				for(n=0; n<8; n++)
				{
					if( block[m*8+n] > 120 )  block[m*8+n] = 120;
					if( block[m*8+n] < -120 )  block[m*8+n] = -120;


//					YData[i*8 +m][j*8 + n] = block[m*8 + n] + CENTERJSAMPLE;

//					printf("i=%d, j= %d, m= %d, n= %d, offset = %d\n",i,j,m,n,offset);

					YData[(i*8 +m)*nCol+(j*8 + n)] = block[m*8 + n] + CENTERJSAMPLE;

//					YData[offset] = block[m*8 + n] + CENTERJSAMPLE;

					offset ++;
				}
				offset += nCol;
			}

			offset += 8;
		}
		offset +=  ( nCol <<  3);
	}
	
	// compute UV
	offset = 0;
	for( i=0; i< nRow/8; i++)
	{
		for( j=0; j<nCol/8; j++)
		{
			// UV block
			memset(block,0,64*sizeof(short));
			m_decode.Huff_Decode_One_Block(block, 1);
			
			//zigzag
			m_dctquant.zigzag_i_trans(block);
			
			//quantization
			m_dctquant.j_i_uv_quant(block);

			//DCT
			m_dctquant.j_rev_dct(block);

			for( m=0; m<8; m++)
			{
				for(n=0; n<8; n++)
				{
//					UVData[i*8 +m][j*8 + n] = (unsigned char)block[m*8 + n];
					UVData[(i*8 +m)*nCol+(j*8 + n)] = (unsigned char)block[m*8 + n];
//					UVData[offset] = (unsigned char)block[m*8 + n];
					offset ++;
				}
				offset += nCol;
			}
			offset += 8;
		}
		offset += ( nCol << 3 ) ;

	}

	return true;

}


// Added by ELiu, size of lpRGB should be >= ( nRow * nCol * 3 )

typedef unsigned char UCHAR;

// assume nRow*nCol is even number
bool CJpegDecode::Decode(const char *lpbuffer, int nSize, unsigned char *lpRGB, int nRow, int nCol)
{

	int nImageSize = nRow * nCol;


	unsigned char *YData = new unsigned char[nImageSize];

	if ( YData == NULL ) return false;

	unsigned char *UVData = new unsigned char[nImageSize];

	if ( UVData == NULL )
	{
		delete [] YData;
		return false;
	}

// real rgb
	bool bRet = false;
	int nTmp;
	if( Decode(lpbuffer, nSize, YData, UVData, nRow, nCol))
	//if( 1)
	{
		// UYVY to RGB
#if 1
		unsigned char *lp = lpRGB;

		unsigned char Y,U,V;
		
		for (int i=0;i<nImageSize-1;i+=2)   // assure even number
		{
			Y = YData[i];
			U = UVData[i];
			V = UVData[i+1];

	
			nTmp = abs((int)(Y + 1.402*((UCHAR)V-128)));
			lp[0] = nTmp>255?255:nTmp;
			nTmp = abs((int)(Y - 0.3441*((UCHAR)U-128) - 0.7139 *((UCHAR)V-128)));
			lp[1] = nTmp>255?255:nTmp;
			nTmp = abs((int)(Y + 1.7718*((UCHAR)U-128) - 0.0012 *((UCHAR)V-128)));
			lp[2] = nTmp>255?255:nTmp;
			lp += 3;
			
			// next pixel
			Y = YData[i+1];

			nTmp = abs((int)(Y + 1.402*((UCHAR)V-128)));
			lp[0] = nTmp>255?255:nTmp;
			nTmp = abs((int)(Y - 0.3441*((UCHAR)U-128) - 0.7139 *((UCHAR)V-128)));
			lp[1] = nTmp>255?255:nTmp;
			nTmp = abs((int)(Y + 1.7718*((UCHAR)U-128) - 0.0012 *((UCHAR)V-128)));
			lp[2] = nTmp>255?255:nTmp;
			lp += 3;
			

		}
#endif


		bRet = true;
	}

	delete [] YData;
	delete [] UVData;

	return bRet;

}


bool CJpegDecode::DecodeImage(const char *szFileName, unsigned char **YData, unsigned char **UVData, int nRow, int nCol)
{
	int i,j,m,n;
	if( m_decode.Huff_Decode_ReadFile(szFileName) == 0 )  return false;
	for( i=0; i<nRow; i++)
	{
		for(j=0; j<nCol; j++)
		{
			YData[i][j] = 0;
			UVData[i][j] = 0;
		}
	}

	// 18 X 22 blocks
	short block[64];
	for( i=0; i< nRow/8; i++)
	{
		for( j=0; j<nCol/8; j++)
		{
			// Y block
			memset(block,0,64*sizeof(short));
			m_decode.Huff_Decode_One_Block(block, 0);
			
			//zigzag
			m_dctquant.zigzag_i_trans(block);

			//quantization
			m_dctquant.j_i_y_quant(block);

			//DCT
			m_dctquant.j_rev_dct(block);
			
			for( m=0; m<8; m++)
			{
				for(n=0; n<8; n++)
				{
					if( block[m*8+n] > 120 )  block[m*8+n] = 120;
					if( block[m*8+n] < -120 )  block[m*8+n] = -120;
					YData[i*8 +m][j*8 + n] = block[m*8 + n] + CENTERJSAMPLE;
				}
			}

		}
	}
	
	for( i=0; i< nRow/8; i++)
	{
		for( j=0; j<nCol/8; j++)
		{
			// UV block
			memset(block,0,64*sizeof(short));
			m_decode.Huff_Decode_One_Block(block, 1);
			
			//zigzag
			m_dctquant.zigzag_i_trans(block);
			
			//quantization
			m_dctquant.j_i_uv_quant(block);

			//DCT
			m_dctquant.j_rev_dct(block);

			for( m=0; m<8; m++)
			{
				for(n=0; n<8; n++)
				{
					UVData[i*8 +m][j*8 + n] = (unsigned char)block[m*8 + n];
				}
			}
		}
	}

	return true;
}



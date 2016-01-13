// JpegDecode.h: interface for the CJpegDecode class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_JPEGDECODE_H__CC39E04B_DC0E_4BA8_878B_8A9C13665DC0__INCLUDED_)
#define AFX_JPEGDECODE_H__CC39E04B_DC0E_4BA8_878B_8A9C13665DC0__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CJpegDecode  
{
public:
	CJpegDecode();
	virtual ~CJpegDecode();

	void SetQuality(int m_nQuality);
	bool DecodeInit(int m_nQuality);
	void DecodeDel();

	bool Decode(const char *lpbuffer, int nSize, unsigned char *lpRGB, int nRow, int nCol);
	bool Decode(const char *lpbuffer, int nSize, unsigned char *YData, unsigned char *UVData, int nRow, int nCol);
	bool DecodeImage(const char *szFileName, unsigned char **YData, unsigned char **UVData, int nRow = 144, int nCol = 176);

};

#endif // !defined(AFX_JPEGDECODE_H__CC39E04B_DC0E_4BA8_878B_8A9C13665DC0__INCLUDED_)

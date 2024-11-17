// For reduce rgb colors (but only for RGB, no alpha channel process)
// but alpha can easy added by small code modifications
#pragma once

#include "bsptypes.h"

unsigned char FixBounds(int i);
unsigned char FixBounds(unsigned int i);
unsigned char FixBounds(float i);
unsigned char FixBounds(double i);


class Quantizer
{
	typedef struct tagNode
	{
		bool bIsLeaf;
		unsigned int nPixelCount;
		unsigned int nRedSum;
		unsigned int nGreenSum;
		unsigned int nBlueSum;
		unsigned int nIndex;
		struct tagNode* pChild[8];
		struct tagNode* pNext;
	} Node;

protected:
	Node* m_pTree;
	unsigned int m_nLeafCount;
	Node* m_pReducibleNodes[256];
	size_t m_nMaxColors;
	unsigned char m_nColorBits;
	unsigned int m_lastIndex;
	COLOR3* m_pPalette;

public:
	Quantizer(unsigned int nMaxColors, unsigned char nColorBits);
	virtual ~Quantizer();
	void ProcessImage(COLOR3* image, unsigned int size);
	void ApplyColorTable(COLOR3* image, unsigned int size);
	void ApplyColorTableDither(COLOR3* image, int width, int height);
	void FloydSteinbergDither(COLOR3* image, int width, int height, unsigned int* target);
	void FloydSteinbergDither256(COLOR3* image, int width, int height, unsigned char* target);
	unsigned int GetColorCount();
	void GetColorTable(COLOR3* pal);
	void SetColorTable(COLOR3* pal, unsigned int colors);
	unsigned int GetNearestIndex(COLOR3 c, COLOR3* pal);
	unsigned int GetNearestIndexFast(COLOR3 c, COLOR3* pal);
	COLOR3 GetNearestColorFast(COLOR3 c, COLOR3* pal);
	unsigned int GetNearestIndexDither(COLOR3& color, COLOR3* pal);

protected:
	unsigned int GetLeafCount(Node* pTree);
	void GenColorTable();
	void AddColor(Node** ppNode, COLOR3 c, int nLevel, unsigned int* pLeafCount, Node** pReducibleNodes);
	void* CreateNode(int nLevel, unsigned int* pLeafCount, Node** pReducibleNodes);
	void ReduceTree(unsigned int* pLeafCount, Node** pReducibleNodes);
	void DeleteTree(Node** ppNode);
	void GetPaletteColors(Node* pTree, COLOR3* pal, unsigned int* pIndex, unsigned int* pSum);
	unsigned int GetNextBestLeaf(Node** pTree, unsigned int nLevel, COLOR3 c, COLOR3* pal);
	bool ColorsAreEqual(COLOR3 a, COLOR3 b);
};


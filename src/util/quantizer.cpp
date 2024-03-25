#include "quantizer.h"

unsigned char FixBounds(int i)
{
	if (i > 0xFF)
		return 0xFF;
	else if (i < 0x00)
		return 0x00;
	return (unsigned char)i;
}

unsigned char FixBounds(unsigned int i)
{
	if (i > 0xFF)
		return 0xFF;
	return (unsigned char)i;
}

unsigned char FixBounds(float i)
{
	if (i > (double)0xFF)
		return 0xFF;
	else if (i < (double)0x00)
		return 0x00;
	return (unsigned char)i;
}

unsigned char FixBounds(double i)
{
	if (i > (double)0xFF)
		return 0xFF;
	else if (i < (double)0x00)
		return 0x00;
	return (unsigned char)i;
}

Quantizer::Quantizer(unsigned int nMaxColors, unsigned char nColorBits)
{
	m_nColorBits = nColorBits;
	m_pTree = 0;
	m_nLeafCount = 0;
	m_lastIndex = 0;
	for (unsigned char i = 0; i <= m_nColorBits; i++)
		m_pReducibleNodes[i] = 0;
	m_nMaxColors = nMaxColors;
	m_pPalette = NULL;
}

Quantizer::~Quantizer()
{
	if (m_pTree)
		DeleteTree(&m_pTree);
	if (m_pPalette)
		delete[] m_pPalette;

	for (unsigned char i = 0; i <= m_nColorBits; i++)
		m_pReducibleNodes[i] = 0;
	m_nColorBits = 0;
	m_pTree = 0;
	m_nLeafCount = 0;
	m_lastIndex = 0;
	m_pPalette = NULL;
}

void Quantizer::ProcessImage(COLOR3* image, unsigned int size)
{
	if (m_pTree)
		DeleteTree(&m_pTree);

	for (unsigned char i = 0; i <= m_nColorBits; i++)
		m_pReducibleNodes[i] = 0;

	m_pTree = 0;
	m_nLeafCount = 0;
	m_lastIndex = 0;

	for (unsigned int i = 0; i < size; i++)
	{
		AddColor(&m_pTree, image[i], 0, &m_nLeafCount, m_pReducibleNodes);

		if (m_nLeafCount > m_nMaxColors)
			ReduceTree(&m_nLeafCount, m_pReducibleNodes);
	}

	while (m_nLeafCount > m_nMaxColors)
		ReduceTree(&m_nLeafCount, m_pReducibleNodes);


	if (m_pPalette)
		delete[] m_pPalette;

	m_pPalette = new COLOR3[m_nMaxColors];

	GenColorTable();
}

unsigned int Quantizer::GetNearestIndexDither(COLOR3& color, COLOR3* pal)
{
	unsigned int i, distanceSquared, minDistanceSquared, bestIndex = 0;
	minDistanceSquared = 255 * 255 + 255 * 255 + 255 * 255 + 1;
	for (i = 0; i < m_nLeafCount; i++) {
		int Rdiff = ((int)color.r) - pal[i].r;
		int Gdiff = ((int)color.g) - pal[i].g;
		int Bdiff = ((int)color.b) - pal[i].b;
		distanceSquared = Rdiff * Rdiff + Gdiff * Gdiff + Bdiff * Bdiff;
		if (distanceSquared < minDistanceSquared) {
			minDistanceSquared = distanceSquared;
			bestIndex = i;
		}
	}
	return bestIndex;
}

bool Quantizer::ColorsAreEqual(COLOR3 a, COLOR3 b)
{
	return (a.r == b.r && a.g == b.g && a.b == b.b);
}

unsigned int Quantizer::GetNearestIndex(COLOR3 c, COLOR3* pal)
{
	if (!pal) return 0;
	if (ColorsAreEqual(c, pal[m_lastIndex]))
		return m_lastIndex;
	unsigned int cur = 0;
	for (unsigned int i = 0, k = 0, distance = 2147483647; i < m_nLeafCount; i++)
	{
		k = (unsigned int)((pal[i].r - c.r) * (pal[i].r - c.r) + (pal[i].g - c.g) * (pal[i].g - c.g) + (pal[i].b - c.b) * (pal[i].b - c.b));
		if (k <= 0)
		{
			m_lastIndex = i;
			return i;
		}
		if (k < distance)
		{
			distance = k;
			cur = i;
		}
	}
	m_lastIndex = cur;
	return m_lastIndex;
}

unsigned int Quantizer::GetNearestIndexFast(COLOR3 c, COLOR3* pal)
{
	if (m_nMaxColors<16 && m_nLeafCount>m_nMaxColors)
		return GetNearestIndex(c, pal);
	if (!pal) return 0;
	if (ColorsAreEqual(c, pal[m_lastIndex]))
		return m_lastIndex;
	m_lastIndex = GetNextBestLeaf(&m_pTree, 0, c, pal);
	return m_lastIndex;
}

COLOR3 Quantizer::GetNearestColorFast(COLOR3 c, COLOR3* pal)
{
	return pal[GetNearestIndexFast(c, pal)];
}


void Quantizer::FloydSteinbergDither(COLOR3* image, int width, int height, unsigned int* target)
{
	for (int y = 0; y < height; y++)
	{
		if (y % 2 == 1)
		{
			for (int x = 0; x < width; x++)
			{
				int i = width * (height - y - 1) + x;
				int j = width * y + x;
				unsigned int k = GetNearestIndexFast(image[j], m_pPalette);

				target[i] = k;

				int diff[3];
				diff[0] = image[j].r - m_pPalette[k].r;
				diff[1] = image[j].g - m_pPalette[k].g;
				diff[2] = image[j].b - m_pPalette[k].b;

				if (y < height - 1)
				{
					image[j + width].r = FixBounds(image[j + width].r + (diff[0] * 5) / 16);
					image[j + width].g = FixBounds(image[j + width].g + (diff[1] * 5) / 16);
					image[j + width].b = FixBounds(image[j + width].b + (diff[2] * 5) / 16);
					if (x > 0)
					{
						image[j + (width - 1)].r = FixBounds(image[j + (width - 1)].r + (diff[0] * 3) / 16);
						image[j + (width - 1)].g = FixBounds(image[j + (width - 1)].g + (diff[1] * 3) / 16);
						image[j + (width - 1)].b = FixBounds(image[j + (width - 1)].b + (diff[2] * 3) / 16);
					}
					if (x < width - 1)
					{
						image[j + width + 1].r = FixBounds(image[j + width + 1].r + (diff[0] * 1) / 16);
						image[j + width + 1].g = FixBounds(image[j + width + 1].g + (diff[1] * 1) / 16);
						image[j + width + 1].b = FixBounds(image[j + width + 1].b + (diff[2] * 1) / 16);
					}
				}
				if (x < width - 1)
				{
					image[j + 1].r = FixBounds(image[j + 1].r + (diff[0] * 7) / 16);
					image[j + 1].g = FixBounds(image[j + 1].g + (diff[1] * 7) / 16);
					image[j + 1].b = FixBounds(image[j + 1].b + (diff[2] * 7) / 16);
				}

			}
		}
		else
		{
			for (int x = width - 1; x >= 0; x--)
			{
				int i = width * (height - y - 1) + x;
				int j = width * y + x;
				unsigned int k = GetNearestIndexFast(image[j], m_pPalette);
				target[i] = k;
				int diff[3];
				diff[0] = image[j].r - m_pPalette[k].r;
				diff[1] = image[j].g - m_pPalette[k].g;
				diff[2] = image[j].b - m_pPalette[k].b;


				if (y < height - 1)
				{
					image[j + width].r = FixBounds(image[j + width].r + (diff[0] * 5) / 16);
					image[j + width].g = FixBounds(image[j + width].g + (diff[1] * 5) / 16);
					image[j + width].b = FixBounds(image[j + width].b + (diff[2] * 5) / 16);
					if (x > 0)
					{
						image[j + (width - 1)].r = FixBounds(image[j + (width - 1)].r + (diff[0] * 3) / 16);
						image[j + (width - 1)].g = FixBounds(image[j + (width - 1)].g + (diff[1] * 3) / 16);
						image[j + (width - 1)].b = FixBounds(image[j + (width - 1)].b + (diff[2] * 3) / 16);
					}
					if (x < width - 1)
					{
						image[j + width + 1].r = FixBounds(image[j + width + 1].r + (diff[0] * 1) / 16);
						image[j + width + 1].g = FixBounds(image[j + width + 1].g + (diff[1] * 1) / 16);
						image[j + width + 1].b = FixBounds(image[j + width + 1].b + (diff[1] * 1) / 16);
					}
				}
				if (x > 0)
				{
					image[j - 1].r = FixBounds(image[j - 1].r + (diff[0] * 7) / 16);
					image[j - 1].g = FixBounds(image[j - 1].g + (diff[1] * 7) / 16);
					image[j - 1].b = FixBounds(image[j - 1].b + (diff[2] * 7) / 16);
				}

			}
		}
	}
}

void Quantizer::FloydSteinbergDither256(COLOR3* image, int width, int height, unsigned char* target)
{
	for (int y = 0; y < height; y++)
	{
		if (y % 2 == 1)
		{
			for (int x = 0; x < width; x++)
			{
				int i = width * (height - y - 1) + x;
				int j = width * y + x;
				unsigned char k = FixBounds(GetNearestIndexFast(image[j], m_pPalette));

				target[i] = k;

				int diff[3];
				diff[0] = image[j].r - m_pPalette[k].r;
				diff[1] = image[j].g - m_pPalette[k].g;
				diff[2] = image[j].b - m_pPalette[k].b;

				if (y < height - 1)
				{
					image[j + width].r = FixBounds(image[j + width].r + (diff[0] * 5) / 16);
					image[j + width].g = FixBounds(image[j + width].g + (diff[1] * 5) / 16);
					image[j + width].b = FixBounds(image[j + width].b + (diff[2] * 5) / 16);
					if (x > 0)
					{
						image[j + (width - 1)].r = FixBounds(image[j + (width - 1)].r + (diff[0] * 3) / 16);
						image[j + (width - 1)].g = FixBounds(image[j + (width - 1)].g + (diff[1] * 3) / 16);
						image[j + (width - 1)].b = FixBounds(image[j + (width - 1)].b + (diff[2] * 3) / 16);
					}
					if (x < width - 1)
					{
						image[j + width + 1].r = FixBounds(image[j + width + 1].r + (diff[0] * 1) / 16);
						image[j + width + 1].g = FixBounds(image[j + width + 1].g + (diff[1] * 1) / 16);
						image[j + width + 1].b = FixBounds(image[j + width + 1].b + (diff[2] * 1) / 16);
					}
				}
				if (x < width - 1)
				{
					image[j + 1].r = FixBounds(image[j + 1].r + (diff[0] * 7) / 16);
					image[j + 1].g = FixBounds(image[j + 1].g + (diff[1] * 7) / 16);
					image[j + 1].b = FixBounds(image[j + 1].b + (diff[2] * 7) / 16);
				}

			}
		}
		else
		{
			for (int x = width - 1; x > 0; x--)
			{
				int i = width * (height - y - 1) + x;
				int j = width * y + x;
				unsigned char k = FixBounds(GetNearestIndexFast(image[j], m_pPalette));
				target[i] = k;
				int diff[3];
				diff[0] = image[j].r - m_pPalette[k].r;
				diff[1] = image[j].g - m_pPalette[k].g;
				diff[2] = image[j].b - m_pPalette[k].b;


				if (y < height - 1)
				{
					image[j + width].r = FixBounds(image[j + width].r + (diff[0] * 5) / 16);
					image[j + width].g = FixBounds(image[j + width].g + (diff[1] * 5) / 16);
					image[j + width].b = FixBounds(image[j + width].b + (diff[2] * 5) / 16);
					if (x < width - 1)
					{
						image[j + width + 1].r = FixBounds(image[j + width + 1].r + (diff[0] * 1) / 16);
						image[j + width + 1].g = FixBounds(image[j + width + 1].g + (diff[1] * 1) / 16);
						image[j + width + 1].b = FixBounds(image[j + width + 1].b + (diff[2] * 1) / 16);
					}
				}
				if (x < width - 1)
				{
					image[j + 1].r = FixBounds(image[j + 1].r + (diff[0] * 7) / 16);
					image[j + 1].g = FixBounds(image[j + 1].g + (diff[1] * 7) / 16);
					image[j + 1].b = FixBounds(image[j + 1].b + (diff[2] * 7) / 16);
				}
			}
		}
	}
}

void Quantizer::AddColor(Node** ppNode, COLOR3 c, int nLevel, unsigned int* pLeafCount, Node** pReducibleNodes)
{
	if (!(*ppNode))
		*ppNode = (Node*)CreateNode(nLevel, pLeafCount, pReducibleNodes);
	if ((*ppNode)->bIsLeaf)
	{
		(*ppNode)->nPixelCount++;
		(*ppNode)->nRedSum += c.r;
		(*ppNode)->nGreenSum += c.g;
		(*ppNode)->nBlueSum += c.b;
	}
	else
	{
		static unsigned char mask[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
		int	shift = 7 - (int)nLevel;
		int	nIndex = (((c.r & mask[nLevel]) >> shift) << 2) | (((c.g & mask[nLevel]) >> shift) << 1) | ((c.b & mask[nLevel]) >> shift);
		AddColor(&((*ppNode)->pChild[nIndex]), c, nLevel + 1, pLeafCount, pReducibleNodes);
	}
}

void* Quantizer::CreateNode(int nLevel, unsigned int* pLeafCount, Node** pReducibleNodes)
{
	Node* pNode = new Node();
	pNode->bIsLeaf = ((unsigned int)nLevel == m_nColorBits) ? true : false;
	pNode->nIndex = 0;
	if (pNode->bIsLeaf) (*pLeafCount)++;
	else
	{
		pNode->pNext = pReducibleNodes[nLevel];
		pReducibleNodes[nLevel] = pNode;
	}
	return pNode;
}

void Quantizer::ReduceTree(unsigned int* pLeafCount, Node** pReducibleNodes)
{
	unsigned char i = m_nColorBits - 1;
	for (; (i > 0) && (!pReducibleNodes[i]); i--);
	if (!pReducibleNodes[i]) return;
	Node* pNode = pReducibleNodes[i];
	pReducibleNodes[i] = pNode->pNext;

	unsigned int nRedSum = 0;
	unsigned int nGreenSum = 0;
	unsigned int nBlueSum = 0;
	unsigned int nChildren = 0;

	for (i = 0; i < 8; i++)
	{
		if (pNode->pChild[i])
		{
			nRedSum += pNode->pChild[i]->nRedSum;
			nGreenSum += pNode->pChild[i]->nGreenSum;
			nBlueSum += pNode->pChild[i]->nBlueSum;
			pNode->nPixelCount += pNode->pChild[i]->nPixelCount;
			delete(pNode->pChild[i]);
			pNode->pChild[i] = 0;
			nChildren++;
		}
	}

	pNode->bIsLeaf = true;
	pNode->nRedSum = nRedSum;
	pNode->nGreenSum = nGreenSum;
	pNode->nBlueSum = nBlueSum;
	*pLeafCount -= nChildren - 1;
}

void Quantizer::DeleteTree(Node** ppNode)
{
	for (int i = 0; i < 8; i++)
	{
		if ((*ppNode)->pChild[i]) DeleteTree(&((*ppNode)->pChild[i]));
	}
	delete(*ppNode);
	*ppNode = 0;
}

void Quantizer::GetPaletteColors(Node* pTree, COLOR3* pal, unsigned int* pIndex, unsigned int* pSum)
{
	if (pTree)
	{
		if (pTree->bIsLeaf)
		{
			pal[*pIndex].r = (unsigned char)((pTree->nRedSum) / (pTree->nPixelCount));
			pal[*pIndex].g = (unsigned char)((pTree->nGreenSum) / (pTree->nPixelCount));
			pal[*pIndex].b = (unsigned char)((pTree->nBlueSum) / (pTree->nPixelCount));
			pTree->nIndex = *pIndex;
			if (pSum)
				pSum[*pIndex] = pTree->nPixelCount;
			(*pIndex)++;
		}
		else
		{
			for (int i = 0; i < 8; i++)
			{
				if (pTree->pChild[i])
					GetPaletteColors(pTree->pChild[i], pal, pIndex, pSum);
			}
		}
	}
}

unsigned int Quantizer::GetLeafCount(Node* pTree)
{
	if (pTree)
	{
		if (pTree->bIsLeaf)
		{
			return 1;
		}
		else
		{
			unsigned int sum = 0;
			for (int i = 0; i < 8; i++)
			{
				if (pTree->pChild[i])
					sum += GetLeafCount(pTree->pChild[i]);
			}
			return sum;
		}
	}
	return 0;
}

unsigned int Quantizer::GetNextBestLeaf(Node** pTree, unsigned int nLevel, COLOR3 c, COLOR3* pal)
{
	if ((*pTree)->bIsLeaf)
	{
		return (*pTree)->nIndex;
	}
	else
	{
		static unsigned char mask[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
		int	shift = 7 - (int)nLevel;
		int	nIndex = (((c.r & mask[nLevel]) >> shift) << 2) | (((c.g & mask[nLevel]) >> shift) << 1) | ((c.b & mask[nLevel]) >> shift);
		if ((*pTree)->pChild[nIndex])
			return GetNextBestLeaf(&((*pTree)->pChild[nIndex]), nLevel + 1, c, pal);
		else
			return GetNearestIndex(c, pal);
	}
}

unsigned int Quantizer::GetColorCount()
{
	return m_nLeafCount;
}

void Quantizer::SetColorTable(COLOR3* pal, unsigned int colors)
{
	if (m_pTree)
		DeleteTree(&m_pTree);

	if (m_pPalette)
		delete[] m_pPalette;

	*this = Quantizer(colors, m_nColorBits);

	m_pPalette = new COLOR3[colors];
	memcpy(m_pPalette, pal, colors * sizeof(COLOR3));

	for (unsigned int i = 0; i < colors; i++)
	{
		AddColor(&m_pTree, pal[i], 0, &m_nLeafCount, m_pReducibleNodes);
	}

	m_nMaxColors = colors;
}

void Quantizer::GetColorTable(COLOR3* pal)
{
	memcpy(pal, m_pPalette, m_nMaxColors * sizeof(COLOR3));
}

void Quantizer::GenColorTable()
{
	if (!m_pPalette)
		return;
	unsigned int nIndex = 0;
	if (m_nMaxColors<16 && m_nLeafCount>m_nMaxColors)
	{
		unsigned int nSum[16];
		COLOR3 tmppal[16];
		GetPaletteColors(m_pTree, tmppal, &nIndex, nSum);
		unsigned int j, k, nr, ng, nb, ns, a, b;
		for (j = 0; j < m_nMaxColors; j++)
		{
			a = (j * m_nLeafCount) / m_nMaxColors;
			b = ((j + 1) * m_nLeafCount) / m_nMaxColors;
			nr = ng = nb = ns = 0;
			for (k = a; k < b; k++)
			{
				nr += tmppal[k].r * nSum[k];
				ng += tmppal[k].g * nSum[k];
				nb += tmppal[k].b * nSum[k];
				ns += nSum[k];
			}
			if (ns != 0)
			{
				m_pPalette[j].r = FixBounds((int)(nr / ns));
				m_pPalette[j].g = FixBounds((int)(ng / ns));
				m_pPalette[j].b = FixBounds((int)(nb / ns));
			}
		}
	}
	else
	{
		GetPaletteColors(m_pTree, m_pPalette, &nIndex, 0);
	}
}

void Quantizer::ApplyColorTable(COLOR3* image, unsigned int size)
{
	if (!m_pPalette)
		ProcessImage(image, size);

	for (unsigned int i = 0; i < size; i++)
	{
		image[i] = m_pPalette[GetNearestIndexFast(image[i], m_pPalette)];
	}
}

void Quantizer::ApplyColorTableDither(COLOR3* image, int width, int height)
{
	if (!m_pPalette)
		ProcessImage(image, width * height);
	for (unsigned int i = 0; i < width * height; i++)
	{
		image[i] = m_pPalette[GetNearestIndexDither(image[i], m_pPalette)];
	}
	/*unsigned int* tmpcolorarray = new unsigned int[width * height];
	FloydSteinbergDither((COLOR3*)image, width, height, tmpcolorarray);
	for (int i = 0; i < width * height; i++)
	{
		image[i] = m_pPalette[tmpcolorarray[i]];
	}
	delete[] tmpcolorarray;*/
}

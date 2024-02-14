#include "lang.h"
#include <iostream>
#include <fstream>
#include <string.h>
#include "Wad.h"
#include "util.h"
#include "Settings.h"
#include "Renderer.h"

Wad::Wad(void)
{
	dirEntries.clear();
	if (filedata)
		delete[] filedata;
	filedata = NULL;
}

Wad::Wad(const std::string& file)
{
	this->filename = file;
	this->wadname = basename(file);
	dirEntries.clear();
	if (filedata)
		delete[] filedata;
	filedata = NULL;/*

	if (fileExists(file))
	{
		readInfo();
	}*/
}

Wad::~Wad(void)
{
	dirEntries.clear();
	if (filedata)
		delete[] filedata;
	filedata = NULL;
}

void W_CleanupName(const char* in, char* out)
{
	int	i;

	for (i = 0; i < MAXTEXTURENAME; i++) {
		char		c;
		c = in[i];
		if (!c)
			break;

		if (c >= 'A' && c <= 'Z')
			c += ('a' - 'A');
		out[i] = c;
	}

	for (; i < MAXTEXTURENAME; i++)
		out[i] = 0;
}


bool Wad::readInfo()
{
	std::string file = filename;

	if (!fileExists(file))
	{
		print_log(get_localized_string(LANG_0247),filename);
		return false;
	}

	filedata = (unsigned char*)loadFile(file, fileLen);

	if (!filedata)
	{
		print_log(get_localized_string(LANG_1043),filename);
		filedata = NULL;
		return false;
	}

	if (fileLen < sizeof(WADHEADER))
	{
		delete[] filedata;
		filedata = NULL;
		print_log(get_localized_string(LANG_0248),filename);
		return false;
	}

	int offset = 0;

	memcpy((char*)&header, &filedata[offset], sizeof(WADHEADER));

	if (std::string(header.szMagic).find("WAD3") != 0)
	{
		delete[] filedata;
		filedata = NULL;
		print_log(get_localized_string(LANG_0249),filename);
		return false;
	}

	if (header.nDirOffset >= (int)fileLen)
	{
		delete[] filedata;
		filedata = NULL;
		print_log(get_localized_string(LANG_0250),filename);
		return false;
	}

	//
	// WAD DIRECTORY ENTRIES
	//
	offset = header.nDirOffset;

	dirEntries.clear();

	usableTextures = false;

	//print_log("D {} {}\n", header.nDirOffset, header.nDir);

	for (int i = 0; i < header.nDir; i++)
	{
		WADDIRENTRY tmpWadEntry = WADDIRENTRY();

		if (offset + (int)sizeof(WADDIRENTRY) > fileLen)
		{
			print_log(get_localized_string(LANG_0251));
			break;
		}

		memcpy((char*)&tmpWadEntry, &filedata[offset], sizeof(WADDIRENTRY));
		offset += sizeof(WADDIRENTRY);

		W_CleanupName(tmpWadEntry.szName, tmpWadEntry.szName);

		dirEntries.push_back(tmpWadEntry);

		if (dirEntries[i].nType == 0x43) usableTextures = true;
	}


	if (!usableTextures)
	{
		print_log(get_localized_string(LANG_0252), basename(filename));
		if (!dirEntries.size())
		{
			delete[] filedata;
			filedata = NULL;
			return false;
		}
	}

	return true;
}

bool Wad::hasTexture(const std::string& texname)
{
	for (int d = 0; d < header.nDir; d++)
		if (strcasecmp(texname.c_str(), dirEntries[d].szName) == 0)
			return true;
	return false;
}

bool Wad::hasTexture(size_t dirIndex)
{
	if (dirIndex >= (int)dirEntries.size())
	{
		return false;
	}
	return true;
}

WADTEX* Wad::readTexture(size_t dirIndex, int* texturetype)
{
	if (dirIndex >= (int)dirEntries.size())
	{
		print_log(get_localized_string(LANG_0253));
		return NULL;
	}
	//if (cache != NULL)
		//return cache[dirIndex];
	std::string name = std::string(dirEntries[dirIndex].szName);
	return readTexture(name, texturetype);
}

WADTEX* Wad::readTexture(const std::string& texname, int* texturetype)
{
	int idx = -1;
	for (int d = 0; d < header.nDir; d++)
	{
		if (strcasecmp(texname.c_str(), dirEntries[d].szName) == 0)
		{
			idx = d;
			break;
		}
	}

	if (idx < 0)
	{
		return NULL;
	}

	if (dirEntries[idx].bCompression)
	{
		print_log(get_localized_string(LANG_0254));
		return NULL;
	}

	int offset = dirEntries[idx].nFilePos;

	if (texturetype)
	{
		*texturetype = dirEntries[idx].nType;
	}

	BSPMIPTEX mtex = BSPMIPTEX();
	memcpy((char*)&mtex, &filedata[offset], sizeof(BSPMIPTEX));
	offset += sizeof(BSPMIPTEX);
	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0255),mtex.szName,mtex.nWidth,mtex.nHeight);
	int w = mtex.nWidth;
	int h = mtex.nHeight;
	int sz = w * h;	   // miptex 0
	int sz2 = sz / 4;  // miptex 1
	int sz3 = sz2 / 4; // miptex 2
	int sz4 = sz3 / 4; // miptex 3
	int szAll = sz + sz2 + sz3 + sz4 + sizeof(short) + /*pal size*/ sizeof(COLOR3) * 256;

	unsigned char* data = new unsigned char[szAll];/* 4 bytes padding */

	memset(data, 0, szAll);

	memcpy(data, &filedata[offset], szAll);

	WADTEX* tex = new WADTEX();
	memcpy(tex->szName, mtex.szName, MAXTEXTURENAME);
	for (int i = 0; i < MIPLEVELS; i++)
		tex->nOffsets[i] = mtex.nOffsets[i];
	tex->nWidth = mtex.nWidth;
	tex->nHeight = mtex.nHeight;
	tex->data = data;
	tex->dataLen = szAll;
	tex->needclean = true;
	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0256),tex->szName,tex->nWidth,tex->nHeight);
	return tex;
}

bool Wad::write(WADTEX** textures, size_t numTex)
{
	std::vector<WADTEX*> textList = std::vector<WADTEX*>(&textures[0], &textures[numTex]);
	return write(filename, textList);
}

bool Wad::write(std::vector<WADTEX*> textures)
{
	return write(filename, textures);
}

bool Wad::write(const std::string& _filename, std::vector<WADTEX*> textures)
{
	this->filename = _filename;

	std::ofstream myFile(filename, std::ios::trunc | std::ios::binary);

	header.szMagic[0] = 'W';
	header.szMagic[1] = 'A';
	header.szMagic[2] = 'D';
	header.szMagic[3] = '3';
	header.nDir = (int)textures.size();

	size_t tSize = sizeof(BSPMIPTEX) * textures.size();
	for (size_t i = 0; i < textures.size(); i++)
	{
		int w = textures[i]->nWidth;
		int h = textures[i]->nHeight;
		int sz = w * h;	   // miptex 0
		int sz2 = sz / 4;  // miptex 1
		int sz3 = sz2 / 4; // miptex 2
		int sz4 = sz3 / 4; // miptex 3
		int szAll = sz + sz2 + sz3 + sz4 + sizeof(short) + /* pal num */ sizeof(COLOR3) * 256;

		szAll = (szAll + 3) & ~3; // 4 bytes padding

		tSize += szAll;
	}

	if (tSize > 0)
	{
		header.nDirOffset = (int)(sizeof(WADHEADER) + tSize);
		myFile.write((char*)&header, sizeof(WADHEADER));

		for (size_t i = 0; i < textures.size(); i++)
		{
			BSPMIPTEX miptex = BSPMIPTEX();
			memcpy(miptex.szName, textures[i]->szName, MAXTEXTURENAME);

			int w = textures[i]->nWidth;
			int h = textures[i]->nHeight;
			int sz = w * h;	   // miptex 0
			int sz2 = sz / 4;  // miptex 1
			int sz3 = sz2 / 4; // miptex 2
			int sz4 = sz3 / 4; // miptex 3
			int szAll = sz + sz2 + sz3 + sz4 + sizeof(short) /* pal num*/ + sizeof(COLOR3) * 256;

			int padding = ((szAll + 3) & ~3) - szAll; // 4 bytes padding
			
			miptex.nWidth = w;
			miptex.nHeight = h;
			miptex.nOffsets[0] = sizeof(BSPMIPTEX);
			miptex.nOffsets[1] = sizeof(BSPMIPTEX) + sz;
			miptex.nOffsets[2] = sizeof(BSPMIPTEX) + sz + sz2;
			miptex.nOffsets[3] = sizeof(BSPMIPTEX) + sz + sz2 + sz3;

			myFile.write((char*)&miptex, sizeof(BSPMIPTEX));

			// 256 palette
			((unsigned char*)textures[i]->data)[sz + sz2 + sz3 + sz4] = 0x00;
			((unsigned char*)textures[i]->data)[sz + sz2 + sz3 + sz4 + 1] = 0x01;

			myFile.write((char*)textures[i]->data, szAll);
			if (padding > 0)
			{
				unsigned char* zeropad = new unsigned char[padding];
				memset(zeropad, 0, padding);
				myFile.write((const char *)zeropad, padding);
				delete[] zeropad;
			}
		}

		int offset = sizeof(WADHEADER);
		for (size_t i = 0; i < textures.size(); i++)
		{
			WADDIRENTRY entry = WADDIRENTRY();
			entry.nFilePos = offset;
			int w = textures[i]->nWidth;
			int h = textures[i]->nHeight;
			int sz = w * h;	   // miptex 0
			int sz2 = sz / 4;  // miptex 1
			int sz3 = sz2 / 4; // miptex 2
			int sz4 = sz3 / 4; // miptex 3
			int szAll = sz + sz2 + sz3 + sz4 + sizeof(short) + /* pal num */ sizeof(COLOR3) * 256;

			szAll = (szAll + 3) & ~3; // 4 bytes padding

			entry.nDiskSize = szAll + sizeof(BSPMIPTEX);
			entry.nSize = szAll + sizeof(BSPMIPTEX);
			entry.nType = 0x43; // Texture
			entry.bCompression = false;
			entry.nDummy = 0;

			for (int k = 0; k < MAXTEXTURENAME; k++)
				memcpy(entry.szName, textures[i]->szName, MAXTEXTURENAME);

			offset += szAll + sizeof(BSPMIPTEX);

			myFile.write((char*)&entry, sizeof(WADDIRENTRY));
		}
	}
	else
	{
		header.nDirOffset = 0;
		myFile.write((char*)&header, sizeof(WADHEADER));
	}

	myFile.close();

	return true;
}

WADTEX* create_wadtex(const char* name, COLOR3* rgbdata, int width, int height)
{
	if (!name)
		return NULL;
	COLOR3 palette[256];
	memset(&palette, 0, sizeof(COLOR3) * 256);
	unsigned char* mip[MIPLEVELS] = { NULL };

	COLOR3* src = rgbdata;
	int colorCount = 0;

	// create pallete and full-rez mipmap
	mip[0] = new unsigned char[width * height];

	bool do_magic = false;
	if ( name[0] == '{' )
	{
		int sz = width * height;
		for (int i = 0; i < sz; i++)
		{
			if (rgbdata[i] == COLOR3(0, 0, 255))
			{
				do_magic = true;
				break;
			}
		}
		if (do_magic)
		{
			colorCount++;
			palette[0] = COLOR3(0, 0, 255);
		}
	}

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			int paletteIdx = -1;
			for (int k = 0; k < colorCount; k++)
			{
				if (*src == palette[k])
				{
					paletteIdx = k;
					break;
				}
			}
			if (paletteIdx == -1)
			{
				if (colorCount >= 256)
				{
					print_log(get_localized_string(LANG_1044));
					delete[] mip[0];
					return NULL;
				}
				palette[colorCount] = *src;
				paletteIdx = colorCount;
				colorCount++;
			}

			if (do_magic)
			{
				if (paletteIdx == 0)
				{
					mip[0][y * width + x] = (unsigned char)255;
				}
				else if (paletteIdx == 255)
				{
					mip[0][y * width + x] = (unsigned char)0;
				}
				else
				{
					mip[0][y * width + x] = (unsigned char)paletteIdx;
				}
			}
			else
			{
				mip[0][y * width + x] = (unsigned char)paletteIdx;
			}
			src++;
		}
	}

	if (do_magic)
	{
		std::swap(palette[0], palette[255]);
	}

	int texDataSize = width * height + sizeof(short) /* pal num*/ + sizeof(COLOR3) * 256;

	// generate mipmaps
	for (int i = 1; i < MIPLEVELS; i++)
	{
		int div = 1 << i;
		int mipWidth = width / div;
		int mipHeight = height / div;
		texDataSize += mipWidth * mipHeight;
		mip[i] = new unsigned char[texDataSize];

		src = rgbdata;
		for (int y = 0; y < mipHeight; y++)
		{
			for (int x = 0; x < mipWidth; x++)
			{
				int paletteIdx = -1;
				for (int k = 0; k < colorCount; k++)
				{
					if (*src == palette[k])
					{
						paletteIdx = k;
						break;
					}
				}

				mip[i][y * mipWidth + x] = (unsigned char)paletteIdx;
				src += div;
			}
		}
	}

	size_t newTexLumpSize = sizeof(BSPMIPTEX) + texDataSize;

	newTexLumpSize = ((newTexLumpSize + 3) & ~3);

	unsigned char* newTexData = new unsigned char[newTexLumpSize];
	memset(newTexData, 0, newTexLumpSize);

	WADTEX* newMipTex = new WADTEX();
	newMipTex->nWidth = width;
	newMipTex->nHeight = height;

	memcpy(newMipTex->szName, name, MAXTEXTURENAME);

	newMipTex->nOffsets[0] = sizeof(BSPMIPTEX);
	newMipTex->nOffsets[1] = newMipTex->nOffsets[0] + width * height;
	newMipTex->nOffsets[2] = newMipTex->nOffsets[1] + (width >> 1) * (height >> 1);
	newMipTex->nOffsets[3] = newMipTex->nOffsets[2] + (width >> 2) * (height >> 2);

	unsigned char* palleteOffset = newTexData + newMipTex->nOffsets[3] + (width >> 3) * (height >> 3) - sizeof(BSPMIPTEX);
	memcpy(newTexData + newMipTex->nOffsets[0], mip[0], width * height);
	memcpy(newTexData + newMipTex->nOffsets[1], mip[1], (width >> 1) * (height >> 1));
	memcpy(newTexData + newMipTex->nOffsets[2], mip[2], (width >> 2) * (height >> 2));
	memcpy(newTexData + newMipTex->nOffsets[3], mip[3], (width >> 3) * (height >> 3));
	memcpy(palleteOffset, palette, sizeof(COLOR3) * 256);

	*(unsigned short*)palleteOffset = 256;
	memcpy(palleteOffset + 2, palette, sizeof(COLOR3) * 256);

	newMipTex->data = newTexData;
	newMipTex->needclean = true;

	return newMipTex;
}

COLOR3* ConvertWadTexToRGB(WADTEX* wadTex, COLOR3* palette)
{
	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0257),wadTex->szName,wadTex->nWidth,wadTex->nHeight);
	int lastMipSize = (wadTex->nWidth >> 3) * (wadTex->nHeight >> 3);
	if (palette == NULL)
		palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + sizeof(short) - sizeof(BSPMIPTEX));
	unsigned char* src = wadTex->data;

	int sz = wadTex->nWidth * wadTex->nHeight;
	COLOR3* imageData = new COLOR3[sz];


	for (int k = 0; k < sz; k++)
	{
		imageData[k] = palette[src[k]];
	}

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0258),wadTex->szName,wadTex->nWidth,wadTex->nHeight);
	return imageData;
}

COLOR3* ConvertMipTexToRGB(BSPMIPTEX* tex, COLOR3* palette)
{
	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0259),tex->szName,tex->nWidth,tex->nHeight);
	int lastMipSize = (tex->nWidth >> 3) * (tex->nHeight >> 3);

	if (palette == NULL)
		palette = (COLOR3*)(((unsigned char*)tex) + tex->nOffsets[3] + lastMipSize + 2);
	unsigned char* src = (unsigned char*)(((unsigned char*)tex) + tex->nOffsets[0]);

	int sz = tex->nWidth * tex->nHeight;
	COLOR3* imageData = new COLOR3[sz];

	for (int k = 0; k < sz; k++)
	{
		imageData[k] = palette[src[k]];
	}

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0260),tex->szName,tex->nWidth,tex->nHeight);
	return imageData;
}


COLOR4* ConvertWadTexToRGBA(WADTEX* wadTex, COLOR3* palette, int colors)
{
	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0261),wadTex->szName,wadTex->nWidth,wadTex->nHeight);
	int lastMipSize = (wadTex->nWidth >> 3) * (wadTex->nHeight >> 3);

	if (palette == NULL)
		palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + sizeof(short) - sizeof(BSPMIPTEX));
	unsigned char* src = wadTex->data;

	int sz = wadTex->nWidth * wadTex->nHeight;
	COLOR4* imageData = new COLOR4[sz];

	for (int k = 0; k < sz; k++)
	{
		if (wadTex->szName[0] == '{' && (colors - 1 == src[k] || palette[src[k]] == COLOR3(0, 0, 255)))
		{
			imageData[k] = COLOR4(255, 255, 255, 0);
		}
		else
		{
			imageData[k] = palette[src[k]];
		}
	}

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0262),wadTex->szName,wadTex->nWidth,wadTex->nHeight);
	return imageData;
}

COLOR4* ConvertMipTexToRGBA(BSPMIPTEX* tex, COLOR3* palette, int colors)
{
	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0263),tex->szName,tex->nWidth,tex->nHeight);
	int lastMipSize = (tex->nWidth >> 3) * (tex->nHeight >> 3);

	if (palette == NULL)
		palette = (COLOR3*)(((unsigned char*)tex) + tex->nOffsets[3] + lastMipSize + 2);
	unsigned char* src = (unsigned char*)(((unsigned char*)tex) + tex->nOffsets[0]);

	int sz = tex->nWidth * tex->nHeight;
	COLOR4* imageData = new COLOR4[sz];

	for (int k = 0; k < sz; k++)
	{
		if (tex->szName[0] == '{'  && (colors - 1 == src[k] || palette[src[k]] == COLOR3(0, 0, 255)))
		{
			imageData[k] = COLOR4(0, 0, 0, 0);
		}
		else
		{
			imageData[k] = palette[src[k]];
		}
	}

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0264),tex->szName,tex->nWidth,tex->nHeight);
	return imageData;
}

COLOR3 GetMipTexAplhaColor(BSPMIPTEX* tex, COLOR3* palette, int max_colors)
{
	int lastMipSize = (tex->nWidth >> 3) * (tex->nHeight >> 3);
	if (palette == NULL)
	{
		max_colors = *(unsigned short*)(((unsigned char*)tex) + tex->nOffsets[3] + lastMipSize);
		palette = (COLOR3*)(((unsigned char*)tex) + tex->nOffsets[3] + lastMipSize + 2);
	}
	if (max_colors > 256 || max_colors < 0)
	{
		max_colors = 256;
	}
	return palette[max_colors - 1];
}

COLOR3 GetWadTexAplhaColor(WADTEX* wadTex, COLOR3* palette, int max_colors)
{
	int lastMipSize = (wadTex->nWidth >> 3) * (wadTex->nHeight >> 3);
	if (palette == NULL)
	{
		max_colors = *(unsigned short*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize - sizeof(BSPMIPTEX));
		palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + sizeof(short) - sizeof(BSPMIPTEX));
	}
	if (max_colors > 256 || max_colors < 0)
	{
		max_colors = 256;
	}
	return palette[max_colors - 1];
}
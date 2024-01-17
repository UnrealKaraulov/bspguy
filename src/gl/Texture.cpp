#include "lang.h"
#include <GL/glew.h>
#include "Wad.h"
#include "Texture.h"
#include "lodepng.h"
#include "util.h"
#include "Settings.h"
#include "Renderer.h"

std::vector<Texture*> dumpTextures;

Texture::Texture(GLsizei _width, GLsizei _height, unsigned char* data, const std::string& name, bool rgba, bool _owndata)
{
	this->owndata = _owndata;
	this->wad_name = "";
	this->width = _width;
	this->height = _height;
	this->nearFilter = GL_LINEAR;
	this->farFilter = GL_LINEAR;
	this->data = data;
	this->dataLen = (unsigned int)(width * height) * (rgba ? sizeof(COLOR4) : sizeof(COLOR3));
	this->id = 0;
	this->format = rgba ? GL_RGBA : GL_RGB;
	this->texName = name;
	this->uploaded = false;

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0970), name, width, height);

	this->transparentMode = IsTextureTransparent(name) ? 1 : 0;

	if (name.size() && name[0] == '{')
	{
		this->transparentMode = 2;
	}
	dumpTextures.push_back(this);
}

Texture::~Texture()
{
	this->wad_name = "";
	if (uploaded)
		glDeleteTextures(1, &id);

	if (this->owndata)
		delete[] data;

	dumpTextures.erase(std::remove(dumpTextures.begin(), dumpTextures.end(), this));
}

void Texture::upload(int type)
{
	if (uploaded)
	{
		glDeleteTextures(1, &id);
	}
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id); // Binds this texture handle so we can load the data into it

	// Set up filters and wrap mode
	if (type == TYPE_LIGHTMAP)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}
	else if (type == TYPE_TEXTURE)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, this->nearFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, this->nearFilter);
	}
	else if (type == TYPE_DECAL)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, this->nearFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, this->nearFilter);
	}

	if (texName[0] == '{')
	{
		if (format == GL_RGB)
		{
			format = GL_RGBA;
			COLOR3* rgbData = (COLOR3*)data;
			int pixelCount = width * height;
			COLOR4* rgbaData = new COLOR4[pixelCount];
			for (int i = 0; i < pixelCount; i++)
			{
				rgbaData[i] = rgbData[i];
				if (rgbaData[i].r == 0 && rgbaData[i].g == 0 && rgbaData[i].b > 250)
				{
					rgbaData[i] = COLOR4(0, 0, 0, 0);
				}
			}
			delete[] data;
			data = (unsigned char*)rgbaData;
			dataLen = (unsigned int)(width * height) * sizeof(COLOR4);
		}
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0971), texName, width, height);

	uploaded = true;
}

Texture* binded_tex[64];

void Texture::bind(GLuint texnum)
{
	if (binded_tex[texnum] != this)
	{
		glActiveTexture(GL_TEXTURE0 + texnum);
		glBindTexture(GL_TEXTURE_2D, id);
		binded_tex[texnum] = this;
	}
}

bool IsTextureTransparent(const std::string& texname)
{
	if (!texname.size())
		return false;
	for (auto const& s : g_settings.transparentTextures)
	{
		if (s == texname)
			return true;
	}
	return false;
}
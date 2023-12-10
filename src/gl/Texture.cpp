#include "lang.h"
#include <GL/glew.h>
#include "Wad.h"
#include "Texture.h"
#include "lodepng.h"
#include "util.h"
#include "Settings.h"
#include "Renderer.h"

std::vector<Texture*> dumpTextures;

Texture::Texture(GLsizei _width, GLsizei _height, const char* name)
{
	this->wad_name = "";
	this->width = _width;
	this->height = _height;
	this->nearFilter = GL_LINEAR;
	this->farFilter = GL_LINEAR;
	this->data = new unsigned char[(unsigned int)(width * height) * sizeof(COLOR3)];
	this->dataLen = (unsigned int)(width * height) * sizeof(COLOR3);
	this->id = this->format = 0;
	snprintf(texName, 64, "%s", name);
	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0969),name,width,height);
	this->transparentMode = IsTextureTransparent(name) ? 1 : 0;
	if (name && name[0] == '{')
	{
		this->transparentMode = 2;
	}
	dumpTextures.push_back(this);
}

Texture::Texture(GLsizei _width, GLsizei _height, unsigned char* data, const char* name)
{
	this->wad_name = "";
	this->width = _width;
	this->height = _height;
	this->nearFilter = GL_LINEAR;
	this->farFilter = GL_LINEAR;
	this->data = data;
	this->dataLen = (unsigned int)(width * height) * sizeof(COLOR3);
	this->id = this->format = 0;
	snprintf(texName, 64, "%s", name);
	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0970),name,width,height);
	this->transparentMode = IsTextureTransparent(name) ? 1 : 0;
	if (name && name[0] == '{')
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
	delete[] data;

	dumpTextures.erase(std::remove(dumpTextures.begin(), dumpTextures.end(), this), dumpTextures.end());
}

void Texture::upload(int _format, bool lightmap)
{
	if (uploaded)
	{
		glDeleteTextures(1, &id);
	}
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id); // Binds this texture handle so we can load the data into it

	// Set up filters and wrap mode
	if (lightmap)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, this->nearFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, this->nearFilter);

		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		if (texName[0] == '{')
		{
			if (_format == GL_RGB)
			{
				_format = GL_RGBA;
				COLOR3* rgbData = (COLOR3 *)data;
				int pixelCount = width * height;
				COLOR4* rgbaData = new COLOR4[pixelCount];
				for (int i = 0; i < pixelCount; i++)
				{
					rgbaData[i] = rgbData[i];
					if (rgbaData[i].r == 0 && rgbaData[i].g == 0 && rgbaData[i].b == 255)
					{
						rgbaData[i] = COLOR4(0, 0, 0, 0);
					}
				}
				delete [] data;
				data = (unsigned char*)rgbaData;
				dataLen = (unsigned int)(width * height) * sizeof(COLOR4);
			}
		}
	}

	if (_format == GL_RGB)
	{
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	}

	glTexImage2D(GL_TEXTURE_2D, 0, _format, width, height, 0, _format, GL_UNSIGNED_BYTE, data);

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0971),texName,width,height);

	format = _format;
	uploaded = true;
}

void Texture::bind(GLuint texnum)
{
	glActiveTexture(GL_TEXTURE0 + texnum);
	glBindTexture(GL_TEXTURE_2D, id);
}

bool IsTextureTransparent(const char* texname)
{
	if (!texname)
		return false;
	for (auto const& s : g_settings.transparentTextures)
	{
		if (strcasecmp(s.c_str(), texname) == 0)
			return true;
	}
	return false;
}
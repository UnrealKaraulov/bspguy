#pragma once
#include <GL/glew.h>
#include "util.h"

class Texture
{
public:
	unsigned int id; // OpenGL texture ID
	GLsizei height, width;
	int nearFilter;
	int farFilter;
	unsigned int format; // format of the data
	std::string texName;
	std::string wad_name;
	int transparentMode;

	enum TEXTURE_TYPE : int
	{
		TYPE_TEXTURE,
		TYPE_LIGHTMAP,
		TYPE_DECAL
	};

	Texture(GLsizei _width, GLsizei _height, unsigned char* data, const std::string& name, bool rgba = false);
	~Texture();

	// upload the texture with the specified settings
	void upload(int type = TYPE_TEXTURE);

	void setWadName(const std::string& s) {
		wad_name = s;
	}

	// use this texture for rendering
	void bind(GLuint texnum);

	unsigned char* data; // RGB(A) data
	size_t dataLen;

	bool uploaded = false;
};
extern std::vector<Texture*> dumpTextures;
bool IsTextureTransparent(const std::string& texname);
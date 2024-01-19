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

	Texture(GLsizei _width, GLsizei _height, unsigned char* data, const std::string& name, bool rgba = false, bool tex_owndata = true);
	~Texture();

	// upload the texture with the specified settings
	// then cleanup data memory!
	void upload(int type = TYPE_TEXTURE);

	// get data (if deleted, then fill it from texture)
	unsigned char* get_data();

	void setWadName(const std::string& s) {
		wad_name = s;
	}

	// use this texture for rendering
	void bind(GLuint texnum);

	size_t dataLen;

	bool uploaded;
private:
	unsigned char* data; // RGB(A) data
	bool tex_owndata;
};
extern std::vector<Texture*> dumpTextures;
bool IsTextureTransparent(const std::string& texname);
extern Texture* binded_tex[64];
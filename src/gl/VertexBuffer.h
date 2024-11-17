#pragma once
#include <vector>
#include <GL/glew.h>
#include "ShaderProgram.h"
#include "util.h"

class VertexBuffer
{
public:
	ShaderProgram* shaderProgram; // for getting handles to vertex attributes

	int numVerts;
	int primitive;
	int frameId;

	// Specify which common attributes to use. They will be located in the
	// shader program. If passing data, note that data is not copied, but referenced
	VertexBuffer(ShaderProgram* shaderProgram, void* dat, int numVerts, int primitive = 0);
	~VertexBuffer();

	// Note: Data is not copied into the class - don't delete your data.
	//       Data will be deleted when the buffer is destroyed.
	void setData(void* data, int numVerts);

	// get data (if deleted, then fill it from subbuffer)
	unsigned char* get_data();

	void upload(bool hideErrors = true, bool forceReupload = true);
	void deleteBuffer();

	void drawRange(int primitive, int start, int end, bool hideErrors = true);
	void draw(int primitive);
	void drawFull();

	bool ownData; // set to true if buffer should delete data on destruction
	bool uploaded;
private:
	unsigned char* data;
	GLuint vboId;
	GLuint vaoId;
	// add attributes according to the attribute flags
	void addAttributes(int attFlags);
};
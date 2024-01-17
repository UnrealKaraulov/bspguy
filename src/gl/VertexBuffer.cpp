#include "lang.h"
#include "VertexBuffer.h"
#include "util.h"
#include <string.h>

std::vector<VertexBuffer*> totalVertBuffers;

VertexBuffer::VertexBuffer(ShaderProgram* shaderProgram, void* dat, int numVerts, int primitive)
{
	uploaded = false;
	vboId = 0xFFFFFFFF;
	vaoId = 0xFFFFFFFF;
	ownData = false;
	this->shaderProgram = shaderProgram;
	this->primitive = primitive;
	setData(dat, numVerts);
	totalVertBuffers.push_back(this);
}

VertexBuffer::~VertexBuffer() {
	deleteBuffer();
	if (ownData && data) {
		delete[] data;
	}
	data = NULL;
	numVerts = 0;

	totalVertBuffers.erase(std::find(totalVertBuffers.begin(), totalVertBuffers.end(), this));
}

void VertexBuffer::setData(void* _data, int _numVerts)
{
	data = (unsigned char*)_data;
	numVerts = _numVerts;
	uploaded = false;
}

void VertexBuffer::upload(bool hideErrors, bool forceReupload)
{
	if (!shaderProgram || (uploaded && !forceReupload))
		return;

	uploaded = true;
	deleteBuffer();

	glGenVertexArrays(1, &vaoId);
	glBindVertexArray(vaoId);

	glGenBuffers(1, &vboId);
	glBindBuffer(GL_ARRAY_BUFFER, vboId);

	glBufferData(GL_ARRAY_BUFFER, shaderProgram->elementSize * numVerts, data, GL_STATIC_DRAW);

	GLuint64 offset = 0;
	for (const VertexAttr& a : shaderProgram->attribs)
	{
		if (a.handle == -1)
			continue;

		glEnableVertexAttribArray(a.handle);
		glVertexAttribPointer(a.handle, a.numValues, a.valueType, a.normalized != 0, shaderProgram->elementSize, (const GLvoid*)(offset));
		if (glGetError() != GL_NO_ERROR && !hideErrors)
		{
			std::cout << "Error! Name: " << a.varName << std::endl;
		}
		offset += a.size;
	}
}

void VertexBuffer::deleteBuffer() {
	if (vboId != 0xFFFFFFFF)
		glDeleteBuffers(1, &vboId);
	if (vaoId != 0xFFFFFFFF)
		glDeleteVertexArrays(1, &vaoId);
	vboId = 0xFFFFFFFF;
	vaoId = 0xFFFFFFFF;
}

void VertexBuffer::drawRange(int _primitive, int start, int end, bool hideErrors)
{
	shaderProgram->bind();
	upload(true, false);

	if (start < 0 || start > numVerts || numVerts == 0)
	{
		if (g_verbose)
			print_log(get_localized_string(LANG_0976), start, numVerts);
		return;
	}
	else if (end > numVerts || end < 0)
	{
		if (g_verbose)
			print_log(get_localized_string(LANG_0977), end);
		return;
	}
	else if (end - start <= 0)
	{
		if (g_verbose)
			print_log(get_localized_string(LANG_0978), start, end);
		return;
	}
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glBindVertexArray(vaoId);
	glDrawArrays(_primitive, start, end - start);
}

void VertexBuffer::draw(int _primitive)
{
	drawRange(_primitive, 0, numVerts);
}

void VertexBuffer::drawFull()
{
	drawRange(primitive, 0, numVerts);
}

#include "lang.h"
#include "VertexBuffer.h"
#include "util.h"
#include <string.h>
#include "Renderer.h"

VertexBuffer::VertexBuffer(ShaderProgram* shaderProgram, void* dat, size_t _numVerts, int primitive)
{
	frameId = (size_t)-1;
	uploaded = false;
	vboId = 0xFFFFFFFF;
	vaoId = 0xFFFFFFFF;
	ownData = false;
	this->shaderProgram = shaderProgram;
	this->primitive = primitive;
	setData(dat, _numVerts);
}

VertexBuffer::~VertexBuffer() {

	deleteBuffer();
	if (ownData && data) {
		delete[] data;
	}
	data = NULL;
	numVerts = 0;
}

void VertexBuffer::setData(void* _data, size_t _numVerts)
{
	data = (unsigned char*)_data;
	numVerts = _numVerts;
	uploaded = false;
}

unsigned char* VertexBuffer::get_data()
{
	if (data == NULL)
	{
		glBindBuffer(GL_ARRAY_BUFFER, vboId);
		glBindVertexArray(vaoId);
		GLint bufferSize;
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
		data = new unsigned char[bufferSize];
		glGetBufferSubData(GL_ARRAY_BUFFER, 0, bufferSize, data);
	}
	return data;
}

void VertexBuffer::upload(bool hideErrors, bool forceReupload)
{
	if (!shaderProgram || (uploaded && !forceReupload))
		return;

	if (data == NULL)
	{
		get_data();
	}

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


	if (ownData) {
		delete[] data;
		data = NULL;
	}
	uploaded = true;
}

void VertexBuffer::deleteBuffer() {
	if (vboId != 0xFFFFFFFF)
		glDeleteBuffers(1, &vboId);
	if (vaoId != 0xFFFFFFFF)
		glDeleteVertexArrays(1, &vaoId);
	vboId = 0xFFFFFFFF;
	vaoId = 0xFFFFFFFF;
}

void VertexBuffer::drawRange(int _primitive, size_t start, size_t end, bool hideErrors)
{
	if (frameId != (size_t)-1)
	{
		if (frameId == g_drawFrameId)
		{
			return;
		}
		frameId = g_drawFrameId;
	}
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
	glDrawArrays(_primitive, (GLint)start, (GLint)(end - start));
}

void VertexBuffer::draw(int _primitive)
{
	if (numVerts > 0)
	{
		drawRange(_primitive, 0, numVerts);
	}
}

void VertexBuffer::drawFull()
{
	if (numVerts > 0)
	{
		drawRange(primitive, 0, numVerts);
	}
}

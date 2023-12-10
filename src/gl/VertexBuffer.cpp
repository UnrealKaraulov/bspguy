#include "lang.h"
#include "VertexBuffer.h"
#include "util.h"
#include <string.h>

VertexAttr commonAttr[VBUF_FLAGBITS] =
{
	VertexAttr(2, GL_BYTE,          -1, GL_FALSE, ""), // TEX_2B
	VertexAttr(2, GL_SHORT,         -1, GL_FALSE, ""), // TEX_2S
	VertexAttr(2, GL_FLOAT,         -1, GL_FALSE, ""), // TEX_2F
	VertexAttr(3, GL_UNSIGNED_BYTE, -1, GL_TRUE, ""),  // COLOR_3B
	VertexAttr(3, GL_FLOAT,         -1, GL_TRUE, ""),  // COLOR_3F
	VertexAttr(4, GL_UNSIGNED_BYTE, -1, GL_TRUE, ""),  // COLOR_4B
	VertexAttr(4, GL_FLOAT,         -1, GL_TRUE, ""),  // COLOR_4F
	VertexAttr(3, GL_BYTE,          -1, GL_TRUE, ""),  // NORM_3B
	VertexAttr(3, GL_FLOAT,         -1, GL_TRUE, ""),  // NORM_3F
	VertexAttr(2, GL_BYTE,          -1, GL_FALSE, ""), // POS_2B
	VertexAttr(2, GL_SHORT,         -1, GL_FALSE, ""), // POS_2S
	VertexAttr(2, GL_INT,           -1, GL_FALSE, ""), // POS_2I
	VertexAttr(2, GL_FLOAT,         -1, GL_FALSE, ""), // POS_2F
	VertexAttr(3, GL_SHORT,         -1, GL_FALSE, ""), // POS_3S
	VertexAttr(3, GL_FLOAT,         -1, GL_FALSE, ""), // POS_3F
};

VertexAttr::VertexAttr(int numValues, int valueType, int handle, int normalized, const char* varName)
	: numValues(numValues), valueType(valueType), handle(handle), normalized(normalized), varName(varName)
{
	switch (valueType)
	{
		case(GL_BYTE):
		case(GL_UNSIGNED_BYTE):
			size = numValues;
			break;
		case(GL_SHORT):
		case(GL_UNSIGNED_SHORT):
			size = numValues * 2;
			break;
		case(GL_FLOAT):
		case(GL_INT):
		case(GL_UNSIGNED_INT):
			size = numValues * 4;
			break;
		default:
			print_log(get_localized_string(LANG_0972),valueType);
			handle = -1;
			size = 0;
	}
}



VertexBuffer::VertexBuffer(ShaderProgram* shaderProgram, int attFlags, void * dat, int numVerts, int primitive)
{
	attribs = std::vector<VertexAttr>();
	this->shaderProgram = shaderProgram;
	this->primitive = primitive;
	addAttributes(attFlags);
	setData(dat, numVerts);
	vboId = (GLuint)-1;
}

VertexBuffer::VertexBuffer(ShaderProgram* shaderProgram, int attFlags, int primitive)
{
	attribs = std::vector<VertexAttr>();
	numVerts = 0;
	data = NULL;
	vboId = (GLuint)-1;
	this->shaderProgram = shaderProgram;
	this->primitive = primitive;
	addAttributes(attFlags);
}

VertexBuffer::~VertexBuffer() {
	if (attribs.size())
		attribs.clear();
	deleteBuffer();
	if (ownData) {
		delete[] data;
	}
	data = NULL;
	numVerts = 0;
}

void VertexBuffer::addAttributes(int attFlags)
{
	elementSize = 0;
	for (int i = 0; i < VBUF_FLAGBITS; i++)
	{
		if (attFlags & (1 << i))
		{
			if (i >= VBUF_POS_START)
				commonAttr[i].handle = shaderProgram->vposID;
			else if (i >= VBUF_COLOR_START)
				commonAttr[i].handle = shaderProgram->vcolorID;
			else if (i >= VBUF_TEX_START)
				commonAttr[i].handle = shaderProgram->vtexID;
			else
				print_log(get_localized_string(LANG_0973),i);

			attribs.push_back(commonAttr[i]);
			elementSize += commonAttr[i].size;
		}
	}
}

void VertexBuffer::addAttribute(int numValues, int valueType, int normalized, const char* varName) {
	VertexAttr attribute(numValues, valueType, -1, normalized, varName);

	attribs.push_back(attribute);
	elementSize += attribute.size;
}

void VertexBuffer::addAttribute(int type, const char* varName) {

	int idx = 0;
	while (type >>= 1) // unroll for more speed...
	{
		idx++;
	}

	if (idx >= VBUF_FLAGBITS) {
		print_log(get_localized_string(LANG_0974));
		return;
	}

	VertexAttr attribute = commonAttr[idx];
	attribute.handle = -1;
	attribute.varName = varName;

	attribs.push_back(attribute);
	elementSize += attribute.size;
}

void VertexBuffer::setShader(ShaderProgram* program, bool hideErrors) {
	shaderProgram = program;
	attributesBound = false;

	for (int i = 0; i < attribs.size(); i++)
	{
		if (strlen(attribs[i].varName) > 0) {
			attribs[i].handle = -1;
		}
	}

	bindAttributes(hideErrors && !g_verbose);
	if (vboId != (GLuint)-1) {
		deleteBuffer();
		upload();
	}
}

void VertexBuffer::bindAttributes(bool hideErrors) {
	if (attributesBound || !shaderProgram)
		return;

	for (int i = 0; i < attribs.size(); i++)
	{
		if (attribs[i].handle != -1)
			continue;

		attribs[i].handle = glGetAttribLocation(shaderProgram->ID, attribs[i].varName);

		if ((!hideErrors || g_verbose) && attribs[i].handle == -1)
			print_log(get_localized_string(LANG_0975),attribs[i].varName);
	}

	attributesBound = true;
}

void VertexBuffer::setData(void* _data, int _numVerts)
{
	data = (unsigned char*)_data;
	numVerts = _numVerts;
	deleteBuffer();
}

void VertexBuffer::upload(bool hideErrors)
{
	if (!shaderProgram)
		return;
	shaderProgram->bind();
	bindAttributes(hideErrors && !g_verbose);

	if (vboId == (GLuint)-1)
		glGenBuffers(1, &vboId);

	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glBufferData(GL_ARRAY_BUFFER, elementSize * numVerts, data, GL_STATIC_DRAW);

	int offset = 0;
	for (int i = 0; i < attribs.size(); i++)
	{
		VertexAttr& a = attribs[i];
		void* ptr = ((char*)0) + offset;
		offset += a.size;
		if (a.handle == -1) {
			continue;
		}
		glBindBuffer(GL_ARRAY_BUFFER, vboId);
		glEnableVertexAttribArray(a.handle);
		glVertexAttribPointer(a.handle, a.numValues, a.valueType, a.normalized != 0, elementSize, ptr);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::deleteBuffer() {
	if (vboId != (GLuint)-1)
		glDeleteBuffers(1, &vboId);
	vboId = (GLuint)-1;
}

void VertexBuffer::drawRange(int _primitive, int start, int end, bool hideErrors)
{
	shaderProgram->bind();
	bindAttributes(hideErrors && !g_verbose);

	char* offsetPtr = (char*)data;
	if (vboId != (GLuint)-1) {
		glBindBuffer(GL_ARRAY_BUFFER, vboId);
		offsetPtr = NULL;
	}
	{
		int offset = 0;
		for (int i = 0; i < attribs.size(); i++)
		{
			VertexAttr& a = attribs[i];
			void* ptr = offsetPtr + offset;
			offset += a.size;
			if (a.handle == -1)
				continue;
			glEnableVertexAttribArray(a.handle);
			glVertexAttribPointer(a.handle, a.numValues, a.valueType, a.normalized != 0, elementSize, ptr);
		}
	}

	if (start < 0 || start > numVerts || numVerts == 0)
		print_log(get_localized_string(LANG_0976),start,numVerts);
	else if (end > numVerts || end < 0)
		print_log(get_localized_string(LANG_0977),end);
	else if (end - start <= 0)
		print_log(get_localized_string(LANG_0978),start,end);
	else
		glDrawArrays(_primitive, start, end - start);

	if (vboId != (GLuint)-1) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	for (int i = 0; i < attribs.size(); i++)
	{
		VertexAttr& a = attribs[i];
		if (a.handle == -1)
			continue;
		glDisableVertexAttribArray(a.handle);
	}
}

void VertexBuffer::draw(int _primitive)
{
	drawRange(_primitive, 0, numVerts);
}

void VertexBuffer::drawFull()
{
	drawRange(primitive, 0, numVerts);
}
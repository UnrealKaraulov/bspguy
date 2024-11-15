#include "lang.h"
#include "ShaderProgram.h"
#include "log.h"
#include "Renderer.h"
#include "Settings.h"

static unsigned int g_active_shader_program = 0xFFFFFFFF;


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
		print_log(get_localized_string(LANG_0972), valueType);
		handle = -1;
		size = 0;
	}
}

ShaderProgram::ShaderProgram(const char* vshaderSource, const char* fshaderSource)
{
    elementSize = 0;
	modelViewID = modelViewProjID = -1;
	ID = 0xFFFFFFFF;
	vposID = vcolorID = vtexID = 0;
	vShader = new Shader(vshaderSource, GL_VERTEX_SHADER);
	fShader = new Shader(fshaderSource, GL_FRAGMENT_SHADER);
	modelViewProjMat = modelViewMat = NULL;
	attribs = std::vector<VertexAttr>();
	link();
}
void ShaderProgram::link()
{
	// Create Shader And Program Objects
	ID = glCreateProgram();
	// Attach The Shader Objects To The Program Object
	glAttachShader(ID, vShader->ID);
	glAttachShader(ID, fShader->ID);

	glLinkProgram(ID);

	int success;
	glGetProgramiv(ID, GL_LINK_STATUS, &success);
	if (success != GL_TRUE)
	{
		char* log = new char[1024];
		int len;
		glGetProgramInfoLog(ID, 1024, &len, log);
		print_log(get_localized_string(LANG_0961));
		print_log(log);
		if (len > 1024)
			print_log(get_localized_string(LANG_0962));
		delete[] log;
	}
}


ShaderProgram::~ShaderProgram(void)
{
	glDeleteProgram(ID);
	delete vShader;
	delete fShader;
}

void ShaderProgram::bind()
{
	if (g_active_shader_program != ID)
	{
		g_active_shader_program = ID;
		glUseProgram(ID);
		updateMatrixes();
	}
}

void ShaderProgram::removeShader(int shaderID)
{
	glDetachShader(ID, shaderID);
}

void ShaderProgram::setMatrixes(mat4x4* modelView, mat4x4* modelViewProj)
{
	modelViewMat = modelView;
	modelViewProjMat = modelViewProj;
}

void ShaderProgram::updateMatrixes()
{
	if (g_active_shader_program != ID)
	{
		g_active_shader_program = ID;
		glUseProgram(ID);
	}

	*modelViewMat = g_app->matview * g_app->matmodel;
	*modelViewProjMat = g_app->projection * *modelViewMat;

	*modelViewMat = modelViewMat->transpose();
	*modelViewProjMat = modelViewProjMat->transpose();

	if (modelViewID != -1)
		glUniformMatrix4fv(modelViewID, 1, false, (float*)&modelViewMat->m[0]);
	if (modelViewProjID != -1)
		glUniformMatrix4fv(modelViewProjID, 1, false, (float*)&modelViewProjMat->m[0]);
}

void ShaderProgram::updateMatrixes(const mat4x4& viewMat, const mat4x4& viewProjMat)
{
	if (g_active_shader_program != ID)
	{
		g_active_shader_program = ID;
		glUseProgram(ID);
	}
	*modelViewMat = viewMat;
	*modelViewProjMat = viewProjMat;

	if (modelViewID != -1)
		glUniformMatrix4fv(modelViewID, 1, false, (float*)&modelViewMat->m[0]);
	if (modelViewProjID != -1)
		glUniformMatrix4fv(modelViewProjID, 1, false, (float*)&modelViewProjMat->m[0]);
}

void calcMatrixes(mat4x4 & outViewMat, mat4x4 & outViewProjMat)
{
	outViewMat = g_app->matview * g_app->matmodel;
	outViewProjMat = g_app->projection * outViewMat;

	outViewMat = outViewMat.transpose();
	outViewProjMat = outViewProjMat.transpose();
}

void ShaderProgram::setMatrixNames(const char* _modelViewMat, const char* _modelViewProjMat)
{
	if (_modelViewMat)
	{
		modelViewID = glGetUniformLocation(ID, _modelViewMat);
		if (modelViewID == -1)
			print_log(get_localized_string(LANG_0963),_modelViewMat);
	}
	if (_modelViewProjMat)
	{
		modelViewProjID = glGetUniformLocation(ID, _modelViewProjMat);
		if (modelViewProjID == -1)
			print_log(get_localized_string(LANG_0964),_modelViewProjMat);
	}
}

void ShaderProgram::setVertexAttributeNames(const char* posAtt, const char* colorAtt, const char* texAtt, int attFlags)
{
	if (posAtt)
	{
		vposID = glGetAttribLocation(ID, posAtt);
		if (vposID == -1) print_log(get_localized_string(LANG_0965),posAtt);
	}
	if (colorAtt)
	{
		vcolorID = glGetAttribLocation(ID, colorAtt);
		if (vcolorID == -1) print_log(get_localized_string(LANG_0966),colorAtt);
	}
	if (texAtt)
	{
		vtexID = glGetAttribLocation(ID, texAtt);
		if (vtexID == -1) print_log(get_localized_string(LANG_0967),texAtt);
	}

	addAttributes(attFlags);
}

void ShaderProgram::pushMatrix(int matType)
{
	if (matType & MAT_MODEL)	  matStack[0].push_back(g_app->matmodel);
	if (matType & MAT_VIEW)		  matStack[1].push_back(g_app->matview);
	if (matType & MAT_PROJECTION) matStack[2].push_back(g_app->projection);
}

void ShaderProgram::popMatrix(int matType)
{
	mat4x4 * targets[3] = { &g_app->matmodel, &g_app->matview, &g_app->projection};
	for (int idx = 0, mask = 1; idx < 3; ++idx, mask <<= 1)
	{
		if (matType & mask)
		{
			std::vector<mat4x4>& stack = matStack[idx];
			if (!stack.empty())
			{
				*targets[idx] = stack[stack.size() - 1];
				stack.pop_back();
				break;
			}
			else
				print_log(get_localized_string(LANG_0968));
		}
	}
}

void ShaderProgram::addAttributes(int attFlags)
{
	elementSize = 0;
	for (int i = 0; i < VBUF_FLAGBITS; i++)
	{
		if (attFlags & (1 << i))
		{
			if (i >= VBUF_POS_START)
				commonAttr[i].handle = vposID;
			else if (i >= VBUF_COLOR_START)
				commonAttr[i].handle = vcolorID;
			else if (i >= VBUF_TEX_START)
				commonAttr[i].handle = vtexID;
			else
				print_log(get_localized_string(LANG_0973), i);

			attribs.emplace_back(commonAttr[i]);
			elementSize += commonAttr[i].size;
		}
	}
}

void ShaderProgram::addAttribute(int numValues, int valueType, int normalized, const char* varName) {
	VertexAttr attribute(numValues, valueType, -1, normalized, varName);

	attribs.emplace_back(attribute);
	elementSize += attribute.size;
}

void ShaderProgram::addAttribute(int type, const char* varName) {
	if (!varName || varName[0] == '\0')
	{
		print_log(PRINT_RED | PRINT_INTENSITY, "VertexBuffer::addAttribute -> varName is null");
		return;
	}
	int idx = 0;
	while (type >>= 1) // unroll for more speed...
	{
		idx++;
	}

	if (idx >= VBUF_FLAGBITS) {
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0974));
		return;
	}

	VertexAttr attribute = commonAttr[idx];
	attribute.handle = -1;
	attribute.varName = varName;

	attribs.emplace_back(attribute);
	elementSize += attribute.size;
}

void ShaderProgram::bindAttributes(bool hideErrors) {
	if (attributesBound)
		return;

	for (size_t i = 0; i < attribs.size(); i++)
	{
		if (attribs[i].handle != -1)
			continue;

		attribs[i].handle = glGetAttribLocation(ID, attribs[i].varName);

		if ((!hideErrors || g_verbose) && attribs[i].handle == -1)
			print_log(get_localized_string(LANG_0975), attribs[i].varName);
	}

	attributesBound = true;
}
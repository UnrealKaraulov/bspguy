#include "lang.h"
#include <GL/glew.h>
#include "ShaderProgram.h"
#include "util.h"
#include <string>
#include "Renderer.h"
#include "Settings.h"

static unsigned int g_active_shader_program = 0xFFFFFFFF;

ShaderProgram::ShaderProgram(const char* vshaderSource, const char* fshaderSource)
{
	modelViewID = modelViewProjID = -1;
	ID = 0xFFFFFFFF;
	vposID = vcolorID = vtexID = 0;
	vShader = new Shader(vshaderSource, GL_VERTEX_SHADER);
	fShader = new Shader(fshaderSource, GL_FRAGMENT_SHADER);
	modelViewProjMat = modelViewMat = NULL;
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

void ShaderProgram::setVertexAttributeNames(const char* posAtt, const char* colorAtt, const char* texAtt)
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

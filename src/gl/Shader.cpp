#include "lang.h"
#include <GL/glew.h>
#include "Shader.h"
#include "util.h"

Shader::Shader(const char* sourceCode, int shaderType)
{
	// Create Shader And Program Objects
	ID = glCreateShader(shaderType);

	glShaderSource(ID, 1, &sourceCode, NULL);
	glCompileShader(ID);

	int success;
	glGetShaderiv(ID, GL_COMPILE_STATUS, &success);
	if (success != GL_TRUE)
	{
		char* log = new char[512];
		int len;
		glGetShaderInfoLog(ID, 512, &len, log);
		logf(get_localized_string(LANG_0959),shaderType);
		logf(log);
		if (len > 512)
			logf(get_localized_string(LANG_0960));
		delete[] log;
	}
}


Shader::~Shader(void)
{
	glDeleteShader(ID);
}


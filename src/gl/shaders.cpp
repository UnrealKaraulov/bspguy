#include "shaders.h"
namespace Shaders
{
	// IMPORTANT: 
	// if you have an Nvidia GPU, compile shader code in GPU ShaderAnalyzer to be sure it works for AMD too.
	// AMD has a stricter GLSL compiler than Nvidia does.

	const char* g_shader_cVert_vertex =
		// object variables
		"uniform mat4 modelViewProjection;\n"
		"uniform vec4 colorMult;\n"

		// vertex variables
		"attribute vec3 vPosition;\n"
		"attribute vec4 vColor;\n"

		// fragment variables
		"varying vec4 fColor;\n"

		"void main()\n"
		"{\n"
		"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
		"	fColor = vColor * colorMult;\n"
		"}\n";


	const char* g_shader_cVert_fragment =
		"varying vec4 fColor;\n"

		"void main()\n"
		"{\n"
		"	if(fColor.a <= 0.01)\n"
		"		discard;\n"
		"	float gamma = 1.5;\n"
		"	gl_FragColor = vec4(pow(fColor.rgb, vec3(1.0/gamma)), fColor.a);\n"
		"}\n";


	const char* g_shader_tVert_vertex =
		// object variables
		"uniform mat4 modelViewProjection;\n"

		// vertex variables
		"attribute vec3 vPosition;\n"
		"attribute vec2 vTex;\n"

		// fragment variables
		"varying vec2 fTex;\n"

		"void main()\n"
		"{\n"
		"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
		"	fTex = vTex;\n"
		"}\n";


	const char* g_shader_tVert_fragment =
		"varying vec2 fTex;\n"

		"uniform sampler2D sTex;\n"

		"void main()\n"
		"{\n"
		"	gl_FragColor = texture2D(sTex, fTex);\n"
		"	if(gl_FragColor.a <= 0.05)\n"
		"		discard;\n"
		"}\n";

	const char* g_shader_model_vertex =
		// object variables
		"uniform mat4 modelViewProjection;\n"

		// vertex variables
		"attribute vec3 vPosition;\n"
		"attribute vec2 vTex;\n"

		// fragment variables
		"varying vec2 fTex;\n"

		"void main()\n"
		"{\n"
		"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
		"	fTex = vTex;\n"
		"}\n";

	const char* g_shader_model_fragment =
		"varying vec2 fTex;\n"

		"uniform sampler2D sTex;\n"

		"void main()\n"
		"{\n"
		"	gl_FragColor = texture2D(sTex, fTex);\n"
		"	if(gl_FragColor.a <= 0.05)\n"
		"		discard;\n"
		"}\n";

	const char* g_shader_multitexture_vertex =
		// object variables
		"uniform mat4 modelViewProjection;\n"

		// vertex variables
		"attribute vec3 vPosition;\n"
		"attribute vec2 vTex;\n"
		"attribute vec3 vLightmapTex0;\n"
		"attribute vec3 vLightmapTex1;\n"
		"attribute vec3 vLightmapTex2;\n"
		"attribute vec3 vLightmapTex3;\n"
		"attribute vec4 vColor;\n"

		// fragment variables
		"varying vec2 fTex;\n"
		"varying vec3 fLightmapTex0;\n"
		"varying vec3 fLightmapTex1;\n"
		"varying vec3 fLightmapTex2;\n"
		"varying vec3 fLightmapTex3;\n"
		"varying vec4 fColor;\n"

		"void main()\n"
		"{\n"
		"	gl_Position = modelViewProjection * vec4(vPosition, 1);\n"
		"	fTex = vTex;\n"
		"	fLightmapTex0 = vLightmapTex0;\n"
		"	fLightmapTex1 = vLightmapTex1;\n"
		"	fLightmapTex2 = vLightmapTex2;\n"
		"	fLightmapTex3 = vLightmapTex3;\n"
		"	fColor = vColor;\n"
		"}\n";

	const char* g_shader_multitexture_fragment =
		"varying vec2 fTex;\n"
		"varying vec3 fLightmapTex0;\n"
		"varying vec3 fLightmapTex1;\n"
		"varying vec3 fLightmapTex2;\n"
		"varying vec3 fLightmapTex3;\n"
		"varying vec4 fColor;\n"

		"uniform sampler2D sTex;\n"
		"uniform sampler2D sLightmapTex0;\n"
		"uniform sampler2D sLightmapTex1;\n"
		"uniform sampler2D sLightmapTex2;\n"
		"uniform sampler2D sLightmapTex3;\n"

		"void main()\n"
		"{\n"
		"	vec4 color = texture2D(sTex, fTex);\n"
		"	if(color.a <= 0.05)\n"
		"		discard;\n"
		"	vec4 fcolor = fColor.rgba;\n"
	/*	"	if (fcolor.g == 5.0)\n"
		"	{\n"
		"		if (color.b > 0.95 && color.g < 0.05 && color.r < 0.05)\n"
		"		{\n"
		"			gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0); \n"
		"			return;\n"
		"		}\n"
		"	}\n"
		*/"	if (fcolor.g == 6.0)\n"
		"	{\n"
		"		color.rgb = color.rgb * fcolor.a;\n"
		"		fcolor.g = 1.0;\n"
		"	}\n"
		"	else if (fcolor.g > 1.0)\n"
		"	{\n"
		"		fcolor.g = 1.0;\n"
		"	}\n"
		"	vec3 lightmap = texture2D(sLightmapTex0, fLightmapTex0.xy).rgb * fLightmapTex0.z;\n"
		"	lightmap += texture2D(sLightmapTex1, fLightmapTex1.xy).rgb * fLightmapTex1.z;\n"
		"	lightmap += texture2D(sLightmapTex2, fLightmapTex2.xy).rgb * fLightmapTex2.z;\n"
		"	lightmap += texture2D(sLightmapTex3, fLightmapTex3.xy).rgb * fLightmapTex3.z;\n"
		"	color.rgb = color.rgb * lightmap * fcolor.rgb; \n"
		"	float gamma = 1.5;\n"
		"	gl_FragColor = vec4(pow(color.rgb, vec3(1.0/gamma)), color.a * fcolor.a );\n"
		"}\n";
}
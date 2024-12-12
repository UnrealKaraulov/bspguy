#pragma once
#include "Shader.h"
#include <vector>
#include "mat4x4.h"

enum mat_types
{
	MAT_MODEL = 1,
	MAT_VIEW = 2,
	MAT_PROJECTION = 4,
};



// Combinable flags for setting common vertex attributes
#define TEX_2B   (1 << 0)   // 2D byte texture coordinates
#define TEX_2S   (1 << 1)   // 2D short texture coordinates
#define TEX_2F   (1 << 2)   // 2D float texture coordinates
#define COLOR_3B (1 << 3)   // RGB byte color values
#define COLOR_3F (1 << 4)   // RGB float color values
#define COLOR_4B (1 << 5)   // RGBA byte color values
#define COLOR_4F (1 << 6)   // RGBA float color values
#define NORM_3B  (1 << 7)   // 3D byte normal coordinates
#define NORM_3F  (1 << 8)   // 3D float normal coordinates
#define POS_2B   (1 << 9)   // 2D byte position coordinates
#define POS_2S   (1 << 10)  // 2D short position coordinates
#define POS_2I   (1 << 11)  // 2D integer position coordinates
#define POS_2F   (1 << 12)  // 2D float position coordinates
#define POS_3S   (1 << 13)  // 3D short position coordinates
#define POS_3F   (1 << 14)  // 3D float position coordinates

// starting bits for the different types of vertex attributes
#define VBUF_TEX_START     0 // first bit for texture flags
#define VBUF_COLOR_START   3 // first bit for color flags
#define VBUF_NORM_START    7 // first bit for normals flags
#define VBUF_POS_START     9 // first bit for position flags
#define VBUF_FLAGBITS     15 // number of settable bits
#define VBUF_TEX_MASK    0x7 // mask for all texture flags
#define VBUF_COLOR_MASK 0x78 // mask for all color flags
#define VBUF_NORM_MASK 0x180 // mask for all normal flags

struct VertexAttr
{
	int numValues;
	int valueType;  // Ex: GL_FLOAT
	int handle;     // location in shader program (-1 indicates invalid attribute)
	int size;       // size of the attribute in bytes
	int normalized; // GL_TRUE/GL_FALSE Ex: byte color values are normalized (0-255 = 0.0-1.0)
	const char* varName;

	VertexAttr()
	{
		handle = -1;
		numValues = valueType = size = normalized = 0;
		varName = NULL;
	}

	~VertexAttr()
	{
		handle = -1;
		numValues = valueType = size = normalized = 0;
		varName = NULL;
	}

	VertexAttr(int numValues, int valueType, int handle, int normalized, const char* varName);
};


extern VertexAttr commonAttr[VBUF_FLAGBITS];

class ShaderProgram
{
public:
	unsigned int ID; // OpenGL program ID

	Shader* vShader; // vertex shader
	Shader* fShader; // fragment shader

	// commonly used vertex attributes
	unsigned int vposID;
	unsigned int vcolorID;
	unsigned int vtexID;

	// Creates a shader program to replace the fixed-function pipeline
	ShaderProgram(const char* vshaderSource, const char* fshaderSource);
	~ShaderProgram(void);

	// use this shader program instead of the fixed function pipeline.
	// to go back to normal opengl rendering, use this:
	// glUseProgramObject(0);
	void bind();

	void removeShader(int shaderID);

	void setMatrixes(mat4x4* modelView, mat4x4* modelViewProj);

	// Find the the modelView and modelViewProjection matrices
	// used in the shader code, so that we can update them.
	void setMatrixNames(const char* modelViewMat, const char* modelViewProjMat);

	// Find the IDs for the common vertex attributes (position, color, texture coords, normals)
	void setVertexAttributeNames(const char* posAtt, const char* colorAtt, const char* texAtt, int attFlags);

	// upload the model, view, and projection matrices to the shader (or fixed-funcion pipe)
	void updateMatrixes();

	void updateMatrixes(const mat4x4& viewMat, const mat4x4& viewProjMat);

	// save/restore matrices
	void pushMatrix(int matType = MAT_MODEL);
	void popMatrix(int matType = MAT_MODEL);

	void addAttributes(int attFlags);

	void addAttribute(int numValues, int valueType, int normalized, const char* varName);
	void addAttribute(int type, const char* varName);
	void bindAttributes(bool hideErrors = false); // find handles for all vertex attributes (call from main thread only)

	std::vector<VertexAttr> attribs;
	int elementSize;
private:

	bool attributesBound = false;

	// uniforms
	int modelViewID;
	int modelViewProjID;

	// computed from model, view, and projection matrices
	mat4x4 * modelViewProjMat; // for transforming vertices onto the screen
	mat4x4 * modelViewMat;

	// stores previous states of matrices
	std::vector<mat4x4> modelStack;
	std::vector<mat4x4> viewStack;
	std::vector<mat4x4> projStack;
	size_t modelStackIdx;
	size_t viewStackIdx;
	size_t projStackIdx;

	void link();
};

void calcMatrixes(mat4x4& outViewMat, mat4x4& outViewProjMat);
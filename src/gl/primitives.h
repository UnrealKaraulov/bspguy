#pragma once

#include <vector>
#include "vectors.h"
#include "Wad.h"
#include "bsplimits.h"

#pragma pack(push, 1)

struct tVert
{
	vec3 pos;
	// texture coordinates
	float u, v;

	tVert() = default;
	tVert(float x, float y, float z, float u, float v) : u(u), v(v), pos(x, y, z)
	{}
	tVert(vec3 p, float u, float v) : u(u), v(v), pos(p.x, p.y, p.z)
	{}
	tVert(vec3 p, vec2 uv) : u(uv.x), v(uv.y), pos(p.x, p.y, p.z)
	{}
};

struct modelVert
{
	vec3 pos;
	// texture coordinates
	float u, v;
};


struct lightmapVert
{
	vec3 pos;
	// texture coordinates
	float u, v;
	// lightmap texture coordinates
	// last value scales the lightmap brightness
	float luv[MAX_LIGHTMAPS][3];
	// color
	float r, g, b, a;
};

struct cVert
{
	COLOR4 c;
	vec3 pos;

	cVert() = default;
	cVert(float x, float y, float z, COLOR4 c) : c(c), pos(x, y, z)
	{}
	cVert(vec3 p, COLOR4 c) : c(c), pos(p.x, p.y, p.z)
	{}
};

struct tTri
{
	tVert v1, v2, v3;

	tTri() = default;
	tTri(tVert v1, tVert v2, tVert v3) : v1(v1), v2(v2), v3(v3)
	{}
};

struct cTri
{
	cVert v1, v2, v3;

	cTri() = default;
	cTri(cVert v1, cVert v2, cVert v3) : v1(v1), v2(v2), v3(v3)
	{}
};

// Textured 3D Quad
struct tQuad
{
	tVert v1, v2, v3;
	tVert v4, v5, v6;

	tQuad() = default;
	tQuad(float x, float y, float w, float h);
	tQuad(float x, float y, float w, float h, float uu1, float vv1, float uu2, float vv2);
	tQuad(tVert v1, tVert v2, tVert v3, tVert v4);
};

// Colored 3D Quad
struct cQuad
{
	cVert v1, v2, v3;
	cVert v4, v5, v6;

	cQuad() = default;
	cQuad(cVert v1, cVert v2, cVert v3, cVert v4);

	void setColor(COLOR4 c); // color for the entire quad
	void setColor(COLOR4 c1, COLOR4 c2, COLOR4 c3, COLOR4 c4); // color each vertex in CCW order
};

// Textured 3D Cube
struct tCube
{
	tQuad left, right;
	tQuad top, bottom;
	tQuad front, back;

	tCube(vec3 mins, vec3 maxs);
};

// Colored 3D Cube
struct cCube
{
	cQuad left, right;
	cQuad top, bottom;
	cQuad front, back;

	cCube() = default;
	cCube(vec3 mins, vec3 maxs, COLOR4 c);

	void setColor(COLOR4 c); // set color for the entire cube
	void setColor(COLOR4 left, COLOR4 right, COLOR4 top, COLOR4 bottom, COLOR4 front, COLOR4 back);
};

// Colored 3D Cube with axes
struct cCubeAxes
{
	cCube model;
	cCube axes;

	cCubeAxes() = default;
	cCubeAxes(vec3 mins, vec3 maxs, COLOR4 c);
};


#pragma pack(pop)
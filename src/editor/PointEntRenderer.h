#pragma once
#include "util.h"
#include "Fgd.h"
#include "VertexBuffer.h"

struct EntCube
{
	vec3 mins;
	vec3 maxs;
	COLOR4 color;

	VertexBuffer* axesBuffer;
	VertexBuffer* cubeBuffer;
	VertexBuffer* selectBuffer; // red coloring for selected ents
	VertexBuffer* wireframeBuffer; // yellow outline for selected ents

	bool Textured;
};

class PointEntRenderer
{
public:
	Fgd* fgd;

	PointEntRenderer(Fgd* fgd);
	~PointEntRenderer();

	EntCube* getEntCube(Entity* ent);

	std::map<std::string, EntCube*> cubeMap;
	std::vector<EntCube*> entCubes;

	void genPointEntCubes();
	EntCube* getCubeMatchingProps(EntCube* entCube);
	void genCubeBuffers(EntCube* entCube);
private:
	bool defaultCubeGen = false;
};
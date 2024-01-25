#pragma once
#include "util.h"
#include "Fgd.h"
#include "VertexBuffer.h"

struct EntCube
{
	vec3 mins;
	vec3 maxs;
	COLOR4 color;
	COLOR4 sel_color;

	VertexBuffer* axesBuffer;
	VertexBuffer* cubeBuffer;
	VertexBuffer* selectBuffer; // red coloring for selected ents
	VertexBuffer* wireframeBuffer; // yellow outline for selected ents

	bool Textured;

	EntCube()
	{
		axesBuffer = cubeBuffer = selectBuffer = wireframeBuffer = NULL;
		mins = maxs = vec3();
		color = COLOR4(255, 255, 255, 255);
		sel_color = { 255, 255, 0, 255 };
		Textured = false;
	}
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
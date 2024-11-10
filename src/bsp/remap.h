#pragma once
class Bsp;
#include <vector>
// excludes entities
class STRUCTCOUNT
{
public:
	unsigned int planes;
	unsigned int texInfos;
	unsigned int leaves;
	unsigned int nodes;
	unsigned int clipnodes;
	unsigned int verts;
	unsigned int faces;
	unsigned int textures;
	unsigned int markSurfs;
	unsigned int surfEdges;
	unsigned int edges;
	unsigned int models;
	unsigned int texturedata;
	unsigned int lightdata;
	unsigned int visdata;

	STRUCTCOUNT();
	STRUCTCOUNT(Bsp* map);
	~STRUCTCOUNT();

	void add(const STRUCTCOUNT& other);
	void sub(const STRUCTCOUNT& other);
	bool allZero();
	void print_delete_stats(int indent);
};

// used to mark structures that are in use by a model
class STRUCTUSAGE
{
public:
	std::vector<bool> nodes;
	std::vector<bool> clipnodes;
	std::vector<bool> leaves;
	std::vector<bool> planes;
	std::vector<bool> verts;
	std::vector<bool> texInfo;
	std::vector<bool> faces;
	std::vector<bool> textures;
	std::vector<bool> markSurfs;
	std::vector<bool> surfEdges;
	std::vector<bool> edges;

	STRUCTCOUNT count; // size of each array
	STRUCTCOUNT sum;

	int modelIdx;

	STRUCTUSAGE();
	STRUCTUSAGE(Bsp* map);
	~STRUCTUSAGE() = default;

	void compute_sum();
};

// used to remap structure indexes to new locations
class STRUCTREMAP
{
public:
	std::vector<int> nodes;
	std::vector<int> clipnodes;
	std::vector<int> leaves;
	std::vector<int> planes;
	std::vector<int> verts;
	std::vector<int> texInfo;
	std::vector<int> faces;
	std::vector<int> textures;
	std::vector<int> markSurfs;
	std::vector<int> surfEdges;
	std::vector<int> edges;

	// don't try to update the same nodes twice
	std::vector<bool> visitedNodes;
	std::vector<bool> visitedClipnodes;
	std::vector<bool> visitedLeaves;
	std::vector<bool> visitedFaces;

	STRUCTCOUNT count; // size of each array
	STRUCTREMAP();
	STRUCTREMAP(Bsp* map);
	~STRUCTREMAP() = default;
};

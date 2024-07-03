#pragma once
#include "Polygon3D.h"
#include <map>

#define MAX_NAV_LEAVES 4096
#define MAX_NAV_LEAF_LINKS 128
#define MAX_MAP_CLIPNODE_LEAVES 65536 // doubled to account for each clipnode's child contents having its own ID

class Bsp;
class Entity;

struct LeafMesh {
	vec3 center;
	vec3 mins;
	vec3 maxs;
	int idx;
	vector<Polygon3D> leafFaces;
	
	// returns true if point is inside leaf volume
	bool isInside(vec3 p);
};

struct LeafNavLink {
	int16_t node; // which poly is linked to. -1 = end of links
	Polygon3D linkArea; // region in which leaves are making contact
	bool walkable;
};

struct LeafNavNode {
	LeafNavLink links[MAX_NAV_LEAF_LINKS];
	uint32_t flags;
	uint16_t id;

	// adds a link to node "node" on edge "edge" with height difference "zDist"
	bool addLink(int node, Polygon3D linkArea);
	int numLinks();
};


class LeafNavMesh {
public:
	LeafNavNode nodes[MAX_NAV_LEAVES];
	LeafMesh leaves[MAX_NAV_LEAVES];
	uint16_t leafMap[MAX_MAP_CLIPNODE_LEAVES]; // maps a BSP leaf index to nav mesh node index

	int numLeaves;

	LeafNavMesh();

	LeafNavMesh(vector<LeafMesh> polys);

	bool addLink(int from, int to, Polygon3D linkArea);

	void clear();

	vector<int> LeafNavMesh::AStarRoute(Bsp* map, int startNodeIdx, int endNodeIdx);

	float path_cost(int a, int b);

	int getNodeIdx(Bsp* map, Entity* ent);

private:
};
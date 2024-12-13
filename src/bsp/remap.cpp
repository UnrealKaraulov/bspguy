#include "remap.h"
#include "Bsp.h"
#include "log.h"

STRUCTCOUNT::STRUCTCOUNT()
{
	planes = texInfos = leaves
		= nodes = clipnodes = verts
		= faces = textures = markSurfs
		= surfEdges = edges = models
		= texturedata = lightdata = visdata = 0;
}

STRUCTCOUNT::~STRUCTCOUNT()
{
	planes = texInfos = leaves
		= nodes = clipnodes = verts
		= faces = textures = markSurfs
		= surfEdges = edges = models
		= texturedata = lightdata = visdata = 0;
}

STRUCTCOUNT::STRUCTCOUNT(Bsp* map)
{
	planes = map->bsp_header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	texInfos = map->bsp_header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	leaves = map->bsp_header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF32);
	nodes = map->bsp_header.lump[LUMP_NODES].nLength / sizeof(BSPNODE32);
	clipnodes = map->bsp_header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE32);
	verts = map->bsp_header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	faces = map->bsp_header.lump[LUMP_FACES].nLength / sizeof(BSPFACE32);
	textures = *((int*)(map->lumps[LUMP_TEXTURES].data()));
	markSurfs = map->bsp_header.lump[LUMP_MARKSURFACES].nLength / sizeof(int);
	surfEdges = map->bsp_header.lump[LUMP_SURFEDGES].nLength / sizeof(int);
	edges = map->bsp_header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE32);
	models = map->bsp_header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	texturedata = map->bsp_header.lump[LUMP_TEXTURES].nLength;
	lightdata = map->bsp_header.lump[LUMP_LIGHTING].nLength;
	visdata = map->bsp_header.lump[LUMP_VISIBILITY].nLength;
}

void STRUCTCOUNT::add(const STRUCTCOUNT& other)
{
	planes += other.planes;
	texInfos += other.texInfos;
	leaves += other.leaves;
	nodes += other.nodes;
	clipnodes += other.clipnodes;
	verts += other.verts;
	faces += other.faces;
	textures += other.textures;
	markSurfs += other.markSurfs;
	surfEdges += other.surfEdges;
	edges += other.edges;
	models += other.models;
	texturedata += other.texturedata;
	lightdata += other.lightdata;
	visdata += other.visdata;
}

void STRUCTCOUNT::sub(const STRUCTCOUNT& other)
{
	planes -= other.planes;
	texInfos -= other.texInfos;
	leaves -= other.leaves;
	nodes -= other.nodes;
	clipnodes -= other.clipnodes;
	verts -= other.verts;
	faces -= other.faces;
	textures -= other.textures;
	markSurfs -= other.markSurfs;
	surfEdges -= other.surfEdges;
	edges -= other.edges;
	models -= other.models;
	texturedata -= other.texturedata;
	lightdata -= other.lightdata;
	visdata -= other.visdata;
}

bool STRUCTCOUNT::allZero()
{
	STRUCTCOUNT zeros = STRUCTCOUNT();
	return memcmp(&zeros, this, sizeof(zeros)) == 0;
}

void print_stat(int indent, int stat, const char* data)
{
	if (stat == 0)
	{
		return;
	}
	int statabs = abs(stat);

	for (int i = 0; i < indent; i++)
		print_log("    ");
	const char* plural = "s";
	if (std::string(data) == "vertex")
	{
		plural = "es";
	}

	print_log("{} {} {}{}\n", stat > 0 ? "Deleted" : "Added", statabs, data, statabs > 1 ? plural : "");
}

void print_stat_mem(int indent, int bytes, const char* data)
{
	if (bytes == 0)
	{
		return;
	}
	for (int i = 0; i < indent; i++)
		print_log("    ");
	print_log("{} {:.2f} KB of {}\n", bytes > 0 ? "Deleted" : "Added", (abs(bytes) / 1024.0f), data);
}

void STRUCTCOUNT::print_delete_stats(int indent)
{
	print_stat(indent, models, "model");
	print_stat(indent, planes, "plane");
	print_stat(indent, verts, "vertex");
	print_stat(indent, nodes, "node");
	print_stat(indent, texInfos, "texinfo");
	print_stat(indent, faces, "face");
	print_stat(indent, clipnodes, "clipnode");
	print_stat(indent, leaves, "leave");
	print_stat(indent, markSurfs, "marksurface");
	print_stat(indent, surfEdges, "surfedge");
	print_stat(indent, edges, "edge");
	print_stat(indent, textures, "texture");
	print_stat_mem(indent, texturedata, "texture data");
	print_stat_mem(indent, lightdata, "lightmap data");
	print_stat_mem(indent, visdata, "VIS data");
}

STRUCTUSAGE::STRUCTUSAGE()
{
	count = STRUCTCOUNT();
	sum = STRUCTCOUNT();
	modelIdx = 0;
}
STRUCTUSAGE::STRUCTUSAGE(Bsp* map)
{
	modelIdx = 0;

	count = STRUCTCOUNT(map);
	sum = STRUCTCOUNT();

	nodes.resize(count.nodes, false);
	clipnodes.resize(count.clipnodes, false);
	leaves.resize(count.leaves, false);
	planes.resize(count.planes, false);
	verts.resize(count.verts, false);
	texInfo.resize(count.texInfos, false);
	faces.resize(count.faces, false);
	textures.resize(count.textures, false);
	markSurfs.resize(count.markSurfs, false);
	surfEdges.resize(count.surfEdges, false);
	edges.resize(count.edges, false);
}

void STRUCTUSAGE::compute_sum()
{
	memset(&sum, 0, sizeof(STRUCTCOUNT));
	for (unsigned int i = 0; i < count.planes; i++) sum.planes += planes[i];
	for (unsigned int i = 0; i < count.texInfos; i++) sum.texInfos += texInfo[i];
	for (unsigned int i = 0; i < count.leaves; i++) sum.leaves += leaves[i];
	for (unsigned int i = 0; i < count.nodes; i++) sum.nodes += nodes[i];
	for (unsigned int i = 0; i < count.clipnodes; i++) sum.clipnodes += clipnodes[i];
	for (unsigned int i = 0; i < count.verts; i++) sum.verts += verts[i];
	for (unsigned int i = 0; i < count.faces; i++) sum.faces += faces[i];
	for (unsigned int i = 0; i < count.textures; i++) sum.textures += textures[i];
	for (unsigned int i = 0; i < count.markSurfs; i++) sum.markSurfs += markSurfs[i];
	for (unsigned int i = 0; i < count.surfEdges; i++) sum.surfEdges += surfEdges[i];
	for (unsigned int i = 0; i < count.edges; i++) sum.edges += edges[i];
}

STRUCTREMAP::STRUCTREMAP()
{
	count = STRUCTCOUNT();
}
STRUCTREMAP::STRUCTREMAP(Bsp* map)
{
	count = STRUCTCOUNT(map);

	nodes.resize(count.nodes,0);
	clipnodes.resize(count.clipnodes, 0);
	leaves.resize(count.leaves, 0);
	planes.resize(count.planes, 0);
	verts.resize(count.verts, 0);
	texInfo.resize(count.texInfos, 0);
	faces.resize(count.faces, 0);
	textures.resize(count.textures, 0);
	markSurfs.resize(count.markSurfs, 0);
	surfEdges.resize(count.surfEdges, 0);
	edges.resize(count.edges, 0);

	visitedNodes.resize(count.nodes,false);
	visitedClipnodes.resize(count.clipnodes, false);
	visitedLeaves.resize(count.leaves, false);
	visitedFaces.resize(count.faces, false);

	// remap to the same index by default
	for (unsigned int i = 0; i < count.nodes; i++) nodes[i] = i;
	for (unsigned int i = 0; i < count.clipnodes; i++) clipnodes[i] = i;
	for (unsigned int i = 0; i < count.leaves; i++) leaves[i] = i;
	for (unsigned int i = 0; i < count.planes; i++) planes[i] = i;
	for (unsigned int i = 0; i < count.verts; i++) verts[i] = i;
	for (unsigned int i = 0; i < count.texInfos; i++) texInfo[i] = i;
	for (unsigned int i = 0; i < count.faces; i++) faces[i] = i;
	for (unsigned int i = 0; i < count.textures; i++) textures[i] = i;
	for (unsigned int i = 0; i < count.markSurfs; i++) markSurfs[i] = i;
	for (unsigned int i = 0; i < count.surfEdges; i++) surfEdges[i] = i;
	for (unsigned int i = 0; i < count.edges; i++) edges[i] = i;
}

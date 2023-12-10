#pragma once
#include "remap.h"
#include "Bsp.h"

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
	textures = *((int*)(map->lumps[LUMP_TEXTURES]));
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
	nodes = clipnodes = leaves = planes = verts = texInfo = faces
		= textures = markSurfs = surfEdges = edges = 0;
	count = STRUCTCOUNT();
	sum = STRUCTCOUNT();
	modelIdx = 0;
}
STRUCTUSAGE::STRUCTUSAGE(Bsp* map)
{
	modelIdx = 0;

	count = STRUCTCOUNT(map);
	sum = STRUCTCOUNT();

	nodes = new bool[count.nodes + 1];
	clipnodes = new bool[count.clipnodes + 1];
	leaves = new bool[count.leaves + 1];
	planes = new bool[count.planes + 1];
	verts = new bool[count.verts + 1];
	texInfo = new bool[count.texInfos + 1];
	faces = new bool[count.faces + 1];
	textures = new bool[count.textures + 1];
	markSurfs = new bool[count.markSurfs + 1];
	surfEdges = new bool[count.surfEdges + 1];
	edges = new bool[count.edges + 1];

	memset(nodes, 0, count.nodes * sizeof(bool));
	memset(clipnodes, 0, count.clipnodes * sizeof(bool));
	memset(leaves, 0, count.leaves * sizeof(bool));
	memset(planes, 0, count.planes * sizeof(bool));
	memset(verts, 0, count.verts * sizeof(bool));
	memset(texInfo, 0, count.texInfos * sizeof(bool));
	memset(faces, 0, count.faces * sizeof(bool));
	memset(textures, 0, count.textures * sizeof(bool));
	memset(markSurfs, 0, count.markSurfs * sizeof(bool));
	memset(surfEdges, 0, count.surfEdges * sizeof(bool));
	memset(edges, 0, count.edges * sizeof(bool));
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

STRUCTUSAGE::~STRUCTUSAGE()
{
	if (nodes)
		delete[] nodes;
	if (clipnodes)
		delete[] clipnodes;
	if (leaves)
		delete[] leaves;
	if (planes)
		delete[] planes;
	if (verts)
		delete[] verts;
	if (texInfo)
		delete[] texInfo;
	if (faces)
		delete[] faces;
	if (textures)
		delete[] textures;
	if (markSurfs)
		delete[] markSurfs;
	if (surfEdges)
		delete[] surfEdges;
	if (edges)
		delete[] edges;

	nodes = clipnodes = leaves = planes = verts = texInfo = faces
		= textures = markSurfs = surfEdges = edges = 0;
}
STRUCTREMAP::STRUCTREMAP()
{
	nodes = clipnodes = leaves = planes = verts = texInfo = faces
		= textures = markSurfs = surfEdges = edges = 0;
	visitedNodes = visitedClipnodes = visitedLeaves = visitedFaces = 0;
	count = STRUCTCOUNT();
}
STRUCTREMAP::STRUCTREMAP(Bsp* map)
{
	count = STRUCTCOUNT(map);

	nodes = new int[count.nodes + 1];
	clipnodes = new int[count.clipnodes + 1];
	leaves = new int[count.leaves + 1];
	planes = new int[count.planes + 1];
	verts = new int[count.verts + 1];
	texInfo = new int[count.texInfos + 1];
	faces = new int[count.faces + 1];
	textures = new int[count.textures + 1];
	markSurfs = new int[count.markSurfs + 1];
	surfEdges = new int[count.surfEdges + 1];
	edges = new int[count.edges + 1];

	visitedNodes = new bool[count.nodes];
	visitedClipnodes = new bool[count.clipnodes];
	visitedLeaves = new bool[count.leaves];
	visitedFaces = new bool[count.faces];

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

	memset(visitedClipnodes, 0, count.clipnodes * sizeof(bool));
	memset(visitedNodes, 0, count.nodes * sizeof(bool));
	memset(visitedFaces, 0, count.faces * sizeof(bool));
	memset(visitedLeaves, 0, count.leaves * sizeof(bool));
}

STRUCTREMAP::~STRUCTREMAP()
{
	if (nodes)
		delete[] nodes;
	if (clipnodes)
		delete[] clipnodes;
	if (leaves)
		delete[] leaves;
	if (planes)
		delete[] planes;
	if (verts)
		delete[] verts;
	if (texInfo)
		delete[] texInfo;
	if (faces)
		delete[] faces;
	if (textures)
		delete[] textures;
	if (markSurfs)
		delete[] markSurfs;
	if (surfEdges)
		delete[] surfEdges;
	if (edges)
		delete[] edges;

	if (visitedClipnodes)
		delete[] visitedClipnodes;
	if (visitedNodes)
		delete[] visitedNodes;
	if (visitedFaces)
		delete[] visitedFaces;
	if (visitedLeaves)
		delete[] visitedLeaves;

	nodes = clipnodes = leaves = planes = verts = texInfo = faces
		= textures = markSurfs = surfEdges = edges = 0;
	visitedNodes = visitedClipnodes = visitedLeaves = visitedFaces = 0;
}
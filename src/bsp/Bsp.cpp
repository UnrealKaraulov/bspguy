#include "lang.h"
#include "Bsp.h"
#include "util.h"
#include <algorithm>
#include <sstream>
#include "lodepng.h"
#include "rad.h"
#include "vis.h"
#include "remap.h"
#include "Settings.h"
#include "Renderer.h"
#include "BspRenderer.h"
#include <set>
#include <winding.h>
#include "Wad.h"
#include <vector>
#include "forcecrc32.h"
#include "quantizer.h"

#include <unordered_set>

typedef std::map< std::string, vec3 > mapStringToVector;

vec3 default_hull_extents[MAX_MAP_HULLS] = {
	vec3(0.0f,  0.0f,  0.0f),	// hull 0
	vec3(16.0f, 16.0f, 36.0f),	// hull 1
	vec3(32.0f, 32.0f, 64.0f),	// hull 2
	vec3(16.0f, 16.0f, 18.0f)	// hull 3
};

int g_sort_mode = SORT_CLIPNODES;

void Bsp::init_empty_bsp()
{
	lumps = new unsigned char* [HEADER_LUMPS];

	bsp_header.nVersion = 30;

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		replacedLump[i] = true;
		lumps[i] = new unsigned char[512];
		memset(lumps[i], 0, 512);
		bsp_header.lump[i].nOffset = 0;
		bsp_header.lump[i].nLength = 4;
	}

	bsp_name = "empty";
	bsp_path = "empty.bsp";
	bsp_valid = true;
	renderer = NULL;

	print_log(get_localized_string(LANG_0035));

	renderer = NULL;
	bsp_valid = true;

	update_lump_pointers();

	load_ents();

	Entity* ent = new Entity();
	ent->setOrAddKeyvalue("mapversion", "220");
	ent->setOrAddKeyvalue("classname", "worldspawn");
	ents.push_back(ent);

	create_leaf(CONTENTS_EMPTY);
	update_ent_lump();
	update_lump_pointers();
	/*/
		float size = 64;
		COLOR3* imageData = new COLOR3[64 * 64];
		vec3 mins = vec3(-size, -size, -size);
		vec3 maxs = vec3(size, size, size);
		modelCount = 0;
		memset(imageData, 0xFF / 2, 64 * 64 * 3);
		add_texture("WHITETEX", (unsigned char*)imageData, 64, 64);
		create_solid(mins, maxs, 0);

		models[0].iHeadnodes[0] = models[0].iHeadnodes[1] =
			models[0].iHeadnodes[2] = models[0].iHeadnodes[3] = -1;

		update_lump_pointers();
		update_ent_lump();

		modelCount = 1;

		while (models[0].nVisLeafs >= leafCount)
			create_leaf(CONTENTS_EMPTY);*/



	BSPNODE32& node = nodes[0];
	node.iChildren[0] = node.iChildren[1] = -1;

	BSPCLIPNODE32& cnode = clipnodes[0];
	cnode.iChildren[0] = cnode.iChildren[1] = -1;
}

void Bsp::selectModelEnt()
{
	if (!is_bsp_model || ents.empty())
		return;
	for (size_t i = 0; i < mapRenderers.size(); i++)
	{
		BspRenderer* mapRender = mapRenderers[i];
		if (!mapRender)
			continue;
		Bsp* map = mapRender->map;
		if (map && map == this->parentMap)
		{
			g_app->clearSelection();
			g_app->selectMap(map);
			vec3 worldOrigin = map->ents[0]->getOrigin();
			for (size_t n = 1; n < map->ents.size(); n++)
			{
				if (map->ents[n]->hasKey("model") && (map->ents[n]->getOrigin() + worldOrigin) == ents[0]->getOrigin())
				{
					g_app->pickInfo.SetSelectedEnt((int)n);
					return;
				}
			}
			return;
		}
	}
}

Bsp::Bsp()
{
	is_protected = false;
	is_bsp_model = false;
	is_mdl_model = false;
	mdl = NULL;

	undo_lightmaps = NULL;
	undo_lightmaps_count = 0;

	is_bsp30ext = false;
	is_bsp2 = false;
	is_bsp2_old = false;
	is_32bit_clipnodes = false;
	is_bsp29 = false;
	is_broken_clipnodes = false;
	is_blue_shift = false;
	is_colored_lightmap = true;

	extralumps = NULL;

	force_skip_crc = false;

	bsp_header = BSPHEADER();
	bsp_header_ex = BSPHEADER_EX();
	parentMap = NULL;

	init_empty_bsp();
}

Bsp::Bsp(std::string fpath)
{
	is_protected = false;
	is_bsp_model = false;
	is_mdl_model = false;
	mdl = NULL;

	undo_lightmaps = NULL;
	undo_lightmaps_count = 0;

	is_bsp30ext = false;
	is_bsp2 = false;
	is_bsp2_old = false;
	is_32bit_clipnodes = false;
	is_bsp29 = false;
	is_broken_clipnodes = false;
	is_blue_shift = false;
	is_colored_lightmap = true;
	is_texture_pal = true;

	force_skip_crc = false;

	bsp_header = BSPHEADER();
	bsp_header_ex = BSPHEADER_EX();
	parentMap = NULL;

	if (fpath.empty())
	{
		fpath = "newmap.bsp";
		init_empty_bsp();
		return;
	}
	else
	{
		std::string lowerpath = toLowerCase(fpath);
		if (lowerpath.ends_with(".mdl"))
		{
			is_mdl_model = true;
			if (fileExists(fpath))
			{
				mdl = AddNewModelToRender(fpath);
			}
			init_empty_bsp();
			return;
		}
	}
	if (!fileExists(fpath))
	{
		if (fpath.size() < 4 || fpath.rfind(".bsp") != fpath.size() - 4)
		{
			fpath = fpath + ".bsp";
		}
	}

	bsp_path = fpath;
	bsp_name = stripExt(basename(fpath));
	bsp_valid = false;

	if (!fileExists(fpath))
	{
		print_log(get_localized_string(LANG_0036), fpath);
		return;
	}

	if (!load_lumps(fpath))
	{
		print_log(get_localized_string(LANG_0037), fpath);
		return;
	}

	print_log(get_localized_string(LANG_0038), reverse_bits(originCrc32));

	std::string entFilePath;
	if (g_settings.sameDirForEnt) {
		entFilePath = fpath.substr(0, fpath.size() - 4) + ".ent";
	}
	else {
		entFilePath = g_working_dir + (bsp_name + ".ent");
	}

	if (g_settings.autoImportEnt && fileExists(entFilePath)) {
		print_log(get_localized_string(LANG_0039), entFilePath);

		int len;
		char* newlump = loadFile(entFilePath, len);
		replace_lump(LUMP_ENTITIES, newlump, len);
	}

	load_ents();
	update_lump_pointers();

	if (modelCount > 0)
	{
		while (true)
		{
			BSPMODEL& lastModel = models[modelCount - 1];
			if (lastModel.nVisLeafs == 0 &&
				lastModel.iHeadnodes[0] == 0 &&
				lastModel.iHeadnodes[1] == 0 &&
				lastModel.iHeadnodes[2] == 0 &&
				lastModel.iHeadnodes[3] == 0 &&
				lastModel.iFirstFace == 0 &&
				abs(lastModel.vOrigin.z - 9999.0) < 0.01 &&
				lastModel.nFaces == 0)
			{
				print_log(get_localized_string(LANG_0040), modelCount - 1);
				bsp_header.lump[LUMP_MODELS].nLength -= sizeof(BSPMODEL);
				update_lump_pointers();
			}
			else
				break;
		}
	}

	std::set<int> used_models; // Protected map
	used_models.insert(0);

	for (auto const& s : ents)
	{
		int ent_mdl_id = s->getBspModelIdx();
		if (ent_mdl_id >= 0)
		{
			if (!used_models.count(ent_mdl_id))
			{
				used_models.insert(ent_mdl_id);
			}
		}
	}

	for (int i = 0; i < modelCount; i++)
	{
		if (!used_models.count(i))
		{
			print_log(get_localized_string(LANG_0041), bsp_name, i);
		}
	}


	renderer = NULL;
	bsp_valid = true;


	if (!ents.empty() && !ents[0]->hasKey("CRC") && !force_skip_crc)
	{
		print_log(get_localized_string(LANG_0042));
		ents[0]->addKeyvalue("CRC", std::to_string(reverse_bits(originCrc32)));
		update_ent_lump();
	}

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		replacedLump[i] = false;
	}

	save_undo_lightmaps();
}

Bsp::~Bsp()
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (lumps[i] && replacedLump[i])
		{
			delete[] lumps[i];
		}
	}
	delete[] lumps;

	for (size_t i = 0; i < ents.size(); i++)
	{
		delete ents[i];
	}
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		replacedLump[i] = false;
	}

	//if (mdl)
	//{
	//	delete mdl;
	//}
}



void Bsp::get_bounding_box(vec3& mins, vec3& maxs)
{
	if (modelCount)
	{
		BSPMODEL& thisWorld = models[0];

		// the model bounds are little bigger than the actual vertices bounds in the map,
		// but if you go by the vertices then there will be collision problems.

		mins = thisWorld.nMins;
		maxs = thisWorld.nMaxs;
	}
	else
	{
		mins = maxs = vec3();
	}
}

void Bsp::get_model_vertex_bounds(int modelIdx, vec3& mins, vec3& maxs)
{
	if (modelIdx < 0)
		return;
	mins = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
	maxs = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);

	BSPMODEL& model = models[modelIdx];
	/*auto verts = getModelVerts(modelIdx);
	for (auto const& s : verts)
	{
		if (s.pos.x < mins.x)
		{
			mins.x = s.pos.x;
		}
		if (s.pos.y < mins.y)
		{
			mins.y = s.pos.y;
		}
		if (s.pos.z < mins.z)
		{
			mins.z = s.pos.z;
		}

		if (s.pos.x > maxs.x)
		{
			maxs.x = s.pos.x;
		}
		if (s.pos.y > maxs.y)
		{
			maxs.y = s.pos.y;
		}
		if (s.pos.z > maxs.z)
		{
			maxs.z = s.pos.z;
		}
	}
	*/
	for (int i = 0; i < model.nFaces; i++)
	{
		BSPFACE32& face = faces[model.iFirstFace + i];

		for (int e = 0; e < face.nEdges; e++)
		{
			int edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE32& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

			expandBoundingBox(verts[vertIdx], mins, maxs);
		}
	}
}

std::vector<TransformVert> Bsp::getModelVerts(int modelIdx)
{
	std::vector<TransformVert> allVerts;
	std::set<int> visited;

	BSPMODEL& model = models[modelIdx];

	for (int i = 0; i < model.nFaces; i++)
	{
		BSPFACE32& face = faces[model.iFirstFace + i];

		for (int e = 0; e < face.nEdges; e++)
		{
			int edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE32& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

			if (!visited.count(vertIdx))
			{
				TransformVert vert = TransformVert();
				vert.startPos = vert.undoPos = vert.pos = verts[vertIdx];
				vert.ptr = &verts[vertIdx];

				allVerts.push_back(vert);
				visited.insert(vertIdx);
			}
		}
	}

	return allVerts;
}

bool Bsp::getModelPlaneIntersectVerts(int modelIdx, std::vector<TransformVert>& outVerts)
{
	std::vector<int> nodePlaneIndexes;
	BSPMODEL& model = models[modelIdx];
	getNodePlanes(model.iHeadnodes[0], nodePlaneIndexes);

	return getModelPlaneIntersectVerts(modelIdx, nodePlaneIndexes, outVerts);
}

bool Bsp::getModelPlaneIntersectVerts(int modelIdx, const std::vector<int>& nodePlaneIndexes, std::vector<TransformVert>& outVerts)
{
	// TODO: this only works for convex objects. A concave solid will need
	// to get verts by creating convex hulls from each solid node in the tree.
	// That can be done by recursively cutting a huge cube but there's probably
	// a better way.
	std::vector<BSPPLANE> nodePlanes;

	BSPMODEL& model = models[modelIdx];

	outVerts.clear();

	// TODO: model center doesn't have to be inside all planes, even for convex objects(?)
	vec3 modelCenter = model.nMins + (model.nMaxs - model.nMins) * 0.5f;
	for (size_t i = 0; i < nodePlaneIndexes.size(); i++)
	{
		nodePlanes.push_back(planes[nodePlaneIndexes[i]]);
		BSPPLANE& plane = nodePlanes[i];
		vec3 planePoint = plane.vNormal * plane.fDist;
		vec3 planeDir = (planePoint - modelCenter).normalize(1.0f);
		if (dotProduct(planeDir, plane.vNormal) > 0.0f)
		{
			plane.vNormal *= -1.0f;
			plane.fDist *= -1.0f;
		}
	}

	std::vector<vec3> nodeVerts = getPlaneIntersectVerts(nodePlanes);

	if (nodeVerts.size() < 4)
	{
		return false; // solid is either 2D or there were no intersections (not convex)
	}

	// coplanar test
	for (size_t i = 0; i < nodePlanes.size(); i++)
	{
		for (size_t k = 0; k < nodePlanes.size(); k++)
		{
			if (i == k)
				continue;

			if (nodePlanes[i].vNormal == nodePlanes[k].vNormal && abs(nodePlanes[i].fDist - nodePlanes[k].fDist) < ON_EPSILON)
			{
				return false;
			}
		}
	}

	// convex test
	for (size_t k = 0; k < nodePlanes.size(); k++)
	{
		if (!vertsAllOnOneSide(nodeVerts, nodePlanes[k]))
		{
			return false;
		}
	}

	for (size_t k = 0; k < nodeVerts.size(); k++)
	{
		vec3 v = nodeVerts[k];

		TransformVert hullVert;
		hullVert.pos = hullVert.undoPos = hullVert.startPos = v;
		hullVert.ptr = NULL;
		hullVert.selected = false;

		for (size_t i = 0; i < nodePlanes.size(); i++)
		{
			BSPPLANE& p = nodePlanes[i];
			if (abs(dotProduct(v, p.vNormal) - p.fDist) < ON_EPSILON)
			{
				hullVert.iPlanes.push_back(nodePlaneIndexes[i]);
			}
		}

		for (int i = 0; i < model.nFaces && !hullVert.ptr; i++)
		{
			BSPFACE32& face = faces[model.iFirstFace + i];

			for (int e = 0; e < face.nEdges && !hullVert.ptr; e++)
			{
				int edgeIdx = surfedges[face.iFirstEdge + e];
				BSPEDGE32& edge = edges[abs(edgeIdx)];
				int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

				if (verts[vertIdx] != v)
				{
					continue;
				}

				hullVert.ptr = &verts[vertIdx];
			}
		}

		outVerts.push_back(hullVert);
	}

	return true;
}

void Bsp::getNodePlanes(int iNode, std::vector<int>& nodePlanes)
{
	if (iNode < 0)
		return;
	BSPNODE32& node = nodes[iNode];
	nodePlanes.push_back(node.iPlane);

	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			getNodePlanes(node.iChildren[i], nodePlanes);
		}
	}
}

void Bsp::getClipNodePlanes(int iClipNode, std::vector<int>& nodePlanes)
{
	BSPCLIPNODE32& node = clipnodes[iClipNode];
	nodePlanes.push_back(node.iPlane);

	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			getClipNodePlanes(node.iChildren[i], nodePlanes);
		}
	}
}

std::vector<NodeVolumeCuts> Bsp::get_model_leaf_volume_cuts(int modelIdx, int hullIdx)
{
	std::vector<NodeVolumeCuts> modelVolumeCuts;

	if (hullIdx >= 0 && hullIdx < MAX_MAP_HULLS)
	{
		int nodeIdx = models[modelIdx].iHeadnodes[hullIdx];
		bool is_valid_node = false;

		if (hullIdx == 0)
		{
			is_valid_node = nodeIdx >= 0 && nodeIdx < nodeCount;
		}
		else
		{
			is_valid_node = nodeIdx >= 0 && nodeIdx < clipnodeCount;
		}

		if (nodeIdx >= 0 && is_valid_node)
		{
			std::vector<BSPPLANE> clipOrder;
			if (hullIdx == 0)
			{
				get_node_leaf_cuts(nodeIdx, 0, clipOrder, modelVolumeCuts);
			}
			else
			{
				get_clipnode_leaf_cuts(nodeIdx, 0, clipOrder, modelVolumeCuts);
			}
		}
	}
	return modelVolumeCuts;
}

void Bsp::get_clipnode_leaf_cuts(int iNode, int iStartNode, std::vector<BSPPLANE>& clipOrder, std::vector<NodeVolumeCuts>& output)
{
	BSPCLIPNODE32& node = clipnodes[iNode];

	if (node.iPlane < 0 || node.iPlane >= planeCount)
	{
		return;
	}

	for (int i = 0; i < 2; i++)
	{
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0)
		{
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		if (node.iChildren[i] == iStartNode)
		{
			print_log(get_localized_string(LANG_0043), node.iChildren[i]);
			return;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0)
		{
			get_clipnode_leaf_cuts(node.iChildren[i], iStartNode, clipOrder, output);
		}
		else if (node.iChildren[i] != CONTENTS_EMPTY)
		{
			NodeVolumeCuts nodeVolumeCuts;
			nodeVolumeCuts.nodeIdx = iNode;

			if (clipOrder.size())
			{
				// reverse order of branched planes = order of cuts to the world which define this node's volume
				// https://qph.fs.quoracdn.net/main-qimg-2a8faad60cc9d437b58a6e215e6e874d
				for (int k = (int)clipOrder.size() - 1; k >= 0; k--)
				{
					nodeVolumeCuts.cuts.push_back(clipOrder[k]);
				}
			}
			output.push_back(nodeVolumeCuts);
		}

		clipOrder.pop_back();
	}
}


void Bsp::get_leaf_nodes(int leaf, std::vector<int>& out_nodes)
{
	for (int i = 0; i < nodeCount; i++)
	{
		if (~nodes[i].iChildren[0] == leaf)
		{
			out_nodes.push_back(i);
		}
		if (~nodes[i].iChildren[1] == leaf)
		{
			out_nodes.push_back(i);
		}
	}
}

void Bsp::get_node_leaf_cuts(int iNode, int iStartNode, std::vector<BSPPLANE>& clipOrder, std::vector<NodeVolumeCuts>& output)
{
	BSPNODE32& node = nodes[iNode];

	for (int i = 0; i < 2; i++)
	{
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0)
		{
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}

		if (node.iChildren[i] == iStartNode)
		{
			print_log(get_localized_string(LANG_0044), node.iChildren[i]);
			return;
		}

		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0)
		{
			get_node_leaf_cuts(node.iChildren[i], iStartNode, clipOrder, output);
		}
		else if (leaves[~node.iChildren[i]].nContents != CONTENTS_EMPTY)
		{
			NodeVolumeCuts nodeVolumeCuts;
			nodeVolumeCuts.nodeIdx = iNode;
			if (clipOrder.size())
			{
				// reverse order of branched planes = order of cuts to the world which define this node's volume
				// https://qph.fs.quoracdn.net/main-qimg-2a8faad60cc9d437b58a6e215e6e874d
				for (int k = (int)clipOrder.size() - 1; k >= 0; k--)
				{
					nodeVolumeCuts.cuts.push_back(clipOrder[k]);
				}
			}
			output.push_back(nodeVolumeCuts);
		}

		clipOrder.pop_back();
	}
}

bool Bsp::is_convex(int modelIdx)
{
	return models[modelIdx].iHeadnodes[0] >= 0 && is_node_hull_convex(models[modelIdx].iHeadnodes[0]);
}

bool Bsp::is_node_hull_convex(int iNode)
{
	BSPNODE32& node = nodes[iNode];

	// convex models always have one node pointing to empty space
	if (node.iChildren[0] >= 0 && node.iChildren[1] >= 0)
	{
		return false;
	}

	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			if (!is_node_hull_convex(node.iChildren[i]))
			{
				return false;
			}
		}
	}

	return true;
}

int Bsp::addTextureInfo(BSPTEXTUREINFO& copy)
{
	BSPTEXTUREINFO* newInfos = new BSPTEXTUREINFO[texinfoCount + 1];
	memcpy(newInfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

	int newIdx = texinfoCount;
	newInfos[newIdx] = copy;

	replace_lump(LUMP_TEXINFO, newInfos, (texinfoCount + 1) * sizeof(BSPTEXTUREINFO));

	return newIdx;
}

std::vector<ScalableTexinfo> Bsp::getScalableTexinfos(int modelIdx)
{
	BSPMODEL& model = models[modelIdx];
	std::vector<ScalableTexinfo> scalable;
	std::set<int> visitedTexinfos;

	for (int k = 0; k < model.nFaces; k++)
	{
		BSPFACE32& face = faces[model.iFirstFace + k];
		int texinfoIdx = face.iTextureInfo;

		if (!visitedTexinfos.count(texinfoIdx))
		{
			//texinfoIdx = face.iTextureInfo = addTextureInfo(texinfos[texinfoIdx]);
			continue;
		}
		visitedTexinfos.insert(texinfoIdx);

		ScalableTexinfo st;
		st.oldS = texinfos[texinfoIdx].vS;
		st.oldT = texinfos[texinfoIdx].vT;
		st.oldShiftS = texinfos[texinfoIdx].shiftS;
		st.oldShiftT = texinfos[texinfoIdx].shiftT;
		st.texinfoIdx = texinfoIdx;
		st.planeIdx = face.iPlane;
		st.faceIdx = model.iFirstFace + k;
		scalable.push_back(st);
	}

	return scalable;
}

bool Bsp::vertex_manipulation_sync(int modelIdx, std::vector<TransformVert>& hullVerts, bool convexCheckOnly)
{
	if (modelIdx < 0)
		return false;

	std::set<int> affectedPlanes;

	std::map<int, std::vector<vec3>> planeVerts;
	std::vector<vec3> allVertPos;

	for (size_t i = 0; i < hullVerts.size(); i++)
	{
		for (size_t k = 0; k < hullVerts[i].iPlanes.size(); k++)
		{
			int iPlane = hullVerts[i].iPlanes[k];
			if (!affectedPlanes.count(hullVerts[i].iPlanes[k]))
				affectedPlanes.insert(hullVerts[i].iPlanes[k]);
			planeVerts[iPlane].push_back(hullVerts[i].pos);
		}
		allVertPos.push_back(hullVerts[i].pos);
	}

	int planeUpdates = 0;
	std::map<int, BSPPLANE> newPlanes;
	std::map<int, bool> shouldFlipChildren;
	for (auto it = planeVerts.begin(); it != planeVerts.end(); ++it)
	{
		int iPlane = it->first;

		std::vector<vec3>& tverts = it->second;

		if (tverts.size() < 3)
		{
			if (g_settings.verboseLogs)
				print_log(get_localized_string(LANG_0045));
			return false; // invalid solid
		}

		BSPPLANE newPlane;
		if (!getPlaneFromVerts(tverts, newPlane.vNormal, newPlane.fDist))
		{
			if (g_settings.verboseLogs)
				print_log(get_localized_string(LANG_0046));
			return false; // verts not planar
		}

		vec3 oldNormal = planes[iPlane].vNormal;
		if (dotProduct(oldNormal, newPlane.vNormal) < 0.0f)
		{
			newPlane.vNormal = newPlane.vNormal.invert(); // TODO: won't work for big changes
			newPlane.fDist = -newPlane.fDist;
		}

		BSPPLANE testPlane;
		bool expectedFlip = testPlane.update(planes[iPlane].vNormal, planes[iPlane].fDist);
		bool flipped = newPlane.update(newPlane.vNormal, newPlane.fDist);

		testPlane = newPlane;

		// check that all verts are on one side of the plane.
		// plane inversions are ok according to hammer
		if (!vertsAllOnOneSide(allVertPos, testPlane))
		{
			return false;
		}

		newPlanes[iPlane] = newPlane;
		shouldFlipChildren[iPlane] = flipped != expectedFlip;
	}

	if (convexCheckOnly)
		return true;

	for (auto it = newPlanes.begin(); it != newPlanes.end(); ++it)
	{
		auto iPlane = it->first;
		BSPPLANE& newPlane = it->second;

		planes[iPlane] = newPlane;
		planeUpdates++;

		if (shouldFlipChildren[iPlane])
		{
			for (int i = 0; i < faceCount; i++)
			{
				BSPFACE32& face = faces[i];
				if (face.iPlane == iPlane)
				{
					face.nPlaneSide = face.nPlaneSide ? 0 : 1;
				}
			}
			for (int i = 0; i < nodeCount; i++)
			{
				BSPNODE32& node = nodes[i];
				if (node.iPlane == iPlane)
				{
					int temp = node.iChildren[0];
					node.iChildren[0] = node.iChildren[1];
					node.iChildren[1] = temp;
				}
			}
		}
	}

	//print_log(get_localized_string(LANG_0047),planeUpdates);

	BSPMODEL& model = models[modelIdx];
	getBoundingBox(allVertPos, model.nMins, model.nMaxs);
	return true;
}

bool Bsp::move(vec3 offset, int modelIdx, bool onlyModel, bool forceMove, bool logged)
{
	if (modelIdx < 0 || modelIdx >= modelCount)
	{
		print_log(get_localized_string(LANG_0048));
		return false;
	}

	BSPMODEL& target = models[modelIdx];

	// all ents should be moved if the world is being moved
	bool movingWorld = modelIdx == 0 && !onlyModel;

	// Submodels don't use leaves like the world model does. Only the contents of a leaf matters
	// for submodels. All other data is ignored. bspguy will reuse world leaves in submodels to 
	// save space, which means moving leaves for those models would likely break something else.
	// So, don't move leaves for submodels.
	bool dontMoveLeaves = !movingWorld;

	if (!forceMove && does_model_use_shared_structures(modelIdx))
		split_shared_model_structures(modelIdx);

	if (logged)
		g_progress.update("Moving structures", (int)(ents.size() - 1));

	if (movingWorld)
	{
		for (size_t i = 1; i < ents.size(); i++)
		{ // don't move the world entity
			if (logged)
				g_progress.tick();

			vec3 ori = ents[i]->getOrigin();
			ori += offset;

			if (ents[i]->hasKey("spawnorigin"))
			{
				vec3 spawnori = parseVector(ents[i]->keyvalues["spawnorigin"]);

				// entity not moved if destination is 0,0,0
				if (abs(spawnori.x) >= EPSILON || abs(spawnori.y) >= EPSILON || abs(spawnori.z) >= EPSILON)
				{
					ents[i]->setOrAddKeyvalue("spawnorigin", (spawnori + offset).toKeyvalueString());
				}
			}

			ents[i]->setOrAddKeyvalue("origin", ori.toKeyvalueString());
		}

		update_ent_lump();
	}

	target.nMins += offset;
	target.nMaxs += offset;
	if (abs(target.nMins.x) > FLT_MAX_COORD ||
		abs(target.nMins.y) > FLT_MAX_COORD ||
		abs(target.nMins.z) > FLT_MAX_COORD ||
		abs(target.nMaxs.x) > FLT_MAX_COORD ||
		abs(target.nMaxs.y) > FLT_MAX_COORD ||
		abs(target.nMaxs.z) > FLT_MAX_COORD)
	{
		print_log(get_localized_string(LANG_0049));
	}

	STRUCTUSAGE shouldBeMoved(this);
	mark_model_structures(modelIdx, &shouldBeMoved, dontMoveLeaves);

	for (int i = 0; i < nodeCount; i++)
	{
		if (!shouldBeMoved.nodes[i] && !forceMove)
		{
			continue;
		}

		BSPNODE32& node = nodes[i];

		if (abs((float)node.nMins[0] + offset.x) > FLT_MAX_COORD ||
			abs((float)node.nMaxs[0] + offset.x) > FLT_MAX_COORD ||
			abs((float)node.nMins[1] + offset.y) > FLT_MAX_COORD ||
			abs((float)node.nMaxs[1] + offset.y) > FLT_MAX_COORD ||
			abs((float)node.nMins[2] + offset.z) > FLT_MAX_COORD ||
			abs((float)node.nMaxs[2] + offset.z) > FLT_MAX_COORD)
		{
			print_log(get_localized_string(LANG_0050));
		}
		node.nMins[0] += offset.x;
		node.nMaxs[0] += offset.x;
		node.nMins[1] += offset.y;
		node.nMaxs[1] += offset.y;
		node.nMins[2] += offset.z;
		node.nMaxs[2] += offset.z;
	}

	for (int i = 1; i < leafCount; i++)
	{ // don't move the solid leaf (always has 0 size)
		if (!shouldBeMoved.leaves[i] && !forceMove)
		{
			continue;
		}

		BSPLEAF32& leaf = leaves[i];

		if (abs((float)leaf.nMins[0] + offset.x) > FLT_MAX_COORD ||
			abs((float)leaf.nMaxs[0] + offset.x) > FLT_MAX_COORD ||
			abs((float)leaf.nMins[1] + offset.y) > FLT_MAX_COORD ||
			abs((float)leaf.nMaxs[1] + offset.y) > FLT_MAX_COORD ||
			abs((float)leaf.nMins[2] + offset.z) > FLT_MAX_COORD ||
			abs((float)leaf.nMaxs[2] + offset.z) > FLT_MAX_COORD)
		{
			print_log(get_localized_string(LANG_0051));
		}
		leaf.nMins[0] += offset.x;
		leaf.nMaxs[0] += offset.x;
		leaf.nMins[1] += offset.y;
		leaf.nMaxs[1] += offset.y;
		leaf.nMins[2] += offset.z;
		leaf.nMaxs[2] += offset.z;
	}

	for (int i = 0; i < vertCount; i++)
	{
		if (!shouldBeMoved.verts[i] && !forceMove)
		{
			continue;
		}

		vec3& vert = verts[i];

		vert += offset;

		if (abs(vert.x) > FLT_MAX_COORD ||
			abs(vert.y) > FLT_MAX_COORD ||
			abs(vert.z) > FLT_MAX_COORD)
		{
			print_log(get_localized_string(LANG_0052));
		}
	}

	for (int i = 0; i < planeCount; i++)
	{
		if (!shouldBeMoved.planes[i] && !forceMove)
		{
			continue; // don't move submodels with origins
		}

		BSPPLANE& plane = planes[i];
		vec3 newPlaneOri = offset + (plane.vNormal * plane.fDist);

		if (abs(newPlaneOri.x) > FLT_MAX_COORD || abs(newPlaneOri.y) > FLT_MAX_COORD ||
			abs(newPlaneOri.z) > FLT_MAX_COORD)
		{
			print_log(get_localized_string(LANG_0053));
		}

		// get distance between new plane origin and the origin-aligned plane
		plane.fDist = dotProduct(plane.vNormal, newPlaneOri) / dotProduct(plane.vNormal, plane.vNormal);
	}

	for (int i = 0; i < texinfoCount; i++)
	{
		if (!shouldBeMoved.texInfo[i] && !forceMove)
		{
			continue; // don't move submodels with origins
		}

		move_texinfo(i, offset);
	}

	// move_texinfo can change shift value, etc. need update lighting to new
	// need update all lighting offsets!!!!
	if (logged)
	{
		resize_all_lightmaps();
		g_progress.clear();
		g_progress = ProgressMeter();
	}

	return true;
}

void Bsp::move_texinfo(int idx, vec3 offset)
{
	BSPTEXTUREINFO& info = texinfos[idx];
	if (info.iMiptex < 0 || info.iMiptex >= textureCount)
		return;
	int texOffset = ((int*)textures)[info.iMiptex + 1];
	if (texOffset < 0)
		return;

	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

	vec3 offsetDir = offset.normalize();
	float offsetLen = offset.length();

	float scaleS = info.vS.length();
	float scaleT = info.vT.length();
	vec3 nS = info.vS.normalize();
	vec3 nT = info.vT.normalize();

	vec3 newOriS = offset + (nS * info.shiftS);
	vec3 newOriT = offset + (nT * info.shiftT);

	float shiftScaleS = dotProduct(offsetDir, nS);
	float shiftScaleT = dotProduct(offsetDir, nT);

	float shiftAmountS = shiftScaleS * offsetLen * scaleS;
	float shiftAmountT = shiftScaleT * offsetLen * scaleT;

	info.shiftS -= shiftAmountS;
	info.shiftT -= shiftAmountT;

	// minimize shift values (just to be safe. floats can be p wacky and zany)
	while (abs(info.shiftS) > tex.nWidth)
	{
		info.shiftS += (info.shiftS < 0.0f) ? (float)tex.nWidth : (float)(-tex.nWidth);
	}
	while (abs(info.shiftT) > tex.nHeight)
	{
		info.shiftT += (info.shiftT < 0.0f) ? (float)tex.nHeight : (float)(-tex.nHeight);
	}
}

void Bsp::save_undo_lightmaps(bool logged)
{
	if (logged)
	{
		g_progress.update(fmt::format("Undo lightmaps ({})", faceCount).c_str(), faceCount);
	}
	if (undo_lightmaps != NULL)
	{
		delete[] undo_lightmaps;
	}
	undo_lightmaps = new LIGHTMAP[faceCount];
	undo_lightmaps_count = faceCount;

	for (int i = 0; i < faceCount; i++)
	{
		int size[2];
		GetFaceLightmapSize(this, i, size);

		undo_lightmaps[i].layers = lightmap_count(i);

		undo_lightmaps[i].width = size[0];
		undo_lightmaps[i].height = size[1];

		if (logged)
			g_progress.tick();
	}
	if (logged)
	{
		g_progress.clear();
		g_progress = ProgressMeter();
	}
}

bool Bsp::should_resize_lightmap(LIGHTMAP& oldLightmap, LIGHTMAP& newLightmap)
{
	if (oldLightmap.layers == 0)
		return false;

	if (oldLightmap.width != newLightmap.width || oldLightmap.height != newLightmap.height) {
		return true;
	}
	return false;
}


void Bsp::resize_all_lightmaps(bool logged)
{
	if (logged)
		g_progress.update("Resize lightmaps", faceCount);

	std::vector<COLOR3> newLightData;

	for (int faceId = 0; faceId < faceCount; faceId++)
	{
		BSPFACE32& face = faces[faceId];
		int newLightMapOffset = (int)newLightData.size();
		for (int lightId = 0; lightId < MAX_LIGHTMAPS; lightId++)
		{
			if (face.nStyles[lightId] == 255 || face.nLightmapOffset < 0)
				continue;
			int size[2];
			size[0] = undo_lightmaps[faceId].width;
			size[1] = undo_lightmaps[faceId].height;

			int newsize[2];
			GetFaceLightmapSize(this, faceId, newsize);

			int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
			int offset = face.nLightmapOffset + lightId * lightmapSz;

			COLOR3* data = (COLOR3*)(lightdata + offset);

			std::vector<COLOR3> newdata;

			if (newsize[0] == size[0] && size[1] == newsize[1])
			{
				if (lightdata && offset < lightDataLength && lightId < undo_lightmaps[faceId].layers)
				{
					newdata.insert(newdata.end(), data, data + size[0] * size[1]);
				}
				else
				{
					newdata.resize(size[0] * size[1]);
					std::fill(newdata.begin(), newdata.end(), COLOR3(255, 255, 255));
				}
			}
			else
			{
				if (lightdata && offset < lightDataLength && lightId < undo_lightmaps[faceId].layers)
				{
					scaleImage(data, newdata, size[0], size[1], newsize[0], newsize[1]);
				}
				else
				{
					newdata.resize(newsize[0] * newsize[1]);
					std::fill(newdata.begin(), newdata.end(), COLOR3(255, 255, 255));
				}
			}
			newLightData.insert(newLightData.end(), newdata.begin(), newdata.end());
		}

		if (face.nLightmapOffset >= 0)
		{
			face.nLightmapOffset = newLightMapOffset * sizeof(COLOR3);
		}
		if (logged)
			g_progress.tick();
	}

	if (logged)
	{
		g_progress.clear();
		g_progress = ProgressMeter();
	}

	unsigned char* tmpLump = new unsigned char[newLightData.size() * sizeof(COLOR3)];
	memcpy(tmpLump, newLightData.data(), newLightData.size() * sizeof(COLOR3));
	replace_lump(LUMP_LIGHTING, tmpLump, newLightData.size() * sizeof(COLOR3));
	save_undo_lightmaps(logged);
}


void Bsp::split_shared_model_structures(int modelIdx)
{
	// marks which structures should not be moved
	STRUCTUSAGE shouldMove(this);
	STRUCTUSAGE shouldNotMove(this);

	g_progress.update("Split model structures", modelCount);

	mark_model_structures(modelIdx, &shouldMove, modelIdx == 0);
	for (int i = 0; i < modelCount; i++)
	{
		if (i != modelIdx)
			mark_model_structures(i, &shouldNotMove, false);

		g_progress.tick();
	}

	g_progress.clear();
	g_progress = ProgressMeter();

	STRUCTREMAP remappedStuff(this);

	// TODO: handle all of these, assuming it's possible these are ever shared
	for (unsigned int i = 1; i < shouldNotMove.count.leaves; i++)
	{ // skip solid leaf - it doesn't matter
		if (shouldMove.leaves[i] && shouldNotMove.leaves[i])
		{
			print_log(get_localized_string(LANG_0055));
			break;
		}
	}
	for (unsigned int i = 0; i < shouldNotMove.count.nodes; i++)
	{
		if (shouldMove.nodes[i] && shouldNotMove.nodes[i])
		{
			print_log(get_localized_string(LANG_0056));
			break;
		}
	}
	for (unsigned int i = 0; i < shouldNotMove.count.verts; i++)
	{
		if (shouldMove.verts[i] && shouldNotMove.verts[i])
		{
			// this happens on activist series but doesn't break anything
			print_log(get_localized_string(LANG_0057));
			break;
		}
	}

	int duplicatePlanes = 0;
	int duplicateClipnodes = 0;
	int duplicateTexinfos = 0;

	for (unsigned int i = 0; i < shouldNotMove.count.planes; i++)
	{
		duplicatePlanes += shouldMove.planes[i] && shouldNotMove.planes[i];
	}
	for (unsigned int i = 0; i < shouldNotMove.count.clipnodes; i++)
	{
		duplicateClipnodes += shouldMove.clipnodes[i] && shouldNotMove.clipnodes[i];
	}
	for (unsigned int i = 0; i < shouldNotMove.count.texInfos; i++)
	{
		duplicateTexinfos += shouldMove.texInfo[i] && shouldNotMove.texInfo[i];
	}

	int newPlaneCount = planeCount + duplicatePlanes;
	int newClipnodeCount = clipnodeCount + duplicateClipnodes;
	int newTexinfoCount = texinfoCount + duplicateTexinfos;

	BSPPLANE* newPlanes = new BSPPLANE[newPlaneCount];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

	BSPCLIPNODE32* newClipnodes = new BSPCLIPNODE32[newClipnodeCount];
	memcpy(newClipnodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE32));

	BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[newTexinfoCount];
	memcpy(newTexinfos, texinfos, newTexinfoCount * sizeof(BSPTEXTUREINFO));

	int addIdx = planeCount;
	for (unsigned int i = 0; i < shouldNotMove.count.planes; i++)
	{
		if (shouldMove.planes[i] && shouldNotMove.planes[i])
		{
			newPlanes[addIdx] = planes[i];
			remappedStuff.planes[i] = addIdx;
			addIdx++;
		}
	}

	addIdx = clipnodeCount;
	for (unsigned int i = 0; i < shouldNotMove.count.clipnodes; i++)
	{
		if (shouldMove.clipnodes[i] && shouldNotMove.clipnodes[i])
		{
			newClipnodes[addIdx] = clipnodes[i];
			remappedStuff.clipnodes[i] = addIdx;
			addIdx++;
		}
	}

	addIdx = texinfoCount;
	for (unsigned int i = 0; i < shouldNotMove.count.texInfos; i++)
	{
		if (shouldMove.texInfo[i] && shouldNotMove.texInfo[i])
		{
			newTexinfos[addIdx] = texinfos[i];
			remappedStuff.texInfo[i] = addIdx;
			addIdx++;
		}
	}

	replace_lump(LUMP_PLANES, newPlanes, newPlaneCount * sizeof(BSPPLANE));
	replace_lump(LUMP_CLIPNODES, newClipnodes, newClipnodeCount * sizeof(BSPCLIPNODE32));
	replace_lump(LUMP_TEXINFO, newTexinfos, newTexinfoCount * sizeof(BSPTEXTUREINFO));

	bool* newVisitedClipnodes = new bool[newClipnodeCount];
	memset(newVisitedClipnodes, 0, newClipnodeCount);
	delete[] remappedStuff.visitedClipnodes;
	remappedStuff.visitedClipnodes = newVisitedClipnodes;

	remap_model_structures(modelIdx, &remappedStuff);

	if (duplicatePlanes || duplicateClipnodes || duplicateTexinfos)
	{
		print_log(get_localized_string(LANG_0058));
		if (duplicatePlanes)
			print_log(get_localized_string(LANG_0059), duplicatePlanes);
		if (duplicateClipnodes)
			print_log(get_localized_string(LANG_0060), duplicateClipnodes);
		if (duplicateTexinfos)
			print_log(get_localized_string(LANG_0061), duplicateTexinfos);
	}
}

bool Bsp::does_model_use_shared_structures(int modelIdx)
{
	STRUCTUSAGE shouldMove(this);
	STRUCTUSAGE shouldNotMove(this);

	for (int i = 0; i < modelCount; i++)
	{
		if (i == modelIdx)
			mark_model_structures(i, &shouldMove, true);
		else
			mark_model_structures(i, &shouldNotMove, false);
	}

	for (int i = 0; i < planeCount; i++)
	{
		if (shouldMove.planes[i] && shouldNotMove.planes[i])
		{
			return true;
		}
	}
	for (int i = 0; i < clipnodeCount; i++)
	{
		if (shouldMove.clipnodes[i] && shouldNotMove.clipnodes[i])
		{
			return true;
		}
	}
	return false;
}

LumpState Bsp::duplicate_lumps(int targets)
{
	LumpState state{};

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if ((targets & (1 << i)) == 0)
		{
			continue;
		}
		state.lumps[i] = std::vector<unsigned char>(lumps[i], lumps[i] + bsp_header.lump[i].nLength);
	}

	return state;
}

int Bsp::delete_embedded_textures()
{
	unsigned int headerSz = (textureCount + 1) * sizeof(int);
	unsigned int newTexDataSize = headerSz + (textureCount * sizeof(BSPMIPTEX));

	unsigned char* newTextureData = new unsigned char[newTexDataSize];
	memset(newTextureData, 0, newTexDataSize);

	int* header = (int*)newTextureData;
	int offset = headerSz;

	int numRemoved = 0;
	for (int i = 0; i < textureCount; i++)
	{
		int oldoffset = ((int*)textures)[i + 1];
		if (oldoffset < 0)
		{
			numRemoved++;
			continue;
		}

		header[0]++;

		BSPMIPTEX* oldMip = (BSPMIPTEX*)(textures + oldoffset);

		if (oldMip->nOffsets[0] + oldMip->nOffsets[1] +
			oldMip->nOffsets[2] + oldMip->nOffsets[3] > 0)
		{
			numRemoved++;
		}

		BSPMIPTEX* newMip = (BSPMIPTEX*)(newTextureData + offset);

		memcpy(newMip, oldMip, sizeof(BSPMIPTEX));

		newMip->nOffsets[0] = newMip->nOffsets[1] =
			newMip->nOffsets[2] = newMip->nOffsets[3] = 0;

		header[1 + i] = offset;
		offset += sizeof(BSPMIPTEX);
	}

	replace_lump(LUMP_TEXTURES, newTextureData, newTexDataSize);

	return numRemoved;
}

void Bsp::replace_lumps(const LumpState& state)
{
	for (unsigned int i = 0; i < HEADER_LUMPS; i++)
	{
		if (state.lumps[i].size())
		{
			unsigned char* tmplump = new unsigned char[state.lumps[i].size()];
			std::copy(state.lumps[i].begin(), state.lumps[i].end(), tmplump);
			replace_lump(i, tmplump, state.lumps[i].size());
		}
	}

	load_ents();
	update_lump_pointers();
}

unsigned int Bsp::remove_unused_structs(int lumpIdx, bool* usedStructs, int* remappedIndexes)
{
	int structSize = 0;

	switch (lumpIdx)
	{
	case LUMP_PLANES: structSize = sizeof(BSPPLANE); break;
	case LUMP_VERTICES: structSize = sizeof(vec3); break;
	case LUMP_NODES: structSize = sizeof(BSPNODE32); break;
	case LUMP_TEXINFO: structSize = sizeof(BSPTEXTUREINFO); break;
	case LUMP_FACES: structSize = sizeof(BSPFACE32); break;
	case LUMP_CLIPNODES: structSize = sizeof(BSPCLIPNODE32); break;
	case LUMP_LEAVES: structSize = sizeof(BSPLEAF32); break;
	case LUMP_MARKSURFACES: structSize = sizeof(int); break;
	case LUMP_EDGES: structSize = sizeof(BSPEDGE32); break;
	case LUMP_SURFEDGES: structSize = sizeof(int); break;
	default:
		print_log(get_localized_string(LANG_0062), lumpIdx);
		return 0;
	}

	int oldStructCount = bsp_header.lump[lumpIdx].nLength / structSize;

	int removeCount = 0;
	for (int i = 0; i < oldStructCount; i++)
	{
		removeCount += !usedStructs[i];
	}

	if (lumpIdx == LUMP_FACES && renderer)
	{
		for (int i = 0; i < oldStructCount; i++)
		{
			if (!usedStructs[i])
			{
				renderer->numRenderLightmapInfos--;
				for (int n = i; n < renderer->numRenderLightmapInfos; n++)
				{
					renderer->lightmaps[n] = renderer->lightmaps[n + 1];
				}
			}
		}
	}

	int newStructCount = oldStructCount - removeCount;

	unsigned char* oldStructs = lumps[lumpIdx];
	unsigned char* newStructs = new unsigned char[newStructCount * structSize];

	for (int i = 0, k = 0; i < oldStructCount; i++)
	{
		if (!usedStructs[i])
		{
			remappedIndexes[i] = 0; // prevent out-of-bounds remaps later
			continue;
		}
		memcpy(newStructs + k * structSize, oldStructs + i * structSize, structSize);
		remappedIndexes[i] = k++;
	}

	replace_lump(lumpIdx, newStructs, newStructCount * structSize);

	return removeCount;
}

unsigned int Bsp::remove_unused_textures(bool* usedTextures, int* remappedIndexes, int* removeddata)
{
	int oldTexCount = textureCount;

	int removeCount = 0;
	int removeSize = 0;
	for (int i = 0; i < oldTexCount; i++)
	{
		if (!usedTextures[i])
		{
			for (int t = 0; t < texinfoCount; t++)
			{
				BSPTEXTUREINFO& texinfo = texinfos[t];
				if (texinfo.iMiptex == i)
				{
					usedTextures[i] = true;
				}
			}
			if (usedTextures[i])
				continue;

			int offset = ((int*)textures)[i + 1];
			if (offset < 0)
			{
				removeSize += sizeof(int);
				removeCount++;
			}
			else
			{
				BSPMIPTEX* tex = (BSPMIPTEX*)(textures + offset);
				// don't delete single frames from animated textures or else game crashes
				if ((tex->szName[0] == '-' || tex->szName[0] == '+') && strlen(tex->szName) > 2)
				{
					// TODO: delete all frames if none are used. Success ?!

					char* newname = &tex->szName[2]; // +0BTN1 +1BTN1 +ABTN1 +BBTN1
					for (int n = 0; n < oldTexCount; n++)
					{
						if (usedTextures[n] && n != i)
						{
							int offset2 = ((int*)textures)[n + 1];
							if (offset2 >= 0)
							{
								BSPMIPTEX* tex2 = (BSPMIPTEX*)(textures + offset2);
								if (strlen(tex2->szName) > 2 && strcasecmp(newname, &tex2->szName[2]) == 0)
								{
									usedTextures[i] = true;
									break;
								}
							}
						}
					}

					if (usedTextures[i])
					{
						continue;
					}
				}
				removeSize += getBspTextureSize(i) + sizeof(int);
				removeCount++;
			}

		}
	}

	int newTexCount = oldTexCount - removeCount;

	int totalSize = bsp_header.lump[LUMP_TEXTURES].nLength - removeSize;

	totalSize = (totalSize + 3) & ~3; // 4 bytes align lump

	unsigned char* newTexData = new unsigned char[totalSize];
	memset(newTexData, 0, totalSize);

	int* texHeader = (int*)newTexData;


	int newOffset = (newTexCount + 1) * sizeof(int);
	int k = 0;
	for (int i = 0; i < oldTexCount; i++)
	{
		if (!usedTextures[i])
		{
			continue;
		}
		int oldOffset = ((int*)textures)[i + 1];
		if (oldOffset < 0)
		{
			texHeader[k + 1] = oldOffset;
			newOffset += sizeof(int);
		}
		else
		{
			//BSPMIPTEX* tex = (BSPMIPTEX*)(textures + oldOffset);
			int sz = getBspTextureSize(i);

			memcpy(newTexData + newOffset, textures + oldOffset, sz);

			texHeader[k + 1] = newOffset;

			newOffset += sz;
		}
		remappedIndexes[i] = k;
		k++;
	}

	texHeader[0] = k;

	if (removeddata)
		*removeddata = removeSize;

	replace_lump(LUMP_TEXTURES, newTexData, totalSize);

	return removeCount;
}

unsigned int Bsp::remove_unused_lightmaps(bool* usedFaces)
{
	int oldLightdataSize = lightDataLength;

	int* lightmapSizes = new int[faceCount] {};

	int newLightDataSize = 0;

	for (int i = 0; i < faceCount; i++)
	{
		if (usedFaces[i] && faces[i].nLightmapOffset >= 0)
		{
			lightmapSizes[i] = GetFaceLightmapSizeBytes(this, i);
			newLightDataSize += lightmapSizes[i];
		}
		else
		{
			lightmapSizes[i] = 0;
		}
	}

	unsigned char* newColorData = new unsigned char[newLightDataSize];

	int offset = 0;
	for (int i = 0; i < faceCount; i++)
	{
		BSPFACE32& face = faces[i];

		if (usedFaces[i] && face.nLightmapOffset >= 0)
		{
			memcpy(newColorData + offset, lightdata + face.nLightmapOffset, lightmapSizes[i]);
			face.nLightmapOffset = offset;
			offset += lightmapSizes[i];
		}
	}

	delete[] lightmapSizes;

	replace_lump(LUMP_LIGHTING, newColorData, newLightDataSize);

	return (unsigned int)(oldLightdataSize - newLightDataSize);
}

unsigned int Bsp::remove_unused_visdata(bool* usedLeaves, BSPLEAF32* oldLeaves, int oldLeafCount, int oldLeavesMemSize)
{
	int oldVisLength = visDataLength;

	// exclude solid leaf
	int oldVisLeafCount = oldLeafCount;
	int newVisLeafCount = (bsp_header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF32));

	int oldWorldLeaves = ((BSPMODEL*)lumps[LUMP_MODELS])->nVisLeafs; // TODO: allow deleting world leaves
	int newWorldLeaves = ((BSPMODEL*)lumps[LUMP_MODELS])->nVisLeafs;

	unsigned int oldVisRowSize = ((oldVisLeafCount + 63) & ~63) >> 3;
	unsigned int newVisRowSize = ((newVisLeafCount + 63) & ~63) >> 3;

	int decompressedVisSize = oldLeafCount * oldVisRowSize;
	unsigned char* decompressedVis = new unsigned char[decompressedVisSize];
	memset(decompressedVis, 0xFF, decompressedVisSize);
	decompress_vis_lump(this, oldLeaves, lumps[LUMP_VISIBILITY], decompressedVis,
		oldWorldLeaves, oldVisLeafCount - 1, oldVisLeafCount - 1, oldLeavesMemSize, bsp_header.lump[LUMP_VISIBILITY].nLength);

	if (oldVisRowSize != newVisRowSize)
	{
		int newDecompressedVisSize = oldLeafCount * newVisRowSize;
		int minRowSize = std::min(oldVisRowSize, newVisRowSize);
		unsigned char* newDecompressedVis = new unsigned char[newDecompressedVisSize];
		memset(newDecompressedVis, 0xFF, newDecompressedVisSize);

		for (int i = 0; i < oldWorldLeaves; i++)
		{
			if ((int)(i * newVisRowSize + minRowSize) >= newDecompressedVisSize)
			{
				print_log(get_localized_string(LANG_0063));
				break;
			}
			memcpy(newDecompressedVis + i * newVisRowSize, decompressedVis + i * oldVisRowSize, minRowSize);
		}

		delete[] decompressedVis;
		decompressedVis = newDecompressedVis;
	}

	unsigned char* compressedVis = new unsigned char[decompressedVisSize];
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(leaves, decompressedVis, compressedVis, newVisLeafCount - 1, newWorldLeaves, decompressedVisSize, leafCount);

	unsigned char* compressedVisResized = new unsigned char[newVisLen];
	memcpy(compressedVisResized, compressedVis, newVisLen);

	replace_lump(LUMP_VISIBILITY, compressedVisResized, newVisLen);

	delete[] decompressedVis;
	delete[] compressedVis;

	return oldVisLength - newVisLen;
	/*int oldVisLength = visDataLength;

	// exclude solid leaf
	int oldVisLeafCount = oldLeavesMemSize / sizeof(BSPLEAF32);
	int newVisLeafCount = bsp_header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF32);

	int newWorldLeaves = ((BSPMODEL*)lumps[LUMP_MODELS])->nVisLeafs;

	print_log(get_localized_string(LANG_0064),oldVisLeafCount,newVisLeafCount);

	int tmpLumpVisMemSize = bsp_header.lump[LUMP_VISIBILITY].nLength;

	int oldVisRowSize = ((oldVisLeafCount + 63) & ~63) >> 3;
	int newVisRowSize = ((newVisLeafCount + 63) & ~63) >> 3;
	int oldVisRowSize2 = ((oldVisLeafCount + 63 - 1) & ~63) >> 3;
	int newVisRowSize2 = ((newVisLeafCount + 63 - 1) & ~63) >> 3;

	print_log(get_localized_string(LANG_0065),oldVisRowSize,newVisRowSize,oldVisRowSize2,newVisRowSize2,oldVisLeafCount,newVisLeafCount);

	int oldDecompressedLen = oldVisRowSize * newVisLeafCount;

	unsigned char* decompressedVis = new unsigned char[oldDecompressedLen];

	memset(decompressedVis, 0xFF, oldDecompressedLen); // fill with visible VIS, if input data is corrupted.

	decompress_vis_lump(oldLeaves, lumps[LUMP_VISIBILITY], decompressedVis,
		oldWorldLeaves, oldVisLeafCount - 1, newVisLeafCount, oldLeavesMemSize, tmpLumpVisMemSize);

	if (oldVisRowSize != newVisRowSize)
	{
		int newDecompressedVisSize = oldVisLeafCount * newVisRowSize;
		int minRowSize = std::min(oldVisRowSize, newVisRowSize);
		unsigned char* newDecompressedVis = new unsigned char[newDecompressedVisSize];
		memset(newDecompressedVis, 0xFF, newDecompressedVisSize);

		for (int i = 0; i < oldVisLeafCount; i++)
		{
			if (i * newVisRowSize + minRowSize >= newDecompressedVisSize)
			{
				print_log(get_localized_string(LANG_1019));
				break;
			}
			memcpy(newDecompressedVis + i * newVisRowSize, decompressedVis + i * oldVisRowSize, minRowSize);
		}

		delete[] decompressedVis;
		decompressedVis = newDecompressedVis;
	}

	unsigned char* compressedVis = new unsigned char[oldDecompressedLen];
	memset(compressedVis, 0, oldDecompressedLen);
	int newVisLen = CompressAll(leaves, decompressedVis, compressedVis, newWorldLeaves, newVisLeafCount - 1, oldDecompressedLen, leafCount);

	unsigned char* compressedVisResized = new unsigned char[newVisLen];
	memcpy(compressedVisResized, compressedVis, newVisLen);

	replace_lump(LUMP_VISIBILITY, compressedVisResized, newVisLen);

	delete[] decompressedVis;
	delete[] compressedVis;

	return (unsigned int)(oldVisLength - newVisLen);*/
}
bool operator < (const BSPTEXTUREINFO& struct1, const BSPTEXTUREINFO& struct2)
{
	return memcmp(&struct1, &struct2, sizeof(BSPTEXTUREINFO)) < 0;
}

bool operator > (const BSPTEXTUREINFO& struct1, const BSPTEXTUREINFO& struct2)
{
	return memcmp(&struct1, &struct2, sizeof(BSPTEXTUREINFO)) > 0;
}

bool operator == (const BSPTEXTUREINFO& struct1, const BSPTEXTUREINFO& struct2)
{
	return memcmp(&struct1, &struct2, sizeof(BSPTEXTUREINFO)) == 0;
}

void Bsp::clean_unused_texinfos()
{
	//int unusedtexinfos = 0;
	for (int i = 0; i < faceCount; i++)
	{
		if (faces[i].iTextureInfo >= 0)
		{
			int texInfoIdx = faces[i].iTextureInfo;
			BSPTEXTUREINFO& texInfo = texinfos[texInfoIdx];
			for (int n = 0; n < texinfoCount; n++)
			{
				if (n != texInfoIdx && texInfo == texinfos[n])
				{
					for (int z = 0; z < faceCount; z++)
					{
						if (z == i)
							continue;
						if (faces[z].iTextureInfo == n)
						{
							faces[z].iTextureInfo = texInfoIdx;
						}
					}
				}
			}
		}
	}
}

int Bsp::merge_all_verts(float epsilon)
{
	int merged_verts = 0;

	for (int v = vertCount - 1; v >= vertCount; v--)
	{
		bool found1 = false;
		bool found2 = false;
		for (int i = 0; i < edgeCount; i++)
		{
			if (!found1 && VectorCompare(verts[edges[i].iVertex[0]], verts[v], epsilon))
			{
				edges[i].iVertex[0] = v;
				merged_verts++;
				found1 = true;
			}
			if (!found2 && VectorCompare(verts[edges[i].iVertex[1]], verts[v], epsilon))
			{
				edges[i].iVertex[1] = v;
				merged_verts++;
				found2 = true;
			}

			if (found1 && found2)
			{
				break;
			}
		}
	}

	return abs(merged_verts - vertCount);
}

STRUCTCOUNT Bsp::remove_unused_model_structures(unsigned int target)
{
	if (!modelCount)
		return STRUCTCOUNT();

	update_lump_pointers();

	if (g_settings.mark_unused_texinfos && target & CLEAN_TEXINFOS)
		clean_unused_texinfos();

	int merged_verts = 0;
	if (g_settings.merge_verts && target & CLEAN_VERTICES)
	{
		print_log(get_localized_string(LANG_0066));
		merged_verts = merge_all_verts() + merge_all_verts();
	}

	// marks which structures should not be moved
	STRUCTUSAGE usedStructures(this);

	bool* usedModels = new bool[modelCount + 1];
	memset(usedModels, 0, sizeof(bool) * modelCount);
	usedModels[0] = true; // never delete worldspawn
	for (int i = 0; i < (int)ents.size(); i++)
	{
		int modelIdx = ents[i]->getBspModelIdx();
		if (modelIdx >= 0 && modelIdx < modelCount)
		{
			usedModels[modelIdx] = true;
		}
	}

	// reversed so models can be deleted without shifting the next delete index
	if (modelCount > 0)
	{
		for (int i = modelCount - 1; i >= 0; i--)
		{
			if (!usedModels[i])
			{
				delete_model(i);
			}
			else
			{
				mark_model_structures(i, &usedStructures, false, target & CLEAN_CLIPNODES_SOMETHING);
			}
		}
	}

	STRUCTREMAP remap(this);
	STRUCTCOUNT removeCount = STRUCTCOUNT();

	if (this->models[0].nFaces > 0)
		usedStructures.edges[0] = true; // first edge is never used but maps break without it?

	update_lump_pointers();
	int oldLeavesLumpLen = bsp_header.lump[LUMP_LEAVES].nLength;
	unsigned char* oldLeaves = new unsigned char[oldLeavesLumpLen];
	memcpy(oldLeaves, lumps[LUMP_LEAVES], oldLeavesLumpLen);

	if (target & CLEAN_LIGHTMAP && lightDataLength > 0)
		removeCount.lightdata = remove_unused_lightmaps(usedStructures.faces);
	if (target & CLEAN_PLANES)
		removeCount.planes = remove_unused_structs(LUMP_PLANES, usedStructures.planes, remap.planes);
	if (target & CLEAN_NODES)
		removeCount.nodes = remove_unused_structs(LUMP_NODES, usedStructures.nodes, remap.nodes);
	if (target & CLEAN_CLIPNODES)
		removeCount.clipnodes = remove_unused_structs(LUMP_CLIPNODES, usedStructures.clipnodes, remap.clipnodes);
	if (target & CLEAN_LEAVES)
		removeCount.leaves = remove_unused_structs(LUMP_LEAVES, usedStructures.leaves, remap.leaves);
	if (target & CLEAN_MARKSURFACES)
		removeCount.markSurfs = remove_unused_structs(LUMP_MARKSURFACES, usedStructures.markSurfs, remap.markSurfs);
	if (target & CLEAN_FACES)
		removeCount.faces = remove_unused_structs(LUMP_FACES, usedStructures.faces, remap.faces);
	if (target & CLEAN_SURFEDGES)
		removeCount.surfEdges = remove_unused_structs(LUMP_SURFEDGES, usedStructures.surfEdges, remap.surfEdges);
	if (target & CLEAN_TEXINFOS)
		removeCount.texInfos = remove_unused_structs(LUMP_TEXINFO, usedStructures.texInfo, remap.texInfo);
	if (target & CLEAN_EDGES)
		removeCount.edges = remove_unused_structs(LUMP_EDGES, usedStructures.edges, remap.edges);
	if (target & CLEAN_VERTICES)
		removeCount.verts = remove_unused_structs(LUMP_VERTICES, usedStructures.verts, remap.verts) + merged_verts;

	if (target & CLEAN_TEXTURES)
	{
		int removeTexData = 0;

		removeCount.textures = remove_unused_textures(usedStructures.textures, remap.textures, &removeTexData);

		removeCount.texturedata = removeTexData;
	}

	if (target & CLEAN_VISDATA && visDataLength && usedStructures.count.leaves)
		removeCount.visdata = remove_unused_visdata(usedStructures.leaves, (BSPLEAF32*)oldLeaves, usedStructures.count.leaves, oldLeavesLumpLen);

	STRUCTCOUNT newCounts(this);

	for (unsigned int i = 0; i < newCounts.markSurfs; i++)
	{
		marksurfs[i] = remap.faces[marksurfs[i]];

		if (!(target & CLEAN_LEAVES))
		{
			for (unsigned int n = 1; n < newCounts.leaves; n++)
			{
				if (leaves[n].nMarkSurfaces > 0 && leaves[n].iFirstMarkSurface >= 0)
				{
					leaves[n].iFirstMarkSurface = remap.markSurfs[leaves[n].iFirstMarkSurface];
				}
			}
		}
	}

	for (unsigned int i = 0; i < newCounts.surfEdges; i++)
	{
		surfedges[i] = surfedges[i] >= 0 ? remap.edges[surfedges[i]] : -remap.edges[-surfedges[i]];
	}
	for (unsigned int i = 0; i < newCounts.edges; i++)
	{
		for (int k = 0; k < 2; k++)
		{
			edges[i].iVertex[k] = remap.verts[edges[i].iVertex[k]];
		}
	}
	for (unsigned int i = 0; i < newCounts.texInfos; i++)
	{
		texinfos[i].iMiptex = remap.textures[texinfos[i].iMiptex];
	}
	for (unsigned int i = 0; i < newCounts.clipnodes; i++)
	{
		clipnodes[i].iPlane = remap.planes[clipnodes[i].iPlane];
		for (int k = 0; k < 2; k++)
		{
			if (clipnodes[i].iChildren[k] >= 0)
			{
				clipnodes[i].iChildren[k] = remap.clipnodes[clipnodes[i].iChildren[k]];
			}
		}
	}
	for (unsigned int i = 0; i < newCounts.nodes; i++)
	{
		nodes[i].iPlane = remap.planes[nodes[i].iPlane];
		if (nodes[i].nFaces > 0)
			nodes[i].iFirstFace = remap.faces[nodes[i].iFirstFace];
		for (int k = 0; k < 2; k++)
		{
			if (nodes[i].iChildren[k] >= 0)
			{
				nodes[i].iChildren[k] = remap.nodes[nodes[i].iChildren[k]];
			}
			else
			{
				int leafIdx = ~nodes[i].iChildren[k];
				nodes[i].iChildren[k] = ~(remap.leaves[leafIdx]);
			}
		}
	}
	for (unsigned int i = 1; i < newCounts.leaves; i++)
	{
		if (leaves[i].nMarkSurfaces > 0 && leaves[i].iFirstMarkSurface >= 0)
			leaves[i].iFirstMarkSurface = remap.markSurfs[leaves[i].iFirstMarkSurface];
	}
	for (unsigned int i = 0; i < newCounts.faces; i++)
	{
		faces[i].iPlane = remap.planes[faces[i].iPlane];
		if (faces[i].nEdges > 0)
			faces[i].iFirstEdge = remap.surfEdges[faces[i].iFirstEdge];
		faces[i].iTextureInfo = remap.texInfo[faces[i].iTextureInfo];
	}

	for (int i = 0; i < modelCount; i++)
	{
		if (models[i].nFaces > 0)
			models[i].iFirstFace = remap.faces[models[i].iFirstFace];
		if (models[i].iHeadnodes[0] >= 0)
			models[i].iHeadnodes[0] = remap.nodes[models[i].iHeadnodes[0]];
		for (int k = 1; k < MAX_MAP_HULLS; k++)
		{
			if (models[i].iHeadnodes[k] >= 0)
				models[i].iHeadnodes[k] = remap.clipnodes[models[i].iHeadnodes[k]];
		}
	}

	delete[] usedModels;

	if (target & CLEAN_TEXTURES)
		remove_unused_wad_files(this, this);

	return removeCount;
}

void remove_unused_wad_files(Bsp* baseMap, Bsp* targetMap, int tex_type)
{
	if (!baseMap || !targetMap || !baseMap->getBspRender() || baseMap->getBspRender()->wads.empty())
		return;
	// Save ent state
	targetMap->update_ent_lump();
	// Update texture count
	targetMap->update_lump_pointers();

	std::string wadNames{};
	std::set<std::string> texNames{};
	int wads = 0;
	for (auto& wad : baseMap->getBspRender()->wads)
	{
		bool used = false;
		for (int i = 0; i < targetMap->textureCount; i++)
		{
			int offset = ((int*)targetMap->textures)[i + 1];
			if (offset >= 0)
			{
				BSPMIPTEX* tex = (BSPMIPTEX*)(targetMap->textures + offset);

				if (tex_type == 0)
				{
					if (wad->hasTexture(tex->szName))
					{
						used = true;
						break;
					}
				}
				else
				{
					if (tex->nOffsets[0] <= 0)
					{
						if (wad->hasTexture(tex->szName) && texNames.count(tex->szName) == 0)
						{
							WADTEX* wadTex = wad->readTexture(tex->szName);

							texNames.insert(tex->szName);

							if (tex_type == 1)
							{
								int lastMipSize = (wadTex->nWidth / 8) * (wadTex->nHeight / 8);
								COLOR3* palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + sizeof(short) - sizeof(BSPMIPTEX));
								unsigned char* src = wadTex->data;
								COLOR3* imageData = new COLOR3[wadTex->nWidth * wadTex->nHeight];

								int sz = wadTex->nWidth * wadTex->nHeight;
								for (int n = 0; n < sz; n++)
								{
									imageData[n] = palette[src[n]];
								}

								targetMap->add_texture(tex->szName, (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight, NULL, true);

								delete[] imageData;
							}
							else
							{
								targetMap->add_texture(wadTex);
							}

							delete wadTex;
						}
					}
				}
			}
		}

		if (used && wadNames.find(basename(wad->filename)) == std::string::npos)
		{
			wads++;
			wadNames += basename(wad->filename) + ";";
		}
	}

	if (tex_type == 0)
	{
		bool updatewads = false;
		for (size_t i = 0; i < targetMap->ents.size(); i++)
		{
			if (targetMap->ents[i]->isWorldSpawn())
			{
				updatewads = true;
				targetMap->ents[i]->setOrAddKeyvalue("wad", wadNames);
				break;
			}
		}

		if (!updatewads && !targetMap->ents.empty())
		{
			targetMap->ents[0]->setOrAddKeyvalue("wad", wadNames);
		}
	}
	else
	{
		for (size_t i = 0; i < targetMap->ents.size(); i++)
		{
			targetMap->ents[i]->removeKeyvalue("wad");
		}
	}

	targetMap->update_ent_lump();
	targetMap->update_lump_pointers();
	print_log(get_localized_string(LANG_0067), wads);
}

bool Bsp::has_hull2_ents()
{
	// monsters that use hull 2 by default
	static std::set<std::string> largeMonsters{
		"monster_alien_grunt",
		"monster_alien_tor",
		"monster_alien_voltigore",
		"monster_babygarg",
		"monster_bigmomma",
		"monster_bullchicken",
		"monster_gargantua",
		"monster_ichthyosaur",
		"monster_kingpin",
		"monster_apache",
		"monster_blkop_apache"
		// osprey, nihilanth, and tentacle are huge but are basically nonsolid (no brush collision or triggers)
	};

	for (size_t i = 0; i < ents.size(); i++)
	{
		std::string cname = ents[i]->keyvalues["classname"];
		//std::string tname = ents[i]->keyvalues["targetname"];

		if (cname.find("monster_") == std::string::npos)
		{
			vec3 minhull;
			vec3 maxhull;

			if (!ents[i]->keyvalues["minhullsize"].empty())
				minhull = parseVector(ents[i]->keyvalues["minhullsize"]);
			if (!ents[i]->keyvalues["maxhullsize"].empty())
				maxhull = parseVector(ents[i]->keyvalues["maxhullsize"]);

			if (minhull == vec3() && maxhull == vec3())
			{
				// monster is using its default hull size
				if (largeMonsters.find(cname) != largeMonsters.end())
				{
					return true;
				}
			}
			else if (abs(minhull.x) > MAX_HULL1_EXTENT_MONSTER || abs(maxhull.x) > MAX_HULL1_EXTENT_MONSTER
				|| abs(minhull.y) > MAX_HULL1_EXTENT_MONSTER || abs(maxhull.y) > MAX_HULL1_EXTENT_MONSTER)
			{
				return true;
			}
		}
		else if (cname == "func_pushable")
		{
			int modelIdx = ents[i]->getBspModelIdx();
			if (modelIdx >= 0 && modelIdx < modelCount)
			{
				BSPMODEL& model = models[modelIdx];
				vec3 size = model.nMaxs - model.nMins;

				if (size.x > MAX_HULL1_SIZE_PUSHABLE || size.y > MAX_HULL1_SIZE_PUSHABLE)
				{
					return true;
				}
			}
		}
	}

	return false;
}

STRUCTCOUNT Bsp::delete_unused_hulls(bool noProgress)
{
	if (!noProgress)
	{
		g_progress.update("Deleting unused hulls", modelCount - 1);
	}

	int deletedHulls = 0;

	for (int i = 1; i < modelCount; i++)
	{
		if (!noProgress)
		{
			g_progress.tick();
		}

		std::vector<Entity*> usageEnts = get_model_ents(i);

		if (usageEnts.empty())
		{
			print_log(get_localized_string(LANG_0068), i);

			for (int k = 0; k < MAX_MAP_HULLS; k++)
				deletedHulls += models[i].iHeadnodes[k] >= 0;

			delete_model(i);
			//modelCount--; automatically updated when lump is replaced
			i--;
			continue;
		}


		std::string uses;
		bool needsPlayerHulls = false; // HULL 1 + HULL 3
		bool needsMonsterHulls = false; // All HULLs
		bool needsVisibleHull = false; // HULL 0
		for (size_t k = 0; k < usageEnts.size(); k++)
		{
			std::string cname = usageEnts[k]->keyvalues["classname"];
			std::string tname = usageEnts[k]->keyvalues["targetname"];
			int spawnflags = atoi(usageEnts[k]->keyvalues["spawnflags"].c_str());

			if (k != 0)
			{
				uses += ", ";
			}
			uses += "\"" + tname + "\" (" + cname + ")";

			if (std::find(g_settings.entsThatNeverNeedAnyHulls.begin(), g_settings.entsThatNeverNeedAnyHulls.end(), cname) != g_settings.entsThatNeverNeedAnyHulls.end())
			{
				continue; // no collision or faces needed at all
			}
			else if (std::find(g_settings.entsThatNeverNeedCollision.begin(), g_settings.entsThatNeverNeedCollision.end(), cname) != g_settings.entsThatNeverNeedCollision.end())
			{
				needsVisibleHull = !is_invisible_solid(usageEnts[k]);
			}
			else if (std::find(g_settings.passableEnts.begin(), g_settings.passableEnts.end(), cname) != g_settings.passableEnts.end())
			{
				needsPlayerHulls = needsMonsterHulls = !(spawnflags & 8); // "Passable" or "Not solid" unchecked
				needsVisibleHull = !(spawnflags & 8) || !is_invisible_solid(usageEnts[k]);
			}
			else if (cname.find("trigger_") == std::string::npos)
			{
				if (std::find(g_settings.conditionalPointEntTriggers.begin(), g_settings.conditionalPointEntTriggers.end(), cname) != g_settings.conditionalPointEntTriggers.end())
				{
					needsVisibleHull = spawnflags & 8; // "Everything else" flag checked
					needsPlayerHulls = !(spawnflags & 2); // "No clients" unchecked
					needsMonsterHulls = (spawnflags & 1) || (spawnflags & 4); // "monsters" or "pushables" checked
				}
				else if (cname == "trigger_push")
				{
					needsPlayerHulls = !(spawnflags & 8); // "No clients" unchecked
					needsMonsterHulls = (spawnflags & 4) || !(spawnflags & 16); // "Pushables" checked or "No monsters" unchecked
					needsVisibleHull = true; // needed for point-ent pushing
				}
				else if (cname == "trigger_hurt")
				{
					needsPlayerHulls = !(spawnflags & 8); // "No clients" unchecked
					needsMonsterHulls = !(spawnflags & 16) || !(spawnflags & 32); // "Fire/Touch client only" unchecked
				}
				else
				{
					needsPlayerHulls = true;
					needsMonsterHulls = true;
				}
			}
			else if (cname == "func_clip")
			{
				needsPlayerHulls = !(spawnflags & 8); // "No clients" not checked
				needsMonsterHulls = (spawnflags & 8) || !(spawnflags & 16); // "Pushables" checked or "No monsters" unchecked
				needsVisibleHull = (spawnflags & 32) || (spawnflags & 64); // "Everything else" or "item_inv" checked
			}
			else if (cname == "func_conveyor")
			{
				needsPlayerHulls = needsMonsterHulls = !(spawnflags & 2); // "Not Solid" unchecked
				needsVisibleHull = !(spawnflags & 2) || !is_invisible_solid(usageEnts[k]);
			}
			else if (cname == "func_friction")
			{
				needsPlayerHulls = true;
				needsMonsterHulls = true;
			}
			else if (cname == "func_rot_button")
			{
				needsPlayerHulls = needsMonsterHulls = !(spawnflags & 1); // "Not solid" unchecked
				needsVisibleHull = true;
			}
			else if (cname == "func_rotating")
			{
				needsPlayerHulls = needsMonsterHulls = !(spawnflags & 64); // "Not solid" unchecked
				needsVisibleHull = true;
			}
			else if (cname == "func_ladder")
			{
				needsPlayerHulls = true;
				needsVisibleHull = true;
			}
			else if (std::find(g_settings.playerOnlyTriggers.begin(), g_settings.playerOnlyTriggers.end(), cname) != g_settings.playerOnlyTriggers.end())
			{
				needsPlayerHulls = true;
			}
			else if (std::find(g_settings.monsterOnlyTriggers.begin(), g_settings.monsterOnlyTriggers.end(), cname) != g_settings.monsterOnlyTriggers.end())
			{
				needsMonsterHulls = true;
			}
			else
			{
				// assume all hulls are needed
				needsPlayerHulls = true;
				needsMonsterHulls = true;
				needsVisibleHull = true;
				break;
			}
		}

		BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[i];

		if (!needsVisibleHull && !needsMonsterHulls)
		{
			if (models[i].iHeadnodes[0] >= 0)
				print_log(get_localized_string(LANG_0069), i, uses);

			deletedHulls += models[i].iHeadnodes[0] >= 0;

			model.iHeadnodes[0] = -1;
			model.nVisLeafs = 0;
			model.nFaces = 0;
			model.iFirstFace = 0;
		}
		if (!needsPlayerHulls && !needsMonsterHulls)
		{
			bool deletedAnyHulls = false;
			for (int k = 1; k < MAX_MAP_HULLS; k++)
			{
				deletedHulls += models[i].iHeadnodes[k] >= 0;
				if (models[i].iHeadnodes[k] >= 0)
				{
					deletedHulls++;
					deletedAnyHulls = true;
				}
			}

			if (deletedAnyHulls)
				print_log(get_localized_string(LANG_0070), i, uses);

			model.iHeadnodes[1] = -1;
			model.iHeadnodes[2] = -1;
			model.iHeadnodes[3] = -1;
		}
		else if (!needsMonsterHulls)
		{
			if (models[i].iHeadnodes[2] >= 0)
				print_log(get_localized_string(LANG_0071), i, uses);

			deletedHulls += models[i].iHeadnodes[2] >= 0;

			model.iHeadnodes[2] = -1;
		}
		else if (!needsPlayerHulls)
		{
			// monsters use all hulls so can't do anything about this
		}
	}

	STRUCTCOUNT removed = remove_unused_model_structures();

	update_ent_lump();

	if (!noProgress)
	{
		g_progress.clear();
		g_progress = ProgressMeter();
	}

	return removed;
}

bool Bsp::is_invisible_solid(Entity* ent)
{
	if (!ent->isBspModel())
		return false;

	std::string tname = ent->keyvalues["targetname"];
	int rendermode = atoi(ent->keyvalues["rendermode"].c_str());
	int renderamt = atoi(ent->keyvalues["renderamt"].c_str());
	int renderfx = atoi(ent->keyvalues["renderfx"].c_str());

	if (rendermode == RenderMode::kRenderNormal || renderamt != 0)
	{
		return false;
	}

	switch (renderfx)
	{
	case kRenderFxPulseSlow:
	case kRenderFxPulseFast:
	case kRenderFxPulseSlowWide:
	case kRenderFxPulseFastWide:
	case kRenderFxSolidSlow:
	case kRenderFxSolidFast:
	case kRenderFxDistort:
	case kRenderFxHologram:
	case kRenderFxDeadPlayer:
		return false;
	default:
		break;
	}

	static std::set<std::string> renderKeys{
		"rendermode",
		"renderamt",
		"renderfx"
	};

	for (size_t i = 0; i < ents.size(); i++)
	{
		std::string cname = ents[i]->keyvalues["classname"];

		if (cname == "env_render")
		{
			return false; // assume it will affect the brush since it can be moved anywhere
		}
		else if (cname == "env_render_individual")
		{
			if (ents[i]->keyvalues["target"] == tname)
			{
				return false; // assume it's making the ent visible
			}
		}
		else if (cname == "trigger_changevalue")
		{
			if (ents[i]->keyvalues["target"] == tname)
			{
				if (renderKeys.find(ents[i]->keyvalues["m_iszValueName"]) != renderKeys.end())
				{
					return false; // assume it's making the ent visible
				}
			}
		}
		else if (cname == "trigger_copyvalue")
		{
			if (ents[i]->keyvalues["target"] == tname)
			{
				if (renderKeys.find(ents[i]->keyvalues["m_iszDstValueName"]) != renderKeys.end())
				{
					return false; // assume it's making the ent visible
				}
			}
		}
		else if (cname == "trigger_createentity")
		{
			if (ents[i]->keyvalues["+model"] == tname || ents[i]->keyvalues["-model"] == ent->keyvalues["model"])
			{
				return false; // assume this new ent will be visible at some point
			}
		}
		else if (cname == "trigger_changemodel")
		{
			if (ents[i]->keyvalues["model"] == ent->keyvalues["model"])
			{
				return false; // assume the target is visible
			}
		}
	}

	return true;
}

void Bsp::update_ent_lump(bool stripNodes)
{
	std::stringstream ent_data = std::stringstream();

	for (size_t i = 0; i < ents.size(); i++)
	{
		if (stripNodes)
		{
			if (ents[i]->hasKey("classname"))
			{
				std::string cname = ents[i]->keyvalues["classname"];
				if (cname == "info_node" || cname == "info_node_air")
				{
					continue;
				}
			}
		}

		ent_data << "{\n";

		for (size_t k = 0; k < ents[i]->keyOrder.size(); k++)
		{
			std::string key = ents[i]->keyOrder[k];
			if (ents[i]->hasKey(key))
				ent_data << "\"" << key << "\" \"" << ents[i]->keyvalues[key] << "\"\n";
		}

		ent_data << "}";

		if (i < ents.size() - 1)
		{
			ent_data << "\n"; // trailing newline crashes sven, and only sven, and only sometimes
		}
	}

	std::string str_data = ent_data.str();

	unsigned char* newEntData = new unsigned char[str_data.size() + 1];

	if (str_data.size())
		memcpy(newEntData, str_data.c_str(), str_data.size());

	newEntData[str_data.size()] = 0;

	replace_lump(LUMP_ENTITIES, newEntData, str_data.size() + 1);
}

vec3 Bsp::get_model_center(int modelIdx)
{
	if (modelIdx < 0 || modelIdx > bsp_header.lump[LUMP_MODELS].nLength / (int)sizeof(BSPMODEL))
	{
		print_log(get_localized_string(LANG_0072), modelIdx);
		return vec3();
	}

	BSPMODEL& model = models[modelIdx];

	return model.nMins + (model.nMaxs - model.nMins) * 0.5f;
}

int Bsp::lightmap_count(int faceIdx)
{
	BSPFACE32& face = faces[faceIdx];

	if (texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL || face.nLightmapOffset < 0)
		return 0;

	int lightmapCount = 0;
	for (int k = 0; k < MAX_LIGHTMAPS; k++)
	{
		lightmapCount += face.nStyles[k] != 255 ? 1 : 0;
	}

	return lightmapCount;
}

void Bsp::write(const std::string& path)
{
	// Make single backup
	if (g_settings.backUpMap && fileExists(path) && !fileExists(path + ".bak"))
	{
		int len;
		char* oldfile = loadFile(path, len);
		std::ofstream file(path + ".bak", std::ios::trunc | std::ios::binary);
		if (!file.is_open())
		{
			print_log(get_localized_string(LANG_0073), path);
			return;
		}
		print_log(get_localized_string(LANG_0074), path + ".bak");

		file.write(oldfile, len);
		delete[] oldfile;
	}

	std::ofstream file(path, std::ios::trunc | std::ios::binary);
	if (!file.is_open())
	{
		print_log(get_localized_string(LANG_0075), path);
		return;
	}

	//if (is_bsp2_old)
	//{
	//	is_bsp2_old = false;
	//	is_bsp2 = true;
	//	bsp_header.nVersion = 30;
	//}

	update_lump_pointers();

	unsigned char* nulls = new unsigned char[sizeof(BSPHEADER) + sizeof(BSPHEADER_EX)];

	file.write((const char*)nulls, is_bsp30ext && extralumps ? sizeof(BSPHEADER) + sizeof(BSPHEADER_EX) : sizeof(BSPHEADER));

	delete[] nulls;

	unsigned char* oldLighting = (unsigned char*)lightdata;
	unsigned char* freelighting = NULL;

	// first process, for face restore offsets
	if (!is_colored_lightmap)
	{
		int lightPixels = bsp_header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);

		COLOR3* oldLight = (COLOR3*)lightdata;
		freelighting = new unsigned char[lightPixels];

		for (int m = 0; m < lightPixels; m++)
		{
			freelighting[m] = (unsigned char)((int)(oldLight[m].r + oldLight[m].g + oldLight[m].b) / 3);
		}

		bsp_header.lump[LUMP_LIGHTING].nLength = lightPixels;
		lumps[LUMP_LIGHTING] = (unsigned char*)freelighting;


		//int offset = 0;

		for (int n = 0; n < faceCount; n++)
		{
			faces[n].nLightmapOffset /= sizeof(COLOR3);
		}
	}

	unsigned char* oldClipnodes = (unsigned char*)clipnodes;
	BSPCLIPNODE16* freeClipnodes16 = NULL;

	if (!is_bsp2 && (is_broken_clipnodes || !is_32bit_clipnodes || bsp_header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE32) < MAX_MAP_CLIPNODES_DEFAULT))
	{
		freeClipnodes16 = new BSPCLIPNODE16[clipnodeCount];
		for (int n = 0; n < clipnodeCount; n++)
		{
			if (is_broken_clipnodes)
			{
				freeClipnodes16[n].iChildren[0] = (short)(
					(unsigned short)clipnodes[n].iChildren[0] > clipnodeCount ? 65536 - (unsigned short)clipnodes[n].iChildren[0] : clipnodes[n].iChildren[0]);
				freeClipnodes16[n].iChildren[1] = (short)(
					(unsigned short)clipnodes[n].iChildren[1] > clipnodeCount ? 65536 - (unsigned short)clipnodes[n].iChildren[0] : clipnodes[n].iChildren[1]);
			}
			else
			{
				freeClipnodes16[n].iChildren[0] = (short)clipnodes[n].iChildren[0];
				freeClipnodes16[n].iChildren[1] = (short)clipnodes[n].iChildren[1];
			}
			freeClipnodes16[n].iPlane = clipnodes[n].iPlane;
		}
		bsp_header.lump[LUMP_CLIPNODES].nLength = clipnodeCount * sizeof(BSPCLIPNODE16);
		lumps[LUMP_CLIPNODES] = (unsigned char*)freeClipnodes16;
	}

	unsigned char* oldnodes = (unsigned char*)nodes;
	BSPNODE16* freenodes16 = NULL;
	BSPNODE32A* freenodes32a = NULL;

	if (!is_bsp2)
	{
		freenodes16 = new BSPNODE16[nodeCount];
		for (int n = 0; n < nodeCount; n++)
		{
			freenodes16[n].iChildren[0] = (short)nodes[n].iChildren[0];
			freenodes16[n].iChildren[1] = (short)nodes[n].iChildren[1];
			freenodes16[n].iPlane = nodes[n].iPlane;

			freenodes16[n].firstFace = (unsigned short)nodes[n].iFirstFace;
			freenodes16[n].nFaces = (unsigned short)nodes[n].nFaces;
			for (int m = 0; m < 3; m++)
			{
				freenodes16[n].nMaxs[m] = (short)round(nodes[n].nMaxs[m]);
				freenodes16[n].nMins[m] = (short)round(nodes[n].nMins[m]);
			}
		}
		bsp_header.lump[LUMP_NODES].nLength = nodeCount * sizeof(BSPNODE16);
		lumps[LUMP_NODES] = (unsigned char*)freenodes16;
	}
	else if (is_bsp2_old)
	{
		freenodes32a = new BSPNODE32A[nodeCount];
		for (int n = 0; n < nodeCount; n++)
		{
			freenodes32a[n].iChildren[0] = nodes[n].iChildren[0];
			freenodes32a[n].iChildren[1] = nodes[n].iChildren[1];
			freenodes32a[n].iPlane = nodes[n].iPlane;

			freenodes32a[n].firstFace = nodes[n].iFirstFace;
			freenodes32a[n].nFaces = nodes[n].nFaces;
			for (int m = 0; m < 3; m++)
			{
				freenodes32a[n].nMaxs[m] = (short)round(nodes[n].nMaxs[m]);
				freenodes32a[n].nMins[m] = (short)round(nodes[n].nMins[m]);
			}
		}
		bsp_header.lump[LUMP_NODES].nLength = nodeCount * sizeof(BSPNODE32A);
		lumps[LUMP_NODES] = (unsigned char*)freenodes32a;
	}


	unsigned char* oldfaces = (unsigned char*)faces;
	BSPFACE16* freefaces16 = NULL;

	if (!is_bsp2)
	{
		freefaces16 = new BSPFACE16[faceCount];
		for (int n = 0; n < faceCount; n++)
		{
			freefaces16[n].iFirstEdge = faces[n].iFirstEdge;
			freefaces16[n].iPlane = (unsigned short)faces[n].iPlane;
			freefaces16[n].iTextureInfo = (short)faces[n].iTextureInfo;
			freefaces16[n].nEdges = (short)faces[n].nEdges;
			freefaces16[n].nLightmapOffset = faces[n].nLightmapOffset;
			freefaces16[n].nPlaneSide = (short)faces[n].nPlaneSide;
			for (int m = 0; m < MAX_LIGHTMAPS; m++)
			{
				freefaces16[n].nStyles[m] = faces[n].nStyles[m];
			}
		}
		bsp_header.lump[LUMP_FACES].nLength = faceCount * sizeof(BSPFACE16);
		lumps[LUMP_FACES] = (unsigned char*)freefaces16;
	}

	unsigned char* oldmarksurfs = (unsigned char*)marksurfs;
	unsigned short* freemarksurfs16 = NULL;

	if (!is_bsp2)
	{
		freemarksurfs16 = new unsigned short[marksurfCount];
		for (int n = 0; n < marksurfCount; n++)
		{
			freemarksurfs16[n] = (unsigned short)marksurfs[n];
		}
		bsp_header.lump[LUMP_MARKSURFACES].nLength = marksurfCount * sizeof(unsigned short);
		lumps[LUMP_MARKSURFACES] = (unsigned char*)freemarksurfs16;
	}

	unsigned char* oldleaves = (unsigned char*)leaves;
	BSPLEAF16* freeleaves16 = NULL;
	BSPLEAF32A* freeleaves32a = NULL;

	if (!is_bsp2)
	{
		freeleaves16 = new BSPLEAF16[leafCount];
		for (int n = 0; n < leafCount; n++)
		{
			freeleaves16[n].iFirstMarkSurface = (unsigned short)leaves[n].iFirstMarkSurface;
			freeleaves16[n].nMarkSurfaces = (unsigned short)leaves[n].nMarkSurfaces;
			for (int m = 0; m < MAX_AMBIENTS; m++)
			{
				freeleaves16[n].nAmbientLevels[m] = leaves[n].nAmbientLevels[m];
			}
			freeleaves16[n].nContents = leaves[n].nContents;
			freeleaves16[n].nVisOffset = leaves[n].nVisOffset;
			for (int m = 0; m < 3; m++)
			{
				freeleaves16[n].nMaxs[m] = (short)round(leaves[n].nMaxs[m]);
				freeleaves16[n].nMins[m] = (short)round(leaves[n].nMins[m]);
			}
		}
		bsp_header.lump[LUMP_LEAVES].nLength = leafCount * sizeof(BSPLEAF16);
		lumps[LUMP_LEAVES] = (unsigned char*)freeleaves16;
	}
	else if (is_bsp2_old)
	{
		freeleaves32a = new BSPLEAF32A[leafCount];
		for (int n = 0; n < leafCount; n++)
		{
			freeleaves32a[n].iFirstMarkSurface = leaves[n].iFirstMarkSurface;
			freeleaves32a[n].nMarkSurfaces = leaves[n].nMarkSurfaces;
			for (int m = 0; m < MAX_AMBIENTS; m++)
			{
				freeleaves32a[n].nAmbientLevels[m] = leaves[n].nAmbientLevels[m];
			}
			freeleaves32a[n].nContents = leaves[n].nContents;
			freeleaves32a[n].nVisOffset = leaves[n].nVisOffset;
			for (int m = 0; m < 3; m++)
			{
				freeleaves32a[n].nMaxs[m] = (short)round(leaves[n].nMaxs[m]);
				freeleaves32a[n].nMins[m] = (short)round(leaves[n].nMins[m]);
			}
		}
		bsp_header.lump[LUMP_LEAVES].nLength = leafCount * sizeof(BSPLEAF32A);
		lumps[LUMP_LEAVES] = (unsigned char*)freeleaves32a;
	}

	unsigned char* oldedges = (unsigned char*)edges;
	BSPEDGE16* freeedges16 = NULL;

	if (!is_bsp2)
	{
		freeedges16 = new BSPEDGE16[edgeCount];
		for (int n = 0; n < edgeCount; n++)
		{
			freeedges16[n].iVertex[0] = (unsigned short)edges[n].iVertex[0];
			freeedges16[n].iVertex[1] = (unsigned short)edges[n].iVertex[1];
		}
		bsp_header.lump[LUMP_EDGES].nLength = edgeCount * sizeof(BSPEDGE16);
		lumps[LUMP_EDGES] = (unsigned char*)freeedges16;
	}


	if (is_blue_shift)
	{
		std::swap(bsp_header.lump[LUMP_PLANES], bsp_header.lump[LUMP_ENTITIES]);
		std::swap(lumps[LUMP_PLANES], lumps[LUMP_ENTITIES]);
	}

	if (g_settings.preserveCrc32 && !force_skip_crc)
	{
		if (ents.size() && ents[0]->hasKey("CRC"))
		{
			originCrc32 = reverse_bits(std::stoul(ents[0]->keyvalues["CRC"]));
			print_log("SPOOFING CRC value.\nLoading original CRC key from WORLDSPAWN: {}. ",
				reverse_bits(originCrc32));
		}
		else
			print_log(get_localized_string(LANG_0076), reverse_bits(originCrc32));

		unsigned int crc32 = UINT32_C(0xFFFFFFFF);

		for (int i = 0; i < HEADER_LUMPS; i++)
		{
			if (i != LUMP_ENTITIES)
				crc32 = GetCrc32InMemory(lumps[i], bsp_header.lump[i].nLength, crc32);
		}

		print_log(get_localized_string(LANG_0077), reverse_bits(crc32));

		if (originCrc32 == crc32)
		{
			print_log(get_localized_string(LANG_0078));
		}
		else
		{
			int originsize = bsp_header.lump[LUMP_MODELS].nLength;

			unsigned char* tmpNewModelds = new unsigned char[originsize + sizeof(BSPMODEL)];

			memset(tmpNewModelds, 0, originsize + sizeof(BSPMODEL));

			memcpy(tmpNewModelds, lumps[LUMP_MODELS], originsize);

			BSPMODEL* lastmodel = (BSPMODEL*)(tmpNewModelds + (modelCount * sizeof(BSPMODEL)));

			lastmodel->vOrigin.z = 9999.0f;

			if (replacedLump[LUMP_MODELS])
				delete[] lumps[LUMP_MODELS];
			lumps[LUMP_MODELS] = tmpNewModelds;

			bsp_header.lump[LUMP_MODELS].nLength = originsize + sizeof(BSPMODEL);

			crc32 = UINT32_C(0xFFFFFFFF);


			for (int i = 0; i < HEADER_LUMPS; i++)
			{
				if (i != LUMP_ENTITIES)
					crc32 = GetCrc32InMemory(lumps[i], bsp_header.lump[i].nLength, crc32);
			}

			PathCrc32InMemory(lumps[LUMP_MODELS], bsp_header.lump[LUMP_MODELS].nLength, originsize, crc32, originCrc32);

			crc32 = UINT32_C(0xFFFFFFFF);
			for (int i = 0; i < HEADER_LUMPS; i++)
			{
				if (i != LUMP_ENTITIES)
					crc32 = GetCrc32InMemory(lumps[i], bsp_header.lump[i].nLength, crc32);
			}

			print_log(get_localized_string(LANG_0079), reverse_bits(crc32));
		}
	}

	if (is_protected)
	{
		if (surfedgeCount > 0)
		{
			if (surfedges[surfedgeCount - 1] != 0)
			{
				int* newsurfs = new int[surfedgeCount + 1];
				memset(newsurfs, 0, (surfedgeCount + 1) * sizeof(int));
				memcpy(newsurfs, surfedges, surfedgeCount * sizeof(int));
				if (replacedLump[LUMP_SURFEDGES])
					delete[] lumps[LUMP_SURFEDGES];
				lumps[LUMP_SURFEDGES] = (unsigned char*)newsurfs;
				surfedgeCount++;
				bsp_header.lump[LUMP_SURFEDGES].nLength = (surfedgeCount) * sizeof(int);
			}
		}
	}

	print_log(get_localized_string(LANG_0080), bsp_path);
	// calculate lump offsets
	int offset = sizeof(BSPHEADER);

	if (is_bsp30ext && extralumps)
	{
		offset += sizeof(BSPHEADER_EX);

		int extralumpscount = bsp_header_ex.nVersion <= 3 ? EXTRA_LUMPS_OLD : EXTRA_LUMPS;
		for (int i = 0; i < extralumpscount; i++)
		{
			bsp_header_ex.lump[i].nOffset = offset;
			offset += bsp_header_ex.lump[i].nLength;
			file.write((char*)extralumps[i], bsp_header_ex.lump[i].nLength);

			int padding = ((bsp_header_ex.lump[i].nLength + 3) & ~3) - bsp_header_ex.lump[i].nLength;
			if (padding > 0)
			{
				offset += padding;
				unsigned char* zeropad = new unsigned char[padding];
				memset(zeropad, 0, padding);
				file.write((const char*)zeropad, padding);
				delete[] zeropad;
			}
			if (g_settings.verboseLogs)
				print_log(get_localized_string(LANG_0081), i, bsp_header_ex.lump[i].nLength, bsp_header_ex.lump[i].nOffset, padding);

		}
	}

	// write the lumps
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		bsp_header.lump[i].nOffset = offset;
		offset += bsp_header.lump[i].nLength;
		file.write((char*)lumps[i], bsp_header.lump[i].nLength);

		int padding = ((bsp_header.lump[i].nLength + 3) & ~3) - bsp_header.lump[i].nLength;
		if (padding > 0)
		{
			offset += padding;
			unsigned char* zeropad = new unsigned char[padding];
			memset(zeropad, 0, padding);
			file.write((const char*)zeropad, padding);
			delete[] zeropad;
		}

		if (g_settings.verboseLogs)
			print_log(get_localized_string(LANG_0082), i, bsp_header.lump[i].nLength, bsp_header.lump[i].nOffset, padding);
	}

	file.seekp(0);
	file.write((char*)&bsp_header, sizeof(BSPHEADER));
	if (is_bsp30ext && extralumps)
	{
		file.write((char*)&bsp_header_ex, sizeof(BSPHEADER_EX));
	}

	if (is_blue_shift)
	{
		std::swap(bsp_header.lump[LUMP_PLANES], bsp_header.lump[LUMP_ENTITIES]);
		std::swap(lumps[LUMP_PLANES], lumps[LUMP_ENTITIES]);
	}

	if (freeClipnodes16)
	{
		delete[] freeClipnodes16;
		lumps[LUMP_CLIPNODES] = (unsigned char*)oldClipnodes;
		bsp_header.lump[LUMP_CLIPNODES].nLength = clipnodeCount * sizeof(BSPCLIPNODE32);
	}

	if (freenodes16)
	{
		delete[] freenodes16;
		lumps[LUMP_NODES] = (unsigned char*)oldnodes;
		bsp_header.lump[LUMP_NODES].nLength = nodeCount * sizeof(BSPNODE32);
	}
	else if (freenodes32a)
	{
		delete[] freenodes32a;
		lumps[LUMP_NODES] = (unsigned char*)oldnodes;
		bsp_header.lump[LUMP_NODES].nLength = nodeCount * sizeof(BSPNODE32);
	}

	if (freeedges16)
	{
		delete[] freeedges16;
		lumps[LUMP_EDGES] = (unsigned char*)oldedges;
		bsp_header.lump[LUMP_EDGES].nLength = edgeCount * sizeof(BSPEDGE32);
	}

	if (freemarksurfs16)
	{
		delete[] freemarksurfs16;
		lumps[LUMP_MARKSURFACES] = (unsigned char*)oldmarksurfs;
		bsp_header.lump[LUMP_MARKSURFACES].nLength = marksurfCount * sizeof(int);
	}

	if (freefaces16)
	{
		delete[] freefaces16;
		lumps[LUMP_FACES] = (unsigned char*)oldfaces;
		bsp_header.lump[LUMP_FACES].nLength = faceCount * sizeof(BSPFACE32);
	}

	if (freeleaves16)
	{
		delete[] freeleaves16;
		lumps[LUMP_LEAVES] = (unsigned char*)oldleaves;
		bsp_header.lump[LUMP_LEAVES].nLength = leafCount * sizeof(BSPLEAF32);
	}
	else if (freeleaves32a)
	{
		delete[] freeleaves32a;
		lumps[LUMP_LEAVES] = (unsigned char*)oldleaves;
		bsp_header.lump[LUMP_LEAVES].nLength = leafCount * sizeof(BSPLEAF32);
	}

	// revert monochrome lighting and face offsets
	if (freelighting)
	{
		delete[] freelighting;
		lumps[LUMP_LIGHTING] = (unsigned char*)oldLighting;
		bsp_header.lump[LUMP_LIGHTING].nLength = lightDataLength * sizeof(COLOR3);
		if (!is_colored_lightmap)
		{
			for (int n = 0; n < faceCount; n++)
			{
				faces[n].nLightmapOffset = faces[n].nLightmapOffset * sizeof(COLOR3);
			}
		}
	}

	update_lump_pointers();
}

bool Bsp::load_lumps(std::string fpath)
{
	bool valid = true;

	// Read all BSP Data
	std::ifstream fin(fpath, std::ios::binary | std::ios::ate);
	auto size = fin.tellg();
	fin.seekg(0, std::ios::beg);

	if (size < sizeof(BSPHEADER) + sizeof(BSPLUMP) * HEADER_LUMPS)
		return false;

	fin.read((char*)&bsp_header.nVersion, sizeof(int));

	print_log("Bsp version: {}\n", bsp_header.nVersion >= 0 && bsp_header.nVersion <= 100 ? std::to_string(bsp_header.nVersion)
		: std::string({ ((char*)&bsp_header.nVersion)[0],((char*)&bsp_header.nVersion)[1],((char*)&bsp_header.nVersion)[2],((char*)&bsp_header.nVersion)[3] }));

	if (bsp_header.nVersion == '2PSB')
	{
		is_bsp2 = true;
		print_log(get_localized_string(LANG_0083));
	}

	if (bsp_header.nVersion == 'BSP2')
	{
		is_bsp2 = true;
		is_bsp2_old = true;
		print_log(get_localized_string(LANG_0084));
	}

	if (bsp_header.nVersion == 29)
	{
		is_bsp29 = true;
		print_log(get_localized_string(LANG_0085));
	}

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		fin.read((char*)&bsp_header.lump[i], sizeof(BSPLUMP));
		if (g_settings.verboseLogs)
			print_log(get_localized_string(LANG_0086), i, bsp_header.lump[i].nLength, bsp_header.lump[i].nOffset);
	}

	fin.read((char*)&bsp_header_ex.id, sizeof(int));

	if (bsp_header_ex.id == 'HSAX' /* XASH */)
	{
		print_log(get_localized_string(LANG_0087));
		is_bsp30ext = true;

		fin.read((char*)&bsp_header_ex.nVersion, sizeof(int));


		int extralumpscount = bsp_header_ex.nVersion <= 3 ? EXTRA_LUMPS_OLD : EXTRA_LUMPS;
		print_log(get_localized_string(LANG_0088), bsp_header_ex.nVersion, extralumpscount);

		extralumps = new unsigned char* [EXTRA_LUMPS];
		memset(extralumps, 0, sizeof(unsigned char*) * EXTRA_LUMPS);

		for (int i = 0; i < extralumpscount; i++)
		{
			fin.read((char*)&bsp_header_ex.lump[i], sizeof(BSPLUMP));
			if (g_settings.verboseLogs)
				print_log(get_localized_string(LANG_0089), i, bsp_header_ex.lump[i].nLength, bsp_header_ex.lump[i].nOffset);
		}

		for (int i = 0; i < extralumpscount; i++)
		{
			if (bsp_header_ex.lump[i].nLength == 0)
			{
				extralumps[i] = NULL;
				continue;
			}

			if (bsp_header_ex.lump[i].nOffset >= size || bsp_header_ex.lump[i].nOffset < 0 || bsp_header_ex.lump[i].nLength < 0)
			{
				print_log(get_localized_string(LANG_0090), i);
				is_bsp30ext = false;
				break;
			}

			fin.seekg(bsp_header_ex.lump[i].nOffset);
			if (fin.eof() || bsp_header_ex.lump[i].nOffset + bsp_header_ex.lump[i].nLength > size)
			{
				print_log(get_localized_string(LANG_1020), i);
				is_bsp30ext = false;
				break;
			}
			else
			{
				extralumps[i] = new unsigned char[bsp_header_ex.lump[i].nLength];
				fin.read((char*)extralumps[i], bsp_header_ex.lump[i].nLength);
			}
		}
	}


	lumps = new unsigned char* [HEADER_LUMPS];
	memset(lumps, 0, sizeof(unsigned char*) * HEADER_LUMPS);

	unsigned int crc32 = UINT32_C(0xFFFFFFFF);
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (bsp_header.lump[i].nLength == 0)
		{
			lumps[i] = NULL;
			continue;
		}

		fin.seekg(bsp_header.lump[i].nOffset);
		if (fin.eof())
		{
			print_log(get_localized_string(LANG_0091), i);
			valid = false;
		}
		else
		{
			lumps[i] = new unsigned char[bsp_header.lump[i].nLength];
			fin.read((char*)lumps[i], bsp_header.lump[i].nLength);
			replacedLump[i] = true;
		}
	}

	const char* classnametmp = "classname";

	if (bsp_header.lump[LUMP_PLANES].nLength < sizeof(BSPPLANE) ||
		&lumps[LUMP_PLANES][bsp_header.lump[LUMP_PLANES].nLength - 1] != std::search(&lumps[LUMP_PLANES][0], &lumps[LUMP_PLANES][bsp_header.lump[LUMP_PLANES].nLength - 1], &classnametmp[0], &classnametmp[strlen(classnametmp)]))
	{
		print_log(get_localized_string(LANG_0092));
		is_blue_shift = true;
		std::swap(bsp_header.lump[LUMP_PLANES], bsp_header.lump[LUMP_ENTITIES]);
		std::swap(lumps[LUMP_PLANES], lumps[LUMP_ENTITIES]);
	}

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (bsp_header.lump[i].nLength == 0)
		{
			lumps[i] = NULL;
			continue;
		}

		if (i != LUMP_ENTITIES)
		{
			crc32 = GetCrc32InMemory(lumps[i], bsp_header.lump[i].nLength, crc32);
		}

		if (i == LUMP_NODES)
		{
			if (is_bsp2)
			{
				if (is_bsp2_old)
				{
					nodeCount = bsp_header.lump[i].nLength / sizeof(BSPNODE32A);

					BSPNODE32* tmpnodes = new BSPNODE32[nodeCount];

					BSPNODE32A* nodes16 = (BSPNODE32A*)lumps[i];

					for (int n = 0; n < nodeCount; n++)
					{
						tmpnodes[n].iFirstFace = nodes16[n].firstFace;
						tmpnodes[n].iChildren[0] = nodes16[n].iChildren[0];
						tmpnodes[n].iChildren[1] = nodes16[n].iChildren[1];
						tmpnodes[n].iPlane = nodes16[n].iPlane;
						tmpnodes[n].nFaces = nodes16[n].nFaces;

						/*print_log("Face {} child0 {} child0 {} plane {} face {} ", tmpnodes[n].firstFace, tmpnodes[n].iChildren[0], tmpnodes[n].iChildren[1], tmpnodes[n].iPlane
							, tmpnodes[n].nFaces);*/

						for (int m = 0; m < 3; m++)
						{
							tmpnodes[n].nMaxs[m] = (float)nodes16[n].nMaxs[m];
							tmpnodes[n].nMins[m] = (float)nodes16[n].nMins[m];

							//	print_log("{} {} ", nodes16[n].nMaxs[m], nodes16[n].nMins[m]);
						}

						//print_log("\n");
					}

					delete[] lumps[i];

					lumps[i] = (unsigned char*)tmpnodes;
					bsp_header.lump[i].nLength = nodeCount * sizeof(BSPNODE32);
				}
				else
				{
					nodeCount = bsp_header.lump[i].nLength / sizeof(BSPNODE32);
					print_log(get_localized_string(LANG_0093));
				}
			}
			else
			{
				nodeCount = bsp_header.lump[i].nLength / sizeof(BSPNODE16);

				BSPNODE32* tmpnodes = new BSPNODE32[nodeCount];

				BSPNODE16* nodes16 = (BSPNODE16*)lumps[i];

				for (int n = 0; n < nodeCount; n++)
				{
					tmpnodes[n].iFirstFace = nodes16[n].firstFace;
					tmpnodes[n].iChildren[0] = nodes16[n].iChildren[0];
					tmpnodes[n].iChildren[1] = nodes16[n].iChildren[1];
					tmpnodes[n].iPlane = nodes16[n].iPlane;
					tmpnodes[n].nFaces = nodes16[n].nFaces;

					/*print_log("Face {} child0 {} child0 {} plane {} face {} ", tmpnodes[n].firstFace, tmpnodes[n].iChildren[0], tmpnodes[n].iChildren[1], tmpnodes[n].iPlane
						, tmpnodes[n].nFaces);*/

					for (int m = 0; m < 3; m++)
					{
						tmpnodes[n].nMaxs[m] = (float)nodes16[n].nMaxs[m];
						tmpnodes[n].nMins[m] = (float)nodes16[n].nMins[m];

						//	print_log("{} {} ", nodes16[n].nMaxs[m], nodes16[n].nMins[m]);
					}

					//print_log("\n");
				}

				delete[] lumps[i];

				lumps[i] = (unsigned char*)tmpnodes;
				bsp_header.lump[i].nLength = nodeCount * sizeof(BSPNODE32);
			}
		}


		if (i == LUMP_FACES)
		{
			BSPFACE32* tmpMapFaces = NULL;
			if (is_bsp2)
			{
				faceCount = bsp_header.lump[i].nLength / sizeof(BSPFACE32);
				tmpMapFaces = (BSPFACE32*)lumps[i];
				print_log(get_localized_string(LANG_0094));
			}
			else
			{
				faceCount = bsp_header.lump[i].nLength / sizeof(BSPFACE16);

				BSPFACE32* tmpfaces = new BSPFACE32[faceCount];
				tmpMapFaces = tmpfaces;

				BSPFACE16* faces16 = (BSPFACE16*)lumps[i];

				for (int n = 0; n < faceCount; n++)
				{
					tmpfaces[n].iFirstEdge = faces16[n].iFirstEdge;
					tmpfaces[n].iPlane = faces16[n].iPlane;
					tmpfaces[n].iTextureInfo = faces16[n].iTextureInfo;
					tmpfaces[n].nEdges = faces16[n].nEdges;
					tmpfaces[n].nLightmapOffset = faces16[n].nLightmapOffset;
					tmpfaces[n].nPlaneSide = faces16[n].nPlaneSide;
					for (int m = 0; m < MAX_LIGHTMAPS; m++)
					{
						tmpfaces[n].nStyles[m] = faces16[n].nStyles[m];
					}
				}

				delete[] lumps[i];
				lumps[i] = (unsigned char*)tmpfaces;
				replacedLump[i] = true;

				bsp_header.lump[i].nLength = faceCount * sizeof(BSPFACE32);
			}
		}
		if (i == LUMP_CLIPNODES)
		{
			if (is_bsp2 || (bsp_header.lump[i].nLength / sizeof(BSPCLIPNODE32) >= MAX_MAP_CLIPNODES_DEFAULT
				&& bsp_header.lump[i].nLength % sizeof(BSPCLIPNODE32) == 0))
			{
				is_32bit_clipnodes = true;
				clipnodeCount = bsp_header.lump[i].nLength / sizeof(BSPCLIPNODE32);

				print_log(get_localized_string(LANG_0095));
			}
			else
			{
				is_32bit_clipnodes = false;

				clipnodeCount = bsp_header.lump[i].nLength / sizeof(BSPCLIPNODE16);

				BSPCLIPNODE32* tmpclipnodes = new BSPCLIPNODE32[clipnodeCount];

				BSPCLIPNODE16* clipnodes16 = (BSPCLIPNODE16*)lumps[i];

				for (int n = 0; n < clipnodeCount; n++)
				{
					tmpclipnodes[n].iChildren[0] = clipnodes16[n].iChildren[0];

					tmpclipnodes[n].iChildren[1] = clipnodes16[n].iChildren[1];

					if (clipnodes16[n].iChildren[0] < CONTENTS_TRANSLUCENT || clipnodes16[n].iChildren[1] < CONTENTS_TRANSLUCENT)
					{
						is_broken_clipnodes = true;
					}

					tmpclipnodes[n].iPlane = clipnodes16[n].iPlane;
				}

				if (is_broken_clipnodes)
				{
					print_log(get_localized_string(LANG_0096));
				}
				else
				{
					print_log(get_localized_string(LANG_0097));
				}

				if (is_broken_clipnodes)
				{
					for (int n = 0; n < clipnodeCount; n++)
					{
						tmpclipnodes[n].iChildren[0] = (unsigned short)clipnodes16[n].iChildren[0];

						tmpclipnodes[n].iChildren[1] = (unsigned short)clipnodes16[n].iChildren[1];

						// Arguire QBSP 'broken' clipnodes
						if (tmpclipnodes[n].iChildren[0] >= clipnodeCount)
						{
							tmpclipnodes[n].iChildren[0] -= 65536;
						}

						if (tmpclipnodes[n].iChildren[1] >= clipnodeCount)
						{
							tmpclipnodes[n].iChildren[1] -= 65536;
						}

						tmpclipnodes[n].iPlane = clipnodes16[n].iPlane;
					}
				}

				delete[] lumps[i];
				lumps[i] = (unsigned char*)tmpclipnodes;
				replacedLump[i] = true;

				bsp_header.lump[i].nLength = clipnodeCount * sizeof(BSPCLIPNODE32);
			}
		}

		if (i == LUMP_LEAVES)
		{
			if (is_bsp2)
			{
				if (!is_bsp2_old)
				{
					leafCount = bsp_header.lump[i].nLength / sizeof(BSPLEAF32);
					print_log(get_localized_string(LANG_0098));
				}
				else
				{
					leafCount = bsp_header.lump[i].nLength / sizeof(BSPLEAF32A);

					BSPLEAF32* tmpleaves = new BSPLEAF32[leafCount];

					BSPLEAF32A* leaves16 = (BSPLEAF32A*)lumps[i];
					for (int n = 0; n < leafCount; n++)
					{
						tmpleaves[n].iFirstMarkSurface = leaves16[n].iFirstMarkSurface;
						tmpleaves[n].nMarkSurfaces = leaves16[n].nMarkSurfaces;
						for (int m = 0; m < MAX_AMBIENTS; m++)
						{
							tmpleaves[n].nAmbientLevels[m] = leaves16[n].nAmbientLevels[m];
						}
						tmpleaves[n].nContents = leaves16[n].nContents;
						tmpleaves[n].nVisOffset = leaves16[n].nVisOffset;
						for (int m = 0; m < 3; m++)
						{
							tmpleaves[n].nMaxs[m] = (float)leaves16[n].nMaxs[m];
							tmpleaves[n].nMins[m] = (float)leaves16[n].nMins[m];
						}

						//print_log("Leaf iFirstMarkSurface {} nMarkSurfaces {} nContents {} nVisOffset {} \n", 
						//	tmpleaves[n].iFirstMarkSurface, tmpleaves[n].nMarkSurfaces, tmpleaves[n].nContents, tmpleaves[n].nVisOffset);
					}

					delete[] lumps[i];
					lumps[i] = (unsigned char*)tmpleaves;
					replacedLump[i] = true;

					bsp_header.lump[i].nLength = leafCount * sizeof(BSPLEAF32);
				}
			}
			else
			{
				leafCount = bsp_header.lump[i].nLength / sizeof(BSPLEAF16);

				BSPLEAF32* tmpleaves = new BSPLEAF32[leafCount];

				BSPLEAF16* leaves16 = (BSPLEAF16*)lumps[i];
				for (int n = 0; n < leafCount; n++)
				{
					tmpleaves[n].iFirstMarkSurface = leaves16[n].iFirstMarkSurface;
					tmpleaves[n].nMarkSurfaces = leaves16[n].nMarkSurfaces;
					for (int m = 0; m < MAX_AMBIENTS; m++)
					{
						tmpleaves[n].nAmbientLevels[m] = leaves16[n].nAmbientLevels[m];
					}
					tmpleaves[n].nContents = leaves16[n].nContents;
					tmpleaves[n].nVisOffset = leaves16[n].nVisOffset;
					for (int m = 0; m < 3; m++)
					{
						tmpleaves[n].nMaxs[m] = (float)leaves16[n].nMaxs[m];
						tmpleaves[n].nMins[m] = (float)leaves16[n].nMins[m];
					}
				}

				delete[] lumps[i];
				lumps[i] = (unsigned char*)tmpleaves;
				replacedLump[i] = true;

				bsp_header.lump[i].nLength = leafCount * sizeof(BSPLEAF32);
			}
		}
		if (i == LUMP_MARKSURFACES)
		{
			if (is_bsp2)
			{
				marksurfCount = bsp_header.lump[i].nLength / sizeof(int);
				print_log(get_localized_string(LANG_0099));
			}
			else
			{
				marksurfCount = bsp_header.lump[i].nLength / sizeof(unsigned short);

				int* tmpSurf = new int[marksurfCount];

				unsigned short* surfs16 = (unsigned short*)lumps[i];

				for (int n = 0; n < marksurfCount; n++)
				{
					tmpSurf[n] = surfs16[n];
				}

				delete[] lumps[i];
				lumps[i] = (unsigned char*)tmpSurf;
				replacedLump[i] = true;

				bsp_header.lump[i].nLength = marksurfCount * sizeof(int);
			}
		}

		if (i == LUMP_EDGES)
		{
			if (is_bsp2)
			{
				edgeCount = bsp_header.lump[i].nLength / sizeof(BSPEDGE32);
				print_log(get_localized_string(LANG_0100));
			}
			else
			{
				edgeCount = bsp_header.lump[i].nLength / sizeof(BSPEDGE16);

				BSPEDGE32* tmpedges = new BSPEDGE32[edgeCount];

				BSPEDGE16* edges16 = (BSPEDGE16*)lumps[i];
				for (int n = 0; n < edgeCount; n++)
				{
					tmpedges[n].iVertex[0] = edges16[n].iVertex[0];
					tmpedges[n].iVertex[1] = edges16[n].iVertex[1];
				}

				delete[] lumps[i];
				lumps[i] = (unsigned char*)tmpedges;
				replacedLump[i] = true;

				bsp_header.lump[i].nLength = edgeCount * sizeof(BSPEDGE32);
			}
		}
	}

	update_lump_pointers();

	std::set<int> tmp_offsets;
	int lightmap3_bytes = 0;
	for (int i = 0; i < faceCount; i++)
	{
		int light_offset = faces[i].nLightmapOffset;

		if (light_offset >= 0 && !tmp_offsets.count(light_offset))
		{
			tmp_offsets.insert(light_offset);
			lightmap3_bytes += GetFaceLightmapSizeBytes(this, i);
		}
	}

	int lightmap1_bytes = lightmap3_bytes / sizeof(COLOR3);
	int lightmap4_bytes = lightmap1_bytes * sizeof(COLOR4);

	is_colored_lightmap = lightdata == NULL || abs(lightmap1_bytes - lightDataLength) > abs(lightmap3_bytes - lightDataLength);

	bool is_fuck_rgba_lightmap = false;

	if (is_colored_lightmap && lightdata != NULL)
	{
		if (abs(lightmap3_bytes - lightDataLength) > abs(lightmap4_bytes - lightDataLength))
		{
			is_fuck_rgba_lightmap = true;
			if (g_settings.verboseLogs)
			{
				print_log("fuck rgba lightmaps detected\n");
			}
		}
	}

	if (g_settings.verboseLogs)
	{
		//print_log(get_localized_string(LANG_0102), !is_colored_lightmap ? "monochrome" : "colored");
		print_log("Light: {} [mono {}, color {}, map has {}]\n", !is_colored_lightmap ? "monochrome" : "colored", lightmap1_bytes, lightmap3_bytes, lightDataLength);
	}

	int textures_bytes = 0;
	int textures_no_pal_bytes = 0;

	for (int t = 0; t < textureCount; t++)
	{
		int iStartOffset = ((int*)textures)[t + 1];

		if (iStartOffset < 0 || iStartOffset + (int)sizeof(BSPMIPTEX) > textureDataLength)
			continue;

		BSPMIPTEX* tex = ((BSPMIPTEX*)(textures + iStartOffset));

		int data_offset = tex->nOffsets[0];

		if (data_offset > textureDataLength)
			continue;

		textures_bytes += sizeof(BSPMIPTEX);
		textures_no_pal_bytes += sizeof(BSPMIPTEX);
		if (data_offset > 0)
		{
			textures_bytes += sizeof(short);
			textures_no_pal_bytes += sizeof(short);

			textures_bytes += sizeof(COLOR3) * 256;

			for (int i = 0; i < MIPLEVELS; i++)
			{
				textures_bytes += (tex->nWidth >> i) * (tex->nHeight >> i);
				textures_no_pal_bytes += (tex->nWidth >> i) * (tex->nHeight >> i);
			}
		}
	}

	is_texture_pal = textureCount == 0 || textures_no_pal_bytes == textures_bytes || abs(textures_no_pal_bytes - textureDataLength) > abs(textures_bytes - textureDataLength);

	if (g_settings.verboseLogs)
	{
		//print_log("Embedded Textures: {}\n", !is_texture_pal ? "quake pal" : "has pal");
		print_log("Embedded Textures: {} [pal:{}, nopal:{}, map has:{}]\n", !is_texture_pal ? "quake pal" : "has pal", textures_bytes, textures_no_pal_bytes, textureDataLength);
	}

	if (!is_colored_lightmap)
	{
		int lightPixels = bsp_header.lump[LUMP_LIGHTING].nLength;

		COLOR3* newLight = new COLOR3[lightPixels];

		for (int m = 0; m < lightPixels; m++)
		{
			newLight[m] = COLOR3(lumps[LUMP_LIGHTING][m], lumps[LUMP_LIGHTING][m], lumps[LUMP_LIGHTING][m]);
		}

		if (replacedLump[LUMP_LIGHTING])
			delete lumps[LUMP_LIGHTING];

		lumps[LUMP_LIGHTING] = (unsigned char*)newLight;
		bsp_header.lump[LUMP_LIGHTING].nLength = lightPixels * sizeof(COLOR3);


		for (int n = 0; n < faceCount; n++)
		{
			faces[n].nLightmapOffset = faces[n].nLightmapOffset * sizeof(COLOR3);
		}
	}
	else if (is_fuck_rgba_lightmap)
	{
		int lightPixels = bsp_header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR4);

		COLOR3* newLight = new COLOR3[lightPixels];
		COLOR4* oldLight = (COLOR4*)lumps[LUMP_LIGHTING];

		for (int m = 0; m < lightPixels; m++)
		{
			newLight[m] = oldLight[m].rgb(COLOR3(255, 255, 255));
		}

		if (replacedLump[LUMP_LIGHTING])
			delete lumps[LUMP_LIGHTING];

		lumps[LUMP_LIGHTING] = (unsigned char*)newLight;
		bsp_header.lump[LUMP_LIGHTING].nLength = lightPixels * sizeof(COLOR3);


		for (int n = 0; n < faceCount; n++)
		{
			faces[n].nLightmapOffset = (faces[n].nLightmapOffset / sizeof(COLOR4)) * sizeof(COLOR3);
		}
	}

	originCrc32 = crc32;

	fin.close();

	update_lump_pointers();
	return valid;
}

void Bsp::load_ents()
{
	for (size_t i = 0; i < ents.size(); i++)
		delete ents[i];
	ents.clear();

	membuf sbuf((char*)lumps[LUMP_ENTITIES], bsp_header.lump[LUMP_ENTITIES].nLength);
	std::istream in(&sbuf);

	int lineNum = 0;
	int lastBracket = -1;
	Entity* ent = NULL;

	std::string line;
	while (std::getline(in, line))
	{
		lineNum++;

		while (line[0] == ' ' || line[0] == '\t' || line[0] == '\r')
		{
			line.erase(line.begin());
		}

		if (line.length() < 1 || line[0] == '\n')
			continue;

		if (line[0] == '{')
		{
			if (lastBracket == 0)
			{
				print_log(get_localized_string(LANG_0103), bsp_path, lineNum);
				continue;
			}
			lastBracket = 0;
			if (ent)
				delete ent;
			ent = new Entity();

			if (line.find('}') == std::string::npos &&
				line.find('\"') == std::string::npos)
			{
				continue;
			}
		}
		if (line[0] == '}')
		{
			if (lastBracket == 1)
				print_log(get_localized_string(LANG_0104), bsp_path, lineNum);
			lastBracket = 1;
			if (!ent)
				continue;

			if (ent->keyvalues.count("classname"))
				ents.push_back(ent);
			else
				print_log(get_localized_string(LANG_0105));

			ent = NULL;

			// you can end/start an ent on the same line, you know
			if (line.find('{') != std::string::npos)
			{
				ent = new Entity();
				lastBracket = 0;

				if (line.find('\"') == std::string::npos)
				{
					continue;
				}
				line.erase(line.begin());
			}
		}
		if (lastBracket == 0 && ent) // currently defining an entity
		{
			Keyvalues k(line);
			for (size_t i = 0; i < k.keys.size(); i++)
			{
				ent->addKeyvalue(k.keys[i], k.values[i], true);
			}

			if (line.find('}') != std::string::npos)
			{
				lastBracket = 1;

				if (ent->keyvalues.count("classname"))
					ents.push_back(ent);
				else
					print_log(get_localized_string(LANG_1022));
				ent = NULL;
			}
			if (line.find('{') != std::string::npos)
			{
				ent = new Entity();
				lastBracket = 0;
			}
		}
	}

	if (ents.size() > 1)
	{
		if (ents[0]->keyvalues["classname"] != "worldspawn")
		{
			print_log(get_localized_string(LANG_0106));
			for (size_t i = 1; i < ents.size(); i++)
			{
				if (ents[i]->keyvalues["classname"] == "worldspawn")
				{
					std::swap(ents[0], ents[i]);
					break;
				}
			}
		}
	}

	if (ent)
		delete ent;
}

void Bsp::print_stat(const std::string& name, unsigned int val, unsigned int max, bool isMem)
{
	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	set_console_colors();


	print_log(get_localized_string(LANG_0107), name);


	if (isMem)
	{
		print_log("{:8.2f} /{:>7.2f}MB", val / meg, max / meg);
	}
	else
	{
		print_log("{:8} / {:>8}", val, max);
	}



	if (val > max)
	{
		set_console_colors(PRINT_RED | PRINT_INTENSITY);
	}
	else if (percent >= 90)
	{
		set_console_colors(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY);
	}
	else if (percent >= 75)
	{
		set_console_colors(PRINT_RED | PRINT_GREEN | PRINT_BLUE | PRINT_INTENSITY);
	}
	else
	{
		set_console_colors(PRINT_GREEN);
	}

	print_log(" {:6.1f}%", percent);

	set_console_colors();
	if (val > max)
	{
		print_log(get_localized_string(LANG_0108));
	}

	print_log("\n");

}

void Bsp::print_model_stat(STRUCTUSAGE* modelInfo, unsigned int val, int max, bool isMem)
{
	std::string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	std::string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (size_t k = 0; k < ents.size(); k++)
	{
		if (ents[k]->getBspModelIdx() == modelInfo->modelIdx)
		{
			targetname = ents[k]->keyvalues["targetname"];
			classname = ents[k]->keyvalues["classname"];
		}
	}

	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	if (isMem)
	{
		print_log("{:8.1f} / {:>5.1f}", val / meg, max / meg);
	}
	else
	{
		print_log(get_localized_string(LANG_0109), classname, targetname, modelInfo->modelIdx, val);
	}
	if (percent >= 0.1f)
		print_log("  {:6.1f}%", percent);

	print_log("\n");
}

bool sortModelInfos(const STRUCTUSAGE* a, const STRUCTUSAGE* b)
{
	switch (g_sort_mode)
	{
	case SORT_VERTS:
		return a->sum.verts > b->sum.verts;
	case SORT_NODES:
		return a->sum.nodes > b->sum.nodes;
	case SORT_CLIPNODES:
		return a->sum.clipnodes > b->sum.clipnodes;
	case SORT_FACES:
		return a->sum.faces > b->sum.faces;
	}
	return false;
}

bool Bsp::isValid()
{
	if (planeCount > (is_bsp2 ? INT_MAX : MAX_MAP_PLANES))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0179));
	}
	if (texinfoCount > (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0180));
	}
	if (leafCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_LEAVES))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0181));
	}
	if (modelCount > (int)MAX_MAP_MODELS)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0182));
	}
	if (texinfoCount > (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1037));
	}
	if (nodeCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_NODES))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0183));
	}
	if (vertCount > (is_bsp2 ? INT_MAX : MAX_MAP_VERTS))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0184));
	}
	if (faceCount > (is_bsp2 ? INT_MAX : MAX_MAP_FACES))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0185));
	}
	if (clipnodeCount > (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : MAX_MAP_CLIPNODES))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0186));
	}
	if (marksurfCount > (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0187));
	}
	if (surfedgeCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_SURFEDGES))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0188));
	}
	if (edgeCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_EDGES))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0189));
	}
	if (textureCount > (int)MAX_MAP_TEXTURES)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0190));
	}
	if (lightDataLength > (int)MAX_MAP_LIGHTDATA)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0191));
	}
	if (visDataLength > (int)MAX_MAP_VISDATA)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0192));
	}
	if (ents.size() > MAX_MAP_ENTS)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, "Overflowed entities !!!\n");
	}

	return modelCount <= (int)MAX_MAP_MODELS
		&& planeCount <= (is_bsp2 ? INT_MAX : MAX_MAP_PLANES)
		&& vertCount <= MAX_MAP_VERTS
		&& nodeCount <= (is_bsp2 ? INT_MAX : (int)MAX_MAP_NODES)
		&& texinfoCount <= (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS)
		&& faceCount <= (is_bsp2 ? INT_MAX : MAX_MAP_FACES)
		&& clipnodeCount <= (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : MAX_MAP_CLIPNODES)
		&& leafCount <= (is_bsp2 ? INT_MAX : (int)MAX_MAP_LEAVES)
		&& marksurfCount <= (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS)
		&& surfedgeCount <= (is_bsp2 ? INT_MAX : (int)MAX_MAP_SURFEDGES)
		&& edgeCount <= (is_bsp2 ? INT_MAX : (int)MAX_MAP_SURFEDGES)
		&& textureCount <= (int)MAX_MAP_TEXTURES
		&& lightDataLength <= (int)MAX_MAP_LIGHTDATA
		&& visDataLength <= (int)MAX_MAP_VISDATA
		&& ents.size() <= MAX_MAP_ENTS;
}

bool Bsp::validate()
{
	bool isValid = true;

	for (int i = 0; i < marksurfCount; i++)
	{
		if (marksurfs[i] < 0 || marksurfs[i] >= faceCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0110), i, marksurfs[i], faceCount);
			isValid = false;
		}
	}
	for (int i = 0; i < surfedgeCount; i++)
	{
		if (abs(surfedges[i]) >= edgeCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0111), i, surfedges[i], edgeCount);
			isValid = false;
		}
	}
	for (int i = 0; i < texinfoCount; i++)
	{
		if (texinfos[i].iMiptex >= textureCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0112), i, texinfos[i].iMiptex, textureCount);
			isValid = false;
		}
	}
	for (int i = 0; i < faceCount; i++)
	{
		if (faces[i].iPlane < 0 || faces[i].iPlane >= planeCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0113), i, faces[i].iPlane, planeCount);
			isValid = false;
		}
		if (faces[i].nEdges < 0 || faces[i].iFirstEdge < 0 || faces[i].iFirstEdge + faces[i].nEdges > surfedgeCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0114), i, faces[i].iFirstEdge, surfedgeCount);
			isValid = false;
		}
		if (faces[i].nEdges < 3)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string("LANG_BAD_EDGES_NUM"), faces[i].nEdges, i);
			isValid = false;
		}
		if (faces[i].iTextureInfo >= texinfoCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0115), i, faces[i].iTextureInfo, texinfoCount);
			isValid = false;
		}
		if (lightDataLength > 0 &&
			faces[i].nLightmapOffset >= 0 && faces[i].nLightmapOffset > lightDataLength)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0116), i, faces[i].nLightmapOffset, lightDataLength);
			isValid = false;
		}
		int bmins[2];
		int bmaxs[2];
		if (isValid)
			isValid = GetFaceExtents(this, i, bmins, bmaxs);
	}
	for (int i = 0; i < leafCount; i++)
	{
		if (leaves[i].nMarkSurfaces < 0 || leaves[i].iFirstMarkSurface < 0 || leaves[i].iFirstMarkSurface + leaves[i].nMarkSurfaces > marksurfCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0117), i, leaves[i].iFirstMarkSurface, marksurfCount);
			isValid = false;
		}
		if (visDataLength > 0 &&
			leaves[i].nVisOffset != -1 && (leaves[i].nVisOffset < 0 || leaves[i].nVisOffset >= visDataLength))
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0118), i, leaves[i].nVisOffset, visDataLength);
			isValid = false;
		}

		for (int n = 0; n < 3; n++)
		{
			if (leaves[i].nMins[n] > leaves[i].nMaxs[n])
			{
				print_log(PRINT_RED | PRINT_INTENSITY, "backwards mins / maxs in leaf {} Mins: ({}, {}, {}) Maxs: ({} {} {})", i, leaves[i].nMins[0], leaves[i].nMins[1], leaves[i].nMins[2],
					leaves[i].nMaxs[0], leaves[i].nMaxs[1], leaves[i].nMaxs[2]);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < edgeCount; i++)
	{
		for (int k = 0; k < 2; k++)
		{
			if (edges[i].iVertex[k] < 0 || edges[i].iVertex[k] >= vertCount)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0119), i, edges[i].iVertex[k], vertCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < nodeCount; i++)
	{
		if (nodes[i].nFaces < 0 || nodes[i].iFirstFace < 0 || nodes[i].iFirstFace + nodes[i].nFaces > faceCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0120), i, nodes[i].iFirstFace, faceCount);
			isValid = false;
		}
		if (nodes[i].iPlane < 0 || nodes[i].iPlane >= planeCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0121), i, nodes[i].iPlane, planeCount);
			isValid = false;
		}
		for (int k = 0; k < 2; k++)
		{
			if (nodes[i].iChildren[k] != -1 && nodes[i].iChildren[k] > 0 && nodes[i].iChildren[k] >= nodeCount)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0122), i, k, nodes[i].iChildren[k], nodeCount);
				isValid = false;
			}
			else if (~nodes[i].iChildren[k] != -1 && nodes[i].iChildren[k] < 0 && ~nodes[i].iChildren[k] >= leafCount)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0123), i, k, ~nodes[i].iChildren[k], leafCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < clipnodeCount; i++)
	{
		if (clipnodes[i].iPlane < 0 || clipnodes[i].iPlane >= planeCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0124), i, clipnodes[i].iPlane, planeCount);
			isValid = false;
		}
		for (int k = 0; k < 2; k++)
		{
			if (clipnodes[i].iChildren[k] > 0 && clipnodes[i].iChildren[k] >= clipnodeCount)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0125), i, k, clipnodes[i].iChildren[k], clipnodeCount);
				isValid = false;
			}
		}
	}
	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdxForce() > 0 && ents[i]->getBspModelIdxForce() >= modelCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0126), i, ents[i]->getBspModelIdxForce(), modelCount);
			isValid = false;
		}
	}


	int totalVisLeaves = 1; // solid leaf not included in model leaf counts
	int totalFaces = 0;
	for (int i = 0; i < modelCount; i++)
	{
		totalVisLeaves += models[i].nVisLeafs;
		totalFaces += models[i].nFaces;
		if (models[i].nFaces < 0 || models[i].iFirstFace < 0 || models[i].iFirstFace + models[i].nFaces > faceCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0127), i, models[i].iFirstFace, faceCount);
			isValid = false;
		}
		if (models[i].iHeadnodes[0] >= nodeCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0128), i, models[i].iHeadnodes[0], nodeCount);
			isValid = false;
		}
		for (int k = 1; k < MAX_MAP_HULLS; k++)
		{
			if (models[i].iHeadnodes[k] >= clipnodeCount)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0129), i, k, models[i].iHeadnodes[k], clipnodeCount);
				isValid = false;
			}
		}
		if (models[i].nMins.x > models[i].nMaxs.x ||
			models[i].nMins.y > models[i].nMaxs.y ||
			models[i].nMins.z > models[i].nMaxs.z)
		{
			print_log("Backwards mins/maxs in model {}. Mins: ({}, {}, {}) Maxs: ({} {} {})\n", i,
				models[i].nMins.x, models[i].nMins.y, models[i].nMins.z,
				models[i].nMaxs.x, models[i].nMaxs.y, models[i].nMaxs.z);
			isValid = false;
		}
	}
	if (totalVisLeaves != leafCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0130), totalVisLeaves, leafCount);
		isValid = false;
	}

	if (totalFaces > faceCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0131), totalFaces, faceCount);
		isValid = false;
	}

	unsigned int worldspawn_count = 0;
	for (unsigned int i = 0; i < ents.size(); i++)
	{
		if (ents[i]->isWorldSpawn())
		{
			worldspawn_count++;
		}
	}
	if (worldspawn_count != 1)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0132), worldspawn_count, ents.size());
		isValid = false;
	}

	std::set<int> used_models; // Protected map
	used_models.insert(0);

	for (auto const& s : ents)
	{
		int ent_mdl_id = s->getBspModelIdx();
		if (ent_mdl_id >= 0)
		{
			if (!used_models.count(ent_mdl_id))
			{
				used_models.insert(ent_mdl_id);
			}
		}
	}

	for (int i = 0; i < modelCount; i++)
	{
		if (!used_models.count(i))
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1023), bsp_name, i);
		}
	}

	for (int i = 0; i < textureCount; i++)
	{
		int texOffset = ((int*)textures)[i + 1];
		if (texOffset >= 0)
		{
			int texlen = getBspTextureSize(i);
			int dataOffset = (textureCount + 1) * sizeof(int);
			BSPMIPTEX* tex = (BSPMIPTEX*)(textures + texOffset);
			if (tex->szName[0] == '\0')
			{
				isValid = false;
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0133), i);
			}
			else if (strlen(tex->szName) >= MAXTEXTURENAME)
			{
				isValid = false;
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0134), i);
			}
			if (tex->nOffsets[0] > 0 && /*dataOffset + */texOffset + texlen > bsp_header.lump[LUMP_TEXTURES].nLength)
			{
				isValid = false;
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0135), i, /*dataOffset + */texOffset + texlen, bsp_header.lump[LUMP_TEXTURES].nLength);
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0136), texlen, tex->szName[0] != '\0' ? tex->szName : "UNKNOWN_NAME", texOffset, dataOffset);
			}
			if (texlen == 0)
			{
				isValid = false;
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string("LANG_ERROR_TEXLEN"), i);
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0136), texlen, tex->szName[0] != '\0' ? tex->szName : "UNKNOWN_NAME", texOffset, dataOffset);
			}
		}
	}

	if (planeCount > (is_bsp2 ? INT_MAX : MAX_MAP_PLANES))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0179));
	}
	if (texinfoCount > (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0180));
	}
	if (leafCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_LEAVES))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0181));
	}
	if (modelCount > (int)MAX_MAP_MODELS)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0182));
	}
	if (texinfoCount > (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1037));
	}
	if (nodeCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_NODES))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0183));
	}
	if (vertCount > (is_bsp2 ? INT_MAX : MAX_MAP_VERTS))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0184));
	}
	if (faceCount > (is_bsp2 ? INT_MAX : MAX_MAP_FACES))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0185));
	}
	if (clipnodeCount > (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : MAX_MAP_CLIPNODES))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0186));
	}
	if (marksurfCount > (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0187));
	}
	if (surfedgeCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_SURFEDGES))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0188));
	}
	if (edgeCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_EDGES))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0189));
	}
	if (textureCount > (int)MAX_MAP_TEXTURES)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0190));
	}
	if (lightDataLength > (int)MAX_MAP_LIGHTDATA)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0191));
	}
	if (visDataLength > (int)MAX_MAP_VISDATA)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0192));
	}
	if (ents.size() > MAX_MAP_ENTS)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, "Overflowed entities !!!\n");
	}

	unsigned int newVisRowSize = ((leafCount + 63) & ~63) >> 3;
	int decompressedVisSize = leafCount * newVisRowSize;
	unsigned char* decompressedVis = new unsigned char[decompressedVisSize];
	memset(decompressedVis, 0xFF, decompressedVisSize);
	decompress_vis_lump(this, leaves, visdata, decompressedVis,
		models[0].nVisLeafs, leafCount - 1, leafCount - 1, decompressedVisSize, bsp_header.lump[LUMP_VISIBILITY].nLength);
	delete[] decompressedVis;

	return isValid;
}

std::vector<STRUCTUSAGE*> Bsp::get_sorted_model_infos(int sortMode)
{
	std::vector<STRUCTUSAGE*> modelStructs;
	modelStructs.resize(modelCount);

	for (int i = 0; i < modelCount; i++)
	{
		modelStructs[i] = new STRUCTUSAGE(this);
		modelStructs[i]->modelIdx = i;
		mark_model_structures(i, modelStructs[i], false);
		modelStructs[i]->compute_sum();
	}

	g_sort_mode = sortMode;
	sort(modelStructs.begin(), modelStructs.end(), sortModelInfos);

	return modelStructs;
}

void Bsp::print_info(bool perModelStats, int perModelLimit, int sortMode)
{
	unsigned int entCount = (unsigned int)ents.size();

	if (perModelStats)
	{
		g_sort_mode = sortMode;

		if (planeCount >= (is_bsp2 ? INT_MAX : MAX_MAP_PLANES) || texinfoCount >= (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS) || leafCount >= (is_bsp2 ? INT_MAX : (int)MAX_MAP_LEAVES) ||
			modelCount >= (int)MAX_MAP_MODELS || nodeCount >= (is_bsp2 ? INT_MAX : (int)MAX_MAP_NODES) || vertCount >= MAX_MAP_VERTS ||
			faceCount >= (is_bsp2 ? INT_MAX : MAX_MAP_FACES) || clipnodeCount >= (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : MAX_MAP_CLIPNODES) || marksurfCount >= (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS) ||
			surfedgeCount >= (is_bsp2 ? INT_MAX : (int)MAX_MAP_SURFEDGES) || (is_bsp2 ? INT_MAX : edgeCount >= (int)MAX_MAP_EDGES) || textureCount >= (int)MAX_MAP_TEXTURES ||
			lightDataLength >= (int)MAX_MAP_LIGHTDATA || visDataLength >= (int)MAX_MAP_VISDATA)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0137));
			return;
		}

		std::vector<STRUCTUSAGE*> modelStructs = get_sorted_model_infos(sortMode);

		int maxCount = 0;
		const char* countName = "None";

		switch (g_sort_mode)
		{
		case SORT_VERTS:		maxCount = vertCount; countName = "  Verts";  break;
		case SORT_NODES:		maxCount = nodeCount; countName = "  Nodes";  break;
		case SORT_CLIPNODES:	maxCount = clipnodeCount; countName = "Clipnodes";  break;
		case SORT_FACES:		maxCount = faceCount; countName = "  Faces";  break;
		}

		print_log(get_localized_string(LANG_0138), countName);
		print_log("-------------------------  -------------------------  -----  ----------  --------\n");

		for (int i = 0; i < modelCount && i < perModelLimit; i++)
		{

			int val = 0;
			switch (g_sort_mode)
			{
			case SORT_VERTS:		val = modelStructs[i]->sum.verts; break;
			case SORT_NODES:		val = modelStructs[i]->sum.nodes; break;
			case SORT_CLIPNODES:	val = modelStructs[i]->sum.clipnodes; break;
			case SORT_FACES:		val = modelStructs[i]->sum.faces; break;
			}

			if (val == 0)
				break;

			print_model_stat(modelStructs[i], val, maxCount, false);
		}
	}
	else
	{
		print_log(get_localized_string(LANG_0139));
		print_log("------------  -------------------  --------\n");
		print_stat("models", modelCount, MAX_MAP_MODELS, false);
		print_stat("planes", planeCount, (is_bsp2 ? INT_MAX : MAX_MAP_PLANES), false);
		print_stat("vertexes", vertCount, MAX_MAP_VERTS, false);
		print_stat("nodes", nodeCount, is_bsp2 ? INT_MAX : (int)MAX_MAP_NODES, false);
		print_stat("texinfos", texinfoCount, (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS), false);
		print_stat("faces", faceCount, (is_bsp2 ? INT_MAX : MAX_MAP_FACES), false);
		print_stat("clipnodes", clipnodeCount, (is_32bit_clipnodes ? INT_MAX : MAX_MAP_CLIPNODES), false);
		print_stat("leaves", leafCount, (is_bsp2 ? INT_MAX : MAX_MAP_LEAVES), false);
		print_stat("marksurfaces", marksurfCount, (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS), false);
		print_stat("surfedges", surfedgeCount, (is_bsp2 ? INT_MAX : MAX_MAP_SURFEDGES), false);
		print_stat("edges", edgeCount, (is_bsp2 ? INT_MAX : MAX_MAP_EDGES), false);
		print_stat("textures", textureCount, MAX_MAP_TEXTURES, false);
		print_stat("lightdata", lightDataLength, MAX_MAP_LIGHTDATA, true);
		print_stat("visdata", visDataLength, MAX_MAP_VISDATA, true);
		print_stat("entities", entCount, MAX_MAP_ENTS, false);
	}
}

void Bsp::print_model_bsp(int modelIdx)
{
	int node = models[modelIdx].iHeadnodes[0];
	recurse_node_print(node, 0);
}

void Bsp::print_clipnode_tree(int iNode, int depth)
{
	for (int i = 0; i < depth; i++)
	{
		print_log("    ");
	}

	if (iNode < 0)
	{
		print_log(getLeafContentsName(iNode));
		print_log("\n");
		return;
	}
	else
	{
		if (iNode >= clipnodeCount)
		{
			print_log(PRINT_RED, "!NODE ERROR!");
			return;
		}
		else
		{
			if (clipnodes[iNode].iPlane < 0 || clipnodes[iNode].iPlane >= planeCount)
			{
				print_log(PRINT_RED, "!PLANE ERROR!");
				return;
			}
			else
			{
				BSPPLANE& plane = planes[clipnodes[iNode].iPlane];
				print_log(get_localized_string(LANG_0140), plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist);
			}
		}
	}


	for (int i = 0; i < 2; i++)
	{
		print_clipnode_tree(clipnodes[iNode].iChildren[i], depth + 1);
	}
}

void Bsp::print_model_hull(int modelIdx, int hull_number)
{
	if (modelIdx < 0 || modelIdx > bsp_header.lump[LUMP_MODELS].nLength / (int)sizeof(BSPMODEL))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1024), modelIdx);
		return;
	}

	// the first hull is used for point-sized clipping, but uses nodes and not clipnodes.
	if (hull_number < 0 || hull_number >= MAX_MAP_HULLS)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0141), MAX_MAP_HULLS);
		return;
	}

	BSPMODEL& model = models[modelIdx];

	print_log(get_localized_string(LANG_0142), modelIdx, hull_number, get_model_usage(modelIdx));

	if (hull_number == 0)
		print_model_bsp(modelIdx);
	else
		print_clipnode_tree(model.iHeadnodes[hull_number], 0);
}

std::string Bsp::get_model_usage(int modelIdx)
{
	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdx() == modelIdx)
		{
			return "\"" + ents[i]->keyvalues["targetname"] + "\" (" + ents[i]->keyvalues["classname"] + ")";
		}
	}
	return "(unused)";
}

std::vector<Entity*> Bsp::get_model_ents(int modelIdx)
{
	std::vector<Entity*> uses;
	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdx() == modelIdx)
		{
			uses.push_back(ents[i]);
		}
	}
	return uses;
}

std::vector<size_t> Bsp::get_model_ents_ids(int modelIdx)
{
	std::vector<size_t> uses;
	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdxForce() == modelIdx)
		{
			uses.push_back(i);
		}
	}
	return uses;
}

void Bsp::recurse_node_print(int nodeIdx, int depth)
{
	for (int i = 0; i < depth; i++)
	{
		print_log("    ");
	}

	if (nodeIdx < 0)
	{
		if (~nodeIdx >= leafCount)
		{
			print_log(PRINT_RED, "!LEAF ERROR!");
			return;
		}
		BSPLEAF32& leaf = leaves[~nodeIdx];
		print_leaf(leaf);
		print_log(get_localized_string(LANG_0143), ~nodeIdx);
		return;
	}
	else
	{
		if (nodeIdx >= nodeCount)
		{
			print_log(PRINT_RED, "!NODE ERROR!");
			return;
		}
		print_node(nodes[nodeIdx]);
	}

	recurse_node_print(nodes[nodeIdx].iChildren[0], depth + 1);
	recurse_node_print(nodes[nodeIdx].iChildren[1], depth + 1);
}


void Bsp::get_last_node(int nodeIdx, int& node, int& count, int last_node)
{
	if (nodeIdx < 0)
	{
		return;
	}

	if (last_node != -1 && count >= last_node)
	{
		return;
	}
	count++;
	node = nodeIdx;

	get_last_node(nodes[nodeIdx].iChildren[0], node, count, last_node);
	get_last_node(nodes[nodeIdx].iChildren[1], node, count, last_node);
}

void Bsp::get_last_clipnode(int nodeIdx, int& node, int& count, int last_node)
{
	if (nodeIdx < 0)
	{
		return;
	}

	if (last_node != -1 && count >= last_node)
	{
		return;
	}
	count++;
	node = nodeIdx;

	get_last_clipnode(clipnodes[nodeIdx].iChildren[0], node, count, last_node);
	get_last_clipnode(clipnodes[nodeIdx].iChildren[1], node, count, last_node);
}

void Bsp::print_node(const BSPNODE32& node)
{
	if (node.iPlane < 0 || node.iPlane >= planeCount)
	{
		print_log(PRINT_RED, "!PLANE ERROR!");
		return;
	}
	else
	{
		BSPPLANE& plane = planes[node.iPlane];

		print_log("Plane ({} {} {}) d: {}, Faces: {}, Min({}, {}, {}), Max({}, {}, {})\n",
			plane.vNormal.x, plane.vNormal.y, plane.vNormal.z,
			plane.fDist, node.nFaces,
			node.nMins[0], node.nMins[1], node.nMins[2],
			node.nMaxs[0], node.nMaxs[1], node.nMaxs[2]);
	}
}

int Bsp::pointContents(int iNode, const vec3& p, int hull, std::vector<int>& nodeBranch, int& leafIdx, int& childIdx)
{
	leafIdx = -1;
	childIdx = -1;
	if (iNode < 0)
	{
		return CONTENTS_EMPTY;
	}

	if (hull == 0)
	{
		while (iNode >= 0)
		{
			nodeBranch.push_back(iNode);
			BSPNODE32& node = nodes[iNode];
			BSPPLANE& plane = planes[node.iPlane];

			float d = dotProduct(plane.vNormal, p) - plane.fDist;
			if (d < 0)
			{
				iNode = node.iChildren[1];
				childIdx = 1;
			}
			else
			{
				iNode = node.iChildren[0];
				childIdx = 0;
			}
		}

		leafIdx = ~iNode;
		return leaves[~iNode].nContents;
	}
	else
	{
		while (iNode >= 0)
		{
			nodeBranch.push_back(iNode);
			BSPCLIPNODE32& node = clipnodes[iNode];
			BSPPLANE& plane = planes[node.iPlane];

			float d = dotProduct(plane.vNormal, p) - plane.fDist;
			if (d < 0)
			{
				iNode = node.iChildren[1];
				childIdx = 1;
			}
			else
			{
				iNode = node.iChildren[0];
				childIdx = 0;
			}
		}

		return iNode;
	}
}


void Bsp::recurse_node_leafs(int nodeIdx, std::vector<int>& outLeafs)
{
	if (nodeIdx < 0)
	{
		outLeafs.push_back(~nodeIdx);
		return;
	}

	BSPNODE32& node = nodes[nodeIdx];
	recurse_node_leafs(node.iChildren[0], outLeafs);
	recurse_node_leafs(node.iChildren[1], outLeafs);
}

size_t Bsp::modelLeafs(const BSPMODEL& model, std::vector<int>& outLeafs)
{
	int nodeIdx = model.iHeadnodes[0];
	recurse_node_leafs(nodeIdx, outLeafs);
	std::sort(outLeafs.begin(), outLeafs.end());
	outLeafs.erase(std::unique(outLeafs.begin(), outLeafs.end()), outLeafs.end());
	return outLeafs.size();
}

size_t Bsp::modelLeafs(int modelIdx, std::vector<int>& outLeafs)
{
	return modelLeafs(models[modelIdx], outLeafs);
}


int Bsp::pointContents(int iNode, const vec3& p, int hull)
{
	std::vector<int> nodeBranch;
	int leafIdx;
	int childIdx;
	return pointContents(iNode, p, hull, nodeBranch, leafIdx, childIdx);
}

const char* Bsp::getLeafContentsName(int contents)
{
	switch (contents)
	{
	case CONTENTS_EMPTY:
		return "EMPTY";
	case CONTENTS_SOLID:
		return "SOLID";
	case CONTENTS_WATER:
		return "WATER";
	case CONTENTS_SLIME:
		return "SLIME";
	case CONTENTS_LAVA:
		return "LAVA";
	case CONTENTS_SKY:
		return "SKY";
	case CONTENTS_ORIGIN:
		return "ORIGIN";
	case CONTENTS_CURRENT_0:
		return "CURRENT_0";
	case CONTENTS_CURRENT_90:
		return "CURRENT_90";
	case CONTENTS_CURRENT_180:
		return "CURRENT_180";
	case CONTENTS_CURRENT_270:
		return "CURRENT_270";
	case CONTENTS_CURRENT_UP:
		return "CURRENT_UP";
	case CONTENTS_CURRENT_DOWN:
		return "CURRENT_DOWN";
	case CONTENTS_TRANSLUCENT:
		return "TRANSLUCENT";
	default:
		return "UNKNOWN";
	}
}

void Bsp::mark_face_structures(int iFace, STRUCTUSAGE* usage)
{
	if (iFace > faceCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0144));
		return;
	}
	BSPFACE32& face = faces[iFace];
	usage->faces[iFace] = true;

	for (int e = 0; e < face.nEdges; e++)
	{
		int edgeIdx = surfedges[face.iFirstEdge + e];
		BSPEDGE32& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

		usage->surfEdges[face.iFirstEdge + e] = true;
		usage->edges[abs(edgeIdx)] = true;
		usage->verts[vertIdx] = true;
	}

	usage->texInfo[face.iTextureInfo] = true;
	usage->planes[face.iPlane] = true;
	usage->textures[texinfos[face.iTextureInfo].iMiptex] = true;
}

void Bsp::mark_node_structures(int iNode, STRUCTUSAGE* usage, bool skipLeaves)
{
	if (iNode > nodeCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0145));
		return;
	}
	BSPNODE32& node = nodes[iNode];

	usage->nodes[iNode] = true;
	usage->planes[node.iPlane] = true;

	for (int i = 0; i < node.nFaces; i++)
	{
		mark_face_structures(node.iFirstFace + i, usage);
	}

	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			mark_node_structures(node.iChildren[i], usage, skipLeaves);
		}
		else if (!skipLeaves)
		{
			BSPLEAF32& leaf = leaves[~node.iChildren[i]];
			for (int n = 0; n < leaf.nMarkSurfaces; n++)
			{
				usage->markSurfs[leaf.iFirstMarkSurface + n] = true;
				mark_face_structures(marksurfs[leaf.iFirstMarkSurface + n], usage);
			}

			usage->leaves[~node.iChildren[i]] = true;
		}
	}
}

void Bsp::mark_clipnode_structures(int iNode, STRUCTUSAGE* usage)
{
	if (iNode > clipnodeCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0146));
		return;
	}
	BSPCLIPNODE32& node = clipnodes[iNode];

	usage->clipnodes[iNode] = true;
	usage->planes[node.iPlane] = true;

	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			mark_clipnode_structures(node.iChildren[i], usage);
		}
	}
}

void Bsp::mark_model_structures(int modelIdx, STRUCTUSAGE* usage, bool skipLeaves, bool makeSomething)
{
	if (modelIdx > modelCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0147));
		return;
	}
	BSPMODEL& model = models[modelIdx];

	for (int i = 0; i < model.nFaces; i++)
	{
		mark_face_structures(model.iFirstFace + i, usage);
	}
	if (model.iHeadnodes[0] >= 0 && (model.iHeadnodes[0] < clipnodeCount || model.iHeadnodes[0] < nodeCount))
		mark_node_structures(model.iHeadnodes[0], usage, skipLeaves);
	int k = 1;
	if (makeSomething)
		k = 0;
	for (; k < MAX_MAP_HULLS; k++)
	{
		if (model.iHeadnodes[k] >= 0 && model.iHeadnodes[k] < clipnodeCount)
			mark_clipnode_structures(model.iHeadnodes[k], usage);
	}
}

void Bsp::remap_face_structures(int faceIdx, STRUCTREMAP* remap)
{
	if (faceIdx > faceCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0148));
		return;
	}
	if (remap->visitedFaces[faceIdx])
	{
		return;
	}
	remap->visitedFaces[faceIdx] = true;

	BSPFACE32& face = faces[faceIdx];

	face.iPlane = remap->planes[face.iPlane];
	face.iTextureInfo = remap->texInfo[face.iTextureInfo];
	//print_log(get_localized_string(LANG_0149),faceIdx,face.iFirstEdge,remap->surfEdges[face.iFirstEdge]);
	//print_log(get_localized_string(LANG_1025),faceIdx,face.iTextureInfo,remap->texInfo[face.iTextureInfo]);
	//face.iFirstEdge = remap->surfEdges[face.iFirstEdge];
}

void Bsp::remap_node_structures(int iNode, STRUCTREMAP* remap)
{
	if (iNode > nodeCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1026));
		return;
	}
	BSPNODE32& node = nodes[iNode];

	remap->visitedNodes[iNode] = true;

	node.iPlane = remap->planes[node.iPlane];

	for (int i = 0; i < node.nFaces; i++)
	{
		remap_face_structures(node.iFirstFace + i, remap);
	}

	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			node.iChildren[i] = remap->nodes[node.iChildren[i]];
			if (!remap->visitedNodes[node.iChildren[i]])
			{
				remap_node_structures(node.iChildren[i], remap);
			}
		}
	}
}

void Bsp::remap_clipnode_structures(int iNode, STRUCTREMAP* remap)
{
	if (iNode > clipnodeCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1027));
		return;
	}
	BSPCLIPNODE32& node = clipnodes[iNode];

	remap->visitedClipnodes[iNode] = true;
	node.iPlane = remap->planes[node.iPlane];

	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			if (node.iChildren[i] < (int)remap->count.clipnodes)
			{
				node.iChildren[i] = remap->clipnodes[node.iChildren[i]];
			}

			if (!remap->visitedClipnodes[node.iChildren[i]])
				remap_clipnode_structures(node.iChildren[i], remap);
		}
	}
}

void Bsp::remap_model_structures(int modelIdx, STRUCTREMAP* remap)
{
	if (modelIdx > modelCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1028));
		return;
	}
	BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[modelIdx];

	// sometimes the face index is invalid when the model has no faces
	if (model.nFaces > 0)
		model.iFirstFace = remap->faces[model.iFirstFace];

	if (model.iHeadnodes[0] >= 0)
	{
		model.iHeadnodes[0] = remap->nodes[model.iHeadnodes[0]];
		if (model.iHeadnodes[0] < clipnodeCount && !remap->visitedNodes[model.iHeadnodes[0]])
		{
			remap_node_structures(model.iHeadnodes[0], remap);
		}
	}
	for (int k = 1; k < MAX_MAP_HULLS; k++)
	{
		if (model.iHeadnodes[k] >= 0)
		{
			model.iHeadnodes[k] = remap->clipnodes[model.iHeadnodes[k]];
			if (model.iHeadnodes[k] < clipnodeCount && !remap->visitedClipnodes[model.iHeadnodes[k]])
			{
				remap_clipnode_structures(model.iHeadnodes[k], remap);
			}
		}
	}
}

void Bsp::delete_hull(int hull_number, int redirect)
{
	if (hull_number < 0 || hull_number >= MAX_MAP_HULLS)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0150), MAX_MAP_HULLS);
		return;
	}

	for (int i = 0; i < modelCount; i++)
	{
		delete_hull(hull_number, i, redirect);
	}
}

void Bsp::delete_hull(int hull_number, int modelIdx, int redirect)
{
	if (modelIdx < 0 || modelIdx >= modelCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0151), modelIdx);
		return;
	}

	// the first hull is used for point-sized clipping, but uses nodes and not clipnodes.
	if (hull_number < 0 || hull_number >= MAX_MAP_HULLS)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1029), MAX_MAP_HULLS);
		return;
	}

	if (redirect >= MAX_MAP_HULLS)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0152), MAX_MAP_HULLS);
		return;
	}

	if (hull_number == 0 && redirect > 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0153), MAX_MAP_HULLS);
		return;
	}

	BSPMODEL& model = models[modelIdx];

	if (hull_number == 0)
	{
		model.iHeadnodes[0] = -1; // redirect to solid leaf
		model.nVisLeafs = 0;
		model.nFaces = 0;
		model.iFirstFace = 0;
	}
	else if (redirect > 0)
	{
		if (model.iHeadnodes[hull_number] > 0 && model.iHeadnodes[redirect] < 0)
		{
			//print_log(get_localized_string(LANG_0154),redirect);
		}
		else if (model.iHeadnodes[hull_number] == model.iHeadnodes[redirect])
		{
			//print_log(get_localized_string(LANG_0155),hull_number,redirect);
		}
		model.iHeadnodes[hull_number] = model.iHeadnodes[redirect];
	}
	else
	{
		model.iHeadnodes[hull_number] = CONTENTS_EMPTY;
	}
}

void Bsp::delete_model(int modelIdx)
{
	unsigned char* oldModels = (unsigned char*)models;

	int newSize = (modelCount - 1) * sizeof(BSPMODEL);
	unsigned char* newModels = new unsigned char[newSize];

	memcpy(newModels, oldModels, modelIdx * sizeof(BSPMODEL));
	memcpy(newModels + modelIdx * sizeof(BSPMODEL),
		oldModels + (modelIdx + 1) * sizeof(BSPMODEL),
		(modelCount - (modelIdx + 1)) * sizeof(BSPMODEL));

	replace_lump(LUMP_MODELS, newModels, newSize);

	// update model index references
	for (size_t i = 0; i < ents.size(); i++)
	{
		int entModel = ents[i]->getBspModelIdx();
		if (entModel == modelIdx)
		{
			ents[i]->setOrAddKeyvalue("model", "error.mdl");
		}
		else if (entModel > modelIdx)
		{
			ents[i]->setOrAddKeyvalue("model", "*" + std::to_string(entModel - 1));
		}
	}
}

int Bsp::create_solid(const vec3& mins, const vec3& maxs, int textureIdx, bool empty)
{
	int newModelIdx = create_model();
	BSPMODEL& newModel = models[newModelIdx];


	create_node_box(mins, maxs, &newModel, textureIdx);
	create_clipnode_box(mins, maxs, &newModel, 0, false, empty);

	//remove_unused_model_structures(); // will also resize VIS data for new leaf count

	return newModelIdx;
}

int Bsp::create_solid(Solid& solid, int targetModelIdx)
{
	int modelIdx = targetModelIdx >= 0 ? targetModelIdx : create_model();
	BSPMODEL& newModel = models[modelIdx];

	create_nodes(solid, &newModel);
	regenerate_clipnodes(modelIdx, -1);

	return modelIdx;
}

void Bsp::add_model(Bsp* sourceMap, int modelIdx)
{
	STRUCTUSAGE usage(sourceMap);
	sourceMap->mark_model_structures(modelIdx, &usage, false);

	// TODO: add the model lel
	// partially done in Import->BSP Model

	usage.compute_sum();

	print_log("");
}

BSPMIPTEX* Bsp::find_embedded_texture(const char* name, int& texid)
{
	if (!name || name[0] == '\0')
		return NULL;
	for (int i = 0; i < textureCount; i++)
	{
		int oldOffset = ((int*)textures)[i + 1];
		if (oldOffset >= 0)
		{
			BSPMIPTEX* oldTex = (BSPMIPTEX*)(textures + oldOffset);
			if (oldTex->szName[0] != '\0' && strcasecmp(name, oldTex->szName) == 0 && oldTex->nOffsets[0] > 0)
			{
				texid = i;
				return oldTex;
			}
		}
	}
	return NULL;
}

BSPMIPTEX* Bsp::find_embedded_wad_texture(const char* name, int& texid)
{
	if (!name || name[0] == '\0')
		return NULL;
	for (int i = 0; i < textureCount; i++)
	{
		int oldOffset = ((int*)textures)[i + 1];
		if (oldOffset >= 0)
		{
			BSPMIPTEX* oldTex = (BSPMIPTEX*)(textures + oldOffset);
			if (oldTex->szName[0] != '\0' && strcasecmp(name, oldTex->szName) == 0 && oldTex->nOffsets[0] <= 0)
			{
				texid = i;
				return oldTex;
			}
		}
	}
	return NULL;
}

int Bsp::add_texture(const char* oldname, unsigned char* data, int width, int height, const unsigned char* custompal, bool force_quake_pal)
{
	if (!oldname || oldname[0] == '\0' || strlen(oldname) >= MAXTEXTURENAME)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0156));
		return -1;
	}
	char name[MAXTEXTURENAME];
	memset(name, 0, MAXTEXTURENAME);
	memcpy(name, oldname, std::min(MAXTEXTURENAME, (int)strlen(oldname)));

	print_log(get_localized_string(LANG_0157), !data ? "embedded" : "wad", name, width, height);

	if (width % 16 != 0 || height % 16 != 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0158));
		return -1;
	}

	if (width > (int)MAX_TEXTURE_DIMENSION || height > (int)MAX_TEXTURE_DIMENSION)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0159));
		return -1;
	}

	int oldtexid = 0;
	BSPMIPTEX* oldtex = find_embedded_texture(name, oldtexid);

	bool only_copy_data = false;

	// internal, with data
	if (oldtex)
	{
		print_log(get_localized_string(LANG_0160), name);
		if (oldtex->nWidth != width || oldtex->nHeight != height)
		{
			if (!data)
			{
				print_log(get_localized_string(LANG_0161));
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
				oldtex->nWidth = width;
				oldtex->nHeight = height;

				remove_unused_model_structures(CLEAN_TEXTURES);
				return oldtexid;
			}
			else
			{
				oldtex->szName[0] = '\0';
				print_log(PRINT_RED | PRINT_GREEN, "Warning! Texture size different {}x{} > {}x{}.\nRenaming old texture and create new one.\n",
					oldtex->nWidth, oldtex->nHeight, width, height);
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
			}
		}
		else if (data)
		{
			only_copy_data = true;
			print_log(get_localized_string(LANG_0162));
		}
		else
		{
			print_log(get_localized_string(LANG_0163));
			oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
				oldtex->nOffsets[3] = 0;

			remove_unused_model_structures(CLEAN_TEXTURES);
			return oldtexid;
		}
	}
	else
	{
		oldtexid = -1;
		oldtex = find_embedded_wad_texture(name, oldtexid);

		// external without data
		if (oldtex)
		{
			print_log(get_localized_string(LANG_0164), name);
			if (oldtex->nWidth != width || oldtex->nHeight != height)
			{
				if (!data)
				{
					print_log("Same wad texture with size different {}x{} > {}x{} found in map.\nJust update size and return index.\n",
						oldtex->nWidth, oldtex->nHeight, width, height);

					oldtex->nWidth = width;
					oldtex->nHeight = height;
					return oldtexid;
				}
				else
				{
					oldtex->szName[0] = '\0';
					print_log(PRINT_RED | PRINT_GREEN, "Warning! Texture size different {}x{} > {}x{}.\nRenaming old texture and create new one.\n",
						oldtex->nWidth, oldtex->nHeight, width, height);
					oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
						oldtex->nOffsets[3] = 0;
				}
			}
			else if (!data)
			{
				print_log(get_localized_string(LANG_0165));
				return oldtexid;
			}
			else
			{
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
				oldtex->szName[0] = '\0';
				print_log(get_localized_string(LANG_0166));
			}
		}
	}


	int texDataSize = 0;
	COLOR3 palette[256];
	memset(&palette, 0, sizeof(COLOR3) * 256);
	int colorCount = 0;


	if (is_texture_pal && !force_quake_pal)
	{
		texDataSize += width * height + sizeof(short) /* palette count */ + sizeof(COLOR3) * 256;
	}
	else
	{
		texDataSize += width * height + sizeof(short);
	}


	unsigned char* mip[MIPLEVELS] = { NULL };

	if (data)
	{
		COLOR3* src = (COLOR3*)data;

		// If custom pal || quake || force quake
		if (!is_texture_pal || force_quake_pal)
		{
			colorCount = 256;
			if (custompal)
			{
				memcpy(palette, custompal, 256 * sizeof(COLOR3));
			}
			else
			{
				memcpy(palette, g_settings.palette_data, 256 * sizeof(COLOR3));
			}
			Quantizer* tmpCQuantizer = new Quantizer(256, 8);
			tmpCQuantizer->SetColorTable(palette, 256);
			tmpCQuantizer->ApplyColorTable((COLOR3*)data, width * height);
			delete tmpCQuantizer;
			colorCount = 256;
		}

		// create pallete and full-rez mipmap
		mip[0] = new unsigned char[width * height];

		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				int paletteIdx = -1;
				for (int k = 0; k < colorCount; k++)
				{
					if (*src == palette[k])
					{
						paletteIdx = k;
						break;
					}
				}
				if (paletteIdx == -1)
				{
					if (colorCount >= 256)
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0167));
						delete[] mip[0];
						return -1;
					}
					palette[colorCount] = *src;
					paletteIdx = colorCount;
					colorCount++;
				}

				mip[0][y * width + x] = (unsigned char)paletteIdx;
				src++;
			}
		}
		// generate mipmaps
		for (int i = 1; i < MIPLEVELS; i++)
		{
			int div = 1 << i;
			int mipWidth = width / div;
			int mipHeight = height / div;
			texDataSize += mipWidth * mipHeight;
			mip[i] = new unsigned char[texDataSize];

			src = (COLOR3*)data;
			for (int y = 0; y < mipHeight; y++)
			{
				for (int x = 0; x < mipWidth; x++)
				{
					int paletteIdx = -1;
					for (int k = 0; k < colorCount; k++)
					{
						if (*src == palette[k])
						{
							paletteIdx = k;
							break;
						}
					}

					mip[i][y * mipWidth + x] = (unsigned char)paletteIdx;
					src += div;
				}
			}
		}
	}
	else
	{
		for (int i = 1; i < MIPLEVELS; i++)
		{
			int div = 1 << i;
			int mipWidth = width / div;
			int mipHeight = height / div;
			texDataSize += mipWidth * mipHeight;
		}
	}

	if (only_copy_data && oldtex)
	{
		int newTexOffset = ((int*)textures)[oldtexid + 1];

		memcpy(textures + newTexOffset + oldtex->nOffsets[0], mip[0], width * height);
		memcpy(textures + newTexOffset + oldtex->nOffsets[1], mip[1], (width >> 1) * (height >> 1));
		memcpy(textures + newTexOffset + oldtex->nOffsets[2], mip[2], (width >> 2) * (height >> 2));
		memcpy(textures + newTexOffset + oldtex->nOffsets[3], mip[3], (width >> 3) * (height >> 3));


		size_t palleteOffset = oldtex->nOffsets[3] + (width >> 3) * (height >> 3);

		if (is_texture_pal && !force_quake_pal)
		{
			palleteOffset += sizeof(short) /* pal count */;
			memcpy(textures + newTexOffset + palleteOffset, palette, sizeof(COLOR3) * 256);
		}
		// 256 palette
		(textures + newTexOffset + palleteOffset)[0] = 0x00;
		(textures + newTexOffset + palleteOffset)[1] = 0x01;

		for (int i = 0; i < MIPLEVELS; i++)
		{
			delete[] mip[i];
		}
		return oldtexid;
	}

	if (oldtex && oldtexid >= 0)
	{
		for (int i = 0; i < texinfoCount; i++)
		{
			BSPTEXTUREINFO& texinfo = texinfos[i];
			if (texinfo.iMiptex == oldtexid)
			{
				texinfo.iMiptex = textureCount;
			}
		}
	}

	size_t newTexLumpSize = bsp_header.lump[LUMP_TEXTURES].nLength + sizeof(int) + sizeof(BSPMIPTEX) + texDataSize;

	newTexLumpSize = (newTexLumpSize + 3) & ~3; /* 4 bytes lump padding for add new texture aligned to 4? */

	unsigned char* newTexData = new unsigned char[newTexLumpSize];

	memset(newTexData, 0, newTexLumpSize);

	// create new texture lump header
	int* newLumpHeader = (int*)newTexData;
	int* oldLumpHeader = (int*)lumps[LUMP_TEXTURES];
	*newLumpHeader = textureCount + 1;

	for (int i = 0; i < textureCount; i++)
	{
		if (*(oldLumpHeader + i + 1) >= 0)
			*(newLumpHeader + i + 1) = *(oldLumpHeader + i + 1) + sizeof(int); // make room for the new offset
		else
			*(newLumpHeader + i + 1) = *(oldLumpHeader + i + 1);
	}

	// copy old texture data
	size_t oldTexHeaderSize = (textureCount + 1) * sizeof(int);
	size_t newTexHeaderSize = oldTexHeaderSize + sizeof(int);
	size_t oldTexDatSize = bsp_header.lump[LUMP_TEXTURES].nLength - ((textureCount + 1) * sizeof(int));
	memcpy(newTexData + newTexHeaderSize, lumps[LUMP_TEXTURES] + oldTexHeaderSize, oldTexDatSize);

	// add new texture to the end of the lump
	size_t newTexOffset = newTexHeaderSize + oldTexDatSize;

	newLumpHeader[textureCount + 1] = (int)newTexOffset;

	BSPMIPTEX* newMipTex = (BSPMIPTEX*)(newTexData + newTexOffset);
	newMipTex->nWidth = width;
	newMipTex->nHeight = height;
	memcpy(newMipTex->szName, name, MAXTEXTURENAME);

	if (data)
	{
		newMipTex->nOffsets[0] = sizeof(BSPMIPTEX);
		newMipTex->nOffsets[1] = newMipTex->nOffsets[0] + width * height;
		newMipTex->nOffsets[2] = newMipTex->nOffsets[1] + (width >> 1) * (height >> 1);
		newMipTex->nOffsets[3] = newMipTex->nOffsets[2] + (width >> 2) * (height >> 2);

		memcpy(newTexData + newTexOffset + newMipTex->nOffsets[0], mip[0], width * height);
		memcpy(newTexData + newTexOffset + newMipTex->nOffsets[1], mip[1], (width >> 1) * (height >> 1));
		memcpy(newTexData + newTexOffset + newMipTex->nOffsets[2], mip[2], (width >> 2) * (height >> 2));
		memcpy(newTexData + newTexOffset + newMipTex->nOffsets[3], mip[3], (width >> 3) * (height >> 3));

		size_t palleteOffset = newMipTex->nOffsets[3] + (width >> 3) * (height >> 3) + sizeof(short);
		unsigned char* paletteCount = newTexData + newTexOffset + (palleteOffset - sizeof(short));

		if (is_texture_pal && !force_quake_pal)
		{
			memcpy(newTexData + newTexOffset + palleteOffset, palette, sizeof(COLOR3) * 256);
		}
		// 256 palette
		paletteCount[0] = 0x00;
		paletteCount[1] = 0x01;

		for (int i = 0; i < MIPLEVELS; i++)
		{
			delete[] mip[i];
		}
	}

	replace_lump(LUMP_TEXTURES, newTexData, newTexLumpSize);
	update_lump_pointers();
	return textureCount - 1;
}

int Bsp::add_texture(WADTEX* tex)
{
	//print_log(get_localized_string(LANG_0168),tex->szName,tex->nWidth,tex->nHeight,tex->nOffsets[0],tex->nOffsets[1],tex->nOffsets[2],tex->nOffsets[3]);
	print_log(get_localized_string(LANG_0169), tex->szName, tex->nWidth, tex->nHeight);

	if (tex->nWidth % 16 != 0 || tex->nHeight % 16 != 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1030));
		return -1;
	}

	if (tex->nWidth > (int)MAX_TEXTURE_DIMENSION || tex->nHeight > (int)MAX_TEXTURE_DIMENSION)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1031));
		return -1;
	}

	int oldtexid = -1;
	BSPMIPTEX* oldtex = find_embedded_texture(tex->szName, oldtexid);

	bool only_copy_data = false;

	if (oldtex)
	{
		print_log(get_localized_string(LANG_1032), tex->szName);
		if (oldtex->nWidth != tex->nWidth || oldtex->nHeight != tex->nHeight)
		{
			oldtex->szName[0] = '\0';
			print_log(PRINT_RED | PRINT_GREEN, "Warning! Texture size different {}x{} > {}x{}.\nRenaming old texture and create new one.\n",
				oldtex->nWidth, oldtex->nHeight, tex->nWidth, tex->nHeight);
			oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
				oldtex->nOffsets[3] = 0;
		}
		else
		{
			only_copy_data = true;
			print_log(get_localized_string(LANG_1033));
		}
	}
	else
	{
		oldtexid = -1;
		oldtex = find_embedded_wad_texture(tex->szName, oldtexid);

		// external without data
		if (oldtex)
		{
			print_log(get_localized_string(LANG_1034), tex->szName);
			if (oldtex->nWidth != tex->nWidth || oldtex->nHeight != tex->nHeight)
			{
				print_log(PRINT_RED | PRINT_GREEN, "Warning! Texture size different {}x{} > {}x{}.\nRenaming old texture and create new one.\n",
					oldtex->nWidth, oldtex->nHeight, tex->nWidth, tex->nHeight);
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
				oldtex->szName[0] = '\0';
			}
			else
			{
				print_log(get_localized_string(LANG_1035));
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
				oldtex->szName[0] = '\0';
			}
		}
	}

	if (only_copy_data)
	{
		int newTexOffset = ((int*)textures)[oldtexid + 1];
		int w = tex->nWidth;
		int h = tex->nHeight;
		int sz = w * h;	   // miptex 0
		int sz2 = sz / 4;  // miptex 1
		int sz3 = sz2 / 4; // miptex 2
		int sz4 = sz3 / 4; // miptex 3
		int szAll = sz + sz2 + sz3 + sz4 + sizeof(short) + /*pal size*/ sizeof(COLOR3) * 256;
		memcpy(textures + newTexOffset + sizeof(BSPMIPTEX), tex->data, szAll);

		return oldtexid;
	}

	if (oldtex && oldtexid >= 0)
	{
		int tex_usage = 0;
		for (int i = 0; i < texinfoCount; i++)
		{
			BSPTEXTUREINFO& texinfo = texinfos[i];
			if (texinfo.iMiptex == oldtexid)
			{
				texinfo.iMiptex = textureCount;
				tex_usage++;
			}
		}
		if (tex_usage > 0)
		{
			print_log(get_localized_string(LANG_0170), tex_usage);
		}
	}

	int w = tex->nWidth;
	int h = tex->nHeight;
	int sz = w * h;	   // miptex 0
	int sz2 = sz / 4;  // miptex 1
	int sz3 = sz2 / 4; // miptex 2
	int sz4 = sz3 / 4; // miptex 3
	int szAll = sz + sz2 + sz3 + sz4 + sizeof(short) + /*pal size*/ sizeof(COLOR3) * 256;

	size_t newTexLumpSize = bsp_header.lump[LUMP_TEXTURES].nLength + sizeof(int) + sizeof(BSPMIPTEX) + szAll;

	newTexLumpSize = (newTexLumpSize + 3) & ~3; /* 4 bytes lump padding for add new texture aligned to 4? */

	unsigned char* newTexData = new unsigned char[newTexLumpSize];
	memset(newTexData, 0, newTexLumpSize);

	// create new texture lump header
	int* newLumpHeader = (int*)newTexData;
	int* oldLumpHeader = (int*)lumps[LUMP_TEXTURES];
	*newLumpHeader = textureCount + 1;

	for (int i = 0; i < textureCount; i++)
	{
		if (*(oldLumpHeader + i + 1) >= 0)
			*(newLumpHeader + i + 1) = *(oldLumpHeader + i + 1) + sizeof(int); // make room for the new offset
		else
			*(newLumpHeader + i + 1) = *(oldLumpHeader + i + 1);
	}

	// copy old texture data
	size_t oldTexHeaderSize = (textureCount + 1) * sizeof(int);
	size_t newTexHeaderSize = oldTexHeaderSize + sizeof(int);
	size_t oldTexDatSize = bsp_header.lump[LUMP_TEXTURES].nLength - ((textureCount + 1) * sizeof(int));
	memcpy(newTexData + newTexHeaderSize, lumps[LUMP_TEXTURES] + oldTexHeaderSize, oldTexDatSize);

	// add new texture to the end of the lump
	size_t newTexOffset = newTexHeaderSize + oldTexDatSize;

	newLumpHeader[textureCount + 1] = (int)newTexOffset;

	memcpy(newTexData + newTexOffset, tex, sizeof(BSPMIPTEX));
	memcpy(newTexData + newTexOffset + sizeof(BSPMIPTEX), tex->data, szAll);

	// 256 palette
	((unsigned char*)newTexData + newTexOffset)[sizeof(BSPMIPTEX) + sz + sz2 + sz3 + sz4] = 0x00;
	((unsigned char*)newTexData + newTexOffset)[sizeof(BSPMIPTEX) + sz + sz2 + sz3 + sz4 + 1] = 0x01;


	replace_lump(LUMP_TEXTURES, newTexData, newTexLumpSize);
	update_lump_pointers();
	return textureCount - 1;
}

int Bsp::create_leaf(int contents)
{
	BSPLEAF32* newLeaves = new BSPLEAF32[leafCount + 1]{};
	memcpy(newLeaves, leaves, leafCount * sizeof(BSPLEAF32));

	BSPLEAF32& newLeaf = newLeaves[leafCount];

	newLeaf.nVisOffset = -1;
	newLeaf.nContents = contents;

	unsigned int newLeafIdx = leafCount;

	replace_lump(LUMP_LEAVES, newLeaves, (leafCount + 1) * sizeof(BSPLEAF32));

	return newLeafIdx;
}

void Bsp::create_node_box(const vec3& min, const vec3& max, BSPMODEL* targetModel, int textureIdx)
{

	// add new verts (1 for each corner)
	// TODO: subdivide faces to prevent max surface extents error
	unsigned int startVert = vertCount;
	{
		vec3* newVerts = new vec3[vertCount + 8];
		memcpy(newVerts, verts, vertCount * sizeof(vec3));

		newVerts[vertCount + 0] = vec3(min.x, min.y, min.z); // front-left-bottom
		newVerts[vertCount + 1] = vec3(max.x, min.y, min.z); // front-right-bottom
		newVerts[vertCount + 2] = vec3(max.x, max.y, min.z); // back-right-bottom
		newVerts[vertCount + 3] = vec3(min.x, max.y, min.z); // back-left-bottom

		newVerts[vertCount + 4] = vec3(min.x, min.y, max.z); // front-left-top
		newVerts[vertCount + 5] = vec3(max.x, min.y, max.z); // front-right-top
		newVerts[vertCount + 6] = vec3(max.x, max.y, max.z); // back-right-top
		newVerts[vertCount + 7] = vec3(min.x, max.y, max.z); // back-left-top

		replace_lump(LUMP_VERTICES, newVerts, (vertCount + 8) * sizeof(vec3));
	}

	// add new edges (4 for each face)
	// TODO: subdivide >512
	unsigned int startEdge = edgeCount;
	{
		BSPEDGE32* newEdges = new BSPEDGE32[edgeCount + 12];
		memcpy(newEdges, edges, edgeCount * sizeof(BSPEDGE32));

		// left
		newEdges[startEdge + 0] = BSPEDGE32(startVert + 3, startVert + 0);
		newEdges[startEdge + 1] = BSPEDGE32(startVert + 4, startVert + 7);

		// right
		newEdges[startEdge + 2] = BSPEDGE32(startVert + 1, startVert + 2); // bottom edge
		newEdges[startEdge + 3] = BSPEDGE32(startVert + 6, startVert + 5); // right edge

		// front
		newEdges[startEdge + 4] = BSPEDGE32(startVert + 0, startVert + 1); // bottom edge
		newEdges[startEdge + 5] = BSPEDGE32(startVert + 5, startVert + 4); // top edge

		// back
		newEdges[startEdge + 6] = BSPEDGE32(startVert + 3, startVert + 7); // left edge
		newEdges[startEdge + 7] = BSPEDGE32(startVert + 6, startVert + 2); // right edge

		// bottom
		newEdges[startEdge + 8] = BSPEDGE32(startVert + 3, startVert + 2);
		newEdges[startEdge + 9] = BSPEDGE32(startVert + 1, startVert + 0);

		// top
		newEdges[startEdge + 10] = BSPEDGE32(startVert + 7, startVert + 4);
		newEdges[startEdge + 11] = BSPEDGE32(startVert + 5, startVert + 6);

		replace_lump(LUMP_EDGES, newEdges, (edgeCount + 12) * sizeof(BSPEDGE32));
	}

	// add new surfedges (2 for each edge)
	unsigned int startSurfedge = surfedgeCount;
	{
		int* newSurfedges = new int[surfedgeCount + 24];
		memcpy(newSurfedges, surfedges, surfedgeCount * sizeof(int));

		// reverse cuz i fucked the edge order and I don't wanna redo
		for (int i = 12 - 1; i >= 0; i--)
		{
			int edgeIdx = startEdge + i;
			newSurfedges[startSurfedge + (i * 2)] = -edgeIdx; // negative = use second vertex in edge
			newSurfedges[startSurfedge + (i * 2) + 1] = edgeIdx;
		}

		replace_lump(LUMP_SURFEDGES, newSurfedges, (surfedgeCount + 24) * sizeof(int));
	}

	// add new planes (1 for each face/node)
	unsigned int startPlane = planeCount;
	{
		BSPPLANE* newPlanes = new BSPPLANE[planeCount + 6];
		memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

		newPlanes[startPlane + 0] = { vec3(1.0f, 0.0f, 0.0f), min.x, PLANE_X }; // left
		newPlanes[startPlane + 1] = { vec3(1.0f, 0.0f, 0.0f), max.x, PLANE_X }; // right
		newPlanes[startPlane + 2] = { vec3(0.0f, 1.0f, 0.0f), min.y, PLANE_Y }; // front
		newPlanes[startPlane + 3] = { vec3(0.0f, 1.0f, 0.0f), max.y, PLANE_Y }; // back
		newPlanes[startPlane + 4] = { vec3(0.0f, 0.0f, 1.0f), min.z, PLANE_Z }; // bottom
		newPlanes[startPlane + 5] = { vec3(0.0f, 0.0f, 1.0f), max.z, PLANE_Z }; // top

		replace_lump(LUMP_PLANES, newPlanes, (planeCount + 6) * sizeof(BSPPLANE));
	}

	unsigned int startTexinfo = texinfoCount;
	{
		BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[texinfoCount + 6];
		memcpy(newTexinfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

		vec3 up = vec3(0, 0, 1);
		vec3 right = vec3(1, 0, 0);
		vec3 forward = vec3(0, 1, 0);

		vec3 faceNormals[6]{
			vec3(-1, 0, 0),	// left
			vec3(1, 0, 0), // right
			vec3(0, 1, 0), // front
			vec3(0, -1, 0), // back
			vec3(0, 0, -1), // bottom
			vec3(0, 0, 1) // top
		};
		vec3 faceUp[6]{
			vec3(0, 0, -1),	// left
			vec3(0, 0, -1), // right
			vec3(0, 0, -1), // front
			vec3(0, 0, -1), // back
			vec3(0, -1, 0), // bottom
			vec3(0, 1, 0) // top
		};

		for (int i = 0; i < 6; i++)
		{
			BSPTEXTUREINFO& info = newTexinfos[startTexinfo + i];
			info.iMiptex = textureIdx;
			info.nFlags = TEX_SPECIAL;
			info.shiftS = 0;
			info.shiftT = 0;
			info.vT = faceUp[i];
			info.vS = crossProduct(faceUp[i], faceNormals[i]);
			// TODO: fit texture to face

		}

		replace_lump(LUMP_TEXINFO, newTexinfos, (texinfoCount + 6) * sizeof(BSPTEXTUREINFO));
	}

	// add new faces
	unsigned int startFace = faceCount;
	{
		BSPFACE32* newFaces = new BSPFACE32[faceCount + 6];
		memcpy(newFaces, faces, faceCount * sizeof(BSPFACE32));

		for (int i = 0; i < 6; i++)
		{
			BSPFACE32& face = newFaces[faceCount + i];
			face.iFirstEdge = startSurfedge + i * 4;
			face.iPlane = (startPlane + i);
			face.nEdges = 4;
			face.nPlaneSide = i % 2 == 0; // even-numbered planes are inverted
			face.iTextureInfo = (startTexinfo + i);
			face.nLightmapOffset = 0; // TODO: Lighting
			memset(face.nStyles, 255, MAX_LIGHTMAPS);
		}

		replace_lump(LUMP_FACES, newFaces, (faceCount + 6) * sizeof(BSPFACE32));
	}

	// Submodels don't use leaves like the world does. Everything except nContents is ignored.
	// There's really no need to create leaves for submodels. Every map will have a shared
	// SOLID leaf, and there should be at least one EMPTY leaf if the map isn't completely solid.
	// So, just find an existing EMPTY leaf. Also, water brushes work just fine with SOLID nodes.
	// The inner contents of a node is changed dynamically by entity properties.
	int sharedSolidLeaf = 0;
	int anyEmptyLeaf = 0;
	for (int i = 0; i < leafCount; i++)
	{
		if (leaves[i].nContents == CONTENTS_EMPTY)
		{
			anyEmptyLeaf = i;
			break;
		}
	}
	// If emptyLeaf is still 0 (SOLID), it means the map is fully solid, so the contents wouldn't matter.
	// Anyway, still setting this in case someone wants to copy the model to another map
	if (anyEmptyLeaf == 0)
	{
		anyEmptyLeaf = create_leaf(CONTENTS_EMPTY);
		targetModel->nVisLeafs = 1;
	}
	else
	{
		targetModel->nVisLeafs = 0;
	}

	// add new nodes
	unsigned int startNode = nodeCount;
	{
		BSPNODE32* newNodes = new BSPNODE32[nodeCount + 6]{};
		memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE32));
		for (int k = 0; k < 6; k++)
		{
			BSPNODE32& node = newNodes[nodeCount + k];

			node.iFirstFace = (startFace + k); // face required for decals
			node.nFaces = 1;
			node.iPlane = startPlane + k;
			// node mins/maxs don't matter for submodels. Leave them at 0.

			int insideContents = k == 5 ? ~sharedSolidLeaf : (nodeCount + k + 1);
			int outsideContents = ~anyEmptyLeaf;

			// can't have negative normals on planes so children are swapped instead
			if (k % 2 == 0)
			{
				node.iChildren[0] = insideContents;
				node.iChildren[1] = outsideContents;
			}
			else
			{
				node.iChildren[0] = outsideContents;
				node.iChildren[1] = insideContents;
			}
		}

		replace_lump(LUMP_NODES, newNodes, (nodeCount + 6) * sizeof(BSPNODE32));
	}

	targetModel->iHeadnodes[0] = startNode;
	targetModel->iFirstFace = startFace;
	targetModel->nFaces = 6;

	targetModel->nMaxs = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);
	targetModel->nMins = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
	for (int i = 0; i < 8; i++)
	{
		vec3 v = verts[startVert + i];

		if (v.x > targetModel->nMaxs.x) targetModel->nMaxs.x = v.x;
		if (v.y > targetModel->nMaxs.y) targetModel->nMaxs.y = v.y;
		if (v.z > targetModel->nMaxs.z) targetModel->nMaxs.z = v.z;

		if (v.x < targetModel->nMins.x) targetModel->nMins.x = v.x;
		if (v.y < targetModel->nMins.y) targetModel->nMins.y = v.y;
		if (v.z < targetModel->nMins.z) targetModel->nMins.z = v.z;
	}
}

void Bsp::create_nodes(Solid& solid, BSPMODEL* targetModel)
{
	std::vector<int> newVertIndexes;
	unsigned int startVert = vertCount;
	{
		vec3* newVerts = new vec3[vertCount + solid.hullVerts.size()];
		memcpy(newVerts, verts, vertCount * sizeof(vec3));

		for (unsigned int i = 0; i < solid.hullVerts.size(); i++)
		{
			newVerts[vertCount + i] = solid.hullVerts[i].pos;
			newVertIndexes.push_back(vertCount + i);
		}

		replace_lump(LUMP_VERTICES, newVerts, (vertCount + solid.hullVerts.size()) * sizeof(vec3));
	}

	// add new edges (not actually edges - just an indirection layer for the verts)
	// TODO: subdivide >512
	unsigned int startEdge = edgeCount;
	std::map<int, int> vertToSurfedge;
	{
		size_t addEdges = (solid.hullVerts.size() + 1) / 2;

		BSPEDGE32* newEdges = new BSPEDGE32[edgeCount + addEdges];
		memcpy(newEdges, edges, edgeCount * sizeof(BSPEDGE32));

		unsigned int idx = 0;
		for (unsigned int i = 0; i < solid.hullVerts.size(); i += 2)
		{
			unsigned int v0 = i;
			unsigned int v1 = (i + 1) % solid.hullVerts.size();
			newEdges[startEdge + idx] = BSPEDGE32((unsigned int)newVertIndexes[v0], (unsigned int)newVertIndexes[v1]);

			vertToSurfedge[v0] = startEdge + idx;
			if (v1 > 0)
			{
				vertToSurfedge[v1] = -((int)(startEdge + idx)); // negative = use second vert
			}

			idx++;
		}
		replace_lump(LUMP_EDGES, newEdges, (edgeCount + addEdges) * sizeof(BSPEDGE32));
	}

	// add new surfedges (2 for each edge)
	unsigned int startSurfedge = surfedgeCount;
	{
		size_t addSurfedges = 0;
		for (size_t i = 0; i < solid.faces.size(); i++)
		{
			addSurfedges += solid.faces[i].verts.size();
		}

		int* newSurfedges = new int[surfedgeCount + addSurfedges];
		memcpy(newSurfedges, surfedges, surfedgeCount * sizeof(int));

		unsigned int idx = 0;
		for (unsigned int i = 0; i < solid.faces.size(); i++)
		{
			auto& tmpFace = solid.faces[i];
			for (unsigned int k = 0; k < tmpFace.verts.size(); k++)
			{
				newSurfedges[startSurfedge + idx++] = (int)vertToSurfedge[(int)tmpFace.verts[k]];
			}
		}

		replace_lump(LUMP_SURFEDGES, newSurfedges, (surfedgeCount + addSurfedges) * sizeof(int));
	}

	// add new planes (1 for each face/node)
	// TODO: reuse existing planes (maybe not until shared stuff can be split when editing solids)
	unsigned int startPlane = planeCount;
	{
		BSPPLANE* newPlanes = new BSPPLANE[planeCount + solid.faces.size()];
		memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

		for (unsigned int i = 0; i < solid.faces.size(); i++)
		{
			newPlanes[startPlane + i] = solid.faces[i].plane;
		}

		replace_lump(LUMP_PLANES, newPlanes, (planeCount + solid.faces.size()) * sizeof(BSPPLANE));
	}

	// add new faces
	unsigned int startFace = faceCount;
	{
		BSPFACE32* newFaces = new BSPFACE32[faceCount + solid.faces.size()];
		memcpy(newFaces, faces, faceCount * sizeof(BSPFACE32));

		unsigned int surfedgeOffset = 0;
		for (unsigned int i = 0; i < solid.faces.size(); i++)
		{
			BSPFACE32& face = newFaces[faceCount + i];
			face.iFirstEdge = (int)(startSurfedge + surfedgeOffset);
			face.iPlane = (startPlane + i);
			face.nEdges = (int)solid.faces[i].verts.size();
			face.nPlaneSide = solid.faces[i].planeSide;
			face.iTextureInfo = solid.faces[i].iTextureInfo;
			face.nLightmapOffset = 0; // TODO: Lighting
			memset(face.nStyles, 255, MAX_LIGHTMAPS);
			surfedgeOffset += face.nEdges;
		}

		replace_lump(LUMP_FACES, newFaces, (faceCount + solid.faces.size()) * sizeof(BSPFACE32));
	}

	//TODO: move to common function
	int sharedSolidLeaf = 0;
	int anyEmptyLeaf = 0;
	for (int i = 0; i < leafCount; i++)
	{
		if (leaves[i].nContents == CONTENTS_EMPTY)
		{
			anyEmptyLeaf = i;
			break;
		}
	}
	if (anyEmptyLeaf == 0)
	{
		anyEmptyLeaf = create_leaf(CONTENTS_EMPTY);
		targetModel->nVisLeafs = 1;
	}
	else
	{
		targetModel->nVisLeafs = 0;
	}

	// add new nodes
	unsigned int startNode = nodeCount;
	{
		BSPNODE32* newNodes = new BSPNODE32[nodeCount + solid.faces.size() + 1]{};
		memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE32));

		for (size_t k = 0; k < solid.faces.size(); k++)
		{
			BSPNODE32& node = newNodes[nodeCount + k];

			node.iFirstFace = (int)(startFace + k); // face required for decals
			node.nFaces = 1;
			node.iPlane = (int)(startPlane + k);
			// node mins/maxs don't matter for submodels. Leave them at 0.

			int insideContents = solid.faces.size() && k == solid.faces.size() - 1 ? ~sharedSolidLeaf : (int)(nodeCount + k + 1);
			int outsideContents = ~anyEmptyLeaf;

			// can't have negative normals on planes so children are swapped instead
			if (solid.faces[k].planeSide)
			{
				node.iChildren[0] = insideContents;
				node.iChildren[1] = outsideContents;
			}
			else
			{
				node.iChildren[0] = outsideContents;
				node.iChildren[1] = insideContents;
			}
		}

		replace_lump(LUMP_NODES, newNodes, (nodeCount + solid.faces.size()) * sizeof(BSPNODE32));
	}

	targetModel->iHeadnodes[0] = startNode;
	targetModel->iHeadnodes[1] = CONTENTS_EMPTY;
	targetModel->iHeadnodes[2] = CONTENTS_EMPTY;
	targetModel->iHeadnodes[3] = CONTENTS_EMPTY;
	targetModel->iFirstFace = startFace;
	targetModel->nFaces = (int)solid.faces.size();

	targetModel->nMaxs = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);
	targetModel->nMins = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
	for (size_t i = 0; i < solid.hullVerts.size(); i++)
	{
		vec3 v = verts[startVert + i];
		expandBoundingBox(v, targetModel->nMins, targetModel->nMaxs);
	}
}

int Bsp::create_clipnode_box(const vec3& mins, const vec3& maxs, BSPMODEL* targetModel, int targetHull, bool skipEmpty, bool empty)
{
	std::vector<BSPPLANE> addPlanes;
	std::vector<BSPCLIPNODE32> addNodes;
	int solidNodeIdx = 0;

	for (int i = 1; i < MAX_MAP_HULLS; i++)
	{
		if (skipEmpty && targetModel->iHeadnodes[i] < 0)
		{
			continue;
		}
		if (targetHull > 0 && i != targetHull)
		{
			continue;
		}

		vec3 min = mins - default_hull_extents[i];
		vec3 max = maxs + default_hull_extents[i];

		int clipnodeIdx = clipnodeCount + (int)addNodes.size();
		int planeIdx = planeCount + (int)addPlanes.size();

		addPlanes.push_back({ vec3(1.0f, 0.0f, 0.0f), min.x, PLANE_X }); // left
		addPlanes.push_back({ vec3(1.0f, 0.0f, 0.0f), max.x, PLANE_X }); // right
		addPlanes.push_back({ vec3(0.0f, 1.0f, 0.0f), min.y, PLANE_Y }); // front
		addPlanes.push_back({ vec3(0.0f, 1.0f, 0.0f), max.y, PLANE_Y }); // back
		addPlanes.push_back({ vec3(0.0f, 0.0f, 1.0f), min.z, PLANE_Z }); // bottom
		addPlanes.push_back({ vec3(0.0f, 0.0f, 1.0f), max.z, PLANE_Z }); // top

		targetModel->iHeadnodes[i] = clipnodeCount + (int)addNodes.size();

		for (int k = 0; k < 6; k++)
		{
			BSPCLIPNODE32 node = BSPCLIPNODE32();
			node.iPlane = (int)planeIdx++;


			int insideContents = k == 5 ? CONTENTS_SOLID : clipnodeIdx + 1;

			if (insideContents == CONTENTS_SOLID)
				solidNodeIdx = clipnodeIdx;

			clipnodeIdx++;

			// can't have negative normals on planes so children are swapped instead
			if (k % 2 == 0)
			{
				node.iChildren[0] = empty ? CONTENTS_EMPTY : insideContents;
				node.iChildren[1] = CONTENTS_EMPTY;
			}
			else
			{
				node.iChildren[0] = CONTENTS_EMPTY;
				node.iChildren[1] = empty ? CONTENTS_EMPTY : insideContents;
			}

			addNodes.push_back(node);
		}
	}

	BSPPLANE* newPlanes = new BSPPLANE[planeCount + addPlanes.size()];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));
	if (addPlanes.size())
		std::copy(addPlanes.begin(), addPlanes.end(), newPlanes + planeCount);
	replace_lump(LUMP_PLANES, newPlanes, (planeCount + addPlanes.size()) * sizeof(BSPPLANE));

	BSPCLIPNODE32* newClipnodes = new BSPCLIPNODE32[clipnodeCount + addNodes.size()];
	memcpy(newClipnodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE32));
	if (addNodes.size())
		std::copy(addNodes.begin(), addNodes.end(), newClipnodes + clipnodeCount);
	replace_lump(LUMP_CLIPNODES, newClipnodes, (clipnodeCount + addNodes.size()) * sizeof(BSPCLIPNODE32));

	return solidNodeIdx;
}

void Bsp::simplify_model_collision(int modelIdx, int hullIdx)
{
	if (modelIdx < 0 || modelIdx >= modelCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1036), modelIdx);
		return;
	}
	if (hullIdx >= MAX_MAP_HULLS)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1146), MAX_MAP_HULLS);
		return;
	}

	BSPMODEL& model = models[modelIdx];

	if (model.iHeadnodes[1] < 0 && model.iHeadnodes[2] < 0 && model.iHeadnodes[3] < 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0171));
		return;
	}

	if (hullIdx > 0 && model.iHeadnodes[hullIdx] < 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0172), hullIdx);
		return;
	}

	if (model.iHeadnodes[0] < 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0173));
		// TODO: create verts from plane intersections
		return;
	}

	vec3 vertMin(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
	vec3 vertMax(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);
	get_model_vertex_bounds(modelIdx, vertMin, vertMax);

	create_clipnode_box(vertMin, vertMax, &model, hullIdx, true);
}

int Bsp::create_clipnode()
{
	BSPCLIPNODE32* newNodes = new BSPCLIPNODE32[clipnodeCount + 1];
	memcpy(newNodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE32));

	BSPCLIPNODE32* newNode = &newNodes[clipnodeCount];
	memset(newNode, 0, sizeof(BSPCLIPNODE32));

	replace_lump(LUMP_CLIPNODES, newNodes, (clipnodeCount + 1) * sizeof(BSPCLIPNODE32));

	return clipnodeCount - 1;
}

int Bsp::create_plane()
{
	BSPPLANE* newPlanes = new BSPPLANE[planeCount + 1]{};
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

	newPlanes[planeCount] = BSPPLANE();
	//BSPPLANE& newPlane = newPlanes[planeCount];

	replace_lump(LUMP_PLANES, newPlanes, (planeCount + 1) * sizeof(BSPPLANE));

	return planeCount - 1;
}

int Bsp::create_model()
{
	BSPMODEL* newModels = new BSPMODEL[modelCount + 1]{};
	memcpy(newModels, models, modelCount * sizeof(BSPMODEL));

	newModels[modelCount] = BSPMODEL();
	//BSPMODEL& newModel = newModels[modelCount];

	replace_lump(LUMP_MODELS, newModels, (modelCount + 1) * sizeof(BSPMODEL));

	return modelCount - 1;
}

int Bsp::create_texinfo()
{
	BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[texinfoCount + 1]{};
	memcpy(newTexinfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

	newTexinfos[texinfoCount] = BSPTEXTUREINFO();

	replace_lump(LUMP_TEXINFO, newTexinfos, (texinfoCount + 1) * sizeof(BSPTEXTUREINFO));

	return texinfoCount - 1;
}

void Bsp::copy_bsp_model(int modelIdx, Bsp* targetMap, STRUCTREMAP& remap, std::vector<BSPPLANE>& newPlanes, std::vector<vec3>& newVerts,
	std::vector<BSPEDGE32>& newEdges, std::vector<int>& newSurfedges, std::vector<BSPTEXTUREINFO>& newTexinfo,
	std::vector<BSPFACE32>& newFaces, std::vector<COLOR3>& newLightmaps, std::vector<BSPNODE32>& newNodes,
	std::vector<BSPCLIPNODE32>& newClipnodes, std::vector<WADTEX*>& newTextures)
{
	STRUCTUSAGE usage(this);
	mark_model_structures(modelIdx, &usage, true);

	for (unsigned int i = 0; i < usage.count.planes; i++)
	{
		if (usage.planes[i])
		{
			remap.planes[i] = targetMap->planeCount + (int)newPlanes.size();
			newPlanes.push_back(this->planes[i]);
		}
	}

	for (unsigned int i = 0; i < usage.count.verts; i++)
	{
		if (usage.verts[i])
		{
			remap.verts[i] = targetMap->vertCount + (int)newVerts.size();
			newVerts.push_back(this->verts[i]);
		}
	}

	for (unsigned int i = 0; i < usage.count.edges; i++)
	{
		if (usage.edges[i])
		{
			remap.edges[i] = targetMap->edgeCount + (int)newEdges.size();

			BSPEDGE32 edge = this->edges[i];
			for (int k = 0; k < 2; k++)
				edge.iVertex[k] = remap.verts[edge.iVertex[k]];
			newEdges.push_back(edge);
		}
	}

	for (unsigned int i = 0; i < usage.count.surfEdges; i++)
	{
		if (usage.surfEdges[i])
		{
			remap.surfEdges[i] = targetMap->surfedgeCount + (int)newSurfedges.size();

			int surfedge = remap.edges[abs(this->surfedges[i])];
			if (surfedges[i] < 0)
				surfedge = -surfedge;
			newSurfedges.push_back(surfedge);
		}
	}

	// copy src map textures for adding to new
	std::set<int> usedmips;

	for (int i = 0; i < this->texinfoCount; i++)
	{
		BSPTEXTUREINFO& texinfo = this->texinfos[i];
		if (texinfo.iMiptex >= 0 && texinfo.iMiptex < this->textureCount && !usedmips.count(texinfo.iMiptex))
		{
			int texOffset = ((int*)this->textures)[texinfo.iMiptex + 1];
			if (texOffset >= 0)
			{
				usedmips.insert(texinfo.iMiptex);
				BSPMIPTEX* tex = ((BSPMIPTEX*)(this->textures + texOffset));
				WADTEX* newTex = new WADTEX(tex);
				newTextures.push_back(newTex);
			}
		}
	}

	for (unsigned int i = 0; i < usage.count.texInfos; i++)
	{
		if (usage.texInfo[i])
		{
			remap.texInfo[i] = targetMap->texinfoCount + (int)newTexinfo.size();
			newTexinfo.push_back(this->texinfos[i]);
		}
	}

	int lightmapAppendSz = 0;
	for (unsigned int i = 0; i < usage.count.faces; i++)
	{
		if (usage.faces[i])
		{
			remap.faces[i] = targetMap->faceCount + (int)newFaces.size();

			BSPFACE32 face = faces[i];
			face.iFirstEdge = remap.surfEdges[face.iFirstEdge];
			face.iPlane = remap.planes[face.iPlane];
			face.iTextureInfo = remap.texInfo[face.iTextureInfo];

			// TODO: Check if face even has lighting
			int size[2];
			GetFaceLightmapSize(this, i, size);

			int lightmapCount = lightmap_count(i);

			int lightmapSz = size[0] * size[1] * lightmapCount;

			if (face.nLightmapOffset >= 0 && lightmapCount > 0)
			{
				COLOR3* lightmapSrc = (COLOR3*)(this->lightdata + face.nLightmapOffset);
				for (int k = 0; k < lightmapSz; k++)
				{
					newLightmaps.push_back(lightmapSrc[k]);
				}

				face.nLightmapOffset = targetMap->lightDataLength + lightmapAppendSz;
				if (face.nLightmapOffset < 0)
				{
					memset(face.nStyles, 255, MAX_LIGHTMAPS);
				}
			}

			newFaces.push_back(face);

			lightmapAppendSz += lightmapSz * sizeof(COLOR3);
		}
	}

	for (unsigned int i = 0; i < usage.count.nodes; i++)
	{
		if (usage.nodes[i])
		{
			remap.nodes[i] = targetMap->nodeCount + (int)newNodes.size();
			newNodes.push_back(this->nodes[i]);
		}
	}

	for (size_t i = 0; i < newNodes.size(); i++)
	{
		BSPNODE32& node = newNodes[i];
		node.iFirstFace = remap.faces[node.iFirstFace];
		node.iPlane = remap.planes[node.iPlane];

		for (int k = 0; k < 2; k++)
		{
			if (node.iChildren[k] > 0)
			{
				node.iChildren[k] = remap.nodes[node.iChildren[k]];
			}
		}
	}

	for (unsigned int i = 0; i < usage.count.clipnodes; i++)
	{
		if (usage.clipnodes[i])
		{
			remap.clipnodes[i] = targetMap->clipnodeCount + (int)newClipnodes.size();
			newClipnodes.push_back(this->clipnodes[i]);
		}
	}

	for (size_t i = 0; i < newClipnodes.size(); i++)
	{
		BSPCLIPNODE32& clipnode = newClipnodes[i];
		clipnode.iPlane = remap.planes[clipnode.iPlane];

		for (int k = 0; k < 2; k++)
		{
			if (clipnode.iChildren[k] > 0)
			{
				clipnode.iChildren[k] = remap.clipnodes[clipnode.iChildren[k]];
			}
		}
	}
}

void Bsp::duplicate_model_structures(int modelIdx)
{
	std::vector<BSPPLANE> newPlanes;
	std::vector<vec3> newVerts;
	std::vector<BSPEDGE32> newEdges;
	std::vector<int> newSurfedges;
	std::vector<BSPTEXTUREINFO> newTexinfo;
	std::vector<BSPFACE32> newFaces;
	std::vector<COLOR3> newLightmaps;
	std::vector<BSPNODE32> newNodes;
	std::vector<BSPCLIPNODE32> newClipnodes;
	std::vector<WADTEX*> newTextures;

	STRUCTREMAP remap(this);
	copy_bsp_model(modelIdx, this, remap, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces, newLightmaps, newNodes, newClipnodes, newTextures);
	for (auto& s : newTextures)
	{
		delete s;
	}

	if (newClipnodes.size())
		append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE32) * newClipnodes.size());
	if (newEdges.size())
		append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE32) * newEdges.size());
	if (newFaces.size())
	{
		append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE32) * newFaces.size());
	}
	if (newNodes.size())
		append_lump(LUMP_NODES, &newNodes[0], sizeof(BSPNODE32) * newNodes.size());
	if (newPlanes.size())
		append_lump(LUMP_PLANES, &newPlanes[0], sizeof(BSPPLANE) * newPlanes.size());
	if (newSurfedges.size())
		append_lump(LUMP_SURFEDGES, &newSurfedges[0], sizeof(int) * newSurfedges.size());
	if (newTexinfo.size())
		append_lump(LUMP_TEXINFO, &newTexinfo[0], sizeof(BSPTEXTUREINFO) * newTexinfo.size());
	if (newVerts.size())
		append_lump(LUMP_VERTICES, &newVerts[0], sizeof(vec3) * newVerts.size());
	if (newLightmaps.size())
	{
		append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());
		save_undo_lightmaps();
		renderer->loadLightmaps();
	}

	BSPMODEL& oldModel = models[modelIdx];
	oldModel.iFirstFace = remap.faces[oldModel.iFirstFace];
	oldModel.iHeadnodes[0] = oldModel.iHeadnodes[0] < 0 ? -1 : remap.nodes[oldModel.iHeadnodes[0]];
	for (int i = 1; i < MAX_MAP_HULLS; i++)
	{
		oldModel.iHeadnodes[i] = oldModel.iHeadnodes[i] < 0 ? -1 : remap.clipnodes[oldModel.iHeadnodes[i]];
	}
}

int Bsp::duplicate_model(int modelIdx)
{
	std::vector<BSPPLANE> newPlanes;
	std::vector<vec3> newVerts;
	std::vector<BSPEDGE32> newEdges;
	std::vector<int> newSurfedges;
	std::vector<BSPTEXTUREINFO> newTexinfo;
	std::vector<BSPFACE32> newFaces;
	std::vector<COLOR3> newLightmaps;
	std::vector<BSPNODE32> newNodes;
	std::vector<BSPCLIPNODE32> newClipnodes;
	std::vector<WADTEX*> newTextures;

	STRUCTREMAP remap(this);
	copy_bsp_model(modelIdx, this, remap, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces, newLightmaps, newNodes, newClipnodes, newTextures);

	for (auto& s : newTextures)
	{
		delete s;
	}

	if (newClipnodes.size())
		append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE32) * newClipnodes.size());
	if (newEdges.size())
		append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE32) * newEdges.size());
	if (newFaces.size())
	{
		/*if (g_verbose)
		{
			print_log("Origin model faces: {}\n", models[modelIdx].nFaces);
			print_log("Base light offset = {} copy faces {}\n", lightDataLength, newFaces.size());
			for (int i = 0; i < newFaces.size(); i++)
			{
				print_log("Face {} light offset = {}\n", i, newFaces[i].nLightmapOffset);
			}
		}*/
		append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE32) * newFaces.size());
	}
	if (newNodes.size())
		append_lump(LUMP_NODES, &newNodes[0], sizeof(BSPNODE32) * newNodes.size());
	if (newPlanes.size())
		append_lump(LUMP_PLANES, &newPlanes[0], sizeof(BSPPLANE) * newPlanes.size());
	if (newSurfedges.size())
		append_lump(LUMP_SURFEDGES, &newSurfedges[0], sizeof(int) * newSurfedges.size());
	if (newTexinfo.size())
		append_lump(LUMP_TEXINFO, &newTexinfo[0], sizeof(BSPTEXTUREINFO) * newTexinfo.size());
	if (newVerts.size())
		append_lump(LUMP_VERTICES, &newVerts[0], sizeof(vec3) * newVerts.size());
	if (newLightmaps.size())
	{
		/*if (g_verbose)
		{
			print_log("Added lightmap, size {}\n", newLightmaps.size());
			print_log("Data {}x{}x{}\n", newLightmaps[0].r, newLightmaps[0].g, newLightmaps[0].b);
		}*/
		append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());
		save_undo_lightmaps();
		renderer->loadLightmaps();
	}

	int newModelIdx = create_model();
	BSPMODEL& oldModel = models[modelIdx];
	BSPMODEL& newModel = models[newModelIdx];
	memcpy(&newModel, &oldModel, sizeof(BSPMODEL));
	newModel.iFirstFace = remap.faces[oldModel.iFirstFace];
	newModel.iHeadnodes[0] = oldModel.iHeadnodes[0] < 0 ? -1 : remap.nodes[oldModel.iHeadnodes[0]];
	for (int i = 1; i < MAX_MAP_HULLS; i++)
	{
		newModel.iHeadnodes[i] = oldModel.iHeadnodes[i] < 0 ? -1 : remap.clipnodes[oldModel.iHeadnodes[i]];
	}
	newModel.nVisLeafs = 0; // techinically should match the old model, but leaves aren't duplicated yetx
	// recalculate leafs 
	return newModelIdx;
}

bool Bsp::cull_leaf_faces(int leafIdx)
{
	BSPLEAF32& leaf = leaves[leafIdx];
	int rowSize = (((leafCount - 1) + 63) & ~63) >> 3;
	unsigned char* visData = new unsigned char[rowSize];
	memset(visData, 0xFF, rowSize);
	DecompressVis(visdata + leaf.nVisOffset, visData, rowSize, leafCount - 1, visDataLength - leaf.nVisOffset);

	std::vector<int> faces_to_remove;
	std::vector<int> leafs_to_remove;

	std::vector<int> visLeafs;
	modelLeafs(0, visLeafs);

	for (auto l : visLeafs)
	{
		if (l == 0)
			continue;
		if (l == leafIdx || CHECKVISBIT(visData, l - 1))
		{
			auto faceList = getLeafFaces(l);
			leafs_to_remove.push_back(l);
			faces_to_remove.insert(faces_to_remove.end(), faceList.begin(), faceList.end());
		}
	}
	delete[] visData;


	std::sort(faces_to_remove.begin(), faces_to_remove.end());
	faces_to_remove.erase(std::unique(faces_to_remove.begin(), faces_to_remove.end()), faces_to_remove.end());

	STRUCTCOUNT count_1(this);
	g_progress.update("Remove cull faces.[LEAF 0 CLEAN]", (int)faces_to_remove.size());

	while (faces_to_remove.size())
	{
		remove_face(faces_to_remove[faces_to_remove.size() - 1]);
		faces_to_remove.pop_back();
		g_progress.tick();
	}

	g_progress.clear();
	g_progress = ProgressMeter();

	STRUCTCOUNT count_2(this);
	count_1.sub(count_2);
	count_1.print_delete_stats(1);

	return true;
}

bool Bsp::leaf_add_face(int faceIdx, int leafIdx)
{
	if (faceIdx < 0 || faceIdx >= faceCount)
	{
		return false;
	}


	std::vector<int> all_mark_surfaces;
	int surface_idx = 0;
	for (int i = 0; i < leafCount; i++)
	{
		bool has_face = false;
		for (int n = 0; n < leaves[i].nMarkSurfaces; n++)
		{
			if (marksurfs[leaves[i].iFirstMarkSurface + n] == faceIdx)
			{
				has_face = true;
			}
			all_mark_surfaces.push_back(marksurfs[leaves[i].iFirstMarkSurface + n]);
		}

		leaves[i].iFirstMarkSurface = surface_idx;

		if (!has_face && (leafIdx == -1 || leafIdx == i))
		{
			leaves[i].nMarkSurfaces += 1;
			all_mark_surfaces.push_back(faceIdx);
		}
		surface_idx += leaves[i].nMarkSurfaces;
	}

	unsigned char* newLump = new unsigned char[sizeof(int) * all_mark_surfaces.size()];
	memcpy(newLump, &all_mark_surfaces[0], sizeof(int) * all_mark_surfaces.size());
	replace_lump(LUMP_MARKSURFACES, newLump, sizeof(int) * all_mark_surfaces.size());

	return true;
}


bool Bsp::leaf_del_face(int faceIdx, int leafIdx)
{
	if (faceIdx < 0 || faceIdx >= faceCount)
	{
		return false;
	}

	std::vector<int> all_mark_surfaces;
	int surface_idx = 0;
	for (int i = 0; i < leafCount; i++)
	{
		int del_faces = 0;
		for (int n = 0; n < leaves[i].nMarkSurfaces; n++)
		{
			if (marksurfs[leaves[i].iFirstMarkSurface + n] == faceIdx && (leafIdx == -1 || leafIdx == i))
			{
				del_faces++;
			}
			else
			{
				all_mark_surfaces.push_back(marksurfs[leaves[i].iFirstMarkSurface + n]);
			}
		}

		leaves[i].iFirstMarkSurface = surface_idx;
		surface_idx = all_mark_surfaces.size();
		leaves[i].nMarkSurfaces = surface_idx - leaves[i].iFirstMarkSurface;
	}

	unsigned char* newLump = new unsigned char[sizeof(int) * all_mark_surfaces.size()];
	memcpy(newLump, &all_mark_surfaces[0], sizeof(int) * all_mark_surfaces.size());
	replace_lump(LUMP_MARKSURFACES, newLump, sizeof(int) * all_mark_surfaces.size());

	return true;
}

void Bsp::remove_faces_by_content(int content)
{
	std::vector<int> faces_to_remove;

	g_progress.update("Remove faces by content[SEARCH]", faceCount);

	for (int f = 0; f < faceCount; f++)
	{
		auto face_leafs = getFaceLeafs(f);
		bool same_content = true;
		for (auto l : face_leafs)
		{
			if (leaves[l].nContents != content)
			{
				same_content = false;
				break;
			}
		}
		if (same_content)
		{
			faces_to_remove.push_back(f);
		}
		g_progress.tick();
	}

	g_progress.update("Remove faces by content[DELETE]", (int)faces_to_remove.size());

	int removedFaces = 0;

	while (faces_to_remove.size())
	{
		remove_face(faces_to_remove[faces_to_remove.size() - 1]);
		faces_to_remove.pop_back();
		g_progress.tick();
		removedFaces++;
	}

	g_progress.clear();
	g_progress = ProgressMeter();

	print_log("Removed {} faces from map!\n", removedFaces);
}

bool Bsp::remove_face(int faceIdx)
{
	// Check if face index is valid
	if (faceIdx < 0 || faceIdx >= faceCount)
	{
		return false;
	}

	// Create a vector to hold all the faces except the one to be removed
	std::vector<BSPFACE32> all_faces;
	for (int f = 0; f < faceCount; f++)
	{
		if (f != faceIdx)
		{
			all_faces.push_back(faces[f]);
		}
	}

	// Shift face count in models
	for (int m = 0; m < modelCount; m++)
	{
		// If the model has no faces or its first face index is out of bounds, reset the face count and continue to the next model
		if (models[m].nFaces <= 0 || models[m].iFirstFace < 0)
		{
			models[m].iFirstFace = 0;
			models[m].nFaces = 0;
			continue;
		}

		if (faceIdx >= models[m].iFirstFace && faceIdx < models[m].iFirstFace + models[m].nFaces)
		{
			models[m].nFaces--;
		}
		else if (faceIdx < models[m].iFirstFace)
		{
			models[m].iFirstFace--;
		}
		if (models[m].nFaces <= 0 || models[m].iFirstFace < 0)
		{
			models[m].iFirstFace = 0;
			models[m].nFaces = 0;
		}
	}

	// Shift face count in nodes
	for (int n = 0; n < nodeCount; n++)
	{
		// If the node has no faces or its first face index is out of bounds, reset the face count and continue to the next node
		if (nodes[n].nFaces <= 0 || nodes[n].iFirstFace < 0)
		{
			nodes[n].iFirstFace = 0;
			nodes[n].nFaces = 0;
			continue;
		}
		if (faceIdx >= nodes[n].iFirstFace && faceIdx < nodes[n].iFirstFace + nodes[n].nFaces)
		{
			nodes[n].nFaces--;
		}
		else if (faceIdx < nodes[n].iFirstFace)
		{
			nodes[n].iFirstFace--;
		}
		if (nodes[n].nFaces <= 0 || nodes[n].iFirstFace < 0)
		{
			nodes[n].iFirstFace = 0;
			nodes[n].nFaces = 0;
		}
	}

	// Update the faces array after removing the specified face
	unsigned char* newLump = new unsigned char[sizeof(BSPFACE32) * all_faces.size()];
	memcpy(newLump, &all_faces[0], sizeof(BSPFACE32) * all_faces.size());
	replace_lump(LUMP_FACES, newLump, sizeof(BSPFACE32) * all_faces.size());

	// Remove face from all leaves
	leaf_del_face(faceIdx, -1);

	// Shift face count in marksurfs and mark the surfaces to be deleted
	for (int s = 0; s < marksurfCount; s++)
	{
		if (marksurfs[s] < 0)
			continue;

		if (faceIdx < marksurfs[s])
		{
			marksurfs[s]--;
		}
	}

	return true;
}

/*

node.child[0] = node_next
node.child[1] = need_leaf


node.child[1] = new_node

new_node.child[0] = new_node2
new_node_child[1] = old_leaf

new_node2.child[0] = empty_leaf
new_node2.child[1] = new_leaf

* */

int Bsp::clone_world_leaf(int oldleafIdx)
{
	int anyEmptyLeaf = 0;
	for (int i = 0; i < leafCount; i++)
	{
		if (leaves[i].nContents == CONTENTS_EMPTY)
		{
			anyEmptyLeaf = i;
			break;
		}
	}

	if (anyEmptyLeaf == 0)
	{
		anyEmptyLeaf = create_leaf(CONTENTS_EMPTY);
	}

	int startup_node_count = nodeCount;
	for (int i = 0; i < startup_node_count; i++)
	{
		BSPNODE32& node = nodes[i];
		if (node.iChildren[0] < 0)
		{
			int l = ~node.iChildren[0];
			if (l == oldleafIdx)
			{
				BSPNODE32* newThisNodes = new BSPNODE32[nodeCount + 2];
				memcpy(newThisNodes, nodes, nodeCount * sizeof(BSPNODE32));

				newThisNodes[i].iChildren[0] = nodeCount;

				newThisNodes[nodeCount] = node;
				newThisNodes[nodeCount].iChildren[0] = ~l;
				newThisNodes[nodeCount].iChildren[1] = nodeCount + 1;
				newThisNodes[nodeCount + 1] = node;
				newThisNodes[nodeCount + 1].iChildren[1] = ~leafCount;
				newThisNodes[nodeCount + 1].iChildren[0] = ~leafCount;
				if (newThisNodes[nodeCount + 1].iPlane >= 0)
				{
					newThisNodes[nodeCount + 1].iPlane = planeCount;
					BSPPLANE* newThisPlanes = new BSPPLANE[planeCount + 1];
					memcpy(newThisPlanes, planes, planeCount * sizeof(BSPPLANE));
					newThisPlanes[planeCount] = planes[node.iPlane];
					replace_lump(LUMP_PLANES, newThisPlanes, (planeCount + 1) * sizeof(BSPPLANE));
				}
				replace_lump(LUMP_NODES, newThisNodes, (nodeCount + 2) * sizeof(BSPNODE32));
			}
		}
		if (node.iChildren[1] < 0)
		{
			int l = ~node.iChildren[1];
			if (l == oldleafIdx)
			{
				BSPNODE32* newThisNodes = new BSPNODE32[nodeCount + 2];
				memcpy(newThisNodes, nodes, nodeCount * sizeof(BSPNODE32));

				newThisNodes[i].iChildren[1] = nodeCount;

				newThisNodes[nodeCount] = node;
				newThisNodes[nodeCount].iChildren[1] = ~l;
				newThisNodes[nodeCount].iChildren[0] = nodeCount + 1;
				newThisNodes[nodeCount + 1] = node;
				newThisNodes[nodeCount + 1].iChildren[0] = ~leafCount;
				newThisNodes[nodeCount + 1].iChildren[1] = ~leafCount;;

				if (newThisNodes[nodeCount + 1].iPlane >= 0)
				{
					newThisNodes[nodeCount + 1].iPlane = planeCount;
					BSPPLANE* newThisPlanes = new BSPPLANE[planeCount + 1];
					memcpy(newThisPlanes, planes, planeCount * sizeof(BSPPLANE));
					newThisPlanes[planeCount] = planes[node.iPlane];
					replace_lump(LUMP_PLANES, newThisPlanes, (planeCount + 1) * sizeof(BSPPLANE));
				}

				replace_lump(LUMP_NODES, newThisNodes, (nodeCount + 2) * sizeof(BSPNODE32));
			}
		}
	}

	int rowSize = (((leafCount - 1) + 63) & ~63) >> 3;
	int newRowSize = (((leafCount/* - 1*/)+63) & ~63) >> 3;

	std::vector<BSPLEAF32> outLeafs{};
	{
		for (int i = 0; i < leafCount; i++)
		{
			outLeafs.push_back(leaves[i]);
		}
		outLeafs.push_back(leaves[oldleafIdx]);

		if (leaves[oldleafIdx].iFirstMarkSurface >= 0 && leaves[oldleafIdx].nMarkSurfaces > 0)
		{
			outLeafs[outLeafs.size() - 1].iFirstMarkSurface = marksurfCount;
		}

		if (leaves[oldleafIdx].iFirstMarkSurface >= 0 && leaves[oldleafIdx].nMarkSurfaces > 0)
		{
			int* newMarkSurfs = new int[marksurfCount + leaves[oldleafIdx].nMarkSurfaces];
			memcpy(newMarkSurfs, marksurfs, marksurfCount * sizeof(int));
			memcpy(newMarkSurfs + marksurfCount, &marksurfs[leaves[oldleafIdx].iFirstMarkSurface],
				leaves[oldleafIdx].nMarkSurfaces * sizeof(int));
			replace_lump(LUMP_MARKSURFACES, newMarkSurfs, (marksurfCount + leaves[oldleafIdx].nMarkSurfaces) * sizeof(int));
		}

		models[0].nVisLeafs++;
	}

	unsigned char* visData = new unsigned char[newRowSize];
	unsigned char* compressed = new unsigned char[MAX_MAP_LEAVES / 8];

	// ADD ONE LEAF TO ALL VISIBILITY BYTES
	for (int i = 1; i < leafCount; i++)
	{
		if (leaves[i].nVisOffset >= 0)
		{
			memset(visData, 0, newRowSize);
			DecompressVis(visdata + leaves[i].nVisOffset, visData, rowSize, leafCount - 1, visDataLength - leaves[i].nVisOffset);

			memset(compressed, 0, MAX_MAP_LEAVES / 8);
			int size = CompressVis(visData, newRowSize, compressed, MAX_MAP_LEAVES / 8);

			leaves[i].nVisOffset = visDataLength;

			unsigned char* newVisLump = new unsigned char[visDataLength + size];
			memcpy(newVisLump, visdata, visDataLength);
			memcpy(newVisLump + visDataLength, compressed, size);
			replace_lump(LUMP_VISIBILITY, newVisLump, visDataLength + size);
		}
	}

	delete[] compressed;
	delete[] visData;

	BSPLEAF32* newLeaves = new BSPLEAF32[outLeafs.size()];
	memcpy(newLeaves, outLeafs.data(), outLeafs.size() * sizeof(BSPLEAF32));
	replace_lump(LUMP_LEAVES, newLeaves, outLeafs.size() * sizeof(BSPLEAF32));

	// repack visdata
	auto removed = remove_unused_model_structures(CLEAN_VISDATA);

	if (!removed.allZero())
		removed.print_delete_stats(1);

	return leafCount - 1;
}

int Bsp::merge_two_models(int src_model, int dst_model)
{
	if (models[dst_model].iFirstFace > models[src_model].iFirstFace)
		std::swap(src_model, dst_model);

	int newfaces = models[src_model].nFaces;

	for (int f = 0; f < newfaces; f++)
	{
		leaf_del_face(models[src_model].iFirstFace + f, -1);
	}

	for (int f2 = 0; f2 < models[dst_model].nFaces; f2++)
	{
		leaf_del_face(models[dst_model].iFirstFace + f2, -1);
	}

	std::vector<BSPFACE32> all_faces;

	for (int f = 0; f < faceCount; f++)
	{
		all_faces.push_back(faces[f]);
		if (f == models[dst_model].iFirstFace + models[dst_model].nFaces - 1)
		{
			for (int f2 = 0; f2 < newfaces; f2++)
			{
				all_faces.push_back(faces[models[src_model].iFirstFace + f2/* + 1*/]);
			}
		}
	}

	for (int m = 0; m < modelCount; m++)
	{
		if (models[m].iFirstFace >= models[dst_model].iFirstFace + models[dst_model].nFaces)
		{
			models[m].iFirstFace += newfaces;
		}
	}

	for (int m = 0; m < nodeCount; m++)
	{
		if (nodes[m].iFirstFace >= models[dst_model].iFirstFace + models[dst_model].nFaces)
		{
			nodes[m].iFirstFace += newfaces;
		}
	}

	for (int m = 0; m < marksurfCount; m++)
	{
		if (marksurfs[m] >= models[dst_model].iFirstFace + models[dst_model].nFaces)
		{
			marksurfs[m] += newfaces;
		}
	}

	// add faces from first model to second model leafs and back


	unsigned char* newLump = new unsigned char[sizeof(BSPFACE32) * all_faces.size()];
	memcpy(newLump, &all_faces[0], sizeof(BSPFACE32) * all_faces.size());
	replace_lump(LUMP_FACES, newLump, sizeof(BSPFACE32) * all_faces.size());

	vec3 amin = models[dst_model].nMins;
	vec3 amax = models[dst_model].nMaxs;
	vec3 bmin = models[src_model].nMins;
	vec3 bmax = models[src_model].nMaxs;

	std::vector<vec3> veclist;
	veclist.push_back(amin);
	veclist.push_back(amax);
	veclist.push_back(bmin);
	veclist.push_back(bmax);

	vec3 new_min, new_max;

	getBoundingBox(veclist, new_min, new_max);

	BSPPLANE separate_plane = getSeparatePlane(amin, amax, bmin, bmax);

	int separationPlaneIdx = planeCount;

	BSPPLANE* newThisPlanes = new BSPPLANE[planeCount + 1];
	memcpy(newThisPlanes, planes, planeCount * sizeof(BSPPLANE));

	bool swapNodeChildren = separate_plane.vNormal.x < 0 || separate_plane.vNormal.y < 0 || separate_plane.vNormal.z < 0;
	if (swapNodeChildren)
		separate_plane.vNormal = separate_plane.vNormal.invert();

	newThisPlanes[planeCount] = separate_plane;
	replace_lump(LUMP_PLANES, newThisPlanes, (planeCount + 1) * sizeof(BSPPLANE));

	{
		BSPNODE32 headNode = {
			separationPlaneIdx,			// plane idx
			{ models[src_model].iHeadnodes[0],
			models[dst_model].iHeadnodes[0] },		// child nodes
			{ new_min.x, new_min.y, new_min.z },	// mins
			{ new_max.x, new_max.y, new_max.z },	// maxs
			0, // first face
			0  // n faces (none since this plane is in the void)
		};

		if (swapNodeChildren)
		{
			int temp = headNode.iChildren[0];
			headNode.iChildren[0] = headNode.iChildren[1];
			headNode.iChildren[1] = temp;
		}


		BSPNODE32* newThisNodes = new BSPNODE32[nodeCount + 1];
		memcpy(newThisNodes, nodes, nodeCount * sizeof(BSPNODE32));
		newThisNodes[nodeCount] = headNode;
		replace_lump(LUMP_NODES, newThisNodes, (nodeCount + 1) * sizeof(BSPNODE32));


		models[dst_model].iHeadnodes[0] = nodeCount - 1;
	}

	{
		const int NEW_NODE_COUNT = MAX_MAP_HULLS - 1;

		BSPCLIPNODE32 newHeadNodes[NEW_NODE_COUNT];
		for (int i = 0; i < NEW_NODE_COUNT; i++)
		{
			newHeadNodes[i] = {
				separationPlaneIdx,	// plane idx
				{	// child nodes
					models[src_model].iHeadnodes[i + 1],
					models[dst_model].iHeadnodes[i + 1]
				},
			};

			if (swapNodeChildren)
			{
				int temp = newHeadNodes[i].iChildren[0];
				newHeadNodes[i].iChildren[0] = newHeadNodes[i].iChildren[1];
				newHeadNodes[i].iChildren[1] = temp;
			}
		}

		BSPCLIPNODE32* newThisNodes = new BSPCLIPNODE32[clipnodeCount + NEW_NODE_COUNT];

		memcpy(newThisNodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE32));
		memcpy(&newThisNodes[clipnodeCount], &newHeadNodes[0], NEW_NODE_COUNT * sizeof(BSPCLIPNODE32));
		replace_lump(LUMP_CLIPNODES, newThisNodes, (clipnodeCount + NEW_NODE_COUNT) * sizeof(BSPCLIPNODE32));

		for (int i = 1; i < MAX_MAP_HULLS; i++)
		{
			models[dst_model].iHeadnodes[i] = (clipnodeCount - i);
		}
	}

	// swap leaves?

	models[dst_model].nFaces += newfaces;
	models[dst_model].nVisLeafs += models[src_model].nVisLeafs;

	models[dst_model].nMins = new_min;
	models[dst_model].nMaxs = new_max;

	models[src_model].iFirstFace = 0;
	models[src_model].iHeadnodes[0] = models[src_model].iHeadnodes[1] =
		models[src_model].iHeadnodes[2] = models[src_model].iHeadnodes[3] = CONTENTS_EMPTY;
	models[src_model].nFaces = 0;
	models[src_model].nVisLeafs = 0;

	update_lump_pointers();

	std::vector<int> leafs;
	modelLeafs(dst_model, leafs);

	for (auto& l : leafs)
	{
		for (int f2 = 0; f2 < models[dst_model].nFaces; f2++)
		{
			leaf_add_face(models[dst_model].iFirstFace + f2, l);
		}
	}

	save_undo_lightmaps();
	return dst_model;
}

BSPTEXTUREINFO* Bsp::get_unique_texinfo(int faceIdx)
{
	BSPFACE32& targetFace = faces[faceIdx];
	int targetInfo = targetFace.iTextureInfo;

	for (int i = 0; i < faceCount; i++)
	{
		if (i != faceIdx && faces[i].iTextureInfo == targetFace.iTextureInfo)
		{
			int newInfo = create_texinfo();
			texinfos[newInfo] = texinfos[targetInfo];
			targetInfo = newInfo;
			targetFace.iTextureInfo = newInfo;
			print_log(get_localized_string(LANG_0174), newInfo);
			break;
		}
	}

	return &texinfos[targetInfo];
}

bool Bsp::is_unique_texinfo(int faceIdx)
{
	BSPFACE32& targetFace = faces[faceIdx];

	for (int i = 0; i < faceCount; i++)
	{
		if (i != faceIdx && faces[i].iTextureInfo == targetFace.iTextureInfo)
		{
			return false;
		}
	}

	return true;
}

int Bsp::get_ent_from_model(int modelIdx)
{
	if (modelIdx < 0)
		return -1;

	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdx() == modelIdx)
			return (int)i;
	}

	if (modelIdx == 0)
	{
		for (size_t i = 0; i < ents.size(); i++)
		{
			if (ents[i]->isWorldSpawn())
				return (int)i;
		}
	}

	return -1;
}

int Bsp::get_model_from_face(int faceIdx)
{
	for (int i = 0; i < modelCount; i++)
	{
		BSPMODEL& model = models[i];
		if (isModelHasFaceIdx(model, faceIdx))
		{
			return i;
		}
	}
	return -1;
}

int Bsp::get_model_from_leaf(int leafIdx)
{
	for (int i = 0; i < modelCount; i++)
	{
		std::vector<int> visLeafs;
		modelLeafs(i, visLeafs);
		if (std::find(visLeafs.begin(), visLeafs.end(), leafIdx) != visLeafs.end())
		{
			return i;
		}
	}
	return -1;
}

bool Bsp::is_worldspawn_ent(int entIdx)
{
	if (entIdx < 0 || entIdx >= (int)ents.size())
		return true;
	if (ents[entIdx]->hasKey("classname") && ents[entIdx]->keyvalues["classname"] == "worldspawn"
		&& ents[entIdx]->getBspModelIdx() <= 0)
		return true;
	return false;
}

int Bsp::regenerate_clipnodes_from_nodes(int iNode, int hullIdx)
{
	BSPNODE32& node = nodes[iNode];

	switch (planes[node.iPlane].nType)
	{
	case PLANE_X: case PLANE_Y: case PLANE_Z:
	{
		// Skip this node. Bounding box clipnodes should have already been generated.
		// Only works for convex models.
		int childContents[2] = { 0, 0 };
		for (int i = 0; i < 2; i++)
		{
			if (node.iChildren[i] < 0)
			{
				BSPLEAF32& leaf = leaves[~node.iChildren[i]];
				childContents[i] = leaf.nContents;
			}
		}

		int solidChild = childContents[0] == CONTENTS_EMPTY ? node.iChildren[1] : node.iChildren[0];
		int solidContents = childContents[0] == CONTENTS_EMPTY ? childContents[1] : childContents[0];

		if (solidChild < 0)
		{
			if (solidContents != CONTENTS_SOLID)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0175), solidContents);
			}
			// solidContents or CONTENTS_SOLID?
			return CONTENTS_SOLID;
		}
		return regenerate_clipnodes_from_nodes(solidChild, hullIdx);
	}
	default:
		break;
	}

	int newClipnodeIdx = create_clipnode();
	clipnodes[newClipnodeIdx].iPlane = create_plane();

	int solidChild = -1;
	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			int childIdx = regenerate_clipnodes_from_nodes(node.iChildren[i], hullIdx);
			clipnodes[newClipnodeIdx].iChildren[i] = childIdx;
			solidChild = solidChild == -1 ? i : -1;
		}
		else
		{
			BSPLEAF32& leaf = leaves[~node.iChildren[i]];
			clipnodes[newClipnodeIdx].iChildren[i] = leaf.nContents;
			if (leaf.nContents == CONTENTS_SOLID)
			{
				solidChild = i;
			}
		}
	}

	BSPPLANE& nodePlane = planes[node.iPlane];
	BSPPLANE& clipnodePlane = planes[clipnodes[newClipnodeIdx].iPlane];
	clipnodePlane = nodePlane;

	// TODO: pretty sure this isn't right. Angled stuff probably lerps between the hull dimensions
	float extent = 0;
	switch (clipnodePlane.nType)
	{
	case PLANE_X: case PLANE_ANYX: extent = default_hull_extents[hullIdx].x; break;
	case PLANE_Y: case PLANE_ANYY: extent = default_hull_extents[hullIdx].y; break;
	case PLANE_Z: case PLANE_ANYZ: extent = default_hull_extents[hullIdx].z; break;
	}

	// TODO: this won't work for concave solids. The node's face could be used to determine which
	// direction the plane should be extended but not all nodes will have faces. Also wouldn't be
	// enough to "link" clipnode planes to node planes during scaling because BSP trees might not match.
	if (solidChild != -1)
	{
		BSPPLANE& p = planes[clipnodes[newClipnodeIdx].iPlane];
		vec3 planePoint = p.vNormal * p.fDist;
		vec3 newPlanePoint = planePoint + p.vNormal * (solidChild == 0 ? -extent : extent);
		p.fDist = dotProduct(p.vNormal, newPlanePoint) / dotProduct(p.vNormal, p.vNormal);
	}

	return newClipnodeIdx;
}


void Bsp::regenerate_clipnodes(int modelIdx, int hullIdx)
{
	BSPMODEL& model = models[modelIdx];

	for (int i = 1; i < MAX_MAP_HULLS; i++)
	{
		if (hullIdx >= 0 && hullIdx != i)
			continue;

		// first create a bounding box for the model. For some reason this is needed to prevent
		// planes from extended farther than they should. All clip types do this.
		int solidNodeIdx = create_clipnode_box(model.nMins, model.nMaxs, &model, i, false); // fills in the headnode

		for (int k = 0; k < 2; k++)
		{
			if (clipnodes[solidNodeIdx].iChildren[k] == CONTENTS_SOLID)
			{
				clipnodes[solidNodeIdx].iChildren[k] = regenerate_clipnodes_from_nodes(model.iHeadnodes[0], i);
			}
		}

		// TODO: create clipnodes to "cap" edges that are 90+ degrees (most CSG clip types do this)
		// that will fix broken collision around those edges (invisible solid areas)
	}
}

void Bsp::write_csg_outputs(const std::string& path)
{
	BSPPLANE* thisPlanes = (BSPPLANE*)lumps[LUMP_PLANES];
	int numPlanes = bsp_header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);

	// add flipped version of planes since face output files can't specify plane side
	BSPPLANE* newPlanes = new BSPPLANE[numPlanes * 2];
	memcpy(newPlanes, thisPlanes, numPlanes * sizeof(BSPPLANE));
	for (int i = 0; i < numPlanes; i++)
	{
		BSPPLANE flipped = thisPlanes[i];
		flipped.vNormal = { flipped.vNormal.x > 0.0f ? -flipped.vNormal.x : flipped.vNormal.x,
							flipped.vNormal.y > 0.0f ? -flipped.vNormal.y : flipped.vNormal.y,
							flipped.vNormal.z > 0.0f ? -flipped.vNormal.z : flipped.vNormal.z, };
		flipped.fDist = -flipped.fDist;
		newPlanes[numPlanes + i] = flipped;
	}
	delete[] lumps[LUMP_PLANES];
	lumps[LUMP_PLANES] = (unsigned char*)newPlanes;
	numPlanes *= 2;
	bsp_header.lump[LUMP_PLANES].nLength = numPlanes * sizeof(BSPPLANE);
	thisPlanes = newPlanes;

	std::ofstream pln_file(path + bsp_name + ".pln", std::ios::trunc | std::ios::binary);
	for (int i = 0; i < numPlanes; i++)
	{
		BSPPLANE& p = thisPlanes[i];
		CSGPLANE csgplane = {
			{p.vNormal.x, p.vNormal.y, p.vNormal.z},
			{0,0,0},
			p.fDist,
			p.nType
		};
		pln_file.write((char*)&csgplane, sizeof(CSGPLANE));
	}
	print_log(get_localized_string(LANG_0176), numPlanes);

	BSPMODEL* tmodels = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPMODEL world = tmodels[0];

	for (int i = 0; i < 4; i++)
	{

		FILE* polyfile = NULL;
		fopen_s(&polyfile, (path + bsp_name + ".p" + std::to_string(i)).c_str(), "wb");
		if (polyfile)
		{
			write_csg_polys(world.iHeadnodes[i], polyfile, numPlanes / 2, i == 0);
			fprintf(polyfile, "-1 -1 -1 -1 -1\n"); // end of file marker (parsing fails without this)
			fclose(polyfile);
		}

		FILE* detailfile = NULL;
		fopen_s(&detailfile, (path + bsp_name + ".b" + std::to_string(i)).c_str(), "wb");
		if (detailfile)
		{
			fprintf(detailfile, "-1\n");
			fclose(detailfile);
		}
	}

	std::ofstream hsz_file(path + bsp_name + ".hsz", std::ios::trunc | std::ios::binary);
	const char* hullSizes = "0 0 0 0 0 0\n"
		"-16 -16 -36 16 16 36\n"
		"-32 -32 -32 32 32 32\n"
		"-16 -16 -18 16 16 18\n";
	hsz_file.write(hullSizes, strlen(hullSizes));

	std::ofstream bsp_file(path + bsp_name + "_new.bsp", std::ios::trunc | std::ios::binary);
	// calculate lump offsets
	int offset = sizeof(BSPHEADER);
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		bsp_header.lump[i].nOffset = offset;
		if (i == LUMP_ENTITIES || i == LUMP_PLANES || i == LUMP_TEXTURES || i == LUMP_TEXINFO)
		{
			offset += bsp_header.lump[i].nLength;
			if (i == LUMP_PLANES)
			{
				int count = bsp_header.lump[i].nLength / sizeof(BSPPLANE);
				print_log(get_localized_string(LANG_0177), count);
			}
		}
		else
		{
			bsp_header.lump[i].nLength = 0;
		}
	}
	bsp_file.write((char*)&bsp_header, sizeof(BSPHEADER));
	// write the lumps
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		bsp_file.write((char*)lumps[i], bsp_header.lump[i].nLength);
	}
}

void Bsp::write_csg_polys(int nodeIdx, FILE* polyfile, int flipPlaneSkip, bool debug)
{
	if (nodeIdx >= 0)
	{
		write_csg_polys(nodes[nodeIdx].iChildren[0], polyfile, flipPlaneSkip, debug);
		write_csg_polys(nodes[nodeIdx].iChildren[1], polyfile, flipPlaneSkip, debug);
		return;
	}

	BSPLEAF32& leaf = leaves[~nodeIdx];

	int detaillevel = 0; // no way to know which faces came from a func_detail

	for (int i = leaf.iFirstMarkSurface; i < leaf.iFirstMarkSurface + leaf.nMarkSurfaces; i++)
	{
		for (int z = 0; z < 2; z++)
		{
			BSPFACE32& face = faces[marksurfs[i]];

			bool flipped = (z == 1 || face.nPlaneSide) && !(z == 1 && face.nPlaneSide);

			int iPlane = !flipped ? face.iPlane : face.iPlane + flipPlaneSkip;

			// FIXME : z always == 1
			// contents in front of the face
			int faceContents = z == 1 ? leaf.nContents : CONTENTS_SOLID;

			//int texInfo = z == 1 ? face.iTextureInfo : -1;

			if (debug)
			{
				BSPPLANE plane = planes[iPlane];
				print_log("Writing face ({:2.0f} {:2.0f} {:2.0f}) {:4.0f}  {}\n",
					plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist,
					(faceContents == CONTENTS_SOLID ? "SOLID" : "EMPTY"));
				if (flipped && false)
				{
					print_log(get_localized_string(LANG_0178));
				}
			}

			fprintf(polyfile, "%i %i %u %i %u\n", detaillevel, iPlane, face.iTextureInfo, faceContents, face.nEdges);

			if (flipped)
			{
				for (int e = (face.iFirstEdge + face.nEdges) - 1; e >= face.iFirstEdge; e--)
				{
					int edgeIdx = surfedges[e];
					BSPEDGE32& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx >= 0 ? verts[edge.iVertex[1]] : verts[edge.iVertex[0]];
					fprintf(polyfile, "%5.8f %5.8f %5.8f\n", v.x, v.y, v.z);
				}
			}
			else
			{
				for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
				{
					int edgeIdx = surfedges[e];
					BSPEDGE32& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx >= 0 ? verts[edge.iVertex[1]] : verts[edge.iVertex[0]];
					fprintf(polyfile, "%5.8f %5.8f %5.8f\n", v.x, v.y, v.z);
				}
			}

			fprintf(polyfile, "\n");
		}
		if (debug)
			print_log("\n");
	}
}

void Bsp::print_leaf(const BSPLEAF32& leaf)
{
	print_log(getLeafContentsName(leaf.nContents));
	print_log(" {} surfs, Min({}, {}, {}), Max({} {} {})", leaf.nMarkSurfaces,
		leaf.nMins[0], leaf.nMins[1], leaf.nMins[2],
		leaf.nMaxs[0], leaf.nMaxs[1], leaf.nMaxs[2]);
}

void Bsp::update_lump_pointers()
{
	planes = (BSPPLANE*)lumps[LUMP_PLANES];
	texinfos = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO];
	leaves = (BSPLEAF32*)lumps[LUMP_LEAVES];
	models = (BSPMODEL*)lumps[LUMP_MODELS];
	nodes = (BSPNODE32*)lumps[LUMP_NODES];
	clipnodes = (BSPCLIPNODE32*)lumps[LUMP_CLIPNODES];
	faces = (BSPFACE32*)lumps[LUMP_FACES];
	verts = (vec3*)lumps[LUMP_VERTICES];
	lightdata = lumps[LUMP_LIGHTING];
	surfedges = (int*)lumps[LUMP_SURFEDGES];
	edges = (BSPEDGE32*)lumps[LUMP_EDGES];
	marksurfs = (int*)lumps[LUMP_MARKSURFACES];
	visdata = lumps[LUMP_VISIBILITY];
	textures = lumps[LUMP_TEXTURES];

	planeCount = bsp_header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	texinfoCount = bsp_header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	leafCount = bsp_header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF32);
	modelCount = bsp_header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	nodeCount = bsp_header.lump[LUMP_NODES].nLength / sizeof(BSPNODE32);
	vertCount = bsp_header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	faceCount = bsp_header.lump[LUMP_FACES].nLength / sizeof(BSPFACE32);
	clipnodeCount = bsp_header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE32);
	marksurfCount = bsp_header.lump[LUMP_MARKSURFACES].nLength / sizeof(int);
	surfedgeCount = bsp_header.lump[LUMP_SURFEDGES].nLength / sizeof(int);
	edgeCount = bsp_header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE32);
	textureCount = *((int*)(textures));
	textureDataLength = bsp_header.lump[LUMP_TEXTURES].nLength;
	lightDataLength = bsp_header.lump[LUMP_LIGHTING].nLength;
	visDataLength = bsp_header.lump[LUMP_VISIBILITY].nLength;

	if (!is_protected)
	{
		if (surfedgeCount > 0)
		{
			if (surfedges[surfedgeCount - 1] == 0)
			{
				is_protected = true;
			}
		}
	}
}

void Bsp::replace_lump(int lumpIdx, void* newData, size_t newLength)
{
	if (replacedLump[lumpIdx] && lumps[lumpIdx])
	{
		delete[] lumps[lumpIdx];
	}
	lumps[lumpIdx] = (unsigned char*)newData;
	bsp_header.lump[lumpIdx].nLength = (int)newLength;
	replacedLump[lumpIdx] = true;
	update_lump_pointers();
}

void Bsp::append_lump(int lumpIdx, void* newData, size_t appendLength)
{
	int oldLen = bsp_header.lump[lumpIdx].nLength;
	unsigned char* newLump = new unsigned char[oldLen + appendLength];

	memcpy(newLump, lumps[lumpIdx], oldLen);
	memcpy(newLump + oldLen, newData, appendLength);

	replace_lump(lumpIdx, newLump, oldLen + appendLength);
}

bool Bsp::isModelHasFaceIdx(const BSPMODEL& bspmdl, int faceid)
{
	if (faceid < bspmdl.iFirstFace)
		return false;
	if (faceid >= bspmdl.iFirstFace + bspmdl.nFaces)
		return false;
	return true;
}

bool Bsp::isModelHasLeafIdx(const BSPMODEL& bspmdl, int leafidx)
{
	if (leafidx < 0 || leafidx >= leafCount)
		return false;
	std::vector<int> visLeafs;
	modelLeafs(bspmdl, visLeafs);
	return std::find(visLeafs.begin(), visLeafs.end(), leafidx) != visLeafs.end();
}

int Bsp::merge_all_faces()
{
	int merged = 0;


	return merged;
}

void Bsp::ExportToObjWIP(const std::string& path, ExportObjOrder order, int iscale, bool lightmapmode)
{
	if (!createDir(path))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0193), path);
		return;
	}

	float scale = iscale < 0 ? 1.0f / iscale : 1.0f * iscale;

	if (iscale == 1)
		scale = 1.0f;

	scale = abs(scale);

	FILE* f = NULL;
	print_log(get_localized_string(LANG_0194), bsp_name + ".obj", path);
	print_log(get_localized_string(LANG_0195), iscale == 1 ? "scale" : iscale < 0 ? "downscale" : "upscale", abs(iscale));
	fopen_s(&f, (path + bsp_name + ".obj").c_str(), "wb");
	if (f)
	{
		fprintf(f, "# Exported using bspguy!\n");

		fprintf(f, "s off\n");

		fprintf(f, "mtllib %s.mtl\n", bsp_name.c_str());

		std::string groupname = std::string();

		BspRenderer* bsprend = renderer;

		bsprend->reload();

		createDir(path + "textures");
		std::vector<std::string> materials;
		std::vector<std::string> matnames;

		int vertoffset = 1;

		std::string materialid = std::string();
		std::string lastmaterialid = std::string();

		std::set<int> refreshedModels;

		for (int i = 0; i < faceCount; i++)
		{
			int mdlid = get_model_from_face(i);
			RenderFace* rface;
			RenderGroup* rgroup;

			if (refreshedModels.find(mdlid) == refreshedModels.end())
			{
				bsprend->refreshModel(mdlid, false, true);
				refreshedModels.insert(mdlid);
			}

			if (!bsprend->getRenderPointers(i, &rface, &rgroup))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0196));
				break;
			}

			BSPFACE32& face = faces[i];
			BSPTEXTUREINFO& texinfo = texinfos[face.iTextureInfo];
			int texOffset = ((int*)textures)[texinfo.iMiptex + 1];
			BSPMIPTEX tex = BSPMIPTEX();
			if (texOffset >= 0)
				tex = *((BSPMIPTEX*)(textures + texOffset));

			std::vector<size_t> entIds = get_model_ents_ids(mdlid);

			if (entIds.empty())
			{
				entIds.push_back(0);
			}

			materialid.clear();
			for (size_t m = 0; m < matnames.size(); m++)
			{
				if (matnames[m] == tex.szName)
					materialid = tex.szName;
			}
			if (materialid.empty())
			{
				materialid = tex.szName;
				materials.emplace_back("newmtl " + materialid);
				if (toLowerCase(tex.szName) == "aaatrigger" ||
					toLowerCase(tex.szName) == "null" ||
					toLowerCase(tex.szName) == "sky" ||
					toLowerCase(tex.szName) == "noclip" ||
					toLowerCase(tex.szName) == "clip" ||
					toLowerCase(tex.szName) == "origin" ||
					toLowerCase(tex.szName) == "bevel" ||
					toLowerCase(tex.szName) == "hint" ||
					toLowerCase(tex.szName) == "skip"
					)
				{
					materials.push_back("illum 4");
					materials.push_back("map_Kd " + std::string("textures/") + tex.szName + std::string(".bmp"));
					materials.push_back("map_d " + std::string("textures/") + tex.szName + std::string(".bmp"));
				}
				else
				{
					materials.push_back("map_Kd " + std::string("textures/") + tex.szName + std::string(".bmp"));
				}
				materials.push_back("");
				matnames.push_back(tex.szName);
			}

			if (!fileExists(path + std::string("textures/") + tex.szName + std::string(".bmp")))
			{
				if (tex.nOffsets[0] > 0)
				{
					if (texOffset >= 0)
					{
						COLOR3* imageData = ConvertMipTexToRGB(((BSPMIPTEX*)(textures + texOffset)), is_texture_with_pal(texinfo.iMiptex) ? NULL : (COLOR3*)g_settings.palette_data);

						for (int k = 0; k < tex.nHeight * tex.nWidth; k++)
						{
							std::swap(imageData[k].b, imageData[k].r);
						}
						//tga_write((path + tex->szName + std::string(".obj")).c_str(), tex->nWidth, tex->nWidth, (unsigned char*)tex + tex->nOffsets[0], 3, 3);
						WriteBMP(path + std::string("textures/") + tex.szName + std::string(".bmp"), (unsigned char*)imageData, tex.nWidth, tex.nHeight, 3);

						delete imageData;
					}
				}
				else
				{
					bool foundInWad = false;
					for (size_t r = 0; r < mapRenderers.size() && !foundInWad; r++)
					{
						for (size_t k = 0; k < mapRenderers[r]->wads.size(); k++)
						{
							if (mapRenderers[r]->wads[k]->hasTexture(tex.szName))
							{
								foundInWad = true;

								WADTEX* wadTex = mapRenderers[r]->wads[k]->readTexture(tex.szName);
								int lastMipSize = (wadTex->nWidth / 8) * (wadTex->nHeight / 8);
								COLOR3* palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + sizeof(short) - sizeof(BSPMIPTEX));
								unsigned char* src = wadTex->data;
								COLOR3* imageData = new COLOR3[wadTex->nWidth * wadTex->nHeight];

								int sz = wadTex->nWidth * wadTex->nHeight;

								for (int m = 0; m < sz; m++)
								{
									imageData[m] = palette[src[m]];
									std::swap(imageData[m].b, imageData[m].r);
								}

								WriteBMP(path + std::string("textures/") + tex.szName + std::string(".bmp"), (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight, 3);

								delete[] imageData;
								delete wadTex;
								break;
							}
						}
					}
				}
			}


			for (size_t e = 0; e < entIds.size(); e++)
			{
				size_t tmpentid = entIds[e];
				Entity* ent = ents[tmpentid];
				vec3 origin_offset = ent->getOrigin().flip();

				if ("Model_" + std::to_string(mdlid) + "_ent_" + std::to_string(tmpentid) != groupname)
				{
					groupname = "Model_" + std::to_string(mdlid) + "_ent_" + std::to_string(tmpentid);
					fprintf(f, "\no %s\n\n", groupname.c_str());
					fprintf(f, "\ng %s\n\n", groupname.c_str());
				}
				else
				{
					fprintf(f, "\n\n");
				}

				if (lastmaterialid != materialid)
					fprintf(f, "usemtl %s\n", materialid.c_str());

				lastmaterialid = materialid;

				for (int n = 0; n < rface->vertCount; n++)
				{
					lightmapVert& vert = ((lightmapVert*)rgroup->buffer->get_data())[rface->vertOffset + n];

					vec3 org_pos = vert.pos + origin_offset;

					fprintf(f, "v %f %f %f\n", org_pos.x * scale, org_pos.y * scale, org_pos.z * scale);
				}

				BSPPLANE tmpPlane = getPlaneFromFace(this, &face);

				for (int n = 0; n < rface->vertCount; n++)
				{
					fprintf(f, "vn %f %f %f\n", tmpPlane.vNormal.x, tmpPlane.vNormal.z, -tmpPlane.vNormal.y);
				}

				for (int n = 0; n < rface->vertCount; n++)
				{
					lightmapVert& vert = ((lightmapVert*)rgroup->buffer->get_data())[rface->vertOffset + n];
					//vec3 org_pos = vec3(vert.x + origin_offset.x, vert.y + origin_offset.z, vert.z + -origin_offset.y);
					//vec3 pos = vec3(org_pos.x, -org_pos.z, -org_pos.y);
					vec3 pos = vert.pos.flipUV();
					float fU = dotProduct(texinfo.vS, pos) + texinfo.shiftS;
					float fV = dotProduct(texinfo.vT, pos) + texinfo.shiftT;
					fU /= (float)tex.nWidth;
					fV /= -(float)tex.nHeight;
					fprintf(f, "vt %f %f\n", fU, fV);
				}

				fprintf(f, "%s", "\nf");
				for (int n = rface->vertCount - 1; n >= 0; n--)
				{
					int id = vertoffset + n;

					fprintf(f, " %i/%i/%i", id, id, id);
				}

				vertoffset += rface->vertCount;
				fprintf(f, "%s", "\n");

			}
		}

		FILE* fmat = NULL;
		fopen_s(&fmat, (path + bsp_name + ".mtl").c_str(), "wt");

		if (fmat)
		{
			for (auto const& s : materials)
			{
				fprintf(fmat, "%s\n", s.c_str());
			}
			fclose(fmat);
		}

		fclose(f);

		for (auto m : refreshedModels)
			bsprend->refreshModel(m, false);
	}
	else
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0197));
	}
}


void recurse_node_map(Bsp* map, int nodeIdx)
{
	if (nodeIdx < 0)
	{
		//BSPLEAF32& leaf = map->leaves[~nodeIdx];
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1038), ~nodeIdx);
		return;
	}

	recurse_node_map(map, map->nodes[nodeIdx].iChildren[0]);
	recurse_node_map(map, map->nodes[nodeIdx].iChildren[1]);
}

void Bsp::ExportPortalFile(const std::string& path)
{
	if (path.size() < 4)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0198));
		return;
	}
	std::string targetFileName = path.substr(0, path.size() - 4) + "X.prt";
	//std::string targetViewFileName = path.substr(0, path.size() - 4) + ".pts";
	std::ofstream targetFile(targetFileName, std::ios::trunc | std::ios::binary);
	if (!targetFile.is_open())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0199), targetFileName);
		return;
	}
	/*std::ofstream targetViewFile(targetFileName, std::ios::trunc | std::ios::binary);
	if (!targetFile.is_open())
	{
		print_log(get_localized_string(LANG_0200),targetViewFileName);
		return;
	}
	print_log(get_localized_string(LANG_0201),targetViewFileName);*/
	print_log(get_localized_string(LANG_0202), targetFileName);

	targetFile << fmt::format("{}\n", leafCount - 1);

	/*int nodeIdx = models[0].iHeadnodes[0];

	std::vector<NodeVolumeCuts> solidNodes;
	std::vector<BSPPLANE> planesx;
	get_node_leaf_cuts(nodeIdx,0, planesx, solidNodes);

	targetFile << fmt::format("{}\n", solidNodes.size());
	for (int i = 0; i < leafCount; i++)
	{
		targetFile << fmt::format("{}\n", 1);
	}
	for (int i = 0; i < solidNodes.size(); i++)
	{
		targetFile << fmt::format("{}\n", solidNodes[i].cuts.size(), solidNodes[i].nodeIdx);
	}
	targetFile.flush();*/
}
void Bsp::ExportLightFile(const std::string& path)
{
	if (path.size() < 4)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0203));
		return;
	}

	std::string targetMapFileName = path.substr(0, path.size() - 4);
	std::string targetFileName = targetMapFileName + ".lit";
	std::ofstream targetFile(targetFileName, std::ios::trunc | std::ios::binary);
	if (!targetFile.is_open())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0204), targetFileName);
		return;
	}
	int version = 1;
	targetFile.write("QLIT", 4);
	targetFile.write((const char*)&version, 4);
	targetFile.write((const char*)lightdata, lightDataLength);
}

void Bsp::ImportLightFile(const std::string& path)
{
	if (path.size() < 4)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0205));
		return;
	}

	std::string targetMapFileName = path.substr(0, path.size() - 4);
	std::string targetFileName = targetMapFileName + ".lit";
	std::ifstream targetFile(targetFileName, std::ios::binary);
	if (!targetFile.is_open())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0206), targetFileName);
		return;
	}
	char header[16]{};
	targetFile.read(header, 4);
	int version;
	targetFile.read((char*)&version, 4);

	if (version == 1 && header == std::string("QLIT"))
	{
		targetFile.read((char*)lightdata, lightDataLength);
	}
	else
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0207));
	}
}

void Bsp::ExportExtFile(const std::string& path)
{
	if (path.size() < 4)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0208));
		return;
	}

	std::string targetMapFileName = path.substr(0, path.size() - 4);
	std::string targetFileName = targetMapFileName + "_nolight.ext";
	std::ofstream targetFile(targetFileName, std::ios::trunc | std::ios::binary);
	if (!targetFile.is_open())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0209), targetFileName);
		return;
	}

	print_log(get_localized_string(LANG_0210), targetFileName);
	write(targetMapFileName + "_nolight.bsp");

	Bsp* tmpBsp = new Bsp(targetMapFileName + "_nolight.bsp");


	print_log(get_localized_string(LANG_0211));

	removeFile(targetMapFileName + "_nolight.bsp");

	print_log(get_localized_string(LANG_0212), targetMapFileName + "_nolight.bsp");

	tmpBsp->lumps[LUMP_LIGHTING] = NULL;
	tmpBsp->bsp_header.lump[LUMP_LIGHTING].nOffset = 0;
	tmpBsp->bsp_header.lump[LUMP_LIGHTING].nLength = 0;

	for (int i = 0; i < tmpBsp->faceCount; i++)
	{
		faces[i].nLightmapOffset = -1;
	}

	tmpBsp->update_lump_pointers();

	print_log(get_localized_string(LANG_0213), targetMapFileName);

	tmpBsp->write(targetMapFileName + "_nolight.bsp");

	print_log(get_localized_string(LANG_0214), targetFileName);

	targetFile << fmt::format("{}\n", faceCount);
	for (int i = 0; i < faceCount; i++)
	{
		int mins[2]; int maxs[2];
		GetFaceExtents(this, i, mins, maxs);
		targetFile << fmt::format("{} {} {} {}\n", mins[0], mins[1], maxs[0], maxs[1]);
	}

	print_log(get_localized_string(LANG_0215), targetMapFileName + "_nolight.wa_");
	Wad* tmpWad = new Wad();

	std::vector<std::string> addedTextures;
	std::vector<WADTEX*> outTextures;

	int missingTexures = 0;

	for (int i = 0; i < faceCount; i++)
	{
		BSPFACE32& face = faces[i];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		if (info.iMiptex >= 0 && info.iMiptex < textureCount)
		{
			int texOffset = ((int*)textures)[info.iMiptex + 1];
			if (texOffset >= 0)
			{
				BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
				if (tex.nOffsets[0] <= 0 && tex.szName[0] != '\0')
				{
					if (std::find(addedTextures.begin(), addedTextures.end(), tex.szName) != addedTextures.end())
					{
						continue;
					}
					WADTEX* texture = NULL;
					for (auto& wad : renderer->wads)
					{
						if (wad->hasTexture(tex.szName))
						{
							addedTextures.push_back(tex.szName);
							texture = wad->readTexture(tex.szName);
							outTextures.push_back(texture);
							break;
						}
					}
					if (!texture)
					{
						COLOR3* tmpColor = new COLOR3[tex.nWidth * tex.nHeight];
						memset(tmpColor, 255, tex.nWidth * tex.nHeight * sizeof(COLOR3));
						texture = create_wadtex(tex.szName, tmpColor, tex.nWidth, tex.nHeight);
						delete[] tmpColor;
						missingTexures++;

						addedTextures.push_back(tex.szName);
						outTextures.push_back(texture);
					}
				}
			}
		}
	}

	print_log(get_localized_string(LANG_0216), addedTextures.size() - missingTexures, missingTexures);

	tmpWad->write(targetMapFileName + "_nolight.wa_", outTextures);

	for (auto& tex : outTextures)
	{
		delete tex;
	}

	delete tmpWad;
	delete tmpBsp;
}


bool Bsp::ExportWad(const std::string& path)
{
	bool retval = true;
	if (textureCount > 0)
	{
		Wad* tmpWad = new Wad(path);
		std::vector<WADTEX*> tmpWadTex;
		for (int i = 0; i < textureCount; i++)
		{
			int oldOffset = ((int*)textures)[i + 1];
			if (oldOffset >= 0)
			{
				BSPMIPTEX* bspTex = (BSPMIPTEX*)(textures + oldOffset);
				if (bspTex->nOffsets[0] <= 0)
					continue;
				WADTEX* oldTex = new WADTEX(bspTex);
				tmpWadTex.push_back(oldTex);
			}
		}
		if (!tmpWadTex.empty())
		{
			tmpWad->write(path, tmpWadTex);
		}
		else
		{
			retval = false;
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0337));
		}
		tmpWadTex.clear();
		delete tmpWad;
	}
	else
	{
		retval = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0338));
	}
	return retval;
}

bool Bsp::ImportWad(const std::string& path)
{
	Wad* tmpWad = new Wad(path);

	if (!tmpWad->readInfo())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0339));
		delete tmpWad;
		return false;
	}
	else
	{
		for (int i = 0; i < (int)tmpWad->dirEntries.size(); i++)
		{
			WADTEX* wadTex = tmpWad->readTexture(i);
			COLOR3* imageData = ConvertWadTexToRGB(wadTex);
			if (is_bsp2 || is_bsp29)
			{
				add_texture(wadTex->szName, (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight);
			}
			else
			{
				add_texture(wadTex);
			}
			delete[] imageData;
			delete wadTex;
		}
		for (size_t i = 0; i < mapRenderers.size(); i++)
		{
			mapRenderers[i]->reloadTextures();
		}
	}

	delete tmpWad;
	return true;
}

struct ENTDATA
{
	int entid;
	std::vector<std::string> vecdata;
};

void Bsp::ExportToMapWIP(const std::string& path)
{
	if (!createDir(path))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1039), path);
		return;
	}
	FILE* f = NULL;
	print_log(get_localized_string(LANG_1040), (bsp_name + ".map"), path);
	fopen_s(&f, (path + bsp_name + ".map").c_str(), "wb");
	if (f)
	{
		fprintf(f, "// Exported using bspguy!\n");

		BspRenderer* bsprend = renderer;

		bsprend->reload();

		for (unsigned int entIdx = 0; entIdx < ents.size(); entIdx++)
		{
			int modelIdx = is_worldspawn_ent(entIdx) ? 0 : bsprend->renderEnts[entIdx].modelIdx;
			if (modelIdx < 0 || modelIdx > bsprend->numRenderModels)
				continue;

			for (int i = 0; i < bsprend->renderModels[modelIdx].groupCount; i++)
			{
				print_log(get_localized_string(LANG_0217), entIdx, modelIdx, i);
				//RenderGroup& rgroup = bsprend->renderModels[modelIdx].renderGroups[i];

			}
		}
		fclose(f);
	}
	else
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1041));
	}
}

BspRenderer* Bsp::getBspRender()
{
	if (!renderer && g_app)
		for (size_t i = 0; i < mapRenderers.size(); i++)
			if (mapRenderers[i]->map == this)
				renderer = mapRenderers[i];
	return renderer;
}

int Bsp::getBspRenderId()
{
	for (size_t i = 0; i < mapRenderers.size(); i++)
		if (mapRenderers[i]->map == this)
			return (int)i;
	return -1;
}

void Bsp::setBspRender(BspRenderer* rnd)
{
	renderer = rnd;
}

void Bsp::decalShoot(vec3 pos, const std::string& texname)
{
	if (!renderer || !renderer->faceMaths)
		return;

	Texture* tex = g_app->giveMeTexture(texname);
	if (!tex->uploaded)
		tex->upload(Texture::TYPE_DECAL);

	int bestMath = -1;
	float bestDist = 30.01f;

	for (int faceIdx = 0; faceIdx < faceCount; faceIdx++)
	{
		FaceMath& face = renderer->faceMaths[faceIdx];
		if (renderer->pickFaceMath(pos + (face.normal * 0.01f), face.normal * -0.01f, face, bestDist))
		{
			bestMath = faceIdx;
		}
	}

	int modelidx = get_model_from_face(bestMath);
	print_log(get_localized_string(LANG_0218), modelidx, bestMath, renderer->intersectVec.toKeyvalueString());

	// 
}


void Bsp::hideEnts(bool hide)
{
	if (!hide)
	{
		for (size_t i = 0; i < ents.size(); i++)
		{
			ents[i]->hide = false;
		}
	}
	else
	{
		for (auto& i : g_app->pickInfo.selectedEnts)
		{
			ents[i]->hide = true;
		}
	}
}

std::vector<int> Bsp::getLeafFaces(BSPLEAF32& leaf)
{
	std::vector<int> retFaces{};
	if (leaf.nMarkSurfaces <= 0 || leaf.iFirstMarkSurface < 0)
	{
		return retFaces;
	}

	if (leaf.nMarkSurfaces > 0)
	{
		retFaces.reserve(leaf.nMarkSurfaces);
		for (int i = 0; i < leaf.nMarkSurfaces; i++)
		{
			retFaces.push_back(marksurfs[leaf.iFirstMarkSurface + i]);
		}

		std::sort(retFaces.begin(), retFaces.end());
		retFaces.erase(std::unique(retFaces.begin(), retFaces.end()), retFaces.end());
	}

	return retFaces;
}

std::vector<int> Bsp::getLeafFaces(int leafIdx)
{
	std::vector<int> retFaces{};
	if (leafIdx < 0)
	{
		return retFaces;
	}

	BSPLEAF32& leaf = leaves[leafIdx];
	if (leaf.nMarkSurfaces <= 0 || leaf.iFirstMarkSurface < 0)
	{
		return retFaces;
	}

	if (leaf.nMarkSurfaces > 0)
	{
		retFaces.reserve(leaf.nMarkSurfaces);
		for (int i = 0; i < leaf.nMarkSurfaces; i++)
		{
			retFaces.push_back(marksurfs[leaf.iFirstMarkSurface + i]);
		}

		std::sort(retFaces.begin(), retFaces.end());
		retFaces.erase(std::unique(retFaces.begin(), retFaces.end()), retFaces.end());
	}
	return retFaces;
}

std::vector<int> Bsp::getFaceLeafs(int faceIdx)
{
	std::vector<int> retLeafes;

	for (int l = 1; l < leafCount; l++)
	{
		BSPLEAF32& leaf = leaves[l];

		for (int i = 0; i < leaf.nMarkSurfaces; i++)
		{
			if (marksurfs[leaf.iFirstMarkSurface + i] == faceIdx)
			{
				retLeafes.push_back(l);
			}
		}
	}

	return retLeafes;
}

int Bsp::getFaceFromPlane(int iPlane)
{
	for (int i = 0; i < faceCount; i++)
	{
		if (faces[i].iPlane == iPlane)
		{
			return i;
		}
	}
	return -1;
}

std::vector<int> Bsp::getFacesFromPlane(int iPlane)
{
	std::vector<int> retval;
	for (int i = 0; i < faceCount; i++)
	{
		if (faces[i].iPlane == iPlane)
		{
			retval.push_back(i);
		}
	}
	return retval;
}

int Bsp::getBspTextureSize(int textureid)
{
	if (textureid < 0 || textureid >= textureCount)
		return 0;

	int iStartOffset = ((int*)textures)[textureid + 1];

	if (iStartOffset < 0 || iStartOffset + (int)sizeof(BSPMIPTEX) > textureDataLength)
		return 0;

	BSPMIPTEX* tex = ((BSPMIPTEX*)(textures + iStartOffset));

	if (tex->nOffsets[0] > textureDataLength)
		return 0;

	int sz = sizeof(BSPMIPTEX);
	if (tex->nOffsets[0] > 0)
	{
		sz += sizeof(short); /* pal size */
		if (is_texture_with_pal(textureid))
		{
			sz += sizeof(COLOR3) * 256; // pallette
		}
		for (int i = 0; i < MIPLEVELS; i++)
		{
			sz += (tex->nWidth >> i) * (tex->nHeight >> i);
		}
	}
	return sz;
}

bool Bsp::is_texture_with_pal(int textureid)
{
	if (textureid < 0 || textureid >= textureCount)
		return false;


	int iStartOffset = ((int*)textures)[textureid + 1];
	if (iStartOffset >= 0)
	{
		BSPMIPTEX* tex = ((BSPMIPTEX*)(textures + iStartOffset));

		if (tex->nOffsets[0] <= 0) // wad texture
			return true;
	}

	return is_texture_pal;
}
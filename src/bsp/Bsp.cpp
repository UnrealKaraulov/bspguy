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

	logf("Loaded empty map.\n");

	renderer = NULL;
	bsp_valid = true;

	update_lump_pointers();

	load_ents();

	Entity* ent = new Entity();
	ent->setOrAddKeyvalue("mapversion", "220");
	ent->setOrAddKeyvalue("classname", "worldspawn");
	ents.push_back(ent);

	create_leaf(CONTENTS_EMPTY);
	update_lump_pointers();
	update_ent_lump();
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
}

void Bsp::selectModelEnt()
{
	if (!is_bsp_model || ents.empty())
		return;
	for (int i = 0; i < g_app->mapRenderers.size(); i++)
	{
		BspRenderer* mapRender = g_app->mapRenderers[i];
		if (!mapRender)
			continue;
		Bsp* map = mapRender->map;
		if (map && map == this->parentMap)
		{
			g_app->clearSelection();
			g_app->selectMap(map);
			vec3 worldOrigin = map->ents[0]->getOrigin();
			for (int n = 1; n < map->ents.size(); n++)
			{
				if (map->ents[n]->hasKey("model") && (map->ents[n]->getOrigin() + worldOrigin) == ents[0]->getOrigin())
				{
					g_app->pickInfo.SetSelectedEnt(n);
					return;
				}
			}
			return;
		}
	}
}

Bsp::Bsp()
{
	is_bsp_model = false;
	is_mdl_model = false;
	mdl = NULL;

	is_bsp30ext = false;
	is_bsp2 = false;
	is_bsp2_old = false;
	is_32bit_clipnodes = false;
	is_bsp29 = false;
	is_broken_clipnodes = false;
	is_blue_shift = false;
	is_colored_lightmap = true;

	force_skip_crc = false;

	bsp_header = BSPHEADER();
	bsp_header_ex = BSPHEADER_EX();
	parentMap = NULL;

	init_empty_bsp();
}

Bsp::Bsp(std::string fpath)
{
	is_bsp_model = false;
	is_mdl_model = false;
	mdl = NULL;

	is_bsp30ext = false;
	is_bsp2 = false;
	is_bsp2_old = false;
	is_32bit_clipnodes = false;
	is_bsp29 = false;
	is_broken_clipnodes = false;
	is_blue_shift = false;
	is_colored_lightmap = true;

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
				mdl = AddNewModelToRender(fpath.c_str());
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
		logf("ERROR: {} not found\n", fpath);
		return;
	}

	if (!load_lumps(fpath))
	{
		logf("{} is not a valid BSP file\n", fpath);
		return;
	}

	logf("(CRC \"{}\")\n", reverse_bits(originCrc32));

	std::string entFilePath;
	if (g_settings.sameDirForEnt) {
		entFilePath = fpath.substr(0, fpath.size() - 4) + ".ent";
	}
	else {
		entFilePath = GetWorkDir() + (bsp_name + ".ent");
	}

	if (g_settings.autoImportEnt && fileExists(entFilePath)) {
		logf("Import entities from: {}\n", entFilePath);

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
				logf("Removing CRC Hacking Model *{}\n", modelCount - 1);
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
			logf("Warning: in map {} found unused model: {}.\n", bsp_name, i);
		}
	}


	renderer = NULL;
	bsp_valid = true;


	if (!ents.empty() && !ents[0]->hasKey("CRC") && !force_skip_crc)
	{
		logf("Saving CRC key to Worldspawn\n");
		ents[0]->addKeyvalue("CRC", std::to_string(reverse_bits(originCrc32)));
		update_ent_lump();
	}

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		replacedLump[i] = false;
	}
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

	for (int i = 0; i < ents.size(); i++)
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
	for (int i = 0; i < nodePlaneIndexes.size(); i++)
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
	for (int i = 0; i < nodePlanes.size(); i++)
	{
		for (int k = 0; k < nodePlanes.size(); k++)
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
	for (int k = 0; k < nodePlanes.size(); k++)
	{
		if (!vertsAllOnOneSide(nodeVerts, nodePlanes[k]))
		{
			return false;
		}
	}

	for (int k = 0; k < nodeVerts.size(); k++)
	{
		vec3 v = nodeVerts[k];

		TransformVert hullVert;
		hullVert.pos = hullVert.undoPos = hullVert.startPos = v;
		hullVert.ptr = NULL;
		hullVert.selected = false;

		for (int i = 0; i < nodePlanes.size(); i++)
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
			logf("Detect stack overflowing! clipnode.iChildren[i] {} already processed!\n ", node.iChildren[i]);
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
			logf("Detect stack overflowing! node.iChildren[i] {} already processed!\n ", node.iChildren[i]);
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

	for (int i = 0; i < hullVerts.size(); i++)
	{
		for (int k = 0; k < hullVerts[i].iPlanes.size(); k++)
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
				logf("Face has less than 3 verts\n");
			return false; // invalid solid
		}

		BSPPLANE newPlane;
		if (!getPlaneFromVerts(tverts, newPlane.vNormal, newPlane.fDist))
		{
			if (g_settings.verboseLogs)
				logf("Verts not planar\n");
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

	//logf("UPDATED {} planes\n", planeUpdates);

	BSPMODEL& model = models[modelIdx];
	getBoundingBox(allVertPos, model.nMins, model.nMaxs);
	return true;
}

bool Bsp::move(vec3 offset, int modelIdx, bool onlyModel, bool forceMove, bool logged)
{
	if (modelIdx < 0 || modelIdx >= modelCount)
	{
		logf("Invalid modelIdx moved");
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

	bool hasLighting = lightDataLength > 0;
	LIGHTMAP* oldLightmaps = NULL;
	LIGHTMAP* newLightmaps = NULL;

	if (hasLighting)
	{
		if (logged)
			g_progress.update("Calculate lightmaps", faceCount);

		oldLightmaps = new LIGHTMAP[faceCount];
		newLightmaps = new LIGHTMAP[faceCount];
		memset(oldLightmaps, 0, sizeof(LIGHTMAP) * faceCount);
		memset(newLightmaps, 0, sizeof(LIGHTMAP) * faceCount);

		get_lightmaps(oldLightmaps, &target, logged);
	}

	if (logged)
		g_progress.update("Moving structures", (int)(ents.size() - 1));

	if (movingWorld)
	{
		for (int i = 1; i < ents.size(); i++)
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
		logf("\nWARNING: Model moved past safe world boundary!\n");
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
			logf("\nWARNING: Bounding box for node moved past safe world boundary!\n");
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
			logf("\nWARNING: Bounding box for leaf moved past safe world boundary!\n");
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
			logf("\nWARNING: Vertex moved past safe world boundary!\n");
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
			logf("\nWARNING: Plane origin moved past safe world boundary!\n");
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

	if (hasLighting && oldLightmaps && newLightmaps)
	{
		int newLightmapsSize = 0;
		COLOR3* newLightmapsBytes = NULL;
		resize_lightmaps(oldLightmaps, newLightmaps, &newLightmapsBytes, newLightmapsSize);

		if (newLightmapsBytes && newLightmapsSize > 0)
		{
			replace_lump(LUMP_LIGHTING, newLightmapsBytes, newLightmapsSize);
		}

		for (int i = 0; i < faceCount; i++)
		{
			if (oldLightmaps[i].luxelFlags)
			{
				delete[] oldLightmaps[i].luxelFlags;
			}
			if (newLightmaps[i].luxelFlags)
			{
				delete[] newLightmaps[i].luxelFlags;
			}
		}
		delete[] oldLightmaps;
		//delete[] newLightmaps;
	}

	if (logged)
		g_progress.clear();

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

void Bsp::get_lightmaps(LIGHTMAP* outLightmaps, BSPMODEL* target, bool logged)
{
	for (int i = 0; i < faceCount; i++)
	{
		int size[2];
		GetFaceLightmapSize(this, i, size);

		int lightmapSz = size[0] * size[1];
		int lightmapCount = lightmap_count(i);
		outLightmaps[i].layers = lightmapCount;
		lightmapSz *= lightmapCount;

		outLightmaps[i].width = size[0];
		outLightmaps[i].height = size[1];

		bool skipResize = target ? (i < target->iFirstFace || i >= target->iFirstFace + target->nFaces) : false;

		if (!skipResize)
		{
			outLightmaps[i].luxelFlags = new unsigned char[size[0] * size[1]];
			qrad_get_lightmap_flags(this, i, outLightmaps[i].luxelFlags);
		}
		if (logged)
			g_progress.tick();
	}
}

void Bsp::resize_lightmaps(LIGHTMAP* oldLightmaps, LIGHTMAP* newLightmaps, COLOR3** newLightData, int& newLightDataSize)
{
	g_progress.update("Recalculate lightmaps", faceCount);
	newLightDataSize = 0;
	*newLightData = NULL;
	// calculate new lightmap sizes
	int tmpLightDataSz = 0;
	int totalLightmaps = 0;
	int lightmapsResizeCount = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE32& face = faces[i];

		g_progress.tick();

		if (lightmap_count(i) == 0)
			continue;

		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		if (info.iMiptex < 0 || info.iMiptex >= textureCount)
			continue;

		int texOffset = ((int*)textures)[info.iMiptex + 1];
		if (texOffset < 0)
			continue;

		//BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		int size[2];
		GetFaceLightmapSize(this, i, size);

		int lightmapSz = size[0] * size[1];

		newLightmaps[i].width = size[0];
		newLightmaps[i].height = size[1];
		newLightmaps[i].layers = oldLightmaps[i].layers;

		tmpLightDataSz += (lightmapSz * newLightmaps[i].layers) * sizeof(COLOR3);

		totalLightmaps += newLightmaps[i].layers;
		if (oldLightmaps[i].width != newLightmaps[i].width || oldLightmaps[i].height != newLightmaps[i].height) {
			lightmapsResizeCount += newLightmaps[i].layers;
		}
	}

	if (lightmapsResizeCount > 0) {
		//logf("%d lightmap(s) to resize\n", lightmapsResizeCount);

		g_progress.update("Resize lightmaps", faceCount);

		int newColorCount = tmpLightDataSz / sizeof(COLOR3);
		COLOR3* tmpLightData = new COLOR3[newColorCount];
		memset(tmpLightData, 255, newColorCount * sizeof(COLOR3));
		int lightmapOffset = 0;

		for (int i = 0; i < faceCount; i++) {
			BSPFACE32& face = faces[i];

			g_progress.tick();

			if (lightmap_count(i) == 0) // no lighting
				continue;

			LIGHTMAP& oldLight = oldLightmaps[i];
			LIGHTMAP& newLight = newLightmaps[i];
			int oldLayerSz = (oldLight.width * oldLight.height) * sizeof(COLOR3);
			int newLayerSz = (newLight.width * newLight.height) * sizeof(COLOR3);
			int oldSz = oldLayerSz * oldLight.layers;
			int newSz = newLayerSz * newLight.layers;

			totalLightmaps++;

			bool faceMoved = oldLightmaps[i].luxelFlags != NULL;
			bool lightmapResized = oldLight.width != newLight.width || oldLight.height != newLight.height;

			if (!faceMoved || !lightmapResized) {
				memcpy((unsigned char*)tmpLightData + lightmapOffset, (unsigned char*)lightdata + face.nLightmapOffset, oldSz);
				newLight.luxelFlags = NULL;
			}
			else {
				newLight.luxelFlags = new unsigned char[newLight.width * newLight.height];
				qrad_get_lightmap_flags(this, i, newLight.luxelFlags);

				//int maxWidth = std::min(newLight.width, oldLight.width);
				// maxHeight = std::min(newLight.height, oldLight.height);

				int srcOffsetX, srcOffsetY;
				get_lightmap_shift(oldLight, newLight, srcOffsetX, srcOffsetY);

				for (int layer = 0; layer < newLight.layers; layer++) {
					int srcOffset = (face.nLightmapOffset + oldLayerSz * layer) / sizeof(COLOR3);
					int dstOffset = (lightmapOffset + newLayerSz * layer) / sizeof(COLOR3);

					int startX = newLight.width > oldLight.width ? -1 : 0;
					int startY = newLight.height > oldLight.height ? -1 : 0;

					for (int y = startY; y < newLight.height; y++) {
						for (int x = startX; x < newLight.width; x++) {
							int offsetX = x + srcOffsetX;
							int offsetY = y + srcOffsetY;

							int srcX = oldLight.width > newLight.width ? offsetX : x;
							int srcY = oldLight.height > newLight.height ? offsetY : y;
							int dstX = newLight.width > oldLight.width ? offsetX : x;
							int dstY = newLight.height > oldLight.height ? offsetY : y;

							srcX = std::max(0, std::min(oldLight.width - 1, srcX));
							srcY = std::max(0, std::min(oldLight.height - 1, srcY));
							dstX = std::max(0, std::min(newLight.width - 1, dstX));
							dstY = std::max(0, std::min(newLight.height - 1, dstY));

							COLOR3& src = ((COLOR3*)lightdata)[srcOffset + srcY * oldLight.width + srcX];
							COLOR3& dst = tmpLightData[dstOffset + dstY * newLight.width + dstX];

							dst = src;
						}
					}
				}
			}

			face.nLightmapOffset = lightmapOffset;
			lightmapOffset += newSz;
		}

		*newLightData = tmpLightData;
		newLightDataSize = lightmapOffset;
	}
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

	STRUCTREMAP remappedStuff(this);

	// TODO: handle all of these, assuming it's possible these are ever shared
	for (unsigned int i = 1; i < shouldNotMove.count.leaves; i++)
	{ // skip solid leaf - it doesn't matter
		if (shouldMove.leaves[i] && shouldNotMove.leaves[i])
		{
			logf("\nWarning: leaf shared with multiple models. Something might break.\n");
			break;
		}
	}
	for (unsigned int i = 0; i < shouldNotMove.count.nodes; i++)
	{
		if (shouldMove.nodes[i] && shouldNotMove.nodes[i])
		{
			logf("\nError: node shared with multiple models. Something will break.\n");
			break;
		}
	}
	for (unsigned int i = 0; i < shouldNotMove.count.verts; i++)
	{
		if (shouldMove.verts[i] && shouldNotMove.verts[i])
		{
			// this happens on activist series but doesn't break anything
			logf("\nError: vertex shared with multiple models. Something will break.\n");
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
		logf("\nShared model structures were duplicated to allow independent movement:\n");
		if (duplicatePlanes)
			logf("    Added {} planes\n", duplicatePlanes);
		if (duplicateClipnodes)
			logf("    Added {} clipnodes\n", duplicateClipnodes);
		if (duplicateTexinfos)
			logf("    Added {} texinfos\n", duplicateTexinfos);
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
	LumpState state = LumpState();

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if ((targets & (1 << i)) == 0)
		{
			state.lumps[i] = NULL;
			state.lumpLen[i] = 0;
			continue;
		}
		state.lumps[i] = new unsigned char[bsp_header.lump[i].nLength];
		state.lumpLen[i] = bsp_header.lump[i].nLength;
		memcpy(state.lumps[i], lumps[i], bsp_header.lump[i].nLength);
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

void Bsp::replace_lumps(LumpState& state)
{
	for (unsigned int i = 0; i < HEADER_LUMPS; i++)
	{
		if (!state.lumps[i])
		{
			continue;
		}

		delete[] lumps[i];
		lumps[i] = new unsigned char[state.lumpLen[i]];
		memcpy(lumps[i], state.lumps[i], state.lumpLen[i]);
		bsp_header.lump[i].nLength = state.lumpLen[i];

		if (i == LUMP_ENTITIES)
		{
			load_ents();
		}
	}

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
		logf("\nERROR: Invalid lump {} passed to remove_unused_structs\n", lumpIdx);
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
	decompress_vis_lump(oldLeaves, lumps[LUMP_VISIBILITY], decompressedVis,
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
				logf("Overflow decompressedVis!\n");
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

	logf("Debug. {} > {} \n", oldVisLeafCount, newVisLeafCount);

	int tmpLumpVisMemSize = bsp_header.lump[LUMP_VISIBILITY].nLength;

	int oldVisRowSize = ((oldVisLeafCount + 63) & ~63) >> 3;
	int newVisRowSize = ((newVisLeafCount + 63) & ~63) >> 3;
	int oldVisRowSize2 = ((oldVisLeafCount + 63 - 1) & ~63) >> 3;
	int newVisRowSize2 = ((newVisLeafCount + 63 - 1) & ~63) >> 3;

	logf("Debug. {} > {} or {} {} ? {} {} \n", oldVisRowSize, newVisRowSize, oldVisRowSize2, newVisRowSize2, oldVisLeafCount, newVisLeafCount);

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
				logf("Overflow decompressedVis!\n");
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
	std::vector<vec3> result_verts;

	for (int i = 0; i < edgeCount; i++)
	{
		bool found1 = false;
		bool found2 = false;
		for (int v = 0; v < vertCount; v++)
		{
			if (!found1 && VectorCompare(verts[edges[i].iVertex[0]], verts[v], epsilon))
			{
				edges[i].iVertex[0] = v;
				found1 = true;
			}
			if (!found2 && VectorCompare(verts[edges[i].iVertex[1]], verts[v], epsilon))
			{
				edges[i].iVertex[1] = v;
				found2 = true;
			}

			if (found1 && found2)
			{
				break;
			}
		}
	}
	return merged_verts;
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
			nodes[i].firstFace = remap.faces[nodes[i].firstFace];
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
		for (int i = 0; i < targetMap->ents.size(); i++)
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
		for (int i = 0; i < targetMap->ents.size(); i++)
		{
			targetMap->ents[i]->removeKeyvalue("wad");
		}
	}

	targetMap->update_ent_lump();
	targetMap->update_lump_pointers();
	logf("Added {} used wad names.\n", wads);
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

	for (int i = 0; i < ents.size(); i++)
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
		if (g_verbose)
			g_progress.update("", 0);
		else
			g_progress.update("Deleting unused hulls", modelCount - 1);
	}

	int deletedHulls = 0;

	for (int i = 1; i < modelCount; i++)
	{
		if (!g_verbose && !noProgress)
			g_progress.tick();

		std::vector<Entity*> usageEnts = get_model_ents(i);

		if (usageEnts.empty())
		{
			logf("Deleting unused model {}\n", i);

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
		for (int k = 0; k < usageEnts.size(); k++)
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
				logf("Deleting HULL 0 from model {}, used in {}\n", i, uses);

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
				logf("Deleting HULL 1-3 from model {}, used in {}\n", i, uses);

			model.iHeadnodes[1] = -1;
			model.iHeadnodes[2] = -1;
			model.iHeadnodes[3] = -1;
		}
		else if (!needsMonsterHulls)
		{
			if (models[i].iHeadnodes[2] >= 0)
				logf("Deleting HULL 2 from model {}, used in {}\n", i, uses);

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

	if (!g_verbose && !noProgress)
	{
		g_progress.clear();
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

	for (int i = 0; i < ents.size(); i++)
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

void Bsp::get_lightmap_shift(const LIGHTMAP& oldLightmap, const LIGHTMAP& newLightmap, int& srcOffsetX, int& srcOffsetY)
{
	int minWidth = std::min(newLightmap.width, oldLightmap.width);
	int minHeight = std::min(newLightmap.height, oldLightmap.height);

	int bestMatch = 0;
	int bestShiftCombo = 0;

	// Try different combinations of shifts to find the best alignment of the lightmaps.
	// Example (2 = unlit, 3 = lit)
	//  old         new
	// 3 3 3      2 3 3 3
	// 3 3 3  ->  2 3 3 3  =  old lightmap matches more luxels when it's shifted right 1 pixel in the new lightmap
	// 3 3 3      2 3 3 3
	// Only works for lightmap resizes caused by precision errors. Faces that are actually different sizes will
	// likely have more than 1 pixel of difference in either dimension.
	for (int t = 0; t < 4; t++)
	{
		int numMatch = 0;
		for (int y = 0; y < minHeight; y++)
		{
			for (int x = 0; x < minWidth; x++)
			{
				int offsetX = x;
				int offsetY = y;

				if (t == 1)
				{
					offsetX = x + 1;
				}
				if (t == 2)
				{
					offsetY = y + 1;
				}
				if (t == 3)
				{
					offsetX = x + 1;
					offsetY = y + 1;
				}

				int srcX = oldLightmap.width > newLightmap.width ? offsetX : x;
				int srcY = oldLightmap.height > newLightmap.height ? offsetY : y;
				int dstX = newLightmap.width > oldLightmap.width ? offsetX : x;
				int dstY = newLightmap.height > oldLightmap.height ? offsetY : y;

				srcX = std::max(0, std::min(oldLightmap.width - 1, srcX));
				srcY = std::max(0, std::min(oldLightmap.height - 1, srcY));
				dstX = std::max(0, std::min(newLightmap.width - 1, dstX));
				dstY = std::max(0, std::min(newLightmap.height - 1, dstY));

				int oldLuxelFlag = oldLightmap.luxelFlags[srcY * oldLightmap.width + srcX];
				int newLuxelFlag = newLightmap.luxelFlags[dstY * newLightmap.width + dstX];

				if (oldLuxelFlag == newLuxelFlag)
				{
					numMatch += 1;
				}
			}
		}

		if (numMatch > bestMatch)
		{
			bestMatch = numMatch;
			bestShiftCombo = t;
		}
	}

	int shouldShiftLeft = bestShiftCombo == 1 || bestShiftCombo == 3;
	int shouldShiftTop = bestShiftCombo == 2 || bestShiftCombo == 3;

	srcOffsetX = newLightmap.width != oldLightmap.width ? shouldShiftLeft : 0;
	srcOffsetY = newLightmap.height != oldLightmap.height ? shouldShiftTop : 0;
}

void Bsp::update_ent_lump(bool stripNodes)
{
	std::stringstream ent_data = std::stringstream();

	for (int i = 0; i < ents.size(); i++)
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

		for (int k = 0; k < ents[i]->keyOrder.size(); k++)
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
	memcpy(newEntData, str_data.c_str(), str_data.size());
	newEntData[str_data.size()] = 0; // null terminator required too(?)

	replace_lump(LUMP_ENTITIES, newEntData, str_data.size() + 1);
}

vec3 Bsp::get_model_center(int modelIdx)
{
	if (modelIdx < 0 || modelIdx > bsp_header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL))
	{
		logf("Invalid model index {}. Must be 0 - {}\n", modelIdx);
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
	for (int k = 0; k < MAXLIGHTMAPS; k++)
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
			logf("Failed to open backup file for writing:\n{}\n", path);
			return;
		}
		logf("Writing backup to {}\n", path + ".bak");

		file.write(oldfile, len);
		delete[] oldfile;
	}

	std::ofstream file(path, std::ios::trunc | std::ios::binary);
	if (!file.is_open())
	{
		logf("Failed to open BSP file for writing:\n{}\n", path);
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
				freeClipnodes16[n].iChildren[0] =
					(unsigned short)clipnodes[n].iChildren[0] > clipnodeCount ? 65536 - (unsigned short)clipnodes[n].iChildren[0] : clipnodes[n].iChildren[0];
				freeClipnodes16[n].iChildren[1] =
					(unsigned short)clipnodes[n].iChildren[1] > clipnodeCount ? 65536 - (unsigned short)clipnodes[n].iChildren[0] : clipnodes[n].iChildren[1];
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

			freenodes16[n].firstFace = (unsigned short)nodes[n].firstFace;
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

			freenodes32a[n].firstFace = nodes[n].firstFace;
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
			for (int m = 0; m < MAXLIGHTMAPS; m++)
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
			logf("SPOOFING CRC value.\nLoading original CRC key from WORLDSPAWN: {}. ",
				reverse_bits(originCrc32));
		}
		else
			logf("SPOOFING CRC value.\nOriginal crc: {}. ", reverse_bits(originCrc32));

		unsigned int crc32 = UINT32_C(0xFFFFFFFF);

		for (int i = 0; i < HEADER_LUMPS; i++)
		{
			if (i != LUMP_ENTITIES)
				crc32 = GetCrc32InMemory(lumps[i], bsp_header.lump[i].nLength, crc32);
		}

		logf("Current value: {}. ", reverse_bits(crc32));

		if (originCrc32 == crc32)
		{
			logf("Same values. Skip hacking.\n");
		}
		else
		{
			int originsize = bsp_header.lump[LUMP_MODELS].nLength;

			unsigned char* tmpNewModelds = new unsigned char[originsize + sizeof(BSPMODEL)];

			memset(tmpNewModelds, 0, originsize + sizeof(BSPMODEL));

			memcpy(tmpNewModelds, lumps[LUMP_MODELS], originsize);

			BSPMODEL* lastmodel = (BSPMODEL*)(tmpNewModelds + (modelCount * sizeof(BSPMODEL)));

			lastmodel->vOrigin.z = 9999.0f;

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

			logf("Hacked value: {}. \n", reverse_bits(crc32));
		}
	}

	logf("Writing {}\n", bsp_path);
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
				logf("Write extra lump {} size {} offset {} + {} align bytes\n", i, bsp_header_ex.lump[i].nLength, bsp_header_ex.lump[i].nOffset, padding);

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
			logf("Write lump {} size {} offset {} + {} align bytes\n", i, bsp_header.lump[i].nLength, bsp_header.lump[i].nOffset, padding);
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

	logf("Bsp version: {}\n", bsp_header.nVersion >= 0 && bsp_header.nVersion <= 100 ? std::to_string(bsp_header.nVersion)
		: std::string({ ((char*)&bsp_header.nVersion)[0],((char*)&bsp_header.nVersion)[1],((char*)&bsp_header.nVersion)[2],((char*)&bsp_header.nVersion)[3] }));

	if (bsp_header.nVersion == '2PSB')
	{
		is_bsp2 = true;
		logf("Found 'BSP2' map format.\n");
	}

	if (bsp_header.nVersion == 'BSP2')
	{
		is_bsp2 = true;
		is_bsp2_old = true;
		logf("Found old '2PSB' (bsp29a) map format\n");
	}

	if (bsp_header.nVersion == 29)
	{
		is_bsp29 = true;
		logf("Found 'BSP29' map format.\n");
	}

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		fin.read((char*)&bsp_header.lump[i], sizeof(BSPLUMP));
		if (g_settings.verboseLogs)
			logf("Lump id: {}. Len: {}. Offset {}.\n", i, bsp_header.lump[i].nLength, bsp_header.lump[i].nOffset);
	}

	fin.read((char*)&bsp_header_ex.id, sizeof(int));

	if (bsp_header_ex.id == 'HSAX' /* XASH */)
	{
		logf("Found 'BSP30ext' format from 'XASH' engine.\n");
		is_bsp30ext = true;

		fin.read((char*)&bsp_header_ex.nVersion, sizeof(int));


		int extralumpscount = bsp_header_ex.nVersion <= 3 ? EXTRA_LUMPS_OLD : EXTRA_LUMPS;
		logf("BSP30ext version {}. Extra lumps {}\n", bsp_header_ex.nVersion, extralumpscount);

		extralumps = new unsigned char* [EXTRA_LUMPS];
		memset(extralumps, 0, sizeof(unsigned char*) * EXTRA_LUMPS);

		for (int i = 0; i < extralumpscount; i++)
		{
			fin.read((char*)&bsp_header_ex.lump[i], sizeof(BSPLUMP));
			if (g_settings.verboseLogs)
				logf("Extra lump id: {}. Len: {}. Offset {}.\n", i, bsp_header_ex.lump[i].nLength, bsp_header_ex.lump[i].nOffset);
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
				logf("FAILED TO READ EXTRA BSP LUMP {}\n", i);
				is_bsp30ext = false;
				break;
			}

			fin.seekg(bsp_header_ex.lump[i].nOffset);
			if (fin.eof() || bsp_header_ex.lump[i].nOffset + bsp_header_ex.lump[i].nLength >= size)
			{
				logf("FAILED TO READ EXTRA BSP LUMP {}\n", i);
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
			logf("FAILED TO READ BSP LUMP {}\n", i);
			valid = false;
		}
		else
		{
			lumps[i] = new unsigned char[bsp_header.lump[i].nLength];
			fin.read((char*)lumps[i], bsp_header.lump[i].nLength);
		}
	}

	const char* classnametmp = "classname";

	if (bsp_header.lump[LUMP_PLANES].nLength < sizeof(BSPPLANE) ||
		&lumps[LUMP_PLANES][bsp_header.lump[LUMP_PLANES].nLength - 1] != std::search(&lumps[LUMP_PLANES][0], &lumps[LUMP_PLANES][bsp_header.lump[LUMP_PLANES].nLength - 1], &classnametmp[0], &classnametmp[strlen(classnametmp)]))
	{
		logf("Found 'BlueShift' v30 map format.\n");
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
						tmpnodes[n].firstFace = nodes16[n].firstFace;
						tmpnodes[n].iChildren[0] = nodes16[n].iChildren[0];
						tmpnodes[n].iChildren[1] = nodes16[n].iChildren[1];
						tmpnodes[n].iPlane = nodes16[n].iPlane;
						tmpnodes[n].nFaces = nodes16[n].nFaces;

						/*logf("Face {} child0 {} child0 {} plane {} face {} ", tmpnodes[n].firstFace, tmpnodes[n].iChildren[0], tmpnodes[n].iChildren[1], tmpnodes[n].iPlane
							, tmpnodes[n].nFaces);*/

						for (int m = 0; m < 3; m++)
						{
							tmpnodes[n].nMaxs[m] = (float)nodes16[n].nMaxs[m];
							tmpnodes[n].nMins[m] = (float)nodes16[n].nMins[m];

							//	logf("{} {} ", nodes16[n].nMaxs[m], nodes16[n].nMins[m]);
						}

						//logf("\n");
					}

					delete[] lumps[i];

					lumps[i] = (unsigned char*)tmpnodes;
					bsp_header.lump[i].nLength = nodeCount * sizeof(BSPNODE32);
				}
				else
				{
					nodeCount = bsp_header.lump[i].nLength / sizeof(BSPNODE32);
					logf("Using extended nodes [32 bit]\n");
				}
			}
			else
			{
				nodeCount = bsp_header.lump[i].nLength / sizeof(BSPNODE16);

				BSPNODE32* tmpnodes = new BSPNODE32[nodeCount];

				BSPNODE16* nodes16 = (BSPNODE16*)lumps[i];

				for (int n = 0; n < nodeCount; n++)
				{
					tmpnodes[n].firstFace = nodes16[n].firstFace;
					tmpnodes[n].iChildren[0] = nodes16[n].iChildren[0];
					tmpnodes[n].iChildren[1] = nodes16[n].iChildren[1];
					tmpnodes[n].iPlane = nodes16[n].iPlane;
					tmpnodes[n].nFaces = nodes16[n].nFaces;

					/*logf("Face {} child0 {} child0 {} plane {} face {} ", tmpnodes[n].firstFace, tmpnodes[n].iChildren[0], tmpnodes[n].iChildren[1], tmpnodes[n].iPlane
						, tmpnodes[n].nFaces);*/

					for (int m = 0; m < 3; m++)
					{
						tmpnodes[n].nMaxs[m] = (float)nodes16[n].nMaxs[m];
						tmpnodes[n].nMins[m] = (float)nodes16[n].nMins[m];

						//	logf("{} {} ", nodes16[n].nMaxs[m], nodes16[n].nMins[m]);
					}

					//logf("\n");
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
				logf("Using extended faces [32 bit]\n");
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
					for (int m = 0; m < MAXLIGHTMAPS; m++)
					{
						tmpfaces[n].nStyles[m] = faces16[n].nStyles[m];
					}
				}

				delete[] lumps[i];
				lumps[i] = (unsigned char*)tmpfaces;

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

				logf("Using extended clipnodes [32 bit]\n");
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
					logf("Using Arguire QBSP 'broken' clipnodes [16 unsigned bit]\n");
				}
				else
				{
					logf("Using default clipnodes [16 bit]\n");
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
					logf("Using extended leaves [32 bit]\n");
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

						//logf("Leaf iFirstMarkSurface {} nMarkSurfaces {} nContents {} nVisOffset {} \n", 
						//	tmpleaves[n].iFirstMarkSurface, tmpleaves[n].nMarkSurfaces, tmpleaves[n].nContents, tmpleaves[n].nVisOffset);
					}

					delete[] lumps[i];
					lumps[i] = (unsigned char*)tmpleaves;

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

				bsp_header.lump[i].nLength = leafCount * sizeof(BSPLEAF32);
			}
		}
		if (i == LUMP_MARKSURFACES)
		{
			if (is_bsp2)
			{
				marksurfCount = bsp_header.lump[i].nLength / sizeof(int);
				logf("Using extended marksurfaces [32 bit]\n");
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
				bsp_header.lump[i].nLength = marksurfCount * sizeof(int);
			}
		}

		if (i == LUMP_EDGES)
		{
			if (is_bsp2)
			{
				edgeCount = bsp_header.lump[i].nLength / sizeof(BSPEDGE32);
				logf("Using extended edges [32 bit]\n");
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

				bsp_header.lump[i].nLength = edgeCount * sizeof(BSPEDGE32);
			}
		}
	}

	update_lump_pointers();

	int iFirstFace = -1;
	int nOffseLight = 0;

	for (int i = faceCount - 1; i >= 0; i--)
	{
		BSPFACE32& face = faces[i];
		if (face.nLightmapOffset >= 0 && face.nLightmapOffset < bsp_header.lump[LUMP_LIGHTING].nLength)
		{
			iFirstFace = i;
			nOffseLight = face.nLightmapOffset;
			break;
		}
	}

	int iNextFace = -1;
	int nNextOffseLight = lightDataLength;

	for (int i = iFirstFace - 1; i >= 0; i--)
	{
		BSPFACE32& face = faces[i];
		if (face.nLightmapOffset >= 0 && face.nLightmapOffset < bsp_header.lump[LUMP_LIGHTING].nLength)
		{
			if (abs(face.nLightmapOffset - nOffseLight) < abs(nNextOffseLight - nOffseLight))
			{
				nNextOffseLight = face.nLightmapOffset;
				iNextFace = i;
			}
		}
	}

	float fLightSamples = is_bsp2 || is_bsp2_old || is_bsp29 ? 1.0f : 3.0f;
	//logf("{} {} {} -> {}\n", is_bsp2, is_bsp2_old, is_bsp29, fLightSamples);
	if (fLightSamples < 3.0 && iNextFace >= 0 && iFirstFace >= 0 && iFirstFace != iNextFace)
	{
		float face1light = GetFaceLightmapSizeBytes(this, iFirstFace) / (float)sizeof(COLOR3);
		float face2light = GetFaceLightmapSizeBytes(this, iNextFace) / (float)sizeof(COLOR3);

		int memsize = abs(nOffseLight - nNextOffseLight);

		if (iFirstFace > iNextFace)
		{
			fLightSamples = memsize / face1light;
		}
		else if (iNextFace > iFirstFace)
		{
			fLightSamples = memsize / face2light;
		}

		//logf("mem size : {} - {} - {} or {}\n", fLightSamples, memsize, face1light, face2light);

		if (abs(fLightSamples - (int)fLightSamples) > 0.01f)
		{
			memsize = (memsize + 3) & ~3;
			if (iFirstFace > iNextFace)
			{
				fLightSamples = memsize / face1light;
			}
			else if (iNextFace > iFirstFace)
			{
				fLightSamples = memsize / face2light;
			}
			//logf("mem size : {} - {} - {} or {}\n", fLightSamples, memsize, face1light, face2light);
		}
	}

	int tmp_is_colored_lightmap = (int)round(fLightSamples);
	if (tmp_is_colored_lightmap < 1)
		tmp_is_colored_lightmap = 1;

	is_colored_lightmap = tmp_is_colored_lightmap > 1;

	if (g_settings.verboseLogs)
		logf("lighting: {}\n", !is_colored_lightmap ? "monochrome" : "colored");

	if (!is_colored_lightmap)
	{
		int lightPixels = bsp_header.lump[LUMP_LIGHTING].nLength;

		COLOR3* newLight = new COLOR3[lightPixels];

		for (int m = 0; m < lightPixels; m++)
		{
			newLight[m] = COLOR3(lumps[LUMP_LIGHTING][m], lumps[LUMP_LIGHTING][m], lumps[LUMP_LIGHTING][m]);
		}

		delete lumps[LUMP_LIGHTING];

		lumps[LUMP_LIGHTING] = (unsigned char*)newLight;
		bsp_header.lump[LUMP_LIGHTING].nLength = lightPixels * sizeof(COLOR3);


		for (int n = 0; n < faceCount; n++)
		{
			faces[n].nLightmapOffset = faces[n].nLightmapOffset * sizeof(COLOR3);
		}
	}

	originCrc32 = crc32;

	fin.close();

	update_lump_pointers();
	return valid;
}

void Bsp::load_ents()
{
	for (int i = 0; i < ents.size(); i++)
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
				logf("{}.bsp ent data (line {}): Unexpected '{'\n", bsp_path, lineNum);
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
				logf("{}.bsp ent data (line {}): Unexpected '}'\n", bsp_path, lineNum);
			lastBracket = 1;
			if (!ent)
				continue;

			if (ent->keyvalues.count("classname"))
				ents.push_back(ent);
			else
				logf("Found unknown classname entity. Skip it.\n");

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
			for (int i = 0; i < k.keys.size(); i++)
			{
				ent->addKeyvalue(k.keys[i], k.values[i], true);
			}

			if (line.find('}') != std::string::npos)
			{
				lastBracket = 1;

				if (ent->keyvalues.count("classname"))
					ents.push_back(ent);
				else
					logf("Found unknown classname entity. Skip it.\n");
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
			logf("First entity has classname different from 'woldspawn', we do fixup it\n");
			for (int i = 1; i < ents.size(); i++)
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

	if (val > max)
	{
		print_color(PRINT_RED | PRINT_BRIGHT);
	}
	else if (percent >= 90)
	{
		print_color(PRINT_RED | PRINT_GREEN | PRINT_BRIGHT);
	}
	else if (percent >= 75)
	{
		print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE | PRINT_BRIGHT);
	}
	else
	{
		print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE);
	}

	logf("{:-12s}  ", name);
	if (isMem)
	{
		logf("{:8.2f} /{:5.2f} MB", val / meg, max / meg);
	}
	else
	{
		logf("{:8u} / {:-8u}", val, max);
	}
	logf(" {:6.1f}%", percent);

	if (val > max)
	{
		logf("  (OVERFLOW!!!)");
	}

	logf("\n");

	print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE);
}

void Bsp::print_model_stat(STRUCTUSAGE* modelInfo, unsigned int val, int max, bool isMem)
{
	std::string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	std::string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (int k = 0; k < ents.size(); k++)
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
		logf("{:8.1f} / {:-5.1f} MB", val / meg, max / meg);
	}
	else
	{
		logf("{:-26s} {:-26s} *{:-6d} {:9d}", classname, targetname, modelInfo->modelIdx, val);
	}
	if (percent >= 0.1f)
		logf("  {:6.1f}%", percent);

	logf("\n");
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
	return modelCount < (int)MAX_MAP_MODELS
		&& planeCount < (is_bsp2 ? INT_MAX : MAX_MAP_PLANES)
		&& vertCount < MAX_MAP_VERTS
		&& nodeCount < (is_bsp2 ? INT_MAX : (int)MAX_MAP_NODES)
		&& texinfoCount < (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS)
		&& faceCount < (is_bsp2 ? INT_MAX : MAX_MAP_FACES)
		&& clipnodeCount < (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : MAX_MAP_CLIPNODES)
		&& leafCount < (is_bsp2 ? INT_MAX : (int)MAX_MAP_LEAVES)
		&& marksurfCount < (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS)
		&& surfedgeCount < (is_bsp2 ? INT_MAX : (int)MAX_MAP_SURFEDGES)
		&& edgeCount < (is_bsp2 ? INT_MAX : (int)MAX_MAP_SURFEDGES)
		&& textureCount < (int)MAX_MAP_TEXTURES
		&& lightDataLength < (int)MAX_MAP_LIGHTDATA
		&& visDataLength < (int)MAX_MAP_VISDATA
		&& ents.size() < (int)MAX_MAP_ENTS;
}

bool Bsp::validate()
{
	bool isValid = true;

	for (int i = 0; i < marksurfCount; i++)
	{
		if (marksurfs[i] >= faceCount)
		{
			logf("Bad face reference in marksurf {}: {} / {}\n", i, marksurfs[i], faceCount);
			isValid = false;
		}
	}
	for (int i = 0; i < surfedgeCount; i++)
	{
		if (abs(surfedges[i]) >= edgeCount)
		{
			logf("Bad edge reference in surfedge {}: {} / {}\n", i, surfedges[i], edgeCount);
			isValid = false;
		}
	}
	for (int i = 0; i < texinfoCount; i++)
	{
		if (texinfos[i].iMiptex >= textureCount)
		{
			logf("Bad texture reference in textureinfo {}: {} / {}\n", i, texinfos[i].iMiptex, textureCount);
			isValid = false;
		}
	}
	for (int i = 0; i < faceCount; i++)
	{
		if (faces[i].iPlane >= planeCount)
		{
			logf("Bad plane reference in face {}: {} / {}\n", i, faces[i].iPlane, planeCount);
			isValid = false;
		}
		if (faces[i].nEdges > 0 && faces[i].iFirstEdge >= surfedgeCount)
		{
			logf("Bad surfedge reference in face {}: {} / {}\n", i, faces[i].iFirstEdge, surfedgeCount);
			isValid = false;
		}
		if (faces[i].iTextureInfo >= texinfoCount)
		{
			logf("Bad textureinfo reference in face {}: {} / {}\n", i, faces[i].iTextureInfo, texinfoCount);
			isValid = false;
		}
		if (lightDataLength > 0 &&
			faces[i].nLightmapOffset >= 0 && faces[i].nLightmapOffset > lightDataLength)
		{
			logf("Bad lightmap offset in face {}: {} / {}\n", i, faces[i].nLightmapOffset, lightDataLength);
			isValid = false;
		}
	}
	for (int i = 0; i < leafCount; i++)
	{
		if (leaves[i].nMarkSurfaces > 0 && leaves[i].iFirstMarkSurface >= marksurfCount)
		{
			logf("Bad marksurf reference in leaf {}: {} / {}\n", i, leaves[i].iFirstMarkSurface, marksurfCount);
			isValid = false;
		}
		if (visDataLength > 0 &&
			leaves[i].nVisOffset != -1 && (leaves[i].nVisOffset < 0 || leaves[i].nVisOffset >= visDataLength))
		{
			logf("Bad vis offset in leaf {}: {} / {}\n", i, leaves[i].nVisOffset, visDataLength);
			isValid = false;
		}

		for (int n = 0; n < 3; n++)
		{
			if (leaves[i].nMins[n] > leaves[i].nMaxs[n])
			{
				logf("backwards mins / maxs in leaf {} Mins: ({}, {}, {}) Maxs: ({} {} {})", i, leaves[i].nMins[0], leaves[i].nMins[1], leaves[i].nMins[2],
					leaves[i].nMaxs[0], leaves[i].nMaxs[1], leaves[i].nMaxs[2]);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < edgeCount; i++)
	{
		for (int k = 0; k < 2; k++)
		{
			if (edges[i].iVertex[k] >= vertCount)
			{
				logf("Bad vertex reference in edge {}: {} / {}\n", i, edges[i].iVertex[k], vertCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < nodeCount; i++)
	{
		if (nodes[i].nFaces > 0 && nodes[i].firstFace >= faceCount)
		{
			logf("Bad face reference in node {}: {} / {}\n", i, nodes[i].firstFace, faceCount);
			isValid = false;
		}
		if (nodes[i].iPlane >= planeCount)
		{
			logf("Bad plane reference in node {}: {} / {}\n", i, nodes[i].iPlane, planeCount);
			isValid = false;
		}
		for (int k = 0; k < 2; k++)
		{
			if (nodes[i].iChildren[k] != -1 && nodes[i].iChildren[k] > 0 && nodes[i].iChildren[k] >= nodeCount)
			{
				logf("Bad node reference in node {} child {}: {} / {}\n", i, k, nodes[i].iChildren[k], nodeCount);
				isValid = false;
			}
			else if (~nodes[i].iChildren[k] != -1 && nodes[i].iChildren[k] < 0 && ~nodes[i].iChildren[k] >= leafCount)
			{
				logf("Bad leaf reference in ~node {} child {}: {} / {}\n", i, k, ~nodes[i].iChildren[k], leafCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < clipnodeCount; i++)
	{
		if (clipnodes[i].iPlane < 0 || clipnodes[i].iPlane >= planeCount)
		{
			logf("Bad plane reference in clipnode {}: {} / {}\n", i, clipnodes[i].iPlane, planeCount);
			isValid = false;
		}
		for (int k = 0; k < 2; k++)
		{
			if (clipnodes[i].iChildren[k] > 0 && clipnodes[i].iChildren[k] >= clipnodeCount)
			{
				logf("Bad clipnode reference in clipnode {} child {}: {} / {}\n", i, k, clipnodes[i].iChildren[k], clipnodeCount);
				isValid = false;
			}
		}
	}
	for (int i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdxForce() > 0 && ents[i]->getBspModelIdxForce() >= modelCount)
		{
			logf("Bad model reference in entity {}: {} / {}\n", i, ents[i]->getBspModelIdxForce(), modelCount);
			isValid = false;
		}
	}


	int totalVisLeaves = 1; // solid leaf not included in model leaf counts
	int totalFaces = 0;
	for (int i = 0; i < modelCount; i++)
	{
		totalVisLeaves += models[i].nVisLeafs;
		totalFaces += models[i].nFaces;
		if (models[i].nFaces > 0 && (models[i].iFirstFace < 0 || models[i].iFirstFace >= faceCount))
		{
			logf("Bad face reference in model {}: {} / {}\n", i, models[i].iFirstFace, faceCount);
			isValid = false;
		}
		if (models[i].iHeadnodes[0] >= nodeCount)
		{
			logf("Bad node reference in model {} hull 0: {} / {}\n", i, models[i].iHeadnodes[0], nodeCount);
			isValid = false;
		}
		for (int k = 1; k < MAX_MAP_HULLS; k++)
		{
			if (models[i].iHeadnodes[k] >= clipnodeCount)
			{
				logf("Bad clipnode reference in model {} hull {}: {} / {}\n", i, k, models[i].iHeadnodes[k], clipnodeCount);
				isValid = false;
			}
		}
		if (models[i].nMins.x > models[i].nMaxs.x ||
			models[i].nMins.y > models[i].nMaxs.y ||
			models[i].nMins.z > models[i].nMaxs.z)
		{
			logf("Backwards mins/maxs in model {}. Mins: ({}, {}, {}) Maxs: ({} {} {})\n", i,
				models[i].nMins.x, models[i].nMins.y, models[i].nMins.z,
				models[i].nMaxs.x, models[i].nMaxs.y, models[i].nMaxs.z);
			isValid = false;
		}
	}
	if (totalVisLeaves != leafCount)
	{
		logf("Bad model vis leaf sum: {} / {}\n", totalVisLeaves, leafCount);
		isValid = false;
	}

	if (totalFaces > faceCount)
	{
		logf("Bad model face sum: {} / {}\n", totalFaces, faceCount);
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
		logf("Found {} worldspawn entities (expected 1).All ents:{}\nThis can cause crashes and svc_bad errors.\n", worldspawn_count, ents.size());
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
			logf("Warning: in map {} found unused model: {}.\n", bsp_name, i);
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
			if (tex->szName[0] == '\0' || strlen(tex->szName) >= MAXTEXTURENAME)
			{
				logf("Warning: invalid texture name in {} texture.\n", i);
			}
			if (tex->nOffsets[0] > 0 && dataOffset + texOffset + texlen > bsp_header.lump[LUMP_TEXTURES].nLength)
			{
				logf("Warning: texture data buffer overrun in {} texture.\n", i);
			}
		}
	}

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
			logf("Unable to show model stats while BSP limits are exceeded.\n");
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

		logf("       Classname                  Targetname          Model  {:-10s}  Usage\n", countName);
		logf("-------------------------  -------------------------  -----  ----------  --------\n");

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
		logf(" Data Type     Current / Max       Fullness\n");
		logf("------------  -------------------  --------\n");
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
	recurse_node(node, 0);
}

void Bsp::print_clipnode_tree(int iNode, int depth)
{
	for (int i = 0; i < depth; i++)
	{
		logf("    ");
	}

	if (iNode < 0)
	{
		logf(getLeafContentsName(iNode));
		logf("\n");
		return;
	}
	else
	{
		BSPPLANE& plane = planes[clipnodes[iNode].iPlane];
		logf("NODE ({:.2f}, {:.2f}, {:.2f} @ {:.2}\n", plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist);
	}


	for (int i = 0; i < 2; i++)
	{
		print_clipnode_tree(clipnodes[iNode].iChildren[i], depth + 1);
	}
}

void Bsp::print_model_hull(int modelIdx, int hull_number)
{
	if (modelIdx < 0 || modelIdx > bsp_header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL))
	{
		logf("Invalid model index {}. Must be 0 - {}\n", modelIdx);
		return;
	}

	// the first hull is used for point-sized clipping, but uses nodes and not clipnodes.
	if (hull_number < 0 || hull_number >= MAX_MAP_HULLS)
	{
		logf("Invalid hull number. Clipnode hull numbers are 0 - {}\n", MAX_MAP_HULLS);
		return;
	}

	BSPMODEL& model = models[modelIdx];

	logf("Model {} Hull {} - {}\n", modelIdx, hull_number, get_model_usage(modelIdx));

	if (hull_number == 0)
		print_model_bsp(modelIdx);
	else
		print_clipnode_tree(model.iHeadnodes[hull_number], 0);
}

std::string Bsp::get_model_usage(int modelIdx)
{
	for (int i = 0; i < ents.size(); i++)
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
	for (int i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdx() == modelIdx)
		{
			uses.push_back(ents[i]);
		}
	}
	return uses;
}

std::vector<int> Bsp::get_model_ents_ids(int modelIdx)
{
	std::vector<int> uses;
	for (int i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdxForce() == modelIdx)
		{
			uses.push_back(i);
		}
	}
	return uses;
}

void Bsp::recurse_node(int nodeIdx, int depth)
{
	for (int i = 0; i < depth; i++)
	{
		logf("    ");
	}

	if (nodeIdx < 0)
	{
		BSPLEAF32& leaf = leaves[~nodeIdx];
		print_leaf(leaf);
		logf(" (LEAF {})\n", ~nodeIdx);
		return;
	}
	else
	{
		print_node(nodes[nodeIdx]);
		logf("\n");
	}

	recurse_node(nodes[nodeIdx].iChildren[0], depth + 1);
	recurse_node(nodes[nodeIdx].iChildren[1], depth + 1);
}

void Bsp::print_node(const BSPNODE32& node)
{
	BSPPLANE& plane = planes[node.iPlane];

	logf("Plane ({} {} {}) d: {}, Faces: {}, Min({}, {}, {}), Max({}, {}, {})",
		plane.vNormal.x, plane.vNormal.y, plane.vNormal.z,
		plane.fDist, node.nFaces,
		node.nMins[0], node.nMins[1], node.nMins[2],
		node.nMaxs[0], node.nMaxs[1], node.nMaxs[2]);
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
		logf("Warning! Found bad surface. Skipping.\n");
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
		logf("Warning! Found bad node. Skipping.\n");
		return;
	}
	BSPNODE32& node = nodes[iNode];

	usage->nodes[iNode] = true;
	usage->planes[node.iPlane] = true;

	for (int i = 0; i < node.nFaces; i++)
	{
		mark_face_structures(node.firstFace + i, usage);
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
		logf("Warning! Found bad clipnode. Skipping.\n");
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
		logf("Warning! Found bad model. Skipping.\n");
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
		logf("Warning! Found bad face. Skipping.\n");
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
	//logf("REMAP FACE {}: {} -> {}\n", faceIdx, face.iFirstEdge, remap->surfEdges[face.iFirstEdge]);
	//logf("REMAP FACE {}: {} -> {}\n", faceIdx, face.iTextureInfo, remap->texInfo[face.iTextureInfo]);
	//face.iFirstEdge = remap->surfEdges[face.iFirstEdge];
}

void Bsp::remap_node_structures(int iNode, STRUCTREMAP* remap)
{
	if (iNode > nodeCount)
	{
		logf("Warning! Found bad node. Skipping.\n");
		return;
	}
	BSPNODE32& node = nodes[iNode];

	remap->visitedNodes[iNode] = true;

	node.iPlane = remap->planes[node.iPlane];

	for (int i = 0; i < node.nFaces; i++)
	{
		remap_face_structures(node.firstFace + i, remap);
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
		logf("Warning! Found bad clipnode. Skipping.\n");
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
		logf("Warning! Found bad model. Skipping.\n");
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
		logf("Invalid hull number. Valid hull numbers are 1-{}\n", MAX_MAP_HULLS);
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
		logf("Invalid model index {}. Must be 0-{}\n", modelIdx);
		return;
	}

	// the first hull is used for point-sized clipping, but uses nodes and not clipnodes.
	if (hull_number < 0 || hull_number >= MAX_MAP_HULLS)
	{
		logf("Invalid hull number. Valid hull numbers are 1-{}\n", MAX_MAP_HULLS);
		return;
	}

	if (redirect >= MAX_MAP_HULLS)
	{
		logf("Invalid redirect hull number. Valid redirect hulls are 1-{}\n", MAX_MAP_HULLS);
		return;
	}

	if (hull_number == 0 && redirect > 0)
	{
		logf("Hull 0 can't be redirected. Hull 0 is the only hull that doesn't use clipnodes.\n", MAX_MAP_HULLS);
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
			//logf("WARNING: HULL {} is empty\n", redirect);
		}
		else if (model.iHeadnodes[hull_number] == model.iHeadnodes[redirect])
		{
			//logf("WARNING: HULL {} and {} are already sharing clipnodes\n", hull_number, redirect);
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
	for (int i = 0; i < ents.size(); i++)
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

	logf("");
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
		logf("add_texture: bad texture name\n");
		return -1;
	}
	char name[MAXTEXTURENAME];
	memset(name, 0, MAXTEXTURENAME);
	memcpy(name, oldname, std::min(MAXTEXTURENAME, (int)strlen(oldname)));

	logf("Adding new {} texture '{}' with size {}x{}\n", !data ? "embedded" : "wad", name, width, height);

	if (width % 16 != 0 || height % 16 != 0)
	{
		logf("Dimensions not divisible by 16\n");
		return -1;
	}

	if (width > (int)MAX_TEXTURE_DIMENSION || height > (int)MAX_TEXTURE_DIMENSION)
	{
		logf("Width/height too large\n");
		return -1;
	}

	int oldtexid = 0;
	BSPMIPTEX* oldtex = find_embedded_texture(name, oldtexid);

	bool only_copy_data = false;

	// internal, with data
	if (oldtex)
	{
		logf("Embedded texture with name {} found in map.\n", name);
		if (oldtex->nWidth != width || oldtex->nHeight != height)
		{
			if (!data)
			{
				logf("Same embedded texture with another size found in map.\nJust clear offsets, update size and return index.\n");
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
				logf("Warning! Texture size different {}x{} > {}x{}.\nRenaming old texture and create new one.\n",
					oldtex->nWidth, oldtex->nHeight, width, height);
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
			}
		}
		else if (data)
		{
			only_copy_data = true;
			logf("Internal texture found with same size. Just update data.\n");
		}
		else
		{
			logf("Same embedded texture with same size found in map.\nJust clear offsets and return index.\n");
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
			logf("Wad texture with name {} found in map.\n", name);
			if (oldtex->nWidth != width || oldtex->nHeight != height)
			{
				if (!data)
				{
					logf("Same wad texture with size different {}x{} > {}x{} found in map.\nJust update size and return index.\n",
						oldtex->nWidth, oldtex->nHeight, width, height);

					oldtex->nWidth = width;
					oldtex->nHeight = height;
					return oldtexid;
				}
				else
				{
					oldtex->szName[0] = '\0';
					logf("Warning! Texture size different {}x{} > {}x{}.\nRenaming old texture and create new one.\n",
						oldtex->nWidth, oldtex->nHeight, width, height);
					oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
						oldtex->nOffsets[3] = 0;
				}
			}
			else if (!data)
			{
				logf("Same wad texture with same size found in map, return index.\n");
				return oldtexid;
			}
			else
			{
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
				oldtex->szName[0] = '\0';
				logf("Warning! Wad texture with same name found in map.\nNeed replace by new texture.\n");
			}
		}
	}


	int texDataSize = 0;
	COLOR3 palette[256];
	memset(&palette, 0, sizeof(COLOR3) * 256);
	int colorCount = 0;


	if (!is_bsp2 && !is_bsp29 && !force_quake_pal)
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
		if (is_bsp2 || is_bsp29 || force_quake_pal || custompal)
		{
			if (custompal)
			{
				memcpy(palette, custompal, 256 * sizeof(COLOR3));
				colorCount = 256;
			}
			if (is_bsp2 || is_bsp29 || force_quake_pal)
			{
				if (!custompal)
					memcpy(palette, quakeDefaultPalette, 256 * sizeof(COLOR3));
				Quantizer* tmpCQuantizer = new Quantizer(256, 8);
				tmpCQuantizer->SetColorTable(palette, 256);
				tmpCQuantizer->ApplyColorTable((COLOR3*)data, width * height);
				delete tmpCQuantizer;
				colorCount = 256;
			}
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
						logf("Too many colors\n");
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

		if (!is_bsp2 && !is_bsp29 && !force_quake_pal)
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

		if (!is_bsp2 && !is_bsp29 && !force_quake_pal)
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
	//logf("Adding new wad texture '{}' with size {}x{} {} {} {} {}\n", tex->szName, tex->nWidth, tex->nHeight, tex->nOffsets[0], tex->nOffsets[1], tex->nOffsets[2], tex->nOffsets[3]);
	logf("Adding new wad texture '{}' with size {}x{}\n", tex->szName, tex->nWidth, tex->nHeight);

	if (tex->nWidth % 16 != 0 || tex->nHeight % 16 != 0)
	{
		logf("Dimensions not divisible by 16\n");
		return -1;
	}

	if (tex->nWidth > (int)MAX_TEXTURE_DIMENSION || tex->nHeight > (int)MAX_TEXTURE_DIMENSION)
	{
		logf("Width/height too large\n");
		return -1;
	}

	int oldtexid = -1;
	BSPMIPTEX* oldtex = find_embedded_texture(tex->szName, oldtexid);

	bool only_copy_data = false;

	if (oldtex)
	{
		logf("Embedded texture with name {} found in map.\n", tex->szName);
		if (oldtex->nWidth != tex->nWidth || oldtex->nHeight != tex->nHeight)
		{
			oldtex->szName[0] = '\0';
			logf("Warning! Texture size different {}x{} > {}x{}.\nRenaming old texture and create new one.\n",
				oldtex->nWidth, oldtex->nHeight, tex->nWidth, tex->nHeight);
			oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
				oldtex->nOffsets[3] = 0;
		}
		else
		{
			only_copy_data = true;
			logf("Internal texture found with same size. Just update data.\n");
		}
	}
	else
	{
		oldtexid = -1;
		oldtex = find_embedded_wad_texture(tex->szName, oldtexid);

		// external without data
		if (oldtex)
		{
			logf("Wad texture with name {} found in map.\n", tex->szName);
			if (oldtex->nWidth != tex->nWidth || oldtex->nHeight != tex->nHeight)
			{
				oldtex->szName[0] = '\0';
				logf("Warning! Texture size different {}x{} > {}x{}.\nRenaming old texture and create new one.\n",
					oldtex->nWidth, oldtex->nHeight, tex->nWidth, tex->nHeight);
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
			}
			else
			{
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
				oldtex->szName[0] = '\0';
				logf("Warning! Wad texture with same name found in map.\nNeed replace by new texture.\n");
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
		for (int i = 0; i < texinfoCount; i++)
		{
			BSPTEXTUREINFO& texinfo = texinfos[i];
			if (texinfo.iMiptex == oldtexid)
			{
				texinfo.iMiptex = textureCount;
			}
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
			face.nLightmapOffset = -1; // TODO: Lighting
			memset(face.nStyles, 255, 4);
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

			node.firstFace = (startFace + k); // face required for decals
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
				newSurfedges[startSurfedge + idx++] = vertToSurfedge[tmpFace.verts[k]];
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
			face.nLightmapOffset = -1; // TODO: Lighting
			memset(face.nStyles, 255, 4);
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

		for (int k = 0; k < solid.faces.size(); k++)
		{
			BSPNODE32& node = newNodes[nodeCount + k];

			node.firstFace = (startFace + k); // face required for decals
			node.nFaces = 1;
			node.iPlane = startPlane + k;
			// node mins/maxs don't matter for submodels. Leave them at 0.

			int insideContents = k == solid.faces.size() - 1 ? ~sharedSolidLeaf : (nodeCount + k + 1);
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
	for (int i = 0; i < solid.hullVerts.size(); i++)
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
	memcpy(newPlanes + planeCount, &addPlanes[0], addPlanes.size() * sizeof(BSPPLANE));
	replace_lump(LUMP_PLANES, newPlanes, (planeCount + addPlanes.size()) * sizeof(BSPPLANE));

	BSPCLIPNODE32* newClipnodes = new BSPCLIPNODE32[clipnodeCount + addNodes.size()];
	memcpy(newClipnodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE32));
	memcpy(newClipnodes + clipnodeCount, &addNodes[0], addNodes.size() * sizeof(BSPCLIPNODE32));
	replace_lump(LUMP_CLIPNODES, newClipnodes, (clipnodeCount + addNodes.size()) * sizeof(BSPCLIPNODE32));

	return solidNodeIdx;
}

void Bsp::simplify_model_collision(int modelIdx, int hullIdx)
{
	if (modelIdx < 0 || modelIdx >= modelCount)
	{
		logf("Invalid model index {}. Must be 0-{}\n", modelIdx);
		return;
	}
	if (hullIdx >= MAX_MAP_HULLS)
	{
		logf("Invalid hull number. Valid hull numbers are 1-{}\n", MAX_MAP_HULLS);
		return;
	}

	BSPMODEL& model = models[modelIdx];

	if (model.iHeadnodes[1] < 0 && model.iHeadnodes[2] < 0 && model.iHeadnodes[3] < 0)
	{
		logf("Model has no clipnode hulls left to simplify\n");
		return;
	}

	if (hullIdx > 0 && model.iHeadnodes[hullIdx] < 0)
	{
		logf("Hull {} has no clipnodes\n", hullIdx);
		return;
	}

	if (model.iHeadnodes[0] < 0)
	{
		logf("Hull 0 was deleted from this model. Can't simplify.\n");
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
	std::vector<BSPCLIPNODE32>& newClipnodes)
{
	STRUCTUSAGE usage(this);
	mark_model_structures(modelIdx, &usage, true);

	for (unsigned int i = 0; i < usage.count.planes; i++)
	{
		if (usage.planes[i])
		{
			remap.planes[i] = targetMap->planeCount + (int)newPlanes.size();
			newPlanes.push_back(planes[i]);
		}
	}

	for (unsigned int i = 0; i < usage.count.verts; i++)
	{
		if (usage.verts[i])
		{
			remap.verts[i] = targetMap->vertCount + (int)newVerts.size();
			newVerts.push_back(verts[i]);
		}
	}

	for (unsigned int i = 0; i < usage.count.edges; i++)
	{
		if (usage.edges[i])
		{
			remap.edges[i] = targetMap->edgeCount + (int)newEdges.size();

			BSPEDGE32 edge = edges[i];
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

			int surfedge = remap.edges[abs(surfedges[i])];
			if (surfedges[i] < 0)
				surfedge = -surfedge;
			newSurfedges.push_back(surfedge);
		}
	}

	for (unsigned int i = 0; i < usage.count.texInfos; i++)
	{
		if (usage.texInfo[i])
		{
			remap.texInfo[i] = targetMap->texinfoCount + (int)newTexinfo.size();
			newTexinfo.push_back(texinfos[i]);
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
				if (is_bsp2 || is_bsp29)
				{
					COLOR3* lightmapSrc = (COLOR3*)(lightdata + face.nLightmapOffset);
					for (int k = 0; k < lightmapSz; k++)
					{
						newLightmaps.push_back(lightmapSrc[k]);
					}
				}
			}

			face.nLightmapOffset = lightmapCount != 0 && face.nLightmapOffset >= 0 ? targetMap->lightDataLength + lightmapAppendSz : -1;
			newFaces.push_back(face);

			lightmapAppendSz += lightmapSz * sizeof(COLOR3);
		}
	}

	for (unsigned int i = 0; i < usage.count.nodes; i++)
	{
		if (usage.nodes[i])
		{
			remap.nodes[i] = targetMap->nodeCount + (int)newNodes.size();
			newNodes.push_back(nodes[i]);
		}
	}

	for (size_t i = 0; i < newNodes.size(); i++)
	{
		BSPNODE32& node = newNodes[i];
		node.firstFace = remap.faces[node.firstFace];
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
			newClipnodes.push_back(clipnodes[i]);
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

	STRUCTREMAP remap(this);

	copy_bsp_model(modelIdx, this, remap, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces, newLightmaps, newNodes, newClipnodes);

	if (newClipnodes.size())
		append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE32) * newClipnodes.size());
	if (newEdges.size())
		append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE32) * newEdges.size());
	if (newFaces.size())
		append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE32) * newFaces.size());
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
		append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());

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
	newModel.nVisLeafs = 0; // techinically should match the old model, but leaves aren't duplicated yet

	return newModelIdx;
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
			logf("Create new texinfo {}\n", newInfo);
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

	for (int i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdx() == modelIdx)
			return i;
	}

	if (modelIdx == 0)
	{
		for (int i = 0; i < ents.size(); i++)
		{
			if (ents[i]->isWorldSpawn())
				return i;
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

bool Bsp::is_worldspawn_ent(int entIdx)
{
	if (entIdx < 0 || entIdx >= ents.size())
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
				logf("UNEXPECTED SOLID CONTENTS {}\n", solidContents);
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
	logf("Wrote {} planes\n", numPlanes);

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
				logf("BSP HAS {} PLANES\n", count);
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
				logf("Writing face ({:2.0f} {:2.0f} {:2.0f}) {:4.0f}  {}\n",
					plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist,
					(faceContents == CONTENTS_SOLID ? "SOLID" : "EMPTY"));
				if (flipped && false)
				{
					logf(" (flipped)");
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
			logf("\n");
	}
}

void Bsp::print_leaf(const BSPLEAF32& leaf)
{
	logf(getLeafContentsName(leaf.nContents));
	logf(" {} surfs, Min({}, {}, {}), Max({} {} {})", leaf.nMarkSurfaces,
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

	if (planeCount > (is_bsp2 ? INT_MAX : MAX_MAP_PLANES))
		logf("Overflowed Planes !!!\n");
	if (texinfoCount > (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS))
		logf("Overflowed texinfos !!!\n");
	if (leafCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_LEAVES))
		logf("Overflowed leaves !!!\n");
	if (modelCount > (int)MAX_MAP_MODELS)
		logf("Overflowed models !!!\n");
	if (texinfoCount > (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS))
		logf("Overflowed texinfos !!!\n");
	if (nodeCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_NODES))
		logf("Overflowed nodes !!!\n");
	if (vertCount > (is_bsp2 ? INT_MAX : MAX_MAP_VERTS))
		logf("Overflowed verts !!!\n");
	if (faceCount > (is_bsp2 ? INT_MAX : MAX_MAP_FACES))
		logf("Overflowed faces !!!\n");
	if (clipnodeCount > (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : MAX_MAP_CLIPNODES))
		logf("Overflowed clipnodes !!!\n");
	if (marksurfCount > (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS))
		logf("Overflowed marksurfs !!!\n");
	if (surfedgeCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_SURFEDGES))
		logf("Overflowed surfedges !!!\n");
	if (edgeCount > (is_bsp2 ? INT_MAX : (int)MAX_MAP_EDGES))
		logf("Overflowed edges !!!\n");
	if (textureCount > (int)MAX_MAP_TEXTURES)
		logf("Overflowed textures !!!\n");
	if (lightDataLength > (int)MAX_MAP_LIGHTDATA)
		logf("Overflowed lightdata !!!\n");
	if (visDataLength > (int)MAX_MAP_VISDATA)
		logf("Overflowed visdata !!!\n");
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

void Bsp::ExportToObjWIP(const std::string& path, ExportObjOrder order, int iscale)
{
	if (!createDir(path))
	{
		logf("Error output path directory \"{}\" can't be created!\n", path);
		return;
	}

	float scale = iscale < 0 ? 1.0f / iscale : 1.0f * iscale;

	if (iscale == 1)
		scale = 1.0f;

	scale = abs(scale);

	FILE* f = NULL;
	logf("Export {} to {}\n", bsp_name + ".obj", path);
	logf("With {} x{}", iscale == 1 ? "scale" : iscale < 0 ? "downscale" : "upscale", abs(iscale));
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
				logf("Bad face index\n");
				break;
			}

			BSPFACE32& face = faces[i];
			BSPTEXTUREINFO& texinfo = texinfos[face.iTextureInfo];
			int texOffset = ((int*)textures)[texinfo.iMiptex + 1];
			BSPMIPTEX tex = BSPMIPTEX();
			if (texOffset >= 0)
				tex = *((BSPMIPTEX*)(textures + texOffset));

			std::vector<int> entIds = get_model_ents_ids(mdlid);

			if (entIds.empty())
			{
				entIds.push_back(0);
			}

			materialid.clear();
			for (int m = 0; m < matnames.size(); m++)
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
						COLOR3* imageData = ConvertMipTexToRGB(((BSPMIPTEX*)(textures + texOffset)), is_texture_with_pal(texinfo.iMiptex) ? NULL : (COLOR3*)quakeDefaultPalette);

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
					for (int r = 0; r < g_app->mapRenderers.size() && !foundInWad; r++)
					{
						Renderer* rend = g_app;
						for (int k = 0; k < rend->mapRenderers[r]->wads.size(); k++)
						{
							if (rend->mapRenderers[r]->wads[k]->hasTexture(tex.szName))
							{
								foundInWad = true;

								WADTEX* wadTex = rend->mapRenderers[r]->wads[k]->readTexture(tex.szName);
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


			for (int e = 0; e < entIds.size(); e++)
			{
				int tmpentid = entIds[e];
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
					lightmapVert& vert = rgroup->verts[rface->vertOffset + n];

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
					lightmapVert& vert = rgroup->verts[rface->vertOffset + n];
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
		logf("Error file access!'n");
	}
}


void recurse_node(Bsp* map, int nodeIdx)
{
	if (nodeIdx < 0)
	{
		//BSPLEAF32& leaf = map->leaves[~nodeIdx];
		logf(" (LEAF {})\n", ~nodeIdx);
		return;
	}

	recurse_node(map, map->nodes[nodeIdx].iChildren[0]);
	recurse_node(map, map->nodes[nodeIdx].iChildren[1]);
}

void Bsp::ExportPortalFile()
{
	if (bsp_path.size() < 4)
	{
		logf("ExportPortalFile: Invalid filename!\n");
		return;
	}
	std::string targetFileName = bsp_path.substr(0, bsp_path.size() - 4) + "X.prt";
	//std::string targetViewFileName = bsp_path.substr(0, bsp_path.size() - 4) + ".pts";
	std::ofstream targetFile(targetFileName, std::ios::trunc | std::ios::binary);
	if (!targetFile.is_open())
	{
		logf("Failed to open portal file for writing:\n{}\n", targetFileName);
		return;
	}
	/*std::ofstream targetViewFile(targetFileName, std::ios::trunc | std::ios::binary);
	if (!targetFile.is_open())
	{
		logf("Failed to open view portal file for writing:\n{}\n", targetViewFileName);
		return;
	}
	logf("Writing view portal file to {}\n", targetViewFileName);*/
	logf("Writing portal file to {}\n", targetFileName);

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
void Bsp::ExportLightFile()
{
	if (bsp_path.size() < 4)
	{
		logf("ExportLightFile: Invalid filename!\n");
		return;
	}

	std::string targetMapFileName = bsp_path.substr(0, bsp_path.size() - 4);
	std::string targetFileName = targetMapFileName + ".lit";
	std::ofstream targetFile(targetFileName, std::ios::trunc | std::ios::binary);
	if (!targetFile.is_open())
	{
		logf("Failed to open lighting file for writing:\n{}\n", targetFileName);
		return;
	}
	int version = 1;
	targetFile.write("QLIT", 4);
	targetFile.write((const char*)&version, 4);
	targetFile.write((const char*)lightdata, lightDataLength);
}

void Bsp::ImportLightFile()
{
	if (bsp_path.size() < 4)
	{
		logf("ImportLightFile: Invalid filename!\n");
		return;
	}

	std::string targetMapFileName = bsp_path.substr(0, bsp_path.size() - 4);
	std::string targetFileName = targetMapFileName + ".lit";
	std::ifstream targetFile(targetFileName, std::ios::binary);
	if (!targetFile.is_open())
	{
		logf("Failed to open lighting file for importing:\n{}\n", targetFileName);
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
		logf("Invalid input file");
	}
}

void Bsp::ExportExtFile()
{
	if (bsp_path.size() < 4)
	{
		logf("ExportExtFile: Invalid filename!\n");
		return;
	}

	std::string targetMapFileName = bsp_path.substr(0, bsp_path.size() - 4);
	std::string targetFileName = targetMapFileName + "_nolight.ext";
	std::ofstream targetFile(targetFileName, std::ios::trunc | std::ios::binary);
	if (!targetFile.is_open())
	{
		logf("Failed to open extents file for writing:\n{}\n", targetFileName);
		return;
	}

	logf("Save map copy to {}_nolight.bsp", targetFileName);
	write(targetMapFileName + "_nolight.bsp");

	Bsp* tmpBsp = new Bsp(targetMapFileName + "_nolight.bsp");


	logf("Remove temp file\n");

	removeFile(targetMapFileName + "_nolight.bsp");

	logf("Remove light data from {}\n", targetMapFileName + "_nolight.bsp");

	tmpBsp->lumps[LUMP_LIGHTING] = NULL;
	tmpBsp->bsp_header.lump[LUMP_LIGHTING].nOffset = 0;
	tmpBsp->bsp_header.lump[LUMP_LIGHTING].nLength = 0;

	for (int i = 0; i < tmpBsp->faceCount; i++)
	{
		faces[i].nLightmapOffset = -1;
	}

	tmpBsp->update_lump_pointers();

	logf("Save map without lightdata to {}_nolight.bsp", targetMapFileName);

	tmpBsp->write(targetMapFileName + "_nolight.bsp");

	logf("Writing extents file to {}\n", targetFileName);

	targetFile << fmt::format("{}\n", faceCount);
	for (int i = 0; i < faceCount; i++)
	{
		int mins[2]; int maxs[2];
		GetFaceExtents(this, i, mins, maxs);
		targetFile << fmt::format("{} {} {} {}\n", mins[0], mins[1], maxs[0], maxs[1]);
	}

	logf("Writing all external textures to {}\n", targetMapFileName + "_nolight.wa_");
	Wad* tmpWad = new Wad();

	std::vector<std::string> addedTextures;
	std::vector<WADTEX*> outTextures;

	int missingTex = 0;

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
						missingTex++;

						addedTextures.push_back(tex.szName);
						outTextures.push_back(texture);
					}
				}
			}
		}
	}

	logf("Found {} external textures. Missing {} textures replaced by white.\n", addedTextures.size() - missingTex, missingTex);

	tmpWad->write(targetMapFileName + "_nolight.wa_", outTextures);

	for (auto& tex : outTextures)
	{
		delete tex;
	}

	delete tmpWad;
	delete tmpBsp;
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
		logf("Error output path directory \"{}\" can't be created!\n", path);
		return;
	}
	FILE* f = NULL;
	logf("Export {} to {}\n", (bsp_name + ".map"), path);
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
				logf("Export ent {} model {} group {}\n", entIdx, modelIdx, i);
				//RenderGroup& rgroup = bsprend->renderModels[modelIdx].renderGroups[i];

			}
		}
		fclose(f);
	}
	else
	{
		logf("Error file access!'n");
	}
}

BspRenderer* Bsp::getBspRender()
{
	if (!renderer)
		for (int i = 0; i < g_app->mapRenderers.size(); i++)
			if (g_app->mapRenderers[i]->map == this)
				renderer = g_app->mapRenderers[i];
	return renderer;
}

int Bsp::getBspRenderId()
{
	for (int i = 0; i < g_app->mapRenderers.size(); i++)
		if (g_app->mapRenderers[i]->map == this)
			return i;
	return -1;
}

void Bsp::setBspRender(BspRenderer* rnd)
{
	renderer = rnd;
}

void Bsp::decalShoot(vec3 pos, const char* texname)
{
	if (!renderer || !renderer->faceMaths)
		return;
	WADTEX* texture = NULL;
	for (auto& wad : renderer->wads)
	{
		if (wad->hasTexture(texname))
		{
			texture = wad->readTexture(texname);
			break;
		}
	}

	/*if (!texture)
		return;*/

	int bestMath = -1;
	float bestDir = 1.01f;

	for (int faceIdx = 0; faceIdx < faceCount; faceIdx++)
	{
		FaceMath& face = renderer->faceMaths[faceIdx];
		if (renderer->pickFaceMath(pos + (face.normal * 0.01f), face.normal * -0.01f, face, bestDir))
		{
			bestMath = faceIdx;
		}
	}

	if (bestMath > 0)
	{
		int modelidx = get_model_from_face(bestMath);
		logf("Best {} {} {}\n", modelidx, bestMath, bestDir);
		std::vector<vec3> worldVerts;
	}

	if (texture)
		delete texture;

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
			if (i >= 0)
			{
				ents[i]->hide = true;
			}
		}
	}
}

std::vector<int> Bsp::getLeafFaces(BSPLEAF32& leaf)
{
	std::vector<int> retFaces;
	for (int i = 0; i < leaf.nMarkSurfaces; i++)
	{
		retFaces.push_back(marksurfs[leaf.iFirstMarkSurface + i]);
	}
	return retFaces;
}

std::vector<int> Bsp::getLeafFaces(int leafIdx)
{
	std::vector<int> retFaces;
	if (leafIdx < 0)
		return retFaces;

	BSPLEAF32& leaf = leaves[leafIdx];
	for (int i = 0; i < leaf.nMarkSurfaces; i++)
	{
		retFaces.push_back(marksurfs[leaf.iFirstMarkSurface + i]);
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

	if (iStartOffset < 0)
		return 0;

	BSPMIPTEX* tex = ((BSPMIPTEX*)(textures + iStartOffset));


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

		//	sz = (sz + 3) & ~3; //  + padding align by 4
	}
	return sz;
}

bool Bsp::is_texture_with_pal(int textureid)
{
	if (textureid < 0 || textureid >= textureCount)
		return false;

	if (bsp_header.nVersion == 30)
		return true;


	int iStartOffset = ((int*)textures)[textureid + 1];
	unsigned char* pStartOffset = (unsigned char*)textures + iStartOffset;
	unsigned char* pEndOffset = (unsigned char*)textures + textureDataLength;

	if (iStartOffset >= 0)
	{
		for (int i = 0; i < textureCount; i++)
		{
			int iCurOffset = ((int*)textures)[i + 1];
			if (iCurOffset < 0 || i == textureid || iCurOffset == iStartOffset)
			{
				continue;
			}

			unsigned char* pCurOffset = (unsigned char*)textures + iCurOffset;

			if (pCurOffset < pEndOffset && pCurOffset > pStartOffset)
			{
				pEndOffset = pCurOffset;
			}
		}

		BSPMIPTEX* tex = ((BSPMIPTEX*)(textures + iStartOffset));

		if (tex->nOffsets[0] <= 0) // wad texture
			return true;

		int lastMipSize = (tex->nWidth / 8) * (tex->nHeight / 8);
		unsigned char* palOffset = pStartOffset + tex->nOffsets[3] + lastMipSize;

		//logf("{}-{}={}\n", (void*)(pStartOffset + palOffset), (void*)(pEndOffset), (int)((pStartOffset + palOffset) - pEndOffset));
		if (abs(palOffset - pEndOffset) >= sizeof(COLOR3) * 256) // No align check
		{
			return true;
		}
	}

	return false;
}
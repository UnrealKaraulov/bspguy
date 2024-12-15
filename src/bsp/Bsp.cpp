#include "Bsp.h"
#include "lang.h"
#include "util.h"
#include "log.h"
#include "lodepng.h"
#include "rad.h"
#include "vis.h"
#include "remap.h"
#include "Settings.h"
#include "Renderer.h"
#include "BspRenderer.h"
#include "winding.h"
#include "forcecrc32.h"
#include "quantizer.h"
#include "Wad.h"
#include "Clipper.h"

#include "JACK_jmf.h"
#include "XASH_csm.h"

#include <deque>
#include <execution>

vec3 default_hull_extents[MAX_MAP_HULLS] = {
	vec3(0.0f,  0.0f,  0.0f),	// hull 0
	vec3(16.0f, 16.0f, 36.0f),	// hull 1
	vec3(32.0f, 32.0f, 64.0f),	// hull 2
	vec3(16.0f, 16.0f, 18.0f)	// hull 3
};

int g_sort_mode = SORT_CLIPNODES;
size_t totalBspStructs = 0;

void Bsp::init_empty_bsp()
{
	lumps.resize(HEADER_LUMPS);

	bsp_header.nVersion = 30;

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		lumps[i].resize(512, 0);
		bsp_header.lump[i].nOffset = 0;
		bsp_header.lump[i].nLength = 4;
	}


	save_cam_pos = save_cam_angles = {};
	bsp_name = "empty";
	bsp_path = "empty.bsp";
	bsp_valid = true;
	renderer = NULL;

	print_log(get_localized_string(LANG_0035));

	update_lump_pointers();

	reload_ents();

	Entity* ent = new Entity();
	ent->setOrAddKeyvalue("mapversion", "220");
	ent->setOrAddKeyvalue("classname", "worldspawn");
	ents.push_back(ent);

	create_leaf(CONTENTS_EMPTY);
	update_ent_lump();
	update_lump_pointers();


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

			Entity* world = map->getWorldspawnEnt();
			Entity* world2 = getWorldspawnEnt();

			vec3 worldOrigin = world ? world->origin : vec3();
			for (size_t n = 1; n < map->ents.size(); n++)
			{
				if (map->ents[n]->hasKey("model") && (map->ents[n]->origin + worldOrigin) == (world2 ? world2->origin : vec3()))
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

	bsp_valid = true;
	renderer = NULL;

	map_mdl = NULL;
	map_spr = NULL;
	map_csm = NULL;

	undo_lightmaps.clear();

	is_bsp30ext = false;
	is_bsp31 = false;
	is_bsp2 = false;
	is_bsp_pathos = false;
	is_bsp2_old = false;
	is_32bit_clipnodes = false;
	is_bsp29 = false;
	is_broken_clipnodes = false;
	is_blue_shift = false;
	is_colored_lightmap = true;

	is_texture_has_pal = true;
	target_save_texture_has_pal = true;


	force_skip_crc = false;

	bsp_header = BSPHEADER();
	bsp_header_ex = BSPHEADER_EX();
	parentMap = NULL;
	pvsFaces = NULL;

	lumps.resize(HEADER_LUMPS);
	extralumps.clear();

	bsp_header.nVersion = 30;
	totalBspStructs++;
	realIdx = totalBspStructs;
	update_lump_pointers();
}

Bsp::Bsp(std::string fpath)
{
	is_protected = false;
	is_bsp_model = false;
	is_mdl_model = false;

	map_mdl = NULL;
	map_spr = NULL;
	map_csm = NULL;

	undo_lightmaps.clear();

	is_bsp30ext = false;
	is_bsp31 = false;
	is_bsp2 = false;
	is_bsp_pathos = false;
	is_bsp2_old = false;
	is_32bit_clipnodes = false;
	is_bsp29 = false;
	is_broken_clipnodes = false;
	is_blue_shift = false;
	is_colored_lightmap = true;

	is_texture_has_pal = true;
	target_save_texture_has_pal = true;

	force_skip_crc = false;

	bsp_header = BSPHEADER();
	bsp_header_ex = BSPHEADER_EX();
	parentMap = NULL;
	pvsFaces = NULL;
	save_cam_pos = save_cam_angles = {};

	totalBspStructs++;
	realIdx = totalBspStructs;

	if (fpath.empty())
	{
		fpath = "newmap.bsp";
		init_empty_bsp();
		return;
	}
	else
	{
		std::string lowerpath = toLowerCase(fpath);
		if (ends_with(lowerpath, ".mdl"))
		{
			is_mdl_model = true;
			if (fileExists(fpath))
			{
				map_mdl = AddNewModelToRender(fpath);
			}
			init_empty_bsp();
			return;
		}
		if (ends_with(lowerpath, ".spr"))
		{
			is_mdl_model = true;
			if (fileExists(fpath))
			{
				map_spr = AddNewSpriteToRender(fpath);
			}
			init_empty_bsp();
			return;
		}
		if (ends_with(lowerpath, ".csm"))
		{
			is_mdl_model = true;
			if (fileExists(fpath))
			{
				map_csm = AddNewXashCsmToRender(fpath);
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
	if (g_settings.same_dir_for_ent) {
		entFilePath = fpath.substr(0, fpath.size() - 4) + ".ent";
	}
	else {
		entFilePath = g_working_dir + (bsp_name + ".ent");
	}

	if (g_settings.auto_import_ent && fileExists(entFilePath)) {
		print_log(get_localized_string(LANG_0039), entFilePath);

		int len;
		char* newlump = loadFile(entFilePath, len);
		replace_lump(LUMP_ENTITIES, newlump, len);
		delete[] newlump;
	}

	reload_ents();
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
				std::fabs(lastModel.vOrigin.z - 9999.0) < 0.01 &&
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

	Entity* world = getWorldspawnEnt();

	if (world && !world->hasKey("CRC") && !force_skip_crc)
	{
		print_log(get_localized_string(LANG_0042));
		world->addKeyvalue("CRC", std::to_string(reverse_bits(originCrc32)));
		update_ent_lump();
	}

	if (g_settings.save_cam)
	{
		if (world)
		{
			if (world->hasKey("camera_pos"))
			{
				save_cam_pos = parseVector(world->keyvalues["camera_pos"]);
			}

			if (world->hasKey("camera_angles"))
			{
				save_cam_angles = parseVector(world->keyvalues["camera_angles"]);
			}
		}
	}

	if (world)
	{
		world->setOrAddKeyvalue("_editor", g_version_string);
	}

	/*for (int i = 0; i < faceCount; i++)
	{
		BSPFACE32& face = faces[i];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		if (info.nFlags & TEX_SPECIAL)
		{
			continue;
		}
		int bmins[2];
		int bmaxs[2];
		if (!GetFaceExtents(i, bmins, bmaxs))
		{
			info.nFlags |= TEX_SPECIAL;
		}
	}*/

	save_undo_lightmaps();
}

Bsp::~Bsp()
{
	lumps.clear();

	for (size_t i = 0; i < ents.size(); i++)
	{
		delete ents[i];
	}
	ents.clear();

	delete[] pvsFaces;
	//if (mdl)
	//{
	//	delete mdl;
	//}
}



void Bsp::get_bounding_box(vec3& mins, vec3& maxs)
{
	if (modelCount > 0)
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

void Bsp::get_bounding_box(int modelidx, vec3& mins, vec3& maxs)
{
	if (modelCount > 0 && modelidx < modelCount)
	{
		BSPMODEL& thisWorld = models[modelidx];

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

void Bsp::get_model_vertex_bounds(int modelIdx, vec3& mins, vec3& maxs, bool skipSpecial)
{
	mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	BSPMODEL& model = models[modelIdx];

	for (int i = 0; i < model.nFaces; i++)
	{
		BSPFACE32& face = faces[model.iFirstFace + i];

		if (skipSpecial)
		{
			if (!face.iTextureInfo || (face.nStyles[0] == 0xFF && face.nStyles[1] == 0xFF && face.nStyles[2] == 0xFF && face.nStyles[3] == 0xFF))
				continue;

			BSPTEXTUREINFO& texinfo = texinfos[face.iTextureInfo];

			if (texinfo.nFlags & TEX_SPECIAL)
			{
				continue;
			}

			int texOffset = ((int*)textures)[texinfo.iMiptex + 1];
			if (texOffset < 0)
			{
				continue;
			}
			else
			{
				BSPMIPTEX* tex = ((BSPMIPTEX*)(textures + texOffset));
				if (toLowerCase(tex->szName) == "aaatrigger" ||
					toLowerCase(tex->szName) == "null" ||
					starts_with(toLowerCase(tex->szName), "sky") ||
					toLowerCase(tex->szName) == "noclip" ||
					toLowerCase(tex->szName) == "clip" ||
					toLowerCase(tex->szName) == "origin" ||
					toLowerCase(tex->szName) == "bevel" ||
					toLowerCase(tex->szName) == "hint" ||
					toLowerCase(tex->szName) == "skip"
					)
				{
					continue;
				}


			}
		}

		for (int e = 0; e < face.nEdges; e++) {
			int edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE32& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];

			expandBoundingBox(verts[vertIdx], mins, maxs);
		}
	}

	if (!model.nFaces && !skipSpecial) {
		// use the clipping hull "faces" instead
		Clipper clipper;
		std::vector<NodeVolumeCuts> solidNodes = get_model_leaf_volume_cuts(modelIdx, 0, CONTENTS_SOLID);

		std::vector<CMesh> solidMeshes;
		for (size_t k = 0; k < solidNodes.size(); k++) {
			solidMeshes.emplace_back(clipper.clip(solidNodes[k].cuts));
		}

		for (size_t m = 0; m < solidMeshes.size(); m++) {
			CMesh& mesh = solidMeshes[m];

			for (size_t i = 0; i < mesh.faces.size(); i++) {

				if (!mesh.faces[i].visible) {
					continue;
				}

				for (size_t k = 0; k < mesh.faces[i].edges.size(); k++) {
					for (int v = 0; v < 2; v++) {
						int vertIdx = mesh.edges[mesh.faces[i].edges[k]].verts[v];
						if (!mesh.verts[vertIdx].visible) {
							continue;
						}
						expandBoundingBox(mesh.verts[vertIdx].pos, mins, maxs);
					}
				}
			}
		}
	}
}

std::vector<int> Bsp::getModelVertsIds(int modelIdx)
{
	std::vector<int> outverts;
	std::set<int> visited;
	BSPMODEL& model = models[modelIdx];

	for (int i = 0; i < model.nFaces; i++)
	{
		BSPFACE32& face = faces[model.iFirstFace + i];

		for (int e = 0; e < face.nEdges; e++)
		{
			int edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE32& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];

			if (!visited.count(vertIdx))
			{
				outverts.push_back(vertIdx);
				visited.insert(vertIdx);
			}
		}
	}
	return outverts;
}
std::vector<TransformVert> Bsp::getModelTransformVerts(int modelIdx)
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
			int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];

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

			if (nodePlanes[i].vNormal.equal(nodePlanes[k].vNormal, EPSILON) && std::fabs(nodePlanes[i].fDist - nodePlanes[k].fDist) < ON_EPSILON)
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
			if (std::fabs(dotProduct(v, p.vNormal) - p.fDist) < ON_EPSILON)
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
				int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];

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

std::vector<NodeVolumeCuts> Bsp::get_model_leaf_volume_cuts(int modelIdx, int hullIdx, int contents) {
	std::vector<NodeVolumeCuts> modelVolumeCuts;

	if (hullIdx >= 0 && hullIdx < MAX_MAP_HULLS)
	{
		int nodeIdx = models[modelIdx].iHeadnodes[hullIdx];
		bool is_valid_node = false;

		if (hullIdx == 0) {
			is_valid_node = nodeIdx >= 0 && nodeIdx < nodeCount;
		}
		else {
			is_valid_node = nodeIdx >= 0 && nodeIdx < clipnodeCount;
		}

		if (nodeIdx >= 0 && is_valid_node) {
			std::vector<BSPPLANE> clipOrder;
			if (hullIdx == 0) {
				get_node_leaf_cuts(nodeIdx, clipOrder, modelVolumeCuts, contents);
			}
			else {
				get_clipnode_leaf_cuts(nodeIdx, clipOrder, modelVolumeCuts, contents);
			}
		}
	}
	return modelVolumeCuts;
}

void Bsp::get_clipnode_leaf_cuts(int iNode, std::vector<BSPPLANE>& clipOrder, std::vector<NodeVolumeCuts>& output, int contents) {
	BSPCLIPNODE32& node = clipnodes[iNode];

	if (node.iPlane < 0) {
		return;
	}

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			get_clipnode_leaf_cuts(node.iChildren[i], clipOrder, output, contents);
		}
		else if (node.iChildren[i] == contents) {
			NodeVolumeCuts nodeVolumeCuts;
			nodeVolumeCuts.nodeIdx = iNode;

			// reverse order of branched planes = order of cuts to the world which define this node's volume
			// https://qph.fs.quoracdn.net/main-qimg-2a8faad60cc9d437b58a6e215e6e874d
			for (int k = (int)clipOrder.size() - 1; k >= 0; k--) {
				nodeVolumeCuts.cuts.push_back(clipOrder[k]);
			}

			output.push_back(nodeVolumeCuts);
		}

		clipOrder.pop_back();
	}
}

void Bsp::get_node_leaf_cuts(int iNode, std::vector<BSPPLANE>& clipOrder, std::vector<NodeVolumeCuts>& output, int contents) {
	BSPNODE32& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			get_node_leaf_cuts(node.iChildren[i], clipOrder, output, contents);
		}
		else if (leaves[~node.iChildren[i]].nContents == contents) {
			NodeVolumeCuts nodeVolumeCuts;
			nodeVolumeCuts.nodeIdx = iNode;

			// reverse order of branched planes = order of cuts to the world which define this node's volume
			// https://qph.fs.quoracdn.net/main-qimg-2a8faad60cc9d437b58a6e215e6e874d
			for (int k = (int)clipOrder.size() - 1; k >= 0; k--) {
				nodeVolumeCuts.cuts.push_back(clipOrder[k]);
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

bool Bsp::isInteriorFace(const Polygon3D& poly, int hull) {
	int headnode = models[0].iHeadnodes[hull];
	vec3 testPos = poly.center + poly.plane_z * 0.5f;
	return pointContents(headnode, testPos, hull) == CONTENTS_EMPTY;
}

int Bsp::addTextureInfo(BSPTEXTUREINFO& copy)
{
	BSPTEXTUREINFO* newInfos = new BSPTEXTUREINFO[texinfoCount + 1];
	memcpy(newInfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

	int newIdx = texinfoCount;
	newInfos[newIdx] = copy;

	replace_lump(LUMP_TEXINFO, newInfos, (texinfoCount + 1) * sizeof(BSPTEXTUREINFO));
	delete[] newInfos;

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

bool Bsp::vertex_manipulation_sync(int modelIdx, const std::vector<TransformVert>& hullVerts, bool convexCheckOnly)
{
	if (modelIdx < 0 || hullVerts.size() < 4)
		return false;

	std::set<int> affectedPlanes;
	std::map<int, std::vector<vec3>> planeVerts;
	std::vector<vec3> allVertPos;

	for (const auto& vert : hullVerts)
	{
		for (int iPlane : vert.iPlanes)
		{
			affectedPlanes.insert(iPlane);
			planeVerts[iPlane].push_back(vert.pos);
		}
		allVertPos.push_back(vert.pos);
	}

	int planeUpdates = 0;
	std::map<int, BSPPLANE> newPlanes;
	std::map<int, bool> shouldFlipChildren;
	for (const auto& [iPlane, tverts] : planeVerts)
	{
		if (tverts.size() < 3)
		{
			if (g_settings.verboseLogs)
				print_log(get_localized_string(LANG_0045));
			return false; // invalid solid
		}

		if (iPlane >= planeCount)
		{
			print_log("Fatal error sync plane bad {} of {}\n", iPlane, planeCount);
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
		bool expectedFlip = testPlane.update_plane(planes[iPlane].vNormal, planes[iPlane].fDist);
		bool flipped = newPlane.update_plane(newPlane.vNormal, newPlane.fDist);

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
		return planeVerts.size();

	for (const auto& [iPlane, newPlane] : newPlanes)
	{
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
					std::swap(node.iChildren[0], node.iChildren[1]);
				}
			}
		}
	}

	//print_log(get_localized_string(LANG_0047), planeUpdates);
	return true;
}

bool Bsp::move(vec3 offset, int modelIdx, bool onlyModel, bool forceMove, bool logged)
{
	if (modelIdx < 0 || modelIdx >= modelCount || !ents.size())
	{
		print_log(get_localized_string(LANG_0048));
		return false;
	}

	if (logged)
	{
		save_undo_lightmaps();
		g_progress.update("Moving structures", (int)ents.size() - 1);
	}

	BSPMODEL& target = models[modelIdx];

	// all ents should be moved if the world is being moved
	bool movingWorld = modelIdx == 0 && !onlyModel;

	// Submodels don't use leaves like the world model does. Only the contents of a leaf matters
	// for submodels. All other data is ignored. bspguy will reuse world leaves in submodels to 
	// save space, which means moving leaves for those models would likely break something else.
	// So, don't move leaves for submodels.
	// bool dontMoveLeaves = !movingWorld;

	if (!forceMove && does_model_use_shared_structures(modelIdx))
		split_shared_model_structures(modelIdx);


	if (movingWorld)
	{
		for (size_t i = 1; i < ents.size(); i++)
		{ // don't move the world entity
			if (logged)
				g_progress.tick();

			vec3 ori = ents[i]->origin;
			ori += offset;

			if (ents[i]->hasKey("spawnorigin"))
			{
				vec3 spawnori = parseVector(ents[i]->keyvalues["spawnorigin"]);

				// entity not moved if destination is 0,0,0
				if (std::fabs(spawnori.x) >= EPSILON || std::fabs(spawnori.y) >= EPSILON || std::fabs(spawnori.z) >= EPSILON)
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
	if (std::fabs(target.nMins.x) > g_limits.fltMaxCoord ||
		std::fabs(target.nMins.y) > g_limits.fltMaxCoord ||
		std::fabs(target.nMins.z) > g_limits.fltMaxCoord ||
		std::fabs(target.nMaxs.x) > g_limits.fltMaxCoord ||
		std::fabs(target.nMaxs.y) > g_limits.fltMaxCoord ||
		std::fabs(target.nMaxs.z) > g_limits.fltMaxCoord)
	{
		print_log(get_localized_string(LANG_0049));
	}

	STRUCTUSAGE shouldBeMoved(this);
	mark_model_structures(modelIdx, &shouldBeMoved, false /*dontMoveLeaves*/);

	for (int i = 0; i < nodeCount; i++)
	{
		if (!shouldBeMoved.nodes[i] && !forceMove)
		{
			continue;
		}

		BSPNODE32& node = nodes[i];

		if (std::fabs((float)node.nMins[0] + offset.x) > g_limits.fltMaxCoord ||
			std::fabs((float)node.nMaxs[0] + offset.x) > g_limits.fltMaxCoord ||
			std::fabs((float)node.nMins[1] + offset.y) > g_limits.fltMaxCoord ||
			std::fabs((float)node.nMaxs[1] + offset.y) > g_limits.fltMaxCoord ||
			std::fabs((float)node.nMins[2] + offset.z) > g_limits.fltMaxCoord ||
			std::fabs((float)node.nMaxs[2] + offset.z) > g_limits.fltMaxCoord)
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

		if (std::fabs((float)leaf.nMins[0] + offset.x) > g_limits.fltMaxCoord ||
			std::fabs((float)leaf.nMaxs[0] + offset.x) > g_limits.fltMaxCoord ||
			std::fabs((float)leaf.nMins[1] + offset.y) > g_limits.fltMaxCoord ||
			std::fabs((float)leaf.nMaxs[1] + offset.y) > g_limits.fltMaxCoord ||
			std::fabs((float)leaf.nMins[2] + offset.z) > g_limits.fltMaxCoord ||
			std::fabs((float)leaf.nMaxs[2] + offset.z) > g_limits.fltMaxCoord)
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

		if (std::fabs(vert.x) > g_limits.fltMaxCoord ||
			std::fabs(vert.y) > g_limits.fltMaxCoord ||
			std::fabs(vert.z) > g_limits.fltMaxCoord)
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

		if (std::fabs(newPlaneOri.x) > g_limits.fltMaxCoord || std::fabs(newPlaneOri.y) > g_limits.fltMaxCoord ||
			std::fabs(newPlaneOri.z) > g_limits.fltMaxCoord)
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

void Bsp::move_texinfo(BSPTEXTUREINFO& info, vec3 offset)
{
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
		g_progress.update(fmt::format("Undo lightmaps ({})", faceCount), faceCount);
	}
	undo_lightmaps.clear();
	undo_lightmaps.resize(faceCount);

	for (int i = 0; i < faceCount; i++)
	{
		int size[2];
		GetFaceLightmapSize(i, size);

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
	{
		g_progress.update("Resize lightmaps", faceCount);
	}
	if (!undo_lightmaps.size()) 
	{
		save_undo_lightmaps(true);
	}

	std::vector<COLOR3> newLightData;

	for (int faceId = 0; faceId < faceCount; faceId++) 
	{
		BSPFACE32& face = faces[faceId];
		int newLightMapOffset = (int)newLightData.size();
		for (int lightId = 0; lightId < MAX_LIGHTMAPS; lightId++) 
		{
			if (face.nStyles[lightId] == 255 || face.nLightmapOffset < 0) 
			{
				continue;
			}
			int size[2] = { 0, 0 };
			if (faceId < (int)undo_lightmaps.size()) 
			{
				size[0] = undo_lightmaps[faceId].width;
				size[1] = undo_lightmaps[faceId].height;
			}
			int newsize[2];
			GetFaceLightmapSize(faceId, newsize);

			int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
			int offset = face.nLightmapOffset + lightId * lightmapSz;

			COLOR3* data = (COLOR3*)(lightdata + offset);

			std::vector<COLOR3> newdata;
			if (newsize[0] == size[0] && newsize[1] == size[1]) 
			{
				if (lightdata && offset < lightDataLength && lightId < undo_lightmaps[faceId].layers) 
				{
					newdata.insert(newdata.end(), data, data + size[0] * size[1]);
				}
				else 
				{
					newdata.resize(size[0] * size[1], COLOR3(255, 255, 255));
				}
			}
			else 
			{
				if (lightmapSz > 0 && lightdata && offset < lightDataLength && lightId < undo_lightmaps[faceId].layers) 
				{
					scaleImage(data, newdata, size[0], size[1], newsize[0], newsize[1]);
				}
				else 
				{
					newdata.resize(newsize[0] * newsize[1], COLOR3(255, 255, 255));
				}
			}
			newLightData.insert(newLightData.end(), newdata.begin(), newdata.end());
		}

		if (face.nLightmapOffset >= 0) 
		{
			face.nLightmapOffset = newLightMapOffset * sizeof(COLOR3);
		}
		if (logged) 
		{
			g_progress.tick();
		}
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
	
	delete[] tmpLump;
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

	delete[] newPlanes;
	delete[] newClipnodes;
	delete[] newTexinfos;

	std::vector<bool> newVisitedClipnodes(newClipnodeCount, false);
	remappedStuff.visitedClipnodes = std::move(newVisitedClipnodes);

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

LumpState Bsp::duplicate_lumps(unsigned int targets)
{
	LumpState state(this);

	if (targets & FL_ENTITIES)
	{
		update_ent_lump();
	}

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if ((targets & (1 << i)) == 0 || lumps[i].empty())
		{
			continue;
		}
		state.lumps[i] = lumps[i];
	}
	return state;
}

int Bsp::delete_embedded_textures()
{
	unsigned int headerSz = (textureCount + 1) * sizeof(int);
	unsigned int newTexDataSize = headerSz + (textureCount * sizeof(BSPMIPTEX));

	unsigned char* newTextureData = new unsigned char[newTexDataSize + sizeof(int)];
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
	delete[] newTextureData;

	remove_unused_model_structures(CLEAN_TEXINFOS | CLEAN_TEXTURES);

	return numRemoved;
}

void Bsp::replace_lumps(const LumpState& state)
{
	bool uploadEnts = false;

	for (unsigned int i = 0; i < HEADER_LUMPS; i++)
	{
		if (state.lumps[i].size())
		{
			lumps[i] = state.lumps[i];
			bsp_header.lump[i].nLength = (int)lumps[i].size();
			if (i == LUMP_ENTITIES)
			{
				uploadEnts = true;
			}
		}
	}

	if (uploadEnts)
		reload_ents();
	update_lump_pointers();
}

unsigned int Bsp::remove_unused_structs(int lumpIdx, std::vector<bool>& usedStructs, std::vector<int>& remappedIndexes)
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

	unsigned char* oldStructs = lumps[lumpIdx].data();
	unsigned char* newStructs = new unsigned char[oldStructCount * structSize];

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
	delete[] newStructs;
	return removeCount;
}

unsigned int Bsp::remove_unused_textures(std::vector<bool>& usedTextures, std::vector<int>& remappedIndexes, int* removeddata)
{
	int oldTexCount = textureCount;

	int usedCount = 0;
	int usedSize = 4;

	for (int i = 0; i < oldTexCount; i++)
	{
		int offset = ((int*)textures)[i + 1];
		for (int t = 0; t < texinfoCount; t++)
		{
			BSPTEXTUREINFO& texinfo = texinfos[t];
			if (texinfo.iMiptex == i)
			{
				usedTextures[i] = true;
				if (offset < 0)
				{
					remappedIndexes[i] = 0;
					texinfo.iMiptex = 0;
				}
			}
		}

		if (usedTextures[i])
		{
			if (offset >= 0)
			{
				usedSize += getBspTextureSize(i) + sizeof(int);
				usedCount++;
			}
			continue;
		}

		if (offset >= 0)
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
					usedSize += getBspTextureSize(i) + sizeof(int);
					usedCount++;
					continue;
				}
			}
		}
	}

	int removeCount = oldTexCount - usedCount;
	int newTexCount = usedCount;

	int removeSize = bsp_header.lump[LUMP_TEXTURES].nLength - usedSize;
	int totalSize = usedSize;

	totalSize = (totalSize + 3) & ~3; // 4 bytes align lump

	unsigned char* newTexData = new unsigned char[totalSize + sizeof(int)];
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
		if (oldOffset >= 0)
		{
			//BSPMIPTEX* tex = (BSPMIPTEX*)(textures + oldOffset);
			int sz = getBspTextureSize(i);
			memcpy(newTexData + newOffset, textures + oldOffset, sz);
			texHeader[k + 1] = newOffset;
			newOffset += sz;
			remappedIndexes[i] = k;
			k++;
		}
	}

	texHeader[0] = k;

	if (removeddata)
		*removeddata = removeSize;

	replace_lump(LUMP_TEXTURES, newTexData, totalSize);
	delete[] newTexData;
	return removeCount;
}

unsigned int Bsp::remove_unused_lightmaps(std::vector<bool>& usedFaces)
{
	int oldLightdataSize = lightDataLength;

	int* lightmapSizes = new int[faceCount] {};

	int newLightDataSize = 0;

	for (int i = 0; i < faceCount; i++)
	{
		if (usedFaces[i] && faces[i].nLightmapOffset >= 0)
		{
			lightmapSizes[i] = GetFaceLightmapSizeBytes(i);
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
	delete[] newColorData;
	return (unsigned int)(oldLightdataSize - newLightDataSize);
}

unsigned int Bsp::remove_unused_visdata(BSPLEAF32* oldLeaves, int oldLeafCount, int oldLeavesMemSize)
{
	int oldVisLength = visDataLength;

	// exclude solid leaf
	int oldVisLeafCount = oldLeafCount;
	int newVisLeafCount = (bsp_header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF32));

	int oldWorldLeaves = ((BSPMODEL*)lumps[LUMP_MODELS].data())->nVisLeafs; // TODO: allow deleting world leaves
	int newWorldLeaves = ((BSPMODEL*)lumps[LUMP_MODELS].data())->nVisLeafs;

	unsigned int oldVisRowSize = ((oldVisLeafCount + 63) & ~63) >> 3;
	unsigned int newVisRowSize = ((newVisLeafCount + 63) & ~63) >> 3;

	int decompressedVisSize = oldLeafCount * oldVisRowSize;
	unsigned char* decompressedVis = new unsigned char[decompressedVisSize];
	memset(decompressedVis, 0xFF, decompressedVisSize);


	decompress_vis_lump(this, oldLeaves, lumps[LUMP_VISIBILITY].data(), decompressedVis,
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

	delete[] compressedVisResized;
	delete[] decompressedVis;
	delete[] compressedVis;

	return oldVisLength - newVisLen;
	/*int oldVisLength = visDataLength;

	// exclude solid leaf
	int oldVisLeafCount = oldLeavesMemSize / sizeof(BSPLEAF32);
	int newVisLeafCount = bsp_header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF32);

	int newWorldLeaves = ((BSPMODEL*)lumps[LUMP_MODELS].data())->nVisLeafs;

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

	decompress_vis_lump(oldLeaves, lumps[LUMP_VISIBILITY].data(), decompressedVis,
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

	delete[] compressedVisResized;
	delete[] decompressedVis;
	delete[] compressedVis;

	return (unsigned int)(oldVisLength - newVisLen);*/
}

bool operator == (BSPTEXTUREINFO& struct1, BSPTEXTUREINFO& struct2)
{
	return struct1.iMiptex == struct2.iMiptex &&
		struct1.nFlags == struct2.nFlags &&
		std::fabs(struct1.shiftS - struct2.shiftS) < 0.01f &&
		std::fabs(struct1.shiftT - struct2.shiftT) < 0.01f &&
		struct1.vS.equal(struct2.vS, 0.001f) && struct1.vT.equal(struct2.vT, 0.001f);
}

bool operator == (BSPPLANE& struct1, BSPPLANE& struct2)
{
	return struct1.vNormal.equal(struct2.vNormal, 0.001f) && std::fabs(struct1.fDist - struct2.fDist) < 0.01f;
}

bool operator !=(BSPTEXTUREINFO& struct1, BSPTEXTUREINFO& struct2)
{
	return !(struct1 == struct2);
}
bool operator !=(BSPPLANE& struct1, BSPPLANE& struct2)
{
	return !(struct1 == struct2);
}


int Bsp::merge_all_texinfos()
{
	int unusedtexinfos = 0;
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
					bool merge = false;
					for (int z = 0; z < faceCount; z++)
					{
						if (z == i)
							continue;

						merge = true;
						if (faces[z].iTextureInfo == n)
						{
							faces[z].iTextureInfo = texInfoIdx;
						}
					}
					if (merge)
						unusedtexinfos++;
				}
			}
		}
	}
	return unusedtexinfos;
}

void Bsp::round_all_verts(int digits)
{
	int d = 1;

	for (int i = 0; i < digits; i++)
		d *= 10;

	for (int v = 0; v < vertCount; v++)
	{
		vec3& p = verts[v];
		for (int j = 0; j < 3; j++)
		{
			p[j] = std::round(p[j] * d) / d;
		}
	}
}

int Bsp::merge_all_verts(float epsilon)
{
	int merged_verts = 0;

	g_progress.update("Merge vertices " + std::to_string(epsilon) + "...", vertCount);

	for (int v = 0; v < vertCount; v++)
	{
		g_progress.tick();
		bool found1 = false;
		bool found2 = false;

		std::vector<int> edges_FOR(edgeCount);
		std::iota(edges_FOR.begin(), edges_FOR.end(), 0);

		std::for_each(std::execution::par_unseq, edges_FOR.begin(), edges_FOR.end(), [&](int i)
			{
				if (!found1 && edges[i].iVertex[0] != v && verts[edges[i].iVertex[0]].equal(verts[v], epsilon))
				{
					edges[i].iVertex[0] = v;
					merged_verts++;
					found1 = true;
				}
				if (!found2 && edges[i].iVertex[1] != v && verts[edges[i].iVertex[1]].equal(verts[v], epsilon))
				{
					edges[i].iVertex[1] = v;
					merged_verts++;
					found2 = true;
				}

				if (found1 && found2)
				{
					return;
				}
			});
	}
	g_progress.clear();
	g_progress = ProgressMeter();
	return merged_verts;
}

STRUCTCOUNT Bsp::remove_unused_model_structures(unsigned int target)
{
	if (!modelCount)
		return STRUCTCOUNT();

	update_lump_pointers();

	int clean_texinfos = 0;

	if (target & CLEAN_TEXINFOS_FORCE || (g_settings.mark_unused_texinfos && target & CLEAN_TEXINFOS))
	{
		clean_texinfos = merge_all_texinfos();
	}

	int merged_verts = 0;
	if (g_settings.merge_verts && target & CLEAN_VERTICES)
	{
		print_log(get_localized_string(LANG_0066));
		merged_verts = merge_all_verts();
	}

	if (target & CLEAN_EDGES_FORCE || (g_settings.merge_edges && target & CLEAN_EDGES))
	{
		for (int n = 0; n < surfedgeCount; n++)
		{
			int surfedge = surfedges[n];

			int vert1 = edges[abs(surfedge)].iVertex[0];
			int vert2 = edges[abs(surfedge)].iVertex[1];

			for (int e = 0; e < edgeCount; e++)
			{
				BSPEDGE32 edge = edges[e];
				if (edge.iVertex[0] == vert1 && edge.iVertex[1] == vert2)
				{
					surfedges[n] = surfedge < 0 ? -e : e;
					break;
				}
				else if (edge.iVertex[1] == vert1 && edge.iVertex[0] == vert2)
				{
					surfedges[n] = surfedge < 0 ? e : -e;
					break;
				}
			}
		}
	}

	// marks which structures should not be moved
	STRUCTUSAGE usedStructures(this);

	bool* usedModels = new bool[modelCount + 1];
	if (target & CLEAN_MODELS)
	{
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
	}

	// reversed so models can be deleted without shifting the next delete index
	if (modelCount > 0)
	{
		for (int i = modelCount - 1; i >= 0; i--)
		{
			if (!(target & CLEAN_MODELS))
			{
				mark_model_structures(i, &usedStructures, false);
			}
			else
			{
				if (!usedModels[i])
				{
					delete_model(i);
				}
				else
				{
					mark_model_structures(i, &usedStructures, false);
				}
			}
		}
	}
	Entity* world = getWorldspawnEnt();

	if (world)
	{
		if (world->hasKey("message") && world->keyvalues["message"] == "bsp model")
		{
			if (nodeCount)
				usedStructures.nodes[0] = true;
			if (clipnodeCount)
				usedStructures.clipnodes[0] = true;
		}
	}

	STRUCTREMAP remap(this);
	STRUCTCOUNT removeCount = STRUCTCOUNT();

	if (edgeCount > 0)
	{
		usedStructures.edges[0] = true;
	}

	if (leafCount > 0)
	{
		usedStructures.leaves[0] = true;
	}

	update_lump_pointers();
	int oldLeavesLumpLen = bsp_header.lump[LUMP_LEAVES].nLength;
	unsigned char* oldLeaves = new unsigned char[oldLeavesLumpLen];
	memcpy(oldLeaves, lumps[LUMP_LEAVES].data(), oldLeavesLumpLen);

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
	if (target & CLEAN_TEXINFOS_FORCE || target & CLEAN_TEXINFOS)
		removeCount.texInfos = remove_unused_structs(LUMP_TEXINFO, usedStructures.texInfo, remap.texInfo);
	if (target & CLEAN_EDGES_FORCE || target & CLEAN_EDGES)
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
		removeCount.visdata = remove_unused_visdata(/*usedStructures.leaves, */(BSPLEAF32*)oldLeaves, usedStructures.count.leaves, oldLeavesLumpLen);

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

	if (newCounts.edges == 1)
	{
		// special for empty model (only collision)
		edges[0].iVertex[0] = 0;
		edges[0].iVertex[1] = 0;
	}
	else
	{
		for (unsigned int i = 0; i < newCounts.edges; i++)
		{
			for (int k = 0; k < 2; k++)
			{
				edges[i].iVertex[k] = remap.verts[edges[i].iVertex[k]];
			}
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
		update_unused_wad_files(this, this);

	return removeCount;
}

void update_unused_wad_files(Bsp* baseMap, Bsp* targetMap, int tex_type)
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
							unsigned int colorCount = 256;
							COLOR3 palette[256];
							if (!targetMap->is_texture_has_pal)
							{
								if (g_settings.pal_id >= 0)
								{
									colorCount = g_settings.palettes[g_settings.pal_id].colors;
									memcpy(palette, g_settings.palettes[g_settings.pal_id].data, g_settings.palettes[g_settings.pal_id].colors * sizeof(COLOR3));
								}
								else
								{
									colorCount = 256;
									memcpy(palette, g_settings.palette_default, 256 * sizeof(COLOR3));
								}
							}
							else
							{
								colorCount = 0;
							}

							WADTEX* wadTex = wad->readTexture(tex->szName);
							texNames.insert(tex->szName);

							if (tex_type == 1)
							{
								COLOR3* newTex = ConvertWadTexToRGB(wadTex);
								Quantizer* tmpCQuantizer = new Quantizer(256, 8);
								if (colorCount != 0)
									tmpCQuantizer->SetColorTable(palette, 256);
								tmpCQuantizer->ApplyColorTable((COLOR3*)newTex, wadTex->nWidth * wadTex->nHeight);
								delete tmpCQuantizer;
								targetMap->add_texture(tex->szName, (unsigned char*)newTex, wadTex->nWidth, wadTex->nHeight, true);
								delete[] newTex;
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

		if (!updatewads && targetMap->getWorldspawnEnt())
		{
			targetMap->getWorldspawnEnt()->setOrAddKeyvalue("wad", wadNames);
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
			else if (std::fabs(minhull.x) > MAX_HULL1_EXTENT_MONSTER || std::fabs(maxhull.x) > MAX_HULL1_EXTENT_MONSTER
				|| std::fabs(minhull.y) > MAX_HULL1_EXTENT_MONSTER || std::fabs(maxhull.y) > MAX_HULL1_EXTENT_MONSTER)
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
			int spawnflags = str_to_int(usageEnts[k]->keyvalues["spawnflags"]);

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

		BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS].data())[i];

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

void Bsp::delete_oob_nodes(int iNode, int* parentBranch, std::vector<BSPPLANE>& clipOrder, int oobFlags,
	bool* oobHistory, bool isFirstPass, int& removedNodes) {
	BSPNODE32& node = nodes[iNode];
	float oob_coord = g_limits.maxMapBoundary;

	if (node.iPlane < 0) {
		return;
	}

	bool isoob = isFirstPass ? true : oobHistory[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			delete_oob_nodes(node.iChildren[i], &node.iChildren[i], clipOrder, oobFlags, oobHistory,
				isFirstPass, removedNodes);
			if (node.iChildren[i] >= 0) {
				isoob = false; // children weren't empty, so this node isn't empty either
			}
		}
		else if (isFirstPass) {
			std::vector<BSPPLANE> cuts;
			for (int k = (int)clipOrder.size() - 1; k >= 0; k--) {
				cuts.push_back(clipOrder[k]);
			}

			Clipper clipper;
			CMesh nodeVolume = clipper.clip(cuts);

			for (size_t k = 0; k < nodeVolume.verts.size(); k++) {
				if (!nodeVolume.verts[k].visible)
					continue;
				vec3 v = nodeVolume.verts[k].pos;

				bool oobx0 = (oobFlags & OOB_CLIP_X) ? (v.x > oob_coord) : false;
				bool oobx1 = (oobFlags & OOB_CLIP_X_NEG) ? (v.x < -oob_coord) : false;
				bool ooby0 = (oobFlags & OOB_CLIP_Y) ? (v.y > oob_coord) : false;
				bool ooby1 = (oobFlags & OOB_CLIP_Y_NEG) ? (v.y < -oob_coord) : false;
				bool oobz0 = (oobFlags & OOB_CLIP_Z) ? (v.z > oob_coord) : false;
				bool oobz1 = (oobFlags & OOB_CLIP_Z_NEG) ? (v.z < -oob_coord) : false;

				if (!oobx0 && !ooby0 && !oobz0 && !oobx1 && !ooby1 && !oobz1) {
					isoob = false; // node can't be empty if both children aren't oob
				}
			}
		}

		clipOrder.pop_back();
	}

	if (isFirstPass) {
		// only check if each node is ever considered in bounds, after considering all branches.
		// don't remove anything until the entire tree has been scanned

		if (!isoob) {
			oobHistory[iNode] = false;
		}
	}
	else if (parentBranch && isoob) {
		// we know which nodes are OOB now, so it's safe to unlink this node from the paranet
		*parentBranch = CONTENTS_SOLID;
		removedNodes++;
	}
}

void Bsp::delete_oob_clipnodes(int iNode, int* parentBranch, std::vector<BSPPLANE>& clipOrder, int oobFlags,
	bool* oobHistory, bool isFirstPass, int& removedNodes) {
	BSPCLIPNODE32& node = clipnodes[iNode];
	float oob_coord = g_limits.maxMapBoundary;

	if (node.iPlane < 0) {
		return;
	}

	bool isoob = isFirstPass ? true : oobHistory[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			delete_oob_clipnodes(node.iChildren[i], &node.iChildren[i], clipOrder, oobFlags,
				oobHistory, isFirstPass, removedNodes);
			if (node.iChildren[i] >= 0) {
				isoob = false; // children weren't empty, so this node isn't empty either
			}
		}
		else if (isFirstPass) {
			std::vector<BSPPLANE> cuts;
			for (int k = (int)clipOrder.size() - 1; k >= 0; k--) {
				cuts.push_back(clipOrder[k]);
			}

			Clipper clipper;
			CMesh nodeVolume = clipper.clip(cuts);

			vec3 mins(FLT_MAX, FLT_MAX, FLT_MAX);
			vec3 maxs(-FLT_MAX, -FLT_MAX, -FLT_MAX);

			for (size_t k = 0; k < nodeVolume.verts.size(); k++) {
				if (!nodeVolume.verts[k].visible)
					continue;
				vec3 v = nodeVolume.verts[k].pos;

				expandBoundingBox(v, mins, maxs);
			}

			bool oobx0 = (oobFlags & OOB_CLIP_X) ? (mins.x > oob_coord) : false;
			bool oobx1 = (oobFlags & OOB_CLIP_X_NEG) ? (maxs.x < -oob_coord) : false;
			bool ooby0 = (oobFlags & OOB_CLIP_Y) ? (mins.y > oob_coord) : false;
			bool ooby1 = (oobFlags & OOB_CLIP_Y_NEG) ? (maxs.y < -oob_coord) : false;
			bool oobz0 = (oobFlags & OOB_CLIP_Z) ? (mins.z > oob_coord) : false;
			bool oobz1 = (oobFlags & OOB_CLIP_Z_NEG) ? (maxs.z < -oob_coord) : false;

			if (!oobx0 && !ooby0 && !oobz0 && !oobx1 && !ooby1 && !oobz1) {
				isoob = false; // node can't be empty if both children aren't oob
			}
		}

		clipOrder.pop_back();
	}

	// clipnodes are reused in the BSP tree. Some paths to the same node involve more plane intersections
	// than others. So, there will be some paths where the node is considered OOB and others not. If it
	// was EVER considered to be within bounds, on any branch, then don't let be stripped. Otherwise you
	// end up with broken clipnodes that are expanded too much because a deeper branch was deleted and
	// so there are fewer clipping planes to define the volume. This then then leads to players getting
	// stuck on shit and unable to escape when touching that region.

	if (isFirstPass) {
		// only check if each node is ever considered in bounds, after considering all branches.
		// don't remove anything until the entire tree has been scanned

		if (!isoob) {
			oobHistory[iNode] = false;
		}
	}
	else if (parentBranch && isoob) {
		// we know which nodes are OOB now, so it's safe to unlink this node from the paranet
		*parentBranch = CONTENTS_SOLID;
		removedNodes++;
	}
}

void Bsp::delete_oob_data(int clipFlags) {
	float oob_coord = g_limits.maxMapBoundary;
	BSPMODEL& worldmodel = models[0];

	// remove OOB nodes and clipnodes
	std::vector<BSPPLANE> clipOrder;

	bool* oobMarks = new bool[nodeCount];

	// collect oob data, then actually remove the nodes
	int removedNodes = 0;
	do {
		removedNodes = 0;
		memset(oobMarks, 1, nodeCount * sizeof(bool)); // assume everything is oob at first
		delete_oob_nodes(worldmodel.iHeadnodes[0], NULL, clipOrder, clipFlags, oobMarks, true, removedNodes);
		delete_oob_nodes(worldmodel.iHeadnodes[0], NULL, clipOrder, clipFlags, oobMarks, false, removedNodes);
	} while (removedNodes);
	delete[] oobMarks;

	oobMarks = new bool[clipnodeCount];
	for (int i = 1; i < MAX_MAP_HULLS; i++) {
		// collect oob data, then actually remove the nodes
		removedNodes = 0;
		do {
			removedNodes = 0;
			memset(oobMarks, 1, clipnodeCount * sizeof(bool)); // assume everything is oob at first
			delete_oob_clipnodes(worldmodel.iHeadnodes[i], NULL, clipOrder, clipFlags, oobMarks, true, removedNodes);
			delete_oob_clipnodes(worldmodel.iHeadnodes[i], NULL, clipOrder, clipFlags, oobMarks, false, removedNodes);
		} while (removedNodes);
	}
	delete[] oobMarks;


	std::vector<Entity*> newEnts;
	newEnts.push_back(ents[0]); // never remove worldspawn

	for (size_t i = 1; i < ents.size(); i++) {
		vec3 v = ents[i]->origin;
		int modelIdx = ents[i]->getBspModelIdx();

		if (modelIdx != -1) {
			vec3 mins, maxs;
			get_model_vertex_bounds(modelIdx, mins, maxs);
			mins += v;
			maxs += v;

			bool oobx0 = (clipFlags & OOB_CLIP_X) ? (mins.x > oob_coord) : false;
			bool oobx1 = (clipFlags & OOB_CLIP_X_NEG) ? (maxs.x < -oob_coord) : false;
			bool ooby0 = (clipFlags & OOB_CLIP_Y) ? (mins.y > oob_coord) : false;
			bool ooby1 = (clipFlags & OOB_CLIP_Y_NEG) ? (maxs.y < -oob_coord) : false;
			bool oobz0 = (clipFlags & OOB_CLIP_Z) ? (mins.z > oob_coord) : false;
			bool oobz1 = (clipFlags & OOB_CLIP_Z_NEG) ? (maxs.z < -oob_coord) : false;

			if (!oobx0 && !ooby0 && !oobz0 && !oobx1 && !ooby1 && !oobz1) {
				newEnts.push_back(ents[i]);
			}
		}
		else {
			bool oobx0 = (clipFlags & OOB_CLIP_X) ? (v.x > oob_coord) : false;
			bool oobx1 = (clipFlags & OOB_CLIP_X_NEG) ? (v.x < -oob_coord) : false;
			bool ooby0 = (clipFlags & OOB_CLIP_Y) ? (v.y > oob_coord) : false;
			bool ooby1 = (clipFlags & OOB_CLIP_Y_NEG) ? (v.y < -oob_coord) : false;
			bool oobz0 = (clipFlags & OOB_CLIP_Z) ? (v.z > oob_coord) : false;
			bool oobz1 = (clipFlags & OOB_CLIP_Z_NEG) ? (v.z < -oob_coord) : false;

			if (!oobx0 && !ooby0 && !oobz0 && !oobx1 && !ooby1 && !oobz1) {
				newEnts.push_back(ents[i]);
			}
		}

	}
	int deletedEnts = (int)ents.size() - (int)newEnts.size();
	if (deletedEnts)
		print_log("    Deleted {} entities\n", deletedEnts);
	ents = std::move(newEnts);

	unsigned char* oobFaces = new unsigned char[faceCount];
	memset(oobFaces, 0, faceCount * sizeof(bool));
	int oobFaceCount = 0;

	for (int i = 0; i < worldmodel.nFaces; i++) {
		BSPFACE32& face = faces[worldmodel.iFirstFace + i];

		bool inBounds = true;
		for (int e = 0; e < face.nEdges; e++) {
			int edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE32& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

			vec3 v = verts[vertIdx];

			bool oobx0 = (clipFlags & OOB_CLIP_X) ? (v.x > oob_coord) : false;
			bool oobx1 = (clipFlags & OOB_CLIP_X_NEG) ? (v.x < -oob_coord) : false;
			bool ooby0 = (clipFlags & OOB_CLIP_Y) ? (v.y > oob_coord) : false;
			bool ooby1 = (clipFlags & OOB_CLIP_Y_NEG) ? (v.y < -oob_coord) : false;
			bool oobz0 = (clipFlags & OOB_CLIP_Z) ? (v.z > oob_coord) : false;
			bool oobz1 = (clipFlags & OOB_CLIP_Z_NEG) ? (v.z < -oob_coord) : false;

			if (oobx0 || ooby0 || oobz0 || oobx1 || ooby1 || oobz1) {
				inBounds = false;
				break;
			}
		}

		if (!inBounds) {
			oobFaces[worldmodel.iFirstFace + i] = 1;
			oobFaceCount++;
		}
	}

	BSPFACE32* newFaces = new BSPFACE32[faceCount - oobFaceCount];

	int outIdx = 0;
	for (int i = 0; i < faceCount; i++) {
		if (!oobFaces[i]) {
			newFaces[outIdx++] = faces[i];
		}
	}

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		int offset = 0;
		int countReduce = 0;

		for (int k = 0; k < model.iFirstFace; k++) {
			offset += oobFaces[k];
		}
		for (int k = 0; k < model.nFaces; k++) {
			countReduce += oobFaces[model.iFirstFace + k];
		}

		model.iFirstFace -= offset;
		model.nFaces -= countReduce;
	}

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE32& node = nodes[i];

		int offset = 0;
		int countReduce = 0;

		for (int k = 0; k < node.iFirstFace; k++) {
			offset += oobFaces[k];
		}
		for (int k = 0; k < node.nFaces; k++) 
		{
			if (node.iFirstFace + k < faceCount)
				countReduce += oobFaces[node.iFirstFace + k];
		}

		node.iFirstFace -= offset;
		node.nFaces -= countReduce;
	}

	for (int i = 0; i < leafCount; i++) {
		BSPLEAF32& leaf = leaves[i];

		if (!leaf.nMarkSurfaces)
			continue;

		int oobCount = 0;

		for (int k = 0; k < leaf.nMarkSurfaces; k++) {
			if (oobFaces[marksurfs[leaf.iFirstMarkSurface + k]]) {
				oobCount++;
			}
		}

		if (oobCount) {
			leaf.nMarkSurfaces = 0;
			leaf.iFirstMarkSurface = 0;

			if (oobCount != leaf.nMarkSurfaces) {
				// always true
				//print_log("leaf {} partially OOB\n", i);
			}
		}
		else {
			for (int k = 0; k < leaf.nMarkSurfaces; k++) {
				int faceIdx = marksurfs[leaf.iFirstMarkSurface + k];

				int offset = 0;
				for (int j = 0; j < faceIdx; j++) {
					offset += oobFaces[j];
				}

				marksurfs[leaf.iFirstMarkSurface + k] = faceIdx - offset;
			}
		}
	}

	replace_lump(LUMP_FACES, newFaces, (faceCount - oobFaceCount) * sizeof(BSPFACE32));

	delete[] newFaces;
	delete[] oobFaces;

	worldmodel = models[0];

	vec3 mins, maxs;
	get_model_vertex_bounds(0, mins, maxs);

	vec3 buffer = vec3(64, 64, 128); // leave room for largest collision hull wall thickness
	worldmodel.nMins = mins - buffer;
	worldmodel.nMaxs = maxs + buffer;

	remove_unused_model_structures().print_delete_stats(1);
}


void Bsp::delete_box_nodes(int iNode, int* parentBranch, std::vector<BSPPLANE>& clipOrder,
	vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes) {
	BSPNODE32& node = nodes[iNode];

	if (node.iPlane < 0) {
		return;
	}

	bool isoob = isFirstPass ? true : oobHistory[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			delete_box_nodes(node.iChildren[i], &node.iChildren[i], clipOrder, clipMins, clipMaxs,
				oobHistory, isFirstPass, removedNodes);
			if (node.iChildren[i] >= 0) {
				isoob = false; // children weren't empty, so this node isn't empty either
			}
		}
		else if (isFirstPass) {
			std::vector<BSPPLANE> cuts;
			for (int k = (int)clipOrder.size() - 1; k >= 0; k--) {
				cuts.push_back(clipOrder[k]);
			}

			Clipper clipper;
			CMesh nodeVolume = clipper.clip(cuts);

			for (size_t k = 0; k < nodeVolume.verts.size(); k++) {
				if (!nodeVolume.verts[k].visible)
					continue;
				vec3 v = nodeVolume.verts[k].pos;

				if (!pointInBox(v, clipMins, clipMaxs)) {
					isoob = false; // node can't be empty if both children aren't oob
				}
			}
		}

		clipOrder.pop_back();
	}

	if (isFirstPass) {
		// only check if each node is ever considered in bounds, after considering all branches.
		// don't remove anything until the entire tree has been scanned

		if (!isoob) {
			oobHistory[iNode] = false;
		}
	}
	else if (parentBranch && isoob) {
		// we know which nodes are OOB now, so it's safe to unlink this node from the paranet
		*parentBranch = CONTENTS_SOLID;
		removedNodes++;
	}
}

void Bsp::delete_box_clipnodes(int iNode, int* parentBranch, std::vector<BSPPLANE>& clipOrder,
	vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes) {
	BSPCLIPNODE32& node = clipnodes[iNode];

	if (node.iPlane < 0) {
		return;
	}

	bool isoob = isFirstPass ? true : oobHistory[iNode];

	for (int i = 0; i < 2; i++) {
		BSPPLANE plane = planes[node.iPlane];
		if (i != 0) {
			plane.vNormal = plane.vNormal.invert();
			plane.fDist = -plane.fDist;
		}
		clipOrder.push_back(plane);

		if (node.iChildren[i] >= 0) {
			delete_box_clipnodes(node.iChildren[i], &node.iChildren[i], clipOrder, clipMins, clipMaxs,
				oobHistory, isFirstPass, removedNodes);
			if (node.iChildren[i] >= 0) {
				isoob = false; // children weren't empty, so this node isn't empty either
			}
		}
		else if (isFirstPass) {
			std::vector<BSPPLANE> cuts;
			for (int k = (int)clipOrder.size() - 1; k >= 0; k--) {
				cuts.push_back(clipOrder[k]);
			}

			Clipper clipper;
			CMesh nodeVolume = clipper.clip(cuts);

			vec3 mins(FLT_MAX, FLT_MAX, FLT_MAX);
			vec3 maxs(-FLT_MAX, -FLT_MAX, -FLT_MAX);

			for (size_t k = 0; k < nodeVolume.verts.size(); k++) {
				if (!nodeVolume.verts[k].visible)
					continue;
				vec3 v = nodeVolume.verts[k].pos;

				expandBoundingBox(v, mins, maxs);
			}

			if (!boxesIntersect(mins, maxs, clipMins, clipMaxs)) {
				isoob = false; // node can't be empty if both children aren't in the clip box
			}
		}

		clipOrder.pop_back();
	}

	if (isFirstPass) {
		// only check if each node is ever considered in bounds, after considering all branches.
		// don't remove anything until the entire tree has been scanned

		if (!isoob) {
			oobHistory[iNode] = false;
		}
	}
	else if (parentBranch && isoob) {
		// we know which nodes are OOB now, so it's safe to unlink this node from the paranet
		*parentBranch = CONTENTS_SOLID;
		removedNodes++;
	}
}

void Bsp::delete_box_data(vec3 clipMins, vec3 clipMaxs) {
	// TODO: most of this code is duplicated in delete_oob_*

	BSPMODEL& worldmodel = models[0];

	// remove nodes and clipnodes in the clipping box
	{
		std::vector<BSPPLANE> clipOrder;

		bool* oobMarks = new bool[nodeCount];

		// collect oob data, then actually remove the nodes
		int removedNodes = 0;
		do {
			removedNodes = 0;
			memset(oobMarks, 1, nodeCount * sizeof(bool)); // assume everything is oob at first
			delete_box_nodes(worldmodel.iHeadnodes[0], NULL, clipOrder, clipMins, clipMaxs, oobMarks, true, removedNodes);
			delete_box_nodes(worldmodel.iHeadnodes[0], NULL, clipOrder, clipMins, clipMaxs, oobMarks, false, removedNodes);
		} while (removedNodes);
		delete[] oobMarks;

		oobMarks = new bool[clipnodeCount];
		for (int i = 1; i < MAX_MAP_HULLS; i++) {
			// collect oob data, then actually remove the nodes
			removedNodes = 0;
			do {
				removedNodes = 0;
				memset(oobMarks, 1, clipnodeCount * sizeof(bool)); // assume everything is oob at first
				delete_box_clipnodes(worldmodel.iHeadnodes[i], NULL, clipOrder, clipMins, clipMaxs, oobMarks, true, removedNodes);
				delete_box_clipnodes(worldmodel.iHeadnodes[i], NULL, clipOrder, clipMins, clipMaxs, oobMarks, false, removedNodes);
			} while (removedNodes);
		}
		delete[] oobMarks;
	}

	std::vector<Entity*> newEnts;
	newEnts.push_back(ents[0]); // never remove worldspawn

	for (size_t i = 1; i < ents.size(); i++) {
		vec3 v = ents[i]->origin;
		int modelIdx = ents[i]->getBspModelIdx();

		if (modelIdx != -1) {
			vec3 mins, maxs;
			get_model_vertex_bounds(modelIdx, mins, maxs);
			mins += v;
			maxs += v;

			if (!boxesIntersect(mins, maxs, clipMins, clipMaxs)) {
				newEnts.push_back(ents[i]);
			}
		}
		else {
			bool isCullEnt = ents[i]->hasKey("classname") && ents[i]->keyvalues["classname"] == "cull";
			if (!pointInBox(v, clipMins, clipMaxs) || isCullEnt) {
				newEnts.push_back(ents[i]);
			}
		}

	}
	int deletedEnts = (int)ents.size() - (int)newEnts.size();
	if (deletedEnts)
		print_log("    Deleted {} entities\n", deletedEnts);
	ents = std::move(newEnts);

	unsigned char* oobFaces = new unsigned char[faceCount];
	memset(oobFaces, 0, faceCount * sizeof(bool));
	int oobFaceCount = 0;

	for (int i = 0; i < worldmodel.nFaces; i++) {
		BSPFACE32& face = faces[worldmodel.iFirstFace + i];

		bool isClipped = false;
		for (int e = 0; e < face.nEdges; e++) {
			int edgeIdx = surfedges[face.iFirstEdge + e];
			BSPEDGE32& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

			vec3 v = verts[vertIdx];

			if (pointInBox(v, clipMins, clipMaxs)) {
				isClipped = true;
				break;
			}
		}

		if (isClipped) {
			oobFaces[worldmodel.iFirstFace + i] = 1;
			oobFaceCount++;
		}
	}

	BSPFACE32* newFaces = new BSPFACE32[faceCount - oobFaceCount];

	int outIdx = 0;
	for (int i = 0; i < faceCount; i++) {
		if (!oobFaces[i]) {
			newFaces[outIdx++] = faces[i];
		}
	}

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		int offset = 0;
		int countReduce = 0;

		for (int k = 0; k < model.iFirstFace; k++) {
			offset += oobFaces[k];
		}
		for (int k = 0; k < model.nFaces; k++) {
			countReduce += oobFaces[model.iFirstFace + k];
		}

		model.iFirstFace -= offset;
		model.nFaces -= countReduce;
	}

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE32& node = nodes[i];

		int offset = 0;
		int countReduce = 0;

		for (int k = 0; k < node.iFirstFace; k++) {
			offset += oobFaces[k];
		}
		for (int k = 0; k < node.nFaces; k++) {
			if (node.iFirstFace + k < faceCount)
				countReduce += oobFaces[node.iFirstFace + k];
		}

		node.iFirstFace -= offset;
		node.nFaces -= countReduce;
	}

	for (int i = 0; i < leafCount; i++) {
		BSPLEAF32& leaf = leaves[i];

		if (!leaf.nMarkSurfaces)
			continue;

		int oobCount = 0;

		for (int k = 0; k < leaf.nMarkSurfaces; k++) {
			if (oobFaces[marksurfs[leaf.iFirstMarkSurface + k]]) {
				oobCount++;
			}
		}

		if (oobCount) {
			leaf.nMarkSurfaces = 0;
			leaf.iFirstMarkSurface = 0;

			if (oobCount != leaf.nMarkSurfaces) {
				// always true
				//print_log("leaf {} partially OOB\n", i);
			}
		}
		else {
			for (int k = 0; k < leaf.nMarkSurfaces; k++) {
				int faceIdx = marksurfs[leaf.iFirstMarkSurface + k];

				int offset = 0;
				for (int j = 0; j < faceIdx; j++) {
					offset += oobFaces[j];
				}

				marksurfs[leaf.iFirstMarkSurface + k] = faceIdx - offset;
			}
		}
	}

	replace_lump(LUMP_FACES, newFaces, (faceCount - oobFaceCount) * sizeof(BSPFACE32));

	delete[] newFaces;
	delete[] oobFaces;

	worldmodel = models[0];

	vec3 mins, maxs;
	get_model_vertex_bounds(0, mins, maxs);

	vec3 buffer = vec3(64, 64, 128); // leave room for largest collision hull wall thickness
	worldmodel.nMins = mins - buffer;
	worldmodel.nMaxs = maxs + buffer;

	remove_unused_model_structures().print_delete_stats(1);
}

void Bsp::count_leaves(int iNode, int& count) {
	BSPNODE32& node = nodes[iNode];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			count_leaves(node.iChildren[i], count);
		}
		else {
			int leafIdx = ~node.iChildren[i];
			if (leafIdx > count)
				count = leafIdx;
		}
	}
}

struct CompareVert {
	vec3 pos;
	float u, v;
	CompareVert()
	{
		u = v = 0.0f;
	}
};

struct ModelIdxRemap {
	int newIdx;
	vec3 offset;
	ModelIdxRemap()
	{
		newIdx = 0;
	}
};

void Bsp::deduplicate_models() {
	const float epsilon = 1.0f;

	std::map<int, ModelIdxRemap> modelRemap;

	for (int i = 1; i < modelCount; i++) {
		BSPMODEL& modelA = models[i];

		if (modelA.nFaces == 0)
			continue;

		if (modelRemap.find(i) != modelRemap.end()) {
			continue;
		}

		bool shouldCompareTextures = false;
		std::string modelKeyA = "*" + std::to_string(i);

		for (Entity* ent : ents) {
			if (ent->hasKey("model") && ent->keyvalues["model"] == modelKeyA) {
				if (ent->isEverVisible()) {
					shouldCompareTextures = true;
					break;
				}
			}
		}

		for (int k = 1; k < modelCount; k++) {
			if (i == k)
				continue;

			BSPMODEL& modelB = models[k];

			if (modelA.nFaces != modelB.nFaces)
				continue;

			vec3 minsA, maxsA, minsB, maxsB;
			get_model_vertex_bounds(i, minsA, maxsA);
			get_model_vertex_bounds(k, minsB, maxsB);

			vec3 sizeA = maxsA - minsA;
			vec3 sizeB = maxsB - minsB;

			if ((sizeB - sizeA).length() > epsilon) {
				continue;
			}

			if (!shouldCompareTextures) {
				std::string modelKeyB = "*" + std::to_string(k);

				for (Entity* ent : ents) {
					if (ent->hasKey("model") && ent->keyvalues["model"] == modelKeyB) {
						if (ent->isEverVisible()) {
							shouldCompareTextures = true;
							break;
						}
					}
				}
			}

			bool similarFaces = true;
			for (int fa = 0; fa < modelA.nFaces; fa++) {
				BSPFACE32& faceA = faces[modelA.iFirstFace + fa];
				BSPTEXTUREINFO& infoA = texinfos[faceA.iTextureInfo];
				BSPPLANE& planeA = planes[faceA.iPlane];
				int texOffset = ((int*)textures)[infoA.iMiptex + 1];
				BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
				float tw = 1.0f / (float)tex.nWidth;
				float th = 1.0f / (float)tex.nHeight;

				std::vector<CompareVert> vertsA;
				for (int e = 0; e < faceA.nEdges; e++) {
					int edgeIdx = surfedges[faceA.iFirstEdge + e];
					BSPEDGE32& edge = edges[abs(edgeIdx)];
					int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

					CompareVert v;
					v.pos = verts[vertIdx];

					float fU = dotProduct(infoA.vS, v.pos) + infoA.shiftS;
					float fV = dotProduct(infoA.vT, v.pos) + infoA.shiftT;
					v.u = fU * tw;
					v.v = fV * th;

					// wrap coords
					v.u = v.u > 0 ? (v.u - (int)v.u) : 1.0f - (v.u - (int)v.u);
					v.v = v.v > 0 ? (v.v - (int)v.v) : 1.0f - (v.v - (int)v.v);

					vertsA.push_back(v);
					//print_log("A Face {} vert {} uv: {} {}\n", fa, e, v.u, v.v);
				}

				bool foundMatch = false;
				for (int fb = 0; fb < modelB.nFaces; fb++) {
					BSPFACE32& faceB = faces[modelB.iFirstFace + fb];
					BSPTEXTUREINFO& infoB = texinfos[faceB.iTextureInfo];
					BSPPLANE& planeB = planes[faceB.iPlane];

					if ((!shouldCompareTextures || infoA.iMiptex == infoB.iMiptex)
						&& planeA.vNormal == planeB.vNormal
						&& faceA.nPlaneSide == faceB.nPlaneSide) {
						// face planes and textures match
						// now check if vertices have same relative positions and texture coords

						std::vector<CompareVert> vertsB;
						for (int e = 0; e < faceB.nEdges; e++) {
							int edgeIdx = surfedges[faceB.iFirstEdge + e];
							BSPEDGE32& edge = edges[abs(edgeIdx)];
							int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

							CompareVert v;
							v.pos = verts[vertIdx];

							float fU = dotProduct(infoB.vS, v.pos) + infoB.shiftS;
							float fV = dotProduct(infoB.vT, v.pos) + infoB.shiftT;
							v.u = fU * tw;
							v.v = fV * th;

							// wrap coords
							v.u = v.u > 0 ? (v.u - (int)v.u) : 1.0f - (v.u - (int)v.u);
							v.v = v.v > 0 ? (v.v - (int)v.v) : 1.0f - (v.v - (int)v.v);

							vertsB.push_back(v);
							//print_log("B Face {} vert {} uv: {} {}\n", fb, e, v.u, v.v);
						}

						bool vertsMatch = true;
						for (CompareVert& vertA : vertsA) {
							bool foundVertMatch = false;

							for (CompareVert& vertB : vertsB) {

								float diffU = fabs(vertA.u - vertB.u);
								float diffV = fabs(vertA.v - vertB.v);
								const float uvEpsilon = 0.005f;

								bool uvsMatch = !shouldCompareTextures ||
									((diffU < uvEpsilon || fabs(diffU - 1.0f) < uvEpsilon)
										&& (diffV < uvEpsilon || fabs(diffV - 1.0f) < uvEpsilon));

								if (((vertA.pos - minsA) - (vertB.pos - minsB)).length() < epsilon
									&& uvsMatch) {
									foundVertMatch = true;
									break;
								}
							}

							if (!foundVertMatch) {
								vertsMatch = false;
								break;
							}
						}

						if (vertsMatch) {
							foundMatch = true;
							break;
						}
					}
				}

				if (!foundMatch) {
					similarFaces = false;
					break;
				}
			}

			if (!similarFaces)
				continue;

			//print_log("Model {} and {} seem very similar ({} faces)\n", i, k, modelA.nFaces);
			ModelIdxRemap remap;
			remap.newIdx = i;
			remap.offset = minsB - minsA;
			modelRemap[k] = remap;
		}
	}

	print_log("Remapped {} BSP model references\n", modelRemap.size());

	for (Entity* ent : ents) {
		if (!ent->keyvalues.count("model")) {
			continue;
		}

		std::string model = ent->keyvalues["model"];

		if (model[0] != '*')
			continue;

		int modelIdx = atoi(model.substr(1).c_str());

		if (modelRemap.find(modelIdx) != modelRemap.end()) {
			ModelIdxRemap remap = modelRemap[modelIdx];

			ent->setOrAddKeyvalue("origin", (ent->origin + remap.offset).toKeyvalueString());
			ent->setOrAddKeyvalue("model", "*" + std::to_string(remap.newIdx));
		}
	}
}

float Bsp::calc_allocblock_usage() {
	int total = 0;

	for (int i = 0; i < faceCount; i++) {
		int size[2];
		GetFaceLightmapSize(i, size);

		total += size[0] * size[1];
	}

	const int allocBlockSize = 128 * 128;

	return total / (float)allocBlockSize;
}

void Bsp::allocblock_reduction() {
	int scaleCount = 0;

	for (int i = 1; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (model.nFaces == 0)
			continue;

		bool isVisibleModel = false;
		std::string modelKey = "*" + std::to_string(i);

		for (Entity* ent : ents) {
			if (ent->hasKey("model") && ent->keyvalues["model"] == modelKey) {
				if (ent->isEverVisible()) {
					isVisibleModel = true;
					break;
				}
			}
		}

		if (isVisibleModel)
			continue;
		for (int fa = 0; fa < model.nFaces; fa++) {
			BSPFACE32& face = faces[model.iFirstFace + fa];
			BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
			info.vS = info.vS.normalize(0.01f);
			info.vT = info.vT.normalize(0.01f);
		}

		scaleCount++;
		print_log("Scale up model {}\n", i);
	}

	print_log("Scaled up textures on {} invisible models\n", scaleCount);
}

bool Bsp::subdivide_face(int faceIdx) {
	BSPFACE32& face = faces[faceIdx];
	BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

	std::vector<vec3> faceVerts;
	for (int e = 0; e < face.nEdges; e++) {
		int edgeIdx = surfedges[face.iFirstEdge + e];
		BSPEDGE32& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[0] : edge.iVertex[1];

		faceVerts.push_back(verts[vertIdx]);
	}

	Polygon3D poly(faceVerts);

	vec3 minVertU, maxVertU;
	vec3 minVertV, maxVertV;

	float minU = FLT_MAX;
	float maxU = -FLT_MAX;
	float minV = FLT_MAX;
	float maxV = -FLT_MAX;
	for (size_t i = 0; i < faceVerts.size(); i++) {
		vec3& pos = faceVerts[i];

		float u = dotProduct(info.vS, pos);
		float v = dotProduct(info.vT, pos);

		if (u < minU) {
			minU = u;
			minVertU = pos;
		}
		if (u > maxU) {
			maxU = u;
			maxVertU = pos;
		}
		if (v < minV) {
			minV = v;
			minVertV = pos;
		}
		if (v > maxV) {
			maxV = v;
			maxVertV = pos;
		}
	}
	vec2 axisU = poly.project(info.vS).normalize();
	vec2 axisV = poly.project(info.vT).normalize();

	vec2 midVertU = poly.project(minVertU + (maxVertU - minVertU) * 0.5f);
	vec2 midVertV = poly.project(minVertV + (maxVertV - minVertV) * 0.5f);

	Line2D ucut(midVertU + axisV * 1000.0f, midVertU + axisV * -1000.0f);
	Line2D vcut(midVertV + axisU * 1000.0f, midVertV + axisU * -1000.0f);

	int size[2];
	GetFaceLightmapSize(faceIdx, size);

	Line2D& cutLine = size[0] > size[1] ? ucut : vcut;

	std::vector<std::vector<vec3>> polys = poly.cut(cutLine);

	if (polys.empty()) {
		int texOffset = ((int*)textures)[info.iMiptex + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
		vec3 center = get_face_center(faceIdx);
		print_log("Failed to subdivide face {} {} ({} {} {})\n", faceIdx, tex.szName,
			(int)center.x, (int)center.y, (int)center.z);
		return false;
	}

	size_t addVerts = polys[0].size() + polys[1].size();

	BSPFACE32* newFaces = new BSPFACE32[faceCount + 1];
	memcpy(newFaces, faces, faceIdx * sizeof(BSPFACE32));
	memcpy(newFaces + faceIdx + 1, faces + faceIdx, (faceCount - faceIdx) * sizeof(BSPFACE32));

	int addMarks = 0;
	for (int i = 0; i < marksurfCount; i++) {
		if (marksurfs[i] == faceIdx) {
			addMarks++;
		}
	}
	int totalMarks = marksurfCount + addMarks;
	int* newMarkSurfs = new int[totalMarks + 1];
	memcpy(newMarkSurfs, marksurfs, marksurfCount * sizeof(int));

	BSPEDGE32* newEdges = new BSPEDGE32[edgeCount + addVerts];
	memcpy(newEdges, edges, edgeCount * sizeof(BSPEDGE32));

	vec3* newVerts = new vec3[vertCount + addVerts];
	memcpy(newVerts, verts, vertCount * sizeof(vec3));

	int* newSurfEdges = new int[surfedgeCount + addVerts];
	memcpy(newSurfEdges, surfedges, surfedgeCount * sizeof(int));

	BSPEDGE32* edgePtr = newEdges + edgeCount;
	vec3* vertPtr = newVerts + vertCount;
	int* surfedgePtr = newSurfEdges + surfedgeCount;

	for (int k = 0; k < 2; k++) {
		std::vector<vec3>& cutPoly = polys[k];

		newFaces[faceIdx + k] = faces[faceIdx];
		newFaces[faceIdx + k].iFirstEdge = (int)(surfedgePtr - newSurfEdges);
		newFaces[faceIdx + k].nEdges = (int)cutPoly.size();

		int vertOffset = (int)(vertPtr - newVerts);
		int edgeOffset = (int)(edgePtr - newEdges);

		for (int i = 0; i < (int)cutPoly.size(); i++) {
			edgePtr->iVertex[0] = vertOffset + i;
			edgePtr->iVertex[1] = vertOffset + ((i + 1) % cutPoly.size());
			edgePtr++;

			*vertPtr++ = cutPoly[i];

			// TODO: make fewer edges and make use of both vertexes?
			*surfedgePtr++ = -(edgeOffset + i);
		}
	}

	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (model.iFirstFace > faceIdx) {
			model.iFirstFace += 1;
		}
		else if (model.iFirstFace + model.nFaces > faceIdx) {
			model.nFaces++;
		}
	}

	for (int i = 0; i < nodeCount; i++) {
		BSPNODE32& node = nodes[i];

		if (node.iFirstFace > faceIdx) {
			node.iFirstFace += 1;
		}
		else if (node.iFirstFace + node.nFaces > faceIdx) {
			node.nFaces++;
		}
	}

	for (int i = 0; i < totalMarks; i++)
	{
		if (newMarkSurfs[i] == faceIdx)
		{
			memmove(newMarkSurfs + i + 1, newMarkSurfs + i, (totalMarks - (i + 1)) * sizeof(unsigned int));
			newMarkSurfs[i + 1] = faceIdx + 1;

			for (int k = 0; k < leafCount; k++) {
				BSPLEAF32& leaf = leaves[k];

				if (!leaf.nMarkSurfaces)
					continue;
				else if (leaf.iFirstMarkSurface > i) {
					leaf.iFirstMarkSurface += 1;
				}
				else if (leaf.iFirstMarkSurface + leaf.nMarkSurfaces > i) {
					//print_log("Added mark {}/{} to leaf {} ({} + {})\n", i, marksurfCount, k, leaf.iFirstMarkSurface, leaf.nMarkSurfaces);
					leaf.nMarkSurfaces += 1;
				}
			}

			i++; // skip the other side of the subdivided face, or else it triggers the next block
		}
		else if (newMarkSurfs[i] > faceIdx) {
			newMarkSurfs[i]++;
		}
	}

	replace_lump(LUMP_MARKSURFACES, newMarkSurfs, totalMarks * sizeof(unsigned int));
	replace_lump(LUMP_FACES, newFaces, (faceCount + 1) * sizeof(BSPFACE32));
	replace_lump(LUMP_EDGES, newEdges, (edgeCount + addVerts) * sizeof(BSPEDGE32));
	replace_lump(LUMP_SURFEDGES, newSurfEdges, (surfedgeCount + addVerts) * sizeof(int));
	replace_lump(LUMP_VERTICES, newVerts, (vertCount + addVerts) * sizeof(vec3));

	delete[] newMarkSurfs;
	delete[] newEdges;
	delete[] newSurfEdges;
	delete[] newVerts;
	return true;
}

void Bsp::fix_bad_surface_extents_with_subdivide(int faceIdx)
{
	// f... ?
	std::vector<int> tmpfaces;
	tmpfaces.push_back(faceIdx);

	int totalFaces = 1;

	while (tmpfaces.size()) {
		int size[2];
		int i = tmpfaces[tmpfaces.size() - 1];
		if (GetFaceLightmapSize(i, size)) {
			tmpfaces.pop_back();
			continue;
		}

		// adjust face indexes if about to split a face with a lower index 
		for (int n = 0; n < (int)tmpfaces.size(); n++) {
			if (tmpfaces[n] > n) {
				tmpfaces[n]++;
			}
		}

		totalFaces++;
		subdivide_face(i);
		tmpfaces.push_back(i + 1);
		tmpfaces.push_back(i);
	}

	print_log("Subdivided into {} faces\n", totalFaces);
}

void Bsp::fix_bad_surface_extents(bool scaleNotSubdivide, bool downscaleOnly, int maxTextureDim) {
	int numSub = 0;
	int numScale = 0;
	int numShrink = 0;
	bool anySubdivides = true;

	if (scaleNotSubdivide) {
		// create unique texinfos in case any are shared with both good and bad faces
		for (int fa = 0; fa < faceCount; fa++) {
			int faceIdx = fa;
			BSPFACE32& face = faces[faceIdx];
			BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

			if (info.nFlags & TEX_SPECIAL) {
				continue;
			}

			int size[2];
			if (GetFaceLightmapSize(faceIdx, size)) {
				continue;
			}

			get_unique_texinfo(faceIdx);
		}
	}

	while (anySubdivides) {
		anySubdivides = false;
		for (int fa = 0; fa < faceCount; fa++) {
			int faceIdx = fa;
			BSPFACE32& face = faces[faceIdx];
			BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

			if (info.nFlags & TEX_SPECIAL) {
				continue;
			}

			int size[2];
			if (GetFaceLightmapSize(faceIdx, size)) {
				continue;
			}

			if (maxTextureDim > 0 && downscale_texture(info.iMiptex, maxTextureDim, false)) {
				// retry after downscaling
				numShrink++;
				fa--;
				continue;
			}

			if (downscaleOnly) {
				continue;
			}

			if (!scaleNotSubdivide) {
				if (subdivide_face(faceIdx)) {
					numSub++;
					anySubdivides = true;
					break;
				}
				// else scale the face because it was too skinny to be subdivided or something
			}

			vec2 oldScale(1.0f / info.vS.length(), 1.0f / info.vT.length());

			bool scaledOk = false;
			for (int i = 0; i < 128; i++) {
				info.vS *= 0.5f;
				info.vT *= 0.5f;

				if (GetFaceLightmapSize(faceIdx, size)) {
					scaledOk = true;
					break;
				}
			}

			if (!scaledOk) {
				int texOffset = ((int*)textures)[info.iMiptex + 1];
				BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
				print_log("Failed to fix face {} with scales {} {}\n", tex.szName, oldScale.x, oldScale.y);
			}
			else {
				int texOffset = ((int*)textures)[info.iMiptex + 1];
				BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
				vec2 newScale(1.0f / info.vS.length(), 1.0f / info.vT.length());

				vec3 center = get_face_center(faceIdx);
				print_log("Scaled up {} from {}x{} -> {}x{} ({} {} {})\n",
					tex.szName, oldScale.x, oldScale.y, newScale.x, newScale.y,
					(int)center.x, (int)center.y, (int)center.z);
				numScale++;
			}
		}
	}

	if (numScale) {
		print_log("Scaled up {} face textures\n", numScale);
	}
	if (numSub) {
		print_log("Subdivided {} faces\n", numSub);
	}
	if (numShrink) {
		print_log("Downscaled {} textures\n", numShrink);
	}
}

vec3 Bsp::get_face_center(int faceIdx) {
	BSPFACE32& face = faces[faceIdx];

	vec3 centroid;

	for (int k = 0; k < face.nEdges; k++) {
		int edgeIdx = surfedges[face.iFirstEdge + k];
		BSPEDGE32& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		centroid += verts[vertIdx];
	}

	return centroid / (float)face.nEdges;
}

bool Bsp::downscale_texture(int textureId, int newWidth, int newHeight) {
	if ((newWidth % 16 != 0) || (newHeight % 16 != 0) || newWidth <= 0 || newHeight <= 0) {
		print_log("Invalid downscale dimensions: {}x{}\n", newWidth, newHeight);
		return false;
	}

	int texOffset = ((int*)textures)[textureId + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

	int oldWidth = tex.nWidth;
	int oldHeight = tex.nHeight;

	tex.nWidth = newWidth;
	tex.nHeight = newHeight;

	int lastMipSize = (oldWidth >> 3) * (oldHeight >> 3);
	unsigned char* palette = (unsigned char*)(textures + texOffset + tex.nOffsets[3] + lastMipSize);

	int oldWidths[4];
	int oldHeights[4];
	int newWidths[4];
	int newHeights[4];
	int newOffset[4];
	for (int i = 0; i < 4; i++) {
		oldWidths[i] = oldWidth >> (1 * i);
		oldHeights[i] = oldHeight >> (1 * i);
		newWidths[i] = tex.nWidth >> (1 * i);
		newHeights[i] = tex.nHeight >> (1 * i);

		if (i > 0) {
			newOffset[i] = newOffset[i - 1] + newWidths[i - 1] * newHeights[i - 1];
		}
		else {
			newOffset[i] = sizeof(BSPMIPTEX);
		}
	}
	unsigned char* newPalette = (unsigned char*)(textures + texOffset + newOffset[3] + newWidths[3] * newHeights[3]);

	float srcScaleX = (float)oldWidth / tex.nWidth;
	float srcScaleY = (float)oldHeight / tex.nHeight;

	for (int i = 0; i < 4; i++) {
		unsigned char* srcData = (unsigned char*)(textures + texOffset + tex.nOffsets[i]);
		unsigned char* dstData = (unsigned char*)(textures + texOffset + newOffset[i]);
		int srcWidth = oldWidths[i];
		int dstWidth = newWidths[i];

		for (int y = 0; y < newHeights[i]; y++) {
			int srcY = (int)(srcScaleY * y + 0.5f);

			for (int x = 0; x < newWidths[i]; x++) {
				int srcX = (int)(srcScaleX * x + 0.5f);

				dstData[y * dstWidth + x] = srcData[srcY * srcWidth + srcX];
			}
		}
	}
	// 2 = palette color count (should always be 256)
	memcpy(newPalette, palette, 256 * sizeof(COLOR3) + 2);

	for (int i = 0; i < 4; i++) {
		tex.nOffsets[i] = newOffset[i];
	}

	adjust_downscaled_texture_coordinates(textureId, oldWidth, oldHeight);

	// shrink texture lump
	int removedBytes = (int)(palette - newPalette);
	unsigned char* texEnd = newPalette + 256 * sizeof(COLOR3);
	int shiftBytes = (int)(texEnd - textures) + removedBytes;

	memcpy(texEnd, texEnd + removedBytes, bsp_header.lump[LUMP_TEXTURES].nLength - shiftBytes);
	for (int k = textureId + 1; k < textureCount; k++) {
		((int*)textures)[k + 1] -= removedBytes;
	}

	/*for (int i = 0; i < textureCount; i++) {
		int tmpOffset = ((int*)textures)[i + 1];
		BSPMIPTEX& tmpTex = *((BSPMIPTEX*)(textures + tmpOffset));
		// f.... ?
	}
	*/

	print_log("Downscale {} {}x{} -> {}x{}\n", tex.szName, oldWidth, oldHeight, tex.nWidth, tex.nHeight);

	return true;
}

bool Bsp::downscale_texture(int textureId, int maxDim, bool allowWad) {
	int texOffset = ((int*)textures)[textureId + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

	int oldWidth = tex.nWidth;
	int oldHeight = tex.nHeight;
	int newWidth = tex.nWidth;
	int newHeight = tex.nHeight;

	if (tex.nWidth > maxDim && tex.nWidth > tex.nHeight) {
		float ratio = oldHeight / (float)oldWidth;
		newWidth = maxDim;
		newHeight = (int)(((newWidth * ratio) + 8) / 16) * 16;
		if (newHeight > oldHeight) {
			newHeight = (int)((newWidth * ratio) / 16) * 16;
		}
	}
	else if (tex.nHeight > maxDim) {
		float ratio = oldWidth / (float)oldHeight;
		newHeight = maxDim;
		newWidth = (int)(((newHeight * ratio) + 8) / 16) * 16;
		if (newWidth > oldWidth) {
			newWidth = (int)((newHeight * ratio) / 16) * 16;
		}
	}
	else {
		return false; // no need to downscale
	}

	if (oldWidth == newWidth && oldHeight == newHeight) {
		print_log("Failed to downscale texture {} {}x{} to max dim {}\n", tex.szName, oldWidth, oldHeight, maxDim);
		return false;
	}

	if (tex.nOffsets[0] == 0) {
		if (allowWad) {
			tex.nWidth = newWidth;
			tex.nHeight = newHeight;
			adjust_downscaled_texture_coordinates(textureId, oldWidth, oldHeight);
			print_log("Texture coords were updated for {}. The WAD texture must be updated separately.\n", tex.szName);
		}
		else {
			print_log("Can't downscale WAD texture {}\n", tex.szName);
		}

		return false;
	}

	return downscale_texture(textureId, newWidth, newHeight);
}

void Bsp::adjust_downscaled_texture_coordinates(int textureId, int oldWidth, int oldHeight) {
	int texOffset = ((int*)textures)[textureId + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

	int newWidth = tex.nWidth;
	int newHeight = tex.nHeight;

	// scale up face texture coordinates
	float scaleX = newWidth / (float)oldWidth;
	float scaleY = newHeight / (float)oldHeight;

	for (int i = 0; i < faceCount; i++) {
		BSPFACE32& face = faces[i];

		if (texinfos[face.iTextureInfo].iMiptex != textureId)
			continue;

		// each affected face should have a unique texinfo because
		// the shift amount may be different for every face after scaling
		BSPTEXTUREINFO* info = get_unique_texinfo(i);

		// get any vert on the face to use a reference point. Why?
		// When textures are scaled, the texture relative to the face will depend on how far away its
		// vertices are from the world origin. This means faces far away from the world origin shift many
		// pixels per scale unit, and faces aligned with the world origin don't shift at all when scaled.
		int edgeIdx = surfedges[face.iFirstEdge];
		BSPEDGE32& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		vec3 vert = verts[vertIdx];

		vec3 oldvs = info->vS;
		vec3 oldvt = info->vT;
		info->vS *= scaleX;
		info->vT *= scaleY;

		// get before/after uv coordinates
		float oldu = (dotProduct(oldvs, vert) + info->shiftS) * (1.0f / (float)oldWidth);
		float oldv = (dotProduct(oldvt, vert) + info->shiftT) * (1.0f / (float)oldHeight);
		float u = dotProduct(info->vS, vert) + info->shiftS;
		float v = dotProduct(info->vT, vert) + info->shiftT;

		// undo the shift in uv coordinates for this face
		info->shiftS += (oldu * newWidth) - u;
		info->shiftT += (oldv * newHeight) - v;
	}
}

void Bsp::downscale_invalid_textures() {
	int count = 0;

	for (int i = 0; i < textureCount; i++) {
		int texOffset = ((int*)textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		if (tex.nOffsets[0] == 0) {
			print_log("Skipping WAD texture {}\n", tex.szName);
			continue;
		}

		if ((unsigned int)tex.nWidth * tex.nHeight > g_limits.maxTextureSize) {

			int oldWidth = tex.nWidth;
			int oldHeight = tex.nHeight;
			int newWidth = tex.nWidth;
			int newHeight = tex.nHeight;

			float ratio = oldHeight / (float)oldWidth;

			while (newWidth > 16) {
				newWidth -= 16;
				newHeight = (int)(newWidth * ratio);

				if (newHeight % 16 != 0) {
					continue;
				}

				if ((unsigned int)newWidth * newHeight <= g_limits.maxTextureSize) {
					break;
				}
			}

			downscale_texture(i, newWidth, newHeight);
			count++;
		}
	}

	print_log("Downscaled {} textures\n", count);
}

bool Bsp::rename_texture(const char* oldName, const char* newName) {
	if (strlen(newName) > 16) {
		print_log("ERROR: New texture name longer than 15 characters ({})\n", strlen(newName));
		return false;
	}

	for (int i = 0; i < textureCount; i++) {
		int texOffset = ((int*)textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		if (!strncmp(tex.szName, oldName, 16)) {
			strncpy(tex.szName, newName, 16);
			print_log("Renamed texture '{}' -> '{}'\n", oldName, newName);
			return true;
		}
	}

	print_log("No texture found with name '{}'\n", oldName);
	return false;
}

std::set<int> Bsp::selectConnectedTexture(int modelId, int faceId) {
	std::set<int> selected;
	const float epsilon = 1.0f;

	BSPMODEL& model = models[modelId];

	BSPFACE32& face = faces[faceId];
	BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
	BSPPLANE& plane = planes[face.iPlane];

	std::vector<vec3> selectedVerts;
	for (int e = 0; e < face.nEdges; e++) {
		int edgeIdx = surfedges[face.iFirstEdge + e];
		BSPEDGE32& edge = edges[abs(edgeIdx)];
		int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
		selectedVerts.push_back(verts[vertIdx]);
	}

	bool anyNewFaces = true;
	while (anyNewFaces) {
		anyNewFaces = false;

		print_log("Loop again!\n");
		for (int fa = 0; fa < model.nFaces; fa++) {
			int testFaceIdx = model.iFirstFace + fa;
			BSPFACE32& faceA = faces[testFaceIdx];
			BSPTEXTUREINFO& infoA = texinfos[faceA.iTextureInfo];
			BSPPLANE& planeA = planes[faceA.iPlane];

			if (planeA.vNormal != plane.vNormal || info.iMiptex != infoA.iMiptex || selected.count(testFaceIdx)) {
				continue;
			}

			std::vector<vec3> uniqueVerts;
			bool isConnected = false;

			for (int e = 0; e < faceA.nEdges && !isConnected; e++) {
				int edgeIdx = surfedges[faceA.iFirstEdge + e];
				BSPEDGE32& edge = edges[abs(edgeIdx)];
				int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];

				vec3 v2 = verts[vertIdx];
				for (vec3 v1 : selectedVerts) {
					if ((v1 - v2).length() < epsilon) {
						isConnected = true;
						break;
					}
				}
			}

			// shares an edge. Select this face
			if (isConnected) {
				for (int e = 0; e < faceA.nEdges; e++) {
					int edgeIdx = surfedges[faceA.iFirstEdge + e];
					BSPEDGE32& edge = edges[abs(edgeIdx)];
					int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
					selectedVerts.push_back(verts[vertIdx]);
				}

				selected.insert(testFaceIdx);
				anyNewFaces = true;
				print_log("Select {} add {}\n", testFaceIdx, uniqueVerts.size());
			}
		}
	}

	return selected;
}

bool Bsp::is_invisible_solid(Entity* ent)
{
	if (!ent->isBspModel())
		return false;

	std::string tname = ent->keyvalues["targetname"];
	int rendermode = str_to_int(ent->keyvalues["rendermode"]);
	int renderamt = str_to_int(ent->keyvalues["renderamt"]);
	int renderfx = str_to_int(ent->keyvalues["renderfx"]);

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
	std::stringstream ent_data{};

	for (int i = 0; i < (int)ents.size(); i++)
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

		if (i < (int)ents.size() - 1)
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
	delete[] newEntData;
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
	if (g_settings.savebackup && fileExists(path) && !fileExists(path + ".bak"))
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

	auto backupLumps = duplicate_lumps();

	std::ofstream file(path, std::ios::trunc | std::ios::binary);
	if (!file.is_open())
	{
		print_log(get_localized_string(LANG_0075), path);
		return;
	}

	if (!is_bsp2 || !is_bsp2_old)
	{
		is_bsp_pathos = false;
	}

	//if (is_bsp2_old)
	//{
	//	is_bsp2_old = false;
	//	is_bsp2 = true;
	//	bsp_header.nVersion = 30;
	//}

	Entity* world = getWorldspawnEnt();

	if (g_settings.save_cam)
	{
		if (world)
		{
			if (!save_cam_pos.IsZero())
				world->setOrAddKeyvalue("camera_pos", save_cam_pos.toKeyvalueString());
			if (!save_cam_angles.IsZero())
				world->setOrAddKeyvalue("camera_angles", save_cam_angles.toKeyvalueString());

			update_ent_lump();
		}
	}
	// convert textures

	if (target_save_texture_has_pal != is_texture_has_pal)
	{
		createDir(g_working_dir);
		removeFile(g_working_dir + "temp.wad");
		if (ExportEmbeddedWad(g_working_dir + "temp.wad"))
		{
			is_texture_has_pal = target_save_texture_has_pal;
			ImportWad(g_working_dir + "temp.wad");
			removeFile(g_working_dir + "temp.wad");
		}
	}

	update_lump_pointers();
	std::vector<unsigned char> nulls(is_bsp30ext && extralumps.size() ? sizeof(BSPHEADER) + sizeof(BSPHEADER_EX) : sizeof(BSPHEADER) + (is_bsp_pathos ? 4 : 0), 0);
	file.write((const char*)nulls.data(), nulls.size());

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
		lumps[LUMP_LIGHTING].assign((unsigned char*)freelighting, (unsigned char*)(freelighting)+bsp_header.lump[LUMP_LIGHTING].nLength);

		//int offset = 0;

		for (int n = 0; n < faceCount; n++)
		{
			faces[n].nLightmapOffset /= sizeof(COLOR3);
		}
	}

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
		lumps[LUMP_CLIPNODES].assign((unsigned char*)freeClipnodes16, (unsigned char*)(freeClipnodes16)+bsp_header.lump[LUMP_CLIPNODES].nLength);
	}

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
		lumps[LUMP_NODES].assign((unsigned char*)freenodes16, (unsigned char*)(freenodes16)+bsp_header.lump[LUMP_NODES].nLength);
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
		lumps[LUMP_NODES].assign((unsigned char*)freenodes32a, (unsigned char*)(freenodes32a)+bsp_header.lump[LUMP_NODES].nLength);
	}

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
		lumps[LUMP_FACES].assign((unsigned char*)freefaces16, (unsigned char*)(freefaces16)+bsp_header.lump[LUMP_FACES].nLength);
	}

	unsigned short* freemarksurfs16 = NULL;

	if (!is_bsp2)
	{
		freemarksurfs16 = new unsigned short[marksurfCount];
		for (int n = 0; n < marksurfCount; n++)
		{
			freemarksurfs16[n] = (unsigned short)marksurfs[n];
		}
		bsp_header.lump[LUMP_MARKSURFACES].nLength = marksurfCount * sizeof(unsigned short);
		lumps[LUMP_MARKSURFACES].assign((unsigned char*)freemarksurfs16, (unsigned char*)(freemarksurfs16)+bsp_header.lump[LUMP_MARKSURFACES].nLength);
	}

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
		lumps[LUMP_LEAVES].assign((unsigned char*)freeleaves16, (unsigned char*)(freeleaves16)+bsp_header.lump[LUMP_LEAVES].nLength);
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
		lumps[LUMP_LEAVES].assign((unsigned char*)freeleaves32a, (unsigned char*)(freeleaves32a)+bsp_header.lump[LUMP_LEAVES].nLength);
	}

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
		lumps[LUMP_EDGES].assign((unsigned char*)freeedges16, (unsigned char*)(freeedges16)+bsp_header.lump[LUMP_EDGES].nLength);
	}


	if (is_blue_shift)
	{
		std::swap(bsp_header.lump[LUMP_PLANES], bsp_header.lump[LUMP_ENTITIES]);
		std::swap(lumps[LUMP_PLANES], lumps[LUMP_ENTITIES]);
		update_lump_pointers();
	}

	if (is_protected)
	{
		if (surfedgeCount > 0)
		{
			if (surfedges[surfedgeCount - 1] != 0)
			{
				int* newsurfs = new int[surfedgeCount + 1];
				memcpy(newsurfs, surfedges, surfedgeCount * sizeof(int));
				newsurfs[surfedgeCount] = 0;
				surfedgeCount++;
				bsp_header.lump[LUMP_SURFEDGES].nLength = (surfedgeCount) * sizeof(int);
				lumps[LUMP_SURFEDGES].assign((unsigned char*)newsurfs, (unsigned char*)(newsurfs)+bsp_header.lump[LUMP_SURFEDGES].nLength);
			}
		}
	}

	if (g_settings.save_crc && !force_skip_crc)
	{
		if (world && world->hasKey("CRC"))
		{
			originCrc32 = reverse_bits(std::stoul(world->keyvalues["CRC"]));
			print_log("SPOOFING CRC value.\nLoading original CRC key from WORLDSPAWN: {}. ",
				reverse_bits(originCrc32));
		}
		else
			print_log(get_localized_string(LANG_0076), reverse_bits(originCrc32));

		unsigned int crc32 = UINT32_C(0xFFFFFFFF);

		for (int i = 0; i < HEADER_LUMPS; i++)
		{
			if (i != LUMP_ENTITIES)
				crc32 = GetCrc32InMemory(lumps[i].data(), bsp_header.lump[i].nLength, crc32);
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

			memcpy(tmpNewModelds, lumps[LUMP_MODELS].data(), originsize);

			BSPMODEL* lastmodel = (BSPMODEL*)(tmpNewModelds + (modelCount * sizeof(BSPMODEL)));

			lastmodel->vOrigin.z = 9999.0f;

			bsp_header.lump[LUMP_MODELS].nLength = originsize + sizeof(BSPMODEL);

			lumps[LUMP_MODELS].assign((unsigned char*)tmpNewModelds, (unsigned char*)(tmpNewModelds)+bsp_header.lump[LUMP_MODELS].nLength);

			delete[] tmpNewModelds;

			crc32 = UINT32_C(0xFFFFFFFF);


			for (int i = 0; i < HEADER_LUMPS; i++)
			{
				if (i != LUMP_ENTITIES)
					crc32 = GetCrc32InMemory(lumps[i].data(), bsp_header.lump[i].nLength, crc32);
			}

			PathCrc32InMemory(lumps[LUMP_MODELS].data(), bsp_header.lump[LUMP_MODELS].nLength, originsize, crc32, originCrc32);

			crc32 = UINT32_C(0xFFFFFFFF);
			for (int i = 0; i < HEADER_LUMPS; i++)
			{
				if (i != LUMP_ENTITIES)
					crc32 = GetCrc32InMemory(lumps[i].data(), bsp_header.lump[i].nLength, crc32);
			}

			print_log(get_localized_string(LANG_0079), reverse_bits(crc32));
		}
	}

	print_log(get_localized_string(LANG_0080), bsp_path);
	// calculate lump offsets
	int offset = sizeof(BSPHEADER) + (is_bsp_pathos ? 4 : 0);

	if (is_bsp30ext && extralumps.size())
	{
		offset += sizeof(BSPHEADER_EX);

		int extralumpscount = bsp_header_ex.nVersion <= 3 ? EXTRA_LUMPS_OLD : EXTRA_LUMPS;
		for (int i = 0; i < extralumpscount; i++)
		{
			bsp_header_ex.lump[i].nOffset = offset;
			offset += bsp_header_ex.lump[i].nLength;
			file.write((char*)extralumps[i].data(), extralumps[i].size());

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
		file.write((char*)lumps[i].data(), lumps[i].size());

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

	if (is_bsp_pathos)
	{
		const char* pathosname = "PBSP";
		file.write(pathosname, 4);
	}

	file.write((char*)&bsp_header, sizeof(BSPHEADER));

	if (is_bsp30ext && extralumps.size())
	{
		file.write((char*)&bsp_header_ex, sizeof(BSPHEADER_EX));
	}

	delete[] freeClipnodes16;
	delete[] freenodes16;
	delete[] freenodes32a;
	delete[] freeedges16;
	delete[] freemarksurfs16;
	delete[] freefaces16;
	delete[] freeleaves16;
	delete[] freeleaves32a;
	delete[] freelighting;

	replace_lumps(backupLumps);
}

bool Bsp::load_lumps(const std::string& fpath)
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

	if (bsp_header.nVersion == 'PSBP')
	{
		is_bsp_pathos = true;
		is_bsp2 = true;
		is_bsp2_old = true;
		fin.read((char*)&bsp_header.nVersion, sizeof(int));
		print_log("PATHOS bsp v{}\n", bsp_header.nVersion);
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

	BSPLUMP extra_clipnodes_lumps[2];
	BSPCLIPNODE16* extra_clipnodes_data[2] = { NULL,NULL };
	int extra_clipnodes_count[2] = { 0, 0 };

	if (bsp_header.nVersion == 31)
	{
		print_log("Detected BSP v31 [UNSUPPORTED]. Need convert to 32 bit clipnodes...\n");
		is_bsp31 = true;

		for (int i = HEADER_LUMPS; i <= HEADER_LUMPS + 1; i++)
		{
			fin.read((char*)&extra_clipnodes_lumps[i - HEADER_LUMPS], sizeof(BSPLUMP));

			if (g_settings.verboseLogs)
				print_log("[BAD v31 BSP] " + get_localized_string(LANG_0086), i, extra_clipnodes_lumps[i - HEADER_LUMPS].nLength, extra_clipnodes_lumps[i - HEADER_LUMPS].nOffset);
		}

		for (int i = 0; i < 2; i++)
		{
			if (extra_clipnodes_lumps[i].nLength == 0)
			{
				extralumps[i].clear();
				continue;
			}

			if (extra_clipnodes_lumps[i].nOffset >= size || extra_clipnodes_lumps[i].nOffset < 0 || extra_clipnodes_lumps[i].nLength < 0)
			{
				print_log(get_localized_string(LANG_0090), i);
				break;
			}

			fin.seekg(extra_clipnodes_lumps[i].nOffset);
			if (fin.eof() || extra_clipnodes_lumps[i].nOffset + extra_clipnodes_lumps[i].nLength > size)
			{
				print_log(get_localized_string(LANG_1020), i);
				break;
			}
			else
			{
				extra_clipnodes_count[i] = extra_clipnodes_lumps[i].nLength / sizeof(BSPCLIPNODE16);
				if (extra_clipnodes_count[i] > 0)
				{
					extra_clipnodes_data[i] = new BSPCLIPNODE16[extra_clipnodes_count[i]];
					fin.read((char*)extra_clipnodes_data[i], extra_clipnodes_count[i] * sizeof(BSPCLIPNODE16));
				}
			}
		}
	}

	fin.read((char*)&bsp_header_ex.id, sizeof(int));

	if (bsp_header_ex.id == 'HSAX' /* XASH */)
	{
		print_log(get_localized_string(LANG_0087));
		is_bsp30ext = true;

		fin.read((char*)&bsp_header_ex.nVersion, sizeof(int));


		int extralumpscount = bsp_header_ex.nVersion <= 3 ? EXTRA_LUMPS_OLD : EXTRA_LUMPS;
		print_log(get_localized_string(LANG_0088), bsp_header_ex.nVersion, extralumpscount);

		extralumps.resize(EXTRA_LUMPS);

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
				extralumps[i].clear();
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
				extralumps[i].resize(bsp_header_ex.lump[i].nLength);
				fin.read((char*)extralumps[i].data(), bsp_header_ex.lump[i].nLength);
			}
		}
	}

	lumps.resize(HEADER_LUMPS);

	//update_lump_pointers();

	unsigned int crc32 = UINT32_C(0xFFFFFFFF);
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (bsp_header.lump[i].nLength == 0)
		{
			lumps[i].clear();
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
			lumps[i].resize(bsp_header.lump[i].nLength);
			fin.read((char*)lumps[i].data(), bsp_header.lump[i].nLength);
		}
	}

	update_lump_pointers();

	std::vector<unsigned char> classnametmp = { 'c', 'l', 'a', 's', 's', 'n', 'a', 'm', 'e' };
	auto& lump_planes = lumps[LUMP_PLANES];
	size_t length = bsp_header.lump[LUMP_PLANES].nLength;

	if (length < sizeof(BSPPLANE) ||
		std::search(lump_planes.begin(), lump_planes.end(),
			classnametmp.begin(), classnametmp.end()) != lump_planes.end())
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
			lumps[i].clear();
			continue;
		}

		if (i != LUMP_ENTITIES)
		{
			crc32 = GetCrc32InMemory(lumps[i].data(), bsp_header.lump[i].nLength, crc32);
		}

		if (i == LUMP_NODES)
		{
			if (is_bsp2)
			{
				if (is_bsp2_old)
				{
					nodeCount = bsp_header.lump[i].nLength / sizeof(BSPNODE32A);

					BSPNODE32* tmpnodes = new BSPNODE32[nodeCount];

					BSPNODE32A* nodes16 = (BSPNODE32A*)lumps[i].data();

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

					bsp_header.lump[i].nLength = nodeCount * sizeof(BSPNODE32);
					lumps[i].assign((unsigned char*)tmpnodes, (unsigned char*)(tmpnodes)+bsp_header.lump[i].nLength);
					delete[] tmpnodes;
					print_log(get_localized_string(LANG_0093) + "[OLD]");
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

				BSPNODE16* nodes16 = (BSPNODE16*)lumps[i].data();

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

				bsp_header.lump[i].nLength = nodeCount * sizeof(BSPNODE32);
				lumps[i].assign((unsigned char*)tmpnodes, (unsigned char*)(tmpnodes)+bsp_header.lump[i].nLength);
				delete[] tmpnodes;
			}
		}


		if (i == LUMP_FACES)
		{
			if (is_bsp2)
			{
				faceCount = bsp_header.lump[i].nLength / sizeof(BSPFACE32);
				print_log(get_localized_string(LANG_0094));
			}
			else
			{
				faceCount = bsp_header.lump[i].nLength / sizeof(BSPFACE16);

				BSPFACE32* tmpfaces = new BSPFACE32[faceCount];
				BSPFACE16* faces16 = (BSPFACE16*)lumps[i].data();

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

				bsp_header.lump[i].nLength = faceCount * sizeof(BSPFACE32);
				lumps[i].assign((unsigned char*)tmpfaces, (unsigned char*)(tmpfaces)+bsp_header.lump[i].nLength);
				delete[] tmpfaces;
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

				BSPCLIPNODE16* clipnodes16 = (BSPCLIPNODE16*)lumps[i].data();

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
				else
				{
					if (!is_bsp31)
					{
						print_log(get_localized_string(LANG_0097));
					}
				}

				bsp_header.lump[i].nLength = clipnodeCount * sizeof(BSPCLIPNODE32);
				lumps[i].assign((unsigned char*)tmpclipnodes, (unsigned char*)(tmpclipnodes)+bsp_header.lump[i].nLength);
				delete[] tmpclipnodes;
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

					BSPLEAF32A* leaves16 = (BSPLEAF32A*)lumps[i].data();
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

					bsp_header.lump[i].nLength = leafCount * sizeof(BSPLEAF32);
					lumps[i].assign((unsigned char*)tmpleaves, (unsigned char*)(tmpleaves)+bsp_header.lump[i].nLength);
					delete[] tmpleaves;
				}
			}
			else
			{
				leafCount = bsp_header.lump[i].nLength / sizeof(BSPLEAF16);

				BSPLEAF32* tmpleaves = new BSPLEAF32[leafCount];

				BSPLEAF16* leaves16 = (BSPLEAF16*)lumps[i].data();
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

				bsp_header.lump[i].nLength = leafCount * sizeof(BSPLEAF32);
				lumps[i].assign((unsigned char*)tmpleaves, (unsigned char*)(tmpleaves)+bsp_header.lump[i].nLength);
				delete[] tmpleaves;
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

				unsigned short* surfs16 = (unsigned short*)lumps[i].data();

				for (int n = 0; n < marksurfCount; n++)
				{
					tmpSurf[n] = surfs16[n];
				}

				bsp_header.lump[i].nLength = marksurfCount * sizeof(int);
				lumps[i].assign((unsigned char*)tmpSurf, (unsigned char*)(tmpSurf)+bsp_header.lump[i].nLength);
				delete[] tmpSurf;
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

				BSPEDGE16* edges16 = (BSPEDGE16*)lumps[i].data();
				for (int n = 0; n < edgeCount; n++)
				{
					tmpedges[n].iVertex[0] = edges16[n].iVertex[0];
					tmpedges[n].iVertex[1] = edges16[n].iVertex[1];
				}

				bsp_header.lump[i].nLength = edgeCount * sizeof(BSPEDGE32);
				lumps[i].assign((unsigned char*)tmpedges, (unsigned char*)(tmpedges)+bsp_header.lump[i].nLength);
				delete[] tmpedges;
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
			lightmap3_bytes += GetFaceLightmapSizeBytes(i);
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

	int textures_bytes = sizeof(int) + textureCount * sizeof(int);
	int textures_no_pal_bytes = sizeof(int) + textureCount * sizeof(int);

	for (int t = 0; t < textureCount; t++)
	{
		int iStartOffset = ((int*)textures)[t + 1];

		if (iStartOffset < 0 || iStartOffset + (int)sizeof(BSPMIPTEX) > textureDataLength)
		{
			if (g_settings.verboseLogs)
			{
				print_log("Skip calculate bad texture offset {}...\n", iStartOffset);
			}
			continue;
		}

		BSPMIPTEX* tex = ((BSPMIPTEX*)(textures + iStartOffset));

		int data_offset = tex->nOffsets[0];

		if (iStartOffset + data_offset > textureDataLength)
		{
			if (g_settings.verboseLogs)
			{
				print_log("Skip calculate bad texture data offset...\n");
			}
			continue;
		}

		textures_bytes += sizeof(BSPMIPTEX);
		textures_no_pal_bytes += sizeof(BSPMIPTEX);
		if (data_offset > 0)
		{
			textures_bytes += sizeof(short);
			textures_bytes += sizeof(COLOR3) * 256;

			for (int i = 0; i < MIPLEVELS; i++)
			{
				int div = 1 << i;
				int mipWidth = tex->nWidth / div;
				int mipHeight = tex->nHeight / div;
				textures_bytes += mipWidth * mipHeight;
				textures_no_pal_bytes += mipWidth * mipHeight;
			}
		}
	}

	is_texture_has_pal = textureCount == 0 || textures_no_pal_bytes == textures_bytes || abs(textures_no_pal_bytes - textureDataLength) > abs(textures_bytes - textureDataLength);
	target_save_texture_has_pal = is_texture_has_pal;

	if (g_settings.verboseLogs)
	{
		//print_log("Embedded Textures: {}\n", !is_texture_has_pal ? "quake pal" : "has pal");
		print_log("Embedded Textures: {} [pal:{}, nopal:{}, map has:{}]\n", !is_texture_has_pal ? "quake pal" : "has pal", textures_bytes, textures_no_pal_bytes, textureDataLength);
	}

	if (!is_colored_lightmap)
	{
		int lightPixels = bsp_header.lump[LUMP_LIGHTING].nLength;

		COLOR3* newLight = new COLOR3[lightPixels];

		for (int m = 0; m < lightPixels; m++)
		{
			newLight[m] = COLOR3(lumps[LUMP_LIGHTING].data()[m], lumps[LUMP_LIGHTING].data()[m], lumps[LUMP_LIGHTING].data()[m]);
		}

		bsp_header.lump[LUMP_LIGHTING].nLength = lightPixels * sizeof(COLOR3);
		lumps[LUMP_LIGHTING].assign((unsigned char*)newLight, (unsigned char*)(newLight)+bsp_header.lump[LUMP_LIGHTING].nLength);
		delete[] newLight;


		for (int n = 0; n < faceCount; n++)
		{
			faces[n].nLightmapOffset = faces[n].nLightmapOffset * sizeof(COLOR3);
		}
	}
	else if (is_fuck_rgba_lightmap)
	{
		int lightPixels = bsp_header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR4);

		COLOR3* newLight = new COLOR3[lightPixels];
		COLOR4* oldLight = (COLOR4*)lumps[LUMP_LIGHTING].data();

		for (int m = 0; m < lightPixels; m++)
		{
			newLight[m] = oldLight[m].rgb(COLOR3(255, 255, 255));
		}

		bsp_header.lump[LUMP_LIGHTING].nLength = lightPixels * sizeof(COLOR3);
		lumps[LUMP_LIGHTING].assign((unsigned char*)newLight, (unsigned char*)(newLight)+bsp_header.lump[LUMP_LIGHTING].nLength);
		delete[] newLight;

		for (int n = 0; n < faceCount; n++)
		{
			faces[n].nLightmapOffset = (faces[n].nLightmapOffset / sizeof(COLOR4)) * sizeof(COLOR3);
		}
	}

	originCrc32 = crc32;

	fin.close();


	update_lump_pointers();

	if (is_bsp31)
	{
		is_32bit_clipnodes = true;
		if (extra_clipnodes_count[0] || extra_clipnodes_count[1])
		{
			print_log("Convert clipnodes from 31 BSP version to BSP30ex\n");
			int newCount = clipnodeCount + extra_clipnodes_count[0] + extra_clipnodes_count[1];

			print_log("Old clipnode count {} new clipnode count {}\n", clipnodeCount, newCount);

			BSPCLIPNODE32* newClipnodes = new BSPCLIPNODE32[newCount];
			memcpy(newClipnodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE32));

			print_log("First clipnode of hull 2 : {}\n", clipnodeCount);

			int offset = clipnodeCount;

			for (int i = 0; i < extra_clipnodes_count[0]; i++)
			{
				newClipnodes[i + offset].iChildren[0] = extra_clipnodes_data[0][i].iChildren[0];

				if (newClipnodes[i + offset].iChildren[0] >= 0)
					newClipnodes[i + offset].iChildren[0] += offset;

				newClipnodes[i + offset].iChildren[1] = extra_clipnodes_data[0][i].iChildren[1];

				if (newClipnodes[i + offset].iChildren[1] >= 0)
					newClipnodes[i + offset].iChildren[1] += offset;

				newClipnodes[i + offset].iPlane = extra_clipnodes_data[0][i].iPlane;
			}

			print_log("First clipnode of hull 2 : {}\n", offset + extra_clipnodes_count[0]);

			offset += extra_clipnodes_count[0];

			for (int i = 0; i < extra_clipnodes_count[1]; i++)
			{
				newClipnodes[i + offset].iChildren[0] = extra_clipnodes_data[1][i].iChildren[0];

				if (newClipnodes[i + offset].iChildren[0] >= 0)
					newClipnodes[i + offset].iChildren[0] += offset;

				newClipnodes[i + offset].iChildren[1] = extra_clipnodes_data[1][i].iChildren[1];

				if (newClipnodes[i + offset].iChildren[1] >= 0)
					newClipnodes[i + offset].iChildren[1] += offset;

				newClipnodes[i + offset].iPlane = extra_clipnodes_data[1][i].iPlane;
			}

			bsp_header.lump[LUMP_CLIPNODES].nLength = newCount * sizeof(BSPCLIPNODE32);
			lumps[LUMP_CLIPNODES].assign((unsigned char*)newClipnodes, (unsigned char*)(newClipnodes)+bsp_header.lump[LUMP_CLIPNODES].nLength);
			delete[] newClipnodes;

			for (int i = 0; i < modelCount; i++)
			{
				offset = clipnodeCount;

				if (models[i].iHeadnodes[2] >= 0)
				{
					models[i].iHeadnodes[2] += offset;
				}

				offset += extra_clipnodes_count[0];

				if (models[i].iHeadnodes[3] >= 0)
				{
					models[i].iHeadnodes[3] += offset;
				}
			}

			clipnodeCount = newCount;
		}
	}

	update_lump_pointers();
	return valid;
}

void Bsp::reload_ents()
{
	for (size_t i = 0; i < ents.size(); i++)
		delete ents[i];
	ents.clear();

	ents = load_ents(std::string((char*)lumps[LUMP_ENTITIES].data(), (char*)lumps[LUMP_ENTITIES].data() + bsp_header.lump[LUMP_ENTITIES].nLength),bsp_name);
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
	if (leafCount > (is_bsp2 ? INT_MAX : (int)g_limits.maxMapLeaves))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0181));
	}
	if (modelCount > (int)g_limits.maxMapModels)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0182));
	}
	if (texinfoCount > (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1037));
	}
	if (nodeCount > (is_bsp2 ? INT_MAX : (int)g_limits.maxMapNodes))
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
	if (clipnodeCount > (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : g_limits.maxMapClipnodes))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0186));
	}
	if (marksurfCount > (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0187));
	}
	if (surfedgeCount > (is_bsp2 ? INT_MAX : (int)g_limits.maxMapSurfedges))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0188));
	}
	if (edgeCount > (is_bsp2 ? INT_MAX : (int)g_limits.maxMapEdges))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0189));
	}
	if (textureCount > (int)g_limits.maxMapTextures)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0190));
	}
	if (lightDataLength > (int)g_limits.maxMapLightdata)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0191));
	}
	if (visDataLength > (int)g_limits.maxMapVisdata)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0192));
	}
	if (ents.size() > g_limits.maxMapEnts)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, "Overflowed entities !!!\n");
	}

	return modelCount <= (int)g_limits.maxMapModels
		&& planeCount <= (is_bsp2 ? INT_MAX : MAX_MAP_PLANES)
		&& vertCount <= MAX_MAP_VERTS
		&& nodeCount <= (is_bsp2 ? INT_MAX : (int)g_limits.maxMapNodes)
		&& texinfoCount <= (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS)
		&& faceCount <= (is_bsp2 ? INT_MAX : MAX_MAP_FACES)
		&& clipnodeCount <= (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : g_limits.maxMapClipnodes)
		&& leafCount <= (is_bsp2 ? INT_MAX : (int)g_limits.maxMapLeaves)
		&& marksurfCount <= (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS)
		&& surfedgeCount <= (is_bsp2 ? INT_MAX : (int)g_limits.maxMapSurfedges)
		&& edgeCount <= (is_bsp2 ? INT_MAX : (int)g_limits.maxMapSurfedges)
		&& textureCount <= (int)g_limits.maxMapTextures
		&& lightDataLength <= (int)g_limits.maxMapLightdata
		&& visDataLength <= (int)g_limits.maxMapVisdata
		&& ents.size() <= g_limits.maxMapEnts;
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
	for (int i = faceCount - 1; i >= 0; i--)
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

		if (isValid && !GetFaceExtents(i, bmins, bmaxs))
		{
			print_log(PRINT_RED | PRINT_INTENSITY, "Bad face {} extents\n", i);
			print_log(PRINT_GREEN | PRINT_INTENSITY, "Removing invalid (invisible) face...\n", i);
			remove_face(i);
		}

		if (isValid)
		{
			if (g_settings.verboseLogs && is_face_duplicate_edges(i))
			{
				print_log("Warn: Face {} has duplicate verts!\n", i);
			}
		}
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

		bool swapped = false;
		for (int n = 0; n < 3; n++)
		{
			if (leaves[i].nMins[n] > leaves[i].nMaxs[n])
			{
				swapped = true;
				isValid = false;
			}
		}
		if (swapped)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, "backwards mins / maxs in leaf {} Mins: ({}, {}, {}) Maxs: ({} {} {})\n", i, leaves[i].nMins[0], leaves[i].nMins[1], leaves[i].nMins[2],
				leaves[i].nMaxs[0], leaves[i].nMaxs[1], leaves[i].nMaxs[2]);
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
			if (nodes[i].iChildren[k] >= 0 && nodes[i].iChildren[k] >= nodeCount)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0122), i, k, nodes[i].iChildren[k], nodeCount);
				isValid = false;
			}
			else if (nodes[i].iChildren[k] < 0 && ~nodes[i].iChildren[k] >= leafCount)
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
			if (clipnodes[i].iChildren[k] >= 0 && clipnodes[i].iChildren[k] >= clipnodeCount)
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
			print_log(PRINT_RED | PRINT_INTENSITY, "Backwards mins/maxs in model {}. Mins: ({}, {}, {}) Maxs: ({} {} {})\n", i,
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
			/*if (tex->szName[0] == '\0')
			{
				isValid = false;
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0133), i);
			}
			else*/ if (strlen(tex->szName) >= MAXTEXTURENAME)
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
			else if (tex->nOffsets[0] > 0 && texlen == 0)
			{
				isValid = false;
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string("LANG_ERROR_TEXLEN"), i);
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0136), texlen, tex->szName[0] != '\0' ? tex->szName : "UNKNOWN_NAME", texOffset, dataOffset);
			}
			else if (tex->nOffsets[0] > 0)
			{
				int texdata = (int)(((unsigned char*)tex) - textures) + tex->nOffsets[0] + texlen - sizeof(BSPMIPTEX);
				if (texdata > bsp_header.lump[LUMP_TEXTURES].nLength)
				{
					isValid = false;
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0135), i, texdata, bsp_header.lump[LUMP_TEXTURES].nLength);
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0136), texlen, tex->szName[0] != '\0' ? tex->szName : "UNKNOWN_NAME", texOffset, dataOffset);
				}
			}
			else if ((unsigned int)tex->nWidth * tex->nHeight > g_limits.maxTextureSize) {
				print_log("Texture '{}' too large ({}x{})\n", tex->szName, tex->nWidth, tex->nHeight);
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
	if (leafCount > (is_bsp2 ? INT_MAX : (int)g_limits.maxMapLeaves))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0181));
	}
	if (modelCount > (int)g_limits.maxMapModels)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0182));
	}
	if (texinfoCount > (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1037));
	}
	if (nodeCount > (is_bsp2 ? INT_MAX : (int)g_limits.maxMapNodes))
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
	if (clipnodeCount > (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : g_limits.maxMapClipnodes))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0186));
	}
	if (marksurfCount > (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0187));
	}
	if (surfedgeCount > (is_bsp2 ? INT_MAX : (int)g_limits.maxMapSurfedges))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0188));
	}
	if (edgeCount > (is_bsp2 ? INT_MAX : (int)g_limits.maxMapEdges))
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0189));
	}
	if (textureCount > (int)g_limits.maxMapTextures)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0190));
	}
	if (lightDataLength > (int)g_limits.maxMapLightdata)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0191));
	}
	if (visDataLength > (int)g_limits.maxMapVisdata)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0192));
	}
	if (ents.size() > g_limits.maxMapEnts)
	{
		isValid = false;
		print_log(PRINT_RED | PRINT_INTENSITY, "Overflowed entities !!!\n");
	}

	if (leaves)
	{
		unsigned int newVisRowSize = ((leafCount + 63) & ~63) >> 3;
		int decompressedVisSize = leafCount * newVisRowSize;
		unsigned char* decompressedVis = new unsigned char[decompressedVisSize];
		memset(decompressedVis, 0xFF, decompressedVisSize);
		decompress_vis_lump(this, leaves, visdata, decompressedVis,
			models[0].nVisLeafs, leafCount - 1, leafCount - 1, decompressedVisSize, bsp_header.lump[LUMP_VISIBILITY].nLength);
		delete[] decompressedVis;
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

		if (planeCount >= (is_bsp2 ? INT_MAX : MAX_MAP_PLANES) || texinfoCount >= (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS) || leafCount >= (is_bsp2 ? INT_MAX : (int)g_limits.maxMapLeaves) ||
			modelCount >= (int)g_limits.maxMapModels || nodeCount >= (is_bsp2 ? INT_MAX : (int)g_limits.maxMapNodes) || vertCount >= MAX_MAP_VERTS ||
			faceCount >= (is_bsp2 ? INT_MAX : MAX_MAP_FACES) || clipnodeCount >= (int)(is_32bit_clipnodes ? INT_MAX : is_broken_clipnodes ? (MAX_MAP_CLIPNODES_DEFAULT * 2 - 15) : g_limits.maxMapClipnodes) || marksurfCount >= (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS) ||
			surfedgeCount >= (is_bsp2 ? INT_MAX : (int)g_limits.maxMapSurfedges) || (is_bsp2 ? INT_MAX : edgeCount >= (int)g_limits.maxMapEdges) || textureCount >= (int)g_limits.maxMapTextures ||
			lightDataLength >= (int)g_limits.maxMapLightdata || visDataLength >= (int)g_limits.maxMapVisdata)
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
		print_stat("models", modelCount, g_limits.maxMapModels, false);
		print_stat("planes", planeCount, (is_bsp2 ? INT_MAX : MAX_MAP_PLANES), false);
		print_stat("vertexes", vertCount, MAX_MAP_VERTS, false);
		print_stat("nodes", nodeCount, is_bsp2 ? INT_MAX : (int)g_limits.maxMapNodes, false);
		print_stat("texinfos", texinfoCount, (is_bsp2 ? INT_MAX : MAX_MAP_TEXINFOS), false);
		print_stat("faces", faceCount, (is_bsp2 ? INT_MAX : MAX_MAP_FACES), false);
		print_stat("clipnodes", clipnodeCount, (is_32bit_clipnodes ? INT_MAX : g_limits.maxMapClipnodes), false);
		print_stat("leaves", leafCount, (is_bsp2 ? INT_MAX : g_limits.maxMapLeaves), false);
		print_stat("marksurfaces", marksurfCount, (is_bsp2 ? INT_MAX : MAX_MAP_MARKSURFS), false);
		print_stat("surfedges", surfedgeCount, (is_bsp2 ? INT_MAX : g_limits.maxMapSurfedges), false);
		print_stat("edges", edgeCount, (is_bsp2 ? INT_MAX : g_limits.maxMapEdges), false);
		print_stat("textures", textureCount, g_limits.maxMapTextures, false);
		print_stat("lightdata", lightDataLength, g_limits.maxMapLightdata, true);
		print_stat("visdata", visDataLength, g_limits.maxMapVisdata, true);
		print_stat("entities", entCount, g_limits.maxMapEnts, false);
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
		print_log(" \n");
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

std::vector<int> Bsp::get_model_ents_ids(int modelIdx)
{
	std::vector<int> uses;
	for (int i = 0; i < (int)ents.size(); i++)
	{
		if (ents[i]->getBspModelIdx() == modelIdx)
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
		//BSPLEAF32& leaf = leaves[~nodeIdx];
		//print_log(get_localized_string(LANG_0143), ~nodeIdx);
		print_leaf(~nodeIdx);
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


int Bsp::pointLeaf(int iNode, const vec3& p, int hull, int& leafIdx, int& planeIdx)
{
	leafIdx = -1;
	planeIdx = -1;
	if (iNode < 0)
	{
		return CONTENTS_EMPTY;
	}

	if (hull == 0)
	{
		while (iNode >= 0)
		{
			BSPNODE32& node = nodes[iNode];
			planeIdx = node.iPlane;
			BSPPLANE& plane = planes[planeIdx];

			float d = dotProduct(plane.vNormal, p) - plane.fDist;
			if (d < 0)
			{
				iNode = node.iChildren[1];
			}
			else
			{
				iNode = node.iChildren[0];
			}
		}

		leafIdx = ~iNode;
		return leaves[leafIdx].nContents;
	}
	else
	{
		while (iNode >= 0)
		{
			BSPCLIPNODE32& node = clipnodes[iNode];
			BSPPLANE& plane = planes[node.iPlane];

			float d = dotProduct(plane.vNormal, p) - plane.fDist;
			if (d < 0)
			{
				iNode = node.iChildren[1];
			}
			else
			{
				iNode = node.iChildren[0];
			}
		}

		return iNode;
	}
}

std::vector<int> Bsp::getLeafsFromPos(const vec3& p, float radius) {
	std::vector<int> leafsInRadius;

	for (int i = 1; i < leafCount; i++) {
		vec3& mins = leaves[i].nMins;
		vec3& maxs = leaves[i].nMaxs;

		bool intersects =
			(p.x + radius >= mins.x && p.x - radius <= maxs.x) &&
			(p.y + radius >= mins.y && p.y - radius <= maxs.y) &&
			(p.z + radius >= mins.z && p.z - radius <= maxs.z);

		if (intersects) {
			leafsInRadius.push_back(i);
		}
	}

	return leafsInRadius;
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
		return leaves[leafIdx].nContents;
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

int Bsp::modelLeafs(const BSPMODEL& model, std::vector<int>& outLeafs)
{
	int nodeIdx = model.iHeadnodes[0];
	recurse_node_leafs(nodeIdx, outLeafs);
	std::sort(outLeafs.begin(), outLeafs.end());
	outLeafs.erase(std::unique(outLeafs.begin(), outLeafs.end()), outLeafs.end());
	return (int)(outLeafs.size());
}

int Bsp::modelLeafs(int modelIdx, std::vector<int>& outLeafs)
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

bool Bsp::recursiveHullCheck(int hull, int num, float p1f, float p2f, vec3 p1, vec3 p2, TraceResult* trace)
{
	if (num < 0) {
		if (num != CONTENTS_SOLID) {
			trace->fAllSolid = false;

			if (num == CONTENTS_EMPTY)
				trace->fInOpen = true;

			else if (num != CONTENTS_TRANSLUCENT)
				trace->fInWater = true;
		}
		else {
			trace->fStartSolid = true;
		}

		// empty
		return true;
	}

	if (num >= clipnodeCount) {
		print_log("{}: bad node number\n", __func__);
		return false;
	}

	// find the point distances
	BSPCLIPNODE32* node = &clipnodes[num];
	BSPPLANE* plane = &planes[node->iPlane];

	float t1 = dotProduct(plane->vNormal, p1) - plane->fDist;
	float t2 = dotProduct(plane->vNormal, p2) - plane->fDist;

	// keep descending until we find a plane that bisects the trace line
	if (t1 >= 0.0f && t2 >= 0.0f)
		return recursiveHullCheck(hull, node->iChildren[0], p1f, p2f, p1, p2, trace);
	if (t1 < 0.0f && t2 < 0.0f)
		return recursiveHullCheck(hull, node->iChildren[1], p1f, p2f, p1, p2, trace);

	int side = (t1 < 0.0f) ? 1 : 0;

	// put the crosspoint DIST_EPSILON pixels on the near side
	float frac;
	if (side) {
		frac = (t1 + EPSILON) / (t1 - t2);
	}
	else {
		frac = (t1 - EPSILON) / (t1 - t2);
	}
	frac = clamp(frac, 0.0f, 1.0f);

	if (frac != frac) {
		return false; // NaN
	}

	float pdif = p2f - p1f;
	float midf = p1f + pdif * frac;

	vec3 point = p2 - p1;
	vec3 mid = p1 + (point * frac);

	// check if trace is empty up until this plane that was just intersected
	if (!recursiveHullCheck(hull, node->iChildren[side], p1f, midf, p1, mid, trace)) {
		// hit an earlier plane that caused the trace to be fully solid here
		return false;
	}

	// check if trace can go through this plane without entering a solid area
	if (pointContents(node->iChildren[side ^ 1], mid, hull) != CONTENTS_SOLID) {
		// continue the trace from this plane
		// won't collide with it again because trace starts from a child of the intersected node
		return recursiveHullCheck(hull, node->iChildren[side ^ 1], midf, p2f, mid, p2, trace);
	}

	if (trace->fAllSolid) {
		return false; // never got out of the solid area
	}

	// the other side of the node is solid, this is the impact point
	trace->vecPlaneNormal = plane->vNormal;
	trace->flPlaneDist = side ? -plane->fDist : plane->fDist;

	// backup the trace if the collision point is considered solid due to poor float precision
	// shouldn't really happen, but does occasionally
	int headnode = models[0].iHeadnodes[hull];
	while (pointContents(headnode, mid, hull) == CONTENTS_SOLID) {
		frac -= 0.1f;
		if (frac < 0.0f)
		{
			trace->flFraction = midf;
			trace->vecEndPos = mid;
			//print_log("backup past 0\n");
			return false;
		}

		midf = p1f + pdif * frac;

		point = p2 - p1;
		mid = p1 + (point * frac);
	}

	trace->flFraction = midf;
	trace->vecEndPos = mid;

	return false;
}

void Bsp::traceHull(vec3 start, vec3 end, int hull, TraceResult* trace)
{
	if (hull < 0 || hull > 3)
		hull = 0;

	int headnode = models[0].iHeadnodes[hull];

	// fill in a default trace
	memset(trace, 0, sizeof(TraceResult));
	trace->vecEndPos = end;
	trace->flFraction = 1.0f;
	trace->fAllSolid = true;

	// trace a line through the appropriate clipping hull
	recursiveHullCheck(hull, headnode, 0.0f, 1.0f, start, end, trace);
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

int Bsp::get_leaf(vec3 pos, int hull) {
	int iNode = models->iHeadnodes[hull];

	if (hull == 0) {
		while (iNode >= 0)
		{
			BSPNODE32& node = nodes[iNode];
			BSPPLANE& plane = planes[node.iPlane];

			float d = dotProduct(plane.vNormal, pos) - plane.fDist;
			if (d < 0) {
				iNode = node.iChildren[1];
			}
			else {
				iNode = node.iChildren[0];
			}
		}

		return ~iNode;
	}

	int lastNode = -1;
	int lastSide = 0;

	while (iNode >= 0)
	{
		BSPCLIPNODE32& node = clipnodes[iNode];
		BSPPLANE& plane = planes[node.iPlane];

		float d = dotProduct(plane.vNormal, pos) - plane.fDist;
		if (d < 0) {
			lastNode = iNode;
			iNode = node.iChildren[1];
			lastSide = 1;
		}
		else {
			lastNode = iNode;
			iNode = node.iChildren[0];
			lastSide = 0;
		}
	}

	// clipnodes don't have leaf structs, so generate an id based on the last clipnode index and
	// the side of the plane that would be recursed to reach the leaf contents, if there were a leaf
	return lastNode * 2 + lastSide;
}

bool Bsp::is_leaf_visible(int ileaf, vec3 pos) {
	int ipvsLeaf = get_leaf(pos, 0);
	BSPLEAF32& pvsLeaf = leaves[ipvsLeaf];

	int p = pvsLeaf.nVisOffset; // pvs offset
	unsigned char* pvs = lumps[LUMP_VISIBILITY].data();

	bool isVisible = false;
	int numVisible = 0;

	if (!pvs) {
		return true;
	}

	//print_log("leaf {} can see:", ipvsLeaf);

	for (int lf = 1; lf < leafCount; p++)
	{
		if (pvs[p] == 0) // prepare to skip leafs
			lf += 8 * pvs[++p]; // next unsigned char holds number of leafs to skip
		else
		{
			for (unsigned char bit = 1; bit != 0; bit *= 2, lf++)
			{
				if ((pvs[p] & bit) && lf < leafCount) // leaf is flagged as visible
				{
					numVisible++;
					//print_log(" {}", lf);
					if (lf == ileaf) {
						isVisible = true;
					}
				}
			}
		}
	}

	//print_log("\n");

	return isVisible;
}

bool Bsp::is_face_visible(int faceIdx, vec3 pos, vec3 angles) {
	BSPFACE32& face = faces[faceIdx];
	BSPPLANE& plane = planes[face.iPlane];
	vec3 normal = plane.vNormal;

	// TODO: is it in the frustrum? Is it part of an entity model? If so is the entity linked in the PVS?
	// is it facing the camera? Is it a special face?

	return true;
}

int Bsp::count_visible_polys(vec3 pos, vec3 angles) {
	int ipvsLeaf = get_leaf(pos, 0);
	BSPLEAF32& pvsLeaf = leaves[ipvsLeaf];

	int p = pvsLeaf.nVisOffset; // pvs offset
	unsigned char* pvs = lumps[LUMP_VISIBILITY].data();

	int numVisible = 0;

	if (ipvsLeaf == 0) {
		return faceCount;
	}

	memset(pvsFaces, 0, pvsFaceCount * sizeof(bool));
	int renderFaceCount = 0;

	for (int lf = 1; lf < leafCount; p++)
	{
		if (pvs[p] == 0) // prepare to skip leafs
			lf += 8 * pvs[++p]; // next unsigned char holds number of leafs to skip
		else
		{
			for (unsigned char bit = 1; bit != 0; bit *= 2, lf++)
			{
				if ((pvs[p] & bit) && lf < leafCount) // leaf is flagged as visible
				{
					numVisible++;
					BSPLEAF32& leaf = leaves[lf];

					for (int i = 0; i < leaf.nMarkSurfaces; i++) {
						int faceIdx = marksurfs[leaf.iFirstMarkSurface + i];
						if (!pvsFaces[faceIdx]) {
							pvsFaces[faceIdx] = true;
							if (is_face_visible(faceIdx, pos, angles))
								renderFaceCount++;
						}
					}
				}
			}
		}
	}

	return renderFaceCount;
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
		int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];

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

void Bsp::mark_model_structures(int modelIdx, STRUCTUSAGE* usage, bool skipLeaves)
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
	BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS].data())[modelIdx];

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

	unsigned char* oldModels = (unsigned char*)models;

	int newSize = (modelCount - 1) * sizeof(BSPMODEL);
	unsigned char* newModels = new unsigned char[newSize];

	memcpy(newModels, oldModels, modelIdx * sizeof(BSPMODEL));
	memcpy(newModels + modelIdx * sizeof(BSPMODEL),
		oldModels + (modelIdx + 1) * sizeof(BSPMODEL),
		(modelCount - (modelIdx + 1)) * sizeof(BSPMODEL));

	replace_lump(LUMP_MODELS, newModels, newSize);
	delete[] newModels;
}

int Bsp::create_solid(const vec3& mins, const vec3& maxs, int textureIdx, bool empty)
{
	int newModelIdx = create_model();
	BSPMODEL& newModel = models[newModelIdx];


	create_primitive_box(mins, maxs, &newModel, textureIdx);
	//regenerate_clipnodes(newModelIdx, -1);
	if (!empty)
	{
		create_clipnode_box(mins, maxs, &newModel, 0, false, empty);
	}
	else
	{
		newModel.iHeadnodes[1] = newModel.iHeadnodes[2] = newModel.iHeadnodes[3] = CONTENTS_EMPTY;
	}
	//remove_unused_model_structures(CLEAN_VISDATA | CLEAN_LEAVES); 

	return newModelIdx;
}

int Bsp::create_solid(Solid& solid, int targetModelIdx)
{
	int modelIdx = targetModelIdx >= 0 ? targetModelIdx : create_model();
	BSPMODEL& newModel = models[modelIdx];

	create_solid_nodes(solid, &newModel);
	regenerate_clipnodes(modelIdx, -1);

	return modelIdx;
}

void Bsp::add_model(Bsp* sourceMap, int modelIdx)
{
	STRUCTUSAGE usage(sourceMap);
	sourceMap->mark_model_structures(modelIdx, &usage, false);

	// TODO: add the model lel
	// done in Import->BSP Model

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
			if (oldTex->szName[0] != '\0' && oldTex->nOffsets[0] > 0 && strcasecmp(name, oldTex->szName) == 0)
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

int Bsp::add_texture(const char* oldname, unsigned char* data, int width, int height, bool force_custompal)
{
	if (!oldname || oldname[0] == '\0' || strlen(oldname) >= MAXTEXTURENAME)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0156));
		return -1;
	}
	char name[MAXTEXTURENAME];
	memset(name, 0, MAXTEXTURENAME);
	memcpy(name, oldname, std::min(MAXTEXTURENAME, (int)strlen(oldname)));

	print_log(get_localized_string(LANG_0157), data == NULL ? "wad" : "embedded", name, width, height);

	if (width % 16 != 0 || height % 16 != 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0158));
		return -1;
	}

	if (width > (int)g_limits.maxTextureDimension || height > (int)g_limits.maxTextureDimension)
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
			if (data == NULL)
			{
				print_log(get_localized_string(LANG_0161));
				oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
					oldtex->nOffsets[3] = 0;
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
		else if (data != NULL)
		{
			only_copy_data = true;
			print_log(get_localized_string(LANG_0162));
		}
		else
		{
			print_log(get_localized_string(LANG_0163));
			oldtex->nOffsets[0] = oldtex->nOffsets[1] = oldtex->nOffsets[2] =
				oldtex->nOffsets[3] = 0;

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
				if (data == NULL)
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
			else if (data == NULL)
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

	if (is_texture_has_pal && !force_custompal)
	{
		texDataSize += width * height + sizeof(short) /* palette count */ + sizeof(COLOR3) * 256;
	}
	else
	{
		texDataSize += width * height;
	}


	unsigned char* mip[MIPLEVELS] = { NULL };

	if (data != NULL)
	{
		COLOR3* src = (COLOR3*)data;

		// If custom pal || quake || force quake
		if (!is_texture_has_pal || force_custompal)
		{
			int colors = 0;
			if (g_settings.pal_id >= 0)
			{
				colors = g_settings.palettes[g_settings.pal_id].colors;
				memcpy(palette, g_settings.palettes[g_settings.pal_id].data, g_settings.palettes[g_settings.pal_id].colors * sizeof(COLOR3));
			}
			else
			{
				colorCount = 256;
				memcpy(palette, g_settings.palette_default, 256 * sizeof(COLOR3));
			}
			Quantizer* tmpCQuantizer = new Quantizer(colors, 8);
			if (colors != 0)
				tmpCQuantizer->SetColorTable(palette, colors);
			tmpCQuantizer->ApplyColorTable((COLOR3*)data, width * height);
			delete tmpCQuantizer;
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
			int mipWidth = width >> i;
			int mipHeight = height >> i;
			texDataSize += mipWidth * mipHeight;
			mip[i] = new unsigned char[mipWidth * mipHeight];

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

	if (only_copy_data && oldtex && mip[0])
	{
		int newTexOffset = ((int*)textures)[oldtexid + 1];

		memcpy(textures + newTexOffset + oldtex->nOffsets[0], mip[0], width * height);
		memcpy(textures + newTexOffset + oldtex->nOffsets[1], mip[1], (width >> 1) * (height >> 1));
		memcpy(textures + newTexOffset + oldtex->nOffsets[2], mip[2], (width >> 2) * (height >> 2));
		memcpy(textures + newTexOffset + oldtex->nOffsets[3], mip[3], (width >> 3) * (height >> 3));

		unsigned char* palleteOffset = textures + (newTexOffset + oldtex->nOffsets[3] + (width >> 3) * (height >> 3));

		if (is_texture_has_pal && !force_custompal)
		{
			*(unsigned short*)palleteOffset = 256;
			memcpy(palleteOffset + 2, palette, sizeof(COLOR3) * 256);
		}

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

	int newTexLumpSize = bsp_header.lump[LUMP_TEXTURES].nLength + sizeof(int) + sizeof(BSPMIPTEX) + texDataSize;

	newTexLumpSize = (newTexLumpSize + 3) & ~3; /* 4 bytes lump padding for add new texture aligned to 4? */

	unsigned char* newTexData = new unsigned char[newTexLumpSize];

	memset(newTexData, 0, newTexLumpSize);

	// create new texture lump header
	int* newLumpHeader = (int*)newTexData;
	int* oldLumpHeader = (int*)lumps[LUMP_TEXTURES].data();
	*newLumpHeader = textureCount + 1;

	for (int i = 0; i < textureCount; i++)
	{
		if (*(oldLumpHeader + i + 1) >= 0)
			*(newLumpHeader + i + 1) = *(oldLumpHeader + i + 1) + sizeof(int); // make room for the new offset
		else
			*(newLumpHeader + i + 1) = *(oldLumpHeader + i + 1);
	}

	// copy old texture data
	int oldTexHeaderSize = (textureCount + 1) * sizeof(int);
	int newTexHeaderSize = oldTexHeaderSize + sizeof(int);
	int oldTexDatSize = bsp_header.lump[LUMP_TEXTURES].nLength - ((textureCount + 1) * sizeof(int));
	memcpy(newTexData + newTexHeaderSize, lumps[LUMP_TEXTURES].data() + oldTexHeaderSize, oldTexDatSize);

	// add new texture to the end of the lump
	int newTexOffset = newTexHeaderSize + oldTexDatSize;

	newLumpHeader[textureCount + 1] = (int)newTexOffset;

	BSPMIPTEX* newMipTex = (BSPMIPTEX*)(newTexData + newTexOffset);
	newMipTex->nWidth = width;
	newMipTex->nHeight = height;
	memcpy(newMipTex->szName, name, MAXTEXTURENAME);

	if (data != NULL)
	{
		newMipTex->nOffsets[0] = sizeof(BSPMIPTEX);
		newMipTex->nOffsets[1] = newMipTex->nOffsets[0] + width * height;
		newMipTex->nOffsets[2] = newMipTex->nOffsets[1] + (width >> 1) * (height >> 1);
		newMipTex->nOffsets[3] = newMipTex->nOffsets[2] + (width >> 2) * (height >> 2);
		unsigned char* palleteOffset = newTexData + (newTexOffset + newMipTex->nOffsets[3] + (width >> 3) * (height >> 3));

		memcpy(newTexData + newTexOffset + newMipTex->nOffsets[0], mip[0], width * height);
		memcpy(newTexData + newTexOffset + newMipTex->nOffsets[1], mip[1], (width >> 1) * (height >> 1));
		memcpy(newTexData + newTexOffset + newMipTex->nOffsets[2], mip[2], (width >> 2) * (height >> 2));
		memcpy(newTexData + newTexOffset + newMipTex->nOffsets[3], mip[3], (width >> 3) * (height >> 3));

		if (is_texture_has_pal && !force_custompal)
		{
			*(unsigned short*)palleteOffset = 256;
			memcpy(palleteOffset + 2, palette, sizeof(COLOR3) * 256);
		}

		for (int i = 0; i < MIPLEVELS; i++)
		{
			delete[] mip[i];
		}
	}

	replace_lump(LUMP_TEXTURES, newTexData, newTexLumpSize);
	delete[] newTexData;
	return textureCount - 1;
}

int Bsp::add_texture(WADTEX* tex, bool embedded)
{
	//print_log(get_localized_string(LANG_0168),tex->szName,tex->nWidth,tex->nHeight,tex->nOffsets[0],tex->nOffsets[1],tex->nOffsets[2],tex->nOffsets[3]);
	print_log(get_localized_string(LANG_0169), tex->szName, tex->nWidth, tex->nHeight);

	if (tex->nWidth % 16 != 0 || tex->nHeight % 16 != 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1030));
		return -1;
	}

	if (tex->nWidth > (int)g_limits.maxTextureDimension || tex->nHeight > (int)g_limits.maxTextureDimension)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1031));
		return -1;
	}

	if (embedded)
	{
		return add_texture(tex->szName, NULL, tex->nWidth, tex->needclean);
	}
	else
	{
		COLOR3* newTex = ConvertWadTexToRGB(tex);

		if (!is_texture_has_pal)
		{
			COLOR3 palette[256];
			unsigned int colorCount = 0;
			if (g_settings.pal_id >= 0)
			{
				colorCount = g_settings.palettes[g_settings.pal_id].colors;
				memcpy(palette, g_settings.palettes[g_settings.pal_id].data, g_settings.palettes[g_settings.pal_id].colors * sizeof(COLOR3));
			}
			else
			{
				colorCount = 256;
				memcpy(palette, g_settings.palette_default, 256 * sizeof(COLOR3));
			}

			Quantizer* tmpCQuantizer = new Quantizer(colorCount, 8);
			if (colorCount != 0)
				tmpCQuantizer->SetColorTable(palette, colorCount);
			tmpCQuantizer->ApplyColorTable((COLOR3*)newTex, tex->nWidth * tex->nHeight);
			delete tmpCQuantizer;
			int rettex = add_texture(tex->szName, (unsigned char*)newTex, tex->nWidth, tex->nHeight);
			delete[] newTex;
			return rettex;
		}

		int rettex = add_texture(tex->szName, (unsigned char*)newTex, tex->nWidth, tex->nHeight);
		delete[] newTex;
		return rettex;
	}
}

bool Bsp::export_wad_to_pngs(const std::string& wadpath, const std::string& targetdir)
{
	if (!fileExists(wadpath))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0351), wadpath);
		return false;
	}

	if (!createDir(targetdir))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0351), targetdir);
		return false;
	}


	Wad* wad = new Wad(wadpath);
	if (wad->readInfo())
	{
		std::vector<int> texturesIds(wad->dirEntries.size());
		std::iota(texturesIds.begin(), texturesIds.end(), 0);

		std::for_each(std::execution::par_unseq, texturesIds.begin(), texturesIds.end(), [&](int file)
			{
				WADTEX* texture = wad->readTexture(file);

				if (texture->szName[0] != '\0')
				{
					print_log(get_localized_string(LANG_0346), texture->szName, basename(wad->filename));
					COLOR4* texturedata = ConvertWadTexToRGBA(texture);

					lodepng_encode32_file((g_working_dir + "wads/" + basename(wad->filename) + "/" + std::string(texture->szName) + ".png").c_str()
						, (unsigned char*)texturedata, texture->nWidth, texture->nHeight);
					delete texturedata;
				}
				delete texture;
			});
		delete wad;
		return true;
	}
	return false;
}

bool Bsp::import_textures_to_wad(const std::string& wadpath, const std::string& texpath, bool dithering)
{
	if (!dirExists(texpath))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0351), texpath);
		return false;
	}
	else
	{
		if (!fileExists(wadpath))
		{
			Wad* resetWad = new Wad(wadpath);
			resetWad->write(NULL, 0);
			delete resetWad;

			if (!fileExists(wadpath))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0351), wadpath);
				return false;
			}
		}
		else
		{
			copyFile(wadpath, wadpath + ".bak");
		}

		Wad* tmpWad = new Wad(wadpath);

		std::vector<WADTEX*> textureList{};

		std::vector<std::string> files{};

		for (auto& dir_entry : std::filesystem::directory_iterator(texpath))
		{
			if (!dir_entry.is_directory() && ends_with(toLowerCase(dir_entry.path().string()), ".png"))
			{
				files.emplace_back(dir_entry.path().string());
			}
		}

		std::for_each(std::execution::par_unseq, files.begin(), files.end(), [&](const auto file)
			{
				print_log(get_localized_string(LANG_0352), basename(file), basename(wadpath));
				COLOR4* image_bytes = NULL;
				unsigned int w2, h2;
				auto error = lodepng_decode32_file((unsigned char**)&image_bytes, &w2, &h2, file.c_str());
				if (error == 0 && image_bytes)
				{
					COLOR3* image_bytes_rgb = (COLOR3*)&image_bytes[0];
					for (unsigned int i = 0; i < w2 * h2; i++)
					{
						COLOR4& curPixel = image_bytes[i];

						if (curPixel.a == 0)
						{
							image_bytes_rgb[i] = COLOR3(0, 0, 255);
						}
						else
						{
							image_bytes_rgb[i] = COLOR3(curPixel.r, curPixel.g, curPixel.b);
						}
					}

					int oldcolors = 0;
					if ((oldcolors = GetImageColors((COLOR3*)image_bytes, w2 * h2)) > 256)
					{
						print_log(get_localized_string(LANG_0353), basename(file));
						Quantizer* tmpCQuantizer = new Quantizer(256, 8);

						if (dithering)
							tmpCQuantizer->ApplyColorTableDither((COLOR3*)image_bytes, w2, h2);
						else
							tmpCQuantizer->ApplyColorTable((COLOR3*)image_bytes, w2 * h2);

						print_log(get_localized_string(LANG_0354), oldcolors, GetImageColors((COLOR3*)image_bytes, w2 * h2));

						delete tmpCQuantizer;
					}

					std::string tmpTexName = stripExt(basename(file));

					WADTEX* tmpWadTex = create_wadtex(tmpTexName.c_str(), (COLOR3*)image_bytes, w2, h2);
					g_mutex_list[1].lock();
					textureList.push_back(tmpWadTex);
					g_mutex_list[1].unlock();
					free(image_bytes);
				}
			});
		print_log(get_localized_string(LANG_0355));

		if (textureList.size())
		{
			tmpWad->write(textureList);
		}
		for (auto& tex : textureList)
			delete tex;

		delete tmpWad;
		if (renderer && textureList.size())
		{
			renderer->reuploadTextures();
			renderer->preRenderFaces();
		}
		else
		{
			print_log(PRINT_RED, get_localized_string(LANG_0252), wadpath);
			return false;
		}
	}
	return true;
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
	delete[] newLeaves;

	return newLeafIdx;
}

int Bsp::create_leaf_back(int contents)
{
	BSPLEAF32* newLeaves = new BSPLEAF32[leafCount + 1]{};
	memcpy(&newLeaves[1], leaves, leafCount * sizeof(BSPLEAF32));

	BSPLEAF32& newLeaf = newLeaves[0];

	newLeaf.nVisOffset = -1;
	newLeaf.nContents = contents;

	replace_lump(LUMP_LEAVES, newLeaves, (leafCount + 1) * sizeof(BSPLEAF32));
	delete[] newLeaves;

	return 0;
}

void Bsp::create_inside_box(const vec3& min, const vec3& max, BSPMODEL* targetModel, int textureIdx)
{
	create_primitive_box(min, max, targetModel, textureIdx, true);
	targetModel->iHeadnodes[1] = targetModel->iHeadnodes[2] = targetModel->iHeadnodes[3] = CONTENTS_EMPTY;
	for (int f = targetModel->iFirstFace; f < targetModel->iFirstFace + targetModel->nFaces; f++)
	{
		BSPFACE32 face = faces[f];
		int size[2];
		GetFaceLightmapSize(f, size);

		if (face.iTextureInfo >= 0)
		{
			BSPTEXTUREINFO* texinfo = get_unique_texinfo(f);

			size[0] *= g_limits.textureStep;
			size[1] *= g_limits.textureStep;

			if (texinfo->iMiptex >= 0)
			{
				int texOffset = ((int*)textures)[texinfo->iMiptex + 1];
				if (texOffset >= 0)
				{
					BSPMIPTEX tex = *((BSPMIPTEX*)(textures + texOffset));
					texinfo->vS /= 1.0f * size[0] / tex.nWidth;
					texinfo->vT /= 1.0f * size[1] / tex.nHeight;
				}
			}

			texinfo->nFlags = TEX_SPECIAL;
		}
	}
}

void Bsp::create_primitive_box(const vec3& min, const vec3& max, BSPMODEL* targetModel, int textureIdx, bool inside)
{
	// add new verts (1 for each corner)
	// TODO: subdivide faces to prevent max surface extents error
	unsigned int startVert = vertCount;
	{
		vec3* newVerts = new vec3[vertCount + 8];
		memcpy(newVerts, verts, vertCount * sizeof(vec3));

		if (inside)
		{
			newVerts[vertCount + 0] = vec3(min.x, min.y, min.z); // back-left-bottom
			newVerts[vertCount + 1] = vec3(max.x, min.y, min.z); // back-right-bottom
			newVerts[vertCount + 2] = vec3(max.x, max.y, min.z); // front-right-bottom
			newVerts[vertCount + 3] = vec3(min.x, max.y, min.z); // front-left-bottom

			newVerts[vertCount + 4] = vec3(min.x, min.y, max.z); // back-left-top
			newVerts[vertCount + 5] = vec3(max.x, min.y, max.z); // back-right-top
			newVerts[vertCount + 6] = vec3(max.x, max.y, max.z); // front-right-top
			newVerts[vertCount + 7] = vec3(min.x, max.y, max.z); // front-left-top
		}
		else
		{
			newVerts[vertCount + 0] = vec3(min.x, min.y, min.z); // front-left-bottom
			newVerts[vertCount + 1] = vec3(max.x, min.y, min.z); // front-right-bottom
			newVerts[vertCount + 2] = vec3(max.x, max.y, min.z); // back-right-bottom
			newVerts[vertCount + 3] = vec3(min.x, max.y, min.z); // back-left-bottom

			newVerts[vertCount + 4] = vec3(min.x, min.y, max.z); // front-left-top
			newVerts[vertCount + 5] = vec3(max.x, min.y, max.z); // front-right-top
			newVerts[vertCount + 6] = vec3(max.x, max.y, max.z); // back-right-top
			newVerts[vertCount + 7] = vec3(min.x, max.y, max.z); // back-left-top
		}

		replace_lump(LUMP_VERTICES, newVerts, (vertCount + 8) * sizeof(vec3));
		delete[] newVerts;
	}

	// add new edges (4 for each face)
	// TODO: subdivide >512
	unsigned int startEdge = edgeCount;
	{
		BSPEDGE32* newEdges = new BSPEDGE32[edgeCount + 12];
		memcpy(newEdges, edges, edgeCount * sizeof(BSPEDGE32));

		if (inside)
		{
			// left
			newEdges[startEdge + 0] = BSPEDGE32(startVert + 0, startVert + 3);
			newEdges[startEdge + 1] = BSPEDGE32(startVert + 7, startVert + 4);

			// right
			newEdges[startEdge + 2] = BSPEDGE32(startVert + 2, startVert + 1); // bottom edge
			newEdges[startEdge + 3] = BSPEDGE32(startVert + 5, startVert + 6); // right edge

			// front
			newEdges[startEdge + 4] = BSPEDGE32(startVert + 1, startVert + 0); // bottom edge
			newEdges[startEdge + 5] = BSPEDGE32(startVert + 4, startVert + 5); // top edge

			// back
			newEdges[startEdge + 6] = BSPEDGE32(startVert + 7, startVert + 3); // left edge
			newEdges[startEdge + 7] = BSPEDGE32(startVert + 2, startVert + 6); // right edge

			// bottom
			newEdges[startEdge + 8] = BSPEDGE32(startVert + 2, startVert + 3);
			newEdges[startEdge + 9] = BSPEDGE32(startVert + 0, startVert + 1);

			// top
			newEdges[startEdge + 10] = BSPEDGE32(startVert + 4, startVert + 7);
			newEdges[startEdge + 11] = BSPEDGE32(startVert + 6, startVert + 5);
		}
		else
		{
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
		}

		replace_lump(LUMP_EDGES, newEdges, (edgeCount + 12) * sizeof(BSPEDGE32));
		delete[] newEdges;
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
		delete[] newSurfedges;
	}

	BSPPLANE* newPlanes = new BSPPLANE[planeCount + 6];
	// add new planes (1 for each face/node)
	unsigned int startPlane = planeCount;
	{
		memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));

		newPlanes[startPlane + 0] = { /*inside ? -vec3(1.0f, 0.0f, 0.0f) :*/ vec3(1.0f, 0.0f, 0.0f), /*inside ? -min.x : */min.x, PLANE_X }; // left
		newPlanes[startPlane + 1] = { /*inside ? -vec3(1.0f, 0.0f, 0.0f) : */vec3(1.0f, 0.0f, 0.0f), /*inside ? -max.x : */max.x, PLANE_X }; // right
		newPlanes[startPlane + 2] = { /*inside ? -vec3(0.0f, 1.0f, 0.0f) : */vec3(0.0f, 1.0f, 0.0f), /*inside ? -min.y : */min.y, PLANE_Y }; // front
		newPlanes[startPlane + 3] = { /*inside ? -vec3(0.0f, 1.0f, 0.0f) : */vec3(0.0f, 1.0f, 0.0f), /*inside ? -max.y : */max.y, PLANE_Y }; // back
		newPlanes[startPlane + 4] = { /*inside ? -vec3(0.0f, 0.0f, 1.0f) : */vec3(0.0f, 0.0f, 1.0f), /*inside ? -min.z : */min.z, PLANE_Z }; // bottom
		newPlanes[startPlane + 5] = { /*inside ? -vec3(0.0f, 0.0f, 1.0f) : */vec3(0.0f, 0.0f, 1.0f), /*inside ? -max.z : */max.z, PLANE_Z }; // top

		/*	if (inside)
			{
				for (int i = 0; i < 6; i++)
				{
					newPlanes[startPlane + i].update_plane(newPlanes[startPlane + i].vNormal, newPlanes[startPlane + i].fDist);
				}
			}*/

		replace_lump(LUMP_PLANES, newPlanes, (planeCount + 6) * sizeof(BSPPLANE));
		delete[] newPlanes;
	}

	unsigned int startTexinfo = texinfoCount;
	{
		BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[texinfoCount + 6];
		memcpy(newTexinfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

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
			if (inside)
			{
				info.vT = -faceUp[i];
				info.vS = crossProduct(-faceUp[i], faceNormals[i]);
			}
			else
			{
				info.vT = faceUp[i];
				info.vS = crossProduct(faceUp[i], faceNormals[i]);
			}
			// TODO: fit texture to face
		}

		replace_lump(LUMP_TEXINFO, newTexinfos, (texinfoCount + 6) * sizeof(BSPTEXTUREINFO));
		delete[] newTexinfos;
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
			face.nPlaneSide = inside ? i % 2 != 0 : i % 2 == 0; // even-numbered planes are inverted
			face.iTextureInfo = (startTexinfo + i);
			face.nLightmapOffset = 0;
			memset(face.nStyles, 255, MAX_LIGHTMAPS);
		}

		replace_lump(LUMP_FACES, newFaces, (faceCount + 6) * sizeof(BSPFACE32));
		delete[] newFaces;
	}

	// Submodels don't use leaves like the world does. Everything except nContents is ignored.
	// There's really no need to create leaves for submodels. Every map will have a shared
	// SOLID leaf, and there should be at least one EMPTY leaf if the map isn't completely solid.
	// So, just find an existing EMPTY leaf. Also, water brushes work just fine with SOLID nodes.
	// The inner contents of a node is changed dynamically by entity properties.

	int sharedSolidLeaf = 0;
	int anyEmptyLeaf = create_leaf(CONTENTS_EMPTY);

	for (int f = faceCount - 7; f < faceCount; f++)
	{
		leaf_add_face(f, anyEmptyLeaf);
	}


	targetModel->nVisLeafs += 1;

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

			int insideContents = k == 5 ? (~sharedSolidLeaf) : (nodeCount + k + 1);
			int outsideContents = ~anyEmptyLeaf;

			// can't have negative normals on planes so children are swapped instead
			if (inside ? k % 2 != 0 : k % 2 == 0)
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
		delete[] newNodes;
	}

	targetModel->iHeadnodes[0] = startNode;
	targetModel->iFirstFace = startFace;
	targetModel->nFaces = 6;

	targetModel->nMaxs = vec3(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);
	targetModel->nMins = vec3(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
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
	leaves[anyEmptyLeaf].nMins = targetModel->nMins;
	leaves[anyEmptyLeaf].nMaxs = targetModel->nMaxs;
}

void Bsp::create_solid_nodes(Solid& solid, BSPMODEL* targetModel)
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
		delete[] newVerts;
	}

	// add new edges (not actually edges - just an indirection layer for the verts)
	// TODO: subdivide >512
	unsigned int startEdge = edgeCount;
	std::map<int, int> vertToSurfedge;
	{
		int addEdges = ((int)(solid.hullVerts.size()) + 1) / 2;

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
		delete[] newEdges;
	}

	// add new surfedges (2 for each edge)
	unsigned int startSurfedge = surfedgeCount;
	{
		int addSurfedges = 0;
		for (size_t i = 0; i < solid.faces.size(); i++)
		{
			addSurfedges += (int)(solid.faces[i].verts.size());
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
		delete[] newSurfedges;
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
		delete[] newPlanes;
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
		delete[] newFaces;
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
		targetModel->nVisLeafs += 1;
	}

	// add new nodes
	unsigned int startNode = nodeCount;
	{
		BSPNODE32* newNodes = new BSPNODE32[nodeCount + solid.faces.size()]{};
		memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE32));

		for (size_t k = 0; k < solid.faces.size(); k++)
		{
			BSPNODE32& node = newNodes[nodeCount + k];

			node.iFirstFace = (int)(startFace + k); // face required for decals
			node.nFaces = 1;
			node.iPlane = (int)(startPlane + k);
			// node mins/maxs don't matter for submodels. Leave them at 0.

			int insideContents = k == solid.faces.size() - 1 ? ~sharedSolidLeaf : (int)(nodeCount + k + 1);
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
		delete[] newNodes;
	}

	targetModel->iHeadnodes[0] = startNode;
	targetModel->iHeadnodes[1] = CONTENTS_EMPTY;
	targetModel->iHeadnodes[2] = CONTENTS_EMPTY;
	targetModel->iHeadnodes[3] = CONTENTS_EMPTY;
	targetModel->iFirstFace = startFace;
	targetModel->nFaces = (int)solid.faces.size();

	targetModel->nMaxs = vec3(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);
	targetModel->nMins = vec3(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
	for (size_t i = 0; i < solid.hullVerts.size(); i++)
	{
		vec3 v = verts[startVert + i];
		expandBoundingBox(v, targetModel->nMins, targetModel->nMaxs);
	}
}

int Bsp::create_node_box(const vec3& mins, const vec3& maxs, BSPMODEL* targetModel, bool empty, int leafIdx)
{
	int sharedSolidLeaf = 0;
	int anyEmptyLeaf = leafIdx;
	if (anyEmptyLeaf == 0)
	{
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
			targetModel->nVisLeafs += 1;
		}
	}

	vec3 min = mins;
	vec3 max = maxs;

	int nodeIdx = nodeCount;
	int planeIdx = planeCount;

	targetModel->iHeadnodes[0] = nodeCount;

	std::vector<BSPPLANE> addPlanes;
	std::vector<BSPNODE32> addNodes;

	addPlanes.emplace_back(vec3(1.0f, 0.0f, 0.0f), min.x, PLANE_X); // left
	addPlanes.emplace_back(vec3(1.0f, 0.0f, 0.0f), max.x, PLANE_X); // right
	addPlanes.emplace_back(vec3(0.0f, 1.0f, 0.0f), min.y, PLANE_Y); // front
	addPlanes.emplace_back(vec3(0.0f, 1.0f, 0.0f), max.y, PLANE_Y); // back
	addPlanes.emplace_back(vec3(0.0f, 0.0f, 1.0f), min.z, PLANE_Z); // bottom
	addPlanes.emplace_back(vec3(0.0f, 0.0f, 1.0f), max.z, PLANE_Z); // top

	int solidNodeIdx = 0;

	for (int k = 0; k < 6; k++)
	{
		BSPNODE32 node = BSPNODE32();
		node.iPlane = (int)planeIdx++;

		node.nMins = node.nMaxs = vec3();


		if (k == 5)
			solidNodeIdx = nodeIdx;

		nodeIdx++;
		int insideContents = k == 5 ? (empty ? ~anyEmptyLeaf : ~sharedSolidLeaf) : nodeIdx;

		// can't have negative normals on planes so children are swapped instead
		if (k % 2 == 0)
		{
			node.iChildren[0] = insideContents;
			node.iChildren[1] = ~anyEmptyLeaf;
		}
		else
		{
			node.iChildren[0] = ~anyEmptyLeaf;
			node.iChildren[1] = insideContents;
		}

		addNodes.push_back(node);
	}


	BSPPLANE* newPlanes = new BSPPLANE[planeCount + addPlanes.size()];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));
	std::copy(addPlanes.begin(), addPlanes.end(), newPlanes + planeCount);
	replace_lump(LUMP_PLANES, newPlanes, (planeCount + addPlanes.size()) * sizeof(BSPPLANE));

	BSPNODE32* newNodes = new BSPNODE32[nodeCount + addNodes.size()];
	memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE32));
	if (addNodes.size())
		std::copy(addNodes.begin(), addNodes.end(), newNodes + nodeCount);
	replace_lump(LUMP_NODES, newNodes, (nodeCount + addNodes.size()) * sizeof(BSPNODE32));


	delete[] newPlanes;
	delete[] newNodes;

	return solidNodeIdx;
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

		addPlanes.emplace_back(vec3(1.0f, 0.0f, 0.0f), min.x, PLANE_X); // left
		addPlanes.emplace_back(vec3(1.0f, 0.0f, 0.0f), max.x, PLANE_X); // right
		addPlanes.emplace_back(vec3(0.0f, 1.0f, 0.0f), min.y, PLANE_Y); // front
		addPlanes.emplace_back(vec3(0.0f, 1.0f, 0.0f), max.y, PLANE_Y); // back
		addPlanes.emplace_back(vec3(0.0f, 0.0f, 1.0f), min.z, PLANE_Z); // bottom
		addPlanes.emplace_back(vec3(0.0f, 0.0f, 1.0f), max.z, PLANE_Z); // top

		targetModel->iHeadnodes[i] = clipnodeCount + (int)addNodes.size();

		for (int k = 0; k < 6; k++)
		{
			BSPCLIPNODE32 node = BSPCLIPNODE32();
			node.iPlane = (int)planeIdx++;


			int insideContents = k == 5 ? CONTENTS_SOLID : clipnodeIdx + 1;

			if (k == 5)
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


	delete[] newPlanes;
	delete[] newClipnodes;

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

	vec3 vertMin(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
	vec3 vertMax(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);

	create_clipnode_box(vertMin, vertMax, &model, hullIdx, true);
}

int Bsp::create_clipnode(bool force_reversed, int reversed_id)
{
	if (!force_reversed)
	{
		BSPCLIPNODE32* newNodes = new BSPCLIPNODE32[clipnodeCount + 1];
		memcpy(newNodes, clipnodes, clipnodeCount * sizeof(BSPCLIPNODE32));
		newNodes[clipnodeCount] = BSPCLIPNODE32();
		replace_lump(LUMP_CLIPNODES, newNodes, (clipnodeCount + 1) * sizeof(BSPCLIPNODE32));
		delete[] newNodes;
		return clipnodeCount - 1;
	}

	// do big magic!
	std::vector<BSPCLIPNODE32> newNodes;
	for (int i = 0; i < clipnodeCount; i++)
	{
		if (i == reversed_id)
			newNodes.emplace_back(BSPCLIPNODE32());
		newNodes.push_back(clipnodes[i]);
	}

	BSPCLIPNODE32* newNodesArray = new BSPCLIPNODE32[newNodes.size()];
	memcpy(newNodesArray, newNodes.data(), newNodes.size() * sizeof(BSPCLIPNODE32));
	replace_lump(LUMP_CLIPNODES, newNodesArray, newNodes.size() * sizeof(BSPCLIPNODE32));
	delete[] newNodesArray;

	for (int i = 0; i < clipnodeCount; i++)
	{
		if (clipnodes[i].iChildren[0] >= reversed_id)
		{
			clipnodes[i].iChildren[0]++;
		}
		if (clipnodes[i].iChildren[1] >= reversed_id)
		{
			clipnodes[i].iChildren[1]++;
		}
	}

	for (int i = 0; i < modelCount; i++)
	{
		for (int h = 1; h < MAX_MAP_HULLS; h++)
		{
			if (models[i].iHeadnodes[h] >= reversed_id)
				models[i].iHeadnodes[h]++;
		}
	}

	return reversed_id;
}

int Bsp::create_node(bool force_reversed, int reversed_id)
{
	if (!force_reversed)
	{
		BSPNODE32* newNodes = new BSPNODE32[nodeCount + 1];
		memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE32));
		newNodes[nodeCount] = BSPNODE32();
		replace_lump(LUMP_NODES, newNodes, (nodeCount + 1) * sizeof(BSPNODE32));
		delete[] newNodes;
		return nodeCount - 1;
	}

	// do big magic!
	std::vector<BSPNODE32> newNodes;
	for (int i = 0; i < nodeCount; i++)
	{
		if (i == reversed_id)
			newNodes.emplace_back(BSPNODE32());
		newNodes.push_back(nodes[i]);
	}

	BSPNODE32* newNodesArray = new BSPNODE32[newNodes.size()];
	memcpy(newNodesArray, newNodes.data(), newNodes.size() * sizeof(BSPNODE32));
	replace_lump(LUMP_NODES, newNodesArray, newNodes.size() * sizeof(BSPNODE32));
	delete[] newNodesArray;

	for (int i = 0; i < nodeCount; i++)
	{
		if (nodes[i].iChildren[0] >= reversed_id)
		{
			nodes[i].iChildren[0]++;
		}
		if (nodes[i].iChildren[1] >= reversed_id)
		{
			nodes[i].iChildren[1]++;
		}
	}

	for (int i = 0; i < modelCount; i++)
	{
		if (models[i].iHeadnodes[0] >= reversed_id)
		{
			models[i].iHeadnodes[0]++;
		}
	}

	return reversed_id;
}

int Bsp::create_edge()
{
	BSPEDGE32* newEdges = new BSPEDGE32[edgeCount + 1];
	memcpy(newEdges, edges, edgeCount * sizeof(BSPEDGE32));
	newEdges[edgeCount] = BSPEDGE32();
	replace_lump(LUMP_EDGES, newEdges, (edgeCount + 1) * sizeof(BSPEDGE32));
	delete[] newEdges;
	return edgeCount - 1;
}

int Bsp::create_vert()
{
	vec3* newVerts = new vec3[vertCount + 1];
	memcpy(newVerts, verts, vertCount * sizeof(vec3));
	newVerts[vertCount] = vec3();
	replace_lump(LUMP_VERTICES, newVerts, (vertCount + 1) * sizeof(vec3));
	delete[] newVerts;
	return vertCount - 1;
}

int Bsp::create_surfedge()
{
	int* newSurfedges = new int[surfedgeCount + 1];
	memcpy(newSurfedges, surfedges, surfedgeCount * sizeof(int));
	newSurfedges[surfedgeCount] = 0;
	replace_lump(LUMP_SURFEDGES, newSurfedges, (surfedgeCount + 1) * sizeof(int));
	delete[] newSurfedges;
	return surfedgeCount - 1;
}

int Bsp::create_plane()
{
	BSPPLANE* newPlanes = new BSPPLANE[planeCount + 1];
	memcpy(newPlanes, planes, planeCount * sizeof(BSPPLANE));
	newPlanes[planeCount] = BSPPLANE();
	replace_lump(LUMP_PLANES, newPlanes, (planeCount + 1) * sizeof(BSPPLANE));
	delete[] newPlanes;
	return planeCount - 1;
}

int Bsp::create_model()
{
	BSPMODEL* newModels = new BSPMODEL[modelCount + 1];
	memcpy(newModels, models, modelCount * sizeof(BSPMODEL));

	newModels[modelCount] = BSPMODEL();
	//BSPMODEL& newModel = newModels[modelCount];

	replace_lump(LUMP_MODELS, newModels, (modelCount + 1) * sizeof(BSPMODEL));
	delete[] newModels;
	return modelCount - 1;
}


int Bsp::create_texinfo()
{
	BSPTEXTUREINFO* newTexinfos = new BSPTEXTUREINFO[texinfoCount + 1];
	memcpy(newTexinfos, texinfos, texinfoCount * sizeof(BSPTEXTUREINFO));

	newTexinfos[texinfoCount] = BSPTEXTUREINFO();

	replace_lump(LUMP_TEXINFO, newTexinfos, (texinfoCount + 1) * sizeof(BSPTEXTUREINFO));
	delete[] newTexinfos;
	return texinfoCount - 1;
}

void Bsp::copy_bsp_model(int modelIdx, Bsp* targetMap, STRUCTREMAP& remap, STRUCTUSAGE& usage, std::vector<BSPPLANE>& newPlanes, std::vector<vec3>& newVerts,
	std::vector<BSPEDGE32>& newEdges, std::vector<int>& newSurfedges, std::vector<BSPTEXTUREINFO>& newTexinfo,
	std::vector<BSPFACE32>& newFaces, std::vector<COLOR3>& newLightmaps, std::vector<BSPNODE32>& newNodes,
	std::vector<BSPCLIPNODE32>& newClipnodes, std::vector<WADTEX*>& newTextures, std::vector<BSPLEAF32>& newLeafs, std::vector<int>& newMarkSurfs, bool forExport)
{
	if (forExport && leafCount > 0)
		usage.leaves[0] = true;

	if (forExport && edgeCount > 0)
		usage.edges[0] = true;

	mark_model_structures(modelIdx, &usage, !forExport);

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

	for (unsigned int i = 0; i < usage.count.texInfos; i++)
	{
		if (usage.texInfo[i])
		{
			BSPTEXTUREINFO texinfo = texinfos[i];
			if (texinfo.iMiptex >= 0)
			{
				int texOffset = ((int*)textures)[texinfo.iMiptex + 1];
				if (texOffset >= 0 && !usedmips.count(texinfo.iMiptex))
				{
					usedmips.insert(texinfo.iMiptex);
					BSPMIPTEX* tex = ((BSPMIPTEX*)(textures + texOffset));

					if (!is_texture_has_pal)
					{
						if (g_settings.pal_id >= 0)
						{
							WADTEX* newTex = new WADTEX(tex, g_settings.palettes[g_settings.pal_id].data,
								(unsigned short)g_settings.palettes[g_settings.pal_id].colors);
							newTextures.push_back(newTex);
						}
						else
						{
							WADTEX* newTex = new WADTEX(tex, g_settings.palette_default);
							newTextures.push_back(newTex);
						}
					}
					else
					{
						WADTEX* newTex = new WADTEX(tex);
						newTextures.push_back(newTex);
					}
				}
			}
			remap.texInfo[i] = targetMap->texinfoCount + (int)newTexinfo.size();
			newTexinfo.push_back(texinfo);
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
			GetFaceLightmapSize(i, size);

			int lightmapCount = lightmap_count(i);

			int lightmapSz = size[0] * size[1] * lightmapCount;

			if (this->lightdata && face.nLightmapOffset >= 0 && lightmapCount > 0)
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

	if (forExport)
	{
		for (unsigned int i = 0; i < usage.count.markSurfs; i++)
		{
			if (usage.markSurfs[i])
			{
				remap.markSurfs[i] = targetMap->marksurfCount + (int)newMarkSurfs.size();
				int marksurf = remap.faces[this->marksurfs[i]];
				newMarkSurfs.push_back(marksurf);
			}
		}

		for (unsigned int i = 0; i < usage.count.leaves; i++)
		{
			if (usage.leaves[i])
			{
				remap.leaves[i] = targetMap->leafCount + (int)newLeafs.size();
				BSPLEAF32 leaf = this->leaves[i];
				// no visdata at this time
				leaf.nVisOffset = -1;
				leaf.iFirstMarkSurface = remap.markSurfs[leaf.iFirstMarkSurface];
				newLeafs.push_back(leaf);
			}
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

	bool found_zero_leaf = false;

	for (size_t i = 0; i < newNodes.size(); i++)
	{
		BSPNODE32& node = newNodes[i];
		node.iFirstFace = remap.faces[node.iFirstFace];
		node.iPlane = remap.planes[node.iPlane];

		for (int k = 0; k < 2; k++)
		{
			if (node.iChildren[k] >= 0)
			{
				node.iChildren[k] = remap.nodes[node.iChildren[k]];
			}
			else if (forExport)
			{
				int leafIdx = ~node.iChildren[k];
				if (leafIdx == 0)
				{
					node.iChildren[k] = -1;
					if (~(remap.leaves[leafIdx]) != -1)
						found_zero_leaf = true;
				}
				else
					node.iChildren[k] = ~(remap.leaves[leafIdx]);
			}
		}
	}

	if (forExport && found_zero_leaf)
	{
		newLeafs.erase(newLeafs.begin());
		for (size_t i = 0; i < newNodes.size(); i++)
		{
			BSPNODE32& node = newNodes[i];
			if (node.iChildren[0] < -1)
			{
				node.iChildren[0]++;
			}
			if (node.iChildren[1] < -1)
			{
				node.iChildren[1]++;
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
			if (clipnode.iChildren[k] >= 0)
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
	std::vector<BSPLEAF32> newLeaves;
	std::vector<int> newMarkSurfaces;

	STRUCTREMAP remap(this);
	STRUCTUSAGE usage(this);
	copy_bsp_model(modelIdx, this, remap, usage, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces,
		newLightmaps, newNodes, newClipnodes, newTextures, newLeaves, newMarkSurfaces);

	for (auto& s : newTextures)
	{
		delete s;
	}

	if (newClipnodes.size())
	{
		append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE32) * newClipnodes.size());
	}

	if (newEdges.size())
	{
		append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE32) * newEdges.size());
	}

	if (newFaces.size())
	{
		append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE32) * newFaces.size());
	}

	if (newNodes.size())
	{
		append_lump(LUMP_NODES, &newNodes[0], sizeof(BSPNODE32) * newNodes.size());
	}

	if (newPlanes.size())
	{
		append_lump(LUMP_PLANES, &newPlanes[0], sizeof(BSPPLANE) * newPlanes.size());
	}

	if (newSurfedges.size())
	{
		append_lump(LUMP_SURFEDGES, &newSurfedges[0], sizeof(int) * newSurfedges.size());
	}

	if (newTexinfo.size())
	{
		append_lump(LUMP_TEXINFO, &newTexinfo[0], sizeof(BSPTEXTUREINFO) * newTexinfo.size());
	}

	if (newVerts.size())
	{
		append_lump(LUMP_VERTICES, &newVerts[0], sizeof(vec3) * newVerts.size());
	}

	//if (newLeaves.size())
	//{
	//	append_lump(LUMP_LEAVES, &newLeaves[0], sizeof(BSPLEAF32) * newLeaves.size());
	//}

	//if (newMarkSurfaces.size())
	//{
	//	append_lump(LUMP_MARKSURFACES, &newMarkSurfaces[0], sizeof(int) * newMarkSurfaces.size());
	//}

	if (newLightmaps.size())
	{
		append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());
		save_undo_lightmaps();
		resize_all_lightmaps();
		renderer->loadLightmaps();
	}

	BSPMODEL& oldModel = models[modelIdx];
	oldModel.iFirstFace = remap.faces[oldModel.iFirstFace];
	oldModel.iHeadnodes[0] = oldModel.iHeadnodes[0] < 0 ? -1 : remap.nodes[oldModel.iHeadnodes[0]];
	for (int i = 1; i < MAX_MAP_HULLS; i++)
	{
		oldModel.iHeadnodes[i] = oldModel.iHeadnodes[i] < 0 ? -1 : remap.clipnodes[oldModel.iHeadnodes[i]];
	}

	pickCount++;
	vertPickCount++;
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
	std::vector<BSPLEAF32> newLeaves;
	std::vector<int> newMarkSurfaces;

	STRUCTREMAP remap(this);
	STRUCTUSAGE usage(this);
	copy_bsp_model(modelIdx, this, remap, usage, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces,
		newLightmaps, newNodes, newClipnodes, newTextures, newLeaves, newMarkSurfaces);

	for (auto& s : newTextures)
	{
		delete s;
	}

	if (newClipnodes.size())
	{
		append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE32) * newClipnodes.size());
	}

	if (newEdges.size())
	{
		append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE32) * newEdges.size());
	}

	if (newFaces.size())
	{
		/*if (g_settings.verboseLogs)
		{
			print_log("Origin model faces: {}\n", models[modelIdx].nFaces);
			print_log("Base light offset = {} copy faces {}\n", lightDataLength, newFaces.size());
			for (size_t i = 0; i < newFaces.size(); i++)
			{
				print_log("Face {} light offset = {}\n", i, newFaces[i].nLightmapOffset);
			}
		}*/
		append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE32) * newFaces.size());
	}

	if (newNodes.size())
	{
		append_lump(LUMP_NODES, &newNodes[0], sizeof(BSPNODE32) * newNodes.size());
	}

	if (newPlanes.size())
	{
		append_lump(LUMP_PLANES, &newPlanes[0], sizeof(BSPPLANE) * newPlanes.size());
	}

	if (newSurfedges.size())
	{
		append_lump(LUMP_SURFEDGES, &newSurfedges[0], sizeof(int) * newSurfedges.size());
	}

	if (newTexinfo.size())
	{
		append_lump(LUMP_TEXINFO, &newTexinfo[0], sizeof(BSPTEXTUREINFO) * newTexinfo.size());
	}

	if (newVerts.size())
	{
		append_lump(LUMP_VERTICES, &newVerts[0], sizeof(vec3) * newVerts.size());
	}

	//if (newLeaves.size())
	//{
	//	append_lump(LUMP_LEAVES, &newLeaves[0], sizeof(BSPLEAF32) * newLeaves.size());
	//}

	//if (newMarkSurfaces.size())
	//{
	//	append_lump(LUMP_MARKSURFACES, &newMarkSurfaces[0], sizeof(int) * newMarkSurfaces.size());
	//}

	if (newLightmaps.size())
	{
		append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());
		save_undo_lightmaps();
		resize_all_lightmaps();
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
	newModel.nVisLeafs = oldModel.nVisLeafs; // techinically should match the old model, but leaves aren't duplicated yetx
	oldModel.nVisLeafs = 0;


	pickCount++;
	vertPickCount++;
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

	save_undo_lightmaps();
	resize_all_lightmaps();

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

		if (!has_face && (leafIdx <= -1 || leafIdx == i))
		{
			leaves[i].nMarkSurfaces += 1;
			all_mark_surfaces.push_back(faceIdx);
		}
		surface_idx += leaves[i].nMarkSurfaces;
	}

	unsigned char* newLump = new unsigned char[sizeof(int) * all_mark_surfaces.size()];
	memcpy(newLump, &all_mark_surfaces[0], sizeof(int) * all_mark_surfaces.size());
	replace_lump(LUMP_MARKSURFACES, newLump, sizeof(int) * all_mark_surfaces.size());
	delete[] newLump;
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
			if (marksurfs[leaves[i].iFirstMarkSurface + n] == faceIdx && (leafIdx <= -1 || leafIdx == i))
			{
				del_faces++;
			}
			else
			{
				all_mark_surfaces.push_back(marksurfs[leaves[i].iFirstMarkSurface + n]);
			}
		}

		leaves[i].iFirstMarkSurface = surface_idx;
		surface_idx = (int)all_mark_surfaces.size();
		leaves[i].nMarkSurfaces = surface_idx - leaves[i].iFirstMarkSurface;
	}

	unsigned char* newLump = new unsigned char[sizeof(int) * all_mark_surfaces.size()];
	memcpy(newLump, &all_mark_surfaces[0], sizeof(int) * all_mark_surfaces.size());
	replace_lump(LUMP_MARKSURFACES, newLump, sizeof(int) * all_mark_surfaces.size());
	delete[] newLump;
	return true;
}

std::vector<int> Bsp::getFaceContents(int faceIdx)
{
	std::vector<int> out;
	auto face_leafs = getFaceLeafs(faceIdx);
	for (auto l : face_leafs)
	{
		if (std::find(out.begin(), out.end(), leaves[l].nContents) == out.end())
		{
			out.push_back(leaves[l].nContents);
		}
	}
	return out;
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

	save_undo_lightmaps();
	resize_all_lightmaps();

	g_progress.clear();
	g_progress = ProgressMeter();

	print_log("Removed {} faces from map!\n", removedFaces);
}

bool Bsp::remove_face(int faceIdx, bool fromModels)
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

	if (!fromModels)
	{
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
		delete[] newLump;
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
					delete[] newThisPlanes;
				}
				replace_lump(LUMP_NODES, newThisNodes, (nodeCount + 2) * sizeof(BSPNODE32));
				delete[] newThisNodes;
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
				newThisNodes[nodeCount + 1].iChildren[1] = ~leafCount;

				if (newThisNodes[nodeCount + 1].iPlane >= 0)
				{
					newThisNodes[nodeCount + 1].iPlane = planeCount;
					BSPPLANE* newThisPlanes = new BSPPLANE[planeCount + 1];
					memcpy(newThisPlanes, planes, planeCount * sizeof(BSPPLANE));
					newThisPlanes[planeCount] = planes[node.iPlane];
					replace_lump(LUMP_PLANES, newThisPlanes, (planeCount + 1) * sizeof(BSPPLANE));
					delete[] newThisPlanes;
				}

				replace_lump(LUMP_NODES, newThisNodes, (nodeCount + 2) * sizeof(BSPNODE32));
				delete[] newThisNodes;
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

			int* newMarkSurfs = new int[marksurfCount + leaves[oldleafIdx].nMarkSurfaces];
			memcpy(newMarkSurfs, marksurfs, marksurfCount * sizeof(int));
			memcpy(newMarkSurfs + marksurfCount, &marksurfs[leaves[oldleafIdx].iFirstMarkSurface],
				leaves[oldleafIdx].nMarkSurfaces * sizeof(int));
			replace_lump(LUMP_MARKSURFACES, newMarkSurfs, (marksurfCount + leaves[oldleafIdx].nMarkSurfaces) * sizeof(int));
			delete[] newMarkSurfs;
		}

		models[0].nVisLeafs++;
	}

	unsigned char* visData = new unsigned char[newRowSize];
	unsigned char* compressed = new unsigned char[g_limits.maxMapLeaves / 8];

	// ADD ONE LEAF TO ALL VISIBILITY BYTES
	for (int i = 1; i < leafCount; i++)
	{
		if (leaves[i].nVisOffset >= 0)
		{
			memset(visData, 0, newRowSize);
			DecompressVis(visdata + leaves[i].nVisOffset, visData, rowSize, leafCount - 1, visDataLength - leaves[i].nVisOffset);

			memset(compressed, 0, g_limits.maxMapLeaves / 8);
			int size = CompressVis(visData, newRowSize, compressed, g_limits.maxMapLeaves / 8);

			leaves[i].nVisOffset = visDataLength;

			unsigned char* newVisLump = new unsigned char[visDataLength + size];
			memcpy(newVisLump, visdata, visDataLength);
			memcpy(newVisLump + visDataLength, compressed, size);
			replace_lump(LUMP_VISIBILITY, newVisLump, visDataLength + size);
			delete[] newVisLump;
		}
	}

	delete[] compressed;
	delete[] visData;

	BSPLEAF32* newLeaves = new BSPLEAF32[outLeafs.size()];
	memcpy(newLeaves, outLeafs.data(), outLeafs.size() * sizeof(BSPLEAF32));
	replace_lump(LUMP_LEAVES, newLeaves, outLeafs.size() * sizeof(BSPLEAF32));
	delete[] newLeaves;

	// repack visdata
	auto removed = remove_unused_model_structures(CLEAN_VISDATA);

	if (!removed.allZero())
		removed.print_delete_stats(1);

	return leafCount - 1;
}

void Bsp::swap_two_models(int model1, int model2)
{
	if (model1 != model2)
	{
		if (model1 < modelCount &&
			model2 < modelCount)
		{
			std::swap(models[model1], models[model2]);
			for (size_t i = 0; i < ents.size(); i++)
			{
				if (ents[i]->getBspModelIdx() == model1)
				{
					ents[i]->setOrAddKeyvalue("model", "*" + std::to_string(model2));
				}
				else if (ents[i]->getBspModelIdx() == model2)
				{
					ents[i]->setOrAddKeyvalue("model", "*" + std::to_string(model1));
				}
			}
			update_ent_lump();
		}
	}
}

int Bsp::merge_two_models_idx(int src_model, int dst_model, int& tryanotherway)
{
	vec3 amin, amax, bmin, bmax;

	amin = models[src_model].nMins;
	amax = models[src_model].nMaxs;
	bmin = models[dst_model].nMins;
	bmax = models[dst_model].nMaxs;

	vec3 ent_offset = vec3();

	vec3 verts_offset = getCenter(amax, amin) - getCenter(bmax, bmin);

	BSPPLANE separate_plane = getSeparatePlane(bmin, bmax, amin, amax);

	if (separate_plane.nType == -1 && tryanotherway == 0)
	{
		tryanotherway++;
		// try to swap
		swap_two_models(dst_model, src_model);
		return merge_two_models_idx(src_model, dst_model, tryanotherway);
	}
	else if (separate_plane.nType == -1 && tryanotherway == 1)
	{
		tryanotherway++;
		return -1;
	}

	STRUCTUSAGE shouldBeMoved(this);
	mark_model_structures(src_model, &shouldBeMoved, true);

	// TODO update planes for headnode[0] ?
	for (int i = 0; i < planeCount; i++)
	{
		if (!shouldBeMoved.planes[i])
		{
			continue; // don't move submodels with origins
		}

		BSPPLANE& plane = planes[i];
		vec3 newPlaneOri = ent_offset + (plane.vNormal * plane.fDist);

		if (std::fabs(newPlaneOri.x) > g_limits.fltMaxCoord || std::fabs(newPlaneOri.y) > g_limits.fltMaxCoord ||
			std::fabs(newPlaneOri.z) > g_limits.fltMaxCoord)
		{
			print_log(get_localized_string(LANG_0053));
		}

		// get distance between new plane origin and the origin-aligned plane
		plane.fDist = dotProduct(plane.vNormal, newPlaneOri) / dotProduct(plane.vNormal, plane.vNormal);
	}


	auto src_verts = getModelVertsIds(src_model);
	for (auto v : src_verts)
	{
		verts[v] += ent_offset;
	}

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
	delete[] newLump;

	print_log(PRINT_GREEN, "SeparatePlane : {:4f} {:4f} {:4f} -> {:4f}\n", separate_plane.vNormal.x, separate_plane.vNormal.y, separate_plane.vNormal.z, separate_plane.fDist);

	std::vector<vec3> veclist = { amin,amax,bmin,bmax };

	print_log("- vec1 : {} {} {} \n", amin.x, amin.y, amin.z);
	print_log(" vec2 : {} {} {} \n", amax.x, amax.y, amax.z);
	print_log(" vec1 : {} {} {} \n", bmin.x, bmin.y, bmin.z);
	print_log(" vec2 : {} {} {} \n", bmax.x, bmax.y, bmax.z);

	print_log(PRINT_GREEN, "MODEL {} AND {} SUCCESS MERGED TO {}!\n", src_model, dst_model, dst_model);

	vec3 new_min, new_max;
	getBoundingBox(veclist, new_min, new_max);


	int separationPlaneIdx = planeCount;

	BSPPLANE* newThisPlanes = new BSPPLANE[planeCount + 1];
	memcpy(newThisPlanes, planes, planeCount * sizeof(BSPPLANE));

	bool swapNodeChildren = separate_plane.vNormal.x < 0 || separate_plane.vNormal.y < 0 || separate_plane.vNormal.z < 0;
	if (swapNodeChildren)
		separate_plane.vNormal = separate_plane.vNormal.invert();

	newThisPlanes[planeCount] = separate_plane;
	replace_lump(LUMP_PLANES, newThisPlanes, (planeCount + 1) * sizeof(BSPPLANE));
	delete[] newThisPlanes;

	{
		if (models[dst_model].iHeadnodes[0] >= 0 || models[src_model].iHeadnodes[0] >= 0)
		{
			int target_node = models[dst_model].iHeadnodes[0] >= 0 && models[src_model].iHeadnodes[0] >= 0 ?
				std::min(models[dst_model].iHeadnodes[0], models[src_model].iHeadnodes[0]) : -1;
			if (target_node == -1)
				target_node = models[dst_model].iHeadnodes[0] >= 0 ? models[dst_model].iHeadnodes[0] : models[src_model].iHeadnodes[0];

			int newnode = create_node(true, target_node);

			BSPNODE32& headNode = nodes[newnode];

			headNode = {
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
				std::swap(headNode.iChildren[0], headNode.iChildren[1]);
			}
			models[dst_model].iHeadnodes[0] = newnode;
		}
	}

	{
		for (int h = 1; h < MAX_MAP_HULLS; h++)
		{
			if (models[dst_model].iHeadnodes[h] >= 0 || models[src_model].iHeadnodes[h] >= 0)
			{
				int target_node = models[dst_model].iHeadnodes[h] >= 0 && models[src_model].iHeadnodes[h] >= 0 ?
					std::min(models[dst_model].iHeadnodes[h], models[src_model].iHeadnodes[h]) : -1;
				if (target_node == -1)
					target_node = models[dst_model].iHeadnodes[h] >= 0 ? models[dst_model].iHeadnodes[h] : models[src_model].iHeadnodes[h];

				int newclip = create_clipnode(true, target_node);

				BSPCLIPNODE32& headNode = clipnodes[newclip];

				headNode = {
					separationPlaneIdx,	// plane idx
					{	// child nodes
						models[src_model].iHeadnodes[h],
						models[dst_model].iHeadnodes[h]
					},
				};

				if (swapNodeChildren)
				{
					std::swap(headNode.iChildren[0], headNode.iChildren[1]);
				}

				models[dst_model].iHeadnodes[h] = newclip;
			}
		}
	}

	models[dst_model].nFaces += newfaces;
	models[dst_model].nVisLeafs += models[src_model].nVisLeafs;

	models[dst_model].nMins = new_min;
	models[dst_model].nMaxs = new_max;

	models[dst_model].vOrigin = models[src_model].vOrigin;

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

	return dst_model;
}

int Bsp::merge_two_models_ents(int src_ent, int dst_ent, int& tryanotherway)
{
	int src_model = ents[src_ent]->getBspModelIdx();
	int dst_model = ents[dst_ent]->getBspModelIdx();

	vec3 amin, amax, bmin, bmax;

	get_model_vertex_bounds(src_model, amin, amax);
	get_model_vertex_bounds(dst_model, bmin, bmax);

	vec3 ent_offset = ents[src_ent]->origin - ents[dst_ent]->origin;

	vec3 verts_offset = getCenter(amax, amin) - getCenter(bmax, bmin);

	amin += ents[src_ent]->origin;
	amax += ents[src_ent]->origin;

	bmin += ents[dst_ent]->origin;
	bmax += ents[dst_ent]->origin;

	BSPPLANE separate_plane = getSeparatePlane(bmin, bmax, amin, amax);

	if (separate_plane.nType == -1 && tryanotherway == 0)
	{
		tryanotherway++;
		return merge_two_models_ents(dst_ent, src_ent, tryanotherway);
	}
	else if (separate_plane.nType == -1 && tryanotherway == 1)
	{
		tryanotherway++;
		return -1;
	}

	STRUCTUSAGE shouldBeMoved(this);
	mark_model_structures(src_model, &shouldBeMoved, true);

	// TODO update planes for headnode[0] ?
	for (int i = 0; i < planeCount; i++)
	{
		if (!shouldBeMoved.planes[i])
		{
			continue; // don't move submodels with origins
		}

		BSPPLANE& plane = planes[i];
		vec3 newPlaneOri = ent_offset + (plane.vNormal * plane.fDist);

		if (std::fabs(newPlaneOri.x) > g_limits.fltMaxCoord || std::fabs(newPlaneOri.y) > g_limits.fltMaxCoord ||
			std::fabs(newPlaneOri.z) > g_limits.fltMaxCoord)
		{
			print_log(get_localized_string(LANG_0053));
		}

		// get distance between new plane origin and the origin-aligned plane
		plane.fDist = dotProduct(plane.vNormal, newPlaneOri) / dotProduct(plane.vNormal, plane.vNormal);
	}


	auto src_verts = getModelVertsIds(src_model);
	for (auto v : src_verts)
	{
		verts[v] += ent_offset;
	}

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
	delete[] newLump;

	print_log(PRINT_GREEN, "SeparatePlane : {:4f} {:4f} {:4f} -> {:4f}\n", separate_plane.vNormal.x, separate_plane.vNormal.y, separate_plane.vNormal.z, separate_plane.fDist);


	std::vector<vec3> veclist = { amin,amax,bmin,bmax };

	print_log("- vec1 : {} {} {} \n", amin.x, amin.y, amin.z);
	print_log(" vec2 : {} {} {} \n", amax.x, amax.y, amax.z);
	print_log(" vec1 : {} {} {} \n", bmin.x, bmin.y, bmin.z);
	print_log(" vec2 : {} {} {} \n", bmax.x, bmax.y, bmax.z);

	vec3 new_min, new_max;
	getBoundingBox(veclist, new_min, new_max);


	int separationPlaneIdx = planeCount;

	BSPPLANE* newThisPlanes = new BSPPLANE[planeCount + 1];
	memcpy(newThisPlanes, planes, planeCount * sizeof(BSPPLANE));

	bool swapNodeChildren = separate_plane.vNormal.x < 0 || separate_plane.vNormal.y < 0 || separate_plane.vNormal.z < 0;
	if (swapNodeChildren)
		separate_plane.vNormal = separate_plane.vNormal.invert();

	newThisPlanes[planeCount] = separate_plane;
	replace_lump(LUMP_PLANES, newThisPlanes, (planeCount + 1) * sizeof(BSPPLANE));
	delete[] newThisPlanes;
	{
		if (models[dst_model].iHeadnodes[0] >= 0 || models[src_model].iHeadnodes[0] >= 0)
		{
			int target_node = models[dst_model].iHeadnodes[0] >= 0 && models[src_model].iHeadnodes[0] >= 0 ?
				std::min(models[dst_model].iHeadnodes[0], models[src_model].iHeadnodes[0]) : -1;
			if (target_node == -1)
				target_node = models[dst_model].iHeadnodes[0] >= 0 ? models[dst_model].iHeadnodes[0] : models[src_model].iHeadnodes[0];

			int newnode = create_node(true, target_node);

			BSPNODE32& headNode = nodes[newnode];

			headNode = {
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
				std::swap(headNode.iChildren[0], headNode.iChildren[1]);
			}
			models[dst_model].iHeadnodes[0] = newnode;
		}
	}

	{
		for (int h = 1; h < MAX_MAP_HULLS; h++)
		{
			if (models[dst_model].iHeadnodes[h] >= 0 || models[src_model].iHeadnodes[h] >= 0)
			{
				int target_node = models[dst_model].iHeadnodes[h] >= 0 && models[src_model].iHeadnodes[h] >= 0 ?
					std::min(models[dst_model].iHeadnodes[h], models[src_model].iHeadnodes[h]) : -1;
				if (target_node == -1)
					target_node = models[dst_model].iHeadnodes[h] >= 0 ? models[dst_model].iHeadnodes[h] : models[src_model].iHeadnodes[h];

				int newclip = create_clipnode(true, target_node);

				BSPCLIPNODE32& headNode = clipnodes[newclip];

				headNode = {
					separationPlaneIdx,	// plane idx
					{	// child nodes
						models[src_model].iHeadnodes[h],
						models[dst_model].iHeadnodes[h]
					},
				};

				if (swapNodeChildren)
				{
					std::swap(headNode.iChildren[0], headNode.iChildren[1]);
				}

				models[dst_model].iHeadnodes[h] = newclip;
			}
		}
	}

	models[dst_model].nFaces += newfaces;
	models[dst_model].nVisLeafs += models[src_model].nVisLeafs;

	models[dst_model].nMins = new_min;
	models[dst_model].nMaxs = new_max;

	models[dst_model].vOrigin = models[src_model].vOrigin;

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

	if (modelIdx == 0)
	{
		for (size_t i = 0; i < ents.size(); i++)
		{
			if (ents[i]->isWorldSpawn())
				return (int)i;
		}
	}

	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i]->getBspModelIdx() == modelIdx)
			return (int)i;
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

std::vector<int> Bsp::get_face_edges(int faceIdx)
{
	std::vector<int> out;
	for (int e = faces[faceIdx].iFirstEdge; e < faces[faceIdx].iFirstEdge + faces[faceIdx].nEdges; e++)
	{
		out.push_back(abs(surfedges[e]));
	}
	return out;
}

std::vector<vec3> Bsp::get_face_verts(int faceIdx, int limited)
{
	std::vector<vec3> out;
	for (int e = faces[faceIdx].iFirstEdge; e < faces[faceIdx].iFirstEdge + faces[faceIdx].nEdges; e++)
	{
		int edgeIdx = surfedges[e];
		BSPEDGE32& edge = edges[abs(edgeIdx)];
		vec3 v = edgeIdx > 0 ? verts[edge.iVertex[0]] : verts[edge.iVertex[1]];
		out.push_back(v);
		limited--;
		if (limited == 0)
			break;
	}
	return out;
}

std::vector<int> Bsp::get_face_verts_idx(int faceIdx, int limited)
{
	std::vector<int> out;
	for (int e = faces[faceIdx].iFirstEdge; e < faces[faceIdx].iFirstEdge + faces[faceIdx].nEdges; e++)
	{
		int edgeIdx = surfedges[e];
		BSPEDGE32& edge = edges[abs(edgeIdx)];
		out.push_back(edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1]);
		limited--;
		if (limited == 0)
			break;
	}
	return out;
}

bool Bsp::is_worldspawn_ent(int entIdx)
{
	if (entIdx >= (int)ents.size())
		return true;
	if (ents[entIdx]->hasKey("classname") && ents[entIdx]->keyvalues["classname"] == "worldspawn"
		&& ents[entIdx]->getBspModelIdx() <= 0)
		return true;
	return false;
}

int Bsp::regenerate_clipnodes_from_nodes(int iNode, int hullIdx, bool& success)
{
	if (iNode < 0 || iNode >= nodeCount)
	{
		success = false;
		return -1;
	}

	BSPNODE32& node = nodes[iNode];

	if (node.iPlane < 0 || node.iPlane >= planeCount)
	{
		success = false;
		return -1;
	}

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
				int leafIndex = ~node.iChildren[i];
				if (leafIndex >= leafCount)
				{
					success = false;
					return -1;
				}
				BSPLEAF32& leaf = leaves[leafIndex];
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
			return solidContents;
		}
		return regenerate_clipnodes_from_nodes(solidChild, hullIdx, success);
	}
	default:
		break;
	}

	int newClipnodeIdx = create_clipnode();
	if (newClipnodeIdx < 0 || newClipnodeIdx >= clipnodeCount)
	{
		success = false;
		return -1;
	}
	clipnodes[newClipnodeIdx].iPlane = create_plane();
	success = true;

	int solidChild = -1;
	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			int childIdx = regenerate_clipnodes_from_nodes(node.iChildren[i], hullIdx, success);
			if (childIdx < 0 || childIdx >= clipnodeCount)
			{
				success = false;
				return -1;
			}
			clipnodes[newClipnodeIdx].iChildren[i] = childIdx;
			solidChild = solidChild == -1 ? i : -1;
		}
		else
		{
			int leafIndex = ~node.iChildren[i];
			if (leafIndex >= leafCount)
			{
				success = false;
				return -1;
			}
			BSPLEAF32& leaf = leaves[leafIndex];
			clipnodes[newClipnodeIdx].iChildren[i] = leaf.nContents;
			if (leaf.nContents == CONTENTS_SOLID)
			{
				solidChild = i;
			}
		}
	}

	if (node.iPlane >= planeCount)
	{
		success = false;
		return -1;
	}
	BSPPLANE& nodePlane = planes[node.iPlane];
	if (clipnodes[newClipnodeIdx].iPlane < 0 || clipnodes[newClipnodeIdx].iPlane >= planeCount)
	{
		success = false;
		return -1;
	}
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
		if (clipnodes[newClipnodeIdx].iPlane < 0 || clipnodes[newClipnodeIdx].iPlane >= planeCount)
		{
			success = false;
			return -1;
		}
		BSPPLANE& p = planes[clipnodes[newClipnodeIdx].iPlane];
		vec3 planePoint = p.vNormal * p.fDist;
		vec3 newPlanePoint = planePoint + p.vNormal * (solidChild == 0 ? -extent : extent);
		p.fDist = dotProduct(p.vNormal, newPlanePoint) / dotProduct(p.vNormal, p.vNormal);
	}

	return newClipnodeIdx;
}

bool Bsp::regenerate_clipnodes(int modelIdx, int hullIdx)
{
	BSPMODEL& model = models[modelIdx];
	bool retval = false;

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
				clipnodes[solidNodeIdx].iChildren[k] = regenerate_clipnodes_from_nodes(model.iHeadnodes[0], i, retval);
			}
		}

		// TODO: create clipnodes to "cap" edges that are 90+ degrees (most CSG clip types do this)
		// that will fix broken collision around those edges (invisible solid areas)
	}

	remove_unused_model_structures(CLEAN_CLIPNODES | CLEAN_PLANES);
	return retval;
}

void Bsp::write_csg_outputs(const std::string& path)
{
	BSPPLANE* thisPlanes = (BSPPLANE*)lumps[LUMP_PLANES].data();
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

	numPlanes *= 2;
	bsp_header.lump[LUMP_PLANES].nLength = numPlanes * sizeof(BSPPLANE);
	lumps[LUMP_PLANES].assign((unsigned char*)newPlanes, (unsigned char*)(newPlanes)+bsp_header.lump[LUMP_PLANES].nLength);

	update_lump_pointers();

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

	BSPMODEL* tmodels = (BSPMODEL*)lumps[LUMP_MODELS].data();
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
		bsp_file.write((char*)lumps[i].data(), bsp_header.lump[i].nLength);
	}
	delete[] newPlanes;
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

			fprintf(polyfile, "%i %i %i %i %i\n", detaillevel, iPlane, face.iTextureInfo, faceContents, face.nEdges);

			if (flipped)
			{
				for (int e = (face.iFirstEdge + face.nEdges) - 1; e >= face.iFirstEdge; e--)
				{
					int edgeIdx = surfedges[e];
					BSPEDGE32& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx > 0 ? verts[edge.iVertex[0]] : verts[edge.iVertex[1]];
					fprintf(polyfile, "%5.8f %5.8f %5.8f\n", v.x, v.y, v.z);
				}
			}
			else
			{
				for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
				{
					int edgeIdx = surfedges[e];
					BSPEDGE32& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx > 0 ? verts[edge.iVertex[0]] : verts[edge.iVertex[1]];
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
	print_log(" {} -> {} surfs, Min({}, {}, {}), Max({} {} {})", getLeafContentsName(leaf.nContents), leaf.nMarkSurfaces,
		leaf.nMins[0], leaf.nMins[1], leaf.nMins[2],
		leaf.nMaxs[0], leaf.nMaxs[1], leaf.nMaxs[2]);
}

void Bsp::print_leaf(int leafid)
{
	BSPLEAF32& leaf = leaves[leafid];
	print_log(fmt::format(fmt::runtime(get_localized_string(LANG_0143)), leafid));
	print_log(" {} -> {} surfs, Min({}, {}, {}), Max({} {} {})\n", getLeafContentsName(leaf.nContents), leaf.nMarkSurfaces,
		leaf.nMins[0], leaf.nMins[1], leaf.nMins[2],
		leaf.nMaxs[0], leaf.nMaxs[1], leaf.nMaxs[2]);
}

void Bsp::update_lump_pointers()
{
	planes = (BSPPLANE*)lumps[LUMP_PLANES].data();
	texinfos = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO].data();
	leaves = (BSPLEAF32*)lumps[LUMP_LEAVES].data();
	models = (BSPMODEL*)lumps[LUMP_MODELS].data();
	nodes = (BSPNODE32*)lumps[LUMP_NODES].data();
	clipnodes = (BSPCLIPNODE32*)lumps[LUMP_CLIPNODES].data();
	faces = (BSPFACE32*)lumps[LUMP_FACES].data();
	if (is_bsp30ext && extralumps.size())
		faceinfos = (BSPFACE_INFOEX*)extralumps[LUMP_FACEINFO].data();
	else
		faceinfos = NULL;
	verts = (vec3*)lumps[LUMP_VERTICES].data();
	lightdata = lumps[LUMP_LIGHTING].data();
	surfedges = (int*)lumps[LUMP_SURFEDGES].data();
	edges = (BSPEDGE32*)lumps[LUMP_EDGES].data();
	marksurfs = (int*)lumps[LUMP_MARKSURFACES].data();
	visdata = lumps[LUMP_VISIBILITY].data();
	textures = lumps[LUMP_TEXTURES].data();

	planeCount = bsp_header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	texinfoCount = bsp_header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	leafCount = bsp_header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF32);
	modelCount = bsp_header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	nodeCount = bsp_header.lump[LUMP_NODES].nLength / sizeof(BSPNODE32);
	vertCount = bsp_header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	faceCount = bsp_header.lump[LUMP_FACES].nLength / sizeof(BSPFACE32);
	if (is_bsp30ext && extralumps.size())
		faceinfoCount = bsp_header_ex.lump[LUMP_FACEINFO].nLength / sizeof(BSPFACE_INFOEX);
	else
		faceinfoCount = 0;
	clipnodeCount = bsp_header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE32);
	marksurfCount = bsp_header.lump[LUMP_MARKSURFACES].nLength / sizeof(int);
	surfedgeCount = bsp_header.lump[LUMP_SURFEDGES].nLength / sizeof(int);
	edgeCount = bsp_header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE32);
	if (textures)
		textureCount = *((int*)(textures));
	else
		textureCount = 0;
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

	if (pvsFaceCount != faceCount) {
		pvsFaceCount = faceCount;

		if (pvsFaces)
		{
			delete[] pvsFaces;
		}
		pvsFaces = new bool[pvsFaceCount];
	}
}

void Bsp::replace_lump(int lumpIdx, void* newData, size_t newLength)
{
	bsp_header.lump[lumpIdx].nLength = (int)newLength;
	lumps[lumpIdx].assign((unsigned char*)newData, (unsigned char*)(newData)+bsp_header.lump[lumpIdx].nLength);
	update_lump_pointers();
}

void Bsp::append_lump(int lumpIdx, void* newData, size_t appendLength)
{
	int oldLen = bsp_header.lump[lumpIdx].nLength;
	unsigned char* newLump = new unsigned char[oldLen + appendLength];

	if (oldLen > 0)
		memcpy(newLump, lumps[lumpIdx].data(), oldLen);
	memcpy(newLump + oldLen, newData, appendLength);

	replace_lump(lumpIdx, newLump, oldLen + appendLength);
	delete[] newLump;
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

int Bsp::merge_all_planes()
{
	int merged = 0;


	return merged;
}

struct SMD_Triangle
{
	vec3 verts[3];
	vec3 norm;
	std::string texName;
	float u[3], v[3];
	int boneid;
};

void Bsp::ExportToSmdWIP(const std::string& path, bool split, bool oneRoot)
{
	if (!createDir(path))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0193), path);
		return;
	}

	std::deque<SMD_Triangle> toExport;

	if (!createDir(path + bsp_name + ".smd/"))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0193), path + bsp_name + ".smd/");
		return;
	}

	print_log(get_localized_string(LANG_0194), bsp_name + ".smd", path);

	BspRenderer* bsprend = renderer;

	remove_faces_by_content(CONTENTS_SKY);
	remove_faces_by_content(CONTENTS_SOLID);

	remove_unused_model_structures();

	//int merged = merge_all_verts(0.1f);
	//print_log(PRINT_RED, " Merged {} verts \n", merged);
	remove_unused_model_structures(CLEAN_EDGES_FORCE | CLEAN_TEXINFOS_FORCE);

	save_undo_lightmaps();
	resize_all_lightmaps();

	bsprend->reuploadTextures();
	bsprend->loadLightmaps();

	update_ent_lump();
	update_lump_pointers();

	//g_app->reloading = true;
	//bsprend->reload();
	//g_app->reloading = false;

	if (!createDir(path + bsp_name + ".smd/tex_8bit/"))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0193), path + bsp_name + ".smd/tex_8bit/");
		return;
	}

	int vertoffset = 1;

	std::set<int> refreshedModels;

	std::map<int, int> bonemap;
	int lastboneid = 0;

	for (int i = 0; i < faceCount; i++)
	{
		int mdlid = get_model_from_face(i);
		RenderFace* rface;
		RenderGroup* rgroup;

		if (refreshedModels.find(mdlid) == refreshedModels.end())
		{
			bsprend->refreshModel(mdlid, false, /* do triangulate */ true);
			refreshedModels.insert(mdlid);
		}

		if (!bsprend->getRenderPointers(i, &rface, &rgroup))
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0196));
			continue;
		}

		BSPFACE32& face = faces[i];
		BSPTEXTUREINFO& texinfo = texinfos[face.iTextureInfo];
		int texOffset = ((int*)textures)[texinfo.iMiptex + 1];
		BSPMIPTEX* tex = NULL;

		if (texOffset >= 0)
			tex = ((BSPMIPTEX*)(textures + texOffset));

		std::vector<int> entIds = get_model_ents_ids(mdlid);

		if (entIds.empty())
		{
			entIds.push_back(0);
		}

		if (tex && !fileExists(path + bsp_name + std::string(".smd/tex_8bit/") + tex->szName + std::string(".bmp")))
		{
			if (tex->nOffsets[0] > 0)
			{
				if (texOffset >= 0)
				{
					WADTEX* wadTex = NULL;
					if (!is_texture_has_pal)
					{
						if (g_settings.pal_id >= 0)
						{
							wadTex = new WADTEX(tex, g_settings.palettes[g_settings.pal_id].data,
								(unsigned short)g_settings.palettes[g_settings.pal_id].colors);
						}
						else
						{
							wadTex = new WADTEX(tex, g_settings.palette_default);
						}
					}
					else
					{
						wadTex = new WADTEX(tex);
					}
					int lastMipSize = (wadTex->nWidth >> 3) * (wadTex->nHeight >> 3);
					COLOR3* palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + sizeof(short) - sizeof(BSPMIPTEX));
					unsigned char* src = wadTex->data;

					WriteBMP_PAL(path + bsp_name + std::string(".smd/tex_8bit/") + tex->szName + std::string(".bmp"), (unsigned char*)src, tex->nWidth, tex->nHeight, palette);

					delete wadTex;
				}
			}
			else
			{
				bool foundInWad = false;
				for (size_t r = 0; r < mapRenderers.size() && !foundInWad; r++)
				{
					for (size_t k = 0; k < mapRenderers[r]->wads.size(); k++)
					{
						if (mapRenderers[r]->wads[k]->hasTexture(tex->szName))
						{
							foundInWad = true;

							WADTEX* wadTex = mapRenderers[r]->wads[k]->readTexture(tex->szName);
							int lastMipSize = (wadTex->nWidth >> 3) * (wadTex->nHeight >> 3);
							COLOR3* palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + sizeof(short) - sizeof(BSPMIPTEX));
							unsigned char* src = wadTex->data;

							WriteBMP_PAL(path + bsp_name + std::string(".smd/tex_8bit/") + tex->szName + std::string(".bmp"), (unsigned char*)src, wadTex->nWidth, wadTex->nHeight, palette);

							delete wadTex;
							break;
						}
					}
				}
			}
		}

		if (tex)
		{
			for (size_t e = 0; e < entIds.size(); e++)
			{
				int tmpentid = entIds[e];
				Entity* ent = ents[tmpentid];
				vec3 origin_offset = ent->origin.flip();

				if (bonemap.find(tmpentid) == bonemap.end())
				{
					lastboneid++;
					bonemap[tmpentid] = lastboneid;
				}

				BSPPLANE tmpPlane = getPlaneFromFace(&face);

				for (int v = 0; v < rface->vertCount; v += 3)
				{
					SMD_Triangle tmpTriangle;
					tmpTriangle.texName = std::string(tex->szName) + ".bmp";
					tmpTriangle.norm = tmpPlane.vNormal;
					tmpTriangle.boneid = bonemap[tmpentid];
					for (int n = 0; n < 3; n++)
					{
						lightmapVert& vert = ((lightmapVert*)rgroup->buffer->get_data())[rface->vertOffset + (3 - (n + 1)) + v];

						vec3 org_pos = vert.pos.unflip() + origin_offset;
						vec3 pos = vert.pos.unflip();

						float fU = dotProduct(texinfo.vS, pos) + texinfo.shiftS;
						float fV = dotProduct(texinfo.vT, pos) + texinfo.shiftT;
						fU /= (float)tex->nWidth;
						fV /= -(float)tex->nHeight;


						tmpTriangle.verts[n] = org_pos;
						tmpTriangle.u[n] = fU;
						tmpTriangle.v[n] = fV;
					}
					toExport.push_back(tmpTriangle);
				}
				vertoffset += rface->vertCount;
			}
		}
	}

	print_log(PRINT_BLUE, "Bones {} triangles {}\n", oneRoot ? 1 : lastboneid, toExport.size());

	for (auto m : refreshedModels)
		bsprend->refreshModel(m, false);

	std::ofstream outQC(path + bsp_name + ".smd/" + bsp_name + "_part1.qc");

	int smd_parts = 1;
	int smd_saved_parts = 1;

	int qc_part = 1;

	const int MAX_SMD_VERTS = 2047;
	const int MAX_MDL_MESHES = 63;
	const int MAX_MDL_TEXTURES = 99;
	const int MAX_MDL_BONES = 127;

	std::map<int, int> bones_to;
	int cur_bone_id = 1;

	std::ofstream outSMD_file(path + bsp_name + ".smd/" + bsp_name + "_" + std::to_string(smd_parts) + ".smd");

	std::ostringstream outSMD;
	outSMD << "triangles" << std::endl;

	std::vector<std::pair<vec3, int>> total_verts;
	std::vector<vec3> total_normals;
	std::set<std::string> total_textures;

	g_progress.update("Export smd", (int)(toExport.size()));
	while (toExport.size())
	{
		g_progress.tick();
		auto t = toExport[0];
		toExport.erase(toExport.begin());

		outSMD << t.texName << std::endl;

		if (bones_to.find(t.boneid) == bones_to.end())
		{
			bones_to[t.boneid] = cur_bone_id;
			cur_bone_id++;
		}

		for (int v = 0; v < 3; v++)
		{
			outSMD << (oneRoot ? 0 : bones_to[t.boneid]) << " ";
			outSMD << t.verts[v].toKeyvalueString() << " ";
			outSMD << t.norm.toKeyvalueString() << " ";
			outSMD << std::to_string(t.u[v]) << " ";
			outSMD << std::to_string(t.v[v]) << std::endl;
			bool found = false;
			for (auto& vert : total_verts)
			{
				if (t.verts[v].x == vert.first.x &&
					t.verts[v].y == vert.first.y &&
					t.verts[v].z == vert.first.z && vert.second == bones_to[t.boneid])
				{
					found = true;
				}
			}
			if (!found)
				total_verts.emplace_back(t.verts[v], bones_to[t.boneid]);
		}

		bool found = false;
		for (auto& vert : total_normals)
		{
			if (t.norm.x == vert.x &&
				t.norm.y == vert.y &&
				t.norm.z == vert.z)
			{
				found = true;
			}
		}
		if (!found)
			total_normals.push_back(t.norm);


		if (!total_textures.count(t.texName))
			total_textures.insert(t.texName);


		if (split)
		{
			if (bones_to.size() >= MAX_MDL_BONES ||
				total_verts.size() >= MAX_SMD_VERTS ||
				total_normals.size() >= MAX_SMD_VERTS ||
				total_textures.size() >= MAX_MDL_TEXTURES ||
				std::abs(smd_parts - smd_saved_parts) >= MAX_MDL_MESHES)
			{
				outSMD << "end" << std::endl;

				std::ostringstream outSMD_head;
				outSMD_head << "version 1" << std::endl;
				outSMD_head << "nodes" << std::endl;
				outSMD_head << "0 \"root\" -1" << std::endl;

				for (size_t b = 0; !oneRoot && b < bones_to.size(); b++)
				{
					outSMD_head << (b + 1) << " \"bone" << b << "\" -1" << std::endl;
				}
				outSMD_head << "end" << std::endl;
				outSMD_head << "skeleton" << std::endl;
				outSMD_head << "time 0" << std::endl;
				outSMD_head << "0 0 0 0 0 0 0" << std::endl;
				for (size_t b = 0; !oneRoot && b < bones_to.size(); b++)
				{
					outSMD_head << (b + 1) << " 0 0 0 0 0 0" << std::endl;
				}
				outSMD_head << "end" << std::endl;

				outSMD_file << outSMD_head.str();
				outSMD_file << outSMD.str();
				outSMD_file.flush();
				outSMD_file.close();

				outSMD.str("");
				outSMD.clear();

				outSMD << "triangles" << std::endl;

				//clear smd limits
				total_verts.clear();
				total_normals.clear();

				//small fix for latest triangle
				if (toExport.size())
				{
					smd_parts++;
					outSMD_file = std::ofstream(path + bsp_name + ".smd/" + bsp_name + "_" + std::to_string(smd_parts) + ".smd");

					if (bones_to.size() >= MAX_MDL_BONES ||
						total_textures.size() >= MAX_MDL_TEXTURES ||
						std::abs(smd_parts - smd_saved_parts) >= MAX_MDL_MESHES)
					{
						bones_to.clear();
						cur_bone_id = 1;
						total_textures.clear();
						qc_part++;

						outQC << "$modelname " << bsp_name << "_" << std::to_string(qc_part - 1) << std::endl;
						outQC << "$cd ." << std::endl;
						outQC << "$cdtexture ./tex_8bit" << std::endl;

						for (int i = smd_saved_parts; i < smd_parts; i++)
						{
							outQC << "$body \"" << bsp_name << "\" \"" << (bsp_name + "_" + std::to_string(i)) << "\"" << std::endl;
							smd_saved_parts++;
						}
						outQC.flush();
						outQC.close();

						outQC = std::ofstream(path + bsp_name + ".smd/" + bsp_name + "_part" + std::to_string(qc_part) + ".qc");
					}
				}
				else
				{
					break;
				}
			}
		}
	}

	if (outSMD_file.is_open())
	{
		outSMD << "end" << std::endl;

		std::ostringstream outSMD_head;
		outSMD_head << "version 1" << std::endl;
		outSMD_head << "nodes" << std::endl;
		outSMD_head << "0 \"root\" -1" << std::endl;

		for (size_t b = 0; !oneRoot && b < bones_to.size(); b++)
		{
			outSMD_head << (b + 1) << " \"bone" << b << "\" -1" << std::endl;
		}
		outSMD_head << "end" << std::endl;
		outSMD_head << "skeleton" << std::endl;
		outSMD_head << "time 0" << std::endl;
		outSMD_head << "0 0 0 0 0 0 0" << std::endl;
		for (size_t b = 0; !oneRoot && b < bones_to.size(); b++)
		{
			outSMD_head << (b + 1) << " 0 0 0 0 0 0" << std::endl;
		}
		outSMD_head << "end" << std::endl;

		outSMD_file << outSMD_head.str();
		outSMD_file << outSMD.str();
		outSMD_file.flush();
		outSMD_file.close();
	}

	smd_parts++;
	qc_part++;

	outQC << "$modelname " << bsp_name << "_" << std::to_string(qc_part - 1) << std::endl;
	outQC << "$cd ." << std::endl;
	outQC << "$cdtexture ./tex_8bit" << std::endl;

	for (int i = smd_saved_parts; i < smd_parts; i++)
	{
		outQC << "$body \"" << bsp_name << "\" \"" << (bsp_name + "_" + std::to_string(i)) << "\"" << std::endl;
		smd_saved_parts++;
	}
	outQC.flush();
	outQC.close();

	g_progress.clear();
	g_progress = ProgressMeter();

	update_ent_lump();
	update_lump_pointers();

	remove_unused_model_structures(CLEAN_MODELS);
	resize_all_lightmaps();
	bsprend->reloadTextures();
	bsprend->loadLightmaps();

	update_ent_lump();
	update_lump_pointers();
	renderer->pushUndoState("EXPORT .SMD EDITED", EDIT_MODEL_LUMPS | FL_ENTITIES);
}

void Bsp::ExportToObjWIP(const std::string& path, int iscale, bool lightmapmode, bool with_mdl, bool export_csm, int grouping)
{
	if (!createDir(path))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0193), path);
		return;
	}

	float scale = iscale < 0 ? 1.0f / iscale : 1.0f * iscale;

	if (iscale == 1)
		scale = 1.0f;

	scale = std::fabs(scale);

	print_log(get_localized_string(LANG_0194), bsp_name + ".obj", path);
	print_log(get_localized_string(LANG_0195), iscale == 1 ? "scale" : iscale < 0 ? "downscale" : "upscale", abs(iscale));



	std::string groupname = std::string();

	BspRenderer* bsprend = renderer;

	remove_faces_by_content(CONTENTS_SKY);
	remove_faces_by_content(CONTENTS_SOLID);

	remove_unused_model_structures();

	int merged = merge_all_verts(0.1f);
	print_log(PRINT_RED, " Merged {} verts \n", merged);
	remove_unused_model_structures(CLEAN_EDGES_FORCE | CLEAN_TEXINFOS_FORCE);


	save_undo_lightmaps();
	resize_all_lightmaps();

	bsprend->reuploadTextures();
	bsprend->loadLightmaps();

	//g_app->reloading = true;
	//bsprend->reload();
	//g_app->reloading = false;

	createDir(path + "textures");
	std::vector<std::string> materials;
	std::vector<std::string> matnames;

	int vertoffset = 1;
	int normoffset = 0;

	int materialid = -1;
	int lastmaterialid = -2;

	std::set<int> refreshedModels;

	ProgressMeter tmp = g_progress;


	if (with_mdl)
	{
		int model_count = 0;
		for (size_t ent = 0; ent < ents.size(); ent++)
		{
			if (renderer->renderEnts[ent].mdl)
			{
				model_count++;
			}
		}

		tmp.update("MDL TO BSP...", model_count);
		g_progress = tmp;

		for (size_t ent = 0; ent < ents.size(); ent++)
		{
			if (renderer->renderEnts[ent].mdl)
			{
				import_mdl_to_bspmodel((int)ent, false);

				tmp.tick();
				g_progress = tmp;
			}
		}

		tmp.update("RELOADING MAP...", 8);
		g_progress = tmp;

		tmp.tick();
		g_progress = tmp;
		remove_unused_model_structures();

		tmp.tick();
		g_progress = tmp;
		resize_all_lightmaps();

		tmp.tick();
		g_progress = tmp;
		renderer->reuploadTextures();

		tmp.tick();
		g_progress = tmp;
		renderer->loadLightmaps();

		tmp.tick();
		g_progress = tmp;
		renderer->preRenderFaces();

		tmp.tick();
		g_progress = tmp;
		renderer->pushUndoState("CREATE MDL->BSP MODEL", EDIT_MODEL_LUMPS | FL_ENTITIES);
	}
	else
		renderer->preRenderEnts();

	if (export_csm)
		tmp.update("Export to csm...", faceCount);
	else
		tmp.update("Export to obj...", faceCount);

	g_progress = tmp;

	std::map<std::string, std::stringstream> group_verts;
	std::map<std::string, std::stringstream> group_normals;
	std::map<std::string, std::stringstream> group_textures;
	std::map<std::string, std::stringstream> group_objects;

	std::map<std::string, int> group_vert_groups;

	std::vector<std::string> group_list;

	CSMFile* csm_export = new CSMFile();

	csm_face tmpFace;

	int csm_groups = 0;


	for (int i = 0; i < faceCount; i++)
	{
		tmp.tick();
		g_progress = tmp;


		int mdlid = get_model_from_face(i);
		RenderFace* rface;
		RenderGroup* rgroup;

		if (refreshedModels.find(mdlid) == refreshedModels.end())
		{
			bsprend->refreshModel(mdlid, false, export_csm ? true : false);
			refreshedModels.insert(mdlid);
		}

		if (!bsprend->getRenderPointers(i, &rface, &rgroup))
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0196));
			continue;
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

		materialid = -1;
		for (size_t m = 0; m < matnames.size(); m++)
		{
			if (matnames[m] == tex.szName)
				materialid = (int)m;
		}

		if (materialid == -1)
		{
			materialid = (int)matnames.size();
			matnames.emplace_back(tex.szName);

			if (!export_csm)
			{
				materials.emplace_back("");
				materials.emplace_back("newmtl " + matnames[materialid]);

				materials.emplace_back("Ns 0");
				materials.emplace_back("Ka 1 1 1");
				materials.emplace_back("Ks 0 0 0");
				materials.emplace_back("Ke 0 0 0");
				materials.emplace_back("Ni 1");

				if (toLowerCase(tex.szName) == "aaatrigger" ||
					toLowerCase(tex.szName) == "null" ||
					starts_with(toLowerCase(tex.szName), "sky") ||
					toLowerCase(tex.szName) == "noclip" ||
					toLowerCase(tex.szName) == "clip" ||
					toLowerCase(tex.szName) == "origin" ||
					toLowerCase(tex.szName) == "bevel" ||
					toLowerCase(tex.szName) == "hint" ||
					toLowerCase(tex.szName) == "skip"
					)
				{
					materials.emplace_back("d 0.25");
					materials.emplace_back("illum 1");
				}
				else
				{
					materials.emplace_back("d 1");
					materials.emplace_back("illum 2");
				}

				materials.emplace_back("map_Kd " + std::string("textures/") + tex.szName + std::string(".bmp"));
			}

			if (export_csm)
			{
				csm_export->materials.emplace_back(std::string("textures/") + tex.szName + std::string(".bmp"));
			}
		}

		if (!fileExists(path + std::string("textures/") + tex.szName + std::string(".bmp")))
		{
			if (tex.nOffsets[0] > 0)
			{
				if (texOffset >= 0)
				{
					int colorCount = 256;
					COLOR3 palette[256];
					if (g_settings.pal_id >= 0)
					{
						colorCount = g_settings.palettes[g_settings.pal_id].colors;
						memcpy(palette, g_settings.palettes[g_settings.pal_id].data, g_settings.palettes[g_settings.pal_id].colors * sizeof(COLOR3));
					}
					else
					{
						colorCount = 256;
						memcpy(palette, g_settings.palette_default,
							256 * sizeof(COLOR3));
					}

					COLOR3* imageData = ConvertMipTexToRGB(((BSPMIPTEX*)(textures + texOffset)), is_texture_with_pal(texinfo.iMiptex) ? NULL : palette);

					for (int k = 0; k < tex.nHeight * tex.nWidth; k++)
					{
						std::swap(imageData[k].b, imageData[k].r);
					}

					WriteBMP_RGB(path + std::string("textures/") + tex.szName + std::string(".bmp"), (unsigned char*)imageData, tex.nWidth, tex.nHeight);

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
							int lastMipSize = (wadTex->nWidth >> 3) * (wadTex->nHeight >> 3);
							COLOR3* palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + sizeof(short) - sizeof(BSPMIPTEX));
							unsigned char* src = wadTex->data;
							COLOR3* imageData = new COLOR3[wadTex->nWidth * wadTex->nHeight];

							int sz = wadTex->nWidth * wadTex->nHeight;

							for (int m = 0; m < sz; m++)
							{
								imageData[m] = palette[src[m]];
								std::swap(imageData[m].b, imageData[m].r);
							}

							WriteBMP_RGB(path + std::string("textures/") + tex.szName + std::string(".bmp"), (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight);

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
			int tmpentid = entIds[e];

			Entity* ent = ents[tmpentid];

			RenderEnt* rendEnt = &renderer->renderEnts[tmpentid];

			vec3 origin_offset = ent->origin.flip();

			std::string next_group_name = "M_" + std::to_string(mdlid) + "_ENT_" + std::to_string(tmpentid);

			if (next_group_name != groupname)
			{
				groupname = std::move(next_group_name);

				if (std::find(group_list.begin(), group_list.end(), groupname) == group_list.end())
					group_list.push_back(groupname);

				csm_groups++;
			}

			mat4x4 angle_mat;
			angle_mat.loadIdentity();

			if (rendEnt->needAngles)
			{
				vec3 angles = rendEnt->angles;

				angles.z = -angles.x;
				angles.x = angles.z;

				renderer->setRenderAngles(ent->classname, angle_mat, angles);
			}


			BSPPLANE tmpPlane = getPlaneFromFace(&face);
			vec3 org_norm = tmpPlane.vNormal;

			if (rendEnt->needAngles)
			{
				org_norm = (angle_mat * vec4(org_norm, 1.0)).xyz();
			}

			org_norm = org_norm.flip();

			if (!export_csm)
			{
				for (int n = rface->vertCount - 1; n >= 0; n--)
				{
					lightmapVert& vert = ((lightmapVert*)rgroup->buffer->get_data())[rface->vertOffset + n];

					vec3 org_pos = vert.pos;

					if (rendEnt->needAngles)
					{
						org_pos = (angle_mat * vec4(org_pos, 1.0)).xyz();
					}

					org_pos += origin_offset;

					org_pos *= scale;

					group_verts[groupname] << "v " << org_pos.toKeyvalueString() << "\n";
				}

				group_normals[groupname] << "vn " << org_norm.toKeyvalueString() << "\n";

				normoffset++;
			}

			int uv_idx = 0;

			for (int n = rface->vertCount - 1; n >= 0; n--)
			{
				lightmapVert& vert = ((lightmapVert*)rgroup->buffer->get_data())[rface->vertOffset + n];

				vec3 org_pos = vert.pos;

				if (rendEnt->needAngles)
				{
					org_pos = (angle_mat * vec4(org_pos, 1.0)).xyz();
				}

				org_pos = org_pos.flipUV();

				float fU = dotProduct(texinfo.vS, org_pos) + texinfo.shiftS;
				float fV = dotProduct(texinfo.vT, org_pos) + texinfo.shiftT;

				fU /= (float)tex.nWidth;
				fV /= -(float)tex.nHeight;

				if (!export_csm)
				{
					group_textures[groupname] << "vt " << flt_to_str(fU) << " " << flt_to_str(fV) << "\n";
				}

				if (export_csm)
				{
					if (uv_idx == 0)
					{
						unsigned int startvert = (unsigned int)csm_export->vertices.size();

						tmpFace.edgeFlags = 0;
						tmpFace.lmGroup = csm_groups;
						tmpFace.matIdx = (unsigned short)(materialid);
						tmpFace.vertIdx[2] = startvert;
						tmpFace.vertIdx[1] = startvert + 1;
						tmpFace.vertIdx[0] = startvert + 2;

						csm_export->faces.push_back(tmpFace);

						csm_export->header.lmGroups = csm_groups;
					}


					org_pos.unflipUV();

					org_pos += origin_offset;
					org_pos *= scale;

					COLOR4 rndColor;
					srand(csm_groups);
					rndColor.r = 50 + rand() % 206;
					rndColor.g = 50 + rand() % 206;
					rndColor.b = 50 + rand() % 206;
					rndColor.a = 255;

					csm_export->vertices.emplace_back(org_pos, org_norm, rndColor);

					int cur_faceIdx = (int)(csm_export->faces.size()) - 1;
					if (cur_faceIdx >= 0)
					{
						csm_export->faces[cur_faceIdx].uvs[0].uv[uv_idx].x = fU;
						csm_export->faces[cur_faceIdx].uvs[0].uv[uv_idx].y = fU;

						csm_export->faces[cur_faceIdx].uvs[1].uv[uv_idx].x = fU;
						csm_export->faces[cur_faceIdx].uvs[1].uv[uv_idx].y = fU;
					}
					uv_idx++;

					if (uv_idx == 3)
						uv_idx = 0;
				}
			}

			if (lastmaterialid != materialid)
			{
				group_vert_groups[groupname]++;
				if (grouping == 1)
				{
					group_objects[groupname] << "g " << groupname << "_f" << group_vert_groups[groupname] << "\n";
				}
				else if (grouping == 2)
				{
					group_objects[groupname] << "o " << groupname << "_f" << group_vert_groups[groupname] << "\n";
				}
				if (!export_csm)
				{
					if (materialid >= 0)
					{
						group_objects[groupname] << "usemtl " << matnames[materialid] << "\n";
					}
				}
			}

			lastmaterialid = materialid;

			if (!export_csm)
			{
				group_objects[groupname] << "f";

				for (int n = 0; n < rface->vertCount; n++)
				{
					int id = vertoffset + n;

					group_objects[groupname] << " " << id << "/" << id << "/" << normoffset;
				}

				group_objects[groupname] << "\n";
			}

			vertoffset += rface->vertCount;
		}
	}

	if (!export_csm)
	{
		std::ofstream obj_file(path + bsp_name + ".obj", std::ios::binary);
		if (obj_file)
		{
			obj_file << "# Exported using bspguy!\n";
			obj_file << "mtllib " << bsp_name << ".mtl\n";

			for (auto& group : group_list)
			{
				if (grouping == 0 || grouping == 1)
					obj_file << "o " << group << "\n";
				else if (grouping == 2)
					obj_file << "g " << group << "\n";

				obj_file << group_verts[group].str();

				obj_file << group_normals[group].str();

				obj_file << group_textures[group].str();

				obj_file << group_objects[group].str();
			}

			obj_file.flush();
			obj_file.close();
		}


		std::ofstream mat_file(path + bsp_name + ".mtl", std::ios::binary);

		if (mat_file)
		{
			mat_file << "# Exported using bspguy!\n";

			for (auto const& s : materials)
			{
				mat_file << s << '\n';
			}

			mat_file.flush();
			mat_file.close();
		}
	}
	else
	{
		csm_export->write(path + bsp_name + ".csm");

		print_log(PRINT_GREEN, "VALIDATE START\n");
		csm_export->validate();
		print_log(PRINT_GREEN, "VALIDATE END\n");

		/*
		csm_export->read(path + bsp_name + ".csm");
		csm_export->validate();*/
	}
	delete csm_export;

	renderer->undo();


	for (auto m : refreshedModels)
		bsprend->refreshModel(m, false);
}

void placePointsToPlane(std::vector<vec3>& points, const BSPPLANE& plane)
{
	for (auto& point : points)
	{
		double distance = (point.x * plane.vNormal.x + point.y * plane.vNormal.y + point.z * plane.vNormal.z - plane.fDist);
		if (std::fabs(distance) > EPSILON2)
		{
			point.x -= (float)(distance * plane.vNormal.x);
			point.y -= (float)(distance * plane.vNormal.y);
			point.z -= (float)(distance * plane.vNormal.z);
		}
	}
}


bool isPointsToPlane(const std::vector<vec3>& points, const BSPPLANE& plane)
{
	for (auto& point : points)
	{
		double distance = (point.x * plane.vNormal.x + point.y * plane.vNormal.y + point.z * plane.vNormal.z - plane.fDist);
		if (std::fabs(distance) > mON_EPSILON)
		{
			return false;
		}
	}

	return true;
}


vec3 findCenter(const std::vector<vec3>& points) {
	vec3 center = { 0, 0, 0 };

	if (points.size() > 0) {
		for (const auto& point : points) {
			center.x += point.x;
			center.y += point.y;
			center.z += point.z;
		}
		center.x /= points.size();
		center.y /= points.size();
		center.z /= points.size();
	}
	return center;
}

vec3 findClosestEdgePoint(const std::vector<vec3>& points)
{
	float minDistance = FLT_MAX;
	vec3 closestPoint1, closestPoint2;

	for (size_t i = 0; i < points.size(); ++i)
	{
		for (size_t j = 0; j < points.size(); ++j)
		{
			if (j != i)
			{
				float dist = points[i].dist(points[j]);
				if (dist < minDistance) {
					minDistance = dist;
					closestPoint1 = points[i];
					closestPoint2 = points[j];
				}
			}
		}
	}

	vec3 faceCenterPoint = getCentroid(points);

	vec3 finalCenter = getCentroid({ faceCenterPoint, closestPoint1, closestPoint2 });

	return finalCenter;
}


struct MapBrush
{
	Winding wind;
	BSPTEXTUREINFO texInfo;
	BSPPLANE plane;
	std::vector<int> contents;
	int mdlIdx;
	BSPMIPTEX tex;
	BSPMIPTEX back_tex;
	std::vector<Winding> backWinds;
	float back_dist;
	bool back_empty;
	bool skip_merge;
};

std::string GenerateCuboid(float x1, float y1, float z1, float x2, float y2, float z2, std::string texture = "SKY")
{
	std::stringstream outcuboid;

	outcuboid << "{" << std::endl;
	outcuboid << "( " << x2 << " " << y1 << " " << z2 << " ) ( " << x2 << " " << y1 << " " << z1 << " ) ( " << x2 << " " << y2 << " " << z2 << " ) " << texture << " [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1" << std::endl;
	outcuboid << "( " << x1 << " " << y2 << " " << z2 << " ) ( " << x1 << " " << y2 << " " << z1 << " ) ( " << x1 << " " << y1 << " " << z2 << " ) " << texture << " [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1" << std::endl;
	outcuboid << "( " << x2 << " " << y2 << " " << z2 << " ) ( " << x2 << " " << y2 << " " << z1 << " ) ( " << x1 << " " << y2 << " " << z2 << " ) " << texture << " [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1" << std::endl;
	outcuboid << "( " << x1 << " " << y1 << " " << z2 << " ) ( " << x1 << " " << y1 << " " << z1 << " ) ( " << x2 << " " << y1 << " " << z2 << " ) " << texture << " [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1" << std::endl;
	outcuboid << "( " << x1 << " " << y1 << " " << z1 << " ) ( " << x1 << " " << y2 << " " << z1 << " ) ( " << x2 << " " << y1 << " " << z1 << " ) " << texture << " [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1" << std::endl;
	outcuboid << "( " << x2 << " " << y2 << " " << z2 << " ) ( " << x1 << " " << y2 << " " << z2 << " ) ( " << x2 << " " << y1 << " " << z2 << " ) " << texture << " [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1" << std::endl;
	outcuboid << "}";

	return outcuboid.str();
}

void Bsp::ExportToMapWIP(const std::string& path, bool selected, bool merge_faces, bool use_one_back_vert, bool create_worldspawnbox)
{
	if (!createDir(path))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1039), path);
		return;
	}

	removeFile(bsp_name + ".map");
	removeFile(bsp_name + ".jmf");

	std::ofstream map_file(path + "/" + bsp_name + ".map");
	JackWriter jack_file(path + "/" + bsp_name + ".jmf");

	print_log(get_localized_string(LANG_1040), (bsp_name + ".map"), path);
	print_log(get_localized_string(LANG_1040), (bsp_name + ".jmf"), path);

	if (map_file && jack_file.is_open())
	{
		BspRenderer* bsprend = renderer;

		if (!selected && leafCount > 2 && marksurfCount > 2)
		{
			remove_faces_by_content(CONTENTS_SKY);
			remove_faces_by_content(CONTENTS_SOLID);


			save_undo_lightmaps();
			resize_all_lightmaps();

			remove_unused_model_structures();
		}

		int merged = merge_all_verts(0.1f);
		print_log(PRINT_RED, " Merged {} verts \n", merged);
		remove_unused_model_structures(CLEAN_EDGES_FORCE | CLEAN_TEXINFOS_FORCE);

		resize_all_lightmaps();
		bsprend->reuploadTextures();
		bsprend->loadLightmaps();

		update_ent_lump();
		update_lump_pointers();

		std::string groupname = std::string();
		std::set<int> decompiledEnts;
		std::stringstream emptystr{};

		std::map<Entity*, std::stringstream> map_text_data{};

		std::map<Entity*, std::deque<MapBrush>> jack_mesh_data{};

		vec3 w_mins = vec3(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
		vec3 w_maxs = vec3(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);


		if (!selected)
		{
			get_bounding_box(w_mins, w_maxs);
		}
		else
		{
			for (auto& f : g_app->pickInfo.selectedFaces)
			{
				auto faceverts = get_face_verts(f);
				for (auto& v : faceverts)
				{
					expandBoundingBox(v, w_mins, w_maxs);
				}
			}
		}


		w_mins -= 16.0f;
		w_maxs += 16.0f;

		w_mins.snap(1.0f);
		w_maxs.snap(1.0f);

		// MAGIC NUMBER
		jack_file.write<int>('FMHJ');


		// VERSION
		jack_file.write<int>(122);
		// EXPORT PATHES
		jack_file.write<int>(1);
		jack_file.writeLenStr(path + "/" + bsp_name + ".map");
		// BACKGROUND IMAGES
		for (int img = 0; img < 3; img++)
		{
			jack_file.writeLenStr("");
			jack_file.write<double>(1.0);
			jack_file.write<int>(255);
			jack_file.write<int>(1);
			jack_file.write<int>(0);
			jack_file.write<int>(0);
			jack_file.write<int>(0);
			jack_file.write<int>(0);
		}
		// GROUP
		jack_file.write<int>(1);
		jack_file.write<int>(0);
		jack_file.write<int>(0);
		jack_file.write<int>(0);
		jack_file.write<int>(0);
		jack_file.write(COLOR4(255, 255, 255, 255));
		// VIS GROUPS
		jack_file.write<int>(1);
		jack_file.writeLenStr("Default");
		jack_file.write<int>(0);
		jack_file.write(COLOR4(255, 255, 255, 255));
		jack_file.write<unsigned char>(1);
		// GONDON MINS/MAX
		jack_file.write(vec3(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord));
		jack_file.write(vec3(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord));
		// CAMERAS
		jack_file.write<int>(0);
		// PATH OBJECTS
		jack_file.write<int>(0);

		update_lump_pointers();


		int newModelIdx = create_model();

		int null_tex_id = -1;
		if (!find_embedded_texture("NULL", null_tex_id) && !find_embedded_wad_texture("NULL", null_tex_id))
		{
			null_tex_id = add_texture("NULL", NULL, 64, 64);
		}

		int null_tex_offset = ((int*)textures)[null_tex_id + 1];
		BSPMIPTEX null_tex = BSPMIPTEX();
		null_tex.nWidth = null_tex.nHeight = 64;
		strcpy(null_tex.szName, "NULL");
		if (null_tex_offset >= 0)
			null_tex = *((BSPMIPTEX*)(textures + null_tex_offset));

		create_inside_box(w_mins, w_maxs, &models[newModelIdx], null_tex_id);

		update_lump_pointers();



		std::set<int> decompiled_faces;

		std::deque<int> faceList;

		if (selected)
		{
			for (auto& f : g_app->pickInfo.selectedFaces)
				faceList.push_back(f);
		}
		else
		{
			faceList.resize(faceCount);
			std::iota(faceList.begin(), faceList.end(), 0);
		}


		std::deque<int> mergedFaces(modelCount);


		int bad_tries = 0;

		g_progress.update("Preparing map brushes...", (int)faceList.size());

		std::deque<MapBrush> toExport;

		for (size_t f = 0; f < faceList.size(); f++)
		{
			g_progress.tick();
			int i = faceList[f];

			int mdlid = get_model_from_face(i);

			if (decompiled_faces.find(i) != decompiled_faces.end())
			{
				continue;
			}
			else
			{
				decompiled_faces.insert(i);
			}

			BSPFACE32 face = faces[i];

			BSPTEXTUREINFO texinfo = texinfos[face.iTextureInfo];

			int texOffset = ((int*)textures)[texinfo.iMiptex + 1];
			BSPMIPTEX tex = BSPMIPTEX();
			strcpy(tex.szName, "AAATRIGGER");
			if (texOffset >= 0)
				tex = *((BSPMIPTEX*)(textures + texOffset));

			std::vector<int> faceContents = getFaceContents(i);

			BSPPLANE tmpPlane = getPlaneFromFace(&face);

			std::vector<vec3> points = get_face_verts(i);
			placePointsToPlane(points, tmpPlane);

			Winding winding(points);

			//auto tmp_points = getSortedPlanarVerts(points);
			//if (tmp_points.size() < 3)
			//{
			//	print_log(PRINT_RED, "BSPDECOMPILER: Found non convex face!\n");
			//	tmp_points = convexHull(points);
			//	tmp_points = getSortedPlanarVerts(tmp_points);
			//	if (tmp_points.size() < 3)
			//	{
			//		print_log(PRINT_RED, "BSPDECOMPILER: Can not be fixed!\n");
			//	}
			//	else
			//	{
			//		print_log(PRINT_GREEN, "BSPDECOMPILER: Make convexHull!\n");
			//		points = tmp_points;
			//		std::reverse(points.begin(), points.end());
			//	}
			//}
			//else
			//{
			//	points = tmp_points;
			//	std::reverse(points.begin(), points.end());
			//}

			MapBrush tmpBrush;
			tmpBrush.contents = std::move(faceContents);
			tmpBrush.plane = tmpPlane;
			tmpBrush.texInfo = texinfo;
			tmpBrush.mdlIdx = mdlid;
			tmpBrush.tex = tex;
			tmpBrush.wind = winding;
			tmpBrush.backWinds = std::vector<Winding>();
			tmpBrush.back_dist = 0.5f;
			tmpBrush.skip_merge = false;
			toExport.push_back(tmpBrush);
		}


		bool one_brush_merged = true;
		int pass_num = 0;

		while (merge_faces && one_brush_merged)
		{
			one_brush_merged = false;

			std::set<size_t> decompiledBrushes;
			pass_num++;

			g_progress.update(fmt::format("Merge faces pass {}", pass_num), (int)toExport.size());

			for (size_t b1 = 0; b1 < toExport.size(); b1++)
			{
				g_progress.tick();
				if (decompiledBrushes.count(b1))
					continue;

				decompiledBrushes.insert(b1);

				auto& brush = toExport[b1];

				if (brush.wind.m_Points.size() == 0 || brush.skip_merge)
					continue;

				bool findSomethingMerge = false;

				int mergeStages = 2;

				for (int stage = 0; stage < mergeStages; stage++)
				{
					for (size_t b2 = 0; b2 < toExport.size(); b2++)
					{
						if (decompiledBrushes.count(b2))
							continue;


						auto& brush2 = toExport[b2];

						if (brush2.wind.m_Points.size() == 0)
							continue;
						// check for has same parameters

						if ((brush.plane != brush2.plane) ||
							(brush.mdlIdx != brush2.mdlIdx) ||
							(brush.texInfo != brush2.texInfo))
						{
							continue;
						}

						// check for has same contents
						if (brush.contents.size() && brush2.contents.size())
						{
							bool same_contents = false;
							for (int c1 : brush.contents)
							{
								if (std::find(brush2.contents.begin(), brush2.contents.end(), c1) != brush2.contents.end())
								{
									same_contents = true;
									break;
								}
							}
							if (!same_contents)
								continue;
						}

						int connected_edges = 0;

						for (auto& v1 : brush.wind.m_Points)
						{
							if (std::find(brush2.wind.m_Points.begin(), brush2.wind.m_Points.end(), v1) != brush2.wind.m_Points.end())
							{
								connected_edges++;
								findSomethingMerge = true;
							}
						}

						if (stage == 0 || findSomethingMerge)
						{
							Winding wind1(brush.wind);
							Winding wind2(brush2.wind);

							Winding* tryMergeWinding = wind1.Merge(wind2, brush.plane);

							if (tryMergeWinding)
							{
								if (tryMergeWinding->m_Points.size() >= 3)
								{
									brush.wind = Winding(*tryMergeWinding);
									mergedFaces[brush.mdlIdx]++;
									decompiledBrushes.insert(b2);
									one_brush_merged = true;
									brush2.wind = Winding(0);
									delete tryMergeWinding;
									break;
								}
								else
								{
									print_log(PRINT_RED, "ERROR [2] REVERT BACK[ONE BRUSH CAN BE BROKEN!]!\n");
									bad_tries++;
									delete tryMergeWinding;
								}
							}
						}
					}
					if (!findSomethingMerge)
						break;
				}

				if (!findSomethingMerge)
				{
					brush.skip_merge = true;
				}
			}
		}

		if (bad_tries > 0)
		{
			print_log(PRINT_RED, "Bad merge brush tried : {} \n", bad_tries);
		}

		for (int i = 0; i < modelCount; i++)
		{
			if (mergedFaces[i] > 0)
			{
				print_log(PRINT_GREEN, "Merged {} faces for {} model!\n", mergedFaces[i], i);
			}
		}

		g_progress.update("Generate back verts...", (int)toExport.size());

		// Genereate backWinds using bruteforce
		for (auto& brush : toExport)
		{
			g_progress.tick();
			if (brush.wind.m_Points.size() == 0)
			{
				continue;
			}
			if (brush.wind.m_Points.size() < 3)
			{
				brush.wind.m_Points.clear();
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0045));
				continue;
			}

			if (!use_one_back_vert)
			{
				Winding back_wind = brush.wind;
				back_wind.m_Points.resize(3);
				std::reverse(back_wind.m_Points.begin(), back_wind.m_Points.end());

				if (brush.plane.vNormal.x > 1.0f - EPSILON)
				{
					brush.plane.vNormal.x = 1.0f;
					brush.plane.vNormal.y = brush.plane.vNormal.z = 0.0f;
				}

				if (brush.plane.vNormal.y > 1.0f - EPSILON)
				{
					brush.plane.vNormal.y = 1.0f;
					brush.plane.vNormal.z = brush.plane.vNormal.x = 0.0f;
				}

				if (brush.plane.vNormal.z > 1.0f - EPSILON)
				{
					brush.plane.vNormal.z = 1.0f;
					brush.plane.vNormal.x = brush.plane.vNormal.y = 0.0f;
				}

				if (brush.plane.vNormal.x < -1.0f + EPSILON)
				{
					brush.plane.vNormal.x = -1.0f;
					brush.plane.vNormal.y = brush.plane.vNormal.z = 0.0f;
				}

				if (brush.plane.vNormal.y < -1.0f + EPSILON)
				{
					brush.plane.vNormal.y = -1.0f;
					brush.plane.vNormal.z = brush.plane.vNormal.x = 0.0f;
				}

				if (brush.plane.vNormal.z < -1.0f + EPSILON)
				{
					brush.plane.vNormal.z = -1.0f;
					brush.plane.vNormal.x = brush.plane.vNormal.y = 0.0f;
				}

				float normal_offset = 0.01f;
				vec3 normal = brush.plane.vNormal.normalize() * normal_offset;

				while (normal.length() < ON_EPSILON * 5.0f)
				{
					normal_offset += 0.01f;
					normal = brush.plane.vNormal.normalize() * normal_offset;
				}

				for (auto& v : back_wind.m_Points)
				{
					v -= normal;
				}

				brush.backWinds.push_back(back_wind);

				for (size_t n = 0; n < brush.wind.m_Points.size(); n++)
				{
					vec3 v1_b = brush.wind.m_Points[n];
					vec3 v2_b = brush.wind.m_Points[(n + 1) % brush.wind.m_Points.size()];
					vec3 v3_b = v2_b - normal;
					Winding tmpWind{};
					tmpWind.m_Points = { v3_b, v2_b, v1_b };

					brush.backWinds.push_back(tmpWind);
				}

				continue;
			}

			vec3 centoid_real = getCentroid(brush.wind.m_Points);
			vec3 centoid_smaller = findClosestEdgePoint(brush.wind.m_Points);

			vec3 back_vert1 = centoid_smaller - brush.plane.vNormal.normalize() * brush.back_dist;
			vec3 back_vert2 = centoid_real - brush.plane.vNormal.normalize() * brush.back_dist;

			brush.back_dist = 0.5f;

			BSPPLANE plane{};

			vec3 test_vert = vec3();

			for (int i = 0; i < 20; i++)
			{
				std::vector<BSPPLANE> planesForTest;

				// Add front plane
				brush.wind.getPlane(plane);
				planesForTest.push_back(plane);

				bool foundCoplanar = false;

				back_vert1 = centoid_smaller - brush.plane.vNormal.normalize() * brush.back_dist;
				back_vert2 = centoid_real - brush.plane.vNormal.normalize() * brush.back_dist;

				for (size_t n = 0; n < brush.wind.m_Points.size(); n++)
				{
					vec3 v1_b = brush.wind.m_Points[n];
					vec3 v2_b = brush.wind.m_Points[(n + 1) % brush.wind.m_Points.size()];

					vec3 v3_b = back_vert1;
					test_vert = v3_b;

					Winding tmpWind{};
					tmpWind.m_Points = { v2_b, v1_b, v3_b };
					tmpWind.getPlane(plane);

					/*if (tmpWind.IsTiny())
					{
						print_log(PRINT_RED, "TINY2\n");
					}*/

					for (auto& p : planesForTest)
					{
						if (p.vNormal.equal(plane.vNormal, ON_EPSILON))
						{
							foundCoplanar = true;
							break;
						}
					}

					if (foundCoplanar)
						break;
					planesForTest.push_back(plane);
					brush.backWinds.push_back(tmpWind);
				}

				if (foundCoplanar)
				{
					foundCoplanar = false;
					planesForTest.clear();
					brush.backWinds.clear();

					// Add front plane
					brush.wind.getPlane(plane);
					planesForTest.push_back(plane);

					for (size_t n = 0; n < brush.wind.m_Points.size(); n++)
					{
						vec3 v1_b = brush.wind.m_Points[n];
						vec3 v2_b = brush.wind.m_Points[(n + 1) % brush.wind.m_Points.size()];

						vec3 v3_b = back_vert2;

						test_vert = v3_b;

						Winding tmpWind{};
						tmpWind.m_Points = { v2_b, v1_b, v3_b };
						tmpWind.getPlane(plane);

						for (auto& p : planesForTest)
						{
							if (p.vNormal.equal(plane.vNormal, ON_EPSILON))
							{
								foundCoplanar = true;
								break;
							}
						}

						if (foundCoplanar && i + 1 != 20)
						{
							break;
						}

						planesForTest.push_back(plane);
						brush.backWinds.push_back(tmpWind);
					}

					if (foundCoplanar && i + 1 != 20)
					{
						brush.backWinds.clear();
						brush.back_dist += 1.0f;
						//print_log("Found colplanar faces {} for {}\n", i, brush.mdlIdx);
					}
					else if (i + 1 == 20)
					{
						/*brush.backWinds.clear();
						brush.back_dist = 5.0f;
						back_vert2 = centoid_real - brush.plane.vNormal.normalize() * brush.back_dist;
						for (size_t n = 0; n < brush.wind.m_Points.size(); n++)
						{
							vec3 v1_b = brush.wind.m_Points[n];
							vec3 v2_b = brush.wind.m_Points[(n + 1) % brush.wind.m_Points.size()];
							vec3 v3_b = back_vert2;

							test_vert = v3_b;

							Winding tmpWind{};
							tmpWind.m_Points = { v2_b, v1_b, v3_b };
							tmpWind.getPlane(plane);

							brush.backWinds.push_back(tmpWind);
						}
						break;*/
						Winding back_wind = brush.wind;
						back_wind.m_Points.resize(3);
						std::reverse(back_wind.m_Points.begin(), back_wind.m_Points.end());

						if (brush.plane.vNormal.x > 1.0f - EPSILON)
						{
							brush.plane.vNormal.x = 1.0f;
							brush.plane.vNormal.y = brush.plane.vNormal.z = 0.0f;
						}

						if (brush.plane.vNormal.y > 1.0f - EPSILON)
						{
							brush.plane.vNormal.y = 1.0f;
							brush.plane.vNormal.z = brush.plane.vNormal.x = 0.0f;
						}

						if (brush.plane.vNormal.z > 1.0f - EPSILON)
						{
							brush.plane.vNormal.z = 1.0f;
							brush.plane.vNormal.x = brush.plane.vNormal.y = 0.0f;
						}

						if (brush.plane.vNormal.x < -1.0f + EPSILON)
						{
							brush.plane.vNormal.x = -1.0f;
							brush.plane.vNormal.y = brush.plane.vNormal.z = 0.0f;
						}

						if (brush.plane.vNormal.y < -1.0f + EPSILON)
						{
							brush.plane.vNormal.y = -1.0f;
							brush.plane.vNormal.z = brush.plane.vNormal.x = 0.0f;
						}

						if (brush.plane.vNormal.z < -1.0f + EPSILON)
						{
							brush.plane.vNormal.z = -1.0f;
							brush.plane.vNormal.x = brush.plane.vNormal.y = 0.0f;
						}

						float normal_offset = 0.01f;
						vec3 normal = brush.plane.vNormal.normalize() * normal_offset;

						while (normal.length() < ON_EPSILON * 5.0f)
						{
							normal_offset += 0.01f;
							normal = brush.plane.vNormal.normalize() * normal_offset;
						}

						for (auto& v : back_wind.m_Points)
						{
							v -= normal;
						}

						brush.backWinds.push_back(back_wind);

						for (size_t n = 0; n < brush.wind.m_Points.size(); n++)
						{
							vec3 v1_b = brush.wind.m_Points[n];
							vec3 v2_b = brush.wind.m_Points[(n + 1) % brush.wind.m_Points.size()];
							vec3 v3_b = v2_b - normal;
							Winding tmpWind{};
							tmpWind.m_Points = { v3_b, v2_b, v1_b };

							brush.backWinds.push_back(tmpWind);
						}
						break;
					}
					else
					{
						//todo check no edge
						break;
					}
				}
				else
				{
					//todo check no edge
					break;
				}
			}


			bool back_face_is_empty = !selected;

			if (back_face_is_empty)
			{
				std::vector<int> nodeBranch;
				int leafIdx;
				int childIdx = -1;
				int headNode = models[0].iHeadnodes[0];
				int contents = pointContents(headNode, test_vert, 0, nodeBranch, leafIdx, childIdx);

				if (contents != CONTENTS_SOLID && leafIdx != 0)
				{
					back_face_is_empty = false;
				}
			}
			brush.back_empty = back_face_is_empty;
		}

		std::map<int, int> brushNumbers;


		for (int ent = 0; ent < (int)ents.size(); ent++)
		{
			brushNumbers[ent] = 0;
			jack_mesh_data[ents[ent]] = std::deque<MapBrush>();
		}

		Entity* worldEnt = getWorldspawnEnt();
		if (worldEnt)
		{
			worldEnt->setOrAddKeyvalue("mapversion", "220");
		}

		g_progress.update("Export to .map...", (int)toExport.size());

		for (auto& brush : toExport)
		{
			g_progress.tick();
			if (brush.wind.m_Points.size() == 0)
			{
				continue;
			}

			if (brush.wind.m_Points.size() < 3)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0045));
				continue;
			}

			std::vector<int> entIds = get_model_ents_ids(brush.mdlIdx == newModelIdx ? 0 : brush.mdlIdx);

			if (entIds.empty() || selected)
			{
				if (selected)
				{
					entIds.clear();
				}

				for (size_t ent = 0; ent < ents.size(); ent++)
				{
					Entity* entity = ents[ent];
					if (entity->isWorldSpawn())
					{
						entIds.push_back((int)ent);
						if (selected)
							break;
					}
				}

				if (entIds.empty())
				{
					entIds.push_back(0);
				}
			}

			int mdlid = brush.mdlIdx;

			for (size_t e = 0; e < entIds.size(); e++)
			{
				MapBrush tempBrush = brush;

				BSPTEXTUREINFO& texinfo = tempBrush.texInfo;

				int texOffset = ((int*)textures)[texinfo.iMiptex + 1];
				BSPMIPTEX tex = BSPMIPTEX();
				strcpy(tex.szName, "AAATRIGGER");
				if (texOffset >= 0)
					tex = *((BSPMIPTEX*)(textures + texOffset));


				int tmpentid = entIds[e];
				bool newDecompile = false;

				if (decompiledEnts.find(tmpentid) == decompiledEnts.end())
				{
					newDecompile = true;
					decompiledEnts.insert(tmpentid);
				}

				Entity* ent = ents[tmpentid];


				vec3 offset = ent->origin;

				if (!offset.IsZero())
				{
					move_texinfo(texinfo, offset);
				}
				else
				{
					offset = vec3();
				}

				if (selected)
				{
					mdlid = 0;
					ent->setOrAddKeyvalue("mapversion", "220");
				}

				if (newDecompile)
				{
					map_text_data[ent] << "{\n";
					auto keyOrder = ent->keyOrder;
					std::reverse(keyOrder.begin(), keyOrder.end());

					for (auto& keyName : keyOrder)
					{
						std::string keyValue = ent->keyvalues[keyName];

						if (keyName == "origin")
							continue;
						if (keyName == "model" && starts_with(keyValue, "*"))
							continue;

						if (keyName == "wad")
						{
							if (getEmbeddedTexCount() > 0)
							{
								if (keyValue.find(bsp_name + "_emb.wad;") == std::string::npos)
								{
									keyValue += bsp_name + "_emb.wad;";
								}
							}
						}

						map_text_data[ent] << "\"" << keyName << "\" \"" << keyValue << "\"\n";
					}

					if (mdlid == 0)
					{
						map_text_data[ent] << "\"_decompiler\" \"" << g_version_string << "\"\n";
					}
				}


				tempBrush.wind.Offset(offset);
				for (auto& bw : tempBrush.backWinds)
				{
					bw.Offset(offset);
				}


				vec3 v1 = tempBrush.wind.m_Points[0];
				vec3 v2 = tempBrush.wind.m_Points[1];
				vec3 v3 = tempBrush.wind.m_Points[2];

				float scaleS = 1.0f / texinfo.vS.length();
				float scaleT = 1.0f / texinfo.vT.length();

				vec3 nS = texinfo.vS.normalize();
				vec3 nT = texinfo.vT.normalize();

				vec3 xv, yv;
				int val = TextureAxisFromPlane(tempBrush.plane, xv, yv);
				float rotateX = AngleFromTextureAxis(texinfo.vS, true, val);
				float rotateY = AngleFromTextureAxis(texinfo.vT, false, val);
				float rotateTotal = rotateY - rotateX;

				map_text_data[ent] << "{\n";

				// front
				map_text_data[ent] << fmt::format("( {} ) ( {} ) ( {} ) {} [ {} {} ] [ {} {} ] {} {} {}\n",
					v1.toKeyvalueString(),
					v2.toKeyvalueString(),
					v3.toKeyvalueString(),
					tex.szName,
					nS.toKeyvalueString(), texinfo.shiftS,
					nT.toKeyvalueString(), texinfo.shiftT,
					rotateTotal, scaleS, scaleT);

				tempBrush.back_tex = tempBrush.back_empty && tempBrush.mdlIdx > 0 ? null_tex : tex;

				int id = 0;

				// back
				for (auto& bw : tempBrush.backWinds)
				{
					id++;
					vec3 v1_b = bw.m_Points[0];
					vec3 v2_b = bw.m_Points[1];
					vec3 v3_b = bw.m_Points[2];

					vec3 edge1 = v2_b - v1_b;
					vec3 edge2 = v3_b - v1_b;

					vec3 normal = crossProduct(edge1, edge2).normalize();
					TextureAxisFromPlane(normal, xv, yv);
					xv = xv.normalize();
					yv = yv.normalize();
					map_text_data[ent] << fmt::format("( {} ) ( {} ) ( {} ) {} [ {} {} ] [ {} {} ] {} {} {}\n",
						v1_b.toKeyvalueString(),
						v2_b.toKeyvalueString(),
						v3_b.toKeyvalueString(),
						!use_one_back_vert && id != 1 ? tempBrush.tex.szName : tempBrush.back_tex.szName,
						xv.toKeyvalueString(), 0,
						yv.toKeyvalueString(), 0,
						rotateTotal, 1.0f, 1.0f);
				}

				if (g_settings.verboseLogs && DebugKeyPressed)
					map_text_data[ent] << "// ENT:" << entIds[(int)e] << " BRUSH:" << brushNumbers[(int)e] << "\n";

				map_text_data[ent] << "}\n";

				brushNumbers[(int)e]++;

				jack_mesh_data[ent].push_back(tempBrush);
			}
		}
		// worldspawn
		if (!worldEnt)
		{
			map_file << "{\n";
			map_file << "\"classname\" \"" << "worldspawn" << "\"\n";
			map_file << "\"mapversion\" \"" << "220" << "\"\n";
			map_file << "\"_decompiler\" \"" << g_version_string << "\"\n";
			map_file << "\"wad\" \"" << bsp_name + "_emb.wad;" << "\"\n";

			if (create_worldspawnbox)
			{
				float item_z_offset = -std::max(std::fabs(w_maxs.z), std::fabs(w_mins.z));
				float item_y_offset = std::max(std::max(std::fabs(w_maxs.y), std::fabs(w_mins.y)),
					std::max(std::fabs(w_maxs.x), std::fabs(w_mins.x)));
				float item_x_offset = -item_y_offset;
				float cell_size = 8.0f;

				map_file <<
					GenerateCuboid(item_x_offset, item_y_offset,
						item_z_offset - cell_size,
						-item_x_offset,
						-item_y_offset,
						item_z_offset, "SKY");
				map_file << std::endl;

				map_file <<
					GenerateCuboid(item_x_offset, item_y_offset,
						-item_z_offset,
						-item_x_offset,
						-item_y_offset,
						-item_z_offset + cell_size, "SKY");
				map_file << std::endl;

				map_file <<
					GenerateCuboid(item_x_offset - cell_size, item_y_offset,
						item_z_offset - cell_size,
						item_x_offset,
						-item_y_offset,
						-item_z_offset + cell_size, "SKY");
				map_file << std::endl;

				map_file <<
					GenerateCuboid(-item_x_offset, item_y_offset,
						item_z_offset - cell_size,
						-item_x_offset + cell_size,
						-item_y_offset,
						-item_z_offset + cell_size, "SKY");
				map_file << std::endl;

				map_file <<
					GenerateCuboid(item_x_offset - cell_size, item_y_offset + cell_size,
						item_z_offset - cell_size,
						-item_x_offset + cell_size,
						item_y_offset,
						-item_z_offset + cell_size, "SKY");
				map_file << std::endl;

				map_file <<
					GenerateCuboid(item_x_offset - cell_size, -item_y_offset,
						item_z_offset - cell_size,
						-item_x_offset + cell_size,
						-item_y_offset - cell_size,
						-item_z_offset + cell_size, "SKY");
				map_file << std::endl;
			}

			map_file << "}\n";
		}
		else
		{
			for (auto& out : map_text_data)
			{
				if (out.first == worldEnt)
				{
					map_file << out.second.str();

					if (create_worldspawnbox)
					{
						float item_z_offset = -std::max(std::fabs(w_maxs.z), std::fabs(w_mins.z));
						float item_y_offset = std::max(std::max(std::fabs(w_maxs.y), std::fabs(w_mins.y)),
							std::max(std::fabs(w_maxs.x), std::fabs(w_mins.x)));
						float item_x_offset = -item_y_offset;
						float cell_size = 8.0f;

						map_file <<
							GenerateCuboid(item_x_offset, item_y_offset,
								item_z_offset - cell_size,
								-item_x_offset,
								-item_y_offset,
								item_z_offset, "SKY");
						map_file << std::endl;

						map_file <<
							GenerateCuboid(item_x_offset, item_y_offset,
								-item_z_offset,
								-item_x_offset,
								-item_y_offset,
								-item_z_offset + cell_size, "SKY");
						map_file << std::endl;

						map_file <<
							GenerateCuboid(item_x_offset - cell_size, item_y_offset,
								item_z_offset - cell_size,
								item_x_offset,
								-item_y_offset,
								-item_z_offset + cell_size, "SKY");
						map_file << std::endl;

						map_file <<
							GenerateCuboid(-item_x_offset, item_y_offset,
								item_z_offset - cell_size,
								-item_x_offset + cell_size,
								-item_y_offset,
								-item_z_offset + cell_size, "SKY");
						map_file << std::endl;

						map_file <<
							GenerateCuboid(item_x_offset - cell_size, item_y_offset + cell_size,
								item_z_offset - cell_size,
								-item_x_offset + cell_size,
								item_y_offset,
								-item_z_offset + cell_size, "SKY");
						map_file << std::endl;

						map_file <<
							GenerateCuboid(item_x_offset - cell_size, -item_y_offset,
								item_z_offset - cell_size,
								-item_x_offset + cell_size,
								-item_y_offset - cell_size,
								-item_z_offset + cell_size, "SKY");
						map_file << std::endl;
					}


					map_file << "}" << std::endl;
				}
			}
		}

		for (auto& out : map_text_data)
		{
			if (!worldEnt || out.first != worldEnt)
			{
				map_file << out.second.str() << "}" << std::endl;
			}
		}


		auto processEntry = [&](const std::pair<Entity*, std::deque<MapBrush>>& out)
			{
				if (selected && !out.first->isBspModel())
				{
					return;
				}
				//classname
				jack_file.writeLenStr(out.first->classname);
				//origin
				if (out.first->isBspModel())
					jack_file.write(vec3());
				else
					jack_file.write(out.first->origin);
				//editorflags
				if (out.first->isBspModel())
				{
					jack_file.write<int>(out.first == worldEnt ? 0x20 : 1); // SHOW 1
				}
				else
					jack_file.write<int>(1);
				//groupid
				jack_file.write<int>(0);
				//rootgroup
				jack_file.write<int>(0);
				//color
				COLOR4 rndColor;
				rndColor.r = 50 + rand() % 206;
				rndColor.g = 50 + rand() % 206;
				rndColor.b = 50 + rand() % 206;
				rndColor.a = 255;
				jack_file.write(rndColor);
				//attrs
				// 13 secret password
				jack_file.writeLenStr("spawnflags");
				jack_file.writeLenStr("origin");
				jack_file.writeLenStr("angles");
				jack_file.writeLenStr("scale");
				jack_file.writeLenStr("targetname");
				jack_file.writeLenStr("target");
				jack_file.writeLenStr("skyname");
				jack_file.writeLenStr("model");
				jack_file.writeLenStr("model");
				jack_file.writeLenStr("texture");
				jack_file.writeLenStr("model");
				jack_file.writeLenStr("model");
				jack_file.writeLenStr("script");
				// trash
				// 
				// spawn flags
				jack_file.write<int>(0);
				// sp_angles
				jack_file.write(vec3());
				// sp_rendering
				jack_file.write<int>(0x100);
				// sp_fx_color
				jack_file.write(COLOR4(255, 255, 255, 255));
				// sp_rendermode
				jack_file.write<int>(0);
				// sp_render_fx
				jack_file.write<int>(0);
				// sp_body
				jack_file.write<short>(0);
				// sp_skin
				jack_file.write<short>(0);
				// sp_sequence
				jack_file.write<int>(0);
				// sp_framerate
				jack_file.write<float>(10.0f);
				// sp_scale 
				jack_file.write<float>(1.0f);
				// sp_radius
				jack_file.write<float>(0.0f);
				// more trash 
				unsigned char tmpTrash[28]{};
				jack_file.write(tmpTrash);
				// keyvalues
				auto keyOrder = out.first->keyOrder;
				std::reverse(keyOrder.begin(), keyOrder.end());

				int keyvalues_count = 0;
				for (auto& keyName : keyOrder)
				{
					std::string keyValue = out.first->keyvalues[keyName];

					if (keyName == "origin" ||
						(keyName == "model" && starts_with(keyValue, '*')) ||
						keyName == "classname")
						continue;

					keyvalues_count++;
				}

				jack_file.write<int>(keyvalues_count);

				for (auto& keyName : keyOrder)
				{
					std::string keyValue = out.first->keyvalues[keyName];

					if (keyName == "origin" ||
						(keyName == "model" && starts_with(keyValue, '*')) ||
						keyName == "classname")
						continue;


					jack_file.writeKeyVal(keyName, keyValue);
				}
				// vis groups
				jack_file.write<int>(0);
				// brushes 
				jack_file.write<int>((int)out.second.size());
				for (MapBrush brush : out.second)
				{
					// meshes quake3
					jack_file.write<int>(0);
					// editor flags
					jack_file.write<int>(0);
					// group id 
					jack_file.write<int>(0);
					// root group id
					jack_file.write<int>(0);
					// color
					rndColor.r = 50 + rand() % 206;
					rndColor.g = 50 + rand() % 206;
					rndColor.b = 50 + rand() % 206;
					jack_file.write(rndColor);
					// vis groups
					jack_file.write<int>(0);
					// face count
					jack_file.write<int>((int)brush.backWinds.size() + 1);
					// back faces
					int id = 0;

					for (auto& bw : brush.backWinds)
					{
						id++;
						vec3 v1_b = bw.m_Points[0];
						vec3 v2_b = bw.m_Points[1];
						vec3 v3_b = bw.m_Points[2];

						vec3 edge1 = v2_b - v1_b;
						vec3 edge2 = v3_b - v1_b;

						BSPPLANE tmpPlane{};

						getPlaneFromVerts({ v2_b,v1_b,v3_b }, tmpPlane.vNormal, tmpPlane.fDist);

						tmpPlane.update_plane(false);

						vec3 xv, yv;
						int val = TextureAxisFromPlane(tmpPlane.vNormal, xv, yv);
						xv = xv.normalize();
						yv = yv.normalize();

						jack_file.write<int>(0x10);
						// vertex count
						jack_file.write<int>(3);


						jack_file.write(xv);
						jack_file.write<float>(0.0f);

						jack_file.write(yv);
						jack_file.write<float>(0.0f);

						float scaleS = 1.0f / xv.length();
						float scaleT = 1.0f / yv.length();

						jack_file.write<float>(scaleS);
						jack_file.write<float>(scaleT);

						float rotateX = AngleFromTextureAxis(brush.texInfo.vS, true, val);
						float rotateY = AngleFromTextureAxis(brush.texInfo.vT, false, val);
						float rotateTotal = rotateY - rotateX;

						jack_file.write<float>(rotateTotal);

						jack_file.write<int>(1); // world?

						unsigned char tmpTrash2[12]{};
						jack_file.write(tmpTrash2);

						jack_file.write<int>(0);

						unsigned char texName[64];

						if (!use_one_back_vert && id != 1)
						{
							memcpy(texName, brush.tex.szName, sizeof(brush.tex.szName));
						}
						else
						{
							memcpy(texName, brush.back_tex.szName, sizeof(brush.back_tex.szName));
						}
						jack_file.write(texName);

						// end texinfo

						// bsp plane
						jack_file.write(tmpPlane.vNormal);
						jack_file.write<float>(tmpPlane.fDist);
						jack_file.write<int>(tmpPlane.nType);

						float fU = dotProduct(brush.texInfo.vS, v2_b) + brush.texInfo.shiftS;
						float fV = dotProduct(brush.texInfo.vT, v2_b) + brush.texInfo.shiftT;
						fU /= (float)brush.back_tex.nWidth;
						fV /= -(float)brush.back_tex.nHeight;


						jack_file.write(v2_b);
						jack_file.write<float>(fU);
						jack_file.write<float>(fV);
						jack_file.write<int>(0);

						fU = dotProduct(brush.texInfo.vS, v1_b) + brush.texInfo.shiftS;
						fV = dotProduct(brush.texInfo.vT, v1_b) + brush.texInfo.shiftT;
						fU /= (float)brush.back_tex.nWidth;
						fV /= -(float)brush.back_tex.nHeight;


						jack_file.write(v1_b);
						jack_file.write<float>(fU);
						jack_file.write<float>(fV);
						jack_file.write<int>(0);

						fU = dotProduct(brush.texInfo.vS, v3_b) + brush.texInfo.shiftS;
						fV = dotProduct(brush.texInfo.vT, v3_b) + brush.texInfo.shiftT;
						fU /= (float)brush.back_tex.nWidth;
						fV /= -(float)brush.back_tex.nHeight;


						jack_file.write(v3_b);
						jack_file.write<float>(fU);
						jack_file.write<float>(fV);
						jack_file.write<int>(0);
					}



					// front face
					{
						// render_flags
						jack_file.write<int>(0x10);
						// vertex count
						jack_file.write<int>((int)brush.wind.m_Points.size());
						// texture info
						float scaleS = 1.0f / brush.texInfo.vS.length();
						float scaleT = 1.0f / brush.texInfo.vT.length();

						vec3 nS = brush.texInfo.vS.normalize();
						vec3 nT = brush.texInfo.vT.normalize();

						jack_file.write(nS);
						jack_file.write<float>(brush.texInfo.shiftS);

						jack_file.write(nT);
						jack_file.write<float>(brush.texInfo.shiftT);

						jack_file.write<float>(scaleS);
						jack_file.write<float>(scaleT);

						vec3 xv, yv;
						int val = TextureAxisFromPlane(brush.plane, xv, yv);
						float rotateX = AngleFromTextureAxis(brush.texInfo.vS, true, val);
						float rotateY = AngleFromTextureAxis(brush.texInfo.vT, false, val);
						float rotateTotal = rotateY - rotateX;

						jack_file.write<float>(rotateTotal);

						jack_file.write<int>(1); // world?

						unsigned char tmpTrash2[12]{};
						jack_file.write(tmpTrash2);

						jack_file.write<int>(0);

						unsigned char texName[64];
						memcpy(texName, brush.tex.szName, sizeof(brush.tex.szName));
						jack_file.write(texName);

						// end texinfo
						std::reverse(brush.wind.m_Points.begin(), brush.wind.m_Points.end());
						/*	BSPPLANE tmpPlane;
							getPlaneFromVerts({ brush.wind.m_Points[1],brush.wind.m_Points[2],brush.wind.m_Points[0] }, tmpPlane.vNormal, tmpPlane.fDist);*/

							// bsp plane
						jack_file.write(brush.plane.vNormal);
						jack_file.write<float>(brush.plane.fDist);
						jack_file.write<int>(brush.plane.nType);
						for (auto& p : brush.wind.m_Points)
						{
							jack_file.write(p);

							float fU = dotProduct(brush.texInfo.vS, p) + brush.texInfo.shiftS;
							float fV = dotProduct(brush.texInfo.vT, p) + brush.texInfo.shiftT;
							fU /= (float)brush.tex.nWidth;
							fV /= -(float)brush.tex.nHeight;

							jack_file.write<float>(fU);
							jack_file.write<float>(fV);
							jack_file.write<int>(0);
						}
					}
				}
			};

		if (!worldEnt)
		{
			Entity* tmpEnt = new Entity("worldspawn");
			processEntry({ tmpEnt,std::deque<MapBrush>() });
		}

		// worldspawn
		if (worldEnt)
		{
			for (auto& out : jack_mesh_data)
			{
				if (out.first == worldEnt)
				{
					processEntry(out);
				}
			}
		}
		g_progress.update("Export to .jmf...", (int)toExport.size());

		// other ents
		for (auto& out : jack_mesh_data)
		{
			g_progress.tick();
			if (!worldEnt || out.first != worldEnt)
			{
				processEntry(out);
			}
		}

		g_progress.clear();
		g_progress = ProgressMeter();

		if (!selected)
		{
			for (int i = 0; i < (int)ents.size(); i++)
			{
				if (decompiledEnts.find(i) == decompiledEnts.end())
				{
					auto keyOrder = ents[i]->keyOrder;
					std::reverse(keyOrder.begin(), keyOrder.end());

					map_file << "{" << "\n";
					for (auto& keyName : keyOrder)
					{
						std::string keyValue = ents[i]->keyvalues[keyName];
						map_file << ("\"" + keyName + "\" \"" + keyValue + "\"") << std::endl;
					}
					map_file << "}" << "\n";
				}
			}
		}

		map_file.flush();
		map_file.close();

		if (bsp_path.length() <= 4)
		{
			bsp_path = "unnamed.bsp";
		}

		update_ent_lump();
		update_lump_pointers();

		remove_unused_model_structures(CLEAN_MODELS);

		if (!selected)
		{
			resize_all_lightmaps();
			bsprend->reloadTextures();
			bsprend->loadLightmaps();

			update_ent_lump();
			update_lump_pointers();
		}

		if (getEmbeddedTexCount() > 0)
		{
			std::string targetMapFileName = bsp_path.substr(0, bsp_path.size() - 4) + "_emb.wad";
			createDir(g_working_dir);
			ExportEmbeddedWad(g_working_dir + basename(targetMapFileName));
			print_log(PRINT_BLUE, "Export {} wad!\n", targetMapFileName);
		}

		renderer->pushUndoState("EXPORT .MAP EDITED", EDIT_MODEL_LUMPS | FL_ENTITIES);
	}
	else
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1041));
	}
}


void recurse_node_map(Bsp* map, int nodeIdx)
{
	if (nodeIdx < 0)
	{
		BSPLEAF32& leaf = map->leaves[~nodeIdx];
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1038), ~nodeIdx);
		map->print_leaf(leaf);
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
	for (size_t i = 0; i < solidNodes.size(); i++)
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

void Bsp::ExportExtFile(const std::string& path, std::string& out_map_path)
{
	if (path.size() < 4)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0208));
		return;
	}

	std::string targetMapFileName = path.substr(0, path.size() - 4);
	std::string targetFileName = targetMapFileName + "_nolight.ext";

	removeFile(targetFileName);

	std::ofstream targetFile(targetFileName, std::ios::trunc | std::ios::binary);
	if (!targetFile.is_open())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0209), targetFileName);
		return;
	}

	print_log(get_localized_string(LANG_0210), targetFileName);
	write(targetMapFileName + "_nolight.bsp");

	out_map_path = targetMapFileName + "_nolight.bsp";

	Bsp* tmpBsp = new Bsp(targetMapFileName + "_nolight.bsp");


	print_log(get_localized_string(LANG_0211));

	removeFile(targetMapFileName + "_nolight.bsp");

	print_log(get_localized_string(LANG_0212), targetMapFileName + "_nolight.bsp");

	tmpBsp->lumps[LUMP_LIGHTING].clear();
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
		GetFaceExtents(i, mins, maxs);
		targetFile << fmt::format("{} {} {} {}\n", mins[0], mins[1], maxs[0], maxs[1]);
	}

	print_log(get_localized_string(LANG_0215), targetMapFileName + "_nolight.wa_");

	removeFile(targetMapFileName + "_nolight.wa_");

	ExportEmbeddedWad(targetMapFileName + "_nolight.wa_");

	Wad* tmpWad = new Wad(targetMapFileName + "_nolight.wa_");



	std::vector<std::string> addedTextures;
	std::vector<WADTEX*> outTextures;

	if (tmpWad->readInfo())
	{
		if (tmpWad->dirEntries.size())
		{
			for (int i = 0; i < (int)tmpWad->dirEntries.size(); i++)
			{
				WADTEX* tex = tmpWad->readTexture(i);

				if (tex->szName[0] == '\0' || std::find(addedTextures.begin(), addedTextures.end(), tex->szName) != addedTextures.end())
				{
					continue;
				}

				addedTextures.push_back(tex->szName);
				outTextures.push_back(tex);
			}
		}
	}

	int missingTexures = 0;

	for (int i = 0; i < textureCount; i++)
	{
		int texOffset = ((int*)textures)[i + 1];
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
			else if (tex.nOffsets[0] > 0 && tex.szName[0] != '\0')
			{
				if (std::find(addedTextures.begin(), addedTextures.end(), tex.szName) != addedTextures.end())
				{
					continue;
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

int Bsp::getEmbeddedTexCount()
{
	int count = 0;

	for (int i = 0; i < textureCount; i++)
	{
		int oldOffset = ((int*)textures)[i + 1];
		if (oldOffset >= 0)
		{
			BSPMIPTEX* bspTex = (BSPMIPTEX*)(textures + oldOffset);
			if (bspTex->nOffsets[0] <= 0)
				continue;

			count++;
		}
	}

	return count;
}

bool Bsp::ExportEmbeddedWad(const std::string& path)
{
	bool retval = true;
	if (textureCount > 0)
	{
		if (fileExists(path))
			removeFile(path);
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
				if (!is_texture_has_pal)
				{
					if (g_settings.pal_id >= 0)
					{
						WADTEX* newTex = new WADTEX(bspTex, g_settings.palettes[g_settings.pal_id].data,
							(unsigned short)g_settings.palettes[g_settings.pal_id].colors);
						tmpWadTex.push_back(newTex);
					}
					else
					{
						WADTEX* newTex = new WADTEX(bspTex, g_settings.palette_default);
						tmpWadTex.push_back(newTex);
					}
				}
				else
				{
					WADTEX* newTex = new WADTEX(bspTex);
					tmpWadTex.push_back(newTex);
				}
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
			mapRenderers[i]->reuploadTextures();
			mapRenderers[i]->preRenderFaces();
		}
	}

	delete tmpWad;
	return true;
}

int Bsp::import_mdl_to_bspmodel(int ent, int generateClipnodes)
{
	auto& rndEntity = renderer->renderEnts[ent];

	if (rndEntity.mdl)
	{
		auto rendmdl = rndEntity.mdl;
		if (rendmdl->mdl_mesh_groups.size())
		{
			bool is_valid_nodes = false;

			mat4x4 angle_mat;
			angle_mat.loadIdentity();

			if (rndEntity.needAngles)
			{
				vec3 angles = -rndEntity.angles;


				angle_mat.rotateX((angles.z * (HL_PI / 180.0f)));
				angle_mat.rotateY((angles.x * (HL_PI / 180.0f)));
				angle_mat.rotateZ((angles.y * (HL_PI / 180.0f)));

				//print_log(PRINT_RED, "CONVERT ANGLES : {} for {} ent\n", angles.toKeyvalueString(), ents[ent]->classname);
			}

			std::vector<vec3> all_verts;
			std::vector<StudioMesh> merged_meshes;
			for (size_t group = 0; group < rendmdl->mdl_mesh_groups.size(); group++)
			{
				for (size_t meshid = 0; meshid < rendmdl->mdl_mesh_groups[group].size(); meshid++)
				{
					merged_meshes.push_back(rendmdl->mdl_mesh_groups[group][meshid]);
					if (generateClipnodes)
					{
						for (auto v : rendmdl->mdl_mesh_groups[group][meshid].verts)
							all_verts.push_back((angle_mat * vec4(v.pos.flipUV(), 1.0f)).xyz());
					}
				}
			}

			int newModelIdx = import_mdl_to_bspmodel(merged_meshes, angle_mat, is_valid_nodes);

			if (ents[ent]->hasKey("angles"))
				ents[ent]->removeKeyvalue("angles");
			if (ents[ent]->hasKey("angle"))
				ents[ent]->removeKeyvalue("angle");

			ents[ent]->setOrAddKeyvalue("model", "*" + std::to_string(newModelIdx));
			ents[ent]->setOrAddKeyvalue("classname", "func_wall");
			rndEntity.mdl = NULL;

			update_ent_lump();

			if (!is_valid_nodes && generateClipnodes)
			{
				int max_rows;
				auto collision_list = make_collision_from_triangles(all_verts, max_rows);
				std::reverse(collision_list.begin(), collision_list.end());

				std::vector<int> merged_models;
				std::vector<BBOX> merged_cubes;
				int errors = 0;

				int defaultModels = modelCount;

				// PASS #1 [MERGE X]
				for (auto& cube_list : collision_list)
				{
					for (int z = max_rows; z >= 0; z--)
					{
						std::vector<int> models_to_merge;
						std::vector<BBOX> cubes_to_merge;
						for (auto& cube : cube_list)
						{
							if (cube.row == z)
							{
								int tmpModelIdx = create_solid(cube.mins, cube.maxs, 0, false);
								BSPMODEL& model = models[tmpModelIdx];
								model.iFirstFace = 0;
								model.nFaces = 0;
								models_to_merge.push_back(tmpModelIdx);
								cubes_to_merge.push_back(cube);
							}
						}
						if (models_to_merge.size() == 1)
						{
							merged_models.push_back(models_to_merge[0]);
							merged_cubes.push_back(cubes_to_merge[0]);
						}
						else if (models_to_merge.size() > 1)
						{
							while (models_to_merge.size() > 1)
							{
								int tries = 0;

								int idx1 = models_to_merge[0];
								int idx2 = models_to_merge[1];

								int merged_index = merge_two_models_idx(idx1, idx2, tries);
								models_to_merge.erase(models_to_merge.begin());

								if (merged_index >= 0)
								{
									models_to_merge[0] = merged_index;
									if (idx2 == merged_index)
									{
										cubes_to_merge.erase(cubes_to_merge.begin());
									}
									else
									{
										std::swap(cubes_to_merge[1], cubes_to_merge[0]);
										cubes_to_merge.erase(cubes_to_merge.begin());
									}
								}
								else
								{
									cubes_to_merge.erase(cubes_to_merge.begin());
									errors++;
								}
							}

							merged_models.push_back(models_to_merge[0]);
							merged_cubes.push_back(cubes_to_merge[0]);
						}
					}
				}

				print_log(PRINT_BLUE, "Merged_cubes after first PASS {} !\n", merged_cubes.size() + errors);

				// PASS #2 [MERGE Y]
				std::vector<int> merged_models_pass2;
				std::vector<BBOX> merged_cubes_pass2;

				for (int z = max_rows; z >= 0; z--)
				{
					std::vector<int> models_to_merge;
					std::vector<BBOX> cubes_to_merge;

					for (size_t cube = 0; cube < merged_cubes.size(); cube++)
					{
						if (merged_cubes[cube].row == z)
						{
							int tmpModelIdx = merged_models[cube];
							models_to_merge.push_back(tmpModelIdx);
							cubes_to_merge.push_back(merged_cubes[cube]);
						}
					}
					if (models_to_merge.size() == 1)
					{
						merged_models_pass2.push_back(models_to_merge[0]);
						merged_cubes_pass2.push_back(cubes_to_merge[0]);
					}
					else if (models_to_merge.size() > 1)
					{
						while (models_to_merge.size() > 1)
						{
							int tries = 0;

							int idx1 = models_to_merge[0];
							int idx2 = models_to_merge[1];

							int merged_index = merge_two_models_idx(idx1, idx2, tries);
							models_to_merge.erase(models_to_merge.begin());

							if (merged_index >= 0)
							{
								models_to_merge[0] = merged_index;
								if (idx2 == merged_index)
								{
									cubes_to_merge.erase(cubes_to_merge.begin());
								}
								else
								{
									std::swap(cubes_to_merge[1], cubes_to_merge[0]);
									cubes_to_merge.erase(cubes_to_merge.begin());
								}
							}
							else
							{
								cubes_to_merge.erase(cubes_to_merge.begin());
								errors++;
							}
						}

						merged_models_pass2.push_back(models_to_merge[0]);
						merged_cubes_pass2.push_back(cubes_to_merge[0]);
					}
				}

				// PASS #3 [MERGE Z]
				std::vector<int> models_to_merge_pass3;

				for (int z = max_rows; z >= 0; z--)
				{
					for (size_t cube = 0; cube < merged_cubes_pass2.size(); cube++)
					{
						if (merged_cubes_pass2[cube].row == z)
						{
							models_to_merge_pass3.push_back(merged_models_pass2[cube]);
						}
					}
				}

				print_log(PRINT_BLUE, "Merged_cubes after second PASS {} !\n", merged_cubes_pass2.size() + errors);
				while (models_to_merge_pass3.size() > 1)
				{
					int tries = 0;

					int idx1 = models_to_merge_pass3[0];
					int idx2 = models_to_merge_pass3[1];

					int merged_index = merge_two_models_idx(idx1, idx2, tries);
					models_to_merge_pass3.erase(models_to_merge_pass3.begin());

					if (merged_index >= 0)
					{
						models_to_merge_pass3[0] = merged_index;
					}
					else
					{
						errors++;
					}
				}

				print_log(PRINT_BLUE, "Merged_cubes after finall PASS {} !\n", models_to_merge_pass3.size() + errors);

				STRUCTUSAGE modelUsage = STRUCTUSAGE(this);
				mark_model_structures(models_to_merge_pass3[0], &modelUsage, true);

				for (int i = 0; i < planeCount; i++)
				{
					if (modelUsage.planes[i])
					{
						planes[i].fDist = std::signbit(planes[i].fDist) ? planes[i].fDist - 0.01f : planes[i].fDist + 0.01f;
					}
				}

				//models[newModelIdx].iHeadnodes[0] = models[models_to_merge_pass3[0]].iHeadnodes[0];
				models[newModelIdx].iHeadnodes[1] = models[models_to_merge_pass3[0]].iHeadnodes[1];
				models[newModelIdx].iHeadnodes[2] = models[models_to_merge_pass3[0]].iHeadnodes[2];
				models[newModelIdx].iHeadnodes[3] = models[models_to_merge_pass3[0]].iHeadnodes[3];

				for (int i = defaultModels; i < modelCount; i++)
				{
					models[i].iHeadnodes[0] =
						models[i].iHeadnodes[1] =
						models[i].iHeadnodes[2] =
						models[i].iHeadnodes[3] = 0;
					models[i].iFirstFace = 0;
					models[i].nFaces = 0;
					models[i].nVisLeafs = 0;
				}


				remove_unused_model_structures(CLEAN_LEAVES);

				int totalLeaves = 0;
				for (int i = 0; i < modelCount && i < defaultModels; i++)
				{
					totalLeaves += models[i].nVisLeafs;
				}

				models[newModelIdx].nVisLeafs = leafCount - totalLeaves;

				print_log(PRINT_BLUE, "Very bad clipnodes regenerated with {} errors {} leaves!\n", errors, models[newModelIdx].nVisLeafs);
			}
			return newModelIdx;
		}
	}

	return -1;
}

int Bsp::import_mdl_to_bspmodel(std::vector<StudioMesh>& meshes, mat4x4 angles, bool& validNodes)
{
	int newModelIdx = create_model();

	std::set<Texture*> added_textures;

	int modelFirstFace = faceCount;
	int modelFaces = 0;
	int lightOffset = lightDataLength;

	int tmpLightSize = (512 * 512 * 3);

	unsigned char* testlightdata = new unsigned char[lightDataLength + tmpLightSize];
	memcpy(testlightdata, lightdata, lightDataLength);
	memset(testlightdata + lightDataLength, 125, tmpLightSize);
	replace_lump(LUMP_LIGHTING, testlightdata, lightDataLength + tmpLightSize);
	delete[] testlightdata;

	vec3 mins = vec3(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
	vec3 maxs = vec3(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);

	for (auto& mesh : meshes)
	{
		auto mesh_verts = mesh.verts;
		auto mesh_texture = mesh.texture;

		std::vector<int> newVertIndexes;

		int startVertCount = vertCount;
		int newVertCount = startVertCount + (int)(mesh_verts.size());

		vec3* newverts = new vec3[newVertCount];
		memcpy(newverts, verts, startVertCount * sizeof(vec3));
		replace_lump(LUMP_VERTICES, newverts, newVertCount * sizeof(vec3));
		delete[] newverts;

		newverts = verts;

		std::vector<vec2> newuv;
		newuv.resize(newVertCount);

		int v = 0;

		for (v = (int)startVertCount; v < newVertCount; v++)
		{
			newverts[v] = (angles * vec4(mesh_verts[v - startVertCount].pos.unflip(), 1.0f)).xyz();
			newuv[v] = { mesh_verts[v - startVertCount].u, mesh_verts[v - startVertCount].v };
			newVertIndexes.push_back(v);
			expandBoundingBox(newverts[v], mins, maxs);
		}


		std::map<int, int> vertToSurfedge;
		bool inverse = false;
		unsigned int startEdge = edgeCount;
		int newdedgescount = startEdge + ((int)(mesh_verts.size()) + 1) / 2;
		BSPEDGE32* newedges = new BSPEDGE32[newdedgescount];
		memcpy(newedges, edges, startEdge * sizeof(BSPEDGE32));
		replace_lump(LUMP_EDGES, newedges, newdedgescount * sizeof(BSPEDGE32));
		delete[] newedges;
		newedges = edges;


		v = 0;
		for (unsigned int i = 0; i < mesh_verts.size(); i += 2)
		{
			unsigned int v0 = i;
			unsigned int v1 = (i + 1) % mesh_verts.size();
			newedges[startEdge + v] = BSPEDGE32((unsigned int)newVertIndexes[v0], (unsigned int)newVertIndexes[v1]);

			vertToSurfedge[v0] = startEdge + v;
			if (v1 > 0)
			{
				vertToSurfedge[v1] = -((int)(startEdge + v)); // negative = use second vert
			}

			v++;
		}


		inverse = false;

		int startSurfedgeCount = surfedgeCount;
		int newsurfedges_count = startSurfedgeCount + (int)(mesh_verts.size());
		int* newsurfedges = new int[newsurfedges_count];
		memcpy(newsurfedges, surfedges, startSurfedgeCount * sizeof(int));
		replace_lump(LUMP_SURFEDGES, newsurfedges, newsurfedges_count * sizeof(int));
		delete[] newsurfedges;
		newsurfedges = surfedges;

		for (v = (int)startSurfedgeCount; v < newsurfedges_count; v++)
		{
			newsurfedges[v] = vertToSurfedge[v - (int)startSurfedgeCount];
		}

		int numTriangles = (int)(mesh_verts.size()) / 3;
		modelFaces += (int)numTriangles;

		int startFaceCount = faceCount;
		int newFaceCount = startFaceCount + numTriangles;
		BSPFACE32* newfaces = new BSPFACE32[newFaceCount];
		memcpy(newfaces, faces, startFaceCount * sizeof(BSPFACE32));
		replace_lump(LUMP_FACES, newfaces, newFaceCount * sizeof(BSPFACE32));
		delete[] newfaces;
		newfaces = faces;

		int startPlaneCount = planeCount;
		int newPlaneCount = startPlaneCount + numTriangles;
		BSPPLANE* newplanes = new BSPPLANE[newPlaneCount];
		memcpy(newplanes, planes, startPlaneCount * sizeof(BSPPLANE));
		replace_lump(LUMP_PLANES, newplanes, newPlaneCount * sizeof(BSPPLANE));
		delete[] newplanes;
		newplanes = planes;

		int startTexinfoCount = texinfoCount;
		int newTexinfosCount = startTexinfoCount + numTriangles;
		BSPTEXTUREINFO* newtexinfos = new BSPTEXTUREINFO[newTexinfosCount];
		memcpy(newtexinfos, texinfos, startTexinfoCount * sizeof(BSPTEXTUREINFO));
		replace_lump(LUMP_TEXINFO, newtexinfos, newTexinfosCount * sizeof(BSPTEXTUREINFO));
		delete[] newtexinfos;
		newtexinfos = texinfos;

		for (v = (int)startFaceCount; v < newFaceCount; v++)
		{
			int edgeIdx = (int)(startSurfedgeCount + (v - startFaceCount) * 3);

			newfaces[v].iFirstEdge = edgeIdx;
			newfaces[v].nEdges = 3;
			newfaces[v].iPlane = (int)((v - startFaceCount) + startPlaneCount);
			newfaces[v].iTextureInfo = (int)((v - startFaceCount) + startTexinfoCount);
			newfaces[v].nLightmapOffset = lightOffset;
			memset(newfaces[v].nStyles, 255, MAX_LIGHTMAPS);
			newfaces[v].nStyles[0] = 0;


			BSPTEXTUREINFO& texInfo = newtexinfos[newfaces[v].iTextureInfo];
			texInfo = BSPTEXTUREINFO();
			int miptex = 0;

			if (!added_textures.count(mesh_texture))
			{
				COLOR4* tmpDataTex = (COLOR4*)mesh_texture->get_data();

				int newWidth = mesh_texture->width;
				int newHeight = mesh_texture->height;
				getTrueTexSize(newWidth, newHeight, g_limits.maxTextureDimension);

				COLOR3* tmpData = new COLOR3[mesh_texture->width * mesh_texture->height];
				if (mesh_texture->format == GL_RGBA)
				{
					for (int i = 0; i < mesh_texture->width * mesh_texture->height; i++)
					{
						tmpData[i] = tmpDataTex[i].rgb();
					}
				}
				else
				{
					memcpy(tmpData, tmpDataTex, mesh_texture->width * mesh_texture->height * sizeof(COLOR3));
				}
				added_textures.insert(mesh_texture);

				if (newWidth != mesh_texture->width
					|| newHeight != mesh_texture->height)
				{
					std::vector<COLOR3> newData;
					scaleImage(tmpData, newData, mesh_texture->width, mesh_texture->height,
						newWidth, newHeight);
					delete[] tmpData;
					tmpData = new COLOR3[newWidth * newHeight];

					if (newData.size() == newWidth * newHeight)
						std::copy(newData.begin(), newData.end(), tmpData);

					if (GetImageColors(tmpData, newWidth * newHeight) > 256)
					{
						Quantizer* tmpCQuantizer = new Quantizer(256, 8);

						/*if (ditheringEnabled)
							tmpCQuantizer->ApplyColorTableDither((COLOR3*)tmpData, newWidth, newHeight);
						else*/
						tmpCQuantizer->ApplyColorTable((COLOR3*)tmpData, newWidth * newHeight);

						delete tmpCQuantizer;
					}

					//memcpy(tmpData, newData.data(), newData.size() * sizeof(COLOR3));
				}

				std::string trueTexName = mesh_texture->texName;
				while (trueTexName.size() > 15)
				{
					trueTexName.erase(trueTexName.begin());
				}

				auto tmpTex = find_embedded_texture(trueTexName.c_str(), miptex);

				if (miptex < 0 || !tmpTex || tmpTex->nWidth != newWidth || tmpTex->nHeight != newHeight)
				{
					miptex = add_texture(trueTexName.c_str(), (unsigned char*)tmpData, newWidth,
						newHeight);
				}

				delete[] tmpData;
			}
			else
			{
				std::string trueTexName = mesh_texture->texName;
				while (trueTexName.size() > 15)
				{
					trueTexName.erase(trueTexName.begin());
				}
				find_embedded_texture(trueTexName.c_str(), miptex);
			}

			BSPPLANE& plane = newplanes[newfaces[v].iPlane];

			int vert_id1 = newsurfedges[edgeIdx + 0] > 0 ? newedges[abs(newsurfedges[edgeIdx + 0])].iVertex[0]
				: newedges[abs(newsurfedges[edgeIdx + 0])].iVertex[1];
			vec3 vertex1 = newverts[vert_id1];

			int vert_id2 = newsurfedges[edgeIdx + 1] > 0 ? newedges[abs(newsurfedges[edgeIdx + 1])].iVertex[0]
				: newedges[abs(newsurfedges[edgeIdx + 1])].iVertex[1];
			vec3 vertex2 = newverts[vert_id2];

			int vert_id3 = newsurfedges[edgeIdx + 2] > 0 ? newedges[abs(newsurfedges[edgeIdx + 2])].iVertex[0]
				: newedges[abs(newsurfedges[edgeIdx + 2])].iVertex[1];
			vec3 vertex3 = newverts[vert_id3];


			std::vector<vec3> vertexes{};
			vertexes.push_back(vertex1);
			vertexes.push_back(vertex2);
			vertexes.push_back(vertex3);

			// Texture coordinates
			std::vector<vec2> uvs{};
			uvs.push_back(newsurfedges[edgeIdx + 0] > 0 ? newuv[newedges[abs(newsurfedges[edgeIdx + 0])].iVertex[0]]
				: newuv[newedges[abs(newsurfedges[edgeIdx])].iVertex[1]]);
			uvs.push_back(newsurfedges[edgeIdx + 1] > 0 ? newuv[newedges[abs(newsurfedges[edgeIdx + 1])].iVertex[0]]
				: newuv[newedges[abs(newsurfedges[edgeIdx + 1])].iVertex[1]]);
			uvs.push_back(newsurfedges[edgeIdx + 2] > 0 ? newuv[newedges[abs(newsurfedges[edgeIdx + 2])].iVertex[0]]
				: newuv[newedges[abs(newsurfedges[edgeIdx + 2])].iVertex[1]]);

			for (auto& uv : uvs)
			{
				int newWidth = mesh_texture->width;
				int newHeight = mesh_texture->height;

				getTrueTexSize(newWidth, newHeight);
				uv.x *= newWidth;
				uv.y *= newHeight;
			}

			// Compute edges and delta UVs
			vec3 edge1 = vertexes[1] - vertexes[0];
			vec3 edge2 = vertexes[2] - vertexes[0];

			vec3 normal = crossProduct(edge1, edge2).normalize();

			// Calculate the distance from the origin
			float dist = getDistAlongAxis(normal, vertex1);
			plane.vNormal = normal;
			plane.fDist = dist;

			newfaces[v].nPlaneSide = !plane.update_plane(plane.vNormal, plane.fDist);

			calculateTextureInfo(texInfo, vertexes, uvs);
			texInfo.iMiptex = miptex;
		}

		for (int f = startFaceCount; f < newFaceCount; f++)
		{
			int tmins[2];
			int tmaxs[2];
			if (!GetFaceExtents((int)f, tmins, tmaxs))
			{
				BSPTEXTUREINFO& texInfo = newtexinfos[newfaces[f].iTextureInfo];
				texInfo.nFlags = TEX_SPECIAL;
				print_log(PRINT_BLUE, "Found bad face {} extents, set to SPECIAL\n", f);
			}
		}
	}

	vec3 origin = getCenter(maxs, mins);

	models[newModelIdx].nMins = mins;
	models[newModelIdx].nMaxs = maxs;
	models[newModelIdx].vOrigin = vec3();
	models[newModelIdx].iFirstFace = modelFirstFace;
	models[newModelIdx].nFaces = modelFaces;

	int empty_leaf = create_leaf(CONTENTS_EMPTY);

	models[newModelIdx].nVisLeafs += 1;

	leaves[empty_leaf].nMins = mins + 1.0f;
	leaves[empty_leaf].nMaxs = maxs - 1.0f;
	leaves[empty_leaf].nVisOffset = -1;

	update_lump_pointers();

	for (int i = modelFirstFace; i < modelFirstFace + modelFaces; i++)
	{
		leaf_add_face(i, empty_leaf);
	}


	/*unsigned int startNode = nodeCount;
	{
		int newnodecount = nodeCount + modelFaces + 2;
		BSPNODE32* newNodes = new BSPNODE32[newnodecount];
		memcpy(newNodes, nodes, nodeCount * sizeof(BSPNODE32));

		int sharedSolidLeaf = 0;
		int anyEmptyLeaf = empty_leaf;

		for (int k = 0; k < modelFaces; k++)
		{
			BSPNODE32& node = newNodes[nodeCount + k];

			node.iFirstFace = (int)(modelFirstFace + k); // face required for decals
			node.nFaces = 1;
			node.iPlane = (int)(modelFirstPlane + k);
			node.nMins = node.nMaxs = vec3();
			// node mins/maxs don't matter for submodels. Leave them at 0.

			int insideContents = k + 1 == modelFaces ? ~sharedSolidLeaf : (int)(nodeCount + k + 1);
			int outsideContents = ~anyEmptyLeaf;

			// can't have negative normals on planes so children are swapped instead
			if (faces[k].nPlaneSide)
			{
				node.iChildren[0] = insideContents;
				node.iChildren[1] = outsideContents;
			}
			else
			{
				node.iChildren[0] = outsideContents;
				node.iChildren[1] = insideContents;
			}

			if (k + 1 == modelFaces)
			{
				if (!faces[node.iFirstFace].nPlaneSide)
				{
					node.iChildren[0] = (int)(nodeCount + k + 1);
					node.iChildren[1] = (int)(nodeCount + k + 2);
				}
				else
				{
					node.iChildren[0] = (int)(nodeCount + k + 2);
					node.iChildren[1] = (int)(nodeCount + k + 1);
				}

				BSPNODE32& lastnode1 = newNodes[nodeCount + k + 1];
				lastnode1 = node;
				lastnode1.iChildren[0] = ~sharedSolidLeaf;
				lastnode1.iChildren[1] = outsideContents;

				BSPNODE32& lastnode2 = newNodes[nodeCount + k + 2];
				lastnode2 = node;
				lastnode2.nFaces = 0;
				lastnode2.iChildren[0] = outsideContents;
				lastnode2.iChildren[1] = ~sharedSolidLeaf;
			}
		}

		replace_lump(LUMP_NODES, newNodes, newnodecount * sizeof(BSPNODE32));
		delete[] newNodes;
	}

	models[newModelIdx].iHeadnodes[0] = startNode;*/
	models[newModelIdx].iHeadnodes[1] = CONTENTS_EMPTY;
	models[newModelIdx].iHeadnodes[2] = CONTENTS_EMPTY;
	models[newModelIdx].iHeadnodes[3] = CONTENTS_EMPTY;


	/*std::vector<TransformVert> hullVerts;
	if (getModelPlaneIntersectVerts(newModelIdx, hullVerts))
	{
		print_log(PRINT_GREEN, "Found valid intersect verts for model {}\n", newModelIdx);
		validNodes = regenerate_clipnodes(newModelIdx, -1);
	}*/

	/*if (!validNodes)
	{

		print_log(PRINT_RED, "Invalid intersect verts for model {}...\n", newModelIdx);*/
	create_node_box(models[newModelIdx].nMins, models[newModelIdx].nMaxs, &models[newModelIdx], true, empty_leaf);
	validNodes = false;
	/*}*/

	return newModelIdx;
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
	/*if (!renderer || renderer->faceMaths.empty())
		return;

	Texture* tex = g_app->giveMeTexture(texname);
	if (!tex->uploaded)
		tex->upload(Texture::TYPE_DECAL);

	int bestMath = -1;
	float bestDist = 30.01f;

	for (int faceIdx = 0; faceIdx < renderer->faceMaths.size(); faceIdx++)
	{
		FaceMath& face = renderer->faceMaths[faceIdx];
		if (renderer->pickFaceMath(pos + (face.normal * 0.01f), face.normal * -0.01f, face, bestDist))
		{
			bestMath = faceIdx;
		}
	}

	int modelidx = get_model_from_face(bestMath);
	print_log(get_localized_string(LANG_0218), modelidx, bestMath, renderer->intersectVec.toKeyvalueString());*/

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

	retFaces.reserve(leaf.nMarkSurfaces);
	for (int i = 0; i < leaf.nMarkSurfaces; i++)
	{
		retFaces.push_back(marksurfs[leaf.iFirstMarkSurface + i]);
	}

	std::sort(retFaces.begin(), retFaces.end());
	retFaces.erase(std::unique(retFaces.begin(), retFaces.end()), retFaces.end());

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

	retFaces.reserve(leaf.nMarkSurfaces);
	for (int i = 0; i < leaf.nMarkSurfaces; i++)
	{
		retFaces.push_back(marksurfs[leaf.iFirstMarkSurface + i]);
	}

	std::sort(retFaces.begin(), retFaces.end());
	retFaces.erase(std::unique(retFaces.begin(), retFaces.end()), retFaces.end());
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


int Bsp::getFaceFromVec(const vec3& pos, int modelIdx, int& content)
{
	if (modelIdx >= 0 && modelIdx < modelCount)
	{
		int iNode = models[modelIdx].iHeadnodes[0];
		int iTargetPlane = -1;
		while (iNode >= 0)
		{
			BSPNODE32& node = nodes[iNode];
			iTargetPlane = node.iPlane;
			BSPPLANE& plane = planes[iTargetPlane];
			float d = dotProduct(plane.vNormal, pos) - plane.fDist;
			if (d < 0)
			{
				iNode = node.iChildren[1];
			}
			else
			{
				iNode = node.iChildren[0];
			}
		}

		if (iTargetPlane >= 0)
		{
			BSPMODEL& model = models[modelIdx];
			for (int i = 0; i < model.nFaces; i++)
			{
				BSPFACE32& face = faces[model.iFirstFace + i];
				if (face.iPlane == iTargetPlane)
				{
					return model.iFirstFace + i;
				}
			}
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
		if (is_texture_has_pal && is_texture_with_pal(textureid))
		{
			sz += sizeof(short); /* pal size */
			sz += sizeof(COLOR3) * 256; // pallette
		}

		sz += calcMipsSize(tex->nWidth, tex->nHeight);
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

	return is_texture_has_pal;
}

void Bsp::face_fix_duplicate_edges_index(int faceIdx)
{
	if (faceIdx < 0 || faceIdx >= faceCount)
	{
		return;
	}

	std::set<int> verts_usage;
	BSPFACE32 face = faces[faceIdx];

	for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
	{
		int edgeIdx = surfedges[e];
		BSPEDGE32& edge = edges[abs(edgeIdx)];
		int vert = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];;
		if (verts_usage.count(vert))
		{
			int newedge_id = create_edge();

			if (edgeIdx >= 0)
			{
				edgeIdx = newedge_id;
			}
			else
			{
				edgeIdx = -newedge_id;
			}

			BSPEDGE32& newedge = edges[newedge_id];
			newedge = edge;

			int v1 = create_vert();
			verts[v1] = verts[edge.iVertex[0]];
			newedge.iVertex[0] = v1;

			int v2 = create_vert();
			verts[v2] = verts[edge.iVertex[1]];
			newedge.iVertex[1] = v1;

			continue;
		}
		verts_usage.insert(vert);
	}
}

bool Bsp::is_face_duplicate_edges(int faceIdx)
{
	if (faceIdx < 0 || faceIdx >= faceCount)
	{
		return true;
	}

	std::set<int> verts_usage;
	BSPFACE32 face = faces[faceIdx];

	for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
	{
		int edgeIdx = surfedges[e];
		BSPEDGE32 edge = edges[abs(edgeIdx)];
		int vert = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];
		if (verts_usage.count(vert))
		{
			return true;
		}
		verts_usage.insert(vert);
	}

	return false;
}


int Bsp::getWorlspawnEntId()
{
	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i]->isWorldSpawn())
			return (int)i;
	}
	return -1;
}

Entity* Bsp::getWorldspawnEnt()
{
	int entId = getWorlspawnEntId();
	if (entId != -1)
	{
		return ents[entId];
	}
	return NULL;
}



bool Bsp::CalcFaceExtents(lightinfo_t* l)
{
	int bmins[2];
	int bmaxs[2];
	if (!GetFaceExtents(l->surfnum, bmins, bmaxs))
	{
		for (int i = 0; i < 2; i++)
		{
			l->texmins[i] = 0;
			l->texsize[i] = 0;
		}
		return false;
	}
	for (int i = 0; i < 2; i++)
	{
		l->texmins[i] = bmins[i];
		l->texsize[i] = bmaxs[i] - bmins[i];
	}
	return true;
}

bool Bsp::GetFaceExtents(int facenum, int mins_out[2], int maxs_out[2])
{
	bool retval = CanFindFacePosition(this, facenum, mins_out, maxs_out);

	BSPFACE32& face = faces[facenum];
	BSPTEXTUREINFO tex = texinfos[face.iTextureInfo];

	if (tex.nFlags & TEX_SPECIAL)
	{
		retval = true;
	}

	for (int i = 0; i < 2; i++)
	{
		int tmpTextureStep = CalcFaceTextureStep(facenum);

		if (!(tex.nFlags & TEX_SPECIAL) && (maxs_out[i] - mins_out[i]) * tmpTextureStep > (g_limits.maxSurfaceExtent * g_limits.maxSurfaceExtent))
		{
			if (retval)
			{
				print_log(get_localized_string("BAD_SURFACE_EXT"), facenum, (int)((maxs_out[i] - mins_out[i]) * tmpTextureStep), (g_limits.maxSurfaceExtent * g_limits.maxSurfaceExtent));
				print_log("TRACE: Mins {} maxs {}\n", mins_out[i], maxs_out[i]);
			}
			retval = false;
			mins_out[i] = 1;
			maxs_out[i] = 1;
		}

		if (maxs_out[i] - mins_out[i] < 0)
		{
			if (retval)
			{
				print_log(PRINT_RED, "Face {} extents are bad. Map can crash.\n", facenum);
				print_log("TRACE: Mins {} maxs {}\n", mins_out[i], maxs_out[i]);
			}
			retval = false;
			mins_out[i] = 1;
			maxs_out[i] = 1;
		}
	}
	return retval;
}

int Bsp::GetFaceSingleLightmapSizeBytes(int facenum)
{
	int size[2];
	GetFaceLightmapSize(facenum, size);
	BSPFACE32& face = faces[facenum];
	if (face.nStyles[0] == 255)
		return 0;
	return size[0] * size[1] * sizeof(COLOR3);
}

bool Bsp::GetFaceLightmapSize(int facenum, int size[2])
{
	int mins[2];
	int maxs[2];

	bool foundExtents = GetFaceExtents(facenum, mins, maxs);

	size[0] = (maxs[0] - mins[0]);
	size[1] = (maxs[1] - mins[1]);

	size[0] += 1;
	size[1] += 1;
	return foundExtents;
}

int Bsp::GetFaceLightmapSizeBytes(int facenum)
{
	int size[2];
	GetFaceLightmapSize(facenum, size);
	BSPFACE32& face = faces[facenum];

	int lightmapCount = 0;
	for (int k = 0; k < MAX_LIGHTMAPS; k++)
	{
		lightmapCount += face.nStyles[k] != 255;
	}
	return size[0] * size[1] * lightmapCount * sizeof(COLOR3);
}


BSPPLANE Bsp::getPlaneFromFace(const BSPFACE32* const face)
{
	if (!face)
	{
		print_log(get_localized_string(LANG_0990));
		return BSPPLANE();
	}

	if (face->nPlaneSide)
	{
		BSPPLANE backplane = planes[face->iPlane];
		backplane.fDist = -backplane.fDist;
		backplane.vNormal = backplane.vNormal.invert();
		return backplane;
	}
	else
	{
		return planes[face->iPlane];
	}
}

int Bsp::CalcFaceTextureStep(int facenum)
{
	if (is_bsp31)
	{
		return 8;
	}

	// next xash 
	if (is_bsp30ext && extralumps.size())
	{
		BSPTEXTUREINFO& tex = texinfos[faces[facenum].iTextureInfo];

		if (tex.nFlags & TEX_WORLD_LUXELS)
			return 1;

		if (tex.nFlags & TEX_EXTRA_LIGHTMAP)
			return 8;

		short faceInfo = (tex.nFlags >> 16) & 0xFFFF;
		if (faceInfo >= 0 && faceInfo < faceinfoCount)
		{
			return faceinfos[faceInfo].texture_step;
		}
	}

	return g_limits.textureStep;
}

int Bsp::GetTriggerTexture()
{
	unsigned int totalTextures = ((unsigned int*)textures)[0];
	for (unsigned int i = 0; i < totalTextures; i++)
	{
		int texOffset = ((int*)textures)[i + 1];
		if (texOffset >= 0)
		{
			BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));
			if (tex.szName[0] != '\0' && strcasecmp(tex.szName, "aaatrigger") == 0)
			{
				return i;
			}
		}
	}
	return -1;
}

int Bsp::AddTriggerTexture()
{
	//print_log(get_localized_string(LANG_0295));
	return add_texture("aaatrigger", aaatriggerTex->get_data(), aaatriggerTex->width, aaatriggerTex->height);
}
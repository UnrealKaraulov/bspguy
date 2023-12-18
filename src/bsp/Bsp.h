#pragma once
#include <chrono>
#include <ctime> 
#include "Wad.h"
#include "Entity.h"
#include "bsplimits.h"
#include "rad.h"
#include <string.h>
#include "remap.h"
#include <set>
#include "bsptypes.h"
#include "mdl_studio.h"

class BspRenderer;

struct membuf : std::streambuf
{
	membuf(char* begin, int len)
	{
		this->setg(begin, begin, begin + len);
	}
};

enum ExportObjOrder
{
	EXPORT_XYZ,
	EXPORT_XZY
};


struct LeafDebug
{
	int leafIdx;
	unsigned char* leafVIS;
	LeafDebug()
	{
		leafIdx = 0;
		leafVIS = 0;
	}
};

class Bsp
{
public:
	BSPHEADER bsp_header;
	BSPHEADER_EX bsp_header_ex;

	unsigned char** lumps;
	unsigned char** extralumps;

	LIGHTMAP* undo_lightmaps;
	int undo_lightmaps_count;

	bool is_bsp30ext;

	bool is_bsp2;
	bool is_bsp2_old;
	bool is_bsp29;
	bool is_32bit_clipnodes;
	bool is_broken_clipnodes;
	bool is_blue_shift;

	bool force_skip_crc;

	std::vector<Entity*> ents;
	int planeCount;
	int textureCount;
	int textureDataLength;
	int vertCount;
	int lightDataLength;
	int nodeCount;
	int texinfoCount;
	int faceCount;
	int visDataLength;
	int clipnodeCount;
	int leafCount;
	int marksurfCount;
	int edgeCount;
	int surfedgeCount;
	int modelCount;

	BSPPLANE* planes;
	unsigned char* textures;
	vec3* verts;
	unsigned char* lightdata;
	BSPNODE32* nodes;
	BSPTEXTUREINFO* texinfos;
	BSPFACE32* faces;
	unsigned char* visdata;
	BSPCLIPNODE32* clipnodes;
	BSPLEAF32* leaves;
	int* marksurfs;
	BSPEDGE32* edges;
	int* surfedges;
	BSPMODEL* models;

	bool is_colored_lightmap;

	std::string bsp_path;
	std::string bsp_name;

	bool replacedLump[32];

	bool bsp_valid;
	bool is_bsp_model;
	bool is_mdl_model;
	StudioModel* mdl;

	Bsp* parentMap;
	void selectModelEnt();


	Bsp();
	Bsp(std::string fname);
	~Bsp();

	void init_empty_bsp();

	// if modelIdx=0, the world is moved and all entities along with it
	bool move(vec3 offset, int modelIdx = 0, bool onlyModel = false, bool forceMove = false, bool logged = true);

	void move_texinfo(int idx, vec3 offset);
	void write(const std::string& path);

	void print_info(bool perModelStats, int perModelLimit, int sortMode);
	void print_model_hull(int modelIdx, int hull);
	void print_clipnode_tree(int iNode, int depth);
	void recurse_node(int node, int depth);

	void get_last_node(int nodeIdx, int& node, int& count);
	void get_last_clipnode(int nodeIdx, int& node, int& count);

	int pointContents(int iNode, const vec3& p, int hull, std::vector<int>& nodeBranch, int& leafIdx, int& childIdx);
	int pointContents(int iNode, const vec3& p, int hull);
	const char* getLeafContentsName(int contents);

	// strips a collision hull from the given model index
	// and redirects to the given hull, if redirect>0
	void delete_hull(int hull_number, int modelIdx, int redirect);

	// strips a collision hull from all models
	// and redirects to the given hull, if redirect>0
	void delete_hull(int hull_number, int redirect);

	void write_csg_outputs(const std::string& path);

	// get the bounding box for the world
	void get_bounding_box(vec3& mins, vec3& maxs);

	// get the bounding box for all vertexes in a BSP tree
	void get_model_vertex_bounds(int modelIdx, vec3& mins, vec3& maxs);

	// get all verts used by this model
	// TODO: split any verts shared with other models!
	std::vector<TransformVert> getModelVerts(int modelIdx);

	// gets verts formed by plane intersections with the nodes in this model
	bool getModelPlaneIntersectVerts(int modelIdx, std::vector<TransformVert>& outVerts);
	bool getModelPlaneIntersectVerts(int modelIdx, const std::vector<int>& planes, std::vector<TransformVert>& outVerts);
	void getNodePlanes(int iNode, std::vector<int>& nodePlanes);
	void getClipNodePlanes(int iClipNode, std::vector<int>& nodePlanes);
	bool is_convex(int modelIdx);
	bool is_node_hull_convex(int iNode);

	// get cuts required to create bounding volumes for each solid leaf in the model
	std::vector<NodeVolumeCuts> get_model_leaf_volume_cuts(int modelIdx, int hullIdx);
	void get_clipnode_leaf_cuts(int iNode, int iStartNode, std::vector<BSPPLANE>& clipOrder, std::vector<NodeVolumeCuts>& output);
	void get_node_leaf_cuts(int iNode, int iStartNode, std::vector<BSPPLANE>& clipOrder, std::vector<NodeVolumeCuts>& output);

	// this a cheat to recalculate plane normals after scaling a solid. Really I should get the plane
	// intersection code working for nonconvex solids, but that's looking like a ton of work.
	// Scaling/stretching really only needs 3 verts _anywhere_ on the plane to calculate new normals/origins.
	std::vector<ScalableTexinfo> getScalableTexinfos(int modelIdx); // for scaling
	int addTextureInfo(BSPTEXTUREINFO& copy);

	// fixes up the model planes/nodes after vertex posisions have been modified
	// returns false if the model has non-planar faces
	// TODO: split any planes shared with other models
	bool vertex_manipulation_sync(int modelIdx, std::vector<TransformVert>& hullVerts, bool convexCheckOnly);

	void load_ents();

	// call this after editing ents
	void update_ent_lump(bool stripNodes = false);

	vec3 get_model_center(int modelIdx);

	// returns the number of lightmaps applied to the face, or 0 if it has no lighting
	int lightmap_count(int faceIdx);

	bool isValid(); // check if any lumps are overflowed

	// delete structures not used by the map (needed after deleting models/hulls)
	STRUCTCOUNT remove_unused_model_structures(unsigned int target = CLEAN_LIGHTMAP | CLEAN_PLANES | CLEAN_NODES | CLEAN_CLIPNODES |
											   CLEAN_LEAVES | CLEAN_MARKSURFACES | CLEAN_FACES | CLEAN_SURFEDGES | CLEAN_TEXINFOS |
											   CLEAN_EDGES | CLEAN_VERTICES | CLEAN_TEXTURES | CLEAN_VISDATA);
	void delete_model(int modelIdx);
	void clean_unused_texinfos();
	int merge_all_verts(float epsilon = 1.0f);

	// conditionally deletes hulls for entities that aren't using them
	STRUCTCOUNT delete_unused_hulls(bool noProgress = false);

	// returns true if the map has eny entities that make use of hull 2
	bool has_hull2_ents();

	// check for bad indexes
	bool validate();

	// creates a solid cube
	int create_solid(const vec3& mins, const vec3& maxs, int textureIdx, bool empty = false);

	// creates a new solid from the given solid definition (must be convex).
	int create_solid(Solid& solid, int targetModelIdx = -1);

	int create_leaf(int contents);
	void create_node_box(const vec3& mins, const vec3& maxs, BSPMODEL* targetModel, int textureIdx);
	void create_nodes(Solid& solid, BSPMODEL* targetModel);
	// returns index of the solid node
	int create_clipnode_box(const vec3& mins, const vec3& maxs, BSPMODEL* targetModel, int targetHull = 0, bool skipEmpty = false, bool empty = false);
	// copies a model from the sourceMap into this one
	void add_model(Bsp* sourceMap, int modelIdx);

	// create a new texture from raw RGB data, and embeds into the bsp. 
	// Returns -1 on failure, else the new texture index
	int add_texture(const char* name, unsigned char* data, int width, int height, const unsigned char* custompal = NULL, bool force_quake_pal = false);
	int add_texture(WADTEX* tex);

	void replace_lump(int lumpIdx, void* newData, size_t newLength);
	void append_lump(int lumpIdx, void* newData, size_t appendLength);

	bool is_invisible_solid(Entity* ent);

	// replace a model's clipnode hull with a axis-aligned bounding box
	void simplify_model_collision(int modelIdx, int hullIdx);

	// for use after scaling a model. Convex only.
	// Skips axis-aligned planes (bounding box should have been generated beforehand)
	void regenerate_clipnodes(int modelIdx, int hullIdx);
	int regenerate_clipnodes_from_nodes(int iNode, int hullIdx);

	int create_clipnode();
	int create_plane();
	int create_model();
	int create_texinfo();

	void copy_bsp_model(int modelIdx, Bsp* targetMap, STRUCTREMAP& remap, std::vector<BSPPLANE>& newPlanes, std::vector<vec3>& newVerts,
						std::vector<BSPEDGE32>& newEdges, std::vector<int>& newSurfedges, std::vector<BSPTEXTUREINFO>& newTexinfo,
						std::vector<BSPFACE32>& newFaces, std::vector<COLOR3>& newLightmaps, std::vector<BSPNODE32>& newNodes,
						std::vector<BSPCLIPNODE32>& newClipnodes, std::vector<WADTEX *>& newTextures);

	int duplicate_model(int modelIdx);
	void duplicate_model_structures(int modelIdx);
	int add_model_to_worldspawn(int modelIdx);

	// if the face's texinfo is not unique, a new one is created and returned. Otherwise, it's current texinfo is returned
	BSPTEXTUREINFO* get_unique_texinfo(int faceIdx);

	bool is_unique_texinfo(int faceIdx);

	int get_model_from_face(int faceIdx);

	bool is_worldspawn_ent(int entIdx);

	int get_ent_from_model(int modelIdx);

	void decalShoot(vec3 pos, const char* texname);

	std::vector<STRUCTUSAGE*> get_sorted_model_infos(int sortMode);

	// split structures that are shared between the target and other models
	void split_shared_model_structures(int modelIdx);

	// true if the model is sharing planes/clipnodes with other models
	bool does_model_use_shared_structures(int modelIdx);

	// returns the current lump contents
	LumpState duplicate_lumps(int targets);

	void replace_lumps(const LumpState& state);

	int delete_embedded_textures();

	BSPMIPTEX* find_embedded_texture(const char* name, int& texid);
	BSPMIPTEX* find_embedded_wad_texture(const char* name, int& texid);

	void update_lump_pointers();

	int getBspRenderId();
	BspRenderer* getBspRender();
	void setBspRender(BspRenderer* rnd);


	void ExportToObjWIP(const std::string& path, ExportObjOrder order = ExportObjOrder::EXPORT_XYZ, int iscale = 1);

	void ExportToMapWIP(const std::string& path);

	void ExportPortalFile();
	void ExportExtFile();
	void ExportLightFile();
	void ImportLightFile();
	bool isModelHasFaceIdx(const BSPMODEL& mdl, int faceid);

	void hideEnts(bool hide = true);

	std::vector<int> getLeafFaces(int leafIdx);
	std::vector<int> getLeafFaces(BSPLEAF32& leaf);
	std::vector<int> getFaceLeafs(int faceIdx);
	int getFaceFromPlane(int iPlane);
	std::vector<int> getFacesFromPlane(int iPlane);
	bool is_texture_with_pal(int textureid);
	int getBspTextureSize(int textureid);

	void save_undo_lightmaps(bool logged = false);
	void resize_all_lightmaps(bool logged = false);
	bool should_resize_lightmap(LIGHTMAP& oldLightmap, LIGHTMAP& newLightmap);
	int get_new_lightmaps_data_size();

private:
	unsigned int remove_unused_lightmaps(bool* usedFaces);
	unsigned int remove_unused_visdata(bool* usedLeaves, BSPLEAF32* oldLeaves, int oldWorldLeaves, int oldLeavesMemSize); // called after removing unused leaves
	unsigned int remove_unused_textures(bool* usedTextures, int* remappedIndexes, int * removeddata = NULL);
	unsigned int remove_unused_structs(int lumpIdx, bool* usedStructs, int* remappedIndexes);


	bool load_lumps(std::string fname);

	// lightmaps that are resized due to precision errors should not be stretched to fit the new canvas.
	// Instead, the texture should be shifted around, depending on which parts of the canvas is "lit" according
	// to the qrad code. Shifts apply to one or both of the lightmaps, depending on which dimension is bigger.
	void get_lightmap_shift(const LIGHTMAP& oldLightmap, const LIGHTMAP& newLightmap, int& srcOffsetX, int& srcOffsetY);

	void print_model_bsp(int modelIdx);
	void print_leaf(const BSPLEAF32& leaf);
	void print_node(const BSPNODE32& node);
	void print_stat(const std::string& name, unsigned int val, unsigned int max, bool isMem);
	void print_model_stat(STRUCTUSAGE* modelInfo, unsigned int val, int max, bool isMem);

	std::string get_model_usage(int modelIdx);
	std::vector<Entity*> get_model_ents(int modelIdx);
	std::vector<int> get_model_ents_ids(int modelIdx);

	void write_csg_polys(int nodeIdx, FILE* fout, int flipPlaneSkip, bool debug);

	// marks all structures that this model uses
	// TODO: don't mark faces in submodel leaves (unused)
	void mark_model_structures(int modelIdx, STRUCTUSAGE* STRUCTUSAGE, bool skipLeaves, bool makeSomething = false);
	void mark_face_structures(int iFace, STRUCTUSAGE* usage);
	void mark_node_structures(int iNode, STRUCTUSAGE* usage, bool skipLeaves);
	void mark_clipnode_structures(int iNode, STRUCTUSAGE* usage);

	// remaps structure indexes to new locations
	void remap_face_structures(int faceIdx, STRUCTREMAP* remap);
	void remap_model_structures(int modelIdx, STRUCTREMAP* remap);
	void remap_node_structures(int iNode, STRUCTREMAP* remap);
	void remap_clipnode_structures(int iNode, STRUCTREMAP* remap);

	BspRenderer* renderer;
	unsigned int originCrc32 = 0;
};

void remove_unused_wad_files(Bsp* baseMap, Bsp* targetMap, int tex_type = 0);
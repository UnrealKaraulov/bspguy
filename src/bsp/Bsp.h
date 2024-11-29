#pragma once
#include <chrono>
#include <ctime> 
#include <set>
#include <string.h>

#include "Wad.h"
#include "Entity.h"
#include "bsplimits.h"
#include "rad.h"
#include "remap.h"
#include "bsptypes.h"
#include "mdl_studio.h"
#include "Sprite.h"
#include "XASH_csm.h"
#include "Polygon3D.h"

class BspRenderer;
#define OOB_CLIP_X 1
#define OOB_CLIP_X_NEG 2
#define OOB_CLIP_Y 4
#define OOB_CLIP_Y_NEG 8
#define OOB_CLIP_Z 16
#define OOB_CLIP_Z_NEG 32

struct membuf : std::streambuf
{
	membuf(char* begin, int len)
	{
		this->setg(begin, begin, begin + len);
	}
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

extern size_t totalBspStructs;

class Bsp
{
public:
	BSPHEADER bsp_header;
	BSPHEADER_EX bsp_header_ex;

	unsigned char** lumps;
	unsigned char** extralumps;

	std::vector<LIGHTMAP> undo_lightmaps;

	bool is_protected;

	bool is_bsp30ext;

	bool is_bsp31;

	bool is_bsp_pathos;
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
	int faceinfoCount;
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
	BSPFACE_INFOEX* faceinfos;
	unsigned char* visdata;
	BSPCLIPNODE32* clipnodes;
	BSPLEAF32* leaves;
	int* marksurfs;
	BSPEDGE32* edges;
	int* surfedges;
	BSPMODEL* models;

	bool is_colored_lightmap;
	bool is_texture_has_pal;
	bool target_save_texture_has_pal;

	std::string bsp_path;
	std::string bsp_name;

	bool replacedLump[32];

	vec3 save_cam_pos, save_cam_angles;

	bool bsp_valid;
	bool is_bsp_model;
	bool is_mdl_model;

	StudioModel* map_mdl;
	Sprite* map_spr;
	CSMFile* map_csm;

	Bsp* parentMap;
	void selectModelEnt();


	Bsp();
	Bsp(std::string fname);
	~Bsp();

	void init_empty_bsp();

	// if modelIdx=0, the world is moved and all entities along with it
	bool move(vec3 offset, int modelIdx = 0, bool onlyModel = false, bool forceMove = false, bool logged = true);

	void move_texinfo(BSPTEXTUREINFO& info, vec3 offset);
	void move_texinfo(int idx, vec3 offset);
	void write(const std::string& path);

	void print_info(bool perModelStats, int perModelLimit, int sortMode);
	void print_model_hull(int modelIdx, int hull);
	void print_clipnode_tree(int iNode, int depth);
	void recurse_node_print(int node, int depth);

	void get_last_node(int nodeIdx, int& node, int& count, int last_node = -1);
	void get_last_clipnode(int nodeIdx, int& node, int& count, int last_node = -1);	// get leaf index from world position
	int get_leaf(vec3 pos, int hull);

	int pointContents(int iNode, const vec3& p, int hull, std::vector<int>& nodeBranch, int& leafIdx, int& childIdx);
	int modelLeafs(int modelIdx, std::vector<int>& modelLeafs);
	int modelLeafs(const BSPMODEL& model, std::vector<int>& modelLeafs);
	int pointContents(int iNode, const vec3& p, int hull);
	bool recursiveHullCheck(int hull, int num, float p1f, float p2f, vec3 p1, vec3 p2, TraceResult* trace);
	void traceHull(vec3 start, vec3 end, int hull, TraceResult* ptr);
	int pointLeaf(int iNode, const vec3& p, int hull, int& leafIdx, int & planeIdx);
	std::vector<int> getLeafsFromPos(const vec3& p, float radius);
	const char* getLeafContentsName(int contents);
	// returns true if leaf is in the PVS from the given position
	bool is_leaf_visible(int ileaf, vec3 pos);

	bool is_face_visible(int faceIdx, vec3 pos, vec3 angles);
	int count_visible_polys(vec3 pos, vec3 angles);

	// strips a collision hull from the given model index
	// and redirects to the given hull, if redirect>0
	void delete_hull(int hull_number, int modelIdx, int redirect);

	// strips a collision hull from all models
	// and redirects to the given hull, if redirect>0
	void delete_hull(int hull_number, int redirect);

	void write_csg_outputs(const std::string& path);

	// get the bounding box for the world
	void get_bounding_box(vec3& mins, vec3& maxs);
	void get_bounding_box(int modelidx, vec3& mins, vec3& maxs);

	// get the bounding box for all vertexes in a BSP tree
	void get_model_vertex_bounds(int modelIdx, vec3& mins, vec3& maxs, bool skipSpecial = false);

	// face has duplicate verts, this is bad?
	bool is_face_duplicate_edges(int faceIdx);
	void face_fix_duplicate_edges_index(int faceIdx);

	// get all verts used by this model
	// TODO: split any verts shared with other models!
	std::vector<TransformVert> getModelTransformVerts(int modelIdx);
	std::vector<int> getModelVertsIds(int modelIdx);

	// gets verts formed by plane intersections with the nodes in this model
	bool getModelPlaneIntersectVerts(int modelIdx, std::vector<TransformVert>& outVerts);
	bool getModelPlaneIntersectVerts(int modelIdx, const std::vector<int>& planes, std::vector<TransformVert>& outVerts);
	void getNodePlanes(int iNode, std::vector<int>& nodePlanes);
	void getClipNodePlanes(int iClipNode, std::vector<int>& nodePlanes);
	bool is_convex(int modelIdx);
	bool is_node_hull_convex(int iNode);

	// true if the center of this face is touching an empty leaf
	bool isInteriorFace(const Polygon3D& poly, int hull);

	// get cuts required to create bounding volumes for each solid leaf in the model
	std::vector<NodeVolumeCuts> get_model_leaf_volume_cuts(int modelIdx, int hullIdx, int contents);
	void get_clipnode_leaf_cuts(int iNode, std::vector<BSPPLANE>& clipOrder, std::vector<NodeVolumeCuts>& output, int contents);
	void get_node_leaf_cuts(int iNode, std::vector<BSPPLANE>& clipOrder, std::vector<NodeVolumeCuts>& output, int contents);


	void get_leaf_nodes(int leaf, std::vector<int>& out_nodes);

	// this a cheat to recalculate plane normals after scaling a solid. Really I should get the plane
	// intersection code working for nonconvex solids, but that's looking like a ton of work.
	// Scaling/stretching really only needs 3 verts _anywhere_ on the plane to calculate new normals/origins.
	std::vector<ScalableTexinfo> getScalableTexinfos(int modelIdx); // for scaling
	int addTextureInfo(BSPTEXTUREINFO& copy);

	// fixes up the model planes/nodes after vertex posisions have been modified
	// returns false if the model has non-planar faces
	// TODO: split any planes shared with other models
	bool vertex_manipulation_sync(int modelIdx, const std::vector<TransformVert>& hullVerts, bool convexCheckOnly);

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
		CLEAN_EDGES | CLEAN_VERTICES | CLEAN_TEXTURES | CLEAN_VISDATA | CLEAN_MODELS);
	void delete_model(int modelIdx);
	int merge_all_texinfos();
	int merge_all_verts(float epsilon = 1.0f);
	void round_all_verts(int digits = 8);

	// conditionally deletes hulls for entities that aren't using them
	STRUCTCOUNT delete_unused_hulls(bool noProgress = false);

	
	// deletes data outside the map bounds
	void delete_oob_data(int clipFlags);

	void delete_oob_clipnodes(int iNode, int* parentBranch, std::vector<BSPPLANE>& clipOrder, 
		int oobFlags, bool* oobHistory, bool isFirstPass, int& removedNodes);
	
	void delete_oob_nodes(int iNode, int* parentBranch, std::vector<BSPPLANE>& clipOrder,
		int oobFlags, bool* oobHistory, bool isFirstPass, int& removedNodes);

	// deletes data inside a bounding box
	void delete_box_data(vec3 clipMins, vec3 clipMaxs);
	void delete_box_clipnodes(int iNode, int* parentBranch, std::vector<BSPPLANE>& clipOrder,
		vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes);
	void delete_box_nodes(int iNode, int* parentBranch, std::vector<BSPPLANE>& clipOrder,
		vec3 clipMins, vec3 clipMaxs, bool* oobHistory, bool isFirstPass, int& removedNodes);

	// assumes contiguous leaves starting at 0. Only works for worldspawn, which is the only model which
	// should have leaves anyway.
	void count_leaves(int iNode, int& leafCount);

	// searches for entities that have very similar models,
	// then updates the entities to share a single model reference
	// this reduces the precached model count even though the models are still present in the bsp
	void deduplicate_models();
	
	// scales up texture axes for any face with bad surface extents
	// connected planar faces which use the same texture will also be scaled up to prevent seams
	// showing between faces with different texture scales
	// scaleNotSubdivide:true = scale face textures to lower extents
	// scaleNotSubdivide:false = subdivide face textures to lower extents
	// downscaleOnly:true = don't scale or subdivide anything, just downscale the textures
	// maxTextureDim = downscale textures first if they are larger than this (0 = disable)
	void fix_bad_surface_extents(bool scaleNotSubdivide, bool downscaleOnly, int maxTextureDim);

	// subdivide a face until it has valid surface extents
	void fix_bad_surface_extents_with_subdivide(int faceIdx);

	// reduces size of textures that exceed game limits and adjusts face scales accordingly
	void downscale_invalid_textures();

	void downscale_textures(int maxDim);

	// downscales a texture to the maximum specified width/height
	// allowWad:true = texture coordinates will be scaled even if the the texture is from a WAD and must be scaled separately
	// returns true if was downscaled
	bool downscale_texture(int textureId, int maxDim, bool allowWad);

	bool downscale_texture(int textureId, int newWidth, int newHeight);

	bool rename_texture(const char* oldName, const char* newName);

	// updates texture coordinates after a texture has been downscaled
	void adjust_downscaled_texture_coordinates(int textureId, int oldWidth, int oldHeight);

	vec3 get_face_center(int faceIdx);

	// scales up texture sizes on models that aren't used by visible entities
	void allocblock_reduction();

	// gets estimated number of allocblocks filled
	// actual amount will vary because there is some wasted space when the engine generates lightmap atlases
	float calc_allocblock_usage();

	// subdivides along the axis with the most texture pixels (for biggest surface extent reduction)
	bool subdivide_face(int faceIdx);

	// select faces connected to the given one, which lie on the same plane and use the same texture
	std::set<int> selectConnectedTexture(int modelId, int faceId);

	// returns true if the map has eny entities that make use of hull 2
	bool has_hull2_ents();

	// check for bad indexes
	bool validate();

	// creates a solid cube
	int create_solid(const vec3& mins, const vec3& maxs, int textureIdx, bool empty = false);

	// creates a new solid from the given solid definition (must be convex).
	int create_solid(Solid& solid, int targetModelIdx = -1);

	int create_leaf(int contents);
	int create_leaf_back(int contents);
	void create_inside_box(const vec3& min, const vec3& max, BSPMODEL* targetModel, int textureIdx);
	void create_primitive_box(const vec3& mins, const vec3& maxs, BSPMODEL* targetModel, int textureIdx, bool inside = false);
	void create_solid_nodes(Solid& solid, BSPMODEL* targetModel);
	// returns index of the solid node

	int create_node_box(const vec3& mins, const vec3& maxs, BSPMODEL* targetModel, bool empty = false, int leafIdx = 0);
	int create_clipnode_box(const vec3& mins, const vec3& maxs, BSPMODEL* targetModel, int targetHull = 0, bool skipEmpty = false, bool empty = false);
	// copies a model from the sourceMap into this one
	void add_model(Bsp* sourceMap, int modelIdx);

	// create a new texture from raw RGB data, and embeds into the bsp. 
	// Returns -1 on failure, else the new texture index
	int add_texture(const char* name, unsigned char* data, int width, int height, bool force_custompal = false);
	int add_texture(WADTEX* tex, bool embedded = false);

	bool export_wad_to_pngs(const std::string& wadpath, const std::string& targetdir);
	bool import_textures_to_wad(const std::string& wadpath, const std::string& texpath, bool dithering);

	void replace_lump(int lumpIdx, void* newData, size_t newLength);
	void append_lump(int lumpIdx, void* newData, size_t appendLength);

	bool is_invisible_solid(Entity* ent);

	// replace a model's clipnode hull with a axis-aligned bounding box
	void simplify_model_collision(int modelIdx, int hullIdx);

	// for use after scaling a model. Convex only.
	// Skips axis-aligned planes (bounding box should have been generated beforehand)
	bool regenerate_clipnodes(int modelIdx, int hullIdx);
	int regenerate_clipnodes_from_nodes(int iNode, int hullIdx, bool& success);

	int create_node(bool force_reversed = false, int reversed_id = 0);
	int create_clipnode(bool force_reversed = false, int reversed_id = 0);
	int create_plane();
	int create_model();
	int create_vert();
	int create_texinfo();
	int create_edge();
	int create_surfedge();

	void copy_bsp_model(int modelIdx, Bsp* targetMap, STRUCTREMAP& remap, STRUCTUSAGE& usage, std::vector<BSPPLANE>& newPlanes, std::vector<vec3>& newVerts,
		std::vector<BSPEDGE32>& newEdges, std::vector<int>& newSurfedges, std::vector<BSPTEXTUREINFO>& newTexinfo,
		std::vector<BSPFACE32>& newFaces, std::vector<COLOR3>& newLightmaps, std::vector<BSPNODE32>& newNodes,
		std::vector<BSPCLIPNODE32>& newClipnodes, std::vector<WADTEX*>& newTextures, std::vector<BSPLEAF32> & newLeafs, std::vector<int>& newMarkSurfs, bool forExport = false);

	int duplicate_model(int modelIdx);
	void duplicate_model_structures(int modelIdx);
	bool cull_leaf_faces(int leafIdx);
	bool leaf_add_face(int faceIdx, int leafIdx);
	bool leaf_del_face(int faceIdx, int leafIdx);
	bool remove_face(int faceid);
	void remove_faces_by_content(int content);
	std::vector<int> getFaceContents(int faceIdx);
	int clone_world_leaf(int oldleafIdx);
	int merge_two_models_ents(int src_ent, int dst_ent, int &try_again);
	int merge_two_models_idx(int src_model, int dst_model, int &try_again);

	// if the face's texinfo is not unique, a new one is created and returned. Otherwise, it's current texinfo is returned
	BSPTEXTUREINFO* get_unique_texinfo(int faceIdx);

	bool is_unique_texinfo(int faceIdx);

	int get_model_from_face(int faceIdx);
	int get_model_from_leaf(int leafIdx);

	std::vector<int> get_face_edges(int faceIdx);
	std::vector<vec3> get_face_verts(int faceIdx, int limited = INT_MAX);
	std::vector<int> get_face_verts_idx(int faceIdx, int limited = INT_MAX);

	bool is_worldspawn_ent(int entIdx);

	int get_ent_from_model(int modelIdx);

	void decalShoot(vec3 pos, const std::string& texname);

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

	void ExportToSmdWIP(const std::string& path, bool split, bool oneRoot);

	void ExportToObjWIP(const std::string& path, int iscale = 1, bool lightmap_mode = false, bool with_mdl = false, bool export_csm = false, int grouping = 0);

	void ExportToMapWIP(const std::string& path, bool selected, bool merge_faces, bool use_one_back_vert, bool create_worldbox);

	int import_mdl_to_bspmodel(int ent, int generateClipnodes);
	int import_mdl_to_bspmodel(std::vector<StudioMesh>& meshes,mat4x4 angles, bool & valid_nodes);

	int merge_all_planes();

	void ExportPortalFile(const std::string& path);
	void ExportExtFile(const std::string& path, std::string& out_map_path);
	void ExportLightFile(const std::string& path);
	void ImportLightFile(const std::string& path);

	bool ImportWad(const std::string& path);
	bool ExportEmbeddedWad(const std::string& path);

	bool isModelHasFaceIdx(const BSPMODEL& mdl, int faceid);
	bool isModelHasLeafIdx(const BSPMODEL& bspmdl, int leafidx);

	void hideEnts(bool hide = true);

	std::vector<int> getLeafFaces(int leafIdx);
	std::vector<int> getLeafFaces(BSPLEAF32& leaf);
	std::vector<int> getFaceLeafs(int faceIdx);
	int getFaceFromPlane(int iPlane);
	int getFaceFromVec(const vec3& pos, int modelIdx, int& content);
	std::vector<int> getFacesFromPlane(int iPlane);
	bool is_texture_with_pal(int textureid);
	int getBspTextureSize(int textureid);
	int getEmbeddedTexCount();

	int getWorlspawnEntId();
	Entity * getWorldspawnEnt();

	void save_undo_lightmaps(bool logged = false);
	void resize_all_lightmaps(bool logged = false);
	bool should_resize_lightmap(LIGHTMAP& oldLightmap, LIGHTMAP& newLightmap);

	// marks all structures that this model uses
	// TODO: don't mark faces in submodel leaves (unused)
	void mark_model_structures(int modelIdx, STRUCTUSAGE* STRUCTUSAGE, bool skipLeaves);
	void mark_face_structures(int iFace, STRUCTUSAGE* usage);
	void mark_node_structures(int iNode, STRUCTUSAGE* usage, bool skipLeaves);
	void mark_clipnode_structures(int iNode, STRUCTUSAGE* usage);

	BSPPLANE getPlaneFromFace(const BSPFACE32* const face);
	bool GetFaceLightmapSize(int facenum, int size[2]);
	int GetFaceLightmapSizeBytes(int facenum);
	int GetFaceSingleLightmapSizeBytes(int facenum);
	bool GetFaceExtents(int facenum, int mins_out[2], int maxs_out[2]);
	bool CalcFaceExtents(lightinfo_t* l);

	int CalcFaceTextureStep(int facenum);

	unsigned int remove_unused_lightmaps(std::vector<bool> & usedFaces);
	unsigned int remove_unused_visdata(BSPLEAF32* oldLeaves, int oldWorldLeaves, int oldLeavesMemSize); // called after removing unused leaves
	unsigned int remove_unused_textures(std::vector<bool>& usedTextures, std::vector<int>& remappedIndexes, int* removeddata = NULL);
	unsigned int remove_unused_structs(int lumpIdx, std::vector<bool>& usedStructs, std::vector<int>& remappedIndexes);

	void recurse_node_leafs(int nodeIdx, std::vector<int>& outLeafs);

	bool load_lumps(const std::string & fname);

	void print_model_bsp(int modelIdx);
	void print_leaf(const BSPLEAF32& leaf);
	void print_leaf(int leaf);
	void print_node(const BSPNODE32& node);
	void print_stat(const std::string& name, unsigned int val, unsigned int max, bool isMem);
	void print_model_stat(STRUCTUSAGE* modelInfo, unsigned int val, int max, bool isMem);

	std::string get_model_usage(int modelIdx);
	std::vector<Entity*> get_model_ents(int modelIdx);
	std::vector<int> get_model_ents_ids(int modelIdx);

	void write_csg_polys(int nodeIdx, FILE* fout, int flipPlaneSkip, bool debug);


	// remaps structure indexes to new locations
	void remap_face_structures(int faceIdx, STRUCTREMAP* remap);
	void remap_model_structures(int modelIdx, STRUCTREMAP* remap);
	void remap_node_structures(int iNode, STRUCTREMAP* remap);
	void remap_clipnode_structures(int iNode, STRUCTREMAP* remap);

	BspRenderer* renderer;
	unsigned int originCrc32 = 0;

	bool* pvsFaces = NULL; // flags which faces are marked for rendering in the PVS
	int pvsFaceCount = 0;
	size_t realIdx = 0;
};

void update_unused_wad_files(Bsp* baseMap, Bsp* targetMap, int tex_type = 0);
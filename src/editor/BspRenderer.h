#pragma once
#pragma once
#include "Bsp.h"
#include "Texture.h"
#include "ShaderProgram.h"
#include "LightmapNode.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "PointEntRenderer.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <future>
#include "mdl_studio.h"
#include "Sprite.h"

class Command;

enum RENDER_PASS : int
{
	REND_PASS_MODELSHADER,
	REND_PASS_COLORSHADER,
	REND_PASS_BSPSHADER_TRANSPARENT
};

struct LightmapInfo
{
	// each face can have 4 lightmaps, and those may be split across multiple atlases
	int atlasId[MAX_LIGHTMAPS];
	int x[MAX_LIGHTMAPS];
	int y[MAX_LIGHTMAPS];

	int w, h;

	float midTexU, midTexV;
	float midPolyU, midPolyV;
};

struct FaceMath
{
	mat4x4 worldToLocal; // transforms world coordiantes to this face's plane's coordinate system
	vec3 normal;
	float fdist;
	std::vector<vec2> localVerts;
	FaceMath()
	{
		worldToLocal = mat4x4();
		normal = vec3();
		fdist = 0.0f;
		localVerts = {};
	}
};

struct RenderEnt
{
	mat4x4 modelMat4x4; // model matrix for rendering
	mat4x4 modelMat4x4_angles; // model matrix for rendering with angles
	mat4x4 modelMat4x4_calc;
	mat4x4 modelMat4x4_calc_angles;
	vec3 offset; // vertex transformations for picking
	vec3 angles; // support angles
	int modelIdx; // -1 = point entity
	EntCube* pointEntCube;
	bool needAngles;
	bool isDuplicateModel;
	StudioModel* mdl;
	Sprite* spr;
	std::string mdlFileName;
	RenderEnt() : modelMat4x4(mat4x4()), modelMat4x4_calc(mat4x4()), modelMat4x4_angles(mat4x4()), modelMat4x4_calc_angles(mat4x4()), offset(vec3()), angles(vec3())
	{
		isDuplicateModel = false;
		needAngles = false;
		modelIdx = 0;
		pointEntCube = NULL;
		mdl = NULL;
		mdlFileName = "";
		spr = NULL;
	}
};

struct RenderGroup
{
	Texture* texture;
	Texture* lightmapAtlas[MAX_LIGHTMAPS];
	VertexBuffer* buffer;
	bool transparent;
	bool special;
	RenderGroup()
	{
		buffer = NULL;
		transparent = special = false;
		texture = NULL;
		for (int i = 0; i < MAX_LIGHTMAPS; i++)
		{
			lightmapAtlas[i] = NULL;
		}
	}
};

struct RenderFace
{
	int group;
	int vertOffset;
	int vertCount;
	RenderFace()
	{
		group = vertOffset = vertCount = 0;
	}
};

struct RenderModel
{
	int groupCount;
	int renderFaceCount;

	RenderFace* renderFaces;
	RenderGroup* renderGroups;
	VertexBuffer* wireframeBuffer;

	bool highlighted;

	RenderModel()
	{
		groupCount = renderFaceCount = 0;
		renderFaces = NULL;
		renderGroups = NULL;
		wireframeBuffer = NULL;
		highlighted = false;
	}
};

struct RenderClipnodes
{
	VertexBuffer* clipnodeBuffer[MAX_MAP_HULLS];
	VertexBuffer* wireframeClipnodeBuffer[MAX_MAP_HULLS];
	std::vector<FaceMath> faceMaths[MAX_MAP_HULLS];
	RenderClipnodes()
	{
		for (int i = 0; i < MAX_MAP_HULLS; i++)
		{
			clipnodeBuffer[i] = NULL;
			wireframeClipnodeBuffer[i] = NULL;
			faceMaths[i].clear();
		}
	}
	~RenderClipnodes()
	{
		for (int i = 0; i < MAX_MAP_HULLS; i++)
		{
			clipnodeBuffer[i] = NULL;
			wireframeClipnodeBuffer[i] = NULL;
			faceMaths[i].clear();
		}
	}
};

class PickInfo
{
public:
	std::vector<size_t> selectedEnts;
	std::vector<size_t> selectedFaces;

	float bestDist;
	PickInfo();

	void AddSelectedEnt(size_t entIdx);
	void SetSelectedEnt(size_t entIdx);
	void DelSelectedEnt(size_t entIdx);
	bool IsSelectedEnt(size_t entIdx);
};

class BspRenderer
{
public:
	Bsp* map;
	vec3 mapOffset;
	vec3 renderOffset;
	vec3 localCameraOrigin;

	int curLeafIdx;

	bool lightEnableFlags[4] = { true,true,true,true };
	std::vector<Wad*> wads;
	bool texturesLoaded = false;
	bool needReloadDebugTextures = false;

	BspRenderer(Bsp* map);
	~BspRenderer();

	void render(bool highlightAlwaysOnTop, int clipnodeHull);

	void drawModel(RenderEnt* ent, int transparent, bool highlight, bool edgesOnly);
	void drawModelClipnodes(int modelIdx, bool highlight, int hullIdx);
	void drawPointEntities(std::vector<size_t> highlightEnts, int pass);

	bool pickPoly(vec3 start, const vec3& dir, int hullIdx, PickInfo& pickInfo, Bsp** map);
	bool pickModelPoly(vec3 start, const vec3& dir, vec3 offset, int modelIdx, int hullIdx, PickInfo& pickInfo);
	bool pickFaceMath(const vec3& start, const vec3& dir, FaceMath& faceMath, float& bestDist);

	void setRenderAngles(size_t entIdx, vec3 angles);
	void refreshEnt(size_t entIdx);
	int refreshModel(int modelIdx, bool refreshClipnodes = true, bool noTriangulate = false);
	bool refreshModelClipnodes(int modelIdx);
	void refreshFace(int faceIdx);
	void updateClipnodeOpacity(unsigned char newValue);

	void reload(); // reloads all geometry, textures, and lightmaps
	void reloadLightmaps();
	void reloadClipnodes();
	RenderClipnodes* addClipnodeModel(int modelIdx);

	// calculate vertex positions and uv coordinates once for faster rendering
	// also combines faces that share similar properties into a single buffer
	void preRenderFaces();
	void preRenderEnts();
	void calcFaceMaths();

	void loadTextures(); // will reload them if already loaded
	void reloadTextures();
	void reuploadTextures();

	void updateLightmapInfos();
	bool isFinishedLoading();

	void highlightFace(size_t faceIdx, int highlight, bool reupload = true);
	void updateFaceUVs(int faceIdx);
	unsigned int getFaceTextureId(int faceIdx);

	bool getRenderPointers(int faceIdx, RenderFace** renderFace, RenderGroup** renderGroup);

	vec3 old_rend_offs;

	LightmapInfo* lightmaps;
	std::vector<RenderEnt> renderEnts;
	std::vector<RenderModel> renderModels;
	std::vector<RenderClipnodes> renderClipnodes;
	FaceMath* faceMaths;
	EntCube* leafCube;
	EntCube* nodeCube;/*
	EntCube* nodePlaneCube;*/

	size_t numLightmapAtlases;

	int numRenderModels;
	int numRenderClipnodes;
	int numRenderLightmapInfos;
	int numFaceMaths;
	int numLoadedTextures;


	Texture** glTextures = NULL;
	// textures loaded in a separate thread
	Texture** glTexturesSwap;

	Texture** glLightmapTextures = NULL;
	std::future<void> texturesFuture;

	bool lightmapsGenerated = false;
	bool lightmapsUploaded = false;
	std::future<void> lightmapFuture;
	bool clipnodesLoaded = false;
	int clipnodeLeafCount = 0;
	std::future<void> clipnodesFuture;

	void loadLightmaps();
	void genRenderFaces(int& renderModelCount);
	void addNewRenderFace();
	void loadClipnodes();
	void generateClipnodeBufferForHull(int modelIdx, int hullId);
	void generateClipnodeBuffer(int modelIdx);
	void deleteRenderModel(RenderModel* renderModel);
	void deleteRenderModelClipnodes(RenderClipnodes* renderClip);
	void deleteRenderClipnodes();
	void deleteRenderFaces();
	void deleteTextures();
	void deleteLightmapTextures();
	void deleteFaceMaths();
	void delayLoadData();
	int getBestClipnodeHull(int modelIdx);

	size_t undoMemoryUsage = 0; // approximate space used by undo+redo history
	std::vector<Command*> undoHistory;
	std::vector<Command*> redoHistory;
	std::map<size_t, Entity> undoEntityStateMap;
	LumpState undoLumpState{};

	struct DelayEntUndo
	{
		std::string description;
		size_t entIdx;
		Entity* ent;
	};

	std::vector<DelayEntUndo> delayEntUndoList;

	void pushModelUndoState(const std::string& actionDesc, unsigned int targets);
	void pushEntityUndoState(const std::string& actionDesc, size_t entIdx);
	void pushEntityUndoStateDelay(const std::string& actionDesc, size_t entIdx, Entity* ent);
	void pushUndoCommand(Command* cmd);
	void undo();
	void redo();
	void clearUndoCommands();
	void clearRedoCommands();
	void calcUndoMemoryUsage();
	void saveEntityState(size_t entIdx);
	void saveLumpState();
	void clearDrawCache();

	vec3 renderCameraOrigin;
	vec3 renderCameraAngles;

	vec3 intersectVec;
private:

	struct nodeBuffStr
	{
		int modelIdx = 0;
		int hullIdx = 0;
		nodeBuffStr()
		{
			modelIdx = 0;
			hullIdx = 0;
		}
	};

	std::map<int, nodeBuffStr> nodesBufferCache, clipnodesBufferCache;

	std::set<int> drawedNodes;
	std::set<int> drawedClipnodes;
};
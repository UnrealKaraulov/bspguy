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

class Command;

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
		localVerts = std::vector<vec2>();
	}
	~FaceMath()
	{
		worldToLocal = mat4x4();
		normal = vec3();
		fdist = 0.0f;
		localVerts = std::vector<vec2>();
	}
};

struct RenderEnt
{
	mat4x4 modelMatAngles; // model matrix for rendering with angles
	mat4x4 modelMatOrigin; // model matrix for render origin
	vec3 offset; // vertex transformations for picking
	vec3 angles; // support angles
	int modelIdx; // -1 = point entity
	EntCube* pointEntCube;
	bool needAngles = false;
	bool hide = false;
	StudioModel* mdl;
	std::string mdlFileName;
	RenderEnt() : modelMatAngles(mat4x4()), modelMatOrigin(mat4x4()), offset(vec3()), angles(vec3())
	{
		modelIdx = 0;
		pointEntCube = NULL;
		mdl = NULL;
		mdlFileName = "";
	}
	~RenderEnt()
	{
		modelIdx = 0;
		pointEntCube = NULL;
		mdl = NULL;
		mdlFileName = "";
	}
};

struct RenderGroup
{
	cVert* wireframeVerts; // verts for rendering wireframe
	lightmapVert* verts;
	int vertCount;
	int wireframeVertCount;
	Texture* texture;
	Texture* lightmapAtlas[MAX_LIGHTMAPS];
	VertexBuffer* buffer;
	VertexBuffer* wireframeBuffer;
	bool transparent;
	bool special;
	bool highlighted;
	RenderGroup()
	{
		highlighted = false;
		wireframeVerts = NULL;
		verts = NULL;
		buffer = wireframeBuffer = NULL;
		transparent = special = false;
		vertCount = wireframeVertCount = 0;
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
	RenderModel()
	{
		groupCount = renderFaceCount = 0;
		renderFaces = NULL;
		renderGroups = NULL;
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
	std::vector<int> selectedEnts;
	std::vector<int> selectedFaces;

	float bestDist;
	PickInfo();

	int GetSelectedEnt();
	void AddSelectedEnt(int entIdx);

	void SetSelectedEnt(int entIdx);

	void DelSelectedEnt(int entIdx);

	bool IsSelectedEnt(int entIdx);
};

class BspRenderer
{
public:
	Bsp* map;
	PointEntRenderer* pointEntRenderer;
	vec3 mapOffset;
	int showLightFlag = -1;
	std::vector<Wad*> wads;
	bool texturesLoaded = false;
	bool needReloadDebugTextures = false;

	BspRenderer(Bsp* map, PointEntRenderer* pointEntRenderer);
	~BspRenderer();

	void render(std::vector<int> highlightEnts, bool highlightAlwaysOnTop, int clipnodeHull);

	void drawModel(RenderEnt* ent, bool transparent, bool highlight, bool edgesOnly);
	void drawModelClipnodes(int modelIdx, bool highlight, int hullIdx);
	void drawPointEntities(std::vector<int> highlightEnts);

	bool pickPoly(vec3 start, const vec3& dir, int hullIdx, PickInfo& pickInfo, Bsp** map);
	bool pickModelPoly(vec3 start, const vec3& dir, vec3 offset, int modelIdx, int hullIdx, PickInfo& pickInfo);
	bool pickFaceMath(const vec3& start, const vec3& dir, FaceMath& faceMath, float& bestDist);

	void setRenderAngles(int entIdx, vec3 angles);
	void refreshEnt(int entIdx);
	int refreshModel(int modelIdx, bool refreshClipnodes = true, bool noTriangulate = false);
	bool refreshModelClipnodes(int modelIdx);
	void refreshFace(int faceIdx);
	void refreshPointEnt(int entIdx);
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

	void highlightFace(int faceIdx, bool highlight, COLOR4 color = COLOR4(), bool useColor = false, bool reupload = true);
	void updateFaceUVs(int faceIdx);
	unsigned int getFaceTextureId(int faceIdx);

	bool getRenderPointers(int faceIdx, RenderFace** renderFace, RenderGroup** renderGroup);


	LightmapInfo* lightmaps = NULL;
	RenderEnt* renderEnts = NULL;
	RenderModel* renderModels = NULL;
	RenderClipnodes* renderClipnodes = NULL;
	FaceMath* faceMaths = NULL;

	// textures loaded in a separate thread
	Texture** glTexturesSwap;

	size_t numLightmapAtlases;

	int numRenderModels;
	int numRenderClipnodes;
	int numRenderLightmapInfos;
	int numFaceMaths;
	int numPointEnts;
	int numLoadedTextures;


	Texture** glTextures = NULL;
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
	std::map<int, Entity> undoEntityStateMap;
	LumpState undoLumpState{};

	struct DelayEntUndo
	{
		std::string description;
		int entIdx;
		Entity* ent;
	};

	std::vector<DelayEntUndo> delayEntUndoList;

	void pushModelUndoState(const std::string& actionDesc, unsigned int targets);
	void pushEntityUndoState(const std::string& actionDesc, int entIdx);
	void pushEntityUndoStateDelay(const std::string& actionDesc, int entIdx, Entity * ent);
	void pushUndoCommand(Command* cmd);
	void undo();
	void redo();
	void clearUndoCommands();
	void clearRedoCommands();
	void calcUndoMemoryUsage();
	void saveEntityState(int entIdx);
	void saveLumpState();
	void clearDrawCache();

	vec3 renderCameraOrigin;
	vec3 renderCameraAngles;
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
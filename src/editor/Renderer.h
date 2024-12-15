#pragma once

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "BspMerger.h"
#include "ShaderProgram.h"
#include "BspRenderer.h"
#include "Fgd.h"
#include <thread>
#include <future>
#include "Command.h"
#include "mdl_studio.h"

#define EDIT_MODEL_LUMPS (FL_PLANES | FL_TEXTURES | FL_VERTICES | FL_NODES | FL_TEXINFO | FL_FACES | FL_LIGHTING | FL_CLIPNODES | FL_LEAVES | FL_EDGES | FL_SURFEDGES | FL_MODELS | FL_MARKSURFACES)

class Gui;

enum transform_modes
{
	TRANSFORM_MODE_NONE = 0,
	TRANSFORM_MODE_MOVE,
	TRANSFORM_MODE_SCALE
};

enum transform_targets
{
	TRANSFORM_OBJECT,
	TRANSFORM_VERTEX,
	TRANSFORM_ORIGIN
};

enum pick_modes
{
	PICK_OBJECT,
	PICK_FACE,
	PICK_FACE_LEAF
};

struct TransformAxes
{
	cCube model[6];
	VertexBuffer* buffer;
	vec3 origin;
	vec3 mins[6];
	vec3 maxs[6];
	COLOR4 dimColor[6];
	COLOR4 hoverColor[6];
	int numAxes;
};

extern int current_fps;
extern int ortho_overview;
extern vec3 ortho_mins, ortho_maxs;
extern vec3 ortho_offset;
extern float ortho_near;
extern float ortho_far;
extern float ortho_fov;
extern float ortho_custom_aspect;
extern float ortho_custom_w;
extern float ortho_custom_h;
extern int ortho_tga_w;
extern int ortho_tga_h;
extern bool ortho_save_tga;
extern bool ortho_save_bmp;

extern int make_screenshot;
extern int make_screenshot_target;
extern std::string make_screenshot_dir;

extern vec2 mousePos;
extern vec3 cameraOrigin;
extern vec3 cameraAngles;

extern Texture* whiteTex;
extern Texture* redTex;
extern Texture* yellowTex;
extern Texture* greyTex;
extern Texture* blackTex;
extern Texture* blueTex;
extern Texture* missingTex;
extern Texture* missingTex_rgba;
extern Texture* aaatriggerTex_rgba;
extern Texture* aaatriggerTex;
extern Texture* skyTex_rgba;
extern Texture* clipTex_rgba;



extern int pickCount; 
extern int vertPickCount;

extern int g_drawFrameId;


extern std::vector<BspRenderer*> mapRenderers;

class Renderer
{
	friend class Gui;
	friend class EditBspCommand;

public:

	vec3 debugPoint;
	vec3 debugVec1;
	vec3 debugVec2;
	vec3 debugVec3;
	
	vec3 debugLine0;
	vec3 debugLine1;
	Line2D debugCut;
	Polygon3D debugPoly;
	Polygon3D debugPoly2;
	NavMesh* debugNavMesh = NULL;
	LeafNavMesh* debugLeafNavMesh = NULL;
	int debugNavPoly = -1;
	vec3 debugTraceStart;
	TraceResult debugTrace;
	MergeResult mergeResult;


	unsigned int colorShaderMultId;
	ShaderProgram* modelShader;
	ShaderProgram* colorShader;
	ShaderProgram* bspShader;

	double oldTime = 0.0;
	double curTime = 0.0;

	int gl_max_texture_size;


	Gui* gui;

	GLFWwindow* window;

	Fgd* fgd = NULL;

	bool hideGui = false;
	bool isFocused = false;
	bool isHovered = false;
	bool isIconified = false;
	
	bool isModelsReloading = false;

	Renderer();
	~Renderer();

	void addMap(Bsp* map);

	void updateWindowTitle(double _curTime);

	void reloadBspModels();
	void renderLoop();
	void postLoadFgdsAndTextures();
	void postLoadFgds();
	void reloadMaps();
	void clearMaps();
	void saveGuiSettings();
	void loadGuiSettings();

	int gl_errors = 0;

	bool is_minimized = false;
	bool is_focused = true;
	bool reloading = false;
	bool isLoading = false;
	bool reloadingGameDir = false;
	bool is_closing = false;

	PickInfo pickInfo = PickInfo();
	BspRenderer* getMapContainingCamera();
	Bsp* getSelectedMap();
	int getSelectedMapId();
	void selectMapId(int id);
	void selectMap(Bsp* map);
	void deselectMap();
	void clearSelection();
	void updateEnts();
	bool isEntTransparent(const char* classname);
	
	Bsp* SelectedMap = NULL;
	PointEntRenderer* pointEntRenderer;
	PointEntRenderer* swapPointEntRenderer = NULL;

	static std::future<void> fgdFuture;

	vec3 cameraForward;
	vec3 cameraUp;
	vec3 cameraRight;
	bool cameraIsRotating;
	float moveSpeed = 200.0f;
	float fov = 75.0f;
	float zNear = 1.0f;
	float zFar = 262144.0f;
	float rotationSpeed = 5.0f;
	int windowWidth;
	int windowHeight;
	mat4x4 matmodel = mat4x4(), matview = mat4x4(), projection = mat4x4(), modelView = mat4x4(), modelViewProjection = mat4x4();

	vec2 lastMousePos;
	vec2 totalMouseDrag;

	bool movingEnt = false; // grab an ent and move it with the camera
	vec3 grabStartOrigin;
	vec3 grabStartEntOrigin;
	float grabDist;

	TransformAxes moveAxes = TransformAxes();
	TransformAxes scaleAxes = TransformAxes();
	int hoverAxis; // axis being hovered
	//int draggingAxis = -1; // axis currently being dragged by the mouse
	bool gridSnappingEnabled = true;
	int gridSnapLevel = 1;
	float snapSize = 0.01f;
	int transformMode = TRANSFORM_MODE_MOVE;
	int transformTarget = TRANSFORM_OBJECT;
	int pickMode = PICK_OBJECT;
	bool anyPopupOpened = false;
	int last_face_idx = 0;
	//bool blockMoving = false;
	bool showDragAxes = false;
	bool saveTranformResult = false;
	bool pickClickHeld = true; // true if the mouse button is still held after picking an object
	vec3 axisDragStart;
	vec3 axisDragEntOriginStart;
	vec3 deltaMoveOffset;
	std::vector<ScalableTexinfo> scaleTexinfos; // texture coordinates to scale
	bool textureLock = false;
	bool moveOrigin = true;
	bool invalidSolid = false;
	bool isTransformableSolid = false;
	bool anyEdgeSelected = false;
	bool anyVertSelected = false;

	bool drawingScaleAxes = false;
	bool drawingMoveAxes = false;

	int modelTransform = -1;
	std::vector<TransformVert> modelVerts; // control points for invisible plane intersection verts in HULL 0
	std::vector<TransformVert> modelFaceVerts; // control points for visible face verts
	std::vector<HullEdge> modelEdges;
	cCube* modelVertCubes = NULL;
	cCube modelOriginCube = cCube();
	VertexBuffer* modelVertBuff = NULL;
	VertexBuffer* modelOriginBuff = NULL;
	bool originSelected = false;
	bool originHovered = false;
	int hoverVert = -1;
	int hoverEdge = -1;
	float vertExtentFactor = 0.012f;
	bool modelUsesSharedStructures = false;
	vec3 selectionSize;

	cVert* line_verts = NULL;
	VertexBuffer* lineBuf = NULL;

	cQuad* plane_verts = NULL;
	VertexBuffer* planeBuf = NULL;

	VertexBuffer* entConnections = NULL;
	VertexBuffer* entConnectionPoints = NULL;

	int oldLeftMouse;
	int curLeftMouse;
	int oldRightMouse;
	int curRightMouse;

	int oldScroll;

	bool pressed[GLFW_KEY_LAST];
	bool oldPressed[GLFW_KEY_LAST];

	bool anyCtrlPressed;
	bool anyAltPressed;
	bool anyShiftPressed;

	bool canControl;
	bool oldControl;

	double facePickTime = -1.0;

	int debugInt = 0;
	int debugIntMax = 0;
	int debugNode = 0;
	int debugNodeMax = 0;
	bool debugClipnodes = false;
	bool debugNodes = false;
	int clipnodeRenderHull = -1;
	GLfloat lineWidthRange[2];

	double lastTitleTime = 0.0f;
	int g_rend_vsync = -1;
	int tmpPickIdx = -1, tmpVertPickIdx = -1, tmpTransformTarget = -1, tmpModelIdx = -1;
	bool isScalingObject = false;
	bool isMovingOrigin = false;
	bool isTransformingValid = false;
	bool isTransformingWorld = false;
	bool oldTransforming = false;

	bool hasCullbox;
	vec3 cullMins;
	vec3 cullMaxs;

	vec3 getMoveDir();
	void controls();
	void cameraPickingControls();
	void vertexEditControls();
	void cameraRotationControls();
	void cameraObjectHovering();
	void cameraContextMenus(); // right clicking on ents and things
	void moveGrabbedEnt(); // translates the grabbed ent
	void shortcutControls(); // hotkeys for menus and things
	void globalShortcutControls(); // these work even with the UI selected
	void pickObject(); // select stuff with the mouse
	bool transformAxisControls(); // true if grabbing axes
	void applyTransform(Bsp* map, bool forceUpdate = false);
	void setupView(int forceW = 0, int forceH = 0);
	void setupFakeOrthoView(int forceW, int forceH, vec3 bboxMin, vec3 bboxMax);
	void getPickRay(vec3& start, vec3& pickDir);
	void revertInvalidSolid(Bsp* map, int modelIdx);

	void drawModelVerts();
	void drawModelOrigin(int modelIdx);
	void drawTransformAxes();
	void drawEntConnections();
	void drawLine(vec3& start, vec3& end, COLOR4 color);
	void drawLine2D(vec2 start, vec2 end, COLOR4 color);
	void drawBox(vec3 center, float width, COLOR4 color);
	void drawBox(vec3 mins, vec3 maxs, COLOR4 color);
	void drawPolygon3D(Polygon3D& poly, COLOR4 color);
	float drawPolygon2D(Polygon3D poly, vec2 pos, vec2 maxSz, COLOR4 color); // returns render scale
	void drawBox2D(vec2 center, float width, COLOR4 color);
	void drawPlane(BSPPLANE& plane, COLOR4 color, vec3 offset = vec3());
	void drawClipnodes(Bsp* map, int iNode, int& currentPlane, int activePlane, vec3 offset = vec3());
	void drawNodes(Bsp* map, int iNode, int& currentPlane, int activePlane, vec3 offset = vec3());

	vec3 getEntOrigin(Bsp* map, Entity* ent);
	vec3 getEntOffset(Bsp* map, Entity* ent);

	vec3 getAxisDragPoint(vec3 origin);

	void updateDragAxes();
	void updateModelVerts();
	void updateSelectionSize(Bsp * map, int modelIdx);
	void updateEntConnections();
	void updateEntConnectionPositions(); // only updates positions in the buffer
	bool getModelSolid(std::vector<TransformVert>& hullVerts, Bsp* map, Solid& outSolid); // calculate face vertices from plane intersections
	void moveSelectedVerts(const vec3& delta);
	bool splitModelFace();
	void updateCullBox();

	vec3 snapToGrid(vec3 pos);

	void grabEnt();
	void cutEnt();
	void copyEnt();
	void pasteEnt(bool noModifyOrigin, bool copyModel = false);
	void pasteEntsFromText(std::string text);
	void deleteEnts();
	void scaleSelectedObject(Bsp* map, int modelIdx, float x, float y, float z);
	void scaleSelectedObject(Bsp* map, int modelIdx, vec3 dir, const vec3& fromDir, bool logging = false);
	void scaleSelectedVerts(Bsp* map, int modelIdx, float x, float y, float z);
	void deselectObject(bool onlyobject = false); // keep map selected but unselect all objects
	void selectFace(Bsp* map, int face, bool add = false);
	void deselectFaces();
	void selectEnt(Bsp* map, int entIdx, bool add = false);
	void goToEnt(Bsp* map, int entIdx);
	void goToCoords(float x, float y, float z);
	void goToCoords(const vec3& pos);
	void goToFace(Bsp* map, int faceIdx);
	void ungrabEnt();
	void loadFgds();
	void merge(std::string fpath);

	bool hasCopiedEnt();

	std::vector<std::string> glExteralTextures_names;
	std::vector<Texture*> glExteralTextures_textures;
	std::vector<std::string> glExteralTextures_wads;
	Texture* giveMeTexture(const std::string& texname, const std::string & wadPart = std::string());
};
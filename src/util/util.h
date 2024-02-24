#pragma once

#include <filesystem>
namespace fs = std::filesystem;
#include <fmt/format.h>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include "mat4x4.h"
#include <fstream>
#include <cmath>
#include <thread>
#include <mutex>
#include "ProgressMeter.h"
#include "bsptypes.h"
#include <math.h>
#include "primitives.h"

class Bsp;

extern std::string g_version_string;

#ifndef WIN32
#define fopen_s(pFile,filename,mode) ((*(pFile))=fopen((filename),  (mode)))==NULL
#endif

enum PRINT_CONST : unsigned int
{
	PRINT_BLUE = 1,
	PRINT_GREEN = 2,
	PRINT_RED = 4,
	PRINT_INTENSITY = 8
};

static const vec3  s_baseaxis[18] = {
	{0, 0, 1}, {1, 0, 0}, {0, -1, 0},                      // floor
	{0, 0, -1}, {1, 0, 0}, {0, -1, 0},                     // ceiling
	{1, 0, 0}, {0, 1, 0}, {0, 0, -1},                      // west wall
	{-1, 0, 0}, {0, 1, 0}, {0, 0, -1},                     // east wall
	{0, 1, 0}, {1, 0, 0}, {0, 0, -1},                      // south wall
	{0, -1, 0}, {1, 0, 0}, {0, 0, -1},                     // north wall
};

extern bool DebugKeyPressed;
extern bool g_verbose;
extern ProgressMeter g_progress;
extern std::vector<std::string> g_log_buffer;
extern std::vector<unsigned int> g_color_buffer;

extern std::mutex g_mutex_list[10];


extern unsigned int g_console_colors;

extern bool g_console_visible;
void showConsoleWindow(bool show);

//ImVec4 imguiColorFromConsole(unsigned int colors);
void set_console_colors(unsigned int colors = 0);

std::vector<std::string> splitStringIgnoringQuotes(const std::string& s, const std::string& delimitter);
std::vector<std::string> splitString(const std::string& s, const std::string& delimiter, int maxParts = 0);

void replaceAll(std::string& str, const std::string& from, const std::string& to);

template<class ...Args>
inline void print_log(unsigned int colors, const std::string& format, Args ...args) noexcept
{
	set_console_colors(colors);
	std::string line = fmt::vformat(format, fmt::make_format_args(args...));

	if (!line.size())
		return;

	g_mutex_list[0].lock();
#ifndef NDEBUG
	static std::ofstream outfile("log.txt", std::ios_base::app);
	outfile << line;
	outfile.flush();
#endif
	std::cout << line;
	//replaceAll(line, " ", "+");
	auto newline = line.ends_with('\n');
	auto splitstr = splitString(line, "\n");

	bool ret = line[0] == '\r';
	if (ret)
		line.erase(line.begin());

	if (splitstr.size() == 1)
	{
		if (!newline && g_log_buffer.size())
		{
			if (ret)
			{
				g_log_buffer[g_log_buffer.size() - 1] = line;
				g_color_buffer[g_log_buffer.size() - 1] = colors;
			}
			else
			{
				g_log_buffer[g_log_buffer.size() - 1] += line;
				g_color_buffer[g_log_buffer.size() - 1] = colors;
			}
		}
		else
		{
			line.pop_back();

			g_log_buffer[g_log_buffer.size() - 1] = line;
			g_color_buffer[g_log_buffer.size() - 1] = colors;

			g_log_buffer.emplace_back("");
			g_color_buffer.emplace_back(0);
		}
	}
	else
	{
		for (auto& s : splitstr)
		{
			if (s.size())
			{
				g_log_buffer[g_log_buffer.size() - 1] = s;
				g_color_buffer[g_log_buffer.size() - 1] = colors;

				g_log_buffer.emplace_back("");
				g_color_buffer.emplace_back(0);
			}
		}
	}
	g_mutex_list[0].unlock();
	set_console_colors(0);
}

template<class ...Args>
inline void print_log(const std::string& format, Args ...args) noexcept
{
	std::string line = fmt::vformat(format, fmt::make_format_args(args...));
	print_log(g_console_colors, "{}", line);
}

bool fileExists(const std::string& fileName);

bool copyFile(const std::string& from, const std::string& to);

char* loadFile(const std::string& fileName, int& length);

bool writeFile(const std::string& fileName, const char* data, int len);
bool writeFile(const std::string& fileName, const std::string& data);

bool removeFile(const std::string& fileName);

size_t fileSize(const std::string& filePath);

std::string basename(const std::string& path);

std::string stripExt(const std::string& filename);

std::string stripFileName(const std::string& path);
std::wstring stripFileName(const std::wstring& path);

bool isNumeric(const std::string& s);

bool dirExists(const std::string& dirName);

bool createDir(const std::string& dirName);

void removeDir(const std::string& dirName);

std::string toLowerCase(const std::string& s);

std::string toUpperCase(const std::string& s);

std::string trimSpaces(const std::string& str);

int getTextureSizeInBytes(BSPMIPTEX* bspTexture, bool palette = false);

vec3 parseVector(const std::string& s);

bool IsEntNotSupportAngles(std::string& entname);

bool pickAABB(vec3 start, vec3 rayDir, vec3 mins, vec3 maxs, float& bestDist);

bool rayPlaneIntersect(const vec3& start, const vec3& dir, const vec3& normal, float fdist, float& intersectDist);

float getDistAlongAxis(const vec3& axis, const vec3& p);

// returns false if verts are not planar
bool getPlaneFromVerts(const std::vector<vec3>& verts, vec3& outNormal, float& outDist);

void getBoundingBox(const std::vector<vec3>& verts, vec3& mins, vec3& maxs);
void getBoundingBox(const std::vector<TransformVert>& verts, vec3& mins, vec3& maxs);

vec2 getCenter(const std::vector<vec2>& verts);

vec3 getCenter(const std::vector<vec3>& verts);

vec3 getCenter(const std::vector<cVert>& verts);

vec3 getCenter(const vec3& maxs, const vec3& mins);

void expandBoundingBox(const vec3& v, vec3& mins, vec3& maxs);

void expandBoundingBox(const cVert& v, vec3& mins, vec3& maxs);

void expandBoundingBox(const vec2& v, vec2& mins, vec2& maxs);

std::vector<vec3> getPlaneIntersectVerts(const std::vector<BSPPLANE>& planes);

bool vertsAllOnOneSide(std::vector<vec3>& verts, BSPPLANE& plane);

// get verts from the given set that form a triangle (no duplicates and not colinear)
std::vector<vec3> getTriangularVerts(std::vector<vec3>& verts);

vec3 getNormalFromVerts(std::vector<vec3>& verts);

// transforms verts onto a plane (which is defined by the verts themselves)
std::vector<vec2> localizeVerts( std::vector<vec3>& verts);

// Returns CCW sorted indexes into the verts, as viewed on the plane the verts define
std::vector<size_t> getSortedPlanarVertOrder(std::vector<vec3>& verts);

std::vector<vec3> getSortedPlanarVerts(std::vector<vec3>& verts);

bool pointInsidePolygon(std::vector<vec2>& poly, vec2 p);

int ArrayXYtoId(int w, int x, int y);

enum class FIXUPPATH_SLASH
{
	FIXUPPATH_SLASH_CREATE,
	FIXUPPATH_SLASH_SKIP,
	FIXUPPATH_SLASH_REMOVE
};
void fixupPath(char* path, FIXUPPATH_SLASH startslash, FIXUPPATH_SLASH endslash);
void fixupPath(std::string& path, FIXUPPATH_SLASH startslash, FIXUPPATH_SLASH endslash);
void WriteBMP(const std::string& fileName, unsigned char* pixels, int width, int height, int bytesPerPixel);

int TextureAxisFromPlane(const BSPPLANE& pln, vec3& xv, vec3& yv);
float AngleFromTextureAxis(vec3 axis, bool x, int type);
vec3 AxisFromTextureAngle(float angle, bool x, int type);

size_t strlen(std::string str);

int GetImageColors(COLOR3* image, int size, int max_colors = 256);
int ColorDistance(COLOR3 color, COLOR3 other);
void SimpeColorReduce(COLOR3* image, int size);

bool FindPathInAssets(Bsp* map, const std::string& filename, std::string& outpath, bool tracesearch = false);
void FixupAllSystemPaths();

int BoxOnPlaneSide(const vec3& emins, const vec3& emaxs, const BSPPLANE* p);
#define BOX_ON_PLANE_SIDE( emins, emaxs, p )			\
	((( p )->type < 3 ) ?				\
	(						\
		((p)->dist <= (emins)[(p)->type]) ?		\
			1				\
		:					\
		(					\
			((p)->dist >= (emaxs)[(p)->type]) ?	\
				2			\
			:				\
				3			\
		)					\
	)						\
	:						\
		BoxOnPlaneSide(( emins ), ( emaxs ), ( p )))

void scaleImage(const COLOR4* inputImage, std::vector<COLOR4>& outputImage,
	int inputWidth, int inputHeight, int outputWidth, int outputHeight);
void scaleImage(const COLOR3* inputImage, std::vector<COLOR3>& outputImage,
	int inputWidth, int inputHeight, int outputWidth, int outputHeight);

float floatRound(float f);

std::string GetExecutableDir(std::string arg_0);
std::string GetExecutableDir(std::wstring arg_0);

std::vector<vec3> stretch_model(const std::vector<vec3>& vertices, float stretch_value);
std::vector<cVert> stretch_model(const std::vector<cVert>& vertices, float stretch_value);

BSPPLANE getSeparatePlane(vec3 amin, vec3 amax, vec3 bmin, vec3 bmax, bool force = false);


// true if value begins a group of strings separated by spaces
bool stringGroupStarts(const std::string& s);

// true if any closing paren or quote is found
bool stringGroupEnds(const std::string& s);

// get the value inside a prefixed set of parens
std::string getValueInParens(std::string s);

// groups strings separated by spaces but enclosed in quotes/parens
std::vector<std::string> groupParts(std::vector<std::string>& ungrouped);

std::string getValueInQuotes(std::string s);


std::vector<cVert> removeDuplicateWireframeLines(const std::vector<cVert>& points);

void removeColinearPoints(std::vector<vec3>& vertices, float epsilon);


bool checkCollision(const vec3& obj1Mins, const vec3& obj1Maxs, const vec3& obj2Mins, const vec3& obj2Maxs);


// author https://gist.github.com/multiplemonomials/1d1806062a3809ffe26f7a232757ecb6


#include <string>
#include <vector>

class Process
{
	std::string _program;
	std::vector<std::string> _arguments;

	std::string quoteIfNecessary(std::string toQuote);

public:
	Process(std::string program);
	Process& arg(std::string arg);
	std::string getCommandlineString();
	int executeAndWait(int sin, int sout, int serr);
};

void calculateTextureInfo(BSPTEXTUREINFO& texinfo, const std::vector<vec3>& vertices, const std::vector<vec2>& uvs);
void getTrueTexSize(int& width, int& height, int maxsize = 512);


vec3 getEdgeControlPoint(const std::vector<TransformVert>& hullVerts, HullEdge& edge);

vec3 getCentroid(const std::vector<TransformVert>& hullVerts);
vec3 getCentroid(const std::vector<vec3>& hullVerts);

std::vector<std::vector<COLOR3>> splitImage(const COLOR3* input, int input_width, int input_height, int x_parts, int y_parts, int& out_part_width, int& out_part_height);
std::vector<std::vector<COLOR3>> splitImage(const std::vector<COLOR3>& input, int input_width, int input_height, int x_parts, int y_parts, int& out_part_width, int& out_part_height);
std::vector<COLOR3> getSubImage(const std::vector<std::vector<COLOR3>>& images, int x, int y, int x_parts);

bool isPointInsideMesh(const vec3& point, const std::vector<vec3>& glTriangles);

std::vector<std::vector<BBOX>> make_collision_from_triangles(const std::vector<vec3>& gl_triangles, int& max_row);
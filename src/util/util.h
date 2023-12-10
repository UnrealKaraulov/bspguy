#pragma once

#include <filesystem>
namespace fs = std::filesystem;
// not working on fucking linux (<format> file not found, or std::format not found, etc)
//#include <format>
// replaced to fmt
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

class Bsp;

extern std::string g_version_string;

#ifndef WIN32
#define fopen_s(pFile,filename,mode) ((*(pFile))=fopen((filename),  (mode)))==NULL
#endif

enum PRINT_CONTS : unsigned short
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
extern std::vector<unsigned short> g_color_buffer;
extern std::mutex g_mutex_list[10];


extern unsigned short g_console_colors;
//ImVec4 imguiColorFromConsole(unsigned short colors);
void set_console_colors(unsigned short colors = 0);


template<class ...Args>
inline void print_log(const std::string& format, Args ...args) noexcept
{
	g_mutex_list[0].lock();

	std::string log_line = fmt::vformat(format, fmt::make_format_args(args...));

#ifndef NDEBUG
	static std::ofstream outfile("log.txt", std::ios_base::app);
	outfile << log_line;
	outfile.flush();
#endif

	if (g_log_buffer.size() == 0 || g_log_buffer[g_log_buffer.size() - 1] != log_line)
	{
		std::cout << log_line;
		g_log_buffer.push_back(log_line);
		g_color_buffer.push_back(g_console_colors);
	}
	g_mutex_list[0].unlock();
}


template<class ...Args>
inline void print_log(unsigned short colors, const std::string& format, Args ...args) noexcept
{
	g_mutex_list[0].lock();

	set_console_colors(colors);

	std::string log_line = fmt::vformat(format, fmt::make_format_args(args...));

#ifndef NDEBUG
	static std::ofstream outfile("log.txt", std::ios_base::app);
	outfile << log_line;
	outfile.flush();
#endif

	if (g_log_buffer.size() == 0 || g_log_buffer[g_log_buffer.size() - 1] != log_line)
	{
		std::cout << log_line;
		g_log_buffer.push_back(log_line);
		g_color_buffer.push_back(colors);
	}
	g_mutex_list[0].unlock();

	set_console_colors();
}


bool fileExists(const std::string& fileName);

bool copyFile(const std::string& fileName, const std::string& fileName2);

char* loadFile(const std::string& fileName, int& length);

bool writeFile(const std::string& fileName, const char* data, int len);
bool writeFile(const std::string& fileName, const std::string& data);

bool removeFile(const std::string& fileName);

std::streampos fileSize(const std::string& filePath);

std::vector<std::string> splitStringIgnoringQuotes(std::string s, const std::string& delimitter);
std::vector<std::string> splitString(std::string s, const std::string& delimiter, int maxParts = 0);

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

float clamp(float val, float min, float max);

vec3 parseVector(const std::string& s);

bool IsEntNotSupportAngles(std::string& entname);

bool pickAABB(vec3 start, vec3 rayDir, vec3 mins, vec3 maxs, float& bestDist);

bool rayPlaneIntersect(const vec3& start, const vec3& dir, const vec3& normal, float fdist, float& intersectDist);

float getDistAlongAxis(const vec3& axis, const vec3& p);

// returns false if verts are not planar
bool getPlaneFromVerts(const std::vector<vec3>& verts, vec3& outNormal, float& outDist);

void getBoundingBox(const std::vector<vec3>& verts, vec3& mins, vec3& maxs);

vec2 getCenter(std::vector<vec2>& verts);

vec3 getCenter(std::vector<vec3>& verts);

vec3 getCenter(const vec3& maxs, const vec3& mins);

void expandBoundingBox(const vec3& v, vec3& mins, vec3& maxs);

void expandBoundingBox(const vec2& v, vec2& mins, vec2& maxs);

std::vector<vec3> getPlaneIntersectVerts(std::vector<BSPPLANE>& planes);

bool vertsAllOnOneSide(std::vector<vec3>& verts, BSPPLANE& plane);

// get verts from the given set that form a triangle (no duplicates and not colinear)
std::vector<vec3> getTriangularVerts(std::vector<vec3>& verts);

vec3 getNormalFromVerts(std::vector<vec3>& verts);

// transforms verts onto a plane (which is defined by the verts themselves)
std::vector<vec2> localizeVerts(std::vector<vec3>& verts);

// Returns CCW sorted indexes into the verts, as viewed on the plane the verts define
std::vector<int> getSortedPlanarVertOrder(std::vector<vec3>& verts);

std::vector<vec3> getSortedPlanarVerts(std::vector<vec3>& verts);

bool pointInsidePolygon(std::vector<vec2>& poly, vec2 p);

enum class FIXUPPATH_SLASH
{
	FIXUPPATH_SLASH_CREATE,
	FIXUPPATH_SLASH_SKIP,
	FIXUPPATH_SLASH_REMOVE
};
void fixupPath(char* path, FIXUPPATH_SLASH startslash, FIXUPPATH_SLASH endslash);
void fixupPath(std::string& path, FIXUPPATH_SLASH startslash, FIXUPPATH_SLASH endslash);
void replaceAll(std::string& str, const std::string& from, const std::string& to);

void WriteBMP(const std::string& fileName, unsigned char* pixels, int width, int height, int bytesPerPixel);

std::string GetCurrentDir();

int TextureAxisFromPlane(const BSPPLANE& pln, vec3& xv, vec3& yv);
float AngleFromTextureAxis(vec3 axis, bool x, int type);
vec3 AxisFromTextureAngle(float angle, bool x, int type);

size_t strlen(std::string str);

int GetImageColors(COLOR3* image, int size);
int ColorDistance(COLOR3 color, COLOR3 other);
void SimpeColorReduce(COLOR3* image, int size);

bool FindPathInAssets(Bsp * map, const std::string& path, std::string& outpath, bool tracesearch = false);
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



float floatRound(float f);
#include "lang.h"
#include "util.h"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <string.h>
#include "Wad.h"
#include <cfloat> 
#include <stdarg.h>
#ifdef WIN32
#include <Windows.h>
#include <Shlobj.h>
#else 
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <set>
#include "Settings.h"
#include "Renderer.h"

#include "Bsp.h"

#include <unordered_set>

bool DebugKeyPressed = false;
ProgressMeter g_progress = {};
int g_render_flags;
std::vector<std::string> g_log_buffer = { "" };
std::vector<unsigned int> g_color_buffer = { 0 };

std::mutex g_mutex_list[10] = {};

bool fileExists(const std::string& fileName)
{
	return fs::exists(fileName) && !fs::is_directory(fileName);
}

char* loadFile(const std::string& fileName, int& length)
{
	if (!fileExists(fileName))
		return NULL;
	std::ifstream fin(fileName.c_str(), std::ios::binary);
	long long begin = fin.tellg();
	fin.seekg(0, std::ios::end);
	unsigned int size = (unsigned int)((int)fin.tellg() - begin);
	char* buffer = new char[size];
	fin.seekg(0, std::ios::beg);
	fin.read(buffer, size);
	fin.close();
	length = (int)size; // surely models will never exceed 2 GB
	return buffer;
}

bool writeFile(const std::string& fileName, const char* data, int len)
{
	std::ofstream file(fileName, std::ios::trunc | std::ios::binary);
	if (!file.is_open() || len <= 0)
	{
		return false;
	}
	file.write(data, len);
	file.flush();
	return true;
}

bool writeFile(const std::string& fileName, const std::string& data)
{
	std::ofstream file(fileName, std::ios::trunc | std::ios::binary);
	if (!file.is_open() || !data.size())
	{
		return false;
	}
	file.write(data.c_str(), strlen(data));
	file.flush();
	return true;
}


bool removeFile(const std::string& fileName)
{
	return fs::exists(fileName) && fs::remove(fileName);
}

bool copyFile(const std::string& from, const std::string& to)
{
	if (!fileExists(from))
		return false;
	if (fileExists(to))
	{
		if (!removeFile(to))
			return false;
	}
	return fs::copy_file(from, to);
}

size_t fileSize(const std::string& filePath)
{
	size_t fsize = 0;
	std::ifstream file(filePath, std::ios::binary);

	file.seekg(0, std::ios::end);
	fsize = (size_t)file.tellg();
	file.close();

	return fsize;
}

std::vector<std::string> splitString(const std::string& str, const std::string& delimiter, int maxParts)
{
	std::string s = str;
	std::vector<std::string> split;
	size_t pos;
	while ((pos = s.find(delimiter)) != std::string::npos && (maxParts == 0 || (int)split.size() < maxParts - 1)) {
		if (pos != 0) {
			split.push_back(s.substr(0, pos));
		}
		s.erase(0, pos + delimiter.length());
	}
	if (!s.empty())
		split.push_back(s);
	return split;
}

std::vector<std::string> splitStringIgnoringQuotes(const std::string& str, const std::string& delimitter)
{
	std::vector<std::string> split;
	if (str.empty() || delimitter.empty())
		return split;
	std::string s = str;
	size_t delimitLen = delimitter.length();

	while (s.size())
	{
		bool foundUnquotedDelimitter = false;
		size_t searchOffset = 0;
		while (!foundUnquotedDelimitter && searchOffset < s.size())
		{
			size_t delimitPos = s.find(delimitter, searchOffset);

			if (delimitPos == std::string::npos || delimitPos > s.size())
			{
				split.push_back(s);
				return split;
			}
			size_t quoteCount = 0;
			for (size_t i = 0; i < delimitPos; i++)
			{
				quoteCount += s[i] == '"' && (i == 0 || s[i - 1] != '\\');
			}

			if (quoteCount % 2 == 1)
			{
				searchOffset = delimitPos + 1;
				continue;
			}
			if (delimitPos != 0)
				split.emplace_back(s.substr(0, delimitPos));

			s = s.substr(delimitPos + delimitLen);
			foundUnquotedDelimitter = true;
		}

		if (!foundUnquotedDelimitter)
		{
			break;
		}

	}

	return split;
}


std::string basename(const std::string& path)
{
	size_t lastSlash = path.find_last_of("\\/");
	if (lastSlash != std::string::npos)
	{
		return path.substr(lastSlash + 1);
	}
	return path;
}

std::string stripExt(const std::string& path)
{
	size_t lastDot = path.find_last_of('.');
	if (lastDot != std::string::npos)
	{
		return path.substr(0, lastDot);
	}
	return path;
}

std::string stripFileName(const std::string& path)
{
	size_t lastSlash = path.find_last_of("\\/");
	if (lastSlash != std::string::npos)
	{
		return path.substr(0, lastSlash);
	}
	return path;
}
std::wstring stripFileName(const std::wstring& path)
{
	size_t lastSlash = path.find_last_of(L"\\/");
	if (lastSlash != std::string::npos)
	{
		return path.substr(0, lastSlash);
	}
	return path;
}

bool isNumeric(const std::string& s)
{
	if (s.empty())
		return false;
	std::string::const_iterator it = s.begin();

	while (it != s.end() && isdigit(*it))
		++it;

	return it == s.end();
}

std::string toLowerCase(const std::string& s)
{
	std::string ret = s;
	std::transform(ret.begin(), ret.end(), ret.begin(),
		[](unsigned char c) { return (unsigned char)std::tolower(c); }
	);
	return ret;
}
std::string toUpperCase(const std::string& s)
{
	std::string ret = s;
	std::transform(ret.begin(), ret.end(), ret.begin(),
		[](unsigned char c) { return (unsigned char)std::toupper(c); }
	);
	return ret;
}

std::string trimSpaces(const std::string& str)
{
	if (str.empty())
	{
		return str;
	}

	std::string result = str;
	while (!result.empty() && std::isspace(result.front())) {
		result.erase(result.begin());
	}
	while (!result.empty() && std::isspace(result.back())) {
		result.pop_back();
	}
	return result;
}

int getTextureSizeInBytes(BSPMIPTEX* bspTexture, bool palette)
{
	int sz = sizeof(BSPMIPTEX);
	if (bspTexture->nOffsets[0] > 0)
	{
		if (palette)
		{
			sz += sizeof(short) /* pal count */;
			sz += sizeof(COLOR3) * 256; // pallette
		}

		for (int i = 0; i < MIPLEVELS; i++)
		{
			sz += (bspTexture->nWidth >> i) * (bspTexture->nHeight >> i);
		}
	}
	return sz;
}


vec3 parseVector(const std::string& s)
{
	vec3 v;
	std::vector<std::string> parts = splitString(s, " ");

	while (parts.size() < 3)
	{
		parts.push_back("0");
	}

	v.x = (float)atof(parts[0].c_str());
	v.y = (float)atof(parts[1].c_str());
	v.z = (float)atof(parts[2].c_str());
	return v;
}

bool IsEntNotSupportAngles(std::string& entname)
{
	if (entname == "func_wall" ||
		entname == "func_wall_toggle" ||
		entname == "func_illusionary" ||
		entname == "spark_shower" ||
		entname == "func_plat" ||
		entname == "func_door" ||
		entname == "momentary_door" ||
		entname == "func_water" ||
		entname == "func_conveyor" ||
		entname == "func_rot_button" ||
		entname == "func_button" ||
		entname == "env_blood" ||
		entname == "gibshooter" ||
		entname == "trigger" ||
		entname == "trigger_monsterjump" ||
		entname == "trigger_hurt" ||
		entname == "trigger_multiple" ||
		entname == "trigger_push" ||
		entname == "trigger_teleport" ||
		entname == "func_bomb_target" ||
		entname == "func_hostage_rescue" ||
		entname == "func_vip_safetyzone" ||
		entname == "func_escapezone" ||
		entname == "trigger_autosave" ||
		entname == "trigger_endsection" ||
		entname == "trigger_gravity" ||
		entname == "env_snow" ||
		entname == "func_snow" ||
		entname == "env_rain" ||
		entname == "func_rain")
		return true;
	return false;
}

COLOR3 operator*(COLOR3 c, float scale)
{
	c.r = (unsigned char)(c.r * scale);
	c.g = (unsigned char)(c.g * scale);
	c.b = (unsigned char)(c.b * scale);
	return c;
}

bool operator==(COLOR3 c1, COLOR3 c2)
{
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b;
}

COLOR4 operator*(COLOR4 c, float scale)
{
	c.r = (unsigned char)(c.r * scale);
	c.g = (unsigned char)(c.g * scale);
	c.b = (unsigned char)(c.b * scale);
	return c;
}

bool operator==(COLOR4 c1, COLOR4 c2)
{
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b && c1.a == c2.a;
}

bool pickAABB(vec3 start, vec3 rayDir, vec3 mins, vec3 maxs, float& bestDist)
{
	/*
	Fast Ray-Box Intersection
	by Andrew Woo
	from "Graphics Gems", Academic Press, 1990
	https://web.archive.org/web/20090803054252/http://tog.acm.org/resources/GraphicsGems/gems/RayBox.c
	*/

	bool inside = true;
	char quadrant[3];
	int i;
	int whichPlane;
	float maxT[3];
	float candidatePlane[3];

	float* origin = (float*)&start;
	float* dir = (float*)&rayDir;
	float* minB = (float*)&mins;
	float* maxB = (float*)&maxs;
	float coord[3];

	const char RIGHT = 0;
	const char LEFT = 1;
	const char MIDDLE = 2;

	/* Find candidate planes; this loop can be avoided if
	rays cast all from the eye(assume perpsective view) */
	for (i = 0; i < 3; i++)
	{
		if (origin[i] < minB[i])
		{
			quadrant[i] = LEFT;
			candidatePlane[i] = minB[i];
			inside = false;
		}
		else if (origin[i] > maxB[i])
		{
			quadrant[i] = RIGHT;
			candidatePlane[i] = maxB[i];
			inside = false;
		}
		else
		{
			quadrant[i] = MIDDLE;
		}
	}

	/* Ray origin inside bounding box */
	if (inside)
	{
		return false;
	}

	/* Calculate T distances to candidate planes */
	for (i = 0; i < 3; i++)
	{
		if (quadrant[i] != MIDDLE && abs(dir[i]) >= EPSILON)
			maxT[i] = (candidatePlane[i] - origin[i]) / dir[i];
		else
			maxT[i] = -1.0f;
	}

	/* Get largest of the maxT's for final choice of intersection */
	whichPlane = 0;
	for (i = 1; i < 3; i++)
	{
		if (maxT[whichPlane] < maxT[i])
			whichPlane = i;
	}

	/* Check final candidate actually inside box */
	if (maxT[whichPlane] < 0.0f)
		return false;

	for (i = 0; i < 3; i++)
	{
		if (whichPlane != i)
		{
			coord[i] = origin[i] + maxT[whichPlane] * dir[i];
			if (coord[i] < minB[i] || coord[i] > maxB[i])
				return false;
		}
		else
		{
			coord[i] = candidatePlane[i];
		}
	}
	/* ray hits box */

	vec3 intersectPoint(coord[0], coord[1], coord[2]);
	float dist = (intersectPoint - start).length();

	if (dist < bestDist)
	{
		bestDist = dist;
		return true;
	}

	return false;
}

bool rayPlaneIntersect(const vec3& start, const vec3& dir, const vec3& normal, float fdist, float& intersectDist)
{
	float dot = dotProduct(dir, normal);

	// don't select backfaces or parallel faces
	if (abs(dot) < EPSILON)
	{
		return false;
	}
	intersectDist = dotProduct((normal * fdist) - start, normal) / dot;

	if (intersectDist < 0.f)
	{
		return false; // intersection behind ray
	}

	return true;
}

float getDistAlongAxis(const vec3& axis, const vec3& p)
{
	return dotProduct(axis, p) / sqrt(dotProduct(axis, axis));
}

bool getPlaneFromVerts(const std::vector<vec3>& verts, vec3& outNormal, float& outDist)
{
	const float tolerance = 0.00001f; // normals more different than this = non-planar face

	size_t numVerts = verts.size();
	for (size_t i = 0; i < numVerts; i++)
	{
		vec3 v0 = verts[(i + 0) % numVerts];
		vec3 v1 = verts[(i + 1) % numVerts];
		vec3 v2 = verts[(i + 2) % numVerts];

		vec3 ba = v1 - v0;
		vec3 cb = v2 - v1;

		vec3 normal = crossProduct(ba, cb).normalize(1.0f);

		if (i == 0)
		{
			outNormal = normal;
		}
		else
		{
			float dot = dotProduct(outNormal, normal);
			if (abs(dot) < 1.0f - tolerance)
			{
				return false; // non-planar face
			}
		}
	}

	outDist = getDistAlongAxis(outNormal, verts[0]);
	return true;
}

vec2 getCenter(const std::vector<vec2>& verts)
{
	vec2 maxs = vec2(-FLT_MAX, -FLT_MAX);
	vec2 mins = vec2(FLT_MAX, FLT_MAX);

	for (size_t i = 0; i < verts.size(); i++)
	{
		expandBoundingBox(verts[i], mins, maxs);
	}

	return mins + (maxs - mins) * 0.5f;
}

vec3 getCenter(const std::vector<vec3>& verts)
{
	vec3 maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	vec3 mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);

	for (size_t i = 0; i < verts.size(); i++)
	{
		expandBoundingBox(verts[i], mins, maxs);
	}

	return mins + (maxs - mins) * 0.5f;
}

vec3 getCenter(const std::vector<cVert>& verts)
{
	vec3 maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	vec3 mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);

	for (size_t i = 0; i < verts.size(); i++)
	{
		expandBoundingBox(verts[i], mins, maxs);
	}

	return mins + (maxs - mins) * 0.5f;
}


vec3 getCenter(const vec3& maxs, const vec3& mins)
{
	return mins + (maxs - mins) * 0.5f;
}

void getBoundingBox(const std::vector<vec3>& verts, vec3& mins, vec3& maxs)
{
	maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);

	for (size_t i = 0; i < verts.size(); i++)
	{
		expandBoundingBox(verts[i], mins, maxs);
	}
}

void getBoundingBox(const std::vector<TransformVert>& verts, vec3& mins, vec3& maxs)
{
	maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);

	for (size_t i = 0; i < verts.size(); i++)
	{
		expandBoundingBox(verts[i].pos, mins, maxs);
	}
}

void expandBoundingBox(const vec3& v, vec3& mins, vec3& maxs)
{
	if (v.x > maxs.x) maxs.x = v.x;
	if (v.y > maxs.y) maxs.y = v.y;
	if (v.z > maxs.z) maxs.z = v.z;

	if (v.x < mins.x) mins.x = v.x;
	if (v.y < mins.y) mins.y = v.y;
	if (v.z < mins.z) mins.z = v.z;
}

void expandBoundingBox(const cVert& v, vec3& mins, vec3& maxs)
{
	if (v.pos.x > maxs.x) maxs.x = v.pos.x;
	if (v.pos.y > maxs.y) maxs.y = v.pos.y;
	if (v.pos.z > maxs.z) maxs.z = v.pos.z;

	if (v.pos.x < mins.x) mins.x = v.pos.x;
	if (v.pos.y < mins.y) mins.y = v.pos.y;
	if (v.pos.z < mins.z) mins.z = v.pos.z;
}

void expandBoundingBox(const vec2& v, vec2& mins, vec2& maxs)
{
	if (v.x > maxs.x) maxs.x = v.x;
	if (v.y > maxs.y) maxs.y = v.y;

	if (v.x < mins.x) mins.x = v.x;
	if (v.y < mins.y) mins.y = v.y;
}



int BoxOnPlaneSide(const vec3& emins, const vec3& emaxs, const BSPPLANE* p)
{
	float	dist1, dist2;
	int	sides = 0;
	int signs = 0;

	for (int i = 0; i < 3; i++)
	{
		if (std::signbit(p->vNormal[i]))
		{
			signs += (1U << (i));
		}
	}

	// general case
	switch (signs)
	{
	case 0:
		dist1 = p->vNormal[0] * emaxs[0] + p->vNormal[1] * emaxs[1] + p->vNormal[2] * emaxs[2];
		dist2 = p->vNormal[0] * emins[0] + p->vNormal[1] * emins[1] + p->vNormal[2] * emins[2];
		break;
	case 1:
		dist1 = p->vNormal[0] * emins[0] + p->vNormal[1] * emaxs[1] + p->vNormal[2] * emaxs[2];
		dist2 = p->vNormal[0] * emaxs[0] + p->vNormal[1] * emins[1] + p->vNormal[2] * emins[2];
		break;
	case 2:
		dist1 = p->vNormal[0] * emaxs[0] + p->vNormal[1] * emins[1] + p->vNormal[2] * emaxs[2];
		dist2 = p->vNormal[0] * emins[0] + p->vNormal[1] * emaxs[1] + p->vNormal[2] * emins[2];
		break;
	case 3:
		dist1 = p->vNormal[0] * emins[0] + p->vNormal[1] * emins[1] + p->vNormal[2] * emaxs[2];
		dist2 = p->vNormal[0] * emaxs[0] + p->vNormal[1] * emaxs[1] + p->vNormal[2] * emins[2];
		break;
	case 4:
		dist1 = p->vNormal[0] * emaxs[0] + p->vNormal[1] * emaxs[1] + p->vNormal[2] * emins[2];
		dist2 = p->vNormal[0] * emins[0] + p->vNormal[1] * emins[1] + p->vNormal[2] * emaxs[2];
		break;
	case 5:
		dist1 = p->vNormal[0] * emins[0] + p->vNormal[1] * emaxs[1] + p->vNormal[2] * emins[2];
		dist2 = p->vNormal[0] * emaxs[0] + p->vNormal[1] * emins[1] + p->vNormal[2] * emaxs[2];
		break;
	case 6:
		dist1 = p->vNormal[0] * emaxs[0] + p->vNormal[1] * emins[1] + p->vNormal[2] * emins[2];
		dist2 = p->vNormal[0] * emins[0] + p->vNormal[1] * emaxs[1] + p->vNormal[2] * emaxs[2];
		break;
	case 7:
		dist1 = p->vNormal[0] * emins[0] + p->vNormal[1] * emins[1] + p->vNormal[2] * emins[2];
		dist2 = p->vNormal[0] * emaxs[0] + p->vNormal[1] * emaxs[1] + p->vNormal[2] * emaxs[2];
		break;
	default:
		// shut up compiler
		dist1 = dist2 = 0;
		break;
	}

	if (dist1 >= p->fDist)
		sides = 1;
	if (dist2 < p->fDist)
		sides |= 2;

	return sides;
}

std::vector<vec3> getPlaneIntersectVerts(std::vector<BSPPLANE>& planes)
{
	std::vector<vec3> intersectVerts;

	// https://math.stackexchange.com/questions/1883835/get-list-of-vertices-from-list-of-planes
	size_t numPlanes = planes.size();

	if (numPlanes < 2)
		return intersectVerts;

	for (size_t i = 0; i < numPlanes - 2; i++)
	{
		for (size_t j = i + 1; j < numPlanes - 1; j++)
		{
			for (size_t k = j + 1; k < numPlanes; k++)
			{
				vec3& n0 = planes[i].vNormal;
				vec3& n1 = planes[j].vNormal;
				vec3& n2 = planes[k].vNormal;
				float& d0 = planes[i].fDist;
				float& d1 = planes[j].fDist;
				float& d2 = planes[k].fDist;

				float t = n0.x * (n1.y * n2.z - n1.z * n2.y) +
					n0.y * (n1.z * n2.x - n1.x * n2.z) +
					n0.z * (n1.x * n2.y - n1.y * n2.x);

				if (abs(t) < ON_EPSILON)
				{
					continue;
				}

				// don't use crossProduct because it's less accurate
				//vec3 v = crossProduct(n1, n2)*d0 + crossProduct(n0, n2)*d1 + crossProduct(n0, n1)*d2;
				vec3 v(
					(d0 * (n1.z * n2.y - n1.y * n2.z) + d1 * (n0.y * n2.z - n0.z * n2.y) + d2 * (n0.z * n1.y - n0.y * n1.z)) / -t,
					(d0 * (n1.x * n2.z - n1.z * n2.x) + d1 * (n0.z * n2.x - n0.x * n2.z) + d2 * (n0.x * n1.z - n0.z * n1.x)) / -t,
					(d0 * (n1.y * n2.x - n1.x * n2.y) + d1 * (n0.x * n2.y - n0.y * n2.x) + d2 * (n0.y * n1.x - n0.x * n1.y)) / -t
				);

				bool validVertex = true;

				for (size_t m = 0; m < numPlanes; m++)
				{
					BSPPLANE& pm = planes[m];
					if (m != i && m != j && m != k && dotProduct(v, pm.vNormal) < pm.fDist + EPSILON)
					{
						validVertex = false;
						break;
					}
				}

				if (validVertex)
				{
					intersectVerts.push_back(v);
				}
			}
		}
	}

	return intersectVerts;
}

bool vertsAllOnOneSide(std::vector<vec3>& verts, BSPPLANE& plane)
{
	// check that all verts are on one side of the plane.
	int planeSide = 0;
	for (size_t k = 0; k < verts.size(); k++)
	{
		float d = dotProduct(verts[k], plane.vNormal) - plane.fDist;
		if (d < -0.04f)
		{
			if (planeSide == 1)
			{
				return false;
			}
			planeSide = -1;
		}
		if (d > 0.04f)
		{
			if (planeSide == -1)
			{
				return false;
			}
			planeSide = 1;
		}
	}

	return true;
}

std::vector<vec3> getTriangularVerts(std::vector<vec3>& verts)
{
	int i0 = 0;
	int i1 = -1;
	int i2 = -1;

	int count = 1;
	for (size_t i = 1; i < verts.size() && count < 3; i++)
	{
		if (verts[i] != verts[i0])
		{
			i1 = (int)i;
			break;
		}
		count++;
	}

	if (i1 == -1)
	{
		//print_log(get_localized_string(LANG_1011));
		return std::vector<vec3>();
	}

	for (size_t i = 1; i < verts.size(); i++)
	{
		if ((int)i == i1)
			continue;

		if (verts[i] != verts[i0] && verts[i] != verts[i1])
		{
			vec3 ab = (verts[i1] - verts[i0]).normalize();
			vec3 ac = (verts[i] - verts[i0]).normalize();
			if (abs(dotProduct(ab, ac) - 1.0) < EPSILON)
			{
				continue;
			}

			i2 = (int)i;
			break;
		}
	}

	if (i2 == -1)
	{
		//print_log(get_localized_string(LANG_1012));
		return std::vector<vec3>();
	}

	return { verts[i0], verts[i1], verts[i2] };
}

vec3 getNormalFromVerts(std::vector<vec3>& verts)
{
	std::vector<vec3> triangularVerts = getTriangularVerts(verts);

	if (triangularVerts.empty())
		return vec3();

	vec3 e1 = (triangularVerts[1] - triangularVerts[0]).normalize();
	vec3 e2 = (triangularVerts[2] - triangularVerts[0]).normalize();
	vec3 vertsNormal = crossProduct(e1, e2).normalize();

	return vertsNormal;
}

std::vector<vec2> localizeVerts(std::vector<vec3>& verts)
{
	std::vector<vec3> triangularVerts = getTriangularVerts(verts);

	if (triangularVerts.empty())
	{
		return std::vector<vec2>();
	}

	vec3 e1 = (triangularVerts[1] - triangularVerts[0]).normalize();
	vec3 e2 = (triangularVerts[2] - triangularVerts[0]).normalize();

	vec3 plane_z = crossProduct(e1, e2).normalize();
	vec3 plane_x = e1;
	vec3 plane_y = crossProduct(plane_z, plane_x).normalize();

	mat4x4 worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

	std::vector<vec2> localVerts(verts.size());
	for (size_t e = 0; e < verts.size(); e++)
	{
		localVerts[e] = (worldToLocal * vec4(verts[e], 1)).xy();
	}

	return localVerts;
}

std::vector<size_t> getSortedPlanarVertOrder(std::vector<vec3>& verts)
{

	std::vector<vec2> localVerts = localizeVerts(verts);
	if (localVerts.empty())
	{
		return std::vector<size_t>();
	}

	vec2 center = getCenter(localVerts);
	std::vector<size_t> orderedVerts;
	std::vector<size_t> remainingVerts;

	for (size_t i = 0; i < localVerts.size(); i++)
	{
		remainingVerts.push_back(i);
	}

	orderedVerts.push_back(remainingVerts[0]);
	vec2 lastVert = localVerts[0];
	remainingVerts.erase(remainingVerts.begin() + 0);
	localVerts.erase(localVerts.begin() + 0);
	for (size_t k = 0, sz = remainingVerts.size(); k < sz; k++)
	{
		size_t bestIdx = 0;
		float bestAngle = FLT_MAX;

		for (size_t i = 0; i < remainingVerts.size(); i++)
		{
			vec2 a = lastVert;
			vec2 b = localVerts[i];
			float a1 = atan2(a.x - center.x, a.y - center.y);
			float a2 = atan2(b.x - center.x, b.y - center.y);
			float angle = a2 - a1;
			if (angle < 0)
				angle += HL_PI * 2;

			if (angle < bestAngle)
			{
				bestAngle = angle;
				bestIdx = i;
			}
		}

		lastVert = localVerts[bestIdx];
		orderedVerts.push_back(remainingVerts[bestIdx]);
		remainingVerts.erase(remainingVerts.begin() + bestIdx);
		localVerts.erase(localVerts.begin() + bestIdx);
	}

	return orderedVerts;
}

std::vector<vec3> getSortedPlanarVerts(std::vector<vec3>& verts)
{
	std::vector<vec3> outVerts;
	std::vector<size_t> vertOrder = getSortedPlanarVertOrder(verts);
	if (vertOrder.empty())
	{
		return outVerts;
	}
	outVerts.resize(vertOrder.size());
	for (size_t i = 0; i < vertOrder.size(); i++)
	{
		outVerts[i] = verts[vertOrder[i]];
	}
	return outVerts;
}

bool pointInsidePolygon(std::vector<vec2>& poly, vec2 p)
{
	// https://stackoverflow.com/a/34689268
	bool inside = true;
	float lastd = 0;
	for (size_t i = 0; i < poly.size(); i++)
	{
		vec2& v1 = poly[i];
		vec2& v2 = poly[(i + 1) % poly.size()];

		if (abs(v1.x - p.x) < EPSILON && abs(v1.y - p.y) < EPSILON)
		{
			break; // on edge = inside
		}

		float d = (p.x - v1.x) * (v2.y - v1.y) - (p.y - v1.y) * (v2.x - v1.x);

		if ((d < 0 && lastd > 0) || (d > 0 && lastd < 0))
		{
			// point is outside of this edge
			inside = false;
			break;
		}
		lastd = d;
	}
	return inside;
}


#define DATA_OFFSET_OFFSET 0x000A
#define WIDTH_OFFSET 0x0012
#define HEIGHT_OFFSET 0x0016
#define BITS_PER_PIXEL_OFFSET 0x001C
#define HEADER_SIZE 14
#define INFO_HEADER_SIZE 40
#define NO_COMPRESION 0
#define MAX_NUMBER_OF_COLORS 0
#define ALL_COLORS_REQUIRED 0

void WriteBMP(const std::string& fileName, unsigned char* pixels, int width, int height, int bytesPerPixel)
{
	FILE* outputFile = NULL;
	fopen_s(&outputFile, fileName.c_str(), "wb");
	if (!outputFile)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1013));
		return;
	}
	//*****HEADER************//
	const char* BM = "BM";
	fwrite(&BM[0], 1, 1, outputFile);
	fwrite(&BM[1], 1, 1, outputFile);
	int paddedRowSize = (int)(4 * ceil((float)width / 4.0f)) * bytesPerPixel;
	int fileSize = paddedRowSize * height + HEADER_SIZE + INFO_HEADER_SIZE;
	fwrite(&fileSize, 4, 1, outputFile);
	int reserved = 0x0000;
	fwrite(&reserved, 4, 1, outputFile);
	int dataOffset = HEADER_SIZE + INFO_HEADER_SIZE;
	fwrite(&dataOffset, 4, 1, outputFile);

	//*******INFO*HEADER******//
	int infoHeaderSize = INFO_HEADER_SIZE;
	fwrite(&infoHeaderSize, 4, 1, outputFile);
	fwrite(&width, 4, 1, outputFile);
	fwrite(&height, 4, 1, outputFile);
	short planes = 1; //always 1
	fwrite(&planes, 2, 1, outputFile);
	short bitsPerPixel = (short)(bytesPerPixel * 8);
	fwrite(&bitsPerPixel, 2, 1, outputFile);
	//write compression
	int compression = NO_COMPRESION;
	fwrite(&compression, 4, 1, outputFile);
	//write image size(in bytes)
	int imageSize = width * height * bytesPerPixel;
	fwrite(&imageSize, 4, 1, outputFile);
	int resolutionX = 11811; //300 dpi
	int resolutionY = 11811; //300 dpi
	fwrite(&resolutionX, 4, 1, outputFile);
	fwrite(&resolutionY, 4, 1, outputFile);
	int colorsUsed = MAX_NUMBER_OF_COLORS;
	fwrite(&colorsUsed, 4, 1, outputFile);
	int importantColors = ALL_COLORS_REQUIRED;
	fwrite(&importantColors, 4, 1, outputFile);
	int i = 0;
	int unpaddedRowSize = width * bytesPerPixel;
	for (i = 0; i < height; i++)
	{
		int pixelOffset = ((height - i) - 1) * unpaddedRowSize;
		fwrite(&pixels[pixelOffset], 1, paddedRowSize, outputFile);
	}
	fclose(outputFile);
}

int ArrayXYtoId(int w, int x, int y)
{
	return x + (y * w);
}


bool dirExists(const std::string& dirName)
{
	return fs::exists(dirName) && fs::is_directory(dirName);
}

#ifndef WIN32
// mkdir_p for linux from https://gist.github.com/ChisholmKyle/0cbedcd3e64132243a39
int mkdir_p(const char* dir, const mode_t mode)
{
	const int PATH_MAX_STRING_SIZE = 256;
	char tmp[PATH_MAX_STRING_SIZE];
	char* p = NULL;
	struct stat sb;
	size_t len;

	/* copy path */
	len = strnlen(dir, PATH_MAX_STRING_SIZE);
	if (len == 0 || len == PATH_MAX_STRING_SIZE)
	{
		return -1;
	}
	memcpy(tmp, dir, len);
	tmp[len] = '\0';

	/* remove trailing slash */
	if (tmp[len - 1] == '/')
	{
		tmp[len - 1] = '\0';
	}

	/* check if path exists and is a directory */
	if (stat(tmp, &sb) == 0)
	{
		if (S_ISDIR(sb.st_mode))
		{
			return 0;
		}
	}

	/* recursive mkdir */
	for (p = tmp + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = 0;
			/* test path */
			if (stat(tmp, &sb) != 0)
			{
				/* path does not exist - create directory */
				if (mkdir(tmp, mode) < 0)
				{
					return -1;
				}
			}
			else if (!S_ISDIR(sb.st_mode))
			{
				/* not a directory */
				return -1;
			}
			*p = '/';
		}
	}
	/* test path */
	if (stat(tmp, &sb) != 0)
	{
		/* path does not exist - create directory */
		if (mkdir(tmp, mode) < 0)
		{
			return -1;
		}
	}
	else if (!S_ISDIR(sb.st_mode))
	{
		/* not a directory */
		return -1;
	}
	return 0;
}
#endif 

bool createDir(const std::string& dirName)
{
	if (dirExists(dirName))
		return true;
	fs::create_directories(dirName);
	if (dirExists(dirName))
		return true;
	return false;
}

void removeDir(const std::string& dirName)
{
	std::error_code e;
	fs::remove_all(dirName, e);
}


void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos)
	{
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
	}
}
void fixupPath(char* path, FIXUPPATH_SLASH startslash, FIXUPPATH_SLASH endslash)
{
	std::string tmpPath = path;
	fixupPath(tmpPath, startslash, endslash);
	memcpy(path, &tmpPath[0], tmpPath.size() + 1);
}

void fixupPath(std::string& path, FIXUPPATH_SLASH startslash, FIXUPPATH_SLASH endslash)
{
	if (path.empty())
		return;
	replaceAll(path, "\"", "");
	replaceAll(path, "\'", "");
	replaceAll(path, "/", "\\");
	replaceAll(path, "\\\\", "\\");
	replaceAll(path, "\\", "/");
	replaceAll(path, "//", "/");
	if (startslash == FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE)
	{
		if (path[0] != '\\' && path[0] != '/')
		{
			path = "/" + path;
		}
	}
	else if (startslash == FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE)
	{
		if (path[0] == '\\' || path[0] == '/')
		{
			path.erase(path.begin());
		}
	}

	if (endslash == FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE)
	{
		if (path.empty() || (path.back() != '\\' && path.back() != '/'))
		{
			path = path + "/";
		}
	}
	else if (endslash == FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE)
	{
		if (path.empty())
			return;

		if (path.back() == '\\' || path.back() == '/')
		{
			path.pop_back();
		}
	}

	replaceAll(path, "/", "\\");
	replaceAll(path, "\\\\", "\\");
	replaceAll(path, "\\", "/");
	replaceAll(path, "//", "/");
}

unsigned int g_console_colors = 0;
bool g_console_visibled = true;
void showConsoleWindow(bool show)
{
	g_console_visibled = show;
#ifdef WIN32		
	if (::GetConsoleWindow())
	{
		::ShowWindow(::GetConsoleWindow(), show ? SW_SHOW : SW_HIDE);
	}
#endif
}

#ifdef WIN32
void set_console_colors(unsigned int colors)
{
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!console)
		console = ::GetConsoleWindow();
	if (console)
	{
		colors = colors ? colors : (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		g_console_colors = colors;
		SetConsoleTextAttribute(console, colors);
	}
}
#else 
void set_console_colors(unsigned int colors)
{
	colors = colors ? colors : (PRINT_GREEN | PRINT_BLUE | PRINT_RED | PRINT_INTENSITY);
	g_console_colors = colors;
	if (colors == 0)
	{
		std::cout << "\x1B[0m";
		return;
	}
	const char* mode = colors & PRINT_INTENSITY ? "1" : "0";
	const char* color = "37";
	switch (colors & ~PRINT_INTENSITY)
	{
	case PRINT_RED:								color = "31"; break;
	case PRINT_GREEN:							color = "32"; break;
	case PRINT_RED | PRINT_GREEN:				color = "33"; break;
	case PRINT_BLUE:							color = "34"; break;
	case PRINT_RED | PRINT_BLUE:				color = "35"; break;
	case PRINT_GREEN | PRINT_BLUE:				color = "36"; break;
	case PRINT_GREEN | PRINT_BLUE | PRINT_RED:	color = "36"; break;
	}
	std::cout << "\x1B[" << mode << ";" << color << "m";
}
#endif


float AngleFromTextureAxis(vec3 axis, bool x, int type)
{
	float retval = 0.0f;

	if (type < 2)
	{
		if (x)
		{
			return -1.f * atan2(axis.y, axis.x) * (180.f / HL_PI);
		}
		else
		{
			return atan2(axis.x, axis.y) * (180.f / HL_PI);
		}
	}


	if (type < 4)
	{
		if (x)
		{
			return -1.f * atan2(axis.z, axis.y) * (180.f / HL_PI);
		}
		else
		{
			return atan2(axis.y, axis.z) * (180.f / HL_PI);
		}
	}

	if (type < 6)
	{
		if (x)
		{
			return -1.f * atan2(axis.z, axis.x) * (180.f / HL_PI);
		}
		else
		{
			return atan2(axis.x, axis.z) * (180.f / HL_PI);
		}
	}


	return retval;
}


vec3 AxisFromTextureAngle(float angle, bool x, int type)
{
	vec3 retval = vec3();


	if (type < 2)
	{
		if (x)
		{
			retval.y = -1.f * sin(angle / 180.f * HL_PI);
			retval.x = cos(angle / 180.f * HL_PI);
		}
		else
		{
			retval.x = -1.f * sin((angle + 180.f) / 180.f * HL_PI);
			retval.y = -1.f * cos((angle + 180.f) / 180.f * HL_PI);
		}
		return retval;
	}

	if (type < 4)
	{
		if (x)
		{
			retval.z = -1.f * sin(angle / 180.f * HL_PI);
			retval.y = cos(angle / 180.f * HL_PI);
		}
		else
		{
			retval.y = -1.f * sin((angle + 180.f) / 180.f * HL_PI);
			retval.z = -1.f * cos((angle + 180.f) / 180.f * HL_PI);
		}
		return retval;
	}


	if (type < 6)
	{
		if (x)
		{
			retval.z = -1.f * sin(angle / 180.f * HL_PI);
			retval.x = cos(angle / 180.f * HL_PI);
		}
		else
		{
			retval.x = -1.f * sin((angle + 180.f) / 180.f * HL_PI);
			retval.z = -1.f * cos((angle + 180.f) / 180.f * HL_PI);
		}
		return retval;
	}

	return retval;
}

// For issue when string.size > 0 but string length is zero ("\0\0\0" string for example)
size_t strlen(std::string str)
{
	return str.size() ? strlen(str.c_str()) : 0;
}

int ColorDistance(COLOR3 color, COLOR3 other)
{
	return (int)std::hypot(std::hypot(color.r - other.r, color.b - other.b), color.g - other.g);
}

int GetImageColors(COLOR3* image, int size, int max_colors)
{
	int colorCount = 0;
	COLOR3* palette = new COLOR3[size];
	memset(palette, 0, size * sizeof(COLOR3));
	for (int y = 0; y < size; y++)
	{
		int paletteIdx = -1;
		for (int k = 0; k < colorCount; k++)
		{
			if (image[y] == palette[k])
			{
				paletteIdx = k;
				break;
			}
		}
		if (paletteIdx == -1)
		{
			if (colorCount > max_colors + 2)
				break; // Just for speed reason
			palette[colorCount] = image[y];
			paletteIdx = colorCount;
			colorCount++;
		}
	}
	delete[]palette;
	return colorCount;
}

void SimpeColorReduce(COLOR3* image, int size)
{
	// Fast change count of grayscale
	std::vector<COLOR3> colorset;
	for (unsigned char i = 255; i > 0; i--)
	{
		colorset.emplace_back(COLOR3(i, i, i));
	}

	for (int i = 0; i < size; i++)
	{
		for (auto& color : colorset)
		{
			if (ColorDistance(image[i], color) <= 3)
			{
				image[i] = color;
			}
		}
	}
}

std::set<std::string> traced_path_list;

bool FindPathInAssets(Bsp* map, const std::string& filename, std::string& outpath, bool tracesearch)
{
	int fPathId = 1;
	if (fileExists(filename))
	{
		outpath = filename;
		return true;
	}

	tracesearch = tracesearch && g_settings.verboseLogs;

	if (traced_path_list.count(filename))
		tracesearch = false;
	else
		traced_path_list.insert(filename);

	std::ostringstream outTrace;
	// First search path directly
	if (tracesearch)
	{
		outTrace << "-------------START PATH TRACING-------------\n";
		outTrace << "Search paths [" << fPathId++ << "] : [" << filename.c_str() << "]\n";
	}
	if (fileExists(filename))
	{
		outpath = filename;
		return true;
	}
	if (tracesearch)
	{
		outTrace << "Search paths [" << fPathId++ << "] : [" << ("./" + filename) << "]\n";
	}
	if (fileExists("./" + filename))
	{
		outpath = "./" + filename;
		return true;
	}
	if (tracesearch)
	{
		outTrace << "Search paths [" << fPathId++ << "] : [" << (g_working_dir + filename) << "]\n";
	}
	if (fileExists(g_working_dir + filename))
	{
		outpath = g_working_dir + filename;
		return true;
	}

	if (tracesearch)
	{
		outTrace << "Search paths [" << fPathId++ << "] : [" << (g_game_dir + filename) << "]\n";
	}
	if (fileExists(g_game_dir + filename))
	{
		outpath = g_game_dir + filename;
		return true;
	}

	// Next search path in fgd directories
	for (auto const& dir : g_settings.fgdPaths)
	{
		if (dir.enabled)
		{
			std::string fixedfilename = filename;
			fixupPath(fixedfilename, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);
			std::string dirpath = dir.path;
			for (int i = 0; i < 3; i++)
			{
				fixupPath(dirpath, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE);
				dirpath = stripFileName(fs::path(dirpath).string());
				fixupPath(dirpath, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);

				if (tracesearch)
				{
					outTrace << "Search paths [" << fPathId++ << "] : [" << (dirpath + fixedfilename) << "]\n";
				}
				if (fileExists(dirpath + fixedfilename))
				{
					outpath = dirpath + fixedfilename;
					return true;
				}

				if (tracesearch)
				{
					outTrace << "Search paths [" << fPathId++ << "] : [" << (dirpath + basename(fixedfilename)) << "]\n";
				}
				if (fileExists(dirpath + basename(fixedfilename)))
				{
					outpath = dirpath + basename(fixedfilename);
					return true;
				}

				if (tracesearch)
				{
					outTrace << "Search paths [" << fPathId++ << "] : [" << ("./" + dirpath + fixedfilename) << "]\n";
				}
				if (fileExists("./" + dirpath + fixedfilename))
				{
					outpath = "./" + dirpath + fixedfilename;
					return true;
				}

				if (tracesearch)
				{
					outTrace << "Search paths [" << fPathId++ << "] : [" << (g_game_dir + dirpath + fixedfilename) << "]\n";
				}
				if (fileExists(g_game_dir + dirpath + fixedfilename))
				{
					outpath = g_game_dir + dirpath + fixedfilename;
					return true;
				}

			}
		}
	}

	// Next search path in assets directories
	for (auto const& dir : g_settings.resPaths)
	{
		if (dir.enabled)
		{
			if (tracesearch)
			{
				outTrace << "Search paths [" << fPathId++ << "] : [" << (dir.path + filename) << "]\n";
			}
			if (fileExists(dir.path + filename))
			{
				outpath = dir.path + filename;
				return true;
			}

			if (tracesearch)
			{
				outTrace << "Search paths [" << fPathId++ << "] : [" << (dir.path + basename(filename)) << "]\n";
			}
			if (fileExists(dir.path + basename(filename)))
			{
				outpath = dir.path + basename(filename);
				return true;
			}

			if (tracesearch)
			{
				outTrace << "Search paths [" << fPathId++ << "] : [" << ("./" + dir.path + filename) << "]\n";
			}
			if (fileExists("./" + dir.path + filename))
			{
				outpath = "./" + dir.path + filename;
				return true;
			}

			if (tracesearch)
			{
				outTrace << "Search paths [" << fPathId++ << "] : [" << (g_game_dir + dir.path + filename) << "]\n";
			}
			if (fileExists(g_game_dir + dir.path + filename))
			{
				outpath = g_game_dir + dir.path + filename;
				return true;
			}
		}
	}

	// End, search files in map directory relative
	if (map)
	{
		if (tracesearch)
		{
			outTrace << "Search paths [" << fPathId++ << "] : [" << (stripFileName(stripFileName(map->bsp_path)) + "/" + filename) << "]\n";
		}
		if (fileExists((stripFileName(stripFileName(map->bsp_path)) + "/" + filename)))
		{
			outpath = stripFileName(stripFileName(map->bsp_path)) + "/" + filename;
			return true;
		}
	}

	if (tracesearch)
	{
		outTrace << "-------------END PATH TRACING-------------\n";
		print_log("{}", outTrace.str());
	}
	return false;
}


void FixupAllSystemPaths()
{
	/* fixup gamedir can be only like C:/gamedir/ or /gamedir/ */
	fixupPath(g_settings.gamedir, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);
	if (!dirExists(g_settings.gamedir))
	{
		if (!dirExists("./" + g_settings.gamedir))
		{
			print_log(PRINT_RED | PRINT_INTENSITY, "Error{}: Gamedir {} not exits!", "[1]", g_settings.gamedir);
			print_log(PRINT_RED | PRINT_INTENSITY, "Error{}: Gamedir {} not exits!", "[2]", "./" + g_settings.gamedir);
		}
		else
		{
			g_game_dir = "./" + g_settings.gamedir;
		}
	}
	else
	{
		g_game_dir = g_settings.gamedir;
	}

	// first fix slashes and check if dir exists
	fixupPath(g_settings.workingdir, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);

	if (!dirExists(g_settings.workingdir))
	{
		/*
			fixup workingdir to relative
		*/
		fixupPath(g_settings.workingdir, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);

		if (!dirExists("./" + g_settings.workingdir))
		{
			if (!dirExists(g_game_dir + g_settings.workingdir))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, "Warning: Workdir {} not exits!\n", g_settings.workingdir);
				print_log(PRINT_RED | PRINT_INTENSITY, "Warning: Workdir {} not exits!\n", g_game_dir + g_settings.workingdir);
				print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "Using default path\n");
			}
			g_working_dir = g_game_dir + g_settings.workingdir;
			try
			{
				if (!dirExists(g_working_dir))
					createDir(g_working_dir);
			}
			catch (...)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, "Error: Can't create workdir at {} !\n", g_settings.workingdir);
				g_working_dir = "./";
			}
		}
		else
		{
			g_working_dir = "./" + g_settings.workingdir;
		}
	}
	else
	{
		g_working_dir = g_settings.workingdir;
	}

	for (auto& s : g_settings.fgdPaths)
	{
		// first fix slashes and check if file exists
		fixupPath(s.path, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);
		if (!fileExists(s.path))
		{
			// relative like relative/to/fgdname.fgd
			fixupPath(s.path, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);
		}
	}

	for (auto& s : g_settings.resPaths)
	{
		// first fix slashes and check if file exists
		fixupPath(s.path, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);
		if (!fileExists(s.path))
		{
			// relative like ./cstrike/ or valve/
			fixupPath(s.path, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);
		}
	}
}

float floatRound(float f) {
	return (float)((f >= 0 || (float)(int)f == f) ? (int)f : (int)f - 1);
}

void scaleImage(const COLOR4* inputImage, std::vector<COLOR4>& outputImage,
	int inputWidth, int inputHeight, int outputWidth, int outputHeight)
{
	outputImage.resize(outputWidth * outputHeight);

	float xScale = static_cast<float>(inputWidth) / outputWidth;
	float yScale = static_cast<float>(inputHeight) / outputHeight;

	for (int y = 0; y < outputHeight; y++) {
		float srcY = y * yScale;
		int srcY1 = static_cast<int>(srcY);
		int srcY2 = std::min(srcY1 + 1, inputHeight - 1);

		float yWeight = srcY - srcY1;

		for (int x = 0; x < outputWidth; x++) {
			float srcX = x * xScale;

			int srcX1 = static_cast<int>(srcX);
			int srcX2 = std::min(srcX1 + 1, inputWidth - 1);

			float xWeight = srcX - srcX1;

			COLOR4 pixel1 = inputImage[srcY1 * inputWidth + srcX1];
			COLOR4 pixel2 = inputImage[srcY1 * inputWidth + srcX2];
			COLOR4 pixel3 = inputImage[srcY2 * inputWidth + srcX1];
			COLOR4 pixel4 = inputImage[srcY2 * inputWidth + srcX2];

			COLOR4 interpolatedPixel;
			interpolatedPixel.r = static_cast<unsigned char>(
				(1 - xWeight) * ((1 - yWeight) * pixel1.r + yWeight * pixel3.r) +
				xWeight * ((1 - yWeight) * pixel2.r + yWeight * pixel4.r)
				);
			interpolatedPixel.g = static_cast<unsigned char>(
				(1 - xWeight) * ((1 - yWeight) * pixel1.g + yWeight * pixel3.g) +
				xWeight * ((1 - yWeight) * pixel2.g + yWeight * pixel4.g)
				);
			interpolatedPixel.b = static_cast<unsigned char>(
				(1 - xWeight) * ((1 - yWeight) * pixel1.b + yWeight * pixel3.b) +
				xWeight * ((1 - yWeight) * pixel2.b + yWeight * pixel4.b)
				);
			interpolatedPixel.a = static_cast<unsigned char>(
				(1 - xWeight) * ((1 - yWeight) * pixel1.a + yWeight * pixel3.a) +
				xWeight * ((1 - yWeight) * pixel2.a + yWeight * pixel4.a)
				);

			outputImage[y * outputWidth + x] = interpolatedPixel;
		}
	}
}

void scaleImage(const COLOR3* inputImage, std::vector<COLOR3>& outputImage,
	int inputWidth, int inputHeight, int outputWidth, int outputHeight) {
	if (inputWidth <= 0 || inputHeight <= 0 || outputWidth <= 0 || outputHeight <= 0) {
		// Invalid input dimensions
		return;
	}

	outputImage.resize(outputWidth * outputHeight);

	float xScale = static_cast<float>(inputWidth) / outputWidth;
	float yScale = static_cast<float>(inputHeight) / outputHeight;

	for (int y = 0; y < outputHeight; y++) {
		float srcY = y * yScale;

		int srcY1 = static_cast<int>(srcY);
		int srcY2 = std::min(srcY1 + 1, inputHeight - 1);

		float yWeight = srcY - srcY1;

		for (int x = 0; x < outputWidth; x++) {
			float srcX = x * xScale;
			int srcX1 = static_cast<int>(srcX);
			int srcX2 = std::min(srcX1 + 1, inputWidth - 1);

			float xWeight = srcX - srcX1;

			if (srcY1 < 0 || srcY1 >= inputHeight || srcY2 < 0 || srcY2 >= inputHeight ||
				srcX1 < 0 || srcX1 >= inputWidth || srcX2 < 0 || srcX2 >= inputWidth) {
				// Invalid source coordinates
				continue;
			}

			COLOR3 pixel1 = inputImage[srcY1 * inputWidth + srcX1];
			COLOR3 pixel2 = inputImage[srcY1 * inputWidth + srcX2];
			COLOR3 pixel3 = inputImage[srcY2 * inputWidth + srcX1];
			COLOR3 pixel4 = inputImage[srcY2 * inputWidth + srcX2];
			COLOR3 interpolatedPixel;
			interpolatedPixel.r = static_cast<unsigned char>(
				(1 - xWeight) * ((1 - yWeight) * pixel1.r + yWeight * pixel3.r) +
				xWeight * ((1 - yWeight) * pixel2.r + yWeight * pixel4.r)
				);
			interpolatedPixel.g = static_cast<unsigned char>(
				(1 - xWeight) * ((1 - yWeight) * pixel1.g + yWeight * pixel3.g) +
				xWeight * ((1 - yWeight) * pixel2.g + yWeight * pixel4.g)
				);
			interpolatedPixel.b = static_cast<unsigned char>(
				(1 - xWeight) * ((1 - yWeight) * pixel1.b + yWeight * pixel3.b) +
				xWeight * ((1 - yWeight) * pixel2.b + yWeight * pixel4.b)
				);

			outputImage[y * outputWidth + x] = interpolatedPixel;
		}
	}
}



std::string GetExecutableDirInternal(std::string arg_0_dir)
{
	std::string retdir = arg_0_dir;
	retdir = stripFileName(retdir);
	fixupPath(retdir, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);
	if (dirExists(retdir + "languages") && dirExists(retdir + "fonts"))
	{
		return retdir;
	}
	else
	{
		try
		{
			retdir = stripFileName(std::filesystem::canonical(arg_0_dir).string());
			fixupPath(retdir, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_CREATE);
			if (dirExists(retdir + "languages") && dirExists(retdir + "fonts"))
			{
				return retdir;
			}
		}
		catch (...)
		{

		}
#ifdef WIN32
		char path[MAX_PATH];
		GetModuleFileName(NULL, path, MAX_PATH);
		retdir = stripFileName(std::filesystem::canonical(path).string());
#else
		retdir = stripFileName(std::filesystem::canonical("/proc/self/exe").string());
		if (dirExists(retdir + "languages") && dirExists(retdir + "fonts"))
		{
			return retdir;
		}
#endif
	}
	return retdir;
}

std::string GetExecutableDir(std::string arg_0)
{
	fs::path retpath = arg_0.size() ? fs::path(arg_0) : fs::current_path();
	return GetExecutableDirInternal(retpath.string());
}

std::string GetExecutableDir(std::wstring arg_0)
{
	fs::path retpath = arg_0.size() ? fs::path(arg_0) : fs::current_path();
	return GetExecutableDirInternal(retpath.string());
}



std::vector<vec3> stretch_model(const std::vector<vec3>& vertices, float stretch_value)
{
	vec3 center_model = getCenter(vertices);

	std::vector<vec3> stretched_vertices;

	for (const vec3& vertex : vertices) {
		vec3 shifted_vertex = { vertex.x - center_model.x, vertex.y - center_model.y, vertex.z - center_model.z };
		vec3 stretched_vertex = { std::signbit(shifted_vertex.x) ? shifted_vertex.x - stretch_value : shifted_vertex.x + stretch_value ,
			std::signbit(shifted_vertex.y) ? shifted_vertex.y - stretch_value : shifted_vertex.y + stretch_value,
		std::signbit(shifted_vertex.z) ? shifted_vertex.z - stretch_value : shifted_vertex.z + stretch_value };
		vec3 final_vertex = { stretched_vertex.x + center_model.x, stretched_vertex.y + center_model.y, stretched_vertex.z + center_model.z };
		stretched_vertices.push_back(final_vertex);
	}

	return stretched_vertices;
}

std::vector<cVert> stretch_model(const std::vector<cVert>& vertices, float stretch_value)
{
	vec3 center_model = getCenter(vertices);

	std::vector<cVert> stretched_vertices;

	for (const cVert& vertex : vertices) {
		vec3 shifted_vertex = { vertex.pos.x - center_model.x, vertex.pos.y - center_model.y, vertex.pos.z - center_model.z };
		vec3 stretched_vertex = { std::signbit(shifted_vertex.x) ? shifted_vertex.x - stretch_value : shifted_vertex.x + stretch_value ,
			std::signbit(shifted_vertex.y) ? shifted_vertex.y - stretch_value : shifted_vertex.y + stretch_value,
		std::signbit(shifted_vertex.z) ? shifted_vertex.z - stretch_value : shifted_vertex.z + stretch_value };
		cVert final_vertex = vertex;
		final_vertex.pos = { stretched_vertex.x + center_model.x, stretched_vertex.y + center_model.y, stretched_vertex.z + center_model.z };
		stretched_vertices.push_back(final_vertex);
	}

	return stretched_vertices;
}

BSPPLANE getSeparatePlane(vec3 amin, vec3 amax, vec3 bmin, vec3 bmax, bool force)
{
	BSPPLANE separationPlane = BSPPLANE();

	// separating plane points toward the other map (b)
	if (bmin.x >= amax.x)
	{
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = { 1, 0, 0 };
		separationPlane.fDist = amax.x + (bmin.x - amax.x) * 0.5f;
	}
	else if (bmax.x <= amin.x)
	{
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = { -1, 0, 0 };
		separationPlane.fDist = bmax.x + (amin.x - bmax.x) * 0.5f;
	}
	else if (bmin.y >= amax.y)
	{
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, 1, 0 };
		separationPlane.fDist = bmin.y;
	}
	else if (bmax.y <= amin.y)
	{
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, -1, 0 };
		separationPlane.fDist = bmax.y;
	}
	else if (bmin.z >= amax.z)
	{
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, 1 };
		separationPlane.fDist = bmin.z;
	}
	else if (bmax.z <= amin.z)
	{
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, -1 };
		separationPlane.fDist = bmax.z;
	}
	else
	{
		if (force) // Tried generate valid, but overlapped plane
		{
			// Calculate the separation distances for each axis
			float dx = std::max(amax.x - bmin.x, bmax.x - amin.x);
			float dy = std::max(amax.y - bmin.y, bmax.y - amin.y);
			float dz = std::max(amax.z - bmin.z, bmax.z - amin.z);

			// Find the axis with the largest separation
			if (dx > dy && dx > dz)
			{
				separationPlane.nType = PLANE_ANYX;
				separationPlane.vNormal = { dx > 0 ? 1.0f : -1.0f, 0, 0 };
				separationPlane.fDist = dx > 0 ? amax.x + (bmin.x - amax.x) * 0.5f : amin.x + (bmax.x - amin.x) * 0.5f;
			}
			else if (dy > dx && dy > dz)
			{
				separationPlane.nType = PLANE_ANYY;
				separationPlane.vNormal = { 0, dy > 0 ? 1.0f : -1.0f, 0 };
				separationPlane.fDist = dy > 0 ? amax.y + (bmin.y - amax.y) * 0.5f : amin.y + (bmax.y - amin.y) * 0.5f;
			}
			else
			{
				separationPlane.nType = PLANE_ANYZ;
				separationPlane.vNormal = { 0, 0, dz > 0 ? 1.0f : -1.0f };
				separationPlane.fDist = dz > 0 ? amax.z + (bmin.z - amax.z) * 0.5f : amin.z + (bmax.z - amin.z) * 0.5f;
			}
		}
		else
		{
			separationPlane.nType = -1; // no simple separating axis

			print_log(PRINT_RED, get_localized_string(LANG_0239));
			print_log(PRINT_RED, "({:6.0f}, {:6.0f}, {:6.0f})", amin.x, amin.y, amin.z);
			print_log(PRINT_RED, " - ({:6.0f}, {:6.0f}, {:6.0f}) {}\n", amax.x, amax.y, amax.z, "MODEL1");

			print_log(PRINT_RED, "({:6.0f}, {:6.0f}, {:6.0f})", bmin.x, bmin.y, bmin.z);
			print_log(PRINT_RED, " - ({:6.0f}, {:6.0f}, {:6.0f}) {}\n", bmax.x, bmax.y, bmax.z, "MODEL2");
		}
	}

	return separationPlane;
}



std::vector<std::string> groupParts(std::vector<std::string>& ungrouped)
{
	std::vector<std::string> grouped;

	for (size_t i = 0; i < ungrouped.size(); i++)
	{
		if (stringGroupStarts(ungrouped[i]))
		{
			std::string groupedPart = ungrouped[i];
			i++;
			for (; i < ungrouped.size(); i++)
			{
				groupedPart += " " + ungrouped[i];
				if (stringGroupEnds(ungrouped[i]))
				{
					break;
				}
			}
			grouped.push_back(groupedPart);
		}
		else
		{
			grouped.push_back(ungrouped[i]);
		}
	}

	return grouped;
}

bool stringGroupStarts(const std::string& s)
{
	if (s.find('(') != std::string::npos)
	{
		return s.find(')') == std::string::npos;
	}

	size_t startStringPos = s.find('\"');
	if (startStringPos != std::string::npos)
	{
		size_t endStringPos = s.rfind('\"');
		return endStringPos == startStringPos || endStringPos == std::string::npos;
	}

	return false;
}

bool stringGroupEnds(const std::string& s)
{
	return s.find(')') != std::string::npos || s.find('\"') != std::string::npos;
}

std::string getValueInParens(std::string s)
{
	if (s.length() <= 2)
	{
		replaceAll(s, "(", "");
		replaceAll(s, ")", "");
		return s;
	}

	auto find1 = s.find('(');
	auto find2 = s.rfind(')');
	if (find1 == std::string::npos || find1 >= find2)
	{
		replaceAll(s, "(", "");
		replaceAll(s, ")", "");
		return s;
	}
	return s.substr(find1 + 1, (find2 - find1) - 1);
}

std::string getValueInQuotes(std::string s)
{
	if (s.length() <= 2)
	{
		replaceAll(s, "\"", "");
		return s;
	}

	auto find1 = s.find('\"');
	auto find2 = s.rfind('\"');
	if (find1 == std::string::npos || find1 == find2)
	{
		replaceAll(s, "\"", "");
		return s;
	}
	return s.substr(find1 + 1, (find2 - find1) - 1);
}



std::vector<cVert> removeDuplicateWireframeLines(const std::vector<cVert>& points) {
	std::unordered_set<std::pair<vec3, vec3>, pairHash> uniqueLines;
	std::vector<cVert> result;

	for (size_t i = 0; i < points.size(); i += 2) {
		cVert start = points[i];
		cVert end = points[i + 1];

		std::pair<vec3, vec3> line1(start.pos, end.pos);
		std::pair<vec3, vec3> line2(end.pos, start.pos);

		if (uniqueLines.count(line1) == 0 && uniqueLines.count(line2) == 0) {
			uniqueLines.insert(line1);
			uniqueLines.insert(line2);
			result.push_back(start);
			result.push_back(end);
		}
	}

	return result;
}

void removeColinearPoints(std::vector<vec3>& verts, float epsilon) {

	for (int i1 = 0; i1 < verts.size(); i1++)
	{
		bool colinear = false;
		for (int i2 = 0; !colinear && i2 < verts.size(); i2++)
		{
			for (int i3 = 0; i3 < verts.size(); i3++)
			{
				if (i1 == i2 || i1 == i3 || i2 == i3)
					continue;

				if (verts[i1].x == verts[i2].x &&
					verts[i2].x == verts[i3].x)
				{
					if (verts[i1].y == verts[i2].y &&
						verts[i2].y == verts[i3].y)
					{
						if (verts[i1].z > verts[i2].z && verts[i1].z < verts[i3].z)
						{
							colinear = true;
							break;
						}
					}
				}

				if (verts[i1].x == verts[i2].x &&
					verts[i2].x == verts[i3].x)
				{
					if (verts[i1].z == verts[i2].z &&
						verts[i2].z == verts[i3].z)
					{
						if (verts[i1].y > verts[i2].y && verts[i1].y < verts[i3].y)
						{
							colinear = true;
							break;
						}
					}
				}

				if (verts[i1].y == verts[i2].y &&
					verts[i2].y == verts[i3].y)
				{
					if (verts[i1].z == verts[i2].z &&
						verts[i2].z == verts[i3].z)
					{
						if (verts[i1].x > verts[i2].x && verts[i1].x < verts[i3].x)
						{
							colinear = true;
							break;
						}
					}
				}
			}
		}
		if (colinear)
		{
			verts.erase(verts.begin() + i1);
			i1--;
		}
	}
}

vec3 getCentroid(std::vector<vec3>& hullVerts)
{
	vec3 centroid;
	for (size_t i = 0; i < hullVerts.size(); i++)
	{
		centroid += hullVerts[i];
	}
	return centroid / (float)hullVerts.size();
}


bool checkCollision(const vec3& obj1Mins, const vec3& obj1Maxs, const vec3& obj2Mins, const vec3& obj2Maxs) {
	// Check for overlap in x dimension
	if (obj1Maxs.x < obj2Mins.x || obj1Mins.x > obj2Maxs.x) {
		return false; // No overlap, no collision
	}

	// Check for overlap in y dimension
	if (obj1Maxs.y < obj2Mins.y || obj1Mins.y > obj2Maxs.y) {
		return false; // No overlap, no collision
	}

	// Check for overlap in z dimension
	if (obj1Maxs.z < obj2Mins.z || obj1Mins.z > obj2Maxs.z) {
		return false; // No overlap, no collision
	}

	// If there is overlap in all dimensions, there is a collision
	return true;
}



#ifdef WIN32
#include <io.h>
#define STDERR_FILENO 2
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#else
#include <sys/wait.h>
#endif

std::string Process::quoteIfNecessary(std::string toQuote)
{
	if (toQuote.find(" ") != std::string::npos)
	{
		toQuote = '\"' + toQuote + '\"';
	}

	return toQuote;
}

Process::Process(std::string program) : _program(program), _arguments()
{
}

Process& Process::arg(std::string arg)
{
	_arguments.push_back(arg);
	return *this;
}

std::string Process::getCommandlineString()
{
	std::stringstream cmdline;
	cmdline << quoteIfNecessary(_program);

	for (std::vector<std::string>::iterator arg = _arguments.begin(); arg != _arguments.end(); ++arg)
	{
		cmdline << " " << quoteIfNecessary(*arg);
	}

	return cmdline.str();
}

int Process::executeAndWait(int sin, int sout, int serr)
{
#ifdef WIN32
	STARTUPINFO startInfo;
	ZeroMemory(&startInfo, sizeof(startInfo));
	startInfo.cb = sizeof(startInfo);
	startInfo.dwFlags = NULL;
	// convert file descriptors to win32 handles
	if (sin != 0)
	{
		startInfo.dwFlags = STARTF_USESTDHANDLES;
		startInfo.hStdInput = (HANDLE)_get_osfhandle(sin);
	}
	if (sout != 0)
	{
		startInfo.dwFlags = STARTF_USESTDHANDLES;
		startInfo.hStdOutput = (HANDLE)_get_osfhandle(sout);
	}
	if (serr != 0)
	{
		startInfo.dwFlags = STARTF_USESTDHANDLES;
		startInfo.hStdError = (HANDLE)_get_osfhandle(serr);
	}

	PROCESS_INFORMATION procInfo;
	if (CreateProcessA(NULL, const_cast<char*>(getCommandlineString().c_str()), NULL, NULL, true, 0, NULL, NULL, &startInfo, &procInfo) == 0)
	{
		int lasterror = GetLastError();
		LPTSTR strErrorMessage = NULL;
		FormatMessage(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
			NULL,
			lasterror,
			0,
			(LPTSTR)(&strErrorMessage),
			0,
			NULL);
		print_log(PRINT_RED, "CreateProcess({}) failed with error {} = {}\n", getCommandlineString(), lasterror, strErrorMessage);
		return -1;
	}

	// Wait until child process exits.
	WaitForSingleObject(procInfo.hProcess, INFINITE);

	DWORD exitCode;
	GetExitCodeProcess(procInfo.hProcess, &exitCode);

	// Close process and thread handles.
	CloseHandle(procInfo.hProcess);
	CloseHandle(procInfo.hThread);

	return exitCode;
#else
	std::vector<char*> execvpArguments;

	char* program_c_string = new char[_program.size() + 1];
	strcpy(program_c_string, _program.c_str());
	execvpArguments.push_back(program_c_string);

	for (std::vector<std::string>::iterator arg = _arguments.begin(); arg != _arguments.end(); ++arg)
	{
		char* c_string = new char[(*arg).size() + 1];
		strcpy(c_string, arg->c_str());
		execvpArguments.push_back(c_string);
	}

	execvpArguments.push_back(NULL);

	int status;
	pid_t pid;

	if ((pid = fork()) < 0) {
		perror("fork");
		return -1;
	}

	if (pid == 0) {
		/*if (sin != 0) {
			close(0);  dup(sin);
		}
		if (sout != 1) {
			close(1);  dup(sout);
		}
		if (serr != 2) {
			close(2);  dup(serr);
		}*/

		execvp(_program.c_str(), &execvpArguments[0]);
		perror(("Error executing " + _program).c_str());
		exit(1);
	}

	for (std::vector<char*>::iterator arg = execvpArguments.begin(); arg != execvpArguments.end(); ++arg)
	{
		delete[] * arg;
	}

	while (wait(&status) != pid);
	return status;
#endif
}



std::vector<float> solve_uv_matrix_svd(const std::vector<std::vector<float>>& matrix, const std::vector<float>& vector)
{
	// Construct the augmented matrix
	std::vector<std::vector<float>> augmentedMatrix(3, std::vector<float>(5));
	for (size_t i = 0; i < 3; ++i) {
		for (size_t j = 0; j < 4; ++j) {
			augmentedMatrix[i][j] = matrix[i][j];
		}
		augmentedMatrix[i][4] = vector[i];
	}

	// Perform Gaussian elimination
	for (size_t i = 0; i < 3; ++i) {
		// Find the row with the largest pivot element
		size_t maxRow = i;
		float maxPivot = std::abs(augmentedMatrix[i][i]);
		for (size_t j = i + 1; j < 3; ++j) {
			if (std::abs(augmentedMatrix[j][i]) > maxPivot) {
				maxRow = j;
				maxPivot = std::abs(augmentedMatrix[j][i]);
			}
		}

		// Swap the current row with the row with the largest pivot element
		if (maxRow != i) {
			std::swap(augmentedMatrix[i], augmentedMatrix[maxRow]);
		}

		// Perform row operations to eliminate the lower triangular elements
		for (size_t j = i + 1; j < 3; ++j) {
			float factor = augmentedMatrix[j][i] / augmentedMatrix[i][i];
			for (size_t k = i; k < 5; ++k) {
				augmentedMatrix[j][k] -= factor * augmentedMatrix[i][k];
			}
		}
	}

	// Perform back substitution to solve for the solution vector
	std::vector<float> solution(4);
	for (int i = 2; i >= 0; --i) {
		float sum = augmentedMatrix[i][4];
		for (size_t j = i + 1; j < 3; ++j) {
			sum -= augmentedMatrix[i][j] * solution[j];
		}
		solution[i] = sum / augmentedMatrix[i][i];
	}

	return solution;
}

void calculateTextureInfo(BSPTEXTUREINFO& texinfo, const std::vector<vec3>& vertices, const std::vector<vec2>& uvs)
{
	// Check if the number of vertices and UVs is valid
	if (vertices.size() != 3 || uvs.size() != 3) {
		throw std::invalid_argument("Exactly 3 vertices and 3 UVs are required");
	}

	// Construct the vertices matrix with 3 rows, 4 columns
	std::vector<std::vector<float>> verticesMat(3, std::vector<float>(4));
	for (size_t i = 0; i < 3; ++i) {
		verticesMat[i][0] = vertices[i].x;
		verticesMat[i][1] = vertices[i].y;
		verticesMat[i][2] = vertices[i].z;
		verticesMat[i][3] = 1.0f;
	}

	// Split the UV coordinates
	std::vector<float> uvsU(3);
	std::vector<float> uvsV(3);
	for (size_t i = 0; i < 3; ++i) {
		uvsU[i] = uvs[i].x;
		uvsV[i] = uvs[i].y;
	}

	std::vector<float> solU = solve_uv_matrix_svd(verticesMat, uvsU);
	vec3 vS(solU[0], solU[1], solU[2]); // Extract vS vector
	float shiftS = solU[3]; // Extract shiftS value

	std::vector<float> solV = solve_uv_matrix_svd(verticesMat, uvsV);
	vec3 vT(solV[0], solV[1], solV[2]); // Extract vT vector
	float shiftT = solV[3]; // Extract shiftT value

	texinfo.vS = vS;
	texinfo.vT = vT;
	texinfo.shiftS = shiftS;
	texinfo.shiftT = shiftT;
}

void getTrueTexSize(int& width, int& height, int maxsize)
{
	float aspectRatio = static_cast<float>(width) / height;

	int newWidth = width;
	int newHeight = height;

	if (newWidth > maxsize) {
		newWidth = maxsize;
		newHeight = static_cast<int>(newWidth / aspectRatio);
	}

	if (newHeight > maxsize) {
		newHeight = maxsize;
		newWidth = static_cast<int>(newHeight * aspectRatio);
	}

	if (newWidth % 16 != 0) {
		newWidth = ((newWidth + 15) / 16) * 16;
	}

	if (newHeight % 16 != 0) {
		newHeight = ((newHeight + 15) / 16) * 16;
	}

	if (newWidth == 0)
		newWidth = 16;

	if (newHeight == 0)
		newHeight = 16;


	width = newWidth;
	height = newHeight;
}
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

bool DebugKeyPressed = false;
ProgressMeter g_progress;
int g_render_flags;
std::vector<std::string> g_log_buffer;
std::vector<unsigned short> g_color_buffer;

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

bool copyFile(const std::string& fileName, const std::string& fileName2)
{
	if (fileExists(fileName))
		return false;
	if (!fileExists(fileName2))
		return false;
	return fs::copy_file(fileName, fileName2);
}

std::streampos fileSize(const std::string& filePath)
{
	std::streampos fsize = 0;
	std::ifstream file(filePath, std::ios::binary);

	fsize = file.tellg();
	file.seekg(0, std::ios::end);
	fsize = file.tellg() - fsize;
	file.close();

	return fsize;
}

std::vector<std::string> splitString(std::string s, const std::string& delimiter, int maxParts)
{
	std::vector<std::string> split;
	size_t pos;
	while ((pos = s.find(delimiter)) != std::string::npos && (maxParts == 0 || split.size() < maxParts - 1)) {
		if (pos != 0) {
			split.push_back(s.substr(0, pos));
		}
		s.erase(0, pos + delimiter.length());
	}
	if (!s.empty())
		split.push_back(s);
	return split;
}

std::vector<std::string> splitStringIgnoringQuotes(std::string s, const std::string& delimitter)
{
	std::vector<std::string> split;
	if (s.empty() || delimitter.empty())
		return split;

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
			for (int i = 0; i < delimitPos; i++)
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
		sz += sizeof(short) /* pal count */;

		if (palette)
			sz += sizeof(COLOR3) * 256; // pallette + padding

		for (int i = 0; i < MIPLEVELS; i++)
		{
			sz += (bspTexture->nWidth >> i) * (bspTexture->nHeight >> i);
		}

		//sz = (sz + 3) & ~3; // 4 bytes padding
	}
	return sz;
}

float clamp(float val, float min, float max)
{
	if (val > max)
	{
		return max;
	}
	else if (val < min)
	{
		return min;
	}
	return val;
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

vec2 getCenter(std::vector<vec2>& verts)
{
	vec2 maxs = vec2(-FLT_MAX_COORD, -FLT_MAX_COORD);
	vec2 mins = vec2(FLT_MAX_COORD, FLT_MAX_COORD);

	for (int i = 0; i < verts.size(); i++)
	{
		expandBoundingBox(verts[i], mins, maxs);
	}

	return mins + (maxs - mins) * 0.5f;
}

vec3 getCenter(std::vector<vec3>& verts)
{
	vec3 maxs = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);
	vec3 mins = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);

	for (int i = 0; i < verts.size(); i++)
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
	maxs = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);
	mins = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);

	for (int i = 0; i < verts.size(); i++)
	{
		expandBoundingBox(verts[i], mins, maxs);
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

				for (int m = 0; m < numPlanes; m++)
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
	for (int k = 0; k < verts.size(); k++)
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
	for (int i = 1; i < verts.size() && count < 3; i++)
	{
		if (verts[i] != verts[i0])
		{
			i1 = i;
			break;
		}
		count++;
	}

	if (i1 == -1)
	{
		//print_log(get_localized_string(LANG_1011));
		return std::vector<vec3>();
	}

	for (int i = 1; i < verts.size(); i++)
	{
		if (i == i1)
			continue;

		if (verts[i] != verts[i0] && verts[i] != verts[i1])
		{
			vec3 ab = (verts[i1] - verts[i0]).normalize();
			vec3 ac = (verts[i] - verts[i0]).normalize();
			if (abs(dotProduct(ab, ac) - 1.0) < EPSILON)
			{
				continue;
			}

			i2 = i;
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
	for (int e = 0; e < verts.size(); e++)
	{
		localVerts[e] = (worldToLocal * vec4(verts[e], 1)).xy();
	}

	return localVerts;
}

std::vector<int> getSortedPlanarVertOrder(std::vector<vec3>& verts)
{

	std::vector<vec2> localVerts = localizeVerts(verts);
	if (localVerts.empty())
	{
		return std::vector<int>();
	}

	vec2 center = getCenter(localVerts);
	std::vector<int> orderedVerts;
	std::vector<int> remainingVerts;

	for (int i = 0; i < localVerts.size(); i++)
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
		float bestAngle = FLT_MAX_COORD;

		for (size_t i = 0; i < remainingVerts.size(); i++)
		{
			vec2 a = lastVert;
			vec2 b = localVerts[i];
			float a1 = atan2(a.x - center.x, a.y - center.y);
			float a2 = atan2(b.x - center.x, b.y - center.y);
			float angle = a2 - a1;
			if (angle < 0)
				angle += PI * 2;

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
	std::vector<int> vertOrder = getSortedPlanarVertOrder(verts);
	if (vertOrder.empty())
	{
		return outVerts;
	}
	outVerts.resize(vertOrder.size());
	for (int i = 0; i < vertOrder.size(); i++)
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
	for (int i = 0; i < poly.size(); i++)
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
std::string GetCurrentDir()
{
	return g_current_dir + "/";
}
unsigned short g_console_colors = 0;

#ifdef WIN32
void set_console_colors(unsigned short colors)
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
void set_console_colors(unsigned short colors)
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
			return -1.f * atan2(axis.y, axis.x) * (180.f / PI);
		}
		else
		{
			return atan2(axis.x, axis.y) * (180.f / PI);
		}
	}


	if (type < 4)
	{
		if (x)
		{
			return -1.f * atan2(axis.z, axis.y) * (180.f / PI);
		}
		else
		{
			return atan2(axis.y, axis.z) * (180.f / PI);
		}
	}

	if (type < 6)
	{
		if (x)
		{
			return -1.f * atan2(axis.z, axis.x) * (180.f / PI);
		}
		else
		{
			return atan2(axis.x, axis.z) * (180.f / PI);
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
			retval.y = -1.f * sin(angle / 180.f * PI);
			retval.x = cos(angle / 180.f * PI);
		}
		else
		{
			retval.x = -1.f * sin((angle + 180.f) / 180.f * PI);
			retval.y = -1.f * cos((angle + 180.f) / 180.f * PI);
		}
		return retval;
	}

	if (type < 4)
	{
		if (x)
		{
			retval.z = -1.f * sin(angle / 180.f * PI);
			retval.y = cos(angle / 180.f * PI);
		}
		else
		{
			retval.y = -1.f * sin((angle + 180.f) / 180.f * PI);
			retval.z = -1.f * cos((angle + 180.f) / 180.f * PI);
		}
		return retval;
	}


	if (type < 6)
	{
		if (x)
		{
			retval.z = -1.f * sin(angle / 180.f * PI);
			retval.x = cos(angle / 180.f * PI);
		}
		else
		{
			retval.x = -1.f * sin((angle + 180.f) / 180.f * PI);
			retval.z = -1.f * cos((angle + 180.f) / 180.f * PI);
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

int GetImageColors(COLOR3* image, int size)
{
	int colorCount = 0;
	COLOR3* palette = new COLOR3[size];
	memset(palette, 0, size * sizeof(COLOR3));
	for (int y = 0; y < size / 2; y++)
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
			if (colorCount >= 300)
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
		colorset.push_back(COLOR3(i, i, i));
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

bool FindPathInAssets(Bsp* map, const std::string& path, std::string& outpath, bool tracesearch)
{
	int fPathId = 1;
	if (fileExists(path))
	{
		outpath = path;
		return true;
	}

	tracesearch = tracesearch && g_settings.verboseLogs;

	std::ostringstream outTrace;

	if (tracesearch)
	{
		outTrace << "-------------START PATH TRACING-------------\n";
		outTrace << "Search paths [" << fPathId++ << "] : [" << path.c_str() << "]\n";
	}
	if (fileExists(path))
	{
		outpath = path;
		return true;
	}
	if (tracesearch)
	{
		outTrace << "Search paths [" << fPathId++ << "] : [" << (GetCurrentDir() + path) << "]\n";
	}
	if (fileExists(GetCurrentDir() + path))
	{
		outpath = GetCurrentDir() + path;
		return true;
	}
	if (tracesearch)
	{
		outTrace << "Search paths [" << fPathId++ << "] : [" << (g_working_dir + path) << "]\n";
	}
	if (fileExists(g_working_dir + path))
	{
		outpath = g_working_dir + path;
		return true;
	}

	if (tracesearch)
	{
		outTrace << "Search paths [" << fPathId++ << "] : [" << (g_game_dir + path) << "]\n";
	}
	if (fileExists(g_game_dir + path))
	{
		outpath = g_game_dir + path;
		return true;
	}

	for (auto const& dir : g_settings.resPaths)
	{
		if (dir.enabled)
		{
#ifndef WIN32
			if (tracesearch)
			{
				outTrace << "Search paths [" << fPathId++ << "] : [" << (dir.path + path) << "]\n";
			}
			if (fileExists(dir.path + path))
			{
				outpath = dir.path + path;
				return true;
			}
#else 
			if (tracesearch && dir.path.find(':') == std::string::npos)
			{
				outTrace << "Search paths [" << fPathId++ << "] : [" << (dir.path + path) << "]\n";
			}
			if (dir.path.find(':') == std::string::npos && fileExists(dir.path + path))
			{
				outpath = dir.path + path;
				return true;
			}
			if (tracesearch)
			{
				outTrace << "Search paths [" << fPathId++ << "] : [" << (GetCurrentDir() + dir.path + path) << "]\n";
			}
			if (fileExists(GetCurrentDir() + dir.path + path))
			{
				outpath = GetCurrentDir() + dir.path + path;
				return true;
			}
			if (tracesearch)
			{
				outTrace << "Search paths [" << fPathId++ << "] : [" << (g_game_dir + dir.path + path) << "]\n";
			}
			if (fileExists(g_game_dir + dir.path + path))
			{
				outpath = g_game_dir + dir.path + path;
				return true;
			}

#endif
		}
	}

	if (map)
	{
		if (tracesearch)
		{
			outTrace << "Search paths [" << fPathId++ << "] : [" << (stripFileName(stripFileName(map->bsp_path)) + "/" + path) << "]\n";
		}
		if (fileExists((stripFileName(stripFileName(map->bsp_path)) + "/" + path)))
		{
			outpath = stripFileName(stripFileName(map->bsp_path)) + "/" + path;
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
		if (!dirExists(GetCurrentDir() + g_settings.gamedir))
		{
			print_log(PRINT_RED | PRINT_INTENSITY, "Error{}: Gamedir {} not exits!", "[1]", g_settings.gamedir);
			print_log(PRINT_RED | PRINT_INTENSITY, "Error{}: Gamedir {} not exits!", "[2]", GetCurrentDir() + g_settings.gamedir);
		}
		else
		{
			g_game_dir = GetCurrentDir() + g_settings.gamedir;
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

		if (!dirExists(GetCurrentDir() + g_settings.workingdir))
		{
			if (!dirExists(g_game_dir + g_settings.workingdir))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, "Error{}: Workdir {} not exits!", "[1]", g_settings.workingdir);
				print_log(PRINT_RED | PRINT_INTENSITY, "Error{}: Workdir {} not exits!", "[2]", g_game_dir + g_settings.workingdir);
			}
			else
			{
				g_working_dir = g_game_dir + g_settings.workingdir;
			}
		}
		else
		{
			g_working_dir = GetCurrentDir() + g_settings.workingdir;
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
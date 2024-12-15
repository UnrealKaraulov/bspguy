#include "bsptypes.h"
#include <cstring>
#include <cmath>

BSPEDGE16::BSPEDGE16()
{
	iVertex[0] = iVertex[1] = 0;
}

BSPEDGE16::BSPEDGE16(unsigned int v1, unsigned int v2)
{
	iVertex[0] = (unsigned short)v1;
	iVertex[1] = (unsigned short)v2;
}

BSPEDGE16::BSPEDGE16(unsigned short v1, unsigned short v2)
{
	iVertex[0] = v1;
	iVertex[1] = v2;
}

BSPEDGE32::BSPEDGE32()
{
	iVertex[0] = iVertex[1] = 0;
}

BSPEDGE32::BSPEDGE32(unsigned int v1, unsigned int v2)
{
	iVertex[0] = v1;
	iVertex[1] = v2;
}

bool BSPPLANE::update_plane(vec3 newNormal, float fdist, bool flip)
{
	float fx = std::fabs(newNormal.x);
	float fy = std::fabs(newNormal.y);
	float fz = std::fabs(newNormal.z);
	int planeType = PLANE_ANYZ;
	bool shouldFlip = false;
	if (fx > 1.0f - EPSILON && fy < EPSILON && fz < EPSILON)
	{
		planeType = PLANE_X;
		if (newNormal.x < 0.0f) shouldFlip = true;
	}
	else if (fy > 1.0f - EPSILON && fz < EPSILON && fx < EPSILON)
	{
		planeType = PLANE_Y;
		if (newNormal.y < 0.0f) shouldFlip = true;
	}
	else if (fz > 1.0f - EPSILON && fx < EPSILON && fy < EPSILON)
	{
		planeType = PLANE_Z;
		if (newNormal.z < 0.0f) shouldFlip = true;
	}
	else
	{
		if (fx > fy && fx > fz)
		{
			planeType = PLANE_ANYX;
		}
		else if (fy > fx && fy > fz)
		{
			planeType = PLANE_ANYY;
		}
		else
		{
			planeType = PLANE_ANYZ;
		}
	}

	// TODO: negative normals seem to be working for submodels. Just doesn't work for head nodes?
	if (shouldFlip && flip)
	{
		newNormal *= -1;
		fdist = -fdist;
	}

	fDist = fdist;
	vNormal = newNormal;
	nType = planeType;

	return shouldFlip;
}


bool BSPPLANE::update_plane(bool flip)
{
	return update_plane(vNormal, fDist, flip);
}


bool BSPLEAF16::isEmpty()
{
	BSPLEAF16 emptyLeaf;
	memset(&emptyLeaf, 0, sizeof(BSPLEAF16));
	emptyLeaf.nContents = CONTENTS_SOLID;

	return memcmp(&emptyLeaf, this, sizeof(BSPLEAF16)) == 0;
}


bool BSPLEAF32::isEmpty()
{
	BSPLEAF32 emptyLeaf;
	memset(&emptyLeaf, 0, sizeof(BSPLEAF32));
	emptyLeaf.nContents = CONTENTS_SOLID;

	return memcmp(&emptyLeaf, this, sizeof(BSPLEAF32)) == 0;
}

bool BSPLEAF32A::isEmpty()
{
	BSPLEAF32A emptyLeaf;
	memset(&emptyLeaf, 0, sizeof(BSPLEAF32A));
	emptyLeaf.nContents = CONTENTS_SOLID;

	return memcmp(&emptyLeaf, this, sizeof(BSPLEAF32A)) == 0;
}

std::vector<int> getDiffModels(LumpState& oldLump, LumpState& newLump)
{
	std::vector<int> updateModels{};
	if (newLump.lumps[LUMP_MODELS].empty())
		return updateModels;

	if (oldLump.lumps[LUMP_MODELS].empty() && newLump.lumps[LUMP_MODELS].size())
	{
		int addModelCount = (int)(newLump.lumps[LUMP_MODELS].size() / sizeof(BSPMODEL));
		for (int i = 0; i < addModelCount; i++)
		{
			updateModels.push_back(i);
		}
		return updateModels;
	}

	if (newLump.lumps[LUMP_MODELS].size() > oldLump.lumps[LUMP_MODELS].size())
	{
		int curModelCount = (int)(oldLump.lumps[LUMP_MODELS].size() / sizeof(BSPMODEL));
		int addModelCount = (int)((newLump.lumps[LUMP_MODELS].size() - newLump.lumps[LUMP_MODELS].size()) / sizeof(BSPMODEL));
		for (int i = curModelCount; i < curModelCount + addModelCount; i++)
		{
			updateModels.push_back(i);
		}
	}

	size_t modelLumpCount = std::min(newLump.lumps[LUMP_MODELS].size(), oldLump.lumps[LUMP_MODELS].size());

	modelLumpCount /= sizeof(BSPMODEL);

	BSPMODEL* listOld = (BSPMODEL*)oldLump.lumps[LUMP_MODELS].data();
	BSPMODEL* listNew = (BSPMODEL*)newLump.lumps[LUMP_MODELS].data();

	for (size_t i = 0; i < modelLumpCount; i++)
	{
		if (memcmp(&listOld[i], &listNew[i], sizeof(BSPMODEL)) != 0)
		{
			updateModels.push_back((int)i);
		}
	}

	return updateModels;
}
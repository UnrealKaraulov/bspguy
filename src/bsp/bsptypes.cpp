#include "bsptypes.h"
#include <math.h>
#include <string.h>

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
	double fx = abs(newNormal.x);
	double fy = abs(newNormal.y);
	double fz = abs(newNormal.z);
	int planeType = PLANE_ANYZ;
	bool shouldFlip = false;
	if (fx > 0.9999f)
	{
		planeType = PLANE_X;
		if (newNormal.x < 0.0f) shouldFlip = true;
	}
	else if (fy > 0.9999f)
	{
		planeType = PLANE_Y;
		if (newNormal.y < 0.0f) shouldFlip = true;
	}
	else if (fz > 0.9999f)
	{
		planeType = PLANE_Z;
		if (newNormal.z < 0.0f) shouldFlip = true;
	}
	else
	{
		if (fx > fy && fx > fz)
		{
			planeType = PLANE_ANYX;
			//if (newNormal.x < 0) shouldFlip = true;
		}
		else if (fy > fx && fy > fz)
		{
			planeType = PLANE_ANYY;
			//if (newNormal.y < 0) shouldFlip = true;
		}
		else
		{
			planeType = PLANE_ANYZ;
			//if (newNormal.z < 0) shouldFlip = true;
		}
	}

	// TODO: negative normals seem to be working for submodels. Just doesn't work for head nodes?
	if (shouldFlip)
	{
		newNormal *= -1;
		fdist = -fdist;
	}

	fDist = fdist;
	vNormal = newNormal;
	nType = planeType;

	return shouldFlip;
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
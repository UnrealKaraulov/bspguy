#include "lang.h"
#include "rad.h"
#include "winding.h"
#include "Bsp.h"
#include "log.h"

#include <algorithm>

//
// BEGIN COPIED QRAD CODE
//


// ApplyMatrix: (x y z 1)T -> matrix * (x y z 1)T
void ApplyMatrix(const mat4x4& m, const vec3 in, vec3& out)
{
	out = (m * vec4(in, 1.0f)).xyz();
}

bool InvertMatrix(mat4x4 m, mat4x4& m_inverse)
{
	m_inverse = m.invert();
	return true;
}


void TranslateWorldToTex(Bsp* bsp, int facenum, mat4x4& m)
{
	BSPFACE32* f = &bsp->faces[facenum];
	const BSPPLANE fp = bsp->getPlaneFromFace(f);
	BSPTEXTUREINFO* ti = &bsp->texinfos[f->iTextureInfo];

	for (int i = 0; i < 3; i++)
	{
		m.m[i * 4 + 0] = ((float*)&ti->vS)[i];
		m.m[i * 4 + 1] = ((float*)&ti->vT)[i]; 
		m.m[i * 4 + 2] = ((float*)&fp.vNormal)[i]; 
	}

	m.m[3 * 4 + 0] = ti->shiftS;
	m.m[3 * 4 + 1] = ti->shiftT;
	m.m[3 * 4 + 2] = -fp.fDist; 
}

bool CanFindFacePosition(Bsp* bsp, int facenum, int imins[2], int imaxs[2])
{
	float texmins[2] = { 0.0f,0.0f };
	float texmaxs[2] = { 0.0f,0.0f };

	mat4x4 worldtotex;
	worldtotex.loadIdentity();

	BSPFACE32* f = &bsp->faces[facenum];
	if (f->iTextureInfo < 0 || bsp->texinfos[f->iTextureInfo].nFlags & TEX_SPECIAL)
	{
		imins[0] = imins[1] = imaxs[0] = imaxs[1] = 1;
		return false;
	}

	if (f->nEdges <= 1)
	{
		print_log(PRINT_RED, "CanFindFacePosition error empty face {}!\n", facenum);
		imins[0] = imins[1] = imaxs[0] = imaxs[1] = 1;
		return false;
	}

	BSPTEXTUREINFO& tex = bsp->texinfos[f->iTextureInfo];

	TranslateWorldToTex(bsp, facenum, worldtotex);

	bool canInvert = true;
	worldtotex.invert(&canInvert);

	if (!canInvert)
	{
		print_log(PRINT_RED, "CanFindFacePosition error InvertMatrix face {}!\n", facenum);
		imins[0] = imins[1] = imaxs[0] = imaxs[1] = 1;
		return false;
	}

	Winding facewinding(bsp, *f);
	Winding texwinding(facewinding.m_Points.size());

	for (int x = 0; x < facewinding.m_Points.size(); x++)
	{
		ApplyMatrix(worldtotex, facewinding.m_Points[x], texwinding.m_Points[x]);
		texwinding.m_Points[x][2] = 0.0;
	}

	/* texwinding.RemoveColinearPoints(); this is critical error because already removed ? */

	if (texwinding.m_Points.size() == 0)
	{
		print_log(PRINT_RED, "CanFindFacePosition error texwinding face {} [facewind size {}]!\n", facenum, facewinding.m_Points.size());
		imins[0] = imins[1] = imaxs[0] = imaxs[1] = 1;
		return false;
	}

	for (int x = 0; x < texwinding.m_Points.size(); x++)
	{
		for (int k = 0; k < 2; k++)
		{
			if (x == 0 || texwinding.m_Points[x][k] < texmins[k])
				texmins[k] = texwinding.m_Points[x][k];
			if (x == 0 || texwinding.m_Points[x][k] > texmaxs[k])
				texmaxs[k] = texwinding.m_Points[x][k];
		}
	}

	unsigned int tmpTextureStep = bsp->CalcFaceTextureStep(facenum);

	for (int k = 0; k < 2; k++)
	{
		imins[k] = (int)floor(texmins[k] / tmpTextureStep);
		imaxs[k] = (int)ceil(texmaxs[k] / tmpTextureStep );
	}

	int w = imaxs[0] - imins[0] + 1;
	int h = imaxs[1] - imins[1] + 1;
	if (w <= 0 || h <= 0 || w * h > 99999999)
	{
		print_log(PRINT_RED, "CanFindFacePosition invalid size! face {}!\n", facenum);
		imins[0] = imins[1] = imaxs[0] = imaxs[1] = 1;
		return false;
	}
	return true;
}

float CalculatePointVecsProduct(const volatile float* point, const volatile float* vecs)
{
	volatile double val;
	volatile double tmp;

	val = (double)point[0] * (double)vecs[0]; // always do one operation at a time and save to memory
	tmp = (double)point[1] * (double)vecs[1];
	val = val + tmp;
	tmp = (double)point[2] * (double)vecs[2];
	val = val + tmp;
	val = val + (double)vecs[3];

	return (float)val;
}

//
//bool GetFaceExtentsX(Bsp* bsp, int facenum, int mins_out[2], int maxs_out[2])
//{
//	BSPFACE32* f;
//	float mins[2], maxs[2], val;
//	int i, j, e;
//	vec3* v;
//	BSPTEXTUREINFO* tex;
//
//	f = &bsp->faces[facenum];
//
//	mins[0] = mins[1] = 999999.0f;
//	maxs[0] = maxs[1] = -999999.0f;
//
//	tex = &bsp->texinfos[f->iTextureInfo];
//
//	for (i = 0; i < f->nEdges; i++)
//	{
//		e = bsp->surfedges[f->iFirstEdge + i];
//		if (e >= 0)
//		{
//			v = &bsp->verts[bsp->edges[e].iVertex[0]];
//		}
//		else
//		{
//			v = &bsp->verts[bsp->edges[-e].iVertex[1]];
//		}
//		for (j = 0; j < 2; j++)
//		{
//			// The old code: val = v->point[0] * tex->vecs[j][0] + v->point[1] * tex->vecs[j][1] + v->point[2] * tex->vecs[j][2] + tex->vecs[j][3];
//			//   was meant to be compiled for x86 under MSVC (prior to VS 11), so the intermediate values were stored as 64-bit double by default.
//			// The new code will produce the same result as the old code, but it's portable for different platforms.
//			// See this article for details: Intermediate Floating-Point Precision by Bruce-Dawson http://www.altdevblogaday.com/2012/03/22/intermediate-floating-point-precision/
//
//			// The essential reason for having this ugly code is to get exactly the same value as the counterpart of game engine.
//			// The counterpart of game engine is the function CalcFaceExtents in HLSDK.
//			// So we must also know how Valve compiles HLSDK. I think Valve compiles HLSDK with VC6.0 in the past.
//			vec3& axis = j == 0 ? tex->vS : tex->vT;
//			val = CalculatePointVecsProduct((float*)v, (float*)&axis);
//
//			if (val < mins[j])
//			{
//				mins[j] = val;
//			}
//			if (val > maxs[j])
//			{
//				maxs[j] = val;
//			}
//		}
//	}
//
//	for (i = 0; i < 2; i++)
//	{
//		mins_out[i] = (int)floor(mins[i] / TEXTURE_STEP);
//		maxs_out[i] = (int)ceil(maxs[i] / TEXTURE_STEP);
//	}
//	return true;
//}
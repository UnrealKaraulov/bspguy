#include "lang.h"
#include "rad.h"
#include "winding.h"
#include "Bsp.h"
#include <algorithm>

//
// BEGIN COPIED QRAD CODE
//


// ApplyMatrix: (x y z 1)T -> matrix * (x y z 1)T
void ApplyMatrix(const matrix_t& m, const vec3 in, vec3& out)
{
	int i;

	VectorCopy(m.v[3], out);
	for (i = 0; i < 3; i++)
	{
		VectorMA(out, in[i], m.v[i], out);
	}
}

bool InvertMatrix(const matrix_t& m, matrix_t& m_inverse)
{
	float texplanes[2][4]{};
	float faceplane[4]{};
	int i;
	float texaxis[2][3]{};
	float normalaxis[3]{};
	float det, sqrlen1, sqrlen2, sqrlen3;
	float texorg[3]{};

	for (i = 0; i < 4; i++)
	{
		texplanes[0][i] = m.v[i][0];
		texplanes[1][i] = m.v[i][1];
		faceplane[i] = m.v[i][2];
	}

	sqrlen1 = DotProduct(texplanes[0], texplanes[0]);
	sqrlen2 = DotProduct(texplanes[1], texplanes[1]);
	sqrlen3 = DotProduct(faceplane, faceplane);
	if (sqrlen1 <= EPSILON * EPSILON || sqrlen2 <= EPSILON * EPSILON || sqrlen3 <= EPSILON * EPSILON)
		// s gradient, t gradient or face normal is too close to 0
	{
		return false;
	}

	CrossProduct(texplanes[0], texplanes[1], normalaxis);
	det = DotProduct(normalaxis, faceplane);
	if (det * det <= sqrlen1 * sqrlen2 * sqrlen3 * EPSILON * EPSILON)
		// s gradient, t gradient and face normal are coplanar
	{
		return false;
	}
	VectorScale(normalaxis, 1 / det, normalaxis);

	CrossProduct(texplanes[1], faceplane, texaxis[0]);
	VectorScale(texaxis[0], 1 / det, texaxis[0]);

	CrossProduct(faceplane, texplanes[0], texaxis[1]);
	VectorScale(texaxis[1], 1 / det, texaxis[1]);

	VectorScale(normalaxis, -faceplane[3], texorg);
	VectorMA(texorg, -texplanes[0][3], texaxis[0], texorg);
	VectorMA(texorg, -texplanes[1][3], texaxis[1], texorg);

	VectorCopy(texaxis[0], m_inverse.v[0]);
	VectorCopy(texaxis[1], m_inverse.v[1]);
	VectorCopy(normalaxis, m_inverse.v[2]);
	VectorCopy(texorg, m_inverse.v[3]);
	return true;
}

const BSPPLANE getPlaneFromFace(Bsp* bsp, const BSPFACE32* const face)
{
	if (!face)
	{
		print_log(get_localized_string(LANG_0990));
		return BSPPLANE();
	}

	if (face->nPlaneSide)
	{
		BSPPLANE backplane = bsp->planes[face->iPlane];
		backplane.fDist = -backplane.fDist;
		backplane.vNormal = backplane.vNormal.invert();
		return backplane;
	}
	else
	{
		return bsp->planes[face->iPlane];
	}
}

void TranslateWorldToTex(Bsp* bsp, int facenum, matrix_t& m)
// without g_face_offset
{
	BSPFACE32* f;
	BSPTEXTUREINFO* ti;

	int i;

	f = &bsp->faces[facenum];
	const BSPPLANE fp = getPlaneFromFace(bsp, f);
	ti = &bsp->texinfos[f->iTextureInfo];
	for (i = 0; i < 3; i++)
	{
		m.v[i][0] = ((float*)&ti->vS)[i];
		m.v[i][1] = ((float*)&ti->vT)[i];
	}
	m.v[0][2] = fp.vNormal.x;
	m.v[1][2] = fp.vNormal.y;
	m.v[2][2] = fp.vNormal.z;

	m.v[3][0] = ti->shiftS;
	m.v[3][1] = ti->shiftT;
	m.v[3][2] = -fp.fDist;
}

bool CanFindFacePosition(Bsp* bsp, int facenum)
{
	float texmins[2]{}, texmaxs[2]{};
	int imins[2]{}, imaxs[2]{};

	matrix_t worldtotex;
	matrix_t textoworld;

	BSPFACE32* f = &bsp->faces[facenum];
	if (bsp->texinfos[f->iTextureInfo].nFlags & TEX_SPECIAL)
	{
		return false;
	}

	TranslateWorldToTex(bsp, facenum, worldtotex);
	if (!InvertMatrix(worldtotex, textoworld))
	{
		return false;
	}

	Winding facewinding(bsp, *f);
	Winding texwinding(facewinding.m_NumPoints);
	for (int x = 0; x < facewinding.m_NumPoints; x++)
	{
		ApplyMatrix(worldtotex, facewinding.m_Points[x], texwinding.m_Points[x]);
		texwinding.m_Points[x][2] = 0.0;
	}
	texwinding.RemoveColinearPoints();

	if (texwinding.m_NumPoints == 0)
	{
		return false;
	}

	for (int x = 0; x < texwinding.m_NumPoints; x++)
	{
		for (int k = 0; k < 2; k++)
		{
			if (x == 0 || texwinding.m_Points[x][k] < texmins[k])
				texmins[k] = texwinding.m_Points[x][k];
			if (x == 0 || texwinding.m_Points[x][k] > texmaxs[k])
				texmaxs[k] = texwinding.m_Points[x][k];
		}
	}

	for (int k = 0; k < 2; k++)
	{
		imins[k] = (int)floor(texmins[k] / TEXTURE_STEP + 0.5 - ON_EPSILON);
		imaxs[k] = (int)ceil(texmaxs[k] / TEXTURE_STEP - 0.5 + ON_EPSILON);
	}

	int w = imaxs[0] - imins[0] + 1;
	int h = imaxs[1] - imins[1] + 1;
	if (w <= 0 || h <= 0 || (double)w * (double)h > 99999999)
	{
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

void GetFaceLightmapSize(Bsp* bsp, int facenum, int size[2])
{
	int mins[2];
	int maxs[2];

	GetFaceExtents(bsp, facenum, mins, maxs);

	size[0] = (maxs[0] - mins[0]);
	size[1] = (maxs[1] - mins[1]);

	size[0] += 1;
	size[1] += 1;
	//return !badSurfaceExtents;
}

int GetFaceLightmapSizeBytes(Bsp* bsp, int facenum)
{
	int size[2];
	GetFaceLightmapSize(bsp, facenum, size);
	BSPFACE32& face = bsp->faces[facenum];

	int lightmapCount = 0;
	for (int k = 0; k < MAX_LIGHTMAPS; k++)
	{
		lightmapCount += face.nStyles[k] != 255;
	}
	return size[0] * size[1] * lightmapCount * sizeof(COLOR3);
}

int GetFaceSingleLightmapSizeBytes(Bsp* bsp, int facenum)
{
	int size[2];
	GetFaceLightmapSize(bsp, facenum, size);
	BSPFACE32& face = bsp->faces[facenum];
	if (face.nStyles[0] == 255)
		return 0;
	return size[0] * size[1] * sizeof(COLOR3);
}


bool GetFaceExtents(Bsp* bsp, int facenum, int mins_out[2], int maxs_out[2])
{
	float mins[2], maxs[2], val;

	bool retval = true;

	mins[0] = mins[1] = 999999.0f;
	maxs[0] = maxs[1] = -999999.0f;

	BSPFACE32 & face = bsp->faces[facenum];

	BSPTEXTUREINFO tex = bsp->texinfos[face.iTextureInfo];

	for (int i = 0; i < face.nEdges; i++)
	{
		vec3 v = vec3();
		int e = bsp->surfedges[face.iFirstEdge + i];
		if (e >= 0)
		{
			v = bsp->verts[bsp->edges[e].iVertex[0]];
		}
		else
		{
			v = bsp->verts[bsp->edges[-e].iVertex[1]];
		}
		for (int j = 0; j < 2; j++)
		{
			float* axis = j == 0 ? (float*)&tex.vS : (float*)&tex.vT;
			val = CalculatePointVecsProduct((float*)&v, axis);

			if (val < mins[j])
			{
				mins[j] = val;
			}
			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	

	for (int i = 0; i < 2; i++)
	{
		mins_out[i] = (int)floor(mins[i] / TEXTURE_STEP);
		maxs_out[i] = (int)ceil(maxs[i] / TEXTURE_STEP);;

		if (!(tex.nFlags & TEX_SPECIAL) && (maxs_out[i] - mins_out[i]) * TEXTURE_STEP > 4096)
		{
			retval = false;
			print_log(get_localized_string(LANG_0991),facenum,(int)((maxs_out[i] - mins_out[i]) * TEXTURE_STEP));
			mins_out[i] = 0;
			maxs_out[i] = 1;
		}

		if (maxs_out[i] - mins_out[i] < 0)
		{
			retval = false;
			print_log(PRINT_RED, "Face {} extents are bad. Map can crash.", facenum);
			mins_out[i] = 0;
			maxs_out[i] = 1;
		}
	}
	return retval;
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

bool CalcFaceExtents(Bsp* bsp, lightinfo_t* l)
{
	int bmins[2];
	int bmaxs[2];
	if (!GetFaceExtents(bsp, l->surfnum, bmins, bmaxs))
	{
		for (int i = 0; i < 2; i++)
		{
			l->texmins[i] = 0;
			l->texsize[i] = 0;
		}
		return false;
	}
	for (int i = 0; i < 2; i++)
	{
		l->texmins[i] = bmins[i];
		l->texsize[i] = bmaxs[i] - bmins[i];
	}
	return true;
}

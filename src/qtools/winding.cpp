#include "lang.h"
#pragma warning(disable: 4018) //amckern - 64bit - '<' Singed/Unsigned Mismatch

#include "winding.h"
#include "rad.h"
#include "bsptypes.h"
#include "Bsp.h"

Winding& Winding::operator=(const Winding& other)
{
	if (&other == this)
		return *this;
	delete[] m_Points;
	m_NumPoints = other.m_NumPoints;
	m_MaxPoints = (m_NumPoints + 3) & ~3;   // groups of 4

	m_Points = new vec3[m_MaxPoints];
	memcpy(m_Points, other.m_Points, sizeof(vec3) * m_NumPoints);
	return *this;
}

Winding::Winding(size_t numpoints)
{
	m_NumPoints = numpoints;
	m_MaxPoints = (m_NumPoints + 3) & ~3;   // groups of 4

	m_Points = new vec3[m_MaxPoints];
	memset(m_Points, 0, sizeof(vec3) * m_NumPoints);
}


Winding::Winding()
{
	m_Points = NULL;
	m_NumPoints = 0;
	m_MaxPoints = 0;
}

Winding::Winding(const Winding& other)
{
	m_NumPoints = other.m_NumPoints;
	m_MaxPoints = (m_NumPoints + 3) & ~3;   // groups of 4

	m_Points = new vec3[m_MaxPoints];
	memcpy(m_Points, other.m_Points, sizeof(vec3) * m_NumPoints);
}

Winding::~Winding()
{
	delete[] m_Points;
}

Winding::Winding(const BSPPLANE& plane, float epsilon)
{
		int             i;
		float           max, v;
		vec3  org, vright, vup;

		org = vright = vup = vec3();
		// find the major axis               

		max = -BOGUS_RANGE;
		int x = -1;
		for (i = 0; i < 3; i++)
		{
			v = fabs(plane.vNormal[i]);
			if (v > max)
			{
				max = v;
				x = i;
			}
		}
		if (x == -1)
		{
			print_log(get_localized_string(LANG_1008));
		}

		

		switch (x)
		{
			case 0:
			case 1:
				vup[2] = 1;
				break;
			case 2:
				vup[0] = 1;
				break;
		}

		v = DotProduct(vup, plane.vNormal);
		VectorMA(vup, -v, plane.vNormal, vup);
		VectorNormalize(vup);

		VectorScale(plane.vNormal, plane.fDist, org);

		CrossProduct(vup, plane.vNormal, vright);

		VectorScale(vup, BOGUS_RANGE, vup);
		VectorScale(vright, BOGUS_RANGE, vright);

		// project a really big     axis aligned box onto the plane
		m_NumPoints = 4;
		m_MaxPoints = (m_NumPoints + 3) & ~3;   // groups of 4
		m_Points = new vec3[m_MaxPoints];

		VectorSubtract(org, vright, m_Points[0]);
		VectorAdd(m_Points[0], vup, m_Points[0]);

		VectorAdd(org, vright, m_Points[1]);
		VectorAdd(m_Points[1], vup, m_Points[1]);

		VectorAdd(org, vright, m_Points[2]);
		VectorSubtract(m_Points[2], vup, m_Points[2]);

		VectorSubtract(org, vright, m_Points[3]);
		VectorSubtract(m_Points[3], vup, m_Points[3]);
}

void Winding::getPlane(BSPPLANE& plane) 
{
	vec3          v1, v2;
	vec3          plane_normal;
	v1 = v2 = plane_normal = vec3();
	//hlassert(m_NumPoints >= 3);

	if (m_NumPoints >= 3)
	{
		VectorSubtract(m_Points[2], m_Points[1], v1);
		VectorSubtract(m_Points[0], m_Points[1], v2);

		CrossProduct(v2, v1, plane_normal);
		VectorNormalize(plane_normal);
		VectorCopy(plane_normal, plane.vNormal);               // change from vec_t
		plane.fDist = DotProduct(m_Points[0], plane.vNormal);
	}
	else
	{
		plane.vNormal = vec3();
		plane.fDist = 0.0;
	}
}

Winding::Winding(const std::vector<vec3>& points, float epsilon)
{
	m_NumPoints = points.size();
	m_MaxPoints = (m_NumPoints + 3) & ~3;
	m_Points = new vec3[m_NumPoints];

	unsigned i;
	for (i = 0; i < m_NumPoints && i < points.size(); i++)
	{
		m_Points[i] = points[i];
	}

	RemoveColinearPoints(
		epsilon
	);
}

Winding::Winding(Bsp* bsp, const BSPFACE32& face, float epsilon)
{
	int             se;
	vec3* dv;
	int             v;

	m_NumPoints = face.nEdges;
	m_MaxPoints = (m_NumPoints + 3) & ~3;
	m_Points = new vec3[m_NumPoints];

	unsigned i;
	for (i = 0; i < m_NumPoints && i < face.nEdges; i++)
	{
		se = bsp->surfedges[face.iFirstEdge + i];
		if (se < 0)
		{
			v = bsp->edges[-se].iVertex[1];
		}
		else
		{
			v = bsp->edges[se].iVertex[0];
		}

		dv = &bsp->verts[v];
		VectorCopy((float*)dv, m_Points[i]);
	}

	RemoveColinearPoints(
		epsilon
	);
}

// Remove the colinear point of any three points that forms a triangle which is thinner than ON_EPSILON
void Winding::RemoveColinearPoints(float epsilon)
{
	int	i;
	vec3 v1, v2;
	vec3 p1,  p2,  p3;
	for (i = 0; i < m_NumPoints; i++)
	{
		p1 = m_Points[(i + m_NumPoints - 1) % m_NumPoints];
		p2 = m_Points[i];
		p3 = m_Points[(i + 1) % m_NumPoints];
		VectorSubtract(p2, p1, v1);
		VectorSubtract(p3, p2, v2);
		// v1 or v2 might be close to 0
		if (DotProduct(v1, v2) * DotProduct(v1, v2) >= DotProduct(v1, v1) * DotProduct(v2, v2)
			- epsilon * epsilon * (DotProduct(v1, v1) + DotProduct(v2, v2) + epsilon * epsilon))
			// v2 == k * v1 + v3 && abs (v3) < ON_EPSILON || v1 == k * v2 + v3 && abs (v3) < ON_EPSILON
		{
			m_NumPoints--;
			for (; i < m_NumPoints; i++)
			{
				VectorCopy(m_Points[i + 1], m_Points[i]);
			}
			i = -1;
			continue;
		}
	}
}

bool Winding::Clip(BSPPLANE& split, bool keepon, float epsilon)
{
	float           dists[MAX_POINTS_ON_WINDING]{};
	int             sides[MAX_POINTS_ON_WINDING]{};
	int             counts[3]{};
	float           dot;
	int             i, j;

	counts[0] = counts[1] = counts[2] = 0;

	// determine sides for each point
	// do this exactly, with no epsilon so tiny portals still work
	for (i = 0; i < m_NumPoints; i++)
	{
		dot = DotProduct(m_Points[i], split.vNormal);
		dot -= split.fDist;
		dists[i] = dot;
		if (dot > epsilon)
		{
			sides[i] = SIDE_FRONT;
		}
		else if (dot < -epsilon)
		{
			sides[i] = SIDE_BACK;
		}
		else
		{
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];

	if (keepon && !counts[0] && !counts[1])
	{
		return true;
	}

	if (!counts[0])
	{
		delete[] m_Points;
		m_Points = NULL;
		m_NumPoints = 0;
		return false;
	}

	if (!counts[1])
	{
		return true;
	}

	size_t maxpts = m_NumPoints + 4; // can't use counts[0]+2 because of fp grouping errors
	unsigned newNumPoints = 0;
	vec3* newPoints = new vec3[maxpts];
	memset(newPoints, 0, sizeof(vec3) * maxpts);

	for (i = 0; i < m_NumPoints; i++)
	{
		vec3 p1 = m_Points[i];

		if (sides[i] == SIDE_ON)
		{
			VectorCopy(p1, newPoints[newNumPoints]);
			newNumPoints++;
			continue;
		}
		else if (sides[i] == SIDE_FRONT)
		{
			VectorCopy(p1, newPoints[newNumPoints]);
			newNumPoints++;
		}

		if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
		{
			continue;
		}

		// generate a split point
		vec3 mid;
		unsigned int tmp = i + 1;
		if (tmp >= m_NumPoints)
		{
			tmp = 0;
		}
		vec3 p2 = m_Points[tmp];
		dot = dists[i] / (dists[i] - dists[i + 1]);
		for (j = 0; j < 3; j++)
		{                                                  // avoid round off error when possible
			if (abs(split.vNormal[j] - 1.0f) < EPSILON)
				mid[j] = split.fDist;
			else if (abs(split.vNormal[j] - -1.0f) < EPSILON)
				mid[j] = -split.fDist;
			else
				mid[j] = p1[j] + dot * (p2[j] - p1[j]);
		}

		VectorCopy(mid, newPoints[newNumPoints]);
		newNumPoints++;
	}

	if (newNumPoints > maxpts)
	{
		print_log(get_localized_string(LANG_1009));
	}

	delete[] m_Points;
	m_Points = newPoints;
	m_NumPoints = newNumPoints;

	RemoveColinearPoints(
		epsilon
	);
	if (m_NumPoints == 0)
	{
		delete[] m_Points;
		m_Points = NULL;
		return false;
	}

	return true;
}

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
	m_Points = other.m_Points;
	return *this;
}

Winding::Winding(size_t numpoints)
{
	m_Points = std::vector<vec3>(numpoints);
}


Winding::Winding()
{
	m_Points = {};
}

Winding::Winding(const Winding& other)
{
	m_Points = other.m_Points;
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
	m_Points = std::vector<vec3>(4);

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

	if (m_Points.size() >= 3)
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
	m_Points = points;

	RemoveColinearPoints(
		epsilon
	);
}


Winding::Winding(Bsp* bsp, const BSPFACE32& face, float epsilon)
{
	int             se;

	const size_t NumPoints = face.nEdges;

	m_Points = std::vector<vec3>(NumPoints);

	unsigned i;
	for (i = 0; i < NumPoints && i < face.nEdges; i++)
	{
		se = bsp->surfedges[face.iFirstEdge + i];

		int v;

		if (se < 0)
		{
			v = bsp->edges[-se].iVertex[1];
		}
		else
		{
			v = bsp->edges[se].iVertex[0];
		}

		m_Points[i] = bsp->verts[v];
	}

	RemoveColinearPoints(
		epsilon
	);
}


void Winding::MergeVerts(Bsp* src, float epsilon)
{
	for (auto& v : m_Points)
	{
		for (int v2 = src->vertCount - 1; v2 >= 0; v2--)
		{
			if (src->verts[v2].equal(v, epsilon))
			{
				v = src->verts[v2];
				break;
			}
		}
	}
}

// Remove the colinear point of any three points that forms a triangle which is thinner than ON_EPSILON
void Winding::RemoveColinearPoints(float epsilon)
{
	int	i;
	vec3 v1, v2;
	vec3 p1, p2, p3;

	size_t NumPoints = m_Points.size();

	for (i = 0; i < NumPoints; i++)
	{
		p1 = m_Points[(i + NumPoints - 1) % NumPoints];
		p2 = m_Points[i];
		p3 = m_Points[(i + 1) % NumPoints];
		VectorSubtract(p2, p1, v1);
		VectorSubtract(p3, p2, v2);
		// v1 or v2 might be close to 0
		if (DotProduct(v1, v2) * DotProduct(v1, v2) >= DotProduct(v1, v1) * DotProduct(v2, v2)
			- epsilon * epsilon * (DotProduct(v1, v1) + DotProduct(v2, v2) + epsilon * epsilon))
			// v2 == k * v1 + v3 && abs (v3) < ON_EPSILON || v1 == k * v2 + v3 && abs (v3) < ON_EPSILON
		{
			NumPoints--;
			for (; i < NumPoints; i++)
			{
				VectorCopy(m_Points[i + 1], m_Points[i]);
			}
			i = -1;
			continue;
		}
	}

	m_Points.resize(NumPoints);
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
	for (i = 0; i < m_Points.size(); i++)
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
		m_Points.clear();
		return false;
	}

	if (!counts[1])
	{
		return true;
	}

	size_t maxpts = m_Points.size() + 4; // can't use counts[0]+2 because of fp grouping errors
	unsigned newNumPoints = 0;
	std::vector<vec3> newPoints = std::vector<vec3>(maxpts);

	for (i = 0; i < m_Points.size(); i++)
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
		if (tmp >= m_Points.size())
		{
			tmp = 0;
		}
		vec3 p2 = m_Points[tmp];
		dot = dists[i] / (dists[i] - dists[i + 1]);
		for (j = 0; j < 3; j++)
		{                                                  // avoid round off error when possible
			if (std::abs(split.vNormal[j] - 1.0f) < EPSILON)
				mid[j] = split.fDist;
			else if (std::abs(split.vNormal[j] - -1.0f) < EPSILON)
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

	m_Points = newPoints;

	RemoveColinearPoints(
		epsilon
	);
	if (m_Points.empty() == 0)
	{
		return false;
	}

	return true;
}

void Winding::Round(float epsilon)
{
	for (auto& p : m_Points)
	{
		for (int j = 0; j < 3; j++)
		{
			p[j] = round(p[j] / epsilon) * epsilon;
		}
	}
}

bool Winding::IsConvex(const BSPPLANE& plane, float epsilon)
{
	vec3 normal = plane.vNormal;
	vec3 delta;
	float dot;

	for (int i = 0; i < m_Points.size(); i++)
	{
		delta = m_Points[i] - m_Points[(i + 1) % m_Points.size()];
		dot = dotProduct(delta, normal);
		if (dot > epsilon)
			return false;
	}

	return true;
}

bool ArePointsOnALine(const std::vector<vec3>& points)
{
	if (points.size() < 3)
		return true;

	// Choose two reference points
	vec3 p1 = points[0];
	vec3 p2 = points[1];

	// Calculate the vector between the reference points
	vec3 v = p2 - p1;

	// Calculate the cross product of v and the vector between each other point and p1
	for (int i = 2; i < points.size(); i++)
	{
		vec3 u = points[i] - p1;
		vec3 cross = crossProduct(v, u);

		// If the cross product is zero, the points are on the same line
		if (cross.length() < EPSILON)
			return true;
	}

	// If the cross product is not zero for any pair of points, the points are not on the same line
	return false;
}


Winding* Winding::Merge(const Winding& other, const BSPPLANE& plane, float epsilon)
{
	vec3 p1, p2, p3, p4, back;
	Winding* newf = NULL;
	int			i, j, k, l;
	vec3		normal, delta;
	float		dot;
	bool	keep1, keep2;


	//
	// find a common edge
	//
	p1 = p2 = vec3();
	j = 0;

	for (i = 0; i < m_Points.size(); i++)
	{
		p1 = m_Points[i];
		p2 = m_Points[(i + 1) % m_Points.size()];
		for (j = 0; j < other.m_Points.size(); j++)
		{
			p3 = other.m_Points[j];
			p4 = other.m_Points[(j + 1) % other.m_Points.size()];
			for (k = 0; k < 3; k++)
			{
				if (ArePointsOnALine({ p1, p2, p3, p4
					}))
					break;
			} //end for
			if (k == 3)
				break;
		} //end for
		if (j < other.m_Points.size())
			break;
	} //end for

	if (i == m_Points.size())
	{
		return NULL;			// no matching edges
	}

	//
	// check slope of connected lines
	// if the slopes are colinear, the point can be removed
	//
	back = m_Points[(i + m_Points.size() - 1) % m_Points.size()];
	p1 = back - delta;
	vec3 planenormal = crossProduct(delta, normal);
	VectorNormalize(normal);

	back = other.m_Points[(j + 2) % other.m_Points.size()];
	back = p1 - delta;
	dot = dotProduct(delta, normal);
	if (dot > epsilon)
		return NULL;			// not a convex polygon
	keep1 = (dot < -epsilon);

	back = m_Points[(i + 2) % m_Points.size()];
	back = p2 - delta;
	planenormal = crossProduct(delta, normal);
	VectorNormalize(normal);

	back = other.m_Points[(j + other.m_Points.size() - 1) % other.m_Points.size()];
	back = p2 - delta;
	dot = dotProduct(delta, normal);
	if (dot > epsilon)
		return NULL;			// not a convex polygon
	keep2 = (dot < -epsilon);
	
	//
	// build the new polygon
	//
	newf = new Winding();

	// copy first polygon
	for (k = (i + 1) % m_Points.size(); k != i; k = (k + 1) % m_Points.size())
	{
		if (k == (i + 1) % m_Points.size() && !keep2)
			continue;

		newf->m_Points.push_back(m_Points[k]);
	}

	// copy second polygon
	for (l = (j + 1) % other.m_Points.size(); l != j; l = (l + 1) % other.m_Points.size())
	{
		if (l == (j + 1) % other.m_Points.size() && !keep1)
			continue;
		newf->m_Points.push_back(other.m_Points[l]);
	}

	if (!newf->IsConvex(plane, epsilon))
	{
		delete newf;
		return NULL;
	}

	return newf;
}

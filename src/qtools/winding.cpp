#include "lang.h"
#include "winding.h"
#include "rad.h"
#include "bsptypes.h"
#include "Bsp.h"
#include "log.h"


Winding& Winding::operator=(const Winding& other)
{
	if (&other == this)
		return *this;
	m_Points = other.m_Points;
	return *this;
}

Winding::Winding(int numpoints)
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

	max = -g_limits.fltMaxCoord;
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

void Winding::getPlane(BSPPLANE& plane) const
{
	if (m_Points.size() >= 3)
	{
		vec3 v1 = m_Points[2] - m_Points[1];
		vec3 v2 = m_Points[0] - m_Points[1];
		plane.vNormal = crossProduct(v2, v1).normalize();
		plane.fDist = dotProduct(m_Points[0], plane.vNormal);
	}
	else
	{
		plane.vNormal = vec3();
		plane.fDist = 0.0;
	}
}

Winding::Winding(Bsp* bsp, const BSPFACE32& face, float epsilon)
{
	m_Points = std::vector<vec3>(face.nEdges);

	for (int e = 0; e < face.nEdges; e++)
	{
		int edgeIdx = bsp->surfedges[face.iFirstEdge + e];
		BSPEDGE32& edge = bsp->edges[abs(edgeIdx)];

		int v = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];
		m_Points[e] = bsp->verts[v];
	}

	RemoveColinearPoints(epsilon);
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
	int NumPoints = (int)m_Points.size();

	if (NumPoints <= 2)
		return;

	for (int i = 0; i < NumPoints; i++)
	{
		vec3 p1 = m_Points[(i + NumPoints - 1) % NumPoints];
		vec3 p2 = m_Points[i];
		vec3 p3 = m_Points[(i + 1) % NumPoints];
		vec3 v1 = p2 - p1;
		vec3 v2 = p3 - p2;
		// v1 or v2 might be close to 0
		if (dotProduct(v1, v2) * dotProduct(v1, v2) >= dotProduct(v1, v1) * dotProduct(v2, v2)
			- epsilon * epsilon * (dotProduct(v1, v1) + dotProduct(v2, v2) + epsilon * epsilon))
			// v2 == k * v1 + v3 && abs (v3) < ON_EPSILON || v1 == k * v2 + v3 && abs (v3) < ON_EPSILON
		{
			NumPoints--;
			for (; i < NumPoints; i++)
			{
				m_Points[i] = m_Points[i + 1];
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
	size_t             i, j;

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
		size_t tmp = i + 1;
		if (tmp >= m_Points.size())
		{
			tmp = 0;
		}
		vec3 p2 = m_Points[tmp];
		dot = dists[i] / (dists[i] - dists[i + 1]);
		for (j = 0; j < 3ul; j++)
		{                                                  // avoid round off error when possible
			if (std::fabs(split.vNormal[j] - 1.0f) < EPSILON)
				mid[j] = split.fDist;
			else if (std::fabs(split.vNormal[j] - -1.0f) < EPSILON)
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

	m_Points = std::move(newPoints);

	RemoveColinearPoints(epsilon);

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

void Winding::Offset(vec3 Offset)
{
	for (auto& p : m_Points)
	{
		p += Offset;
	}
}

bool Winding::IsConvex()
{
	int numPoint = (int)(m_Points.size());
	float positiveArea = 0.0;
	float negativeArea = 0.0;

	static double tolerance = 1.0e-12;

	float a2 = 0.0f;
	vec3 maxCross(0.0, 0.0, 0.0);

	vec3 vecA = m_Points[1] - m_Points[0];
	vec3 vecB;
	for (int i = 2; i < numPoint; i++, vecA = vecB) {
		vecB = m_Points[i] - m_Points[0];
		vec3 c = crossProduct(vecA, vecB);
		float b2 = (c.x * c.x + c.y * c.y + c.z * c.z);
		if (b2 > a2) {
			a2 = b2;
			maxCross = c;
		}
	}

	vec3 unitNormal = maxCross.normalize();
	vecA = m_Points[0] - m_Points[numPoint - 1];
	for (int i = 1; i <= numPoint; i++, vecA = vecB) {
		vecB = m_Points[i % numPoint] - m_Points[i - 1];
		vec3 c = crossProduct(vecA, vecB);
		float b = dotProduct(c, unitNormal);
		if (b >= 0.0) {
			positiveArea += b;
		}
		else {
			negativeArea += b;
		}
	}

	return fabs(negativeArea) < tolerance * positiveArea;
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
	for (size_t i = 2; i < points.size(); i++)
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

	Winding* newf = NULL;


	//
	// find a common edge
	//
	vec3 p1 = vec3();
	vec3 p2 = vec3();


	size_t i = 0;
	size_t j = 0;

	for (i = 0; i < m_Points.size(); i++)
	{
		p1 = m_Points[i];
		p2 = m_Points[(i + 1) % m_Points.size()];
		for (j = 0; j < other.m_Points.size(); j++)
		{
			vec3 p3 = other.m_Points[j];
			vec3 p4 = other.m_Points[(j + 1) % other.m_Points.size()];


			if (p1.equal(p4, epsilon) && p2.equal(p3, epsilon))
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

	vec3 back = m_Points[(i + m_Points.size() - 1) % m_Points.size()];
	vec3 normal = crossProduct(plane.vNormal, p1 - back).normalize();

	back = other.m_Points[(j + 2) % other.m_Points.size()];
	float dot = dotProduct(back - p1, normal);
	if (dot > epsilon)
		return NULL;			// not a convex polygon

	bool keep1 = (dot < -epsilon);

	back = m_Points[(i + 2) % m_Points.size()];
	normal = crossProduct(plane.vNormal, back - p2).normalize();

	back = other.m_Points[(j + other.m_Points.size() - 1) % other.m_Points.size()];
	dot = dotProduct(back - p2, normal);
	if (dot > epsilon)
		return NULL;			// not a convex polygon

	bool keep2 = (dot < -epsilon);
	//
	// build the new polygon
	//
	newf = new Winding();

	// copy first polygon
	for (size_t k = (i + 1) % m_Points.size(); k != i; k = (k + 1) % m_Points.size())
	{
		if (k == (i + 1) % m_Points.size() && !keep2)
			continue;

		newf->m_Points.push_back(m_Points[k]);
	}

	// copy second polygon
	for (size_t l = (j + 1) % other.m_Points.size(); l != j; l = (l + 1) % other.m_Points.size())
	{
		if (l == (j + 1) % other.m_Points.size() && !keep1)
			continue;
		newf->m_Points.push_back(other.m_Points[l]);
	}

	newf->RemoveColinearPoints();


	if (newf->m_Points.size() >= 3)
	{
		for (i = 0; i < m_Points.size(); i++)
		{
			for (j = i + 1; j < m_Points.size(); j++)
			{
				if (j != i)
				{
					if (m_Points[i].equal(m_Points[j], 0.01f))
					{
						// Has duplicate points (NO NORMAL FOR PLANE!)
						delete newf;
						return NULL;
					}
				}
			}
		}
		if (!newf->IsConvex())
		{
			// not a convex polygon
			delete newf;
			return NULL;
		}
		vec3 norm;
		float dist;
		if (getPlaneFromVerts(newf->m_Points, norm, dist) && norm.length() > 0.01f)
		{
			return newf;
		}
		// (NO NORMAL FOR PLANE!)
	}

	delete newf;
	return NULL;
}

#pragma once

#include "rad.h"

#define MAX_POINTS_ON_WINDING 128
// TODO: FIX THIS STUPID SHIT (MAX_POINTS_ON_WINDING)

#define	SIDE_FRONT		0
#define	SIDE_ON			2
#define	SIDE_BACK		1
#define	SIDE_CROSS		-2

struct BSPPLANE;
struct BSPFACE32;

class Winding
{
public:
	int  m_NumPoints;
	vec3* m_Points;

	Winding(Bsp* bsp, const BSPFACE32& face, float epsilon = ON_EPSILON);
	Winding(const std::vector<vec3> & points, float epsilon = ON_EPSILON);
	Winding(int numpoints);
	Winding(const BSPPLANE& plane, float epsilon = ON_EPSILON);
	Winding();
	Winding(const Winding& other);
	void getPlane(BSPPLANE& plane);
	virtual ~Winding();
	Winding& operator=(const Winding& other);

	void RemoveColinearPoints(float epsilon = ON_EPSILON);
	bool Clip(BSPPLANE& split, bool keepon, float epsilon = ON_EPSILON);


protected:
	unsigned int  m_MaxPoints;
};

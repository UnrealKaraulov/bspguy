#include "primitives.h"
#include <math.h>
#include <string>
#include "Wad.h"

tQuad::tQuad(float x, float y, float w, float h)
{
	v1 = tVert(x, y, 0.0f, 0.0f, 0.0f);
	v2 = tVert(x, y + h, 0.0f, 0.0f, 0.0f);
	v3 = tVert(x + w, y + h, 0.0f, 1.0f, 0.0f);

	v4 = tVert(x, y, 0.0f, 0.0f, 0.0f);
	v5 = tVert(x + w, y + h, 0.0f, 1.0f, 0.0f);
	v6 = tVert(x + w, y, 0.0f, 1.0f, 0.0f);
}

tQuad::tQuad(float x, float y, float w, float h, float uu1, float vv1, float uu2, float vv2)
{
	v1 = tVert(x, y, 0.0f, uu1, vv1);
	v2 = tVert(x, y + h, 0.0f, uu1, vv2);
	v3 = tVert(x + w, y + h, 0.0f, uu2, vv2);

	v4 = tVert(x, y, 0.0f, uu1, vv1);
	v5 = tVert(x + w, y + h, 0.0f, uu2, vv2);
	v6 = tVert(x + w, y, 0.0f, uu2, vv1);
}

tQuad::tQuad(tVert _v1, tVert _v2, tVert _v3, tVert _v4) : v1(_v1), v2(_v2), v3(_v3), v4(_v4), v5(_v3), v6(_v4)
{
	
}
cQuad::cQuad(cVert _v1, cVert _v2, cVert _v3, cVert _v4) : v1(_v1), v2(_v2), v3(_v3), v4(_v1), v5(_v3), v6(_v4)
{

}

void cQuad::setColor(COLOR4 c)
{
	v1.c = c;
	v2.c = c;
	v3.c = c;
	v4.c = c;
	v5.c = c;
	v6.c = c;
}

void cQuad::setColor(COLOR4 c1, COLOR4 c2, COLOR4 c3, COLOR4 c4)
{
	v1.c = c1;
	v2.c = c2;
	v3.c = c3;
	v4.c = c1;
	v5.c = c3;
	v6.c = c4;
}

tCube::tCube(vec3 mins, vec3 maxs)
{
	tVert v1, v2, v3, v4;
	vecs[0] = tVert(mins.x, maxs.y, maxs.z, 0.0f, 1.0f);
	vecs[1] = tVert(mins.x, maxs.y, mins.z, 0.0f, 0.0f);
	vecs[2] = tVert(mins.x, mins.y, mins.z, 1.0f, 0.0f);
	vecs[3] = tVert(mins.x, mins.y, maxs.z, 1.0f, 1.0f);

	vecs[4] = tVert(maxs.x, maxs.y, mins.z, 0.0f, 1.0f);
	vecs[5] = tVert(maxs.x, maxs.y, maxs.z, 0.0f, 0.0f);
	vecs[6] = tVert(maxs.x, mins.y, maxs.z, 1.0f, 0.0f);
	vecs[7] = tVert(maxs.x, mins.y, mins.z, 1.0f, 1.0f);

	vecs[8] = tVert(mins.x, mins.y, mins.z, 0.0f, 1.0f);
	vecs[9] = tVert(maxs.x, mins.y, mins.z, 0.0f, 0.0f);
	vecs[10] = tVert(maxs.x, mins.y, maxs.z, 1.0f, 0.0f);
	vecs[11] = tVert(mins.x, mins.y, maxs.z, 1.0f, 1.0f);

	vecs[12] = tVert(mins.x, maxs.y, maxs.z, 0.0f, 1.0f);
	vecs[13] = tVert(maxs.x, maxs.y, maxs.z, 0.0f, 0.0f);
	vecs[14] = tVert(maxs.x, maxs.y, mins.z, 1.0f, 0.0f);
	vecs[15] = tVert(mins.x, maxs.y, mins.z, 1.0f, 1.0f);

	vecs[16] = tVert(mins.x, maxs.y, mins.z, 0.0f, 1.0f);
	vecs[17] = tVert(maxs.x, maxs.y, mins.z, 0.0f, 0.0f);
	vecs[18] = tVert(maxs.x, mins.y, mins.z, 1.0f, 0.0f);
	vecs[19] = tVert(mins.x, mins.y, mins.z, 1.0f, 1.0f);

	vecs[20] = tVert(maxs.x, maxs.y, maxs.z, 0.0f, 1.0f);
	vecs[21] = tVert(mins.x, maxs.y, maxs.z, 0.0f, 0.0f);
	vecs[22] = tVert(mins.x, mins.y, maxs.z, 1.0f, 0.0f);
	vecs[23] = tVert(maxs.x, mins.y, maxs.z, 1.0f, 1.0f);
}

cCube::cCube(vec3 mins, vec3 maxs, COLOR4 c)
{
	cVert v1, v2, v3, v4;

	v1 = cVert(mins.x, maxs.y, maxs.z, c);
	v2 = cVert(mins.x, maxs.y, mins.z, c);
	v3 = cVert(mins.x, mins.y, mins.z, c);
	v4 = cVert(mins.x, mins.y, maxs.z, c);
	left = { v1, v2, v3, v4 };

	v1 = cVert(maxs.x, maxs.y, mins.z, c);
	v2 = cVert(maxs.x, maxs.y, maxs.z, c);
	v3 = cVert(maxs.x, mins.y, maxs.z, c);
	v4 = cVert(maxs.x, mins.y, mins.z, c);
	right = { v1, v2, v3, v4 };

	v1 = cVert(mins.x, mins.y, mins.z, c);
	v2 = cVert(maxs.x, mins.y, mins.z, c);
	v3 = cVert(maxs.x, mins.y, maxs.z, c);
	v4 = cVert(mins.x, mins.y, maxs.z, c);
	top = { v1, v2, v3, v4 };

	v1 = cVert(mins.x, maxs.y, maxs.z, c);
	v2 = cVert(maxs.x, maxs.y, maxs.z, c);
	v3 = cVert(maxs.x, maxs.y, mins.z, c);
	v4 = cVert(mins.x, maxs.y, mins.z, c);
	bottom = { v1, v2, v3, v4 };

	v1 = cVert(mins.x, maxs.y, mins.z, c);
	v2 = cVert(maxs.x, maxs.y, mins.z, c);
	v3 = cVert(maxs.x, mins.y, mins.z, c);
	v4 = cVert(mins.x, mins.y, mins.z, c);
	front = { v1, v2, v3, v4 };

	v1 = cVert(maxs.x, maxs.y, maxs.z, c);
	v2 = cVert(mins.x, maxs.y, maxs.z, c);
	v3 = cVert(mins.x, mins.y, maxs.z, c);
	v4 = cVert(maxs.x, mins.y, maxs.z, c);
	back = { v1, v2, v3, v4 };
}


void cCube::setColor(COLOR4 c)
{
	left.setColor(c);
	right.setColor(c);
	top.setColor(c);
	bottom.setColor(c);
	front.setColor(c);
	back.setColor(c);
}

void cCube::setColor(COLOR4 lf, COLOR4 rt, COLOR4 tp, COLOR4 bt, COLOR4 ft, COLOR4 bk)
{
	left.setColor(lf);
	right.setColor(rt);
	top.setColor(tp);
	bottom.setColor(bt);
	front.setColor(ft);
	back.setColor(bk);
}

cCubeAxes::cCubeAxes(vec3 mins, vec3 maxs)
{
	vec3 size = maxs - mins;
	vec3 center = mins + size * 0.5f;

	mins.x = maxs.x;
	mins.y = center.y + 2.5f;
	mins.z = center.z - 2.5f;

	maxs.x += size.x;
	maxs.y = center.y - 2.5f;
	maxs.z = center.z + 2.5f;

	cCube tmpCube = cCube(mins, maxs, COLOR4(0, 255, 255, 255));
	this->back = tmpCube.back;
	this->bottom = tmpCube.bottom;
	this->front = tmpCube.front;
	this->left = tmpCube.left;
	this->right = tmpCube.right;
	this->top = tmpCube.top;
}

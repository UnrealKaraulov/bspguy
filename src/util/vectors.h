#pragma once

#include <string>

#define PI 3.141592f

#define EPSILON 0.0001f // EPSILON from rad.h / 10

#define EPSILON2 0.00001f // EPSILON from rad.h / 100

#define ON_EPSILON 0.03125f


struct vec3
{
	float x, y, z;

	void Copy(const vec3& other) 
	{
		x = other.x;
		y = other.y;
		z = other.z;
	}
	vec3& operator =(const vec3& other)
	{
		Copy(other); 
		return *this;
	}

	vec3(const vec3& other) 
	{
		Copy(other);
	}

	vec3() : x(+0.0f), y(+0.0f), z(+0.0f)
	{

	}

	vec3(float x, float y, float z) : x(x), y(y), z(z)
	{
		if (abs(x) < EPSILON2)
		{
			x = +0.0f;
		}
		if (abs(y) < EPSILON2)
		{
			y = +0.0f;
		}
		if (abs(z) < EPSILON2)
		{
			z = +0.0f;
		}
	}
	vec3 normalize(float length = 1.0f);
	vec3 snap(float snapSize);
	vec3 normalize_angles();
	vec3 swap_xz();
	float length();
	bool IsZero();
	vec3 invert();
	std::string toKeyvalueString(bool truncate = false, const std::string& suffix_x = " ", const std::string& suffix_y = " ", const std::string& suffix_z = "");
	std::string toString();
	vec3 flip(); // flip from opengl to Half-life coordinate system and vice versa
	vec3 flipUV(); // flip from opengl to Half-life coordinate system and vice versa
	vec3 unflip(); 
	vec3 unflipUV();

	void operator-=(const vec3& v);
	void operator+=(const vec3& v);
	void operator*=(const vec3& v);
	void operator/=(const vec3& v);

	void operator-=(float f);
	void operator+=(float f);
	void operator*=(float f);
	void operator/=(float f);

	vec3 operator-()
	{
		x *= -1.f;
		y *= -1.f;
		z *= -1.f;
		return *this;
	}

	float operator [] (const int i) const
	{
		switch (i)
		{
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		}
		return z;
	}

	float& operator [] (const int i)
	{
		switch (i)
		{
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		}
		return z;
	}

};

vec3 operator-(vec3 v1, const vec3& v2);
vec3 operator+(vec3 v1, const vec3& v2);
vec3 operator*(vec3 v1, const vec3& v2);
vec3 operator/(vec3 v1, const vec3& v2);

vec3 operator+(vec3 v, float f);
vec3 operator-(vec3 v, float f);
vec3 operator*(vec3 v, float f);
vec3 operator/(vec3 v, float f);

vec3 crossProduct(const vec3& v1, const vec3& v2);
float dotProduct(const vec3& v1, const vec3& v2);
void makeVectors(const vec3& angles, vec3& forward, vec3& right, vec3& up);

bool operator==(const vec3& v1, const vec3& v2);
bool operator!=(const vec3& v1, const vec3& v2);

struct vec2
{
	float x, y;
	vec2() : x(0), y(0)
	{
		if (abs(x) < EPSILON)
			x = +0.0f;
		if (abs(y) < EPSILON)
			y = +0.0f;
	}
	vec2(float x, float y) : x(x), y(y)
	{
		if (abs(x) < EPSILON)
			x = +0.0f;
		if (abs(y) < EPSILON)
			y = +0.0f;
	}
	vec2 swap();
	vec2 normalize(float length = 1.0f);
	float length();

	void operator-=(const vec2& v);
	void operator+=(const vec2& v);
	void operator*=(const vec2& v);
	void operator/=(const vec2& v);

	void operator-=(float f);
	void operator+=(float f);
	void operator*=(float f);
	void operator/=(float f);
};

vec2 operator-(vec2 v1, const vec2& v2);
vec2 operator+(vec2 v1, const vec2& v2);
vec2 operator*(vec2 v1, const vec2& v2);
vec2 operator/(vec2 v1, const vec2& v2);

vec2 operator+(vec2 v, float f);
vec2 operator-(vec2 v, float f);
vec2 operator*(vec2 v, float f);
vec2 operator/(vec2 v, float f);

bool operator==(const vec2& v1, const vec2& v2);
bool operator!=(const vec2& v1, const vec2& v2);

struct vec4
{
	float x, y, z, w;

	vec4() : x(+0.0f), y(+0.0f), z(+0.0f), w(+0.0f)
	{
	}
	vec4(float x, float y, float z) : x(x), y(y), z(z), w(1)
	{
		if (abs(x) < EPSILON)
			x = +0.0f;
		if (abs(y) < EPSILON)
			y = +0.0f;
		if (abs(z) < EPSILON)
			z = +0.0f;
	}
	vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w)
	{
		if (abs(x) < EPSILON)
			x = +0.0f;
		if (abs(y) < EPSILON)
			y = +0.0f;
		if (abs(z) < EPSILON)
			z = +0.0f;
		if (abs(w) < EPSILON)
			w = +0.0f;
	}
	vec4(const vec3& v, float a) : x(v.x), y(v.y), z(v.z), w(a)
	{
		if (abs(x) < EPSILON)
			x = +0.0f;
		if (abs(y) < EPSILON)
			y = +0.0f;
		if (abs(z) < EPSILON)
			z = +0.0f;
		if (abs(w) < EPSILON)
			w = +0.0f;
	}
	vec3 xyz();
	vec2 xy();

	float operator [] (const int i) const
	{
		switch (i)
		{
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		}
		return w;
	}

	float& operator [] (const int i)
	{
		switch (i)
		{
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		}
		return w;
	}

};

vec4 operator-(vec4 v1, const vec4& v2);
vec4 operator+(vec4 v1, const vec4& v2);
vec4 operator*(vec4 v1, const vec4& v2);
vec4 operator/(vec4 v1, const vec4& v2);

vec4 operator+(vec4 v, float f);
vec4 operator-(vec4 v, float f);
vec4 operator*(vec4 v, float f);
vec4 operator/(vec4 v, float f);

bool operator==(const vec4& v1, const vec4& v2);
bool operator!=(const vec4& v1, const vec4& v2);


#define	SIDE_FRONT		0
#define	SIDE_ON			2
#define	SIDE_BACK		1
#define	SIDE_CROSS		-2

#define	Q_PI	(float)(3.14159265358979323846)

#define mDotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])

// Use this definition globally
#define	mON_EPSILON		0.01
#define	mEQUAL_EPSILON	0.001

#define mDotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define mVectorFill(a,b) { (a)[0]=(b); (a)[1]=(b); (a)[2]=(b);}
#define mVectorAvg(a) ( ( (a)[0] + (a)[1] + (a)[2] ) / 3 )
#define mVectorSubtract(a,b,c) {(c)[0]=(a)[0]-(b)[0];(c)[1]=(a)[1]-(b)[1];(c)[2]=(a)[2]-(b)[2];}
#define mVectorAdd(a,b,c) {(c)[0]=(a)[0]+(b)[0];(c)[1]=(a)[1]+(b)[1];(c)[2]=(a)[2]+(b)[2];}
#define mVectorCopy(a,b) {(b)[0]=(a)[0];(b)[1]=(a)[1];(b)[2]=(a)[2];}
#define mVectorScale(a,b,c) {(c)[0]=(b)*(a)[0];(c)[1]=(b)*(a)[1];(c)[2]=(b)*(a)[2];}

float Q_rint(float in);
float _DotProduct(const vec3& v1, const vec3& v2);
void _VectorSubtract(const vec3& va, const vec3& vb, vec3& out);
void _VectorAdd(const vec3& va, const vec3& vb, vec3& out);
void _VectorCopy(const vec3& in, vec3& out);
void _VectorScale(const vec3& v, float scale, vec3& out);

float VectorLength(const vec3& v);

void mVectorMA(const vec3& va, double scale, const vec3& vb, vec3& vc);

void mCrossProduct(const vec3& v1, const vec3& v2, vec3& cross);
void VectorInverse(vec3& v);

void ClearBounds(vec3& mins, vec3& maxs);
void AddPointToBounds(const vec3& v, vec3& mins, vec3& maxs);

void AngleMatrix(const vec3& angles, float(*matrix)[4]);
void AngleIMatrix(const vec3& angles, float matrix[3][4]);
void VectorIRotate(const vec3& in1, const float in2[3][4], vec3& out);
void VectorRotate(const vec3& in1, const float in2[3][4], vec3& out);

void VectorTransform(const vec3& in1, const float in2[3][4], vec3& out);

void QuaternionMatrix(const vec4& quaternion, float(*matrix)[4]);

bool VectorCompare(const vec3& v1, const vec3& v2, float epsilon = EPSILON);

void QuaternionSlerp(const vec4& p, vec4& q, float t, vec4& qt);
void AngleQuaternion(const vec3& angles, vec4& quaternion);
void R_ConcatTransforms(const float in1[3][4], const float in2[3][4], float out[3][4]);
void VectorScale(const vec3& v, float scale, vec3& out);
float VectorNormalize(vec3& v);
float fullnormalizeangle(float angle);

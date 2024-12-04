#pragma once
#include <string>
#include <vector>
#include <cmath>

#define HL_PI 3.141592f

#define EPSILON 0.0001f // EPSILON from rad.h / 10

#define EPSILON2 0.00001f // EPSILON from rad.h / 100

#define ON_EPSILON 0.01f // changed for test default is 0.03125f


float clamp(float val, float min, float max);

#define CLAMP(v, min, max) if (v < min) { v = min; } else if (v > max) { v = max; }

struct COLOR3
{
	unsigned char r, g, b;

	COLOR3() : r(0), g(0), b(0) {};
	COLOR3(unsigned char r, unsigned char g, unsigned char b) : r(r), g(g), b(b)
	{}
	bool operator==(const COLOR3& other) const {
		return r == other.r && g == other.g && b == other.b;
	}

	COLOR3 operator*(float scale)
	{
		r = (unsigned char)(r * scale);
		g = (unsigned char)(g * scale);
		b = (unsigned char)(b * scale);
		return *this;
	}
};

struct COLOR4
{
	unsigned char r, g, b, a;
	COLOR4() : r(0), g(0), b(0), a(0) {};
	COLOR4(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : r(r), g(g), b(b), a(a)
	{}
	COLOR4(const COLOR3& c, unsigned char a) : r(c.r), g(c.g), b(c.b), a(a)
	{}
	COLOR4(const COLOR3& c) : r(c.r), g(c.g), b(c.b), a(255)
	{}

	COLOR3 rgb(COLOR3 background) {
		float alpha = a / 255.0f;
		unsigned char r_new = (unsigned char)clamp((1.0f - alpha) * r + alpha * background.r, 0.0f, 255.0f);
		unsigned char g_new = (unsigned char)clamp((1.0f - alpha) * g + alpha * background.g, 0.0f, 255.0f);
		unsigned char b_new = (unsigned char)clamp((1.0f - alpha) * b + alpha * background.b, 0.0f, 255.0f);
		return COLOR3(r_new, g_new, b_new);
	}
	COLOR3 rgb() {
		return COLOR3(r, g, b);
	}
};

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

	vec3() : x(+0.00f), y(+0.00f), z(+0.00f)
	{

	}

	vec3(float x, float y, float z) : x(x), y(y), z(z)
	{
		
	}

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

	vec3 operator+(const vec3& v) const {
		return vec3(x + v.x, y + v.y, z + v.z);
	}

	vec3 operator-(const vec3& v) const {
		return vec3(x - v.x, y - v.y, z - v.z);
	}

	vec3 operator*(const vec3& v) const {
		return vec3(x * v.x, y * v.y, z * v.z);
	}

	vec3 operator/(const vec3& v) const {
		return vec3(x / v.x, y / v.y, z / v.z);
	}

	vec3 operator+(float f) const {
		return vec3(x + f, y + f, z + f);
	}

	vec3 operator-(float f) const {
		return vec3(x - f, y - f, z - f);
	}

	vec3 operator*(float f) const {
		return vec3(x * f, y * f, z * f);
	}

	vec3 operator/(float f) const {
		return vec3(x / f, y / f, z / f);
	}

	float& operator [] (size_t i) 
	{
		return *(&x + i);
	}

	float operator [] (size_t i) const {
		return *(&x + i);
	}

	bool operator==(const vec3& other) const
	{
		vec3 v = *this - other;
		if (std::fabs(v.x) >= EPSILON)
			return false;
		if (std::fabs(v.y) >= EPSILON)
			return false;
		if (std::fabs(v.z) >= EPSILON)
			return false;
		return true;
	}

	bool operator!=(const vec3& other) const
	{
		vec3 v = *this - other;
		if (std::fabs(v.x) >= EPSILON)
			return true;
		if (std::fabs(v.y) >= EPSILON)
			return true;
		if (std::fabs(v.z) >= EPSILON)
			return true;
		return false;
	}


	vec3 normalize(float length = 1.0f)  const;
	vec3 snap(float snapSize);
	vec3 normalize_angles() const;
	vec3 swap_xz();
	bool equal(vec3 to, float epsilon = EPSILON) const;
	float size_test();
	float sizeXY_test();
	vec3 abs();
	float length() const;
	float lengthSquared() const;
	bool IsZero() const;
	vec3 invert();
	std::string toKeyvalueString(bool truncate = false, const std::string& suffix_x = " ", const std::string& suffix_y = " ", const std::string& suffix_z = "");
	std::string toString();
	vec3 flip(); // flip from opengl to Half-life coordinate system and vice versa
	vec3 flipUV(); // flip from opengl to Half-life coordinate system and vice versa
	vec3 unflip();
	vec3 unflipUV();
	float dist(vec3 to)  const;
	float dot(const vec3& other) const {
		return x * other.x + y * other.y + z * other.z;
	}

	vec3 cross(const vec3& other) const {
		return { y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x };
	}
};

vec3 operator*(float lhs, const vec3& rhs);
vec3 operator/(float lhs, const vec3& rhs);

struct vec3Hash {
	size_t operator()(const vec3(&v)) const {
		size_t seed = 1;
		std::hash<float> hasher;
		seed ^= hasher(v[0]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		seed ^= hasher(v[1]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		seed ^= hasher(v[2]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		return seed;
	}
};

struct pairHash {
	template <typename T1, typename T2>
	size_t operator()(const std::pair<T1, T2>& p) const {
		size_t seed = 2;
		vec3Hash hasher;
		seed ^= hasher(p.first) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		seed ^= hasher(p.second) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		return seed;
	}
};


vec3 crossProduct(const vec3& v1, const vec3& v2);
float dotProduct(const vec3& v1, const vec3& v2);
void makeVectors(const vec3& angles, vec3& forward, vec3& right, vec3& up);
float distanceToPlane(const vec3& point, const vec3& planeNormal, float planeDist);
bool isPointInFace(const vec3& point, const std::vector<vec3>& faceVertices);

struct vec2
{
	float x, y;
	vec2() : x(0), y(0)
	{
		if (std::fabs(x) < EPSILON)
			x = +0.0f;
		if (std::fabs(y) < EPSILON)
			y = +0.0f;
	}
	vec2(float x, float y) : x(x), y(y)
	{
		if (std::fabs(x) < EPSILON)
			x = +0.0f;
		if (std::fabs(y) < EPSILON)
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


float dotProduct(vec2 v1, vec2 v2);
float crossProduct(vec2 v1, vec2 v2);


struct vec4
{
	float x, y, z, w;

	vec4() : x(+0.0f), y(+0.0f), z(+0.0f), w(+0.0f)
	{
	}
	vec4(float x, float y, float z) : x(x), y(y), z(z), w(1)
	{
		if (std::fabs(x) < EPSILON)
			x = +0.0f;
		if (std::fabs(y) < EPSILON)
			y = +0.0f;
		if (std::fabs(z) < EPSILON)
			z = +0.0f;
	}
	vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w)
	{
		if (std::fabs(x) < EPSILON)
			x = +0.0f;
		if (std::fabs(y) < EPSILON)
			y = +0.0f;
		if (std::fabs(z) < EPSILON)
			z = +0.0f;
		if (std::fabs(w) < EPSILON)
			w = +0.0f;
	}
	vec4(const vec3& v, float a) : x(v.x), y(v.y), z(v.z), w(a)
	{
		if (std::fabs(x) < EPSILON)
			x = +0.0f;
		if (std::fabs(y) < EPSILON)
			y = +0.0f;
		if (std::fabs(z) < EPSILON)
			z = +0.0f;
		if (std::fabs(w) < EPSILON)
			w = +0.0f;
	}
	vec4(const COLOR4& c) : x(c.r / 255.0f), y(c.g / 255.0f), z(c.b / 255.0f), w(c.a / 255.0f)
	{
		if (std::fabs(x) < EPSILON)
			x = +0.0f;
		if (std::fabs(y) < EPSILON)
			y = +0.0f;
		if (std::fabs(z) < EPSILON)
			z = +0.0f;
		if (std::fabs(w) < EPSILON)
			w = +0.0f;
	}
	vec3 xyz();
	vec2 xy();


	std::string toKeyvalueString(bool truncate = false, const std::string& suffix_x = " ", const std::string& suffix_y = " ", const std::string& suffix_z = " "
		, const std::string& suffix_w = "");

	float operator [] (size_t i) const
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

	float& operator [] (size_t i)
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

// Use this definition globally
#define	mON_EPSILON		0.01
#define	mEQUAL_EPSILON	0.001

float Q_rint(float in);
float _DotProduct(const vec3& v1, const vec3& v2);
void _VectorSubtract(const vec3& va, const vec3& vb, vec3& out);
void _VectorAdd(const vec3& va, const vec3& vb, vec3& out);
void _VectorCopy(const vec3& in, vec3& out);
void _VectorScale(const vec3& v, float scale, vec3& out);

float VectorLength(const vec3& v);

void mVectorMA(const vec3& va, float scale, const vec3& vb, vec3& vc);

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
void R_ConcatTransforms(float in1[][4], float in2[][4], float out[][4]);
void VectorScale(const vec3& v, float scale, vec3& out);
float VectorNormalize(vec3& v);
float fullnormalizeangle(float angle);

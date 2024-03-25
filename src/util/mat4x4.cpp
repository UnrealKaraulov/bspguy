#include "lang.h"
#include "mat4x4.h"
#include "util.h"
#include "log.h"

float m_identity[16] = {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f
};

float m_zero[16] = { 0.0f };

void mat4x4::loadIdentity()
{
	memcpy(m, m_identity, sizeof(m_identity));
}

void loadEmptyMat4x4(float* m)
{
	memcpy(m, m_zero, sizeof(m_zero));
}

void glhFrustumf2(float* matrix, float left, float right, float bottom, float top,
	float znear, float zfar)
{
	float temp, temp2, temp3, temp4;
	temp = 2.0f * znear;
	temp2 = right - left;
	temp3 = top - bottom;
	temp4 = zfar - znear;
	matrix[0] = temp / temp2;
	matrix[5] = temp / temp3;
	matrix[2] = (right + left) / temp2;
	matrix[6] = (top + bottom) / temp3;
	matrix[10] = (-zfar - znear) / temp4;
	matrix[14] = -1.0f;
	matrix[11] = (-temp * zfar) / temp4;
}

void mat4x4::perspective(float fov, float aspect, float near, float far)
{
	loadIdentity();
	float ymax, xmax;
	ymax = near * tanf(fov * (HL_PI / 360.0f));
	xmax = ymax * aspect;
	glhFrustumf2(m, -xmax, xmax, -ymax, ymax, near, far);
}

// Set up an orthographic projection matrix
// http://en.wikipedia.org/wiki/Orthographic_projection_(geometry)
void mat4x4::ortho(float left, float right, float bottom, float top, float near, float far)
{
	loadIdentity();
	float w = right - left;
	float h = top - bottom;
	float d = far - near;

	// scale by the screen size
	m[4 * 0 + 0] = 2.0f / w;
	m[4 * 1 + 1] = 2.0f / h;
	m[4 * 2 + 2] = -2.0f / d;
	m[4 * 3 + 3] = 1;

	// Translate the origin to the upper-left corner of the screen
	m[4 * 0 + 3] = -(right + left) / w;
	m[4 * 1 + 3] = -(top + bottom) / h;
	m[4 * 2 + 3] = (far + near) / d;
}

void mat4x4::translate(float x, float y, float z)
{
	float tmat[16] =
	{
		1, 0, 0, x,
		0, 1, 0, y,
		0, 0, 1, z,
		0, 0, 0, 1,
	};
	mult(tmat);
}

void mat4x4::scale(float x, float y, float z)
{
	float tmat[16] =
	{
		x, 0, 0, 0,
		0, y, 0, 0,
		0, 0, z, 0,
		0, 0, 0, 1,
	};
	mult(tmat);
}

void mat4x4::rotateX(float r)
{
	float c = cosf(r);
	float s = sinf(r);
	float rmat[16] =
	{
		1, 0, 0, 0,
		0, c,-s, 0,
		0, s, c, 0,
		0, 0, 0, 1,
	};
	mult(rmat);
}

void mat4x4::rotateY(float r)
{
	float c = cosf(r);
	float s = sinf(r);
	float rmat[16] =
	{
		c, 0, s, 0,
		0, 1, 0, 0,
		-s, 0, c, 0,
		0, 0, 0, 1,
	};
	mult(rmat);
}

void mat4x4::rotateZ(float r)
{
	float c = cosf(r);
	float s = sinf(r);
	float rmat[16] =
	{
		c,-s, 0, 0,
		s, c, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};
	mult(rmat);
}

void mat4x4::rotate(float pitch, float yaw, float roll)
{
	float angle;
	float sr, sp, sy, cr, cp, cy;

	angle = yaw * (HL_PI * 2.0f / 360.0f);
	sy = sin(angle);
	cy = cos(angle);
	angle = pitch * (HL_PI * 2.0f / 360.0f);
	sp = sin(angle);
	cp = cos(angle);
	angle = roll * (HL_PI * 2.0f / 360.0f);
	sr = sin(angle);
	cr = cos(angle);

	float mat[16] = { 0.0f };
	mat[0 * 4 + 0] = cp * cy;
	mat[1 * 4 + 0] = cp * sy;
	mat[2 * 4 + 0] = -sp;
	mat[0 * 4 + 1] = sr * sp * cy + cr * -sy;
	mat[1 * 4 + 1] = sr * sp * sy + cr * cy;
	mat[2 * 4 + 1] = sr * cp;
	mat[0 * 4 + 2] = (cr * sp * cy + -sr * -sy);
	mat[1 * 4 + 2] = (cr * sp * sy + -sr * cy);
	mat[2 * 4 + 2] = cr * cp;

	mat[0 * 4 + 3] = 0.0f;
	mat[1 * 4 + 3] = 0.0f;
	mat[2 * 4 + 3] = 0.0f;
	mat[3 * 4 + 0] = 0.0f;
	mat[3 * 4 + 1] = 0.0f;
	mat[3 * 4 + 2] = 0.0f;
	mat[3 * 4 + 3] = 1.0f;

	mult(mat);
}/*
void mat4x4::rotate(float pitch, float yaw, float roll)
{
	float tmp_ch = cosf(-yaw);
	float tmp_sh = sinf(-yaw);
	float tmp_cp = cosf(-pitch);
	float tmp_sp = sinf(-pitch);
	float tmp_cb = cosf(roll);
	float tmp_sb = sinf(roll);

	float rmat[16] = {0};

	rmat[0 * 4 + 0] = tmp_ch * tmp_cb + tmp_sh * tmp_sp * tmp_sb;
	rmat[0 * 4 + 1] = tmp_sb * tmp_cp;
	rmat[0 * 4 + 2] = -tmp_sh * tmp_cb + tmp_ch * tmp_sp * tmp_sb;
	rmat[0 * 4 + 3] = 0.0f;
	rmat[1 * 4 + 0] = -tmp_ch * tmp_sb + tmp_sh * tmp_sp * tmp_cb;
	rmat[1 * 4 + 1] = tmp_cb * tmp_cp;
	rmat[1 * 4 + 2] = tmp_sb * tmp_sh + tmp_ch * tmp_sp * tmp_cb;
	rmat[1 * 4 + 3] = 0.0f;
	rmat[2 * 4 + 0] = tmp_sh * tmp_cp;
	rmat[2 * 4 + 1] = -tmp_sp;
	rmat[2 * 4 + 2] = tmp_ch * tmp_cp;
	rmat[2 * 4 + 3] = 0.0f;
	rmat[3 * 4 + 0] = 0.0f;
	rmat[3 * 4 + 1] = 0.0f;
	rmat[3 * 4 + 2] = 0.0f;
	rmat[3 * 4 + 3] = 1.0f;

	mult(rmat);
}*/
/*
void mat4x4::rotate(float x, float y, float z)
{
	float c1 = cosf(z);
	float c2 = cosf(y);
	float c3 = cosf(-x);
	float s1 = sinf(z);
	float s2 = sinf(y);
	float s3 = sinf(-x);

	float rmat[16] = {0};

	rmat[0 * 4 + 0] = c2 * c3;
	rmat[0 * 4 + 1] = -c1 * s3 + s1 * s2 * c3;
	rmat[0 * 4 + 2] = s1 * s3 + c1 * s2 * c3;
	rmat[0 * 4 + 3] = 0.0f;
	rmat[1 * 4 + 0] = c2 * s3;
	rmat[1 * 4 + 1] = c1 * c3 + s1 * s2 * s3;
	rmat[1 * 4 + 2] = -s1 * c3 + c1 * s2 * s3;
	rmat[1 * 4 + 3] = 0.0f;
	rmat[2 * 4 + 0] = -s2;
	rmat[2 * 4 + 1] = s1 * c2;
	rmat[2 * 4 + 2] = c1 * c2;
	rmat[2 * 4 + 3] = 0.0f;
	rmat[3 * 4 + 0] = 0.0f;
	rmat[3 * 4 + 1] = 0.0f;
	rmat[3 * 4 + 2] = 0.0f;
	rmat[3 * 4 + 3] = 1.0f;

	mult(rmat);
}*/

mat4x4 worldToLocalTransform(const vec3& local_x, const vec3& local_y, const vec3& local_z)
{
	const vec3 world_x(1, 0, 0);
	const vec3 world_y(0, 1, 0);
	const vec3 world_z(0, 0, 1);

	mat4x4 worldToLocal;

	worldToLocal.loadIdentity();

	worldToLocal.m[0 * 4 + 0] = dotProduct(local_x, world_x);
	worldToLocal.m[1 * 4 + 0] = dotProduct(local_x, world_y);
	worldToLocal.m[2 * 4 + 0] = dotProduct(local_x, world_z);
	worldToLocal.m[0 * 4 + 1] = dotProduct(local_y, world_x);
	worldToLocal.m[1 * 4 + 1] = dotProduct(local_y, world_y);
	worldToLocal.m[2 * 4 + 1] = dotProduct(local_y, world_z);
	worldToLocal.m[0 * 4 + 2] = dotProduct(local_z, world_x);
	worldToLocal.m[1 * 4 + 2] = dotProduct(local_z, world_y);
	worldToLocal.m[2 * 4 + 2] = dotProduct(local_z, world_z);

	return worldToLocal;
}

mat4x4 mat4x4::transpose()
{
	mat4x4 result;
	result.m[0 + 0 * 4] = m[0 * 4 + 0];
	result.m[0 + 1 * 4] = m[0 * 4 + 1];
	result.m[0 + 2 * 4] = m[0 * 4 + 2];
	result.m[0 + 3 * 4] = m[0 * 4 + 3];
	result.m[1 + 0 * 4] = m[1 * 4 + 0];
	result.m[1 + 1 * 4] = m[1 * 4 + 1];
	result.m[1 + 2 * 4] = m[1 * 4 + 2];
	result.m[1 + 3 * 4] = m[1 * 4 + 3];
	result.m[2 + 0 * 4] = m[2 * 4 + 0];
	result.m[2 + 1 * 4] = m[2 * 4 + 1];
	result.m[2 + 2 * 4] = m[2 * 4 + 2];
	result.m[2 + 3 * 4] = m[2 * 4 + 3];
	result.m[3 + 0 * 4] = m[3 * 4 + 0];
	result.m[3 + 1 * 4] = m[3 * 4 + 1];
	result.m[3 + 2 * 4] = m[3 * 4 + 2];
	result.m[3 + 3 * 4] = m[3 * 4 + 3];
	return result;
}

// http://stackoverflow.com/questions/1148309/inverting-a-4x4-matrix
mat4x4 mat4x4::invert(bool* result)
{
	mat4x4 out;
	loadEmptyMat4x4(out.m);

	float inv[16];

	inv[0] = m[5] * m[10] * m[15] -
		m[5] * m[11] * m[14] -
		m[9] * m[6] * m[15] +
		m[9] * m[7] * m[14] +
		m[13] * m[6] * m[11] -
		m[13] * m[7] * m[10];

	inv[4] = -m[4] * m[10] * m[15] +
		m[4] * m[11] * m[14] +
		m[8] * m[6] * m[15] -
		m[8] * m[7] * m[14] -
		m[12] * m[6] * m[11] +
		m[12] * m[7] * m[10];

	inv[8] = m[4] * m[9] * m[15] -
		m[4] * m[11] * m[13] -
		m[8] * m[5] * m[15] +
		m[8] * m[7] * m[13] +
		m[12] * m[5] * m[11] -
		m[12] * m[7] * m[9];

	inv[12] = -m[4] * m[9] * m[14] +
		m[4] * m[10] * m[13] +
		m[8] * m[5] * m[14] -
		m[8] * m[6] * m[13] -
		m[12] * m[5] * m[10] +
		m[12] * m[6] * m[9];

	inv[1] = -m[1] * m[10] * m[15] +
		m[1] * m[11] * m[14] +
		m[9] * m[2] * m[15] -
		m[9] * m[3] * m[14] -
		m[13] * m[2] * m[11] +
		m[13] * m[3] * m[10];

	inv[5] = m[0] * m[10] * m[15] -
		m[0] * m[11] * m[14] -
		m[8] * m[2] * m[15] +
		m[8] * m[3] * m[14] +
		m[12] * m[2] * m[11] -
		m[12] * m[3] * m[10];

	inv[9] = -m[0] * m[9] * m[15] +
		m[0] * m[11] * m[13] +
		m[8] * m[1] * m[15] -
		m[8] * m[3] * m[13] -
		m[12] * m[1] * m[11] +
		m[12] * m[3] * m[9];

	inv[13] = m[0] * m[9] * m[14] -
		m[0] * m[10] * m[13] -
		m[8] * m[1] * m[14] +
		m[8] * m[2] * m[13] +
		m[12] * m[1] * m[10] -
		m[12] * m[2] * m[9];

	inv[2] = m[1] * m[6] * m[15] -
		m[1] * m[7] * m[14] -
		m[5] * m[2] * m[15] +
		m[5] * m[3] * m[14] +
		m[13] * m[2] * m[7] -
		m[13] * m[3] * m[6];

	inv[6] = -m[0] * m[6] * m[15] +
		m[0] * m[7] * m[14] +
		m[4] * m[2] * m[15] -
		m[4] * m[3] * m[14] -
		m[12] * m[2] * m[7] +
		m[12] * m[3] * m[6];

	inv[10] = m[0] * m[5] * m[15] -
		m[0] * m[7] * m[13] -
		m[4] * m[1] * m[15] +
		m[4] * m[3] * m[13] +
		m[12] * m[1] * m[7] -
		m[12] * m[3] * m[5];

	inv[14] = -m[0] * m[5] * m[14] +
		m[0] * m[6] * m[13] +
		m[4] * m[1] * m[14] -
		m[4] * m[2] * m[13] -
		m[12] * m[1] * m[6] +
		m[12] * m[2] * m[5];

	inv[3] = -m[1] * m[6] * m[11] +
		m[1] * m[7] * m[10] +
		m[5] * m[2] * m[11] -
		m[5] * m[3] * m[10] -
		m[9] * m[2] * m[7] +
		m[9] * m[3] * m[6];

	inv[7] = m[0] * m[6] * m[11] -
		m[0] * m[7] * m[10] -
		m[4] * m[2] * m[11] +
		m[4] * m[3] * m[10] +
		m[8] * m[2] * m[7] -
		m[8] * m[3] * m[6];

	inv[11] = -m[0] * m[5] * m[11] +
		m[0] * m[7] * m[9] +
		m[4] * m[1] * m[11] -
		m[4] * m[3] * m[9] -
		m[8] * m[1] * m[7] +
		m[8] * m[3] * m[5];

	inv[15] = m[0] * m[5] * m[10] -
		m[0] * m[6] * m[9] -
		m[4] * m[1] * m[10] +
		m[4] * m[2] * m[9] +
		m[8] * m[1] * m[6] -
		m[8] * m[2] * m[5];

	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

	if (std::abs(det) < EPSILON)
	{
		if (result)
			*result = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1010));
		return out;
	}

	det = 1.0f / det;

	out.m[0] = inv[0] * det;
	out.m[1] = inv[1] * det;
	out.m[2] = inv[2] * det;
	out.m[3] = inv[3] * det;
	out.m[4] = inv[4] * det;
	out.m[5] = inv[5] * det;
	out.m[6] = inv[6] * det;
	out.m[7] = inv[7] * det;
	out.m[8] = inv[8] * det;
	out.m[9] = inv[9] * det;
	out.m[10] = inv[10] * det;
	out.m[11] = inv[11] * det;
	out.m[12] = inv[12] * det;
	out.m[13] = inv[13] * det;
	out.m[14] = inv[14] * det;
	out.m[15] = inv[15] * det;


	return out;
}

void mat4x4::mult(float mat[16])
{
	mat4x4& _other = *((mat4x4*)mat);
	*this = *this * _other;
}

mat4x4 operator*(const mat4x4& m1, const mat4x4& m2)
{
	mat4x4 result;
	loadEmptyMat4x4(result.m);
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++)
				result.m[i * 4 + j] += m1.m[i * 4 + k] * m2.m[k * 4 + j];
	return result;
}

vec4 operator*(const mat4x4& mat, const vec4& vec)
{
	vec4 res;
	res.x = mat.m[0] * vec.x + mat.m[4] * vec.y + mat.m[8] * vec.z + mat.m[12] * vec.w;
	res.y = mat.m[1] * vec.x + mat.m[5] * vec.y + mat.m[9] * vec.z + mat.m[13] * vec.w;
	res.z = mat.m[2] * vec.x + mat.m[6] * vec.y + mat.m[10] * vec.z + mat.m[14] * vec.w;
	res.w = vec.w;
	return res;
}

void mat4x4print(const mat4x4& mat)
{
	print_log("{} {} {} {}\n", mat.m[0], mat.m[1], mat.m[2], mat.m[3]);
	print_log("{} {} {} {}\n", mat.m[4], mat.m[5], mat.m[6], mat.m[7]);
	print_log("{} {} {} {}\n", mat.m[8], mat.m[9], mat.m[10], mat.m[11]);
	print_log("{} {} {} {}\n\n", mat.m[12], mat.m[13], mat.m[14], mat.m[15]);
}
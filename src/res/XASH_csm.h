#pragma once
#include <vector>
#include <string>
#include <map>

#include "vectors.h"
#include "Texture.h"
#include "VertexBuffer.h"
#include "primitives.h"


// enumerate coordinate channels
#define FCHAN_TC			0
#define FCHAN_LM			1

#define FMODEL_HAS_LMGROUPS		BIT( 0 )
#define FMODEL_BBOX_COMPUTED		BIT( 1 )
#define FMODEL_COLLAPSED		BIT( 2 )
#define FMODEL_KEEP_NORMALS		BIT( 3 )
#define FMODEL_PORTALS_PROCESSED	BIT( 4 )

#define FFACE_FROM_PLANAR_GROUP	BIT( 0 )
#define FFACE_FROM_PURE_AXIAL_GROUP	BIT( 1 )
#define FFACE_DISSOLVE_FIRST_EDGE	BIT( 2 )
#define FFACE_DISSOLVE_SECOND_EDGE	BIT( 3 )
#define FFACE_DISSOLVE_THIRD_EDGE	BIT( 4 )
#define FFACE_HAS_SOURCE_LMCOORDS	BIT( 5 )
#define FFACE_LANDSCAPE		BIT( 6 )
#define FFACE_STRUCTURAL		BIT( 7 )	// can be used for BSP-processing

#define FFACE_IS_CHECKED		BIT( 15 )	// internal flag
#define FFACE_WORLD_TARGET		(FFACE_FROM_PLANAR_GROUP|FFACE_FROM_PURE_AXIAL_GROUP)


#pragma pack(push, 1)

struct csm_header
{
	unsigned int ident;
	unsigned int version;
	unsigned int flags;
	unsigned int lmGroups;

	unsigned int reserved0[4];
	vec3 model_mins;
	vec3 model_maxs;
	unsigned int reserved1[4];

	unsigned int mat_ofs;
	unsigned int mat_size;

	unsigned int faces_ofs;
	unsigned int face_size;
	unsigned int faces_count;

	unsigned int vertex_ofs;
	unsigned int vertex_size;
	unsigned int vertex_count;
};

struct csm_vertex
{
	vec3 point;
	vec3 normal;
	COLOR4 color;
	csm_vertex(vec3 p, vec3 n, COLOR4 c)
	{
		color = c;
		point = p;
		normal = n;
	}
	csm_vertex()
	{
		point = normal = vec3();
		color = COLOR4();
	}
};

struct csm_uv_t
{
	vec2 uv[3];
};

struct csm_face
{
	unsigned short matIdx;
	unsigned short edgeFlags;
	unsigned int vertIdx[3];
	int lmGroup;
	csm_uv_t uvs[2];
};

#pragma pack(pop)

struct CSM_MDL_MESH
{
	VertexBuffer* buffer;
	unsigned int matid;
	std::vector<modelVert> verts;
	CSM_MDL_MESH()
	{
		buffer = NULL;
		matid = 0;
		verts = std::vector<modelVert>();
	}
	~CSM_MDL_MESH()
	{
		if (buffer)
		{
			delete buffer;
		}
		buffer = NULL;
		matid = 0;
		verts = std::vector<modelVert>();
	}
};


#define IDCSMMODHEADER		(('M'<<24)+('S'<<16)+('C'<<8)+'I') // little-endian "ICSM"
#define IDCSM_VERSION		2



class CSMFile {
private:
	bool readed;
	std::vector<Texture*> mat_textures;
	std::vector<CSM_MDL_MESH *> model;

	void parseMaterialsFromString(const std::string& materialsStr);
	std::string getStringFromMaterials();
	void upload();

public:
	CSMFile();

	CSMFile(std::string path);

	~CSMFile();

	csm_header header;

	std::vector<std::string> materials;
	std::vector<csm_vertex> vertices;
	std::vector<csm_face> faces;

	bool validate();
	bool read(const std::string& filePath);
	bool write(const std::string& filePath);

	void draw();
};



extern std::map<unsigned int, CSMFile*> csm_models;
CSMFile* AddNewXashCsmToRender(const std::string& path, unsigned int sum = 0);
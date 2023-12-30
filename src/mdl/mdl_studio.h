#pragma once 

#include "bsptypes.h"
#include "util.h"
#include "vectors.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Texture.h"
#include "PointEntRenderer.h"

#include <map>
/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
*
==============================================================================

STUDIO MODELS

Studio models are position independent, so the cache manager can move them.
==============================================================================
*/
#pragma pack(push, 1)
#define MAXSTUDIOTRIANGLES	32768	// TODO: tune this
#define MAXSTUDIOVERTS		16384	// TODO: tune this
#define MAXSTUDIOBONES		128		


#define MAX_TRIS_PER_BODYGROUP  MAXSTUDIOTRIANGLES
#define MAX_VERTS_PER_CALL (MAX_TRIS_PER_BODYGROUP * 3)


// lighting options
#define STUDIO_NF_FLATSHADE		0x0001
#define STUDIO_NF_CHROME		0x0002
#define STUDIO_NF_FULLBRIGHT	0x0004
#define STUDIO_NF_NOMIPS        0x0008
#define STUDIO_NF_ALPHA         0x0010
#define STUDIO_NF_ADDITIVE      0x0020
#define STUDIO_NF_MASKED        0x0040
#define STUDIO_NF_UV_COORDS		(1U<<31)

// motion flags
#define STUDIO_X		0x0001
#define STUDIO_Y		0x0002	
#define STUDIO_Z		0x0004
#define STUDIO_XR		0x0008
#define STUDIO_YR		0x0010
#define STUDIO_ZR		0x0020
#define STUDIO_LX		0x0040
#define STUDIO_LY		0x0080
#define STUDIO_LZ		0x0100
#define STUDIO_AX		0x0200
#define STUDIO_AY		0x0400
#define STUDIO_AZ		0x0800
#define STUDIO_AXR		0x1000
#define STUDIO_AYR		0x2000
#define STUDIO_AZR		0x4000
#define STUDIO_TYPES	0x7FFF
#define STUDIO_RLOOP	0x8000	// controller that wraps shortest distance

#define MAXEVENTSTRING      64

typedef struct
{
	int					id;
	int					version;

	char				name[64];
	int					length;

	vec3				eyeposition;	// ideal eye position
	vec3				min;			// ideal movement hull size
	vec3				max;

	vec3				bbmin;			// clipping bounding box
	vec3				bbmax;

	int					flags;

	int					numbones;			// bones
	int					boneindex;

	int					numbonecontrollers;		// bone controllers
	int					bonecontrollerindex;

	int					numhitboxes;			// complex bounding boxes
	int					hitboxindex;

	int					numseq;				// animation sequences
	int					seqindex;

	int					numseqgroups;		// demand loaded sequences
	int					seqgroupindex;

	int					numtextures;		// raw textures
	int					textureindex;
	int					texturedataindex;

	int					numskinref;			// replaceable textures
	int					numskinfamilies;
	int					skinindex;

	int					numbodyparts;
	int					bodypartindex;

	int					numattachments;		// queryable attachable points
	int					attachmentindex;

	int					soundtable;
	int					soundindex;
	int					soundgroups;
	int					soundgroupindex;

	int					numtransitions;		// animation node to animation node transition graph
	int					transitionindex;
} studiohdr_t;

// header for demand loaded sequence group data
typedef struct
{
	int					id;
	int					version;

	char				name[64];
	int					length;
} studioseqhdr_t;

// bones
typedef struct
{
	char				name[32];	// bone name for symbolic links
	int		 			parent;		// parent bone
	int					flags;		// ??
	int					bonecontroller[6];	// bone controller index, -1 == none
	float				value[6];	// default DoF values
	float				scale[6];   // scale for delta DoF values
} mstudiobone_t;


// bone controllers
typedef struct
{
	int					bone;	// -1 == 0
	int					type;	// X, Y, Z, XR, YR, ZR, M
	float				start;
	float				end;
	int					rest;	// unsigned char index value at rest
	int					index;	// 0-3 user set controller, 4 mouth
} mstudiobonecontroller_t;

// intersection boxes
typedef struct
{
	int					bone;
	int					group;			// intersection group
	vec3				bbmin;		// bounding box
	vec3				bbmax;
} mstudiobbox_t;

//
// demand loaded sequence groups
//
typedef struct
{
	char				label[32];	// textual name
	char				name[64];	// file name
	int					unused1;    // was "cache"  - index pointer
	int					unused2;    // was "data" -  hack for group 0
} mstudioseqgroup_t;

// sequence descriptions
typedef struct
{
	char				label[32];	// sequence label

	float				fps;		// frames per second	
	int					flags;		// looping/non-looping flags

	int					activity;
	int					actweight;

	int					numevents;
	int					eventindex;

	int					numframes;	// number of frames per sequence

	int					numpivots;	// number of foot pivots
	int					pivotindex;

	int					motiontype;
	int					motionbone;
	vec3				linearmovement;
	int					automoveposindex;
	int					automoveangleindex;

	vec3				bbmin;		// per sequence bounding box
	vec3				bbmax;

	int					numblends;
	int					animindex;		// mstudioanim_t pointer relative to start of sequence group data
										// [blend][bone][X, Y, Z, XR, YR, ZR]

	int					blendtype[2];	// X, Y, Z, XR, YR, ZR
	float				blendstart[2];	// starting value
	float				blendend[2];	// ending value
	int					blendparent;

	int					seqgroup;		// sequence group for demand loading

	int					entrynode;		// transition node at entry
	int					exitnode;		// transition node at exit
	int					nodeflags;		// transition rules

	int					nextseq;		// auto advancing sequences
} mstudioseqdesc_t;


// pivots
typedef struct
{
	vec3				org;	// pivot point
	int					start;
	int					end;
} mstudiopivot_t;

// attachment
typedef struct
{
	char				name[32];
	int					type;
	int					bone;
	vec3				org;	// attachment point
	vec3				vectors[3];
} mstudioattachment_t;

typedef struct
{
	unsigned short	offset[6];
} mstudioanim_t;

// animation frames
typedef union
{
	struct {
		unsigned char	valid;
		unsigned char	total;
	} num;
	short		value;
} mstudioanimvalue_t;



// body part index
typedef struct
{
	char				name[64];
	int					nummodels;
	int					base;
	int					modelindex; // index into models array
} mstudiobodyparts_t;



// skin info
typedef struct
{
	char					name[64];
	int						flags;
	int						width;
	int						height;
	int						index;
} mstudiotexture_t;


// skin families
// short	index[skinfamilies][skinref]

// studio models
typedef struct
{
	char				name[64];

	int					type;

	float				boundingradius;

	int					nummesh;
	int					meshindex;

	int					numverts;		// number of unique vertices
	int					vertinfoindex;	// vertex bone info
	int					vertindex;		// vertex vec3
	int					numnorms;		// number of unique surface normals
	int					norminfoindex;	// normal bone info
	int					normindex;		// normal vec3

	int					numgroups;		// deformation groups
	int					groupindex;
} mstudiomodel_t;


// vec3	boundingbox[model][bone][2];	// complex intersection info


// meshes
typedef struct
{
	int					numtris;
	int					triindex;
	int					skinref;
	int					numnorms;		// per mesh normals
	int					normindex;		// normal vec3
} mstudiomesh_t;

typedef struct mstudioevent_s
{
	int 				frame;
	int					event;
	int					type;
	char				options[MAXEVENTSTRING];
} mstudioevent_t;
#pragma pack(pop)

struct StudioMesh
{
	VertexBuffer* buffer;
	Texture* texture;
	std::vector<modelVert> verts;
	StudioMesh()
	{
		buffer = NULL;
		texture = NULL;
		verts = std::vector<modelVert>();
	}
};

class StudioModel
{
public:
	// entity settings
	float fps;
	float frametime;        //for small fps render
	float m_frame;			// frame
	int m_sequence;			// sequence index
	int m_bodynum;			// bodypart selection	
	int m_skinnum;			// skin group selection
	int m_iGroup;
	int m_iGroupValue;      // subbody 
	bool needForceUpdate;
	unsigned char m_controller[4];	// bone controllers
	unsigned char m_blending[2];		// animation blending
	unsigned char m_mouth = 0;			// mouth position
	EntCube* mdl_cube = NULL;
	vec3 mins, maxs;

	vec3			g_xformverts[MAXSTUDIOVERTS];	// transformed vertices
	vec3			g_lightvalues[MAXSTUDIOVERTS];	// light surface normals

	vec3			g_lightvec;						// light vector in model reference frame
	vec3			g_blightvec[MAXSTUDIOBONES];	// light vectors in bone reference frames
	int				g_ambientlight;					// ambient world light
	float			g_shadelight;					// direct world light
	vec3			g_lightcolor;

	int				g_smodels_total;				// cookie

	float			g_bonetransform[MAXSTUDIOBONES][3][4];	// bone transformation matrix

	int				g_chrome[MAXSTUDIOVERTS][2];	// texture coords for surface normals
	int				g_chromeage[MAXSTUDIOBONES];	// last time chrome vectors were updated
	vec3			g_chromeup[MAXSTUDIOBONES];		// chrome vector "up" in bone reference frames
	vec3			g_chromeright[MAXSTUDIOBONES];	// chrome vector "right" in bone reference frames

	vec3 g_vright;		// needs to be set to viewer's right in order for chrome to work
	float g_lambert;		// modifier for pseudo-hemispherical lighting

	// internal data
	studiohdr_t* m_pstudiohdr;
	mstudiomodel_t* m_pmodel;

	studiohdr_t* m_ptexturehdr;
	studioseqhdr_t* m_panimhdr[32];

	vec4 m_adj;				// FIX: non persistant, make static
	std::vector<Texture*> mdl_textures;
	std::vector<std::vector<StudioMesh>> mdl_mesh_groups;

	std::string filename;

	StudioModel(const std::string modelname)
	{
		mdl_cube = NULL;
		mins = maxs = vec3();
		fps = 30.0;
		m_bodynum = 0;
		needForceUpdate = true;
		frametime = 999999.0f;
		filename = modelname;
		g_vright = vec3();
		g_lambert = 1.0f;
		mdl_textures = std::vector<Texture*>();
		mdl_mesh_groups = std::vector<std::vector<StudioMesh>>();
		m_sequence = m_bodynum = m_skinnum = 0;
		m_frame = 0.0f;
		m_mouth = 0;
		m_pstudiohdr = NULL;
		m_pmodel = NULL;

		for (int i = 0; i < 32; i++)
		{
			m_panimhdr[i] = NULL;
		}
		for (int i = 0; i < 4; i++)
		{
			m_controller[i] = 0;
		}
		for (int i = 0; i < 2; i++)
		{
			m_blending[i] = 0;
		}

		g_ambientlight = 32;
		g_shadelight = 192.0f;

		g_lightvec[0] = 0.0f;
		g_lightvec[1] = 0.0f;
		g_lightvec[2] = -1.0f;

		g_lightcolor[0] = 1.0f;
		g_lightcolor[1] = 1.0f;
		g_lightcolor[2] = 1.0f;

		for (int i = 0; i < 2048; i++)
		{
			g_lightvalues[i][0] = 1.0f;
			g_lightvalues[i][1] = 1.0f;
			g_lightvalues[i][2] = 1.0f;
		}

		Init(filename);
		SetSkin(0);

		SetSequence(0);
		SetController(0, 0.0f);
		SetController(1, 0.0f);
		SetController(2, 0.0f);
		SetController(3, 0.0f);
		SetMouth(0.0f);

		if (m_pstudiohdr)
		{
			for (int n = 0; n < m_pstudiohdr->numbodyparts; ++n)
			{
				SetBodygroup(n, 0);
			}
		}
		SetSkin(0);
	}
	~StudioModel()
	{
		if (mdl_cube)
		{
			delete mdl_cube;
			mdl_cube = NULL;
		}

		if (m_pstudiohdr)
			delete[] m_pstudiohdr;

		for (auto& tex : mdl_textures)
		{
			delete tex;
		}
		for (int i = 0; i < 32; i++)
		{
			if (m_panimhdr[i])
			{
				delete[] m_panimhdr[i];
			}
		}
		for (auto& body : mdl_mesh_groups)
		{
			//for (auto& subbody : body)
			{
				//for (auto& submesh : subbody)
				for (auto& submesh : body)
				{
					if (submesh.buffer)
					{
						delete submesh.buffer;
					}
				}
			}
		}
	}

	void DrawModel(int mesh = -1);

	void Init(std::string modelname);
	void RefreshMeshList(int body);
	void UpdateModelMeshList(void);
	void GetModelMeshes(int& bodies, int& subbodies, int& skins, int& meshes);

	void AdvanceFrame(float dt);
	void ExtractBBox(vec3& mins, vec3& maxs);
	void GetSequenceInfo(float* pflFrameRate, float* pflGroundSpeed);
	float SetController(int iController, float flValue);
	float SetMouth(float flValue);
	float SetBlending(int iBlender, float flValue);
	int SetBodygroup(int iGroup, int iValue);
	int SetSkin(int iValue);
	int SetSequence(int iSequence);
	int GetSequence(void);
	studiohdr_t* LoadModel(std::string modelname, bool IsTexture = false);
	studioseqhdr_t* LoadDemandSequences(std::string modelname, int seqid);
	void CalcBoneAdj(void);
	void CalcBoneQuaternion(int frame, float s, mstudiobone_t* pbone, mstudioanim_t* panim, vec4& q);
	void CalcBonePosition(int frame, float s, mstudiobone_t* pbone, mstudioanim_t* panim, vec3& pos);
	void CalcRotations(vec3* pos, vec4* q, mstudioseqdesc_t* pseqdesc, mstudioanim_t* panim, float f);
	mstudioanim_t* GetAnim(mstudioseqdesc_t* pseqdesc);
	void SlerpBones(vec4 q1[], vec3 pos1[], vec4 q2[], vec3 pos2[], float s);
	void SetUpBones(void);
	void Lighting(float* lv, int bone, int flags, const vec3& normal);
	void Chrome(int* chrome, int bone, const vec3& normal);
	void SetupLighting(void);
	void SetupModel(int bodypart);
	void UploadTexture(mstudiotexture_t* ptexture, unsigned char* data, COLOR3* pal);
private:
	vec3 static_pos1[MAXSTUDIOBONES];
	vec4 static_q1[MAXSTUDIOBONES];
	vec3 static_pos2[MAXSTUDIOBONES];
	vec4 static_q2[MAXSTUDIOBONES];
	vec3 static_pos3[MAXSTUDIOBONES];
	vec4 static_q3[MAXSTUDIOBONES];
	vec3 static_pos4[MAXSTUDIOBONES];
	vec4 static_q4[MAXSTUDIOBONES];

	float static_bonematrix[3][4];
	//vec3 vertexData[MAX_VERTS_PER_CALL];
	//vec2 texCoordData[MAX_VERTS_PER_CALL];
	//vec3 colorData[MAX_VERTS_PER_CALL];

	float vertexData[MAX_VERTS_PER_CALL * 3];
	float texCoordData[MAX_VERTS_PER_CALL * 2];
	//float colorData[MAX_VERTS_PER_CALL * 4];
};

extern std::map<int, StudioModel *> mdl_models;
StudioModel* AddNewModelToRender(const char * path, unsigned int sum = 0);
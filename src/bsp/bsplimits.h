#pragma once
#include <stdint.h>

#ifdef WIN32
#define strcasecmp _stricmp
#endif

#define MAXTEXTURENAME 16
#define MIPLEVELS 4

#define MAX_MAP_HULLS 4
#define MAX_MAP_PLANES 65535
#define MAX_MAP_TEXINFOS 32767  // Can be 65535 if unsigned short?
#define MAX_MAP_MARKSURFS 65535
#define MAX_MAP_VERTS 65535
#define MAX_MAP_FACES 65535 // (unsgined short) This ought to be 32768, otherwise faces(in world) can become invisible. --vluzacn
#define MAX_KEYS_PER_ENT 128
#define MAX_LIGHTMAPS 4
#define MAX_LIGHTSTYLES		256	// a byte limit, don't modify

extern int LIGHTMAP_ATLAS_SIZE; //max for glTexImage2D

extern float FLT_MAX_COORD;


extern int MAX_SURFACE_EXTENT;
extern unsigned int MAX_MAP_MODELS;
#define MAX_MAP_NODES_DEFAULT 32768
extern unsigned int MAX_MAP_NODES;
#define MAX_MAP_CLIPNODES_DEFAULT 32767
extern unsigned int MAX_MAP_CLIPNODES;
extern unsigned int MAX_MAP_LEAVES;
extern unsigned int MAX_MAP_VISDATA; // 64 MB
extern unsigned int MAX_MAP_ENTS;
extern unsigned int MAX_MAP_SURFEDGES;
extern unsigned int MAX_MAP_EDGES;
extern unsigned int MAX_MAP_TEXTURES;
extern unsigned int MAX_MAP_LIGHTDATA; // 64 MB
extern unsigned int MAX_TEXTURE_DIMENSION;
extern unsigned int MAX_TEXTURE_SIZE;

extern unsigned int MAX_KEY_LEN; // not sure if this includes the null char
extern unsigned int MAX_VAL_LEN; // not sure if this includes the null char

extern unsigned int TEXTURE_STEP;

extern void ResetBspLimits(); // reset all limits to default values
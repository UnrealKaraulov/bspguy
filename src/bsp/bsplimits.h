#pragma once
#include <stdint.h>
#include <map>
#include <string>

#ifdef WIN32
#define strcasecmp _stricmp
#endif

#define MAXTEXTURENAME 16
#define MIPLEVELS 4

#define MAX_MAP_NODES_DEFAULT 32768
#define MAX_MAP_CLIPNODES_DEFAULT 32767
#define MAX_MAP_HULLS 4
#define MAX_MAP_PLANES 65535
#define MAX_MAP_TEXINFOS 32767  // Can be 65535 if unsigned short?
#define MAX_MAP_MARKSURFS 65535
#define MAX_MAP_VERTS 65535
#define MAX_MAP_FACES 65535 // (unsgined short) This ought to be 32768, otherwise faces(in world) can become invisible. --vluzacn
#define MAX_KEYS_PER_ENT 512
#define MAX_LIGHTMAPS 4
#define MAX_LIGHTSTYLES		256	// a unsigned char limit, don't modify

extern int MAX_LIGHTMAP_ATLAS_SIZE;

struct BSPLimits 
{
    float fltMaxCoord;
    int maxSurfaceExtent;
    unsigned int maxMapModels;
    unsigned int maxMapNodes;
    unsigned int maxMapClipnodes;
    unsigned int maxMapLeaves;
    unsigned int maxMapVisdata;
    unsigned int maxMapEnts;
    unsigned int maxMapSurfedges;
    unsigned int maxMapEdges;
    unsigned int maxMapTextures;
    unsigned int maxMapLightdata;
    unsigned int maxTextureDimension;
    unsigned int maxTextureSize;
    float maxMapBoundary;
    unsigned int maxKeyLen;
    unsigned int maxValLen;
    unsigned int textureStep;
    std::string engineName;
    bool operator!=(const BSPLimits& other) const;
};

extern BSPLimits g_limits;
extern std::map<std::string, BSPLimits> limitsMap;

void ResetBspLimits();
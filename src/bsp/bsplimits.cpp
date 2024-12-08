#include "bsplimits.h"

int MAX_LIGHTMAP_ATLAS_SIZE = 512;

BSPLimits g_limits = {
    8192.f, // fltMaxCoord
    64, // maxSurfaceExtent
    4096, // maxMapModels
    32768, // maxMapNodes
    32767, // maxMapClipnodes
    65536, // maxMapLeaves
    64 * (1024 * 1024), // maxMapVisdata
    8192, // maxMapEnts
    512000, // maxMapSurfedges
    256000, // maxMapEdges
    4096, // maxMapTextures
    64 * (1024 * 1024), // maxMapLightdata
    1024, // maxTextureDimension
    (1024 * 1024 * 2 * 3) / 2, // maxTextureSize
    4096.0f, // maxMapBoundary
    256, // maxKeyLen
    4096, // maxValLen
    16, // textureStep
    "half-life1" //engineName
};

std::map<std::string, BSPLimits> limitsMap = {
    {"half-life1", g_limits}
};

void ResetBspLimits() {
    g_limits = limitsMap["half-life1"];
}

bool BSPLimits::operator!=(const BSPLimits& other) const {
    return
        fltMaxCoord != other.fltMaxCoord ||
        maxSurfaceExtent != other.maxSurfaceExtent ||
        maxMapModels != other.maxMapModels ||
        maxMapNodes != other.maxMapNodes ||
        maxMapClipnodes != other.maxMapClipnodes ||
        maxMapLeaves != other.maxMapLeaves ||
        maxMapVisdata != other.maxMapVisdata ||
        maxMapEnts != other.maxMapEnts ||
        maxMapSurfedges != other.maxMapSurfedges ||
        maxMapEdges != other.maxMapEdges ||
        maxMapTextures != other.maxMapTextures ||
        maxMapLightdata != other.maxMapLightdata ||
        maxTextureDimension != other.maxTextureDimension ||
        maxTextureSize != other.maxTextureSize ||
        maxMapBoundary != other.maxMapBoundary ||
        maxKeyLen != other.maxKeyLen ||
        maxValLen != other.maxValLen ||
        textureStep != other.textureStep;
}
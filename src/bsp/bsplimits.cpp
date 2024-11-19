#include "bsplimits.h"


float FLT_MAX_COORD = 8192.f;
int LIGHTMAP_ATLAS_SIZE = 512;


int MAX_SURFACE_EXTENT = 64;

unsigned int MAX_MAP_MODELS = 4096;
unsigned int MAX_MAP_NODES = 32768;
unsigned int MAX_MAP_CLIPNODES = 32767;

unsigned int MAX_MAP_LEAVES = 65536;
unsigned int MAX_MAP_VISDATA = 64 * (1024 * 1024); // 64 MB
unsigned int MAX_MAP_ENTS = 8192;
unsigned int MAX_MAP_SURFEDGES = 512000;
unsigned int MAX_MAP_EDGES = 256000;
unsigned int MAX_MAP_TEXTURES = 4096;
unsigned int MAX_MAP_LIGHTDATA = 64 * (1024 * 1024); // 64 MB
unsigned int MAX_TEXTURE_DIMENSION = 1024;
unsigned int MAX_TEXTURE_SIZE = ((MAX_TEXTURE_DIMENSION * MAX_TEXTURE_DIMENSION * 2 * 3) / 2);

float MAX_MAP_BOUNDARY = 4096.0f;

unsigned int MAX_KEY_LEN = 256; // not sure if this includes the null char
unsigned int MAX_VAL_LEN = 4096; // not sure if this includes the null char

// this constant was previously defined in lightmap.cpp. --vluzacn
unsigned int TEXTURE_STEP = 16;  // BSP 31 has 8

void ResetBspLimits()
{
	FLT_MAX_COORD = 32767.f;
	MAX_MAP_MODELS = 4096;
	MAX_MAP_NODES = 32768;
	MAX_MAP_CLIPNODES = 32767;
	MAX_MAP_LEAVES = 65536;
	MAX_MAP_VISDATA = 64 * (1024 * 1024); // 64 MB
	MAX_MAP_ENTS = 8192;
	MAX_MAP_SURFEDGES = 512000;
	MAX_MAP_EDGES = 256000;
	MAX_MAP_TEXTURES = 4096;
	MAX_MAP_LIGHTDATA = 64 * (1024 * 1024); // 64 MB
	MAX_TEXTURE_DIMENSION = 1024;
	MAX_TEXTURE_SIZE = ((MAX_TEXTURE_DIMENSION * MAX_TEXTURE_DIMENSION * 2 * 3) / 2);

	MAX_KEY_LEN = 256;
	MAX_VAL_LEN = 4096;

	MAX_MAP_BOUNDARY = 4096.0f;

	TEXTURE_STEP = 16;

	MAX_SURFACE_EXTENT = 64;
}
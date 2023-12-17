#include "util.h"

struct BSPLEAF32;

bool shiftVis(unsigned char* vis, int len, int offsetLeaf, int shift);

// decompress the given vis data into arrays of bits where each bit indicates if a leaf is visible or not
// iterationLeaves = number of leaves to decompress vis for
// visDataLeafCount = total leaves in the map (exluding the shared solid leaf 0)
// newNumLeaves = total leaves that will be in the map after merging is finished (again, excluding solid leaf 0)
void decompress_vis_lump(Bsp * map,BSPLEAF32* leafLump,  unsigned char* visLump, unsigned char* output,
						 int iterationLeaves, int visDataLeafCount, int newNumLeaves, int leafMemSize, int visLumpMemSize);

void DecompressVis(unsigned char* src, unsigned char* dest, unsigned int dest_length, unsigned int numLeaves, unsigned int src_length);

int CompressVis(unsigned char* src, unsigned int src_length, unsigned char* dest, unsigned int dest_length);

int CompressAll(BSPLEAF32* leafs, unsigned char* uncompressed, unsigned char* output, int numLeaves, int iterLeaves, int bufferSize, int leafMemSize);

void DecompressLeafVis(unsigned char* src, unsigned int src_len, unsigned char* dest, unsigned int dest_length);

extern bool g_debug_shift;


bool CHECKBITFROMBYTES(unsigned char* bytes, int bitid);

#define CHECKVISBIT( vis, b )		((b) >= 0 ? ((vis)[(b) >> 3] & (1 << ((b) & 7))) != 0 : false )
/*
#define SETVISBIT( vis, b )( void )	((b) >= 0 ? (unsigned char)((vis)[(b) >> 3] |= (1 << ((b) & 7))) : (unsigned char)false )
#define CLEARVISBIT( vis, b )( void )	((b) >= 0 ? (unsigned char)((vis)[(b) >> 3] &= ~(1 << ((b) & 7))) : (unsigned char)false )
*/

// LayerBase.h
// Shared constants, helpers, and the layer-index calculation used by all
// world generation layers.
//
// World Z layout (in blocks):
//   Surface     :  Z >= 0          (ChunkZ >= 0)
//   Underground :  Z = -1  .. -256 (ChunkZ = -1 .. -8)
//   Deep        :  Z = -257.. -512 (ChunkZ = -9 .. -16)
//   (add more layers below by extending LayerIndex return values)

#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"

// ---------------------------------------------------------------------------
//  Layout constants
// ---------------------------------------------------------------------------

/** Depth of each underground layer in voxel blocks. */
static constexpr int32 LAYER_DEPTH_BLOCKS = 256;

/** Depth of each underground layer in chunks (must divide evenly). */
static constexpr int32 LAYER_DEPTH_CHUNKS = LAYER_DEPTH_BLOCKS / CHUNK_SIZE_Z;   // = 8

// ---------------------------------------------------------------------------
//  Layer index helpers
// ---------------------------------------------------------------------------

/**
 * Maps a ChunkZ coordinate to an underground layer index.
 *
 *  ChunkZ >= 0  → -1  (surface — handled by LayerSurface)
 *  ChunkZ -1..-8  →  0  (Underground layer 1)
 *  ChunkZ -9..-16 →  1  (Deep layer 2)
 *  etc.
 *
 * Add a new layer generator for each new index value.
 */
inline int32 ChunkZToLayerIndex(int32 ChunkZ)
{
	if (ChunkZ >= 0) return -1;                         // Surface
	return (-ChunkZ - 1) / LAYER_DEPTH_CHUNKS;          // 0, 1, 2, …
}

// ---------------------------------------------------------------------------
//  Flat-array index helper
// ---------------------------------------------------------------------------

/** Converts local chunk voxel coordinates to the flat block array index. */
FORCEINLINE int32 BlockIdx(int32 X, int32 Y, int32 Z)
{
	return X + CHUNK_SIZE_X * (Y + CHUNK_SIZE_Y * Z);
}

// ---------------------------------------------------------------------------
//  Deterministic per-chunk hash (used by all layers for object placement)
// ---------------------------------------------------------------------------

inline uint32 LayerChunkHash(FIntVector Coord, uint32 Salt)
{
	uint32 H = (uint32)(Coord.X * 2654435761u)
	         ^ (uint32)(Coord.Y *  805459861u)
	         ^ (uint32)(Coord.Z * 1234567891u)
	         ^ Salt;
	H ^= H >> 16;
	H *= 0x45d9f3bu;
	H ^= H >> 16;
	return H;
}

// LayerPrimordialCavern.cpp
// Primordial Cavern layer generation (Level 2, GlobalChunkZ -9 .. -16, world Z -257 .. -512).

#include "LayerPrimordialCavern.h"
#include "LayerBase.h"
#include "Voxel/Chunk.h"

void FPrimordialCavernLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	TArray<EBlockType> OutBlocks;
	OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
	{
		for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
		{
			for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
			{
				// Dense stone with rare gravel (~5%) for visual texture
				const uint32 Hash = ((uint32)(GlobalChunkX * CHUNK_SIZE_X + X) * 1234567891u)
				                  ^ ((uint32)(GlobalChunkY * CHUNK_SIZE_Y + Y) *  987654321u)
				                  ^ ((uint32)(LocalChunkZ  * CHUNK_SIZE_Z + Z) * 2654435761u);
				const uint8 HB = (Hash >> 16) & 0xFF;

				OutBlocks[BlockIdx(X, Y, Z)] = (HB < 13) ? EBlockType::Gravel : EBlockType::Stone;
			}
		}
	}

	// TODO: Add rare ore vein generation (ancient minerals)
	// TODO: Add giant stalactite / stalagmite structures
	// TODO: Add large open cavern chamber carving
	// TODO: Add ancient ruin placement

	Chunk.Initialize(OutBlocks);
}

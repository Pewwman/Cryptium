// LayerCrystalCaves.h
// Crystal Caves layer generator (Level 1, GlobalChunkZ -1 .. -8).
//
// World Z range : -1 to -256
// Theme         : Crystal formations, luminous stone, underground rivers (planned)
// Planned       : Crystal ore deposits, glowing stone variants, cave pools

#pragma once

#include "CoreMinimal.h"
#include "ILevelGenerator.h"
#include "Voxel/VoxelTypes.h"

class AChunk;

class CRYPTCRAFT_API FCrystalCavesLevelGenerator : public ILevelGenerator
{
public:
	virtual void GenerateChunk(
		AChunk& Chunk,
		int32 GlobalChunkX,
		int32 GlobalChunkY,
		int32 LocalChunkZ) override;

	virtual FString GetLevelName() const override { return TEXT("Crystal Caves"); }
};

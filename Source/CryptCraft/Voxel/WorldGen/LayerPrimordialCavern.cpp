// LayerPrimordialCavern.cpp
// Primordial Cavern layer generation (Level 2, GlobalChunkZ -9 .. -16, world Z -257 .. -512).
//
// Block layout (256 blocks = 8 chunks, LocalChunkZ 0 = shallowest):
//   LocalChunkZ 0   : Solid stone ceiling                                ( 32 blocks)
//   LocalChunkZ 1   : Ceiling fringe — Perlin stalactites, up to 32 blocks deep
//   LocalChunkZ 2–5 : Pure open air void                                 (128 blocks)
//   LocalChunkZ 6–7 : Land/Water terrain based on 2-octave Perlin        ( 64 blocks)

#include "LayerPrimordialCavern.h"
#include "LayerBase.h"
#include "Voxel/Chunk.h"

// ---------------------------------------------------------------------------
//  2-D Perlin noise  (self-contained; large XY offset decorrelates from surface)
// ---------------------------------------------------------------------------

// Standard Ken Perlin reference permutation table, doubled to 512.
static const uint8 CavePerm[512] =
{
	151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
	140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
	247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
	 57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
	 74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
	 60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
	 65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
	200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
	 52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
	207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
	119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
	129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
	218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
	 81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
	184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
	222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180,
	// repeat
	151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
	140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
	247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
	 57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
	 74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
	 60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
	 65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
	200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
	 52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
	207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
	119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
	129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
	218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
	 81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
	184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
	222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180,
};

static FORCEINLINE float CavePerlinFade(float T)
{
	return T * T * T * (T * (T * 6.f - 15.f) + 10.f);
}

static FORCEINLINE float CavePerlinGrad2(uint8 Hash, float X, float Y)
{
	switch (Hash & 7)
	{
		case 0: return  X + Y;
		case 1: return -X + Y;
		case 2: return  X - Y;
		case 3: return -X - Y;
		case 4: return  X;
		case 5: return -X;
		case 6: return  Y;
		default: return -Y;
	}
}

static float CavePerlin2D(float X, float Y)
{
	const int32 IX = FMath::FloorToInt(X) & 255;
	const int32 IY = FMath::FloorToInt(Y) & 255;
	const float FX = X - FMath::FloorToInt(X);
	const float FY = Y - FMath::FloorToInt(Y);

	const float UX = CavePerlinFade(FX);
	const float UY = CavePerlinFade(FY);

	const uint8 A = CavePerm[IX    ] + IY;
	const uint8 B = CavePerm[IX + 1] + IY;

	return FMath::Lerp(
		FMath::Lerp(CavePerlinGrad2(CavePerm[A    ], FX,       FY      ),
		            CavePerlinGrad2(CavePerm[B    ], FX - 1.f, FY      ), UX),
		FMath::Lerp(CavePerlinGrad2(CavePerm[A + 1], FX,       FY - 1.f),
		            CavePerlinGrad2(CavePerm[B + 1], FX - 1.f, FY - 1.f), UX),
		UY
	);
}

// 3-octave fBm Perlin for ceiling fringe; result clamped to [0, 1].
static float SampleCeilingFringeNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 20000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 96.f;
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 3; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return FMath::Clamp(Value / Total * 0.5f + 0.5f, 0.f, 1.f);
}

// 2-octave fBm Perlin for terrain; result clamped to [0, 1].
// Samples with different offset so terrain is independent of ceiling fringe.
static float SampleTerrainNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 20000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 128.f;
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 2; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return FMath::Clamp(Value / Total * 0.5f + 0.5f, 0.f, 1.f);
}

// ---------------------------------------------------------------------------
//  Zone indices  (LocalChunkZ within the Primordial Cavern layer, 0 = shallowest)
// ---------------------------------------------------------------------------

static constexpr int32 CEILING_SOLID_Z     = 0;
static constexpr int32 CEILING_FRINGE_Z    = 1;
static constexpr int32 AIR_VOID_START_Z    = 2;
static constexpr int32 AIR_VOID_END_Z      = 5;
static constexpr int32 TERRAIN_START_Z     = 6;
static constexpr int32 TERRAIN_END_Z       = 7;

// ---------------------------------------------------------------------------
//  GenerateChunk
// ---------------------------------------------------------------------------

void FPrimordialCavernLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	TArray<EBlockType> OutBlocks;

	// ---- Solid stone ceiling ----------------------------------------
	if (LocalChunkZ == CEILING_SOLID_Z)
	{
		OutBlocks.Init(EBlockType::Stone, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		Chunk.Initialize(OutBlocks);
		return;
	}

	// ---- Pure air void middle section --------------------------------
	if (LocalChunkZ >= AIR_VOID_START_Z && LocalChunkZ <= AIR_VOID_END_Z)
	{
		OutBlocks.Init(EBlockType::Air, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		Chunk.Initialize(OutBlocks);
		return;
	}

	OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	// ---- Ceiling fringe (LocalChunkZ 1) - Stalactites hanging down --------
	if (LocalChunkZ == CEILING_FRINGE_Z)
	{
		for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
		{
			for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
			{
				const float WX = static_cast<float>(GlobalChunkX * CHUNK_SIZE_X + X);
				const float WY = static_cast<float>(GlobalChunkY * CHUNK_SIZE_Y + Y);

				// How many blocks of stone hang down (0..32)
				const int32 FringeBlocks = FMath::RoundToInt(SampleCeilingFringeNoise(WX, WY) * CHUNK_SIZE_Z);

				for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
				{
					// Z=31 is the shallowest face, adjacent to solid ceiling.
					// Stone hangs DOWN: fill the top FringeBlocks of the chunk.
					EBlockType Type = (Z >= CHUNK_SIZE_Z - FringeBlocks) ? EBlockType::Stone : EBlockType::Air;
					OutBlocks[BlockIdx(X, Y, Z)] = Type;
				}
			}
		}
		Chunk.Initialize(OutBlocks);
		return;
	}

	// ---- Terrain section (LocalChunkZ 6-7) - Land vs water ----------------
	if (LocalChunkZ >= TERRAIN_START_Z && LocalChunkZ <= TERRAIN_END_Z)
	{
		for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
		{
			for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
			{
				// Sample Perlin noise for this column (X, Y)
				int32 WX = GlobalChunkX * CHUNK_SIZE_X + X;
				int32 WY = GlobalChunkY * CHUNK_SIZE_Y + Y;
				float NoiseValue = SampleTerrainNoise((float)WX, (float)WY);

				// Threshold at 0.4 gives ~60% land, 40% water
				EBlockType BlockType = (NoiseValue > 0.4f) ? EBlockType::Dirt : EBlockType::Stone;

				// Fill entire column Z with the determined block type
				for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
				{
					OutBlocks[BlockIdx(X, Y, Z)] = BlockType;
				}
			}
		}
		Chunk.Initialize(OutBlocks);
		return;
	}

	// Fallback (shouldn't happen)
	OutBlocks.Init(EBlockType::Stone, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
	Chunk.Initialize(OutBlocks);
}

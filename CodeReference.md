# CryptCraft Code Reference

**Last Updated:** June 2026  
**Engine:** Unreal Engine 5.7  
**Language:** C++  

---

## Architecture Overview

```
CryptCraft: Voxel-based first-person sandbox game
├── Voxel System          (terrain generation, chunk management, block physics)
├── Game Framework        (game mode, character, player controller)
├── Inventory System      (player items, storage)
├── UI System             (HUD, hotbar, inventory display)
└── Items & Pickups       (block/item drops, collection)
```

---

## Core Voxel System

### **VoxelTypes.h**
**Purpose:** Shared constants, enums, and data structures for the entire voxel system.

**Key Definitions:**
- **Chunk Size:** `CHUNK_SIZE_X = 32`, `CHUNK_SIZE_Y = 32`, `CHUNK_SIZE_Z = 32` — uniform cube chunks
- **Block Scale:** `BLOCK_SIZE = 100.f` UE units (1 block = 1 meter)
- **Generation Modes:**
  - `EWorldGenType::Terrain` — Procedural Perlin noise with horizontal (8 chunks) and vertical (±3 chunks) streaming
  - `EWorldGenType::Flat` — Fixed grid flat plane (no streaming)
- **Block Types:** 32 types including Air, Grass, Dirt, Stone, Sand, Gravel, Ores, Logs, Leaves, Planks, Bedrock, Decorative blocks
- **FBlockDefinition struct:** Static properties per block
  - `Color` — fallback tint
  - `bIsOpaque` — face-culling flag
  - `bIsSolid` — collision flag
  - `TextureTop/Side/Bottom` — texture keys for 6 faces

**Usage:** Imported by all voxel files; provides the type system and constants.

---

### **VoxelWorld.h / VoxelWorld.cpp**
**Purpose:** World-level chunk manager and terrain generator.

**Key Responsibilities:**
1. **Chunk Streaming** — Loads/unloads chunks around player:
   - Horizontal: ±8 chunks in XY (17×17 grid)
   - Vertical: ±3 chunks in Z (7 vertical layers)
   - Total active: ~2000 chunks
2. **Terrain Generation** — Procedural Perlin noise (5-octave fBm)
   - Base height: 50 blocks
   - Height range: 30 blocks (surface varies Z=50..80)
   - Surface layers: 1 Grass, 4 Dirt, rest Stone
3. **Texture Atlas** — Packs all block textures into single runtime atlas
4. **Block API** — Global `GetBlockAt()` / `SetBlockAt()`
5. **Material Injection** — Creates dynamic material and binds atlas

**Designer Properties:**
- `WorldGenType` — Terrain or Flat mode
- `RenderDistance` — XY streaming range (default 8 chunks)
- `FlatExtentChunks` / `FlatSurfaceHeight` — Flat mode parameters
- `ChunkMaterial` — Base material (auto-loads `/Game/Materials/M_VoxelChunk` if null)
- `TextureBasePath` — Auto-discovery folder (default `/Game/Textures/Blocks/`)
- `BlockTextures` — Manual texture overrides (TMap)
- `BlockDefinitions` — Block properties (TMap)

**Key Methods:**
- `BeginPlay()` — Initialize atlas, create WorldGenManager, call ConfigureLayerStack()
- `Tick()` — Stream chunks every 0.5 seconds
- `UpdateStreamingPosition()` — Manually refresh streaming
- `GetBlockAt()` / `SetBlockAt()` — Query/modify blocks
- `GetPlayerSpawnLocation()` — Return spawn point above terrain surface
- `ConfigureLayerStack()` — **Single place to define layer order** (reorder, add, remove layers)
- `GenerateFlatChunkData()` — Flat plane fill
- `BuildTextureAtlas()` — Pack textures into runtime atlas

**Important Implementation Details:**
- **Coordinate Conversion:** Local Z in chunk (0-31) must be converted to world Z before comparing against terrain height
  ```cpp
  const int32 WorldZ = Coord.Z * CHUNK_SIZE_Z + Z;
  if (WorldZ > SurfaceZ) Type = EBlockType::Air;  // CORRECT
  // NOT: if (Z > SurfaceZ)  which is WRONG
  ```
- **Collision:** Sync for Flat/preloaded, Async for streamed terrain
- **Material:** Requires `AtlasTexture` parameter in base material
- **Atlas:** Textures must have `CompressionSettings = TC_EditorIcon` for CPU-readable pixels

---

### **Chunk.h / Chunk.cpp**
**Purpose:** Individual 32×32×32 chunk actor with mesh rendering and collision.

**Key Responsibilities:**
1. **Block Storage** — 32,768 blocks in flat array
2. **Mesh Generation** — Greedy meshing algorithm
3. **Face Culling** — Checks opacity including cross-chunk neighbors
4. **Collision** — ProceduralMesh with sync/async cooking

**Properties:**
- `ChunkCoord` — Grid coordinate (set by VoxelWorld)
- `VoxelWorld` — Owning world reference
- `bUseSyncCollision` — Sync vs async collision cooking

**Key Methods:**
- `Initialize()` — Receive block data and build mesh
- `GetBlock()` / `SetBlock()` — Block access/modification
- `RebuildMesh()` — Regenerate mesh from block data
- `IsBlockOpaque()` — Opacity check (queries world for neighbors)
- `GetBlockWithNeighbors()` — Get block, handling boundaries
- `BuildGreedyMesh()` — Core greedy meshing algorithm

**Greedy Meshing Algorithm:**
- 6 directional passes (±X, ±Y, ±Z)
- Per-axis slicing and scanning
- Horizontal rectangle merging
- Per-quad UV encoding with vertex colors

**UV & Color Encoding:**
- `OutUVs` — Direct atlas-space tile corners
- `OutColors.xy` — Atlas tile offset (0..1)
- `OutColors.z` — Tile size factor
- Material formula: `FinalUV = frac(UV0) * TileSize + TileOffset`

**Important Notes:**
- Cross-chunk face culling enabled via VoxelWorld queries
- Mesh generation is CPU-intensive; batch edits before rebuild
- Collision optional for far chunks

---

## World Generation System

All procedural terrain lives under `Source/CryptCraft/Voxel/WorldGen/`.

### Architecture

```
AVoxelWorld::ConfigureLayerStack()        ← only place to change layer order / add layers
  └── FWorldGenerationManager             ← routes chunks to correct generator
        ├── FSurfaceLevelGenerator         Level 0  GlobalChunkZ >= 0
        ├── FCrystalCavesLevelGenerator    Level 1  GlobalChunkZ  -1 ..  -8  (world Z    -1 ..  -256)
        ├── FPrimordialCavernLevelGenerator Level 2  GlobalChunkZ  -9 .. -16  (world Z  -257 ..  -512)
        ├── FHellscapeLevelGenerator       Level 3  GlobalChunkZ -17 .. -24  (world Z  -513 ..  -768)
        └── FFrostbittenLevelGenerator     Level 4  GlobalChunkZ -25 .. -32  (world Z  -769 .. -1024)
```

Each underground level is **256 blocks (8 chunks) deep** by default. Override `GetDepthInChunks()` on any generator to use a different height (e.g. `return 2` for 64 blocks, `return 16` for 512 blocks) — all deeper layers shift automatically.

---

### **ILevelGenerator.h**
`Source/CryptCraft/Voxel/WorldGen/ILevelGenerator.h`

Abstract base class every layer generator implements.

```cpp
virtual void GenerateChunk(AChunk& Chunk, int32 GlobalChunkX, int32 GlobalChunkY, int32 LocalChunkZ) = 0;
virtual FString GetLevelName() const = 0;
virtual int32 GetDepthInChunks() const { return 8; }  // override for non-standard height
```

- `GenerateChunk` — Build block data and call `Chunk.Initialize(Blocks)` inside
- `LocalChunkZ` — 0 = top (shallowest) chunk of this level, N-1 = bottom chunk
- For Level 0 (surface): `LocalChunkZ == GlobalChunkZ` (no transformation)

---

### **WorldGenerationManager.h / .cpp**
`Source/CryptCraft/Voxel/WorldGen/WorldGenerationManager.h`

Owned by `AVoxelWorld` as `TSharedPtr<FWorldGenerationManager> WorldGenManager`.

**Key Methods:**
- `RegisterLevel(int32 Index, TSharedPtr<ILevelGenerator>)` — Add a generator at a level slot
- `RouteChunkGeneration(AChunk&, GlobalX, GlobalY, GlobalZ)` — Dispatch to correct generator; bedrock fallback if none registered
- `GetLevelIndex(GlobalChunkZ)` — Which level owns this chunk Z
- `GetLocalChunkZ(GlobalChunkZ)` — Local Z (0 = top) within that level

**Routing math** (`ResolveChunkZ` private helper):
- Walks the registered level stack, consuming each level's `GetDepthInChunks()` worth of chunks
- `LocalChunkZ = LevelTopChunkZ - GlobalChunkZ`
- Unregistered depth → bedrock fill fallback with Warning log

---

### **LayerBase.h**
`Source/CryptCraft/Voxel/WorldGen/LayerBase.h`  
Header-only shared utilities included by all generator `.cpp` files.

- `LAYER_DEPTH_BLOCKS = 256`, `LAYER_DEPTH_CHUNKS = 8` — standard level height constants
- `BlockIdx(X, Y, Z)` — flat array index: `X + CHUNK_SIZE_X * (Y + CHUNK_SIZE_Y * Z)`
- `LayerChunkHash(Coord, Salt)` — deterministic per-chunk hash for pseudo-random placement
- `ChunkZToLayerIndex(ChunkZ)` — legacy helper (superseded by `WorldGenerationManager::GetLevelIndex`)

---

### **Layer Files**

| File | Class | Level | World Z | Status |
|---|---|---|---|---|
| `LayerSurface.h/.cpp` | `FSurfaceLevelGenerator` | 0 | ≥ 0 | **Active** — Perlin fBm terrain |
| `LayerCrystalCaves.h/.cpp` | `FCrystalCavesLevelGenerator` | 1 | −1..−256 | **Active** — Procedural stalactites/stalagmites + open air void |
| `LayerPrimordialCavern.h/.cpp` | `FPrimordialCavernLevelGenerator` | 2 | −257..−512 | Placeholder (stone + 5% gravel) |
| `LayerHellscape.h/.cpp` | `FHellscapeLevelGenerator` | 3 | −513..−768 | Placeholder (solid stone) |
| `LayerFrostbitten.h/.cpp` | `FFrostbittenLevelGenerator` | 4 | −769..−1024 | Placeholder (solid stone) |

---

### **Surface Generator (FSurfaceLevelGenerator)**
`Source/CryptCraft/Voxel/WorldGen/LayerSurface.h/.cpp`

**Parameters:**
```cpp
static constexpr int32 BASE_HEIGHT  = 50;  // Minimum surface Z in blocks
static constexpr int32 HEIGHT_RANGE = 30;  // Max extra height above base
static constexpr int32 DIRT_DEPTH   =  4;  // Dirt blocks between grass and stone
```

**Generation formula (per XY column):**
1. Sample 5-octave fBm Perlin noise at (WorldX, WorldY) → value 0..1
2. `SurfaceZ = BASE_HEIGHT + RoundToInt(Noise * HEIGHT_RANGE)` → 50..80
3. Fill Z: `> SurfaceZ` = Air, `== SurfaceZ` = Grass, `>= SurfaceZ - 4` = Dirt, else Stone
4. Second pass: `PlaceSurfaceObjects()` — boulder placement via `LayerChunkHash`

**Public static:**
- `SampleHeight(WorldX, WorldY)` — used by `VoxelWorld::GetPlayerSpawnLocation()`

---

### **Adding a New Layer**

1. Create `LayerXxx.h` and `LayerXxx.cpp` in `Voxel/WorldGen/` following the existing pattern
2. Inherit `ILevelGenerator`, implement `GenerateChunk()` and `GetLevelName()`
3. Override `GetDepthInChunks()` if not 256 blocks
4. In `VoxelWorld.cpp` — add `#include "WorldGen/LayerXxx.h"` at the top
5. In `AVoxelWorld::ConfigureLayerStack()` — add `WorldGenManager->RegisterLevel(N, MakeShared<FXxxLevelGenerator>())` and renumber anything below it

---

## Terrain Generation Details

**Defined in:** `FSurfaceLevelGenerator` (`Voxel/WorldGen/LayerSurface.h/.cpp`)

**Parameters:**
```cpp
FSurfaceLevelGenerator::BASE_HEIGHT  = 50  // Minimum surface Z
FSurfaceLevelGenerator::HEIGHT_RANGE = 30  // Maximum extra Z
FSurfaceLevelGenerator::DIRT_DEPTH   =  4  // Dirt blocks below grass
```

**Generation Formula (per column):**
1. Sample Perlin noise at (WX, WY) → value 0..1
2. Calculate terrain surface: `SurfaceZ = 50 + RoundToInt(Noise * 30.f)` → 50..80 blocks
3. Fill Z column:
   - `WorldZ > SurfaceZ` → Air
   - `WorldZ == SurfaceZ` → Grass
   - `SurfaceZ - 4 <= WorldZ < SurfaceZ` → Dirt
   - `WorldZ < SurfaceZ - 4` → Stone

**Player Spawn (GetPlayerSpawnLocation):**
```cpp
float TerrainNoise = FSurfaceLevelGenerator::SampleHeight(0.f, 0.f);
int32 TerrainSurfaceZ = FSurfaceLevelGenerator::BASE_HEIGHT
                      + RoundToInt(TerrainNoise * FSurfaceLevelGenerator::HEIGHT_RANGE);
int32 SpawnBlockZ = TerrainSurfaceZ + 2;  // 2 blocks above surface
float SpawnZ = (SpawnBlockZ + 1.f) * BLOCK_SIZE + 96.f;  // +96 for capsule
```

---

### **Crystal Caves Generator (FCrystalCavesLevelGenerator)**
`Source/CryptCraft/Voxel/WorldGen/LayerCrystalCaves.h/.cpp`

**Layout (Level 1, 256 blocks total):**
```
LocalChunkZ 0   →  32 blocks  Solid stone ceiling
LocalChunkZ 1   →  32 blocks  Ceiling fringe (Perlin stalactites hang down)
LocalChunkZ 2–5 → 128 blocks  Open air void
LocalChunkZ 6   →  32 blocks  Floor fringe (Perlin stalagmites rise up)
LocalChunkZ 7   →  32 blocks  Solid stone floor
```

**Generation Formula (per XY column):**
1. Sample 3-octave fBm Perlin noise at `(WorldX + 10000.5, WorldY + 10000.5)` with base frequency 1/96 → value 0..1
   - Large offset decorrelates cave pattern from surface terrain
   - Result clamped [0, 1]
2. `FringeDepth = RoundToInt(NoiseValue * 32)` — how many blocks of stone this column contributes (0..32)
3. **Ceiling fringe** (LocalChunkZ = 1): Stone fills top 32 slots of chunk (near Z=31), hanging down
4. **Floor fringe** (LocalChunkZ = 6): Stone fills bottom N slots of chunk (near Z=0), rising up
5. **Open void** (LocalChunkZ 2–5): Pure air, no variation

**Block Layout Within Chunk:**
- Local Z = 31 is the shallowest (adjacent to ceiling)
- Local Z = 0 is the deepest (adjacent to floor)
- Ceiling fringe: `if (Z >= CHUNK_SIZE_Z - FringeDepth) → Stone else Air`
- Floor fringe: `if (Z < FringeDepth) → Stone else Air`
- Fringe depth varies smoothly across XY boundary with no seams

---

## Game Framework

### **CryptCraftGameMode.h / CryptCraftGameMode.cpp**
**Purpose:** Game initialization, world setup, player spawn management.

**Designer Properties:**
- `VoxelWorldClass` — AVoxelWorld subclass
- `WorldGenType` — Mode selection (overrides existing VoxelWorld)
- `SunIntensity` — Directional light brightness
- `SunRotation` — Sun orientation

**Key Methods:**
- `BeginPlay()` — Spawn VoxelWorld, lights; defer player teleport
- `EnsureVoxelWorld()` — Find or spawn VoxelWorld
- `EnsureDirectionalLight()` — Find or spawn sun
- `EnsureSkyLight()` — Find or spawn sky light
- `TeleportPlayersToSurface()` — Move player above terrain

**Important Notes:**
- Player teleport deferred to next tick for initialization
- If VoxelWorld exists, its WorldGenType is overridden
- VoxelWorld is now code-spawned, not placed in editor

---

### **CryptCraftCharacter.h / CryptCraftCharacter.cpp**
**Purpose:** Abstract base class for the first-person character. Provides camera, mesh, and input callback stubs. **Do not use directly in-game — use `CryptWorldCharacter` instead.**

**Class Hierarchy:**
```
ACryptCraftCharacter  (base — camera, mesh, input callbacks)
  └── ACryptWorldCharacter  (concrete — voxel interaction, inventory, input loading)
        └── BP_CryptWorldCharacter  (Blueprint child — the actual in-game pawn)
```

**Key Components:**
- `FirstPersonMesh` — Skeletal mesh for arms
- `FirstPersonCameraComponent` — Player camera
- Protected UPROPERTYs for subclasses to assign: `JumpAction`, `MoveAction`, `LookAction`, `MouseLookAction`

**Input Callbacks (implemented here, triggered by subclass bindings):**
- `DoMove()` — WASD movement
- `DoJumpStart()` / `DoJumpEnd()` — Jump state
- `LookInput()` — Camera Yaw/Pitch (handles both IA_Look and IA_MouseLook)

**SetupPlayerInputComponent:**
- Binds Jump, Move, Look, MouseLook to the above callbacks
- Does NOT load any input assets — subclass must assign the action UPROPERTYs in its constructor

**Important Notes:**
- Uses UE5 Enhanced Input System
- Loads nothing from Content — `CryptWorldCharacter` constructor does all asset loading

---

### **CryptWorldCharacter.h / CryptWorldCharacter.cpp**
**Purpose:** Concrete in-game player character. Inherits `CryptCraftCharacter`, adds voxel interaction and inventory. The Blueprint child `BP_CryptWorldCharacter` is the actual pawn used in the game.

**Key Properties:**
- `TraceRange` — Max reach for block interaction (default 500 UE units = 5 blocks)
- `SelectedBlockType` — Block placed on right-click
- `ItemPickupClass` — Actor class spawned when a block is mined
- `BlockTypeToItemID` — `TMap<EBlockType, FName>` mapping block type to DataTable row name
- `DropSpawnImpulseZ` — Upward impulse for spawned pickups
- `InventoryComponent` — The character's inventory (50-slot grid + 10-slot hotbar + equipment)
- `HotbarWidgetClass` / `InventoryWidgetClass` — Assign WBP_Hotbar / WBP_Inventory in Blueprint details

**Constructor (loads all input assets):**
- Loads `IMC_Default` (priority 0) and `IMC_MouseLook` (priority 1)
- Loads and assigns `JumpAction`, `MoveAction`, `LookAction`, `MouseLookAction` to parent UPROPERTYs
- Sets default `BlockTypeToItemID` entries for all mineable blocks

**BeginPlay (calls Super first):**
- Caches `AVoxelWorld` reference via `TActorIterator`
- Re-attaches camera to capsule at eye height (Z=60) as fallback if no skeletal socket
- Creates and shows `HotbarWidgetInstance`, calls `Init(InventoryComponent)`
- Adds `IMC_Default` (priority 0) and `IMC_MouseLook` (priority 1) to Enhanced Input subsystem

**SetupPlayerInputComponent (calls Super first):**
- Binds `LMB` → `BreakBlock`, `RMB` → `PlaceBlock`, `E` → `ToggleInventory`

**Input Mapping Context Rules (CRITICAL):**
- `IMC_Default` → priority 0 (WASD, Jump, Look)
- `IMC_MouseLook` → priority 1 (Mouse XY look overlay — evaluated **before** priority 0)
- When replacing the input context (e.g. free cam), MUST remove BOTH contexts
- When restoring, MUST re-add both at their original priorities
- Leaving `IMC_MouseLook` active while adding a new context at priority 0 will cause it to silently consume mouse input before the new context sees it

---

### **CryptCraftPlayerController.h / CryptCraftPlayerController.cpp**
**Purpose:** Base player controller. Manages input mapping contexts and optionally spawns touch controls. Used directly — no concrete subclass exists.

**Designer Properties:**
- `DefaultMappingContexts` — Input contexts applied on BeginPlay (note: `CryptWorldCharacter` also adds its own contexts — these are additive)
- `MobileExcludedMappingContexts` — Contexts excluded on mobile
- `MobileControlsWidgetClass` — Touch UI widget to spawn on mobile
- `bForceTouchControls` — Force touch controls on desktop (debug)

**Key Methods:**
- `BeginPlay()` — Applies `DefaultMappingContexts` to local player subsystem
- `ShouldUseTouchControls()` — Returns true on mobile or if `bForceTouchControls` is set

---

### **CryptWorldGameMode.h / CryptWorldGameMode.cpp**
**Purpose:** Concrete (non-abstract) GameMode that makes `ACryptCraftGameMode` instantiable without a Blueprint wrapper. Contains no logic of its own — all behaviour is inherited from `ACryptCraftGameMode`.

**Class Hierarchy:**
```
ACryptCraftGameMode  (base — VoxelWorld spawning, lighting, player teleport)
  └── ACryptWorldGameMode  (concrete — used in levels)
```

**Usage:** Set `ACryptWorldGameMode` (or its Blueprint child) as the GameMode in project settings or level settings.

---

## Recent Changes & Bug Fixes

### Chunk Size Refactor (June 2026)
- **Old:** 16×16×128 (tall, narrow chunks)
- **New:** 32×32×32 (uniform cubes)
- **Reason:** Balanced vertical and horizontal scaling; supports underground caverns
- **Impact:** All coordinate calculations auto-scale via `CHUNK_SIZE_*` constants

### World Coordinate Bug Fix (June 2026)
- **Problem:** Terrain generation compared local chunk Z (0-31) against world surface Z (50-80), filling everything with stone
- **Root Cause:** Missing coordinate conversion in GenerateChunkData Z-loop
- **Solution:** Convert local Z to world Z before comparison
  ```cpp
  const int32 WorldZ = Coord.Z * CHUNK_SIZE_Z + Z;  // REQUIRED
  if (WorldZ > SurfaceZ) Type = EBlockType::Air;
  ```

### Texture Atlas Auto-Load (June 2026)
- **Problem:** VoxelWorld spawned via code had no ChunkMaterial assigned
- **Solution:** Auto-load `/Game/Materials/M_VoxelChunk` in BeginPlay if null
- **Result:** Textured terrain visible without manual editor setup

### World Generation Architecture Refactor (June 2026)
- **Removed:** `VoxelGenLayers.h/.cpp` (old layer stubs, now deleted)
- **Removed:** `VoxelWorld::GenerateChunkData()`, `GenerateSurfaceObjects()`, `SampleTerrainHeight()` — all moved to layer files
- **Added:** `ILevelGenerator` abstract base class (`Voxel/WorldGen/ILevelGenerator.h`)
- **Added:** `FWorldGenerationManager` routing class (`Voxel/WorldGen/WorldGenerationManager.h/.cpp`) — variable-depth stack walker; each layer declares its own `GetDepthInChunks()`
- **Added:** Five layer generators: `FSurfaceLevelGenerator`, `FCrystalCavesLevelGenerator`, `FPrimordialCavernLevelGenerator`, `FHellscapeLevelGenerator`, `FFrostbittenLevelGenerator`
- **Added:** `AVoxelWorld::ConfigureLayerStack()` — single function to reorder/add/remove layers, called from `BeginPlay()`
- **Result:** Adding or reordering layers requires changes in exactly two places: a new `.h/.cpp` file + one `RegisterLevel` line in `ConfigureLayerStack()`

### Crystal Caves Procedural Generation (June 2026)
- **Status:** `FCrystalCavesLevelGenerator` now implements full procedural layout with stalactites/stalagmites
- **Layout:** 32-block stone ceiling → 32-block fringe → 128-block open void → 32-block fringe → 32-block stone floor
- **Algorithm:** 3-octave fBm Perlin noise at frequency 1/96, sampled at (WorldX + 10000.5, WorldY + 10000.5) to decorrelate from surface
- **Result:** Per-column fringe depth (0–32 blocks) varies smoothly with no seams at chunk boundaries; ceiling stalactites hang down, floor stalagmites rise up

### Layers Mode Removal (June 2026)
- **Status:** `VoxelGenLayers.h/.cpp` **deleted** — superseded by the `WorldGen/` layer architecture above

### Inventory System — Complete Implementation (June 2026, bramsell)
- **Added:** Full inventory data model (`UInventoryComponent` — 50-slot grid, 10-slot hotbar, equipment map)
- **Added:** Drag-and-drop slot widgets (`UInventorySlotWidget`, `UInventoryDragDropOperation`)
- **Added:** Full inventory screen (`UInventoryWidget` — WBP_Inventory)
- **Added:** Persistent hotbar (`UHotbarWidget` — WBP_Hotbar, shown at all times)
- **Added:** Item pickup actors and `FItemData` DataTable row struct
- **Wired:** `CryptWorldCharacter` now creates `UInventoryComponent`, spawns hotbar in BeginPlay, E key toggles inventory
- **Wired:** Block mining drops `ItemPickup` actors using `BlockTypeToItemID` map

### CryptWorldCharacter Clarification (June 2026)
- **Confirmed:** The actual in-game pawn is `BP_CryptWorldCharacter` → `ACryptWorldCharacter` → `ACryptCraftCharacter`
- **Rule:** New player features go in `CryptWorldCharacter`, not `CryptCraftCharacter`
- **Input priority bug documented:** `IMC_MouseLook` at priority 1 silently consumes mouse input over any context added at priority 0 — always remove both IMC_Default AND IMC_MouseLook when swapping input contexts

---

## Quick Start (Code-Only Setup)

1. **GameMode Auto-Spawns VoxelWorld** — No manual placement needed
2. **Terrain Generates on Play** — Chunks stream around player in ±8 XY, ±3 Z
3. **Player Spawns Above Surface** — GetPlayerSpawnLocation calculates height from terrain
4. **Textures Injected at Runtime** — Atlas built from `/Game/Textures/Blocks/` or `BlockTextures` map
5. **Material Auto-Loaded** — ChunkMaterial auto-discovers `/Game/Materials/M_VoxelChunk`

**To customize:**
- Override `BlockDefinitions` map in GameMode or Blueprint
- Override `BlockTextures` map to replace textures
- Set `WorldGenType` in GameMode to switch Terrain ↔ Flat
- Adjust terrain parameters: `BASE_HEIGHT`, `HEIGHT_RANGE`, `DIRT_DEPTH` in GenerateChunkData

---

## Inventory System

### **Inventory/InventoryComponent.h / .cpp**
**Purpose:** Data model for player items. Attached as a component to `CryptWorldCharacter`.

**Layout:**
- `MainGrid` — `TArray<FInventorySlot>`, 10×5 = 50 slots
- `Hotbar` — `TArray<FInventorySlot>`, 10 slots
- `EquipmentSlots` — `TMap<EEquipmentSlot, FInventorySlot>`

**FInventorySlot struct:**
```cpp
FName ItemID;      // Row name in ItemDataTable (FName::None = empty)
int32 StackCount;  // 0 when empty
```

**Key Properties:**
- `ItemDataTable` — `UDataTable` (row type: `FItemData`) — assign asset in editor
- `ActiveHotbarIndex` — Currently selected hotbar slot (0-based)

**Core API:**
- `AddItem(FName ItemID, int32 Count)` — Stacks onto partials first, then fills empty slots; returns leftover count

**Events (BlueprintAssignable):**
- `OnInventoryChanged` — Any slot changed
- `OnEquipmentChanged(EEquipmentSlot)` — Equipment slot changed
- `OnHotbarSelectionChanged(int32)` — Active hotbar slot changed

---

### **UI/InventoryWidget.h / .cpp**
**Purpose:** Full inventory screen (WBP_Inventory). 10×5 grid + hotbar strip + optional equipment panel.

**Required Blueprint-bound widgets (exact names):**
- `UUniformGridPanel "MainGridPanel"` — 50-slot grid
- `UHorizontalBox "HotbarBox"` — 10-slot hotbar strip

**Optional bound widgets:**
- `UWidgetSwitcher "TopHalfSwitcher"` — Switches between armor and secondary equipment views
- `UVerticalBox "MainArmorBox"` / `"SecondaryEquipmentBox"` — Equipment panels
- `UVerticalBox "EquipmentBox"` — Legacy single-list fallback

**Designer properties to set in Blueprint:**
- `SlotWidgetClass` — Set to WBP_InventorySlot (for grid/hotbar slots)
- `EquipSlotWidgetClass` — Set to WBP_InventorySlot (for equipment slots)

**API:**
- `Init(UInventoryComponent*)` — Wire to character's inventory; call once on open
- `RefreshAll()` — Rebuild all slot visuals

---

### **UI/InventorySlotWidget.h / .cpp**
**Purpose:** Single inventory/hotbar slot widget with full drag-and-drop support.

**Required Blueprint-bound widgets (BindWidgetOptional — no crash if absent):**
- `UImage "ItemIcon"` — Item icon
- `UTextBlock "StackCountText"` — Stack count label

**API:**
- `RefreshSlot(FInventorySlot&, FItemData*)` — Update visuals; pass nullptr Data for empty
- `SetSelected(bool)` — Highlight for active hotbar slot
- `RestoreItemVisibility()` — Used after drag cancel to show item again

**Key properties:**
- `SlotIndex` — Index in parent container
- `bIsHotbarSlot` — True if hotbar, false if main grid
- `InventoryComponent` — Set by parent widget before use

**Drag-and-Drop (native C++ implementation):**
- `NativeOnMouseButtonDown` → starts drag
- `NativeOnDragDetected` → creates `UInventoryDragDropOperation`
- `NativeOnDrop` → handles swap/stack between slots
- `NativeOnDragCancelled` → restores item visibility on cancel

---

### **UI/InventoryDragDropOperation.h**
**Purpose:** Carries drag source metadata between slot widgets.

**Fields:**
- `bFromHotbar` — Source is hotbar vs main grid
- `FromIndex` — Source slot index
- `ItemID` — Item being dragged
- `Count` — Stack count being dragged
- `SourceSlotWidget` — Weak ref to source for visual feedback

---

### **UI/HotbarWidget.h / .cpp**
**Purpose:** Persistent 10-slot hotbar shown at all times (WBP_Hotbar).

**Lifecycle:** Created in `CryptWorldCharacter::BeginPlay()`, added to viewport at z-order 0, initialized with `Init(InventoryComponent)`.

**Integration:**
- Listens to `InventoryComponent->OnHotbarSelectionChanged` to highlight active slot
- Listens to `InventoryComponent->OnInventoryChanged` to refresh icons

---

## Items System

### **Items/ItemData.h**
**Purpose:** DataTable row struct defining item properties.

- `FItemData` — Row struct: display name, icon, max stack size, equipment slot, etc.
- `EEquipmentSlot` — Enum for equipment slot types (Helmet, Chestplate, etc.)

### **Items/ItemPickup.h / .cpp**
**Purpose:** World actor spawned when a block is mined. Floats above the ground, auto-collected when player walks over it.

**Spawning:** `CryptWorldCharacter::BreakBlock()` uses `BlockTypeToItemID` map to determine the item row name, then spawns an `ItemPickupClass` with upward impulse `DropSpawnImpulseZ`.

---

## Known Limitations

1. **Single Biome** — Terrain uses uniform parameters (no biome variation yet)
2. **No Caves/Tunnels** — Surface terrain is solid height-map; underground layers are stone placeholders (architecture ready, generation not yet implemented)
3. **No Ores** — Stone is uniform; ore placement TODOs exist in each underground layer file
4. **Surface Objects** — Boulder placement exists in `FSurfaceLevelGenerator::PlaceSurfaceObjects()` but limited
5. **No Saving** — World state not persisted (in-memory only)
6. **No Water/Lava** — Placeholder block types only
7. **Underground Layers** — Crystal Caves, Primordial Cavern, Hellscape, Frostbitten are solid stone placeholders awaiting real generation

---

## Performance Notes

- **2000 Chunks Loaded** — ~17×17×7 active at typical RenderDistance=8, ±3Z
- **Async Collision** — Terrain chunks cook collision on worker thread
- **Greedy Mesh CPU** — ~1-5ms per chunk depending on surface complexity
- **Stream Interval** — 0.5 seconds between update passes
- **Memory:** Each chunk ~320KB (32³×4 bytes) + mesh/collision

---

## File Structure

```
Source/CryptCraft/
├── CryptCraft.h / .cpp
├── CryptCraftGameMode.h / .cpp       [Game initialization, VoxelWorld spawning]
├── CryptCraftCharacter.h / .cpp      [Abstract base: camera, movement, input stubs]
├── CryptCraftPlayerController.h / .cpp
├── CryptWorldCharacter.h / .cpp      [Concrete player: voxel interaction, inventory]
├── CryptWorldGameMode.h / .cpp       [Game mode for CryptWorld levels]
├── Voxel/
│   ├── VoxelTypes.h                  [Constants & enums]
│   ├── VoxelWorld.h / .cpp           [World manager, chunk streaming, layer routing]
│   ├── Chunk.h / .cpp                [Chunk actor, greedy meshing]
│   └── WorldGen/
│       ├── ILevelGenerator.h         [Abstract base: GenerateChunk(), GetDepthInChunks()]
│       ├── WorldGenerationManager.h / .cpp  [Routes chunks to correct layer generator]
│       ├── LayerBase.h               [Shared constants: BlockIdx(), LayerChunkHash()]
│       ├── LayerSurface.h / .cpp     [FSurfaceLevelGenerator — Perlin fBm terrain]
│       ├── LayerCrystalCaves.h / .cpp      [FCrystalCavesLevelGenerator — placeholder]
│       ├── LayerPrimordialCavern.h / .cpp  [FPrimordialCavernLevelGenerator — placeholder]
│       ├── LayerHellscape.h / .cpp   [FHellscapeLevelGenerator — placeholder]
│       └── LayerFrostbitten.h / .cpp [FFrostbittenLevelGenerator — placeholder]
├── Inventory/
│   └── InventoryComponent.h / .cpp  [Player inventory: grid, hotbar, equipment]
├── Items/
│   ├── ItemData.h                    [FItemData DataTable row, EEquipmentSlot enum]
│   └── ItemPickup.h / .cpp          [World drop actor]
└── UI/
    ├── HotbarWidget.h / .cpp         [Persistent 10-slot hotbar (WBP_Hotbar)]
    ├── InventoryWidget.h / .cpp      [Full inventory screen (WBP_Inventory)]
    ├── InventorySlotWidget.h / .cpp  [Single slot with drag-drop (WBP_InventorySlot)]
    ├── InventoryDragDropOperation.h  [Drag payload: source index, item, count]
    └── PickupLabelWidget.h / .cpp    [World-space label on item pickups]
```

---

## For Future Developers

- **Coordinate System:** X = East, Y = North, Z = Up. Blocks stored as linear array: `index = X + SizeX * (Y + SizeY * Z)`
- **Chunk Coordinates:** Integer grid (0,0,0) = origin. World voxel to chunk: `FloorDivide(WorldVoxel, ChunkSize)`
- **Material UVs:** Non-standard; check `Chunk.cpp:BuildGreedyMesh()` for encoding scheme
- **Streaming:** Player position queried every 0.5s; use `UpdateStreamingPosition()` for immediate refresh
- **Collision:** Both sync and async paths tested; verify physics bodies on new generation modes

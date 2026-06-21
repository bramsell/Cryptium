// Chunk.cpp

#include "Chunk.h"
#include "VoxelWorld.h"
#include "ProceduralMeshComponent.h"

// ---------------------------------------------------------------------------
AChunk::AChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
	ProceduralMesh->bUseAsyncCooking = true;  // cook collision on a worker thread
	SetRootComponent(ProceduralMesh);
}

// ---------------------------------------------------------------------------
//  Block access
// ---------------------------------------------------------------------------

bool AChunk::BlockIndex(int32 X, int32 Y, int32 Z, int32& OutIndex)
{
	if (X < 0 || X >= CHUNK_SIZE_X ||
		Y < 0 || Y >= CHUNK_SIZE_Y ||
		Z < 0 || Z >= CHUNK_SIZE_Z)
	{
		return false;
	}
	OutIndex = X + CHUNK_SIZE_X * (Y + CHUNK_SIZE_Y * Z);
	return true;
}

EBlockType AChunk::GetBlock(int32 X, int32 Y, int32 Z) const
{
	// Also guards the live-coding re-instancing case: bIsUniform==false but Blocks is empty.
	if (bIsUniform || Blocks.Num() == 0) return UniformBlockType;
	int32 Idx;
	if (!BlockIndex(X, Y, Z, Idx)) return EBlockType::Air;
	return Blocks[Idx];
}

void AChunk::SetBlock(int32 X, int32 Y, int32 Z, EBlockType Type, bool bRebuildMesh)
{
	int32 Idx;
	if (!BlockIndex(X, Y, Z, Idx)) return;
	if (bIsUniform || Blocks.Num() == 0) DeUniformify();
	Blocks[Idx] = Type;
	if (bRebuildMesh) RebuildMesh();
}

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------

void AChunk::Initialize(const TArray<EBlockType>& InBlocks)
{
	check(InBlocks.Num() == CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
	Blocks = InBlocks;

	// Uniform detection — O(32768), runs once at generation time.
	const EBlockType First = Blocks[0];
	bIsUniform = true;
	for (const EBlockType B : Blocks)
	{
		if (B != First) { bIsUniform = false; break; }
	}
	if (bIsUniform)
	{
		UniformBlockType = First;
		Blocks.Empty();
	}

	RebuildMesh();
}

void AChunk::DeUniformify()
{
	// Works both for genuinely uniform chunks and for re-instanced chunks with empty Blocks.
	Blocks.Init(UniformBlockType, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
	bIsUniform = false;
}

void AChunk::RebuildMesh()
{
	// Skip meshing if this chunk and all 6 face-neighbors are uniform with the same
	// block type — every face boundary would be culled anyway.
	if (bIsUniform && VoxelWorld)
	{
		static const FIntVector Dirs[6] = {
			{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
		};
		bool bAllSame = true;
		for (const FIntVector& D : Dirs)
		{
			AChunk* N = VoxelWorld->GetChunkAt(ChunkCoord + D);
			if (!N || !N->bIsUniform || N->UniformBlockType != UniformBlockType)
			{
				bAllSame = false;
				break;
			}
		}
		if (bAllSame)
		{
			ProceduralMesh->ClearAllMeshSections();
			return;
		}
	}

	TArray<FVector>       Vertices;
	TArray<int32>         Triangles;
	TArray<FVector>       Normals;
	TArray<FVector2D>     UVs;
	TArray<FLinearColor>  Colors;

	BuildGreedyMesh(Vertices, Triangles, Normals, UVs, Colors);

	// Sync cooking: collision is ready immediately (used for flat / pre-loaded worlds).
	// Async cooking: collision is built on a worker thread (used for streamed terrain).
	ProceduralMesh->bUseAsyncCooking = !bUseSyncCollision;

	// Use the LinearColor overload so atlas UV offsets have full float precision.
	TArray<FProcMeshTangent> Tangents;
	ProceduralMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
	                                              Colors, Tangents, /*bCreateCollision=*/true);

	// Apply the chunk material if the world has one set
	if (VoxelWorld)
	{
		UMaterialInterface* Mat = VoxelWorld->GetChunkMaterial();
		if (Mat)
		{
			ProceduralMesh->SetMaterial(0, Mat);
		}
	}
}

// ---------------------------------------------------------------------------
//  Neighbor sampling
// ---------------------------------------------------------------------------

EBlockType AChunk::GetBlockWithNeighbors(int32 X, int32 Y, int32 Z) const
{
	// Local block
	if (X >= 0 && X < CHUNK_SIZE_X &&
		Y >= 0 && Y < CHUNK_SIZE_Y &&
		Z >= 0 && Z < CHUNK_SIZE_Z)
	{
		return GetBlock(X, Y, Z);
	}

	// Query the owning world for out-of-bounds positions
	if (!VoxelWorld) return EBlockType::Air;

	// Convert local voxel → world voxel
	FIntVector WorldVoxel(
		ChunkCoord.X * CHUNK_SIZE_X + X,
		ChunkCoord.Y * CHUNK_SIZE_Y + Y,
		ChunkCoord.Z * CHUNK_SIZE_Z + Z
	);
	return VoxelWorld->GetBlockAt(WorldVoxel);
}

bool AChunk::IsBlockOpaque(int32 X, int32 Y, int32 Z) const
{
	EBlockType Type = GetBlockWithNeighbors(X, Y, Z);
	if (Type == EBlockType::Air) return false;
	if (!VoxelWorld) return true;
	return VoxelWorld->GetBlockDefinition(Type).bIsOpaque;
}

// ---------------------------------------------------------------------------
//  Greedy meshing
// ---------------------------------------------------------------------------
//
//  For each of the 3 axes (d), and each slice along that axis, a 2D face mask
//  is built.  Runs of identical face values are merged into the largest
//  possible rectangle, emitting one quad per rectangle.
//
//  Face mask encoding:
//    0          → no face
//    > 0        → face with normal pointing in +d direction; value = block type
//    < 0        → face with normal pointing in −d direction; value = −block type
//
//  Axes: d=0→X, d=1→Y, d=2→Z
//        u=(d+1)%3, v=(d+2)%3  (the two axes spanning the quad)
// ---------------------------------------------------------------------------

void AChunk::BuildGreedyMesh(
	TArray<FVector>&       OutVertices,
	TArray<int32>&         OutTriangles,
	TArray<FVector>&       OutNormals,
	TArray<FVector2D>&     OutUVs,
	TArray<FLinearColor>&  OutColors)
{
	const int32 Dims[3] = { CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };

	const bool  bHasWorld = (VoxelWorld != nullptr);
	const int32 AtlasCols = bHasWorld ? FMath::Max(1, VoxelWorld->GetAtlasCols()) : 1;
	const int32 AtlasRows = AtlasCols; // atlas is square (ceil-sqrt packing)
	const float TileU     = 1.f / static_cast<float>(AtlasCols);
	const float TileV     = 1.f / static_cast<float>(AtlasRows);

	// Returns (AtlasU, AtlasV) — the top-left UV of the tile for this face
	// in normalised atlas space (0..1).
	//   AtlasU = (TileIdx % AtlasCols) * TileU
	//   AtlasV = (TileIdx / AtlasCols) * TileV
	auto FaceTileOrigin = [&](EBlockType Type, int32 FaceAxis, int32 FaceSign,
	                          float& OutU, float& OutV)
	{
		FString TexName;
		if (bHasWorld)
		{
			const FBlockDefinition& Def = VoxelWorld->GetBlockDefinition(Type);
			if      (FaceAxis == 2 && FaceSign > 0) TexName = Def.TextureTop;
			else if (FaceAxis == 2 && FaceSign < 0) TexName = Def.TextureBottom;
			else                                    TexName = Def.TextureSide;
			if (TexName.IsEmpty()) TexName = Def.TextureSide;
		}
		const int32 TileIdx = bHasWorld ? VoxelWorld->GetTileIndex(TexName) : 0;
		OutU = static_cast<float>(TileIdx % AtlasCols) * TileU;
		OutV = static_cast<float>(TileIdx / AtlasCols) * TileV;
	};

	// We process all 3 normal axes
	for (int32 d = 0; d < 3; ++d)
	{
		const int32 u = (d + 1) % 3;
		const int32 v = (d + 2) % 3;

		// Mask: Dims[u] wide × Dims[v] tall
		const int32 MaskW = Dims[u];
		const int32 MaskH = Dims[v];
		TArray<int32> Mask;
		Mask.SetNumUninitialized(MaskW * MaskH);

		// Iterate every slice perpendicular to axis d.
		// Range −1..Dims[d]−1 so that boundary faces against neighbors are included.
		for (int32 s = -1; s < Dims[d]; ++s)
		{
			// ----------------------------------------------------------------
			//  Build the 2D face mask for this slice
			// ----------------------------------------------------------------
			for (int32 j = 0; j < MaskH; ++j)
			{
				for (int32 i = 0; i < MaskW; ++i)
				{
					// Convert (d, u, v) indices to (X, Y, Z) for the two blocks
					// straddling the boundary between slice s and s+1.
					int32 PosA[3], PosB[3];
					PosA[d] = s;     PosA[u] = i; PosA[v] = j;
					PosB[d] = s + 1; PosB[u] = i; PosB[v] = j;

					const EBlockType BlockA = GetBlockWithNeighbors(PosA[0], PosA[1], PosA[2]);
					const EBlockType BlockB = GetBlockWithNeighbors(PosB[0], PosB[1], PosB[2]);
					const bool bAOpaque  = IsBlockOpaque(PosA[0], PosA[1], PosA[2]);
					const bool bBOpaque  = IsBlockOpaque(PosB[0], PosB[1], PosB[2]);
					const bool bAVisible = (BlockA != EBlockType::Air);
					const bool bBVisible = (BlockB != EBlockType::Air);

					if (bAOpaque && !bBOpaque)
					{
						// Opaque A, non-opaque B → face with +d normal, owned by block A
						Mask[i + j * MaskW] = static_cast<int32>(BlockA);
					}
					else if (!bAOpaque && bBOpaque)
					{
						// Non-opaque A, opaque B → face with -d normal, owned by block B
						Mask[i + j * MaskW] = -static_cast<int32>(BlockB);
					}
					else if (bAVisible && !bBVisible)
					{
						// Non-opaque solid A (e.g. Water) against Air → face with +d normal
						Mask[i + j * MaskW] = static_cast<int32>(BlockA);
					}
					else if (!bAVisible && bBVisible)
					{
						// Air against non-opaque solid B (e.g. Water) → face with -d normal
						Mask[i + j * MaskW] = -static_cast<int32>(BlockB);
					}
					else
					{
						// Stone/Stone, Air/Air, Water/Water, etc. → no face
						Mask[i + j * MaskW] = 0;
					}
				}
			}

			// ----------------------------------------------------------------
			//  Greedy merge pass: find largest rectangle of identical values
			// ----------------------------------------------------------------
			for (int32 j = 0; j < MaskH; ++j)
			{
				for (int32 i = 0; i < MaskW; )
				{
					const int32 FaceVal = Mask[i + j * MaskW];
					if (FaceVal == 0)
					{
						++i;
						continue;
					}

					// --- Expand width (along u) ---
					int32 w = 1;
					while (i + w < MaskW && Mask[(i + w) + j * MaskW] == FaceVal)
					{
						++w;
					}

					// --- Expand height (along v) ---
					int32 h = 1;
					bool bDone = false;
					while (!bDone && j + h < MaskH)
					{
						for (int32 k = 0; k < w; ++k)
						{
							if (Mask[(i + k) + (j + h) * MaskW] != FaceVal)
							{
								bDone = true;
								break;
							}
						}
						if (!bDone) ++h;
					}

					// --------------------------------------------------------
					//  Emit quad
					//
					//  The face plane lies at d = s+1 (boundary between s and s+1).
					//  Four corners in (d, u, v) space:
					//    V0 = (s+1,  i,   j  )
					//    V1 = (s+1,  i+w, j  )
					//    V2 = (s+1,  i+w, j+h)
					//    V3 = (s+1,  i,   j+h)
					//
					//  Winding order (CCW from outside):
					//    +d normal → 0,1,3  1,2,3
					//    −d normal → 0,3,1  3,2,1
					// --------------------------------------------------------
					const int32 BaseIdx = OutVertices.Num();
					const float FaceD   = static_cast<float>(s + 1) * BLOCK_SIZE;

					auto MakeVtx = [&](int32 uCoord, int32 vCoord) -> FVector
					{
						FVector P(0.f);
						P[d] = FaceD;
						P[u] = static_cast<float>(uCoord) * BLOCK_SIZE;
						P[v] = static_cast<float>(vCoord) * BLOCK_SIZE;
						return P;
					};

					OutVertices.Add(MakeVtx(i,     j    ));  // V0
					OutVertices.Add(MakeVtx(i + w, j    ));  // V1
					OutVertices.Add(MakeVtx(i + w, j + h));  // V2
					OutVertices.Add(MakeVtx(i,     j + h));  // V3

					// Normal
					FVector Normal(0.f);
					Normal[d] = (FaceVal > 0) ? 1.f : -1.f;
					OutNormals.Add(Normal);
					OutNormals.Add(Normal);
					OutNormals.Add(Normal);
					OutNormals.Add(Normal);

// Vertex colors encode atlas tile address and UV info.
				// Brightness is handled by UE5's native lighting system (normal + directional/sky lights).

					// Vertex colour encodes the atlas tile address:
					//   R = atlas tile U origin,  G = atlas tile V origin
					//   B = tile size (1/AtlasCols; atlas is square so same for U and V)
					//   A = 1.0 (full opacity, brightness handled by UE5 lighting)
					// Material formula:  atlasUV = frac(UV0) * VertexColor.b + VertexColor.rg
					//
					// Axis orientation:  for each face axis d, u=(d+1)%3 and v=(d+2)%3.
					//   d=0 (X face): u=Y (horiz), v=Z (vert)  → UV(u,v) correct as-is.
					//   d=1 (Y face): u=Z (vert),  v=X (horiz) → swap so Z→UV.y (upright).
					//   d=2 (Z face): u=X (horiz), v=Y         → correct as-is; +Z flipped V.
					{
						EBlockType BType = static_cast<EBlockType>(FMath::Abs(FaceVal));
						float AU, AV;
						FaceTileOrigin(BType, d, FaceVal, AU, AV);

						if (d == 1)
						{
							// Y-axis face: u=Z (vert), v=X (horiz).
							// Swap axes so UV.x=horiz(X), UV.y=vert(Z).
							// Flip UV.y so texture top (0) maps to world top (Z=i+w).
							OutUVs.Add(FVector2D(0.f,       (float)w ));  // V0: Z=low,  X=left
							OutUVs.Add(FVector2D(0.f,       0.f      ));  // V1: Z=high, X=left
							OutUVs.Add(FVector2D((float)h,  0.f      ));  // V2: Z=high, X=right
							OutUVs.Add(FVector2D((float)h,  (float)w ));  // V3: Z=low,  X=right
						}
						else if (d == 2 && FaceVal > 0)
						{
							// +Z top face: flip V so texture is right-side up from above.
							OutUVs.Add(FVector2D(0.f,      (float)h ));  // V0
							OutUVs.Add(FVector2D((float)w, (float)h ));  // V1
							OutUVs.Add(FVector2D((float)w, 0.f      ));  // V2
							OutUVs.Add(FVector2D(0.f,      0.f      ));  // V3
						}
						else
						{
							// X-axis side faces and -Z bottom face.
							// u=Y (horiz), v=Z (vert). Flip UV.y so texture top (0)
							// maps to world top (Z=j+h), not world bottom.
							OutUVs.Add(FVector2D(0.f,      (float)h ));  // V0: Y=left,  Z=low
							OutUVs.Add(FVector2D((float)w, (float)h ));  // V1: Y=right, Z=low
							OutUVs.Add(FVector2D((float)w, 0.f      ));  // V2: Y=right, Z=high
							OutUVs.Add(FVector2D(0.f,      0.f      ));  // V3: Y=left,  Z=high
						}

					const FLinearColor TileColor(AU, AV, TileU, 1.0f);  // A = 1.0 (full opacity)
						OutColors.Add(TileColor);
						OutColors.Add(TileColor);
						OutColors.Add(TileColor);
						OutColors.Add(TileColor);
					}

					// Triangles
					//  UE5 uses a left-handed (DirectX) coordinate system where
					//  CLOCKWISE winding (viewed from outside) = front face.
					//  With u=(d+1)%3, v=(d+2)%3 the vertices are laid out so that
					//  CW from outside requires:
					//    +d normal → 0→3→1, 3→2→1
					//    −d normal → 0→1→3, 1→2→3
					if (FaceVal > 0)
					{
						// +d normal → CW from outside
						OutTriangles.Add(BaseIdx + 0);
						OutTriangles.Add(BaseIdx + 3);
						OutTriangles.Add(BaseIdx + 1);

						OutTriangles.Add(BaseIdx + 3);
						OutTriangles.Add(BaseIdx + 2);
						OutTriangles.Add(BaseIdx + 1);
					}
					else
					{
						// −d normal → opposite winding (CW from the other side)
						OutTriangles.Add(BaseIdx + 0);
						OutTriangles.Add(BaseIdx + 1);
						OutTriangles.Add(BaseIdx + 3);

						OutTriangles.Add(BaseIdx + 1);
						OutTriangles.Add(BaseIdx + 2);
						OutTriangles.Add(BaseIdx + 3);
					}

					// --------------------------------------------------------
					//  Clear processed mask cells
					// --------------------------------------------------------
					for (int32 jj = j; jj < j + h; ++jj)
					{
						for (int32 ii = i; ii < i + w; ++ii)
						{
							Mask[ii + jj * MaskW] = 0;
						}
					}

					i += w;  // advance past the merged run
				}
			}
		}
	}
}

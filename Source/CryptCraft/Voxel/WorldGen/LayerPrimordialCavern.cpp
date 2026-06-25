// LayerPrimordialCavern.cpp
// Primordial Cavern layer generation (Level 2, GlobalChunkZ -9 .. -16, world Z -257 .. -512).
//
// Block layout (256 blocks = 8 chunks, LocalChunkZ 0 = shallowest):
//   LocalChunkZ 0   : Solid stone ceiling                                ( 32 blocks)
//   LocalChunkZ 1   : Ceiling fringe — Perlin stalactites, up to 32 blocks deep
//   LocalChunkZ 2–3 : Pure open air void                                 ( 64 blocks)
//   LocalChunkZ 4–7 : Land/Water terrain (procedural ocean/continent system)
//
// Ocean/Continent Rework (four-piece system):
//   1. SampleContinentNoise: 3-octave fBm at 1/3000 frequency for large continental scale
//   2. RemapShapeCurve: Spline remap with compressed ocean side, expanded land side
//   3. SampleCoastJitter: High-freq detail (1/40) applied near coastline (±0.08 of threshold)
//   4. SampleIslandMask: Low-freq sparse islands (1/200) in deep ocean, max 12-block bumps
//
// Resulting terrain: Larger continents, organic coastlines, rare sandy islets.

#include "LayerPrimordialCavern.h"
#include "LayerBase.h"
#include "Voxel/Chunk.h"

// Perlin noise utility functions are now in LayerBase.h

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

// 3-octave fBm continent noise — large scale landmass shape, returns approx [-1, 1].
static float SampleContinentNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 50000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 3000.f;  // Large continental scale
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 3; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return Value / Total;  // [-1, 1]
}

// PIECE 2: Ocean side compressed (smaller fraction of input range), land side unchanged.
// Control points: (input, output)
//   (-1.0, 0.02)  — deep ocean abyss (unchanged)
//   (-0.65, 0.04) — shallow ocean (MOVED: was -0.40)
//   (-0.40, 0.15) — mid ocean (MOVED: was -0.20)
//   (-0.20, 0.35) — deep shelf (MOVED: was -0.10)
//   (-0.05, 0.48) — upper shelf (MOVED: was -0.03)
//   ( 0.0,  0.50) — coastline (unchanged)
//   ( 0.03, 0.52) — coastal plain (unchanged)
//   ( 0.12, 0.60) — plains (unchanged)
//   ( 0.30, 0.72) — foothills (unchanged)
//   ( 0.50, 0.85) — mid hills (unchanged)
//   ( 0.75, 0.95) — high hills (unchanged)
//   ( 1.0,  1.00) — mountain peaks (unchanged)
static float RemapShapeCurve(float t)
{
	struct FControlPoint { float In; float Out; };
	static constexpr FControlPoint Points[] = {
		{ -1.00f, 0.02f },
		{ -0.65f, 0.04f },  // Compressed ocean range
		{ -0.40f, 0.15f },
		{ -0.20f, 0.35f },
		{ -0.05f, 0.48f },
		{  0.00f, 0.50f },
		{  0.03f, 0.52f },
		{  0.12f, 0.60f },
		{  0.30f, 0.72f },
		{  0.50f, 0.85f },
		{  0.75f, 0.95f },
		{  1.00f, 1.00f },
	};
	static constexpr int32 NumPoints = UE_ARRAY_COUNT(Points);

	// Clamp to valid range
	if (t <= Points[0].In)          return Points[0].Out;
	if (t >= Points[NumPoints-1].In) return Points[NumPoints-1].Out;

	// Linear search for the surrounding segment
	for (int32 i = 0; i < NumPoints - 1; ++i)
	{
		if (t <= Points[i+1].In)
		{
			const float SegT = (t - Points[i].In) / (Points[i+1].In - Points[i].In);
			return FMath::Lerp(Points[i].Out, Points[i+1].Out, SegT);
		}
	}

	return Points[NumPoints-1].Out;
}

// 2-octave fBm detail noise — high frequency surface roughness applied to terrain base.
// Adds ripple to ocean floor and ground surface. Returns [-1, 1]; contribution is add-only
// (no downward dips). Amplitude varies by zone: 0.02 (ocean) to 0.18 (hills).
static float SampleDetailNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 70000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 40.f;
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 2; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return Value / Total;  // [-1, 1]
}

// PIECE 3: High-frequency noise (2-octave, 1/40 freq) for breaking up coastline with organic detail.
// Applied ONLY near coastline threshold (ShapeValue 0.42–0.58), fading out smoothly.
// Creates jagged, realistic coastlines instead of perfectly smooth bands.
// Independent seed/offset from all other noise functions.
static float SampleCoastJitter(float WX, float WY)
{
	static constexpr float NoiseOffset = 60000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 40.f;  // High frequency for fine coastal variation
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 2; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return Value / Total;  // [-1, 1]
}

// PIECE 4: Low-frequency noise (1-octave, 1/200 freq) for sparse island clusters in deep ocean.
// Applied ONLY where ShapeValue < 0.35 (deep ocean, well below sea level baseline).
// Islands are rare (triggered only when noise > 0.70) and isolated, adding 5–12 blocks
// to ocean floor where triggered, allowing sand/grass logic to create tiny sandy islets.
// Independent seed/offset from all other noise functions.
static float SampleIslandMask(float WX, float WY)
{
	static constexpr float NoiseOffset = 80000.5f;
	static constexpr float Freq = 1.f / 200.f;  // Low frequency for large island clusters
	
	float Value = CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq);
	return Value;  // [-1, 1]
}

// 2-octave fBm vegetation density — large-scale forest vs plains regions.
// Macro scale (1/2000 frequency) creates regions where forests cluster together.
// Returns normalized [0, 1]. Independent seed from all other noise functions.
static float SampleVegetationDensity(float WX, float WY)
{
	static constexpr float NoiseOffset = 90000.5f;
	float Freq = 1.f / 250.f;
	float Value = 0.f;
	float Amp = 1.f;
	float Total = 0.f;
	
	for (int32 Oct = 0; Oct < 4; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp *= 0.5f;
		Freq *= 2.f;
	}
	
	return FMath::Clamp(Value / Total * 0.5f + 0.5f, 0.f, 1.f);
}

// 3-octave fBm landmark rarity — fragments eligible patches into smaller, rarer shapes.
// Low frequency (1/150) with multiple octaves to create isolated peaks.
// Not currently used; reserved for future Volcano/ProtoaxiteField biomes.
// Returns normalized [0, 1]. Independent seed from all other noise functions.
static float SampleLandmarkRarity(float WX, float WY)
{
	static constexpr float NoiseOffset = 85000.5f;
	float Freq = 1.f / 150.f;
	float Value = 0.f;
	float Amp = 1.f;
	float Total = 0.f;
	
	for (int32 Oct = 0; Oct < 3; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp *= 0.5f;
		Freq *= 2.f;
	}
	
	return FMath::Clamp(Value / Total * 0.5f + 0.5f, 0.f, 1.f);
}

// Mountain region mask — gates where mountains exist via smoothstep.
// 1-octave at 1/1200 frequency. Returns smoothed [0, 1] where mountains are present.
// Independent seed from all other noise functions.
static float SampleMountainMask(float WX, float WY, bool bDebugLog = false)
{
	static constexpr float NoiseOffset = 75000.5f;
	float Freq = 1.f / 900.f;
	float Value = CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq);
	
	// Smoothstep gate: mountains transition in as underlying noise increases
	// Increased denominator from 0.5 to 1.0 to desaturate output
	// Now requires raw noise up to ~0.6 to fully saturate (instead of 0.1)
	// This spreads MountainMask output across [0,1] range instead of clustering near 1.0
	float t = FMath::Clamp((Value + 0.4f) / 1.0f, 0.f, 1.f);
	float Smoothstep = t * t * (3.f - 2.f * t);  // Standard smoothstep formula
	
	if (bDebugLog)
	{
		UE_LOG(LogTemp, Warning, TEXT("    MountainMask(X=%.1f Y=%.1f): Raw=%.4f -> Clamp=%.4f -> Smoothstep=%.4f"), 
			WX, WY, Value, t, Smoothstep);
	}
	
	return Smoothstep;
}

// Peak bonus noise — provides variety within mountains, higher octaves for detailed peaks.
// 2-octave at 1/400 frequency for tighter peak features. Returns normalized [0, 1].
// Independent seed from all other noise functions.
static float SamplePeakNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 76000.5f;
	float Freq = 1.f / 400.f;
	float Value = 0.f;
	float Amp = 1.f;
	float Total = 0.f;
	
	for (int32 Oct = 0; Oct < 2; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp *= 0.5f;
		Freq *= 2.f;
	}
	
	return FMath::Clamp(Value / Total * 0.5f + 0.5f, 0.f, 1.f);
}

// Determine biome type for land columns based on shape and vegetation density.
// Only called for confirmed land (ShapeValue > ~0.50); ocean columns skip biome dispatch.
// High-frequency jitter noise for Forest/Plains boundary — creates speckled transitions.
// Independent offset (88000.5) and modest frequency (1/20) for boundary-scale variation.
static float SampleBiomeJitterNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 88000.5f;
	float Freq = 1.f / 20.f;
	float Value = 0.f;
	float Amp = 1.f;
	float Total = 0.f;
	
	for (int32 Oct = 0; Oct < 2; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp *= 0.5f;
		Freq *= 2.f;
	}
	
	return Value / Total;  // Returns [-1, 1]
}

static EPrimordialBiomeType DetermineLandBiome(float ShapeValue, float VegetationDensity, float WX, float WY)
{
	// Mountains require BOTH high elevation AND presence in an actual mountain range footprint
	if (ShapeValue >= 0.78f && SampleMountainMask(WX, WY) > 0.9f)
	{
		return EPrimordialBiomeType::PrimordialMountains;
	}
	
	// Forest vs Plains with jittered boundary
	float VegForThreshold = VegetationDensity;
	
	// Apply jitter only near the 0.55 threshold (within ±0.1)
	float ThresholdDistance = FMath::Abs(VegetationDensity - 0.55f);
	float BiomeJitterInfluence = 1.f - FMath::Clamp(ThresholdDistance / 0.1f, 0.f, 1.f);
	
	if (BiomeJitterInfluence > 0.f)
	{
		float BiomeJitter = SampleBiomeJitterNoise(WX, WY);
		float JitterAmount = BiomeJitter * 0.1f * BiomeJitterInfluence;  // ±0.1 max jitter
		VegForThreshold = FMath::Clamp(VegetationDensity + JitterAmount, 0.f, 1.f);
	}
	
	return (VegForThreshold > 0.55f) ? EPrimordialBiomeType::PrimordialForest : EPrimordialBiomeType::PrimordialPlains;
}

// Base terrain height for plains/forest — existing curve + detail noise logic.
// Takes the base shape and applies detail noise (add-only) to generate final height [32, 95].
static int32 GeneratePlainsForestHeight(float WX, float WY, float ShapeValue)
{
	// Determine detail amplitude based on terrain zone
	float DetailAmplitude = 0.02f;  // Ocean base
	if (ShapeValue > 0.55f && ShapeValue <= 0.65f)
	{
		DetailAmplitude = FMath::Lerp(0.02f, 0.06f, (ShapeValue - 0.55f) / 0.10f);
	}
	else if (ShapeValue > 0.65f)
	{
		DetailAmplitude = FMath::Lerp(0.06f, 0.18f, FMath::Clamp((ShapeValue - 0.65f) / 0.13f, 0.f, 1.f));
	}
	
	// Sample and apply detail noise (add-only contribution)
	float DetailNoise = SampleDetailNoise(WX, WY);
	float DetailContribution = FMath::Max(0.f, DetailNoise * DetailAmplitude);
	
	// Final shape clamped [0, 1]
	float FinalShape = FMath::Clamp(ShapeValue + DetailContribution, 0.f, 1.f);
	
	// Convert to absolute height [32, 95]
	return 32 + FMath::RoundToInt(FinalShape * 63.f);
}

// Mountain height boost — added on top of base plains/forest height.
// Returns the boost amount (not absolute height); caller adds to baseline and clamps to [32, 160].
static int32 GenerateMountainHeight(float WX, float WY, float ShapeValue)
{
	float MountainMask = SampleMountainMask(WX, WY);  // [0, 1] smoothed gate
	float PeakNoise = SamplePeakNoise(WX, WY);        // [0, 1] peak variety
	
	// Mountain boost = base valley floor lift + peak bonus scaled by mask and peak noise
	constexpr float BaseLift = 50.f;   // Valley floor boost (blocks above sea level baseline)
	constexpr float PeakBonus = 50.f;  // Max additional at peaks
	
	float HeightBoost = BaseLift + (PeakBonus * MountainMask * PeakNoise);
	
	return FMath::RoundToInt(HeightBoost);
}

// ---------------------------------------------------------------------------
//  Public Utility Functions for Biome Queries
// ---------------------------------------------------------------------------

// Now that all helper functions are defined, implement the public query functions

EPrimordialBiomeType FPrimordialCavernLevelGenerator::QueryBiomeAtWorldPosition(float WorldX, float WorldY)
{
	// Run through the same pipeline as terrain generation
	float ContinentNoise = SampleContinentNoise(WorldX, WorldY);
	float ShapeValue = RemapShapeCurve(ContinentNoise);
	
	// Apply coast jitter
	if (ShapeValue > 0.46f && ShapeValue < 0.54f)
	{
		const float CoastDistance = FMath::Abs(ShapeValue - 0.50f) / 0.04f;
		const float QuadraticFade = 1.0f - (CoastDistance * CoastDistance);
		const float CoastJitter = SampleCoastJitter(WorldX, WorldY);
		
		ShapeValue += CoastJitter * 0.12f * QuadraticFade;
		ShapeValue = FMath::Clamp(ShapeValue, 0.0f, 1.0f);
	}
	
	// Ocean check
	if (ShapeValue <= 0.50f)
	{
		return EPrimordialBiomeType::Ocean;
	}
	
	// Land: classify based on vegetation and shape
	float VegetationDensity = SampleVegetationDensity(WorldX, WorldY);
	return DetermineLandBiome(ShapeValue, VegetationDensity, WorldX, WorldY);
}

FString FPrimordialCavernLevelGenerator::GetBiomeDisplayName(EPrimordialBiomeType Biome)
{
	switch (Biome)
	{
		case EPrimordialBiomeType::PrimordialPlains:     return TEXT("Plains");
		case EPrimordialBiomeType::PrimordialForest:     return TEXT("Forest");
		case EPrimordialBiomeType::PrimordialMountains:  return TEXT("Mountains");
		case EPrimordialBiomeType::Ocean:                return TEXT("Ocean");
		default:                                         return TEXT("Unknown");
	}
}

FPrimordialCavernLevelGenerator::FTerrainDebugInfo FPrimordialCavernLevelGenerator::QueryTerrainDebugInfo(float BlockX, float BlockY)
{
	FTerrainDebugInfo Info;
	
	// Sample continent noise
	Info.ContinentNoise = SampleContinentNoise(BlockX, BlockY);
	Info.ShapeValue = RemapShapeCurve(Info.ContinentNoise);
	
	// Apply coast jitter
	if (Info.ShapeValue > 0.46f && Info.ShapeValue < 0.54f)
	{
		const float CoastDistance = FMath::Abs(Info.ShapeValue - 0.50f) / 0.04f;
		const float QuadraticFade = 1.0f - (CoastDistance * CoastDistance);
		const float CoastJitter = SampleCoastJitter(BlockX, BlockY);
		
		Info.ShapeValue += CoastJitter * 0.12f * QuadraticFade;
		Info.ShapeValue = FMath::Clamp(Info.ShapeValue, 0.0f, 1.0f);
	}
	
	// Determine biome and estimate height
	if (Info.ShapeValue <= 0.50f)
	{
		Info.Biome = EPrimordialBiomeType::Ocean;
		float DetailNoise = SampleDetailNoise(BlockX, BlockY);
		float DetailContribution = FMath::Max(0.f, DetailNoise * 0.02f);
		float OceanShape = FMath::Clamp(Info.ShapeValue + DetailContribution, 0.f, 1.f);
		Info.EstimatedHeight = 32 + FMath::RoundToInt(OceanShape * 31.f);
	}
	else
	{
		float VegetationDensity = SampleVegetationDensity(BlockX, BlockY);
		Info.Biome = DetermineLandBiome(Info.ShapeValue, VegetationDensity, BlockX, BlockY);
		
		// Estimate base height
		int32 BaseHeight = GeneratePlainsForestHeight(BlockX, BlockY, Info.ShapeValue);
		
		if (Info.Biome == EPrimordialBiomeType::PrimordialMountains)
		{
			Info.MountainMask = SampleMountainMask(BlockX, BlockY);
			Info.PeakNoise = SamplePeakNoise(BlockX, BlockY);
			int32 MountainBoost = GenerateMountainHeight(BlockX, BlockY, Info.ShapeValue);
			Info.HeightBoost = static_cast<float>(MountainBoost);
			float t = FMath::Clamp((Info.ShapeValue - 0.74f) / 0.08f, 0.f, 1.f);
			float BlendFactor = t * t * (3.f - 2.f * t);
			float AdjustedBoost = FMath::Lerp(0.f, static_cast<float>(MountainBoost), BlendFactor);
			Info.EstimatedHeight = FMath::Clamp(BaseHeight + FMath::RoundToInt(AdjustedBoost), 32, 160);
		}
		else
		{
			Info.EstimatedHeight = BaseHeight;
		}
	}
	
	Info.BiomeName = GetBiomeDisplayName(Info.Biome);
	return Info;
}

void FPrimordialCavernLevelGenerator::GridScanForPeaks(float CenterBlockX, float CenterBlockY, float GridSizeBlocks, float StepBlocks)
{
	UE_LOG(LogTemp, Warning, TEXT("===== GRID SCAN FOR MOUNTAIN PEAKS ====="));
	UE_LOG(LogTemp, Warning, TEXT("Center: X=%.0f Y=%.0f | Grid Size: %.0f | Step: %.0f"), 
		CenterBlockX, CenterBlockY, GridSizeBlocks, StepBlocks);
	
	float HalfSize = GridSizeBlocks * 0.5f;
	float PeakShapeValue = -1.f;
	float PeakMountainMask = 0.f;
	float PeakBlockX = 0.f;
	float PeakBlockY = 0.f;
	EPrimordialBiomeType PeakBiome = EPrimordialBiomeType::Ocean;
	
	int32 SamplesAnalyzed = 0;
	int32 LandSamples = 0;
	int32 MountainSamples = 0;
	
	// Track ShapeValue bands and MountainMask pass rates
	int32 Band_060_065 = 0, Band_060_065_Pass = 0;  // [0.60, 0.65)
	int32 Band_065_070 = 0, Band_065_070_Pass = 0;  // [0.65, 0.70)
	int32 Band_070_075 = 0, Band_070_075_Pass = 0;  // [0.70, 0.75)
	int32 Band_075_078 = 0, Band_075_078_Pass = 0;  // [0.75, 0.78)
	int32 Band_078_Plus = 0, Band_078_Plus_Pass = 0; // [0.78, +∞)
	int32 Band_060_Plus = 0, Band_060_Plus_Pass = 0; // [0.60, +∞) aggregate
	
	// Track MountainMask candidate thresholds for ShapeValue >= 0.78 samples
	int32 Band_078_Total = 0;
	int32 Threshold_050 = 0, Threshold_060 = 0, Threshold_070 = 0, Threshold_075 = 0;
	int32 Threshold_080 = 0, Threshold_085 = 0, Threshold_090 = 0;
	
	// Grid scan across area
	for (float ScanX = CenterBlockX - HalfSize; ScanX <= CenterBlockX + HalfSize; ScanX += StepBlocks)
	{
		for (float ScanY = CenterBlockY - HalfSize; ScanY <= CenterBlockY + HalfSize; ScanY += StepBlocks)
		{
			FTerrainDebugInfo DebugInfo = QueryTerrainDebugInfo(ScanX, ScanY);
			SamplesAnalyzed++;
			
			// For all samples at higher elevations, sample MountainMask directly (not biome-dependent)
			float DirectMountainMask = SampleMountainMask(ScanX, ScanY);
			
			if (DebugInfo.ShapeValue > 0.50f)
			{
				LandSamples++;
				if (DebugInfo.Biome == EPrimordialBiomeType::PrimordialMountains)
				{
					MountainSamples++;
				}
			}
			
			// Track ShapeValue bands and MountainMask pass rates
			if (DebugInfo.ShapeValue >= 0.60f)
			{
				Band_060_Plus++;
				
				// Use direct MountainMask sample to get accurate threshold distribution
				if (DirectMountainMask > 0.9f)
				{
					Band_060_Plus_Pass++;
				}
				
				// Categorize into sub-bands
				if (DebugInfo.ShapeValue < 0.65f)
				{
					Band_060_065++;
					if (DirectMountainMask > 0.9f) Band_060_065_Pass++;
				}
				else if (DebugInfo.ShapeValue < 0.70f)
				{
					Band_065_070++;
					if (DirectMountainMask > 0.9f) Band_065_070_Pass++;
				}
				else if (DebugInfo.ShapeValue < 0.75f)
				{
					Band_070_075++;
					if (DirectMountainMask > 0.9f) Band_070_075_Pass++;
				}
				else if (DebugInfo.ShapeValue < 0.78f)
				{
					Band_075_078++;
					if (DirectMountainMask > 0.9f) Band_075_078_Pass++;
				}
				else
				{
					Band_078_Plus++;
					if (DirectMountainMask > 0.9f) Band_078_Plus_Pass++;
					
					// Track candidate thresholds for ShapeValue >= 0.78 samples
					// Using direct MountainMask sample, not biome-conditional DebugInfo.MountainMask
					Band_078_Total++;
					if (DirectMountainMask > 0.50f) Threshold_050++;
					if (DirectMountainMask > 0.60f) Threshold_060++;
					if (DirectMountainMask > 0.70f) Threshold_070++;
					if (DirectMountainMask > 0.75f) Threshold_075++;
					if (DirectMountainMask > 0.80f) Threshold_080++;
					if (DirectMountainMask > 0.85f) Threshold_085++;
					if (DirectMountainMask > 0.90f) Threshold_090++;
				}
			}
			
			// Track highest ShapeValue (peaks)
			// Use direct MountainMask to capture the actual value regardless of biome classification
			if (DebugInfo.ShapeValue > PeakShapeValue)
			{
				PeakShapeValue = DebugInfo.ShapeValue;
				PeakMountainMask = DirectMountainMask;
				PeakBlockX = ScanX;
				PeakBlockY = ScanY;
				PeakBiome = DebugInfo.Biome;
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Grid Scan Results:"));
	UE_LOG(LogTemp, Warning, TEXT("  Samples analyzed: %d"), SamplesAnalyzed);
	UE_LOG(LogTemp, Warning, TEXT("  Land samples: %d (%.1f%%)"), LandSamples, (LandSamples * 100.f / SamplesAnalyzed));
	UE_LOG(LogTemp, Warning, TEXT("  Mountain samples: %d (%.1f%%)"), MountainSamples, (MountainSamples * 100.f / SamplesAnalyzed));
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("SHAPEVALUE BAND BREAKDOWN (MountainMask > 0.9 pass rates):"));
	UE_LOG(LogTemp, Warning, TEXT("  [0.60, 0.65): %d samples, %d pass (%.1f%%)"), 
		Band_060_065, Band_060_065_Pass, (Band_060_065 > 0 ? Band_060_065_Pass * 100.f / Band_060_065 : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  [0.65, 0.70): %d samples, %d pass (%.1f%%)"), 
		Band_065_070, Band_065_070_Pass, (Band_065_070 > 0 ? Band_065_070_Pass * 100.f / Band_065_070 : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  [0.70, 0.75): %d samples, %d pass (%.1f%%)"), 
		Band_070_075, Band_070_075_Pass, (Band_070_075 > 0 ? Band_070_075_Pass * 100.f / Band_070_075 : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  [0.75, 0.78): %d samples, %d pass (%.1f%%)"), 
		Band_075_078, Band_075_078_Pass, (Band_075_078 > 0 ? Band_075_078_Pass * 100.f / Band_075_078 : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  [0.78, +∞):  %d samples, %d pass (%.1f%%)"), 
		Band_078_Plus, Band_078_Plus_Pass, (Band_078_Plus > 0 ? Band_078_Plus_Pass * 100.f / Band_078_Plus : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  ─────────────────────────────────────────────────────────"));
	UE_LOG(LogTemp, Warning, TEXT("  [0.60, +∞):  %d samples, %d pass (%.1f%% overall)"), 
		Band_060_Plus, Band_060_Plus_Pass, (Band_060_Plus > 0 ? Band_060_Plus_Pass * 100.f / Band_060_Plus : 0.f));
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("MOUNTAINMASK THRESHOLD CANDIDATES (for ShapeValue >= 0.78 only):"));
	UE_LOG(LogTemp, Warning, TEXT("  Total [0.78, +∞) samples: %d"), Band_078_Total);
	UE_LOG(LogTemp, Warning, TEXT("  MountainMask > 0.50: %d pass (%.1f%%)"), 
		Threshold_050, (Band_078_Total > 0 ? Threshold_050 * 100.f / Band_078_Total : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  MountainMask > 0.60: %d pass (%.1f%%)"), 
		Threshold_060, (Band_078_Total > 0 ? Threshold_060 * 100.f / Band_078_Total : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  MountainMask > 0.70: %d pass (%.1f%%)"), 
		Threshold_070, (Band_078_Total > 0 ? Threshold_070 * 100.f / Band_078_Total : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  MountainMask > 0.75: %d pass (%.1f%%)"), 
		Threshold_075, (Band_078_Total > 0 ? Threshold_075 * 100.f / Band_078_Total : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  MountainMask > 0.80: %d pass (%.1f%%)"), 
		Threshold_080, (Band_078_Total > 0 ? Threshold_080 * 100.f / Band_078_Total : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  MountainMask > 0.85: %d pass (%.1f%%)"), 
		Threshold_085, (Band_078_Total > 0 ? Threshold_085 * 100.f / Band_078_Total : 0.f));
	UE_LOG(LogTemp, Warning, TEXT("  MountainMask > 0.90: %d pass (%.1f%%)"), 
		Threshold_090, (Band_078_Total > 0 ? Threshold_090 * 100.f / Band_078_Total : 0.f));
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("HIGHEST PEAK FOUND:"));
	UE_LOG(LogTemp, Warning, TEXT("  Location: BlockX=%.2f BlockY=%.2f (WorldX=%.2f WorldY=%.2f)"), 
		PeakBlockX, PeakBlockY, PeakBlockX * BLOCK_SIZE, PeakBlockY * BLOCK_SIZE);
	UE_LOG(LogTemp, Warning, TEXT("  ShapeValue: %.4f"), PeakShapeValue);
	UE_LOG(LogTemp, Warning, TEXT("  MountainMask: %.4f"), PeakMountainMask);
	UE_LOG(LogTemp, Warning, TEXT("  Biome: %s"), *GetBiomeDisplayName(PeakBiome));
	
	if (PeakShapeValue >= 0.78f && PeakMountainMask > 0.9f)
	{
		UE_LOG(LogTemp, Warning, TEXT("  ✓ MOUNTAINS ELIGIBLE (ShapeValue >= 0.78 AND MountainMask > 0.9)"));
		UE_LOG(LogTemp, Warning, TEXT("  Teleport command: PossessCharacter X=%.2f Y=%.2f Z=-380"), 
			PeakBlockX * BLOCK_SIZE, PeakBlockY * BLOCK_SIZE);
	}
	else if (PeakShapeValue >= 0.78f && PeakMountainMask <= 0.9f)
	{
		UE_LOG(LogTemp, Warning, TEXT("  ⚠ High elevation but NO range (ShapeValue >= 0.78 but MountainMask=%.4f <= 0.9)"), PeakMountainMask);
	}
	else if (PeakShapeValue > 0.50f)
	{
		UE_LOG(LogTemp, Warning, TEXT("  ✗ Below mountains threshold (%.4f < 0.78)"), PeakShapeValue);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("  ✗ All ocean in scan area"));
	}
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void FPrimordialCavernLevelGenerator::DebugMountainMaskDistribution(float CenterBlockX, float CenterBlockY, float GridSizeBlocks)
{
	UE_LOG(LogTemp, Warning, TEXT("===== MOUNTAINMASK RAW-TO-SMOOTHSTEP DISTRIBUTION ====="));
	UE_LOG(LogTemp, Warning, TEXT("Center: X=%.0f Y=%.0f | Grid Size: %.0f"), CenterBlockX, CenterBlockY, GridSizeBlocks);
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("Sampling 20 points scattered across the grid:"));
	
	float HalfSize = GridSizeBlocks * 0.5f;
	
	// Sample 20 scattered points
	for (int32 i = 0; i < 20; ++i)
	{
		// Spread evenly across grid (roughly 4x5 grid = 20 points)
		float GridX = (i % 5) * (GridSizeBlocks / 5.f) - HalfSize;
		float GridY = (i / 5) * (GridSizeBlocks / 4.f) - HalfSize;
		
		float SampleX = CenterBlockX + GridX;
		float SampleY = CenterBlockY + GridY;
		
		// Call SampleMountainMask with debug flag enabled
		float MountainMask = SampleMountainMask(SampleX, SampleY, true);
		
		// Also log biome at this location for context
		EPrimordialBiomeType Biome = QueryBiomeAtWorldPosition(SampleX, SampleY);
		UE_LOG(LogTemp, Warning, TEXT("  [%d] Biome: %s -> FinalMask: %.4f"), 
			i, *GetBiomeDisplayName(Biome), MountainMask);
	}
	
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========== END MOUNTAINMASK DEBUG =========="));
}

// ---------------------------------------------------------------------------
//  Zone indices  (LocalChunkZ within the Primordial Cavern layer, 0 = shallowest)
// ---------------------------------------------------------------------------

// CEILING_SOLID_Z and CEILING_FRINGE_Z are now in LayerBase.h

static constexpr int32 TERRAIN_START_Z     = 5;   // 3 chunks of terrain (LocalChunkZ 5-7)
static constexpr int32 TERRAIN_END_Z       = 7;

// ---------------------------------------------------------------------------
//  GenerateChunk
// ---------------------------------------------------------------------------

void FPrimordialCavernLevelGenerator::GenerateBlocks(
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ,
	TArray<EBlockType>& OutBlocks)
{

	// ---- Solid stone ceiling ----------------------------------------
	if (LocalChunkZ == CEILING_SOLID_Z)
	{
		OutBlocks.Init(EBlockType::Stone, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
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
		return;
	}

	// ---- Terrain section (LocalChunkZ 5-7) - Ocean/Continent with Biome Dispatcher ----
	// Four-piece base system: continental noise (1/3000), spline remap (compressed ocean),
	// coast jitter (1/40, near threshold only), island bumps (1/200, deep ocean only).
	// Biome dispatch: land columns classified as Plains/Forest/Mountains, each with own height generation.
	// Ocean columns bypass biome dispatch, use existing depth logic (unchanged).
	if (LocalChunkZ >= TERRAIN_START_Z && LocalChunkZ <= TERRAIN_END_Z)
	{
		for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
		{
			for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
			{
				// Calculate world position
				const float WX = static_cast<float>(GlobalChunkX * CHUNK_SIZE_X + X);
				const float WY = static_cast<float>(GlobalChunkY * CHUNK_SIZE_Y + Y);
				
				// ─────────────────────────────────────────────
				// 1. Sample continent shape and apply coast jitter
				// ─────────────────────────────────────────────
				float ContinentNoise = SampleContinentNoise(WX, WY);
				float ShapeValue = RemapShapeCurve(ContinentNoise);
				
				// Apply coast jitter — breaks up coastline with organic detail
				// Only applied in ±0.08 band around coastline (ShapeValue 0.50)
				if (ShapeValue > 0.42f && ShapeValue < 0.58f)
				{
					const float CoastDistance = FMath::Abs(ShapeValue - 0.50f) / 0.08f;  // [0, 1]
					const float QuadraticFade = 1.0f - (CoastDistance * CoastDistance);
					const float CoastJitter = SampleCoastJitter(WX, WY);
					
					ShapeValue += CoastJitter * 0.12f * QuadraticFade;
					ShapeValue = FMath::Clamp(ShapeValue, 0.0f, 1.0f);
				}
				
				// ─────────────────────────────────────────────
				// 2. Determine terrain height based on biome
				// ─────────────────────────────────────────────
				int32 GroundHeight = 32;
				const int32 SEA_LEVEL = 64;
				EPrimordialBiomeType Biome = EPrimordialBiomeType::PrimordialPlains;  // Default to Plains for ocean
				
				if (ShapeValue > 0.50f)  // LAND COLUMN
				{
					// Sample vegetation density for biome classification
					float VegetationDensity = SampleVegetationDensity(WX, WY);
					Biome = DetermineLandBiome(ShapeValue, VegetationDensity, WX, WY);
					
					// Get base height from plains/forest system
					int32 BaseHeight = GeneratePlainsForestHeight(WX, WY, ShapeValue);
					
					if (Biome == EPrimordialBiomeType::PrimordialMountains)
					{
						// Generate mountain boost
						int32 MountainBoost = GenerateMountainHeight(WX, WY, ShapeValue);
						
						// Blend at boundary (0.74–0.82) to smooth transition from plains to mountains
						float t = FMath::Clamp((ShapeValue - 0.74f) / 0.08f, 0.f, 1.f);
						float BlendFactor = t * t * (3.f - 2.f * t);  // Smoothstep blend
						float AdjustedBoost = FMath::Lerp(0.f, static_cast<float>(MountainBoost), BlendFactor);
						
						// Add boost to baseline and clamp to [32, 160]
						GroundHeight = FMath::Clamp(BaseHeight + FMath::RoundToInt(AdjustedBoost), 32, 160);
					}
					else  // Plains or Forest
					{
						GroundHeight = BaseHeight;
					}
				}
				else  // OCEAN COLUMN (ShapeValue ≤ 0.50)
				{
					// ─────────────────────────────────────────────
					// Ocean: existing logic (island mask + depths)
					// Completely unchanged from four-piece system
					// ─────────────────────────────────────────────
					
					// Base ocean floor depth map
					float DetailNoise = SampleDetailNoise(WX, WY);
					float DetailContribution = FMath::Max(0.f, DetailNoise * 0.02f);
					float OceanShape = FMath::Clamp(ShapeValue + DetailContribution, 0.f, 1.f);
					GroundHeight = 32 + FMath::RoundToInt(OceanShape * 31.f);  // [32, 63]
					
					// Apply island mask (rare sandy islets)
					if (ShapeValue < 0.35f)
					{
						float IslandMask = SampleIslandMask(WX, WY);
						if (IslandMask > 0.70f)
						{
							float IslandIntensity = FMath::Clamp((IslandMask - 0.70f) / 0.30f, 0.f, 1.f);
							int32 IslandBump = FMath::RoundToInt(IslandIntensity * 12.f);
							GroundHeight = FMath::Clamp(GroundHeight + IslandBump, 32, 95);
						}
					}
				}
				
				// ─────────────────────────────────────────────
				// 3. Surface block selection
				// ─────────────────────────────────────────────
				EBlockType SurfaceBlock = EBlockType::Air;
				const float DetailNoise = SampleDetailNoise(WX, WY);
				
				if (GroundHeight > SEA_LEVEL)  // DRY LAND
				{
					// Stone at peaks (safeguard against visual cliffs)
					if (GroundHeight > 85)
					{
						SurfaceBlock = EBlockType::Stone;
					}
					else
					{
						// Probabilistic sand/grass based on coastal closeness
						float CoastalCloseness = 1.f - FMath::Clamp(FMath::Abs(ShapeValue - 0.50f) / 0.05f, 0.f, 1.f);
						float JitteredCloseness = FMath::Clamp(CoastalCloseness + DetailNoise * 0.25f, 0.f, 1.f);
						
						float BlockRandom = CavePerlin2D(WX * 0.15f, WY * 0.15f) * 0.5f + 0.5f;
						
						// DEBUG: Temporary forest biome visualization (use Dirt for forests, Grass for plains)
						if (BlockRandom < JitteredCloseness)
						{
							SurfaceBlock = EBlockType::Sand;
						}
						else
						{
							SurfaceBlock = (Biome == EPrimordialBiomeType::PrimordialForest) ? EBlockType::Dirt : EBlockType::Grass;
						}
					}
				}
				else  // UNDERWATER
				{
					// Depth-based sand/gravel gradient
					float DepthFraction = FMath::Clamp(static_cast<float>(SEA_LEVEL - GroundHeight) / (SEA_LEVEL - 32.f), 0.f, 1.f);
					float JitteredFraction = FMath::Clamp(DepthFraction + DetailNoise * 0.15f, 0.f, 1.f);
					
					SurfaceBlock = (JitteredFraction > 0.5f) ? EBlockType::Gravel : EBlockType::Sand;
				}
				
				// ─────────────────────────────────────────────
				// 4. Fill column with blocks
				// ─────────────────────────────────────────────
				const int32 ChunkBaseZ = (TERRAIN_END_Z - LocalChunkZ) * CHUNK_SIZE_Z;
				
				for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
				{
					int32 AbsoluteZ = ChunkBaseZ + Z;
					EBlockType BlockType = EBlockType::Air;
					
					if (AbsoluteZ < GroundHeight)
					{
						BlockType = EBlockType::Stone;
					}
					else if (AbsoluteZ == GroundHeight)
					{
						BlockType = SurfaceBlock;
					}
					else if (AbsoluteZ > GroundHeight && AbsoluteZ <= SEA_LEVEL)
					{
						BlockType = EBlockType::Water;
					}
					
					OutBlocks[BlockIdx(X, Y, Z)] = BlockType;
				}
			}
		}

		return;
	}

	// Fallback (shouldn't happen): fill with air as safe default
	OutBlocks.Init(EBlockType::Air, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
}

void FPrimordialCavernLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	TArray<EBlockType> Blocks;
	GenerateBlocks(GlobalChunkX, GlobalChunkY, LocalChunkZ, Blocks);
	Chunk.Initialize(Blocks);
}

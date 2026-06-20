// CaveNoiseUtilities.h
// Seeded deterministic noise utilities for crystal cavern generation.
//
// All functions are pure functions of (Seed, Input) — identical inputs always
// produce identical outputs, and the results are deterministic and repeatable
// across platforms.
//
// Design:
//  - HashCellSeed() combines WorldSeed + CellCoord into a unique deterministic seed
//  - SeededNoise1D/2D/3D() use Perlin-based evaluation with seed-dependent offsets
//  - All functions return values in [0, 1] for noise functions
//
// Usage:
//  1. For a spatial region (e.g. cavern placement grid), use HashCellSeed to get
//     a stable seed for that region based on its coordinates
//  2. Use SeededNoise1D/2D/3D on that seed to generate smooth fields
//
// Testing:
//  The test functions (SeededNoiseTest_xxx) verify determinism and smoothness.

#pragma once

#include "CoreMinimal.h"

namespace CaveNoise
{
	// -----------------------------------------------------------------------
	//  Deterministic Hash: Seed Generation from Coordinates
	// -----------------------------------------------------------------------

	/**
	 * Combines a world seed and a coarse grid cell coordinate into a unique,
	 * deterministic seed value. This allows any chunk to independently ask
	 * "what seed should I use for cell (X,Y,Z)?" without needing neighbors
	 * to be computed first.
	 *
	 * @param WorldSeed        The base world seed (same for all cells in a world)
	 * @param CellCoord        A coarse grid coordinate (e.g., cavern cell position)
	 * @return                 A deterministic 32-bit seed for this cell
	 */
	FORCEINLINE uint32 HashCellSeed(uint32 WorldSeed, FIntVector CellCoord)
	{
		// Combine world seed with cell coordinates using FNV-1a-inspired mixing
		uint32 H = WorldSeed;
		H = (H ^ (uint32)(CellCoord.X * 2654435761u)) * 16777619u;
		H = (H ^ (uint32)(CellCoord.Y *  805459861u)) * 16777619u;
		H = (H ^ (uint32)(CellCoord.Z * 1234567891u)) * 16777619u;

		// Final mixing to improve distribution
		H ^= H >> 16;
		H *= 0x45d9f3bu;
		H ^= H >> 16;
		return H;
	}

	// -----------------------------------------------------------------------
	//  Seeded Noise Functions (deterministic, smooth)
	// -----------------------------------------------------------------------

	/**
	 * Deterministic 1D Perlin noise.
	 * Returns a value in [0, 1]. Same (Seed, X) always produces the same result.
	 * Smooth variation: nearby X values produce smoothly interpolated results.
	 *
	 * Use for: worm wander paths, 1D bias fields along tunnel paths
	 */
	float SeededNoise1D(uint32 Seed, float X);

	/**
	 * Deterministic 2D Perlin noise.
	 * Returns a value in [0, 1]. Same (Seed, X, Y) always produces the same result.
	 * Smooth variation: nearby (X, Y) produce smoothly interpolated results.
	 *
	 * Use for: cavern placement layout, biome region assignment
	 */
	float SeededNoise2D(uint32 Seed, float X, float Y);

	/**
	 * Deterministic 3D Perlin noise.
	 * Returns a value in [0, 1]. Same (Seed, Pos) always produces the same result.
	 * Smooth variation: nearby positions produce smoothly interpolated results.
	 *
	 * Use for: cavern interior density carving, tunnel interior detail
	 */
	float SeededNoise3D(uint32 Seed, FVector Pos);

	// -----------------------------------------------------------------------
	//  Testing / Verification
	// -----------------------------------------------------------------------

	/**
	 * Run determinism tests for all noise functions.
	 * Logs pass/fail status. Call once at startup if you want to verify
	 * the noise implementation is working correctly.
	 *
	 * Tests:
	 * - Same input produces same output (run twice, compare)
	 * - Smoothness: nearby inputs produce nearby outputs (sample 1D/2D/3D fields)
	 */
	void RunNoiseTests();
}

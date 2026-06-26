// CaveNoiseUtilities.cpp
// Implementation of seeded deterministic noise functions.

#include "CaveNoiseUtilities.h"
#include "Containers/List.h"

namespace CaveNoise
{
	// -----------------------------------------------------------------------
	//  Helper: Deterministic Gradient Generation
	// -----------------------------------------------------------------------

	// Given a seed and integer grid cell, produce a deterministic gradient value.
	// Returns a value in [-1, 1] that represents the gradient at this grid point.
	static float GetGradient1D(uint32 Seed, int32 GX)
	{
		uint32 H = Seed;
		H = (H ^ (uint32)(GX * 73856093u));
		H ^= H >> 16;
		H *= 0x45d9f3bu;
		H ^= H >> 16;

		return (H & 1) ? 1.0f : -1.0f;
	}

	static FVector2D GetGradient2D(uint32 Seed, int32 GX, int32 GY)
	{
		uint32 H = Seed;
		H = (H ^ (uint32)(GX * 73856093u)) ^ (uint32)(GY * 19349663u);
		H ^= H >> 16;
		H *= 0x45d9f3bu;
		H ^= H >> 16;

		// 8 possible directions
		const int32 Direction = H % 8;
		static const FVector2D Gradients[8] = {
			FVector2D(1, 1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(-1, -1),
			FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1)
		};
		return Gradients[Direction];
	}

	static FVector GetGradient3D(uint32 Seed, int32 GX, int32 GY, int32 GZ)
	{
		uint32 H = Seed;
		H = (H ^ (uint32)(GX * 73856093u)) ^ (uint32)(GY * 19349663u) ^ (uint32)(GZ * 83492791u);
		H ^= H >> 16;
		H *= 0x45d9f3bu;
		H ^= H >> 16;

		// 12 possible directions
		const int32 Direction = H % 12;
		static const FVector Gradients[12] = {
			FVector(1, 1, 0), FVector(-1, 1, 0), FVector(1, -1, 0), FVector(-1, -1, 0),
			FVector(1, 0, 1), FVector(-1, 0, 1), FVector(1, 0, -1), FVector(-1, 0, -1),
			FVector(0, 1, 1), FVector(0, -1, 1), FVector(0, 1, -1), FVector(0, -1, -1)
		};
		return Gradients[Direction];
	}

	// -----------------------------------------------------------------------
	//  Helper: Perlin Fade Curve (Hermite: 6t^5 - 15t^4 + 10t^3)
	// -----------------------------------------------------------------------

	static FORCEINLINE float Fade(float T)
	{
		return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
	}

	// -----------------------------------------------------------------------
	//  Internal: Single-Octave 1D Perlin Noise (raw output, -1 to 1)
	// -----------------------------------------------------------------------

	static float SingleOctaveNoise1D(uint32 Seed, float X)
	{
		const int32 IX = FMath::FloorToInt(X);
		const float FX = X - FMath::FloorToInt(X);
		const float U = Fade(FX);

		// Gradients at integer boundaries
		const float G0 = GetGradient1D(Seed, IX);
		const float G1 = GetGradient1D(Seed, IX + 1);

		// Dot products: gradient * distance
		const float Dot0 = G0 * FX;
		const float Dot1 = G1 * (FX - 1.0f);

		// Interpolate
		return FMath::Lerp(Dot0, Dot1, U);
	}

	// -----------------------------------------------------------------------
	//  1D Perlin Noise with Fractal Octaves (fBm)
	// -----------------------------------------------------------------------

	float SeededNoise1D(uint32 Seed, float X)
	{
		float Total = 0.0f;
		float Amplitude = 1.0f;
		float Frequency = 1.0f;

		constexpr int32 Octaves = 4;
		constexpr float Persistence = 0.5f;
		constexpr float Lacunarity = 2.0f;

		for (int32 Oct = 0; Oct < Octaves; ++Oct)
		{
			Total += SingleOctaveNoise1D(Seed + Oct * 101u, X * Frequency) * Amplitude;
			Amplitude *= Persistence;
			Frequency *= Lacunarity;
		}

		// Raw octave sum is in roughly [-2.0, 2.0] range due to geometry of Perlin gradients
		// Scale to [-1, 1] then to [0, 1]
		return FMath::Clamp(Total * 0.25f + 0.5f, 0.0f, 1.0f);
	}

	// -----------------------------------------------------------------------
	//  Internal: Single-Octave 2D Perlin Noise (raw output, -1 to 1)
	// -----------------------------------------------------------------------

	static float SingleOctaveNoise2D(uint32 Seed, float X, float Y)
	{
		const int32 IX = FMath::FloorToInt(X);
		const int32 IY = FMath::FloorToInt(Y);
		const float FX = X - FMath::FloorToInt(X);
		const float FY = Y - FMath::FloorToInt(Y);

		const float UX = Fade(FX);
		const float UY = Fade(FY);

		// Four corner gradients
		const FVector2D G00 = GetGradient2D(Seed, IX, IY);
		const FVector2D G10 = GetGradient2D(Seed, IX + 1, IY);
		const FVector2D G01 = GetGradient2D(Seed, IX, IY + 1);
		const FVector2D G11 = GetGradient2D(Seed, IX + 1, IY + 1);

		// Dot products with distance vectors
		const float D00 = FVector2D(FX, FY).Dot(G00);
		const float D10 = FVector2D(FX - 1.0f, FY).Dot(G10);
		const float D01 = FVector2D(FX, FY - 1.0f).Dot(G01);
		const float D11 = FVector2D(FX - 1.0f, FY - 1.0f).Dot(G11);

		// Bilinear interpolation
		const float LX0 = FMath::Lerp(D00, D10, UX);
		const float LX1 = FMath::Lerp(D01, D11, UX);
		return FMath::Lerp(LX0, LX1, UY);
	}

	// -----------------------------------------------------------------------
	//  2D Perlin Noise with Fractal Octaves (fBm)
	// -----------------------------------------------------------------------

	float SeededNoise2D(uint32 Seed, float X, float Y)
	{
		float Total = 0.0f;
		float Amplitude = 1.0f;
		float Frequency = 1.0f;

		constexpr int32 Octaves = 4;
		constexpr float Persistence = 0.5f;
		constexpr float Lacunarity = 2.0f;

		for (int32 Oct = 0; Oct < Octaves; ++Oct)
		{
			Total += SingleOctaveNoise2D(Seed + Oct * 101u, X * Frequency, Y * Frequency) * Amplitude;
			Amplitude *= Persistence;
			Frequency *= Lacunarity;
		}

		// Raw octave sum is in roughly [-2.0, 2.0] range due to geometry of Perlin gradients
		// Scale to [-1, 1] then to [0, 1]
		return FMath::Clamp(Total * 0.25f + 0.5f, 0.0f, 1.0f);
	}

	// -----------------------------------------------------------------------
	//  Internal: Single-Octave 3D Perlin Noise (raw output, -1 to 1)
	// -----------------------------------------------------------------------

	static float SingleOctaveNoise3D(uint32 Seed, FVector Pos)
	{
		const int32 IX = FMath::FloorToInt(Pos.X);
		const int32 IY = FMath::FloorToInt(Pos.Y);
		const int32 IZ = FMath::FloorToInt(Pos.Z);

		const float FX = Pos.X - FMath::FloorToInt(Pos.X);
		const float FY = Pos.Y - FMath::FloorToInt(Pos.Y);
		const float FZ = Pos.Z - FMath::FloorToInt(Pos.Z);

		const float UX = Fade(FX);
		const float UY = Fade(FY);
		const float UZ = Fade(FZ);

		// Eight corner gradients
		const FVector G000 = GetGradient3D(Seed, IX, IY, IZ);
		const FVector G100 = GetGradient3D(Seed, IX + 1, IY, IZ);
		const FVector G010 = GetGradient3D(Seed, IX, IY + 1, IZ);
		const FVector G110 = GetGradient3D(Seed, IX + 1, IY + 1, IZ);
		const FVector G001 = GetGradient3D(Seed, IX, IY, IZ + 1);
		const FVector G101 = GetGradient3D(Seed, IX + 1, IY, IZ + 1);
		const FVector G011 = GetGradient3D(Seed, IX, IY + 1, IZ + 1);
		const FVector G111 = GetGradient3D(Seed, IX + 1, IY + 1, IZ + 1);

		// Dot products
		const float D000 = FVector(FX, FY, FZ).Dot(G000);
		const float D100 = FVector(FX - 1.0f, FY, FZ).Dot(G100);
		const float D010 = FVector(FX, FY - 1.0f, FZ).Dot(G010);
		const float D110 = FVector(FX - 1.0f, FY - 1.0f, FZ).Dot(G110);
		const float D001 = FVector(FX, FY, FZ - 1.0f).Dot(G001);
		const float D101 = FVector(FX - 1.0f, FY, FZ - 1.0f).Dot(G101);
		const float D011 = FVector(FX, FY - 1.0f, FZ - 1.0f).Dot(G011);
		const float D111 = FVector(FX - 1.0f, FY - 1.0f, FZ - 1.0f).Dot(G111);

		// Trilinear interpolation
		const float LX00 = FMath::Lerp(D000, D100, UX);
		const float LX10 = FMath::Lerp(D010, D110, UX);
		const float LX01 = FMath::Lerp(D001, D101, UX);
		const float LX11 = FMath::Lerp(D011, D111, UX);

		const float LY0 = FMath::Lerp(LX00, LX10, UY);
		const float LY1 = FMath::Lerp(LX01, LX11, UY);

		return FMath::Lerp(LY0, LY1, UZ);
	}

	// -----------------------------------------------------------------------
	//  3D Perlin Noise with Fractal Octaves (fBm)
	// -----------------------------------------------------------------------

	float SeededNoise3D(uint32 Seed, FVector Pos)
	{
		float Total = 0.0f;
		float Amplitude = 1.0f;
		float Frequency = 1.0f;

		constexpr int32 Octaves = 4;
		constexpr float Persistence = 0.5f;
		constexpr float Lacunarity = 2.0f;

		for (int32 Oct = 0; Oct < Octaves; ++Oct)
		{
			Total += SingleOctaveNoise3D(Seed + Oct * 101u, Pos * Frequency) * Amplitude;
			Amplitude *= Persistence;
			Frequency *= Lacunarity;
		}

		// Raw octave sum is in roughly [-2.0, 2.0] range due to geometry of Perlin gradients
		// Scale to [-1, 1] then to [0, 1]
		return FMath::Clamp(Total * 0.25f + 0.5f, 0.0f, 1.0f);
	}

	// -----------------------------------------------------------------------
	//  Testing & Verification
	// -----------------------------------------------------------------------

	void RunNoiseTests()
	{
		UE_LOG(LogTemp, Warning, TEXT("=== CaveNoise Determinism Tests ==="));

		// Test 1: Determinism (same input → same output)
		{
			const uint32 TestSeed = 12345u;
			const float N1_1D = SeededNoise1D(TestSeed, 42.5f);
			const float N2_1D = SeededNoise1D(TestSeed, 42.5f);
			const bool bDeterministic1D = FMath::IsNearlyEqual(N1_1D, N2_1D, 0.0001f);
			UE_LOG(LogTemp, Warning, TEXT("  1D Determinism: %s (%.6f == %.6f)"),
				bDeterministic1D ? TEXT("PASS") : TEXT("FAIL"), N1_1D, N2_1D);

			const float N1_2D = SeededNoise2D(TestSeed, 42.5f, 17.3f);
			const float N2_2D = SeededNoise2D(TestSeed, 42.5f, 17.3f);
			const bool bDeterministic2D = FMath::IsNearlyEqual(N1_2D, N2_2D, 0.0001f);
			UE_LOG(LogTemp, Warning, TEXT("  2D Determinism: %s (%.6f == %.6f)"),
				bDeterministic2D ? TEXT("PASS") : TEXT("FAIL"), N1_2D, N2_2D);

			const FVector TestPos(42.5f, 17.3f, 8.1f);
			const float N1_3D = SeededNoise3D(TestSeed, TestPos);
			const float N2_3D = SeededNoise3D(TestSeed, TestPos);
			const bool bDeterministic3D = FMath::IsNearlyEqual(N1_3D, N2_3D, 0.0001f);
			UE_LOG(LogTemp, Warning, TEXT("  3D Determinism: %s (%.6f == %.6f)"),
				bDeterministic3D ? TEXT("PASS") : TEXT("FAIL"), N1_3D, N2_3D);
		}

		// Test 2: Smoothness (nearby inputs → nearby outputs)
		{
			const uint32 TestSeed = 54321u;
			const float BaseX = 100.5f;  // Use non-integer to avoid grid boundaries
			const float N0 = SeededNoise1D(TestSeed, BaseX);
			const float N1 = SeededNoise1D(TestSeed, BaseX + 0.1f);
			const float N2 = SeededNoise1D(TestSeed, BaseX + 1.0f);
			const float Diff01 = FMath::Abs(N1 - N0);
			const float Diff02 = FMath::Abs(N2 - N0);
			const bool bSmooth1D = (Diff01 < Diff02);  // Small delta → small change
			UE_LOG(LogTemp, Warning, TEXT("  1D Smoothness: %s (Δ0.1=%.6f < Δ1.0=%.6f)"),
				bSmooth1D ? TEXT("PASS") : TEXT("FAIL"), Diff01, Diff02);

			const float BaseX2D = 50.5f, BaseY2D = 75.5f;
			const float M0 = SeededNoise2D(TestSeed, BaseX2D, BaseY2D);
			const float M1 = SeededNoise2D(TestSeed, BaseX2D + 0.05f, BaseY2D + 0.05f);
			const float M2 = SeededNoise2D(TestSeed, BaseX2D + 5.0f, BaseY2D + 5.0f);
			const float MDiff01 = FMath::Abs(M1 - M0);
			const float MDiff02 = FMath::Abs(M2 - M0);
			const bool bSmooth2D = (MDiff01 < MDiff02);
			UE_LOG(LogTemp, Warning, TEXT("  2D Smoothness: %s (Δ0.05=%.6f < Δ5.0=%.6f)"),
				bSmooth2D ? TEXT("PASS") : TEXT("FAIL"), MDiff01, MDiff02);
		}

		// Test 3: HashCellSeed determinism and distribution
		{
			const uint32 WorldSeed = 99999u;
			const FIntVector Cell1(10, 20, 30);
			const uint32 Hash1a = HashCellSeed(WorldSeed, Cell1);
			const uint32 Hash1b = HashCellSeed(WorldSeed, Cell1);
			const bool bHashDeterministic = (Hash1a == Hash1b);
			UE_LOG(LogTemp, Warning, TEXT("  HashCellSeed Determinism: %s (%u == %u)"),
				bHashDeterministic ? TEXT("PASS") : TEXT("FAIL"), Hash1a, Hash1b);

			// Different cells should produce different hashes
			const FIntVector Cell2(11, 20, 30);
			const uint32 Hash2 = HashCellSeed(WorldSeed, Cell2);
			const bool bHashDistinct = (Hash1a != Hash2);
			UE_LOG(LogTemp, Warning, TEXT("  HashCellSeed Distribution: %s (adjacent cells differ)"),
				bHashDistinct ? TEXT("PASS") : TEXT("FAIL"));
		}

		// Test 4: Output range [0, 1]
		{
			const uint32 TestSeed = 11111u;
			float MinVal = 1.0f, MaxVal = 0.0f;
			for (int32 i = 0; i < 100; ++i)
			{
				const float Val = SeededNoise2D(TestSeed, i * 0.7f, i * 1.3f);
				MinVal = FMath::Min(MinVal, Val);
				MaxVal = FMath::Max(MaxVal, Val);
			}
			const bool bRangeValid = (MinVal >= 0.0f && MaxVal <= 1.0f);
			UE_LOG(LogTemp, Warning, TEXT("  Output Range: %s (min=%.6f, max=%.6f)"),
				bRangeValid ? TEXT("PASS") : TEXT("FAIL"), MinVal, MaxVal);
		}

		UE_LOG(LogTemp, Warning, TEXT("=== End Tests ==="));
	}
}

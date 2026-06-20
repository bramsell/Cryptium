// CaveNoiseTest.cpp
// Test harness implementation for CaveNoiseUtilities.

#include "CaveNoiseTest.h"
#include "CaveNoiseUtilities.h"

namespace CaveNoiseTest
{
	void RunTests()
	{
		UE_LOG(LogTemp, Warning, TEXT("\n=== STEP 0: Cave Noise Determinism Tests ===\n"));
		CaveNoise::RunNoiseTests();
		UE_LOG(LogTemp, Warning, TEXT("\n=== Tests Complete ===\n"));
	}

	// Console command to run tests
	static FAutoConsoleCommand CaveNoiseTestCmd(
		TEXT("CaveNoiseTest"),
		TEXT("Run determinism tests for cave noise utilities"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			CaveNoiseTest::RunTests();
		})
	);
}

// CaveNoiseTest.h
// Simple test harness for CaveNoiseUtilities determinism verification.
// Call CaveNoiseTest::RunTests() once at startup or from console to verify
// the noise implementation is working correctly before proceeding to cavern generation.

#pragma once

#include "CoreMinimal.h"

namespace CaveNoiseTest
{
	/**
	 * Run all noise utility tests and log results.
	 * Should print PASS/FAIL for each test category.
	 * Call once at startup or when you want to verify noise is working.
	 */
	void RunTests();
}

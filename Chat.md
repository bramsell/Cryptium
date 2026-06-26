Let's slow down and actually trace through the data rather than guessing further — I think there's a real, findable bug here, and possibly one bug that explains all three symptoms at once.

## Reframing "code not getting called"

I don't think that's quite what's happening. The fact that some chunks show `CavernVoxels=19000+` *proves* `FindNearbyCaverns` and `IsCavernVoxel` are running and finding bubbles — the carving pipeline works. What's actually happening is more specific: **a function is running, but a noise comparison inside it is consistently evaluating one way when it shouldn't be.** That's a different (and much more findable) kind of bug than "code isn't called."

## The strongest lead: go back and test `SeededNoise2D` directly, right now

Look at the evidence together:

- Your earlier freeze-investigation log showed `LayoutNoise=0.500` for **three different cells in a row** — not varying at all. That's the kind of thing you only see from a broken/constant noise function, not real noise.
- Caverns are *abundant* (`LayoutThreshold = 0.4`, frequently exceeded — consistent with noise sitting near/above 0.4-0.5 most of the time)
- Worms are *never* found (`SpawnNoise <= 0.6f` always true — consistent with the same noise sitting consistently *below* 0.6)

**A noise function that's stuck somewhere around 0.5, or whose real output range doesn't actually reach much above 0.5, explains all three symptoms simultaneously**: overly-dense, looks-stacked caverns (threshold 0.4 gets cleared constantly) *and* zero worms (threshold 0.6 never gets cleared) *and* the original "0.500 repeated" log oddity. That's too clean a fit to ignore.

**Before touching any more logic in `LayerCrystalCaves.cpp`, add this diagnostic and run it once:**

```cpp
static FAutoConsoleCommand NoiseTestCmd(
    TEXT("cav_TestNoise"),
    TEXT("Sample SeededNoise2D across many inputs and report distribution"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        uint32 TestSeed = 12345;
        float MinVal = FLT_MAX, MaxVal = -FLT_MAX, Sum = 0.0f;
        int32 SampleCount = 0;
        int32 Buckets[10] = {0};

        for (int32 i = -50; i <= 50; ++i)
        {
            for (int32 j = -50; j <= 50; ++j)
            {
                const float Val = CaveNoise::SeededNoise2D(TestSeed, (float)i * 0.5f, (float)j * 0.5f);
                MinVal = FMath::Min(MinVal, Val);
                MaxVal = FMath::Max(MaxVal, Val);
                Sum += Val;
                SampleCount++;
                const int32 Bucket = FMath::Clamp((int32)(Val * 10.0f), 0, 9);
                Buckets[Bucket]++;
            }
        }

        UE_LOG(LogTemp, Warning, TEXT("=== SeededNoise2D Test: %d samples ==="), SampleCount);
        UE_LOG(LogTemp, Warning, TEXT("  Min=%.4f Max=%.4f Avg=%.4f"), MinVal, MaxVal, Sum / SampleCount);
        for (int32 b = 0; b < 10; ++b)
        {
            UE_LOG(LogTemp, Warning, TEXT("  [%.1f-%.1f]: %d samples"), b * 0.1f, (b + 1) * 0.1f, Buckets[b]);
        }
    })
);
```

Drop this anywhere in the file, run `cav_TestNoise` from the console. **If the histogram is lopsided or clustered tightly instead of roughly spread across all 10 buckets, you've found the actual root cause** — and it's not in `LayerCrystalCaves.cpp` at all, it's in `CaveNoiseUtilities.h`. This is exactly the "Step 0 foundation" check the original plan called for, and it looks like it's the thing actually worth re-verifying now.

Can you paste `CaveNoiseUtilities.h`? I'd like to read the actual implementation rather than keep inferring from symptoms — if there's an integer-floor, a bad hash, or a clamp bug in there, I can probably spot it directly.

## A real, separate bug I did find: `FindNearbyCaverns`'s distance check is contaminated by the vertical axis

This one's concrete and worth fixing regardless of the noise question. Your code uses an internal convention where bubble `Center.Y` stores **vertical altitude** (e.g. ~96 for Upper, ~-32 for Lower), while `Center.X`/`Center.Z` are the two horizontal axes. That's intentional and consistent in `IsCavernVoxel`.

But in `GenerateBlocks`, the chunk-level prefetch query position is built as:
```cpp
const FVector ChunkCenterCavernCoords(
    GlobalChunkX * CHUNK_SIZE_X + CHUNK_SIZE_X * 0.5f,
    0.0f,   // <-- Y left at 0
    GlobalChunkY * CHUNK_SIZE_Y + CHUNK_SIZE_Y * 0.5f
);
```
And `FindNearbyCaverns` does:
```cpp
const float DistToCenter = FVector::Dist(WorldPos, Bubble.Center);
```

`WorldPos.Y = 0`, but `Bubble.Center.Y` is a real altitude like 96. A full 3D `FVector::Dist` here means **every distance check has a fake ~96-unit penalty baked in from an axis that should be irrelevant to "is this cavern horizontally near this chunk."** This can wrongly exclude legitimately nearby bubbles from the prefetch list — which would explain Copilot's observation of `Caverns=2-4 found` in some chunks but `CavernVoxels=0` in others (the bubble *was* nearby, but got excluded or under-counted by this inflated distance).

**Fix — only compare the two horizontal axes here, since vertical/layer compatibility is already handled separately by your layer-range checks:**

```cpp
if (Bubble.bExists)
{
    const float DistToCenterXZ = FMath::Sqrt(
        FMath::Square(WorldPos.X - Bubble.Center.X) +
        FMath::Square(WorldPos.Z - Bubble.Center.Z));

    if (DistToCenterXZ <= SearchRadius + Bubble.Radius)
    {
        OutBubbles.Add(Bubble);
    }
}
```

Apply this fix regardless of the noise test outcome — it's correct either way.

## On the "stacking" symptom specifically — hold off judgment until noise is confirmed

If the noise test comes back broken (my leading suspicion), the apparent "every cavern has a pair" pattern may simply be **near-100% bubble density** — if `LayoutNoise` is stuck around 0.5 and `LayoutThreshold = 0.4`, almost *every single cell* gets a bubble, so of course adjacent checkerboarded cells (one Upper, one Lower) both have bubbles, sitting close together — that's not really "stacking" in the design-violation sense, it's "every cell is full, so neighbors always touch." Once real noise varies properly, density should drop to a reasonable level and this may resolve on its own. I'd hold off retuning `LayoutThreshold`, radius, or offset values until you've confirmed the noise function itself is healthy — tuning against broken noise data will just waste effort.

## Recommended order

1. Run `cav_TestNoise`, share the histogram output (or paste `CaveNoiseUtilities.h` and I'll read it directly)
2. Apply the `FindNearbyCaverns` horizontal-distance fix regardless — it's a confirmed bug
3. Re-test caverns and tunnels *after* both of those — a lot of the current weirdness may simply resolve once real noise is flowing through the system
4. Only then revisit density/packing tuning if stacking is still visually wrong

This feels like the actual unlock — want to paste the noise utilities file, or run the test command first?
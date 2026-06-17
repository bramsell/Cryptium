// ItemPickup.h – A world actor that grants an item to the player on overlap.
// Visual: a static mesh that spins on its yaw axis and bobs up and down.
// Collision: a USphereComponent trigger; when the player steps inside it the
// item is added to their UInventoryComponent and this actor destroys itself.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "ItemPickup.generated.h"

class UBoxComponent;
class UPhysicalMaterial;
class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UPickupLabelWidget;

UCLASS()
class CRYPTCRAFT_API AItemPickup : public AActor
{
    GENERATED_BODY()

public:
    AItemPickup();

    static AItemPickup* SpawnManagedPickup(
        UWorld*                       World,
        TSubclassOf<AItemPickup>      SpawnClass,
        FName                         InItemID,
        int32                         InQuantity,
        const FVector&                SpawnLocation,
        const FRotator&               SpawnRotation,
        const FActorSpawnParameters&  SpawnParams);

    /** Adds a small launch impulse when spawned (used by block drops). */
    UFUNCTION(BlueprintCallable, Category = "Pickup|Animation")
    void ApplySpawnImpulse(const FVector& Impulse);

    /** Adds a small angular impulse when spawned so drops tumble slightly. */
    UFUNCTION(BlueprintCallable, Category = "Pickup|Animation")
    void ApplySpawnAngularImpulse(const FVector& AngularImpulse);

    // -----------------------------------------------------------------------
    //  Components
    // -----------------------------------------------------------------------

    /** Physics body for gravity/collision/rolling. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
    TObjectPtr<UBoxComponent> BoxComponent;

    /** Sphere trigger for pickup detection only. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
    TObjectPtr<USphereComponent> SphereComponent;

    /** Visual representation of the item in the world. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    /** Small floating stack count shown when Quantity > 1. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
    TObjectPtr<UWidgetComponent> StackLabelComponent;

    /** Optional widget class override (e.g. WBP_PickupLabel). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pickup|UI")
    TSubclassOf<UPickupLabelWidget> PickupLabelWidgetClass;

    // -----------------------------------------------------------------------
    //  Item
    // -----------------------------------------------------------------------

    /** Row name in the ItemDataTable to add to the player's inventory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup")
    FName ItemID;

    /** How many of the item to give. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup", meta = (ClampMin = "1"))
    int32 Quantity = 1;

    // -----------------------------------------------------------------------
    //  Float animation
    // -----------------------------------------------------------------------

    /** Yaw rotation speed in degrees per second. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Animation")
    float RotationRate = 90.f;

    /** Half-amplitude of the vertical bob in Unreal units. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Animation")
    float BobAmplitude = 10.f;

    /** Bob cycles per second. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Animation")
    float BobFrequency = 1.f;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    static TArray<TWeakObjectPtr<AItemPickup>>& GetActivePickups();
    static void PruneActivePickups();
    static int32 GetActivePickupCount();
    static int32 GetPhysicsActivePickupCount();
    static float GetDynamicMergeIntervalSeconds();
    static bool IsUnderMergePressure();
    static void TriggerPressureMerge();
    static void RebalancePhysicsBudget();

    void ApplyDeferredSpawnImpulse();
    void UpdateStackDisplay();
    int32 AddQuantity(int32 Amount);
    void MarkSettled();
    void MarkUnsettled();
    void ScheduleMergeAttempt(float DelaySeconds, bool bOverrideExisting = true);
    void AttemptMerge();
    void StartMergeInto(AItemPickup* TargetPickup);
    void FinishMerge();
    void HandleDespawn();
    void SetPhysicsInteractionEnabled(bool bEnabled);
    void InitializeNonPhysicsFallTarget();
    bool IsAvailableForMerge() const;
    bool CanBeCapMergeTarget(FName MatchingItemID) const;

    UFUNCTION()
    void OnSphereBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        int32                OtherBodyIndex,
        bool                 bFromSweep,
        const FHitResult&    SweepResult);

    float SpawnTimeSeconds = 0.f;
    float SettledTimeSeconds = 0.f;
    float LowVelocityAccumulatedSeconds = 0.f;
    float MergeElapsedSeconds = 0.f;
    float NonPhysicsGroundZ = 0.f;

    FVector MergeStartLocation = FVector::ZeroVector;
    FVector PendingSpawnImpulse = FVector::ZeroVector;
    FVector PendingSpawnAngularImpulse = FVector::ZeroVector;

    bool bSettled = false;
    bool bMerging = false;
    bool bPhysicsInteractionEnabled = true;
    bool bWantsPhysicsInteractionOnSpawn = true;
    bool bNonPhysicsGroundInitialized = false;
    bool bNonPhysicsLanded = false;

    TWeakObjectPtr<AItemPickup> MergeTargetPickup;

    UPROPERTY(Transient)
    TObjectPtr<UPickupLabelWidget> PickupLabelWidget;

    /** Handle for deferred spawn impulse timer. */
    FTimerHandle DeferredSpawnImpulseTimerHandle;

    /** Handle for delayed merge trigger once the pickup settles. */
    FTimerHandle MergeAttemptTimerHandle;

    /** Handle for long-lived settled drop silent despawn. */
    FTimerHandle DespawnTimerHandle;

    /** High-friction physical material applied to the physics box. */
    UPROPERTY(Transient)
    TObjectPtr<UPhysicalMaterial> RuntimePhysicalMaterial;
};

#include "Items/ItemPickup.h"

#include "Components/BoxComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Inventory/InventoryComponent.h"
#include "UI/PickupLabelWidget.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

namespace
{
    constexpr int32 MaxPickupStackSize = 64;
    constexpr int32 MaxPhysicsActivePickupCount = 30;
    constexpr int32 PhysicsReenableThreshold = 25;
    constexpr float SleepLinearSpeedThreshold = 8.f;
    constexpr float SleepAngularSpeedThreshold = 12.f;
    constexpr float WakeLinearSpeedThreshold = 15.f;
    constexpr float WakeAngularSpeedThreshold = 25.f;
    constexpr float SleepConfirmDuration = 0.5f;
    constexpr float MergeRadius = 150.f;
    constexpr float PressureMergeRadius = 200.f;
    constexpr float MergeAnimationDuration = 0.5f;
    constexpr float DespawnDelaySeconds = 300.f;
    constexpr float NonPhysicsFallInterpSpeed = 4.f;
    constexpr float NonPhysicsTraceDistance = 5000.f;
}

TArray<TWeakObjectPtr<AItemPickup>>& AItemPickup::GetActivePickups()
{
    static TArray<TWeakObjectPtr<AItemPickup>> ActivePickups;
    return ActivePickups;
}

void AItemPickup::PruneActivePickups()
{
    TArray<TWeakObjectPtr<AItemPickup>>& ActivePickups = GetActivePickups();
    ActivePickups.RemoveAll([](const TWeakObjectPtr<AItemPickup>& WeakPickup)
    {
        return !WeakPickup.IsValid() || WeakPickup->IsActorBeingDestroyed();
    });
}

int32 AItemPickup::GetActivePickupCount()
{
    PruneActivePickups();
    return GetActivePickups().Num();
}

int32 AItemPickup::GetPhysicsActivePickupCount()
{
    PruneActivePickups();

    int32 Count = 0;
    for (const TWeakObjectPtr<AItemPickup>& WeakPickup : GetActivePickups())
    {
        const AItemPickup* Pickup = WeakPickup.Get();
        if (Pickup && Pickup->bPhysicsInteractionEnabled && !Pickup->bMerging)
        {
            ++Count;
        }
    }

    return Count;
}

float AItemPickup::GetDynamicMergeIntervalSeconds()
{
    const int32 ActiveCount = GetActivePickupCount();
    if (ActiveCount < 15)
    {
        return 30.f;
    }

    if (ActiveCount <= 25)
    {
        return 10.f;
    }

    return FMath::FRandRange(2.f, 3.f);
}

bool AItemPickup::IsUnderMergePressure()
{
    return GetActivePickupCount() > 30;
}

void AItemPickup::TriggerPressureMerge()
{
    if (!IsUnderMergePressure())
    {
        return;
    }

    for (const TWeakObjectPtr<AItemPickup>& WeakPickup : GetActivePickups())
    {
        if (AItemPickup* Pickup = WeakPickup.Get())
        {
            if (Pickup->IsAvailableForMerge())
            {
                Pickup->ScheduleMergeAttempt(FMath::FRandRange(1.f, 2.f), true);
            }
        }
    }
}

void AItemPickup::RebalancePhysicsBudget()
{
    PruneActivePickups();

    int32 PhysicsActiveCount = GetPhysicsActivePickupCount();
    if (PhysicsActiveCount >= PhysicsReenableThreshold)
    {
        return;
    }

    TArray<AItemPickup*> NonPhysicsPickups;
    for (const TWeakObjectPtr<AItemPickup>& WeakPickup : GetActivePickups())
    {
        if (AItemPickup* Pickup = WeakPickup.Get())
        {
            if (!Pickup->bPhysicsInteractionEnabled && !Pickup->bMerging)
            {
                NonPhysicsPickups.Add(Pickup);
            }
        }
    }

    NonPhysicsPickups.Sort([](const AItemPickup& A, const AItemPickup& B)
    {
        return A.SpawnTimeSeconds < B.SpawnTimeSeconds;
    });

    for (AItemPickup* Pickup : NonPhysicsPickups)
    {
        if (PhysicsActiveCount >= PhysicsReenableThreshold)
        {
            break;
        }

        if (Pickup)
        {
            Pickup->SetPhysicsInteractionEnabled(true);
            ++PhysicsActiveCount;
        }
    }
}

AItemPickup* AItemPickup::SpawnManagedPickup(
    UWorld*                      World,
    TSubclassOf<AItemPickup>     SpawnClass,
    FName                        InItemID,
    int32                        InQuantity,
    const FVector&               SpawnLocation,
    const FRotator&              SpawnRotation,
    const FActorSpawnParameters& SpawnParams)
{
    if (!World || InItemID.IsNone() || InQuantity <= 0)
    {
        return nullptr;
    }

    if (!SpawnClass)
    {
        SpawnClass = AItemPickup::StaticClass();
    }

    PruneActivePickups();

    AItemPickup* Pickup = World->SpawnActor<AItemPickup>(SpawnClass, SpawnLocation, SpawnRotation, SpawnParams);
    if (!Pickup)
    {
        return nullptr;
    }

    Pickup->ItemID = InItemID;
    Pickup->Quantity = FMath::Max(1, InQuantity);
    Pickup->bWantsPhysicsInteractionOnSpawn = (GetPhysicsActivePickupCount() < MaxPhysicsActivePickupCount);
    Pickup->UpdateStackDisplay();

    TriggerPressureMerge();
    return Pickup;
}

AItemPickup::AItemPickup()
{
    PrimaryActorTick.bCanEverTick = true;

    BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxComponent"));
    BoxComponent->SetBoxExtent(FVector(10.f, 10.f, 10.f));
    SetRootComponent(BoxComponent);

    SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComponent"));
    SphereComponent->SetupAttachment(BoxComponent);
    SphereComponent->SetSphereRadius(75.f);
    SphereComponent->SetRelativeLocation(FVector::ZeroVector);

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    MeshComponent->SetupAttachment(BoxComponent);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComponent->SetRelativeLocation(FVector::ZeroVector);

    StackLabelComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("StackLabelComponent"));
    StackLabelComponent->SetupAttachment(BoxComponent);
    StackLabelComponent->SetRelativeLocation(FVector(0.f, 0.f, 42.f));
    StackLabelComponent->SetDrawAtDesiredSize(true);
    StackLabelComponent->SetWidgetSpace(EWidgetSpace::World);
    StackLabelComponent->SetDrawSize(FVector2D(96.f, 36.f));
    StackLabelComponent->SetPivot(FVector2D(0.5f, 0.5f));
    StackLabelComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StackLabelComponent->SetGenerateOverlapEvents(false);
    StackLabelComponent->SetTwoSided(true);
    StackLabelComponent->SetHiddenInGame(false);
    StackLabelComponent->SetVisibility(true, true);

    static ConstructorHelpers::FClassFinder<UPickupLabelWidget> PickupLabelWidgetBPClass(TEXT("/Game/UI/WBP_PickupLabel"));
    if (PickupLabelWidgetBPClass.Succeeded())
    {
        PickupLabelWidgetClass = PickupLabelWidgetBPClass.Class;
    }
}

void AItemPickup::ApplySpawnImpulse(const FVector& Impulse)
{
    if (!BoxComponent)
    {
        return;
    }

    if (!HasActorBegunPlay())
    {
        PendingSpawnImpulse += Impulse;
        return;
    }

    if (!Impulse.IsNearlyZero())
    {
        BoxComponent->AddImpulse(Impulse, NAME_None, true);
        MarkUnsettled();
    }
}

void AItemPickup::ApplySpawnAngularImpulse(const FVector& AngularImpulse)
{
    if (!BoxComponent)
    {
        return;
    }

    if (!HasActorBegunPlay())
    {
        PendingSpawnAngularImpulse += AngularImpulse;
        return;
    }

    if (!AngularImpulse.IsNearlyZero())
    {
        BoxComponent->AddAngularImpulseInDegrees(AngularImpulse, NAME_None, true);
        MarkUnsettled();
    }
}

void AItemPickup::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup] BeginPlay '%s' at %s"),
        *GetName(),
        *GetActorLocation().ToString());

    const bool bHasBox = (BoxComponent != nullptr);
    const bool bHasSphere = (SphereComponent != nullptr);
    const bool bHasMeshComponent = (MeshComponent != nullptr);
    const bool bMeshAssigned = bHasMeshComponent && (MeshComponent->GetStaticMesh() != nullptr);
    const bool bBoxIsRoot = (GetRootComponent() == BoxComponent);
    const bool bMeshAttachedToBox = bHasMeshComponent && (MeshComponent->GetAttachParent() == BoxComponent);
    const bool bSphereAttachedToBox = bHasSphere && (SphereComponent->GetAttachParent() == BoxComponent);

    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup] Components: Box=%s Sphere=%s MeshComp=%s MeshAssigned=%s"),
        bHasBox ? TEXT("true") : TEXT("false"),
        bHasSphere ? TEXT("true") : TEXT("false"),
        bHasMeshComponent ? TEXT("true") : TEXT("false"),
        bMeshAssigned ? TEXT("true") : TEXT("false"));

    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup] Hierarchy: RootIsBox=%s MeshAttachedToBox=%s SphereAttachedToBox=%s"),
        bBoxIsRoot ? TEXT("true") : TEXT("false"),
        bMeshAttachedToBox ? TEXT("true") : TEXT("false"),
        bSphereAttachedToBox ? TEXT("true") : TEXT("false"));

    if (bHasMeshComponent)
    {
        const FString MeshName = bMeshAssigned ? MeshComponent->GetStaticMesh()->GetName() : TEXT("None");
        UE_LOG(LogTemp, Warning, TEXT("[ItemPickup] Mesh asset: %s"), *MeshName);
    }

    if (!bHasBox || !bHasSphere || !bHasMeshComponent || !StackLabelComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[ItemPickup] Missing required components. Pickup setup is invalid."));
        return;
    }

    if (PickupLabelWidgetClass)
    {
        StackLabelComponent->SetWidgetClass(PickupLabelWidgetClass);
        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Label] %s using PickupLabelWidgetClass: %s"),
            *GetName(),
            *GetNameSafe(PickupLabelWidgetClass));
    }
    else
    {
        StackLabelComponent->SetWidgetClass(UPickupLabelWidget::StaticClass());
        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Label] %s PickupLabelWidgetClass not set; using fallback class: %s"),
            *GetName(),
            *GetNameSafe(UPickupLabelWidget::StaticClass()));
    }

    StackLabelComponent->InitWidget();
    PickupLabelWidget = Cast<UPickupLabelWidget>(StackLabelComponent->GetUserWidgetObject());
    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup][Label] %s widget init: Component=%s UserWidget=%s Space=%s DrawAtDesiredSize=%s DrawSize=%s"),
        *GetName(),
        StackLabelComponent ? TEXT("valid") : TEXT("null"),
        *GetNameSafe(StackLabelComponent ? StackLabelComponent->GetUserWidgetObject() : nullptr),
        StackLabelComponent && StackLabelComponent->GetWidgetSpace() == EWidgetSpace::World ? TEXT("World") : TEXT("Screen"),
        StackLabelComponent && StackLabelComponent->GetDrawAtDesiredSize() ? TEXT("true") : TEXT("false"),
        StackLabelComponent ? *StackLabelComponent->GetDrawSize().ToString() : TEXT("(0,0)"));

    if (!PickupLabelWidget)
    {
        StackLabelComponent->SetWidgetClass(UPickupLabelWidget::StaticClass());
        StackLabelComponent->InitWidget();
        PickupLabelWidget = Cast<UPickupLabelWidget>(StackLabelComponent->GetUserWidgetObject());
        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Label] %s fallback widget init result: %s"),
            *GetName(),
            *GetNameSafe(PickupLabelWidget.Get()));
    }

    if (!bMeshAttachedToBox)
    {
        MeshComponent->AttachToComponent(BoxComponent, FAttachmentTransformRules::KeepRelativeTransform);
    }

    if (!bSphereAttachedToBox)
    {
        SphereComponent->AttachToComponent(BoxComponent, FAttachmentTransformRules::KeepRelativeTransform);
    }

    BoxComponent->SetMassOverrideInKg(NAME_None, 1.0f, true);
    BoxComponent->SetLinearDamping(3.0f);
    BoxComponent->SetAngularDamping(3.0f);
    BoxComponent->BodyInstance.SleepFamily = ESleepFamily::Custom;
    BoxComponent->BodyInstance.CustomSleepThresholdMultiplier = 0.25f;

    RuntimePhysicalMaterial = NewObject<UPhysicalMaterial>(this);
    if (RuntimePhysicalMaterial)
    {
        RuntimePhysicalMaterial->Friction = 2.0f;
        RuntimePhysicalMaterial->Restitution = 0.0f;
        BoxComponent->SetPhysMaterialOverride(RuntimePhysicalMaterial);
    }

    SphereComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SphereComponent->SetGenerateOverlapEvents(true);
    SphereComponent->SetCollisionObjectType(ECC_WorldDynamic);
    SphereComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
    SphereComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

    SetPhysicsInteractionEnabled(bWantsPhysicsInteractionOnSpawn);
    if (!bPhysicsInteractionEnabled)
    {
        InitializeNonPhysicsFallTarget();
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup] '%s' – IsSimulatingPhysics=%s, Mass=%f, Gravity=%s"),
        *GetName(),
        bPhysicsInteractionEnabled ? TEXT("true") : TEXT("false"),
        BoxComponent->GetMass(),
        BoxComponent->IsGravityEnabled() ? TEXT("true") : TEXT("false"));

    SpawnTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    SetActorScale3D(FVector::OneVector);
    UpdateStackDisplay();
    GetActivePickups().Add(this);
    RebalancePhysicsBudget();
    TriggerPressureMerge();

    SphereComponent->OnComponentBeginOverlap.AddDynamic(this, &AItemPickup::OnSphereBeginOverlap);

    if (!PendingSpawnImpulse.IsNearlyZero() || !PendingSpawnAngularImpulse.IsNearlyZero())
    {
        GetWorldTimerManager().SetTimer(
            DeferredSpawnImpulseTimerHandle,
            this,
            &AItemPickup::ApplyDeferredSpawnImpulse,
            0.1f,
            false);
    }
}

void AItemPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DeferredSpawnImpulseTimerHandle);
        World->GetTimerManager().ClearTimer(MergeAttemptTimerHandle);
        World->GetTimerManager().ClearTimer(DespawnTimerHandle);
    }

    TArray<TWeakObjectPtr<AItemPickup>>& ActivePickups = GetActivePickups();
    ActivePickups.RemoveAll([this](const TWeakObjectPtr<AItemPickup>& WeakPickup)
    {
        return !WeakPickup.IsValid() || WeakPickup.Get() == this;
    });

    RebalancePhysicsBudget();

    Super::EndPlay(EndPlayReason);
}

void AItemPickup::ApplyDeferredSpawnImpulse()
{
    const FVector DeferredImpulse = PendingSpawnImpulse;
    const FVector DeferredAngularImpulse = PendingSpawnAngularImpulse;
    PendingSpawnImpulse = FVector::ZeroVector;
    PendingSpawnAngularImpulse = FVector::ZeroVector;
    ApplySpawnImpulse(DeferredImpulse);
    ApplySpawnAngularImpulse(DeferredAngularImpulse);
}

void AItemPickup::SetPhysicsInteractionEnabled(bool bEnabled)
{
    if (!BoxComponent)
    {
        return;
    }

    bPhysicsInteractionEnabled = bEnabled;

    BoxComponent->SetCollisionObjectType(ECC_PhysicsBody);
    BoxComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
    BoxComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    BoxComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    BoxComponent->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    BoxComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

    if (bEnabled)
    {
        bNonPhysicsGroundInitialized = false;
        bNonPhysicsLanded = false;
        BoxComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        BoxComponent->SetSimulatePhysics(true);
        BoxComponent->SetEnableGravity(true);
        if (!bSettled)
        {
            BoxComponent->WakeRigidBody();
        }
    }
    else
    {
        bNonPhysicsLanded = false;
        BoxComponent->SetSimulatePhysics(false);
        BoxComponent->SetEnableGravity(false);
        BoxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void AItemPickup::InitializeNonPhysicsFallTarget()
{
    if (!GetWorld())
    {
        return;
    }

    const FVector Start = GetActorLocation();
    const FVector End = Start - FVector(0.f, 0.f, NonPhysicsTraceDistance);

    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ItemPickupNonPhysicsGround), false, this);
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);

    if (bHit)
    {
        NonPhysicsGroundZ = Hit.ImpactPoint.Z + 10.f;
    }
    else
    {
        NonPhysicsGroundZ = Start.Z - 300.f;
    }

    bNonPhysicsGroundInitialized = true;
    bNonPhysicsLanded = false;
}

void AItemPickup::UpdateStackDisplay()
{
    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup][Label] %s UpdateStackDisplay called: Quantity=%d"),
        *GetName(),
        Quantity);

    if (StackLabelComponent)
    {
        StackLabelComponent->SetHiddenInGame(false);
        StackLabelComponent->SetVisibility(true, true);
        StackLabelComponent->SetDrawAtDesiredSize(true);

        if (!StackLabelComponent->GetUserWidgetObject())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[ItemPickup][Label] %s UserWidgetObject missing during update; reinitializing"),
                *GetName());
            StackLabelComponent->InitWidget();
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Label] %s component state: Space=%s DrawSize=%s Visible=%s HiddenInGame=%s Widget=%s"),
            *GetName(),
            StackLabelComponent->GetWidgetSpace() == EWidgetSpace::World ? TEXT("World") : TEXT("Screen"),
            *StackLabelComponent->GetDrawSize().ToString(),
            StackLabelComponent->IsVisible() ? TEXT("true") : TEXT("false"),
            StackLabelComponent->bHiddenInGame ? TEXT("true") : TEXT("false"),
            *GetNameSafe(StackLabelComponent->GetUserWidgetObject()));
    }

    if (!PickupLabelWidget && StackLabelComponent)
    {
        PickupLabelWidget = Cast<UPickupLabelWidget>(StackLabelComponent->GetUserWidgetObject());
    }

    if (PickupLabelWidget)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Label] %s pushing quantity to widget: %d"),
            *GetName(),
            Quantity);
        PickupLabelWidget->SetQuantity(Quantity);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Label] %s PickupLabelWidget is null during update"),
            *GetName());
    }
}

int32 AItemPickup::AddQuantity(int32 Amount)
{
    if (Amount <= 0)
    {
        return 0;
    }

    const int32 SpaceAvailable = FMath::Max(0, MaxPickupStackSize - Quantity);
    const int32 AddedAmount = FMath::Min(SpaceAvailable, Amount);
    const int32 LeftoverAmount = Amount - AddedAmount;

    Quantity = FMath::Clamp(Quantity + AddedAmount, 1, MaxPickupStackSize);
    UpdateStackDisplay();

    return LeftoverAmount;
}

bool AItemPickup::IsAvailableForMerge() const
{
    return !IsActorBeingDestroyed() && bSettled && !bMerging && !ItemID.IsNone() && Quantity > 0 && Quantity < MaxPickupStackSize;
}

bool AItemPickup::CanBeCapMergeTarget(FName MatchingItemID) const
{
    return IsAvailableForMerge() && ItemID == MatchingItemID;
}

void AItemPickup::MarkSettled()
{
    if (bSettled || bMerging)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[ItemPickup][Merge] %s MarkSettled skipped (bSettled=%s, bMerging=%s)"),
            *GetName(),
            bSettled ? TEXT("true") : TEXT("false"),
            bMerging ? TEXT("true") : TEXT("false"));
        return;
    }

    bSettled = true;
    LowVelocityAccumulatedSeconds = 0.f;
    SettledTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

    if (bPhysicsInteractionEnabled && BoxComponent && BoxComponent->IsSimulatingPhysics())
    {
        BoxComponent->PutRigidBodyToSleep();
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup][Merge] %s transitioned to SETTLED (ItemID=%s, Qty=%d, Physics=%s)"),
        *GetName(),
        *ItemID.ToString(),
        Quantity,
        bPhysicsInteractionEnabled ? TEXT("true") : TEXT("false"));

    if (Quantity < MaxPickupStackSize)
    {
        ScheduleMergeAttempt(GetDynamicMergeIntervalSeconds());
    }
    else
    {
        GetWorldTimerManager().ClearTimer(MergeAttemptTimerHandle);
    }

    GetWorldTimerManager().SetTimer(
        DespawnTimerHandle,
        this,
        &AItemPickup::HandleDespawn,
        DespawnDelaySeconds,
        false);
}

void AItemPickup::MarkUnsettled()
{
    bSettled = false;
    LowVelocityAccumulatedSeconds = 0.f;

    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(MergeAttemptTimerHandle);
        GetWorldTimerManager().ClearTimer(DespawnTimerHandle);
    }

    if (bPhysicsInteractionEnabled && BoxComponent && BoxComponent->IsSimulatingPhysics())
    {
        BoxComponent->WakeRigidBody();
    }
}

void AItemPickup::ScheduleMergeAttempt(float DelaySeconds, bool bOverrideExisting)
{
    if (!bSettled || bMerging || Quantity >= MaxPickupStackSize)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[ItemPickup][Merge] %s ScheduleMergeAttempt skipped (Settled=%s, Merging=%s, FullStack=%s)"),
            *GetName(),
            bSettled ? TEXT("true") : TEXT("false"),
            bMerging ? TEXT("true") : TEXT("false"),
            (Quantity >= MaxPickupStackSize) ? TEXT("true") : TEXT("false"));
        GetWorldTimerManager().ClearTimer(MergeAttemptTimerHandle);
        return;
    }

    if (!bOverrideExisting && GetWorldTimerManager().IsTimerActive(MergeAttemptTimerHandle))
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[ItemPickup][Merge] %s ScheduleMergeAttempt skipped (timer already active, override=false)"),
            *GetName());
        return;
    }

    GetWorldTimerManager().ClearTimer(MergeAttemptTimerHandle);
    GetWorldTimerManager().SetTimer(
        MergeAttemptTimerHandle,
        this,
        &AItemPickup::AttemptMerge,
        DelaySeconds,
        false);

    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup][Merge] %s merge timer registered (delay=%.2fs, item=%s, qty=%d, activeDrops=%d, pressure=%s)"),
        *GetName(),
        DelaySeconds,
        *ItemID.ToString(),
        Quantity,
        GetActivePickupCount(),
        IsUnderMergePressure() ? TEXT("true") : TEXT("false"));
}

void AItemPickup::AttemptMerge()
{
    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup][Merge] %s merge timer FIRED (Settled=%s, Merging=%s, ItemID=%s, Qty=%d)"),
        *GetName(),
        bSettled ? TEXT("true") : TEXT("false"),
        bMerging ? TEXT("true") : TEXT("false"),
        *ItemID.ToString(),
        Quantity);

    if (!IsAvailableForMerge())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Merge] %s merge aborted: unavailable (Destroyed=%s, Settled=%s, Merging=%s, ItemNone=%s, Qty=%d)"),
            *GetName(),
            IsActorBeingDestroyed() ? TEXT("true") : TEXT("false"),
            bSettled ? TEXT("true") : TEXT("false"),
            bMerging ? TEXT("true") : TEXT("false"),
            ItemID.IsNone() ? TEXT("true") : TEXT("false"),
            Quantity);
        return;
    }

    PruneActivePickups();

    AItemPickup* NearestSameTypePickup = nullptr;
    float NearestDistanceSq = TNumericLimits<float>::Max();
    const float EffectiveMergeRadius = IsUnderMergePressure() ? PressureMergeRadius : MergeRadius;
    const float MergeRadiusSq = FMath::Square(EffectiveMergeRadius);
    int32 SameTypeInRangeCount = 0;

    for (const TWeakObjectPtr<AItemPickup>& WeakPickup : GetActivePickups())
    {
        AItemPickup* Pickup = WeakPickup.Get();
        if (Pickup == this)
        {
            continue;
        }

        if (!Pickup || !Pickup->IsAvailableForMerge() || Pickup->ItemID != ItemID)
        {
            continue;
        }

        const float DistanceSq = FVector::DistSquared(GetActorLocation(), Pickup->GetActorLocation());
        if (DistanceSq <= MergeRadiusSq && DistanceSq < NearestDistanceSq)
        {
            NearestDistanceSq = DistanceSq;
            NearestSameTypePickup = Pickup;
        }

        if (DistanceSq <= MergeRadiusSq)
        {
            ++SameTypeInRangeCount;
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup][Merge] %s candidate scan complete: same-type-in-range=%d (radius=%.1f, pressure=%s)"),
        *GetName(),
        SameTypeInRangeCount,
        EffectiveMergeRadius,
        IsUnderMergePressure() ? TEXT("true") : TEXT("false"));

    if (!NearestSameTypePickup)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Merge] %s no merge target found; merge not initiated."),
            *GetName());

        if (bSettled)
        {
            ScheduleMergeAttempt(GetDynamicMergeIntervalSeconds());
        }
        return;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[ItemPickup][Merge] %s target selected: %s (targetQty=%d, selfQty=%d, dist=%.1f)"),
        *GetName(),
        *NearestSameTypePickup->GetName(),
        NearestSameTypePickup->Quantity,
        Quantity,
        FMath::Sqrt(NearestDistanceSq));

    if (Quantity >= NearestSameTypePickup->Quantity)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Merge] %s initiating merge: target %s -> self %s"),
            *GetName(),
            *NearestSameTypePickup->GetName(),
            *GetName());
        NearestSameTypePickup->StartMergeInto(this);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ItemPickup][Merge] %s initiating merge: self %s -> target %s"),
            *GetName(),
            *GetName(),
            *NearestSameTypePickup->GetName());
        StartMergeInto(NearestSameTypePickup);
    }

    if (bSettled)
    {
        ScheduleMergeAttempt(GetDynamicMergeIntervalSeconds());
    }
}

void AItemPickup::StartMergeInto(AItemPickup* TargetPickup)
{
    if (!TargetPickup || TargetPickup == this || bMerging)
    {
        return;
    }

    bMerging = true;
    bSettled = false;
    MergeElapsedSeconds = 0.f;
    MergeStartLocation = GetActorLocation();
    MergeTargetPickup = TargetPickup;

    GetWorldTimerManager().ClearTimer(MergeAttemptTimerHandle);
    GetWorldTimerManager().ClearTimer(DespawnTimerHandle);

    SphereComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BoxComponent->SetSimulatePhysics(false);
    BoxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetActorScale3D(FVector::OneVector);
}

void AItemPickup::FinishMerge()
{
    if (AItemPickup* TargetPickup = MergeTargetPickup.Get())
    {
        const int32 LeftoverAfterTransfer = TargetPickup->AddQuantity(Quantity);

        if (TargetPickup->Quantity < MaxPickupStackSize)
        {
            TargetPickup->ScheduleMergeAttempt(GetDynamicMergeIntervalSeconds());
        }
        else
        {
            TargetPickup->GetWorldTimerManager().ClearTimer(TargetPickup->MergeAttemptTimerHandle);
        }

        if (LeftoverAfterTransfer <= 0)
        {
            TriggerPressureMerge();
            RebalancePhysicsBudget();
            Destroy();
            return;
        }

        Quantity = LeftoverAfterTransfer;
        UpdateStackDisplay();

        bMerging = false;
        MergeTargetPickup.Reset();
        MergeElapsedSeconds = 0.f;
        SetActorScale3D(FVector::OneVector);
        SetActorLocation(MergeStartLocation);

        SphereComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        SphereComponent->SetGenerateOverlapEvents(true);
        SphereComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
        SphereComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

        SetPhysicsInteractionEnabled(bPhysicsInteractionEnabled);
        if (!bPhysicsInteractionEnabled)
        {
            bNonPhysicsGroundInitialized = true;
            bNonPhysicsLanded = true;
            NonPhysicsGroundZ = MergeStartLocation.Z;
        }

        bSettled = false;
        MarkSettled();

        TriggerPressureMerge();
        RebalancePhysicsBudget();
        return;
    }

    Destroy();
}
void AItemPickup::HandleDespawn()
{
    if (bMerging)
    {
        return;
    }

    Destroy();
}

void AItemPickup::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bMerging)
    {
        MergeElapsedSeconds += DeltaTime;
        const float Alpha = FMath::Clamp(MergeElapsedSeconds / MergeAnimationDuration, 0.f, 1.f);

        if (AItemPickup* TargetPickup = MergeTargetPickup.Get())
        {
            SetActorLocation(FMath::Lerp(MergeStartLocation, TargetPickup->GetActorLocation(), Alpha));
            SetActorScale3D(FMath::Lerp(FVector::OneVector, FVector::ZeroVector, Alpha));
        }
        else
        {
            bMerging = false;
            SetActorScale3D(FVector::OneVector);
            SphereComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            SphereComponent->SetGenerateOverlapEvents(true);
            SphereComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
            SphereComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

            SetPhysicsInteractionEnabled(bPhysicsInteractionEnabled);
            if (!bPhysicsInteractionEnabled)
            {
                InitializeNonPhysicsFallTarget();
            }

            MarkUnsettled();
            return;
        }

        if (Alpha >= 1.f)
        {
            FinishMerge();
        }
        return;
    }

    if (!BoxComponent)
    {
        return;
    }

    if (!bPhysicsInteractionEnabled)
    {
        if (!bNonPhysicsGroundInitialized)
        {
            InitializeNonPhysicsFallTarget();
        }

        FVector Location = GetActorLocation();
        Location.Z = FMath::FInterpTo(Location.Z, NonPhysicsGroundZ, DeltaTime, NonPhysicsFallInterpSpeed);
        SetActorLocation(Location);

        if (!bNonPhysicsLanded && FMath::Abs(Location.Z - NonPhysicsGroundZ) <= 1.f)
        {
            Location.Z = NonPhysicsGroundZ;
            SetActorLocation(Location);
            bNonPhysicsLanded = true;
            MarkSettled();
        }

        return;
    }

    if (!BoxComponent->IsSimulatingPhysics())
    {
        return;
    }

    const float LinearSpeed = BoxComponent->GetPhysicsLinearVelocity().Size();
    const float AngularSpeed = BoxComponent->GetPhysicsAngularVelocityInDegrees().Size();

    if (!bSettled)
    {
        if (LinearSpeed <= SleepLinearSpeedThreshold && AngularSpeed <= SleepAngularSpeedThreshold)
        {
            LowVelocityAccumulatedSeconds += DeltaTime;
            if (LowVelocityAccumulatedSeconds >= SleepConfirmDuration)
            {
                MarkSettled();
            }
        }
        else
        {
            LowVelocityAccumulatedSeconds = 0.f;
        }
    }
    else if (LinearSpeed > WakeLinearSpeedThreshold || AngularSpeed > WakeAngularSpeedThreshold)
    {
        MarkUnsettled();
    }
}

void AItemPickup::OnSphereBeginOverlap(
    UPrimitiveComponent* /*OverlappedComponent*/,
    AActor*              OtherActor,
    UPrimitiveComponent* /*OtherComp*/,
    int32                /*OtherBodyIndex*/,
    bool                 /*bFromSweep*/,
    const FHitResult&    /*SweepResult*/)
{
    if (!OtherActor || bMerging)
    {
        return;
    }

    if (!OtherActor->IsA<ACharacter>())
    {
        return;
    }

    UInventoryComponent* Inventory = OtherActor->FindComponentByClass<UInventoryComponent>();
    if (!Inventory)
    {
        return;
    }

    const int32 Leftover = Inventory->AddItem(ItemID, Quantity);
    UE_LOG(LogTemp, Warning, TEXT("[ItemPickup] AddItem returned Leftover=%d, Quantity=%d, ItemID=%s"), Leftover, Quantity, *ItemID.ToString());
    
    if (Leftover <= 0)
    {
        Destroy();
        return;
    }

    Quantity = Leftover;
    UpdateStackDisplay();
}

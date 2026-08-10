#include "WorldDestructibleBox.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

AWorldDestructibleBox::AWorldDestructibleBox()
{
	PrimaryActorTick.bCanEverTick = false;
	IntactMeshAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		TEXT("/Game/ModularLostRuinKit/Models/Props/SM_WoodenBox1.SM_WoodenBox1")));
	FracturedGeometryCollectionAsset = TSoftObjectPtr<UGeometryCollection>(FSoftObjectPath(
		TEXT("/Game/PhysicsWorldDemo/GeometryCollections/GC_PW_WoodenBox1_Fractured.GC_PW_WoodenBox1_Fractured")));

	IntactMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IntactMesh"));
	SetRootComponent(IntactMesh);
	IntactMesh->SetMobility(EComponentMobility::Movable);
	IntactMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	IntactMesh->SetCollisionObjectType(ECC_PhysicsBody);
	IntactMesh->SetCollisionResponseToAllChannels(ECR_Block);
	IntactMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		IntactMesh->SetStaticMesh(CubeFinder.Object);
	}
	IntactMesh->SetEnableGravity(true);
	IntactMesh->SetSimulatePhysics(true);

	GeometryCollection = CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("GeometryCollection"));
	GeometryCollection->SetupAttachment(IntactMesh);
	GeometryCollection->SetVisibility(false, true);
	GeometryCollection->SetHiddenInGame(true, true);
	GeometryCollection->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GeometryCollection->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	GeometryCollection->SetSimulatePhysics(false);
}

void AWorldDestructibleBox::BeginPlay()
{
	Super::BeginPlay();
	EnsureIntactMeshActorRoot();
	if (IntactMesh)
	{
		UStaticMesh* CrateMesh = IntactMeshAsset.LoadSynchronous();
		if (!CrateMesh)
		{
			CrateMesh = LoadObject<UStaticMesh>(
				nullptr,
				TEXT("/Game/PhysicsWorldDemo/Meshes/SM_Demo_WoodenCrate.SM_Demo_WoodenCrate"));
		}
		if (CrateMesh)
		{
			IntactMesh->SetStaticMesh(CrateMesh);
		}
	}
	if (!InteractionConfig)
	{
		InteractionConfig = LoadObject<UWorldInteractionConfig>(
			nullptr,
			TEXT("/Game/PhysicsWorldDemo/Config/DA_WorldInteractionConfig.DA_WorldInteractionConfig"));
	}
	if (GeometryCollection)
	{
		UGeometryCollection* Collection = FracturedGeometryCollectionAsset.LoadSynchronous();
		if (!Collection && !GeometryCollection->GetRestCollection())
		{
			Collection = LoadObject<UGeometryCollection>(
				nullptr,
				TEXT("/Game/PhysicsWorldDemo/GeometryCollections/GC_Demo_WoodenCrate_Fractured.GC_Demo_WoodenCrate_Fractured"));
		}
		if (Collection && GeometryCollection->GetRestCollection() != Collection)
		{
			GeometryCollection->SetRestCollection(Collection);
		}
	}
	UPhysicalMaterial* WoodMaterial = LoadObject<UPhysicalMaterial>(
		nullptr,
		TEXT("/Game/PhysicsWorldDemo/Materials/PhysicalMaterials/PM_Wood.PM_Wood"));
	if (IntactMesh)
	{
		if (WoodMaterial)
		{
			IntactMesh->SetPhysMaterialOverride(WoodMaterial);
		}
		IntactMesh->SetLinearDamping(GetSettings().DestructibleIntactLinearDamping);
		IntactMesh->SetAngularDamping(GetSettings().DestructibleIntactAngularDamping);
		IntactMesh->SetSimulatePhysics(bEnableIntactPhysics);
		IntactMesh->SetEnableGravity(bEnableIntactPhysics);
	}
	if (GeometryCollection && GeometryCollection->GetRestCollection())
	{
		GeometryCollection->SetNotifyBreaks(true);
		GeometryCollection->OnChaosBreakEvent.AddUniqueDynamic(
			this,
			&AWorldDestructibleBox::HandleChaosBreak);
		UPhysicalMaterial* BasePhysicalMaterial = WoodMaterial;
		if (!BasePhysicalMaterial && GEngine)
		{
			BasePhysicalMaterial = GEngine->DefaultPhysMaterial.Get();
		}
		if (BasePhysicalMaterial)
		{
			GeometryCollectionPhysicalMaterial = DuplicateObject<UPhysicalMaterial>(
				BasePhysicalMaterial,
				this);
			GeometryCollectionPhysicalMaterial->SetFlags(RF_Transient);
		}

		ConfigureGeometryCollectionMass();
		GeometryCollection->SetLinearDamping(GetSettings().DestructibleDebrisLinearDamping);
		GeometryCollection->SetAngularDamping(GetSettings().DestructibleDebrisAngularDamping);
		// Pre-warm an inert Dynamic proxy so runtime destruction never relies on a
		// state change that UGeometryCollectionComponent cannot forward to an existing proxy.
		GeometryCollection->EnableClustering = true;
		GeometryCollection->ObjectType = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		GeometryCollection->SetEnableGravity(false);
		GeometryCollection->SetSimulatePhysics(true);
	}
	SetBoxMassKg(BoxMassKg);
	CurrentHealth = GetMaxHealth();
}

void AWorldDestructibleBox::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GeometryCollection)
	{
		GeometryCollection->OnChaosBreakEvent.RemoveDynamic(
			this,
			&AWorldDestructibleBox::HandleChaosBreak);
	}
	Super::EndPlay(EndPlayReason);
}

bool AWorldDestructibleBox::EnsureIntactMeshActorRoot()
{
	if (!IntactMesh || GetRootComponent() == IntactMesh)
	{
		return IntactMesh != nullptr;
	}

	USceneComponent* PreviousRoot = GetRootComponent();
	const FTransform IntactWorldTransform = IntactMesh->GetComponentTransform();
	IntactMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	SetRootComponent(IntactMesh);
	IntactMesh->SetWorldTransform(
		IntactWorldTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (PreviousRoot)
	{
		PreviousRoot->AttachToComponent(
			IntactMesh,
			FAttachmentTransformRules::KeepWorldTransform);
	}
	return GetRootComponent() == IntactMesh;
}

const FWorldInteractionSettings& AWorldDestructibleBox::GetSettings() const
{
	return InteractionConfig ? InteractionConfig->Settings : FallbackSettings;
}

float AWorldDestructibleBox::GetMaxHealth() const
{
	return GetSettings().DestructibleBoxMaxHealth;
}

bool AWorldDestructibleBox::HasGeometryCollectionAsset() const
{
	return GeometryCollection && GeometryCollection->GetRestCollection();
}

bool AWorldDestructibleBox::IsGeometryCollectionActive() const
{
	return HasGeometryCollectionAsset() && GeometryCollection->IsVisible() &&
		GeometryCollection->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
}

bool AWorldDestructibleBox::IsGeometryCollectionGravityEnabled() const
{
	// UGeometryCollectionComponent::GetBodyInstance() intentionally returns null,
	// so UPrimitiveComponent::IsGravityEnabled() cannot report this state.
	return GeometryCollection && GeometryCollection->BodyInstance.bEnableGravity;
}

float AWorldDestructibleBox::CalculateDebrisSpread() const
{
	if (!GeometryCollection || !GeometryCollection->GetRestCollection())
	{
		return 0.0f;
	}

	const TArray<FTransform> Transforms = GeometryCollection->GetCurrentTransforms();

	float MaximumDistanceSquared = 0.0f;
	for (int32 FirstIndex = 0; FirstIndex < Transforms.Num(); ++FirstIndex)
	{
		for (int32 SecondIndex = FirstIndex + 1; SecondIndex < Transforms.Num(); ++SecondIndex)
		{
			MaximumDistanceSquared = FMath::Max(
				MaximumDistanceSquared,
				FVector::DistSquared(
					Transforms[FirstIndex].GetLocation(),
					Transforms[SecondIndex].GetLocation()));
		}
	}
	return FMath::Sqrt(MaximumDistanceSquared);
}

float AWorldDestructibleBox::GetDebrisExpansionDistance() const
{
	return bDestroyed ? FMath::Max(0.0f, CalculateDebrisSpread() - InitialDebrisSpread) : 0.0f;
}

bool AWorldDestructibleBox::IsIntactPhysicsSimulating() const
{
	return !bDestroyed && IntactMesh && IntactMesh->IsSimulatingPhysics();
}

float AWorldDestructibleBox::GetIntactPhysicsMassKg() const
{
	return IntactMesh && IntactMesh->IsSimulatingPhysics() ? IntactMesh->GetMass() : 0.0f;
}

float AWorldDestructibleBox::GetConfiguredBoxMassKg() const
{
	return BoxMassKg > UE_KINDA_SMALL_NUMBER
		? BoxMassKg
		: FMath::Max(0.001f, GetSettings().DestructibleBoxDefaultMassKg);
}

float AWorldDestructibleBox::GetGeometryCollectionMassKg() const
{
	return GeometryCollection && GeometryCollection->GetRestCollection()
		? GeometryCollection->GetMass()
		: 0.0f;
}

void AWorldDestructibleBox::ConfigureGeometryCollectionMass()
{
	if (!GeometryCollection || !GeometryCollection->GetRestCollection() ||
		!GeometryCollectionPhysicalMaterial)
	{
		return;
	}

	if (GeometryCollectionAssetMassKg <= UE_KINDA_SMALL_NUMBER)
	{
		GeometryCollection->SetDensityFromPhysicsMaterial(false);
		GeometryCollectionAssetMassKg = GeometryCollection->GetMass();
	}

	bool bAssetUsesDensity = false;
	const float AssetDensityKgPerCm3 =
		GeometryCollection->GetRestCollection()->GetMassOrDensity(bAssetUsesDensity);
	if (!bAssetUsesDensity || GeometryCollectionAssetMassKg <= UE_KINDA_SMALL_NUMBER ||
		AssetDensityKgPerCm3 <= UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Cannot match Geometry Collection mass for %s: the asset must use a positive density."),
			*GetPathName());
		return;
	}

	const float MassScale = GetConfiguredBoxMassKg() / GeometryCollectionAssetMassKg;
	// Physical materials store g/cm3 while Geometry Collections expose kg/cm3.
	GeometryCollectionPhysicalMaterial->Density = AssetDensityKgPerCm3 * MassScale * 1000.0f;
	GeometryCollection->SetPhysMaterialOverride(GeometryCollectionPhysicalMaterial);
	// This call also refreshes the mass multiplier when the Chaos proxy already exists.
	GeometryCollection->SetDensityFromPhysicsMaterial(true);
}

void AWorldDestructibleBox::SetBoxMassKg(const float NewMassKg)
{
	BoxMassKg = FMath::Max(0.0f, NewMassKg);
	if (IntactMesh)
	{
		IntactMesh->SetMassOverrideInKg(NAME_None, GetConfiguredBoxMassKg(), true);
	}
	ConfigureGeometryCollectionMass();
}

bool AWorldDestructibleBox::CanHandleWorldInteraction_Implementation(
	const FWorldInteractionRequest& Request) const
{
	return !bDestroyed && Request.Damage > 0.0f;
}

void AWorldDestructibleBox::HandleWorldInteraction_Implementation(
	const FWorldInteractionRequest& Request)
{
	if (!CanHandleWorldInteraction_Implementation(Request))
	{
		return;
	}

	CurrentHealth = FMath::Max(0.0f, CurrentHealth - Request.Damage);
	OnDamaged.Broadcast(CurrentHealth, Request);
	if (CurrentHealth <= 0.0f)
	{
		BreakBox(Request);
	}
}

void AWorldDestructibleBox::BreakBox(const FWorldInteractionRequest& Request)
{
	if (bDestroyed)
	{
		return;
	}
	bDestroyed = true;
	const FWorldInteractionSettings& Settings = GetSettings();
	const float ScaledBreakImpulse =
		Request.ImpulseStrength * Settings.DestructibleBreakImpulseScale;
	// Radius-zero requests are direct melee hits and need a small separation floor.
	// Radial explosions already carry their own authored strength and must not inherit it.
	const float EffectiveBreakImpulse = Request.Radius <= UE_KINDA_SMALL_NUMBER
		? FMath::Max(ScaledBreakImpulse, Settings.DestructibleMinimumBreakImpulse)
		: ScaledBreakImpulse;

	if (GeometryCollection && GeometryCollection->GetRestCollection())
	{
		const FTransform IntactTransform = IntactMesh->GetComponentTransform();
		PendingInheritedVelocity = IntactMesh->IsSimulatingPhysics()
			? IntactMesh->GetPhysicsLinearVelocity()
			: FVector::ZeroVector;
		PendingInheritedAngularVelocityRadians = IntactMesh->IsSimulatingPhysics()
			? IntactMesh->GetPhysicsAngularVelocityInRadians()
			: FVector::ZeroVector;
		IntactMesh->SetSimulatePhysics(false);
		IntactMesh->SetVisibility(false, true);
		IntactMesh->SetHiddenInGame(true, true);
		IntactMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GeometryCollection->SetWorldTransform(
			IntactTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		BreakTransformTransferError = FVector::Distance(
			GeometryCollection->GetComponentLocation(),
			IntactTransform.GetLocation());
		BreakRotationTransferErrorDegrees = FMath::RadiansToDegrees(
			GeometryCollection->GetComponentQuat().AngularDistance(IntactTransform.GetRotation()));
		const FVector ScaleDelta = GeometryCollection->GetComponentScale() - IntactTransform.GetScale3D();
		BreakScaleTransferError = ScaleDelta.GetAbsMax();

		// Physics enabled immediately; visibility deferred until impulse is ready
		// (Chaos state changes are async - showing the GC before physics bodies
		//  are available produces a visible "frozen debris" frame.)
		GeometryCollection->SetSimulatePhysics(true);
		GeometryCollection->SetEnableGravity(true);
		GeometryCollection->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		InitialDebrisSpread = CalculateDebrisSpread();
		PendingBreakOrigin = Request.Origin;
		PendingBreakDirection = Request.Direction.GetSafeNormal();
		if (PendingBreakDirection.IsNearlyZero())
		{
			PendingBreakDirection = GetActorForwardVector();
		}
		PendingBreakRadius = FMath::Max(Request.Radius, Settings.DestructibleMinimumBreakRadius);
		PendingBreakImpulse = EffectiveBreakImpulse;
		BreakImpulseRetryCount = 0;
		ApplyBreakImpulse();
	}
	else
	{
		IntactMesh->SetSimulatePhysics(true);
		IntactMesh->AddRadialImpulse(
			Request.Origin,
			FMath::Max(Request.Radius, Settings.DestructibleMinimumBreakRadius),
			EffectiveBreakImpulse,
			RIF_Linear,
			Settings.bDestructibleBreakImpulseIgnoresMass);
		bBreakImpulseApplied = true;
	}

	OnDestroyedByInteraction.Broadcast(Request);
	if (Settings.DestructibleDebrisLifetime > 0.0f)
	{
		SetLifeSpan(Settings.DestructibleDebrisLifetime);
	}
}

void AWorldDestructibleBox::ApplyBreakImpulse()
{
	if (!bDestroyed || bBreakImpulseApplied || !GeometryCollection ||
		!GeometryCollection->GetRestCollection())
	{
		return;
	}

	// The hidden collection is pre-warmed in BeginPlay. Retain a short retry only
	// for unusual cases where registration has not exposed its public handles yet.
	if (GeometryCollection->GetAllPhysicsObjects().IsEmpty())
	{
		if (++BreakImpulseRetryCount < MaxBreakImpulseRetries)
		{
			GetWorldTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &AWorldDestructibleBox::ApplyBreakImpulse));
		}
		return;
	}

	const FWorldInteractionSettings& Settings = GetSettings();
	const int32 RootIndex = GeometryCollection->GetRootIndex();
	if (RootIndex != INDEX_NONE)
	{
		GeometryCollection->ApplyExternalStrain(
			RootIndex,
			PendingBreakOrigin,
			PendingBreakRadius,
			Settings.DestructibleStrainPropagationDepth,
			Settings.DestructibleStrainPropagationFactor,
			Settings.DestructibleBreakStrain);
		GeometryCollection->ApplyBreakingLinearVelocity(
			RootIndex,
			PendingInheritedVelocity +
				PendingBreakDirection * Settings.DestructibleDirectionalBreakVelocity);
		GeometryCollection->ApplyBreakingAngularVelocity(
			RootIndex,
			PendingInheritedAngularVelocityRadians);
		bBreakStrainApplied = true;
	}
	GeometryCollection->AddRadialImpulse(
		PendingBreakOrigin,
		PendingBreakRadius,
		PendingBreakImpulse,
		RIF_Linear,
		Settings.bDestructibleBreakImpulseIgnoresMass);

	// Make debris visible only now - impulse is applied, so the first
	// frame the player sees is already in motion (no frozen debris).
	GeometryCollection->SetVisibility(true, true);
	GeometryCollection->SetHiddenInGame(false, true);
	bBreakImpulseApplied = true;
}

void AWorldDestructibleBox::HandleChaosBreak(const FChaosBreakEvent& BreakEvent)
{
	const FWorldInteractionSettings& Settings = GetSettings();
	if (!bDestroyed || BreakEvent.Component != GeometryCollection ||
		SpawnedChaosBreakEffectCount >= Settings.MaxChaosBreakEffectBurstsPerActor ||
		BreakEvent.Velocity.Size() < Settings.ChaosBreakEffectMinSpeed ||
		SpawnedChaosBreakEffectIndices.Contains(BreakEvent.Index))
	{
		return;
	}

	UNiagaraSystem* Effect = Settings.ChaosBreakEffect.LoadSynchronous();
	if (!Effect)
	{
		return;
	}

	FVector Direction = BreakEvent.Velocity.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = PendingBreakDirection;
	}
	const float PieceExtent = BreakEvent.Extents.GetAbsMax();
	const float PieceScale = FMath::Clamp(PieceExtent / 50.0f, 0.35f, 1.5f);
	if (UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		Effect,
		BreakEvent.Location,
		Direction.Rotation(),
		FVector::OneVector * Settings.ChaosBreakEffectScale * PieceScale))
	{
		SpawnedChaosBreakEffectIndices.Add(BreakEvent.Index);
		++SpawnedChaosBreakEffectCount;
	}
}

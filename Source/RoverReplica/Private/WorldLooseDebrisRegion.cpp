#include "WorldLooseDebrisRegion.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "WorldInteractionSubsystem.h"
#include "WorldLooseDebrisConfig.h"

AWorldLooseDebrisRegion::AWorldLooseDebrisRegion()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RegionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RegionBounds"));
	SetRootComponent(RegionBounds);
	RegionBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RegionBounds->SetGenerateOverlapEvents(false);
	RegionBounds->SetHiddenInGame(true);
	RegionBounds->SetBoxExtent(FVector(1400.0f, 1400.0f, 350.0f));

	AmbientDebris = CreateDefaultSubobject<UNiagaraComponent>(TEXT("AmbientDebris"));
	AmbientDebris->SetupAttachment(RegionBounds);
	AmbientDebris->SetAutoActivate(false);
	AmbientDebris->SetAutoDestroy(false);
	AmbientDebris->SetUsingAbsoluteScale(true);
}

void AWorldLooseDebrisRegion::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (RegionBounds)
	{
		RegionBounds->SetBoxExtent(GetResolvedRegionExtent());
	}
}

void AWorldLooseDebrisRegion::BeginPlay()
{
	Super::BeginPlay();
	if (!LooseDebrisConfig)
	{
		LooseDebrisConfig = LoadObject<UWorldLooseDebrisConfig>(
			nullptr,
			TEXT("/Game/PhysicsWorldDemo/LooseDebris/Config/DA_WorldLooseDebrisConfig.DA_WorldLooseDebrisConfig"));
	}

	HandledFieldCount = 0;
	SpawnedBurstCount = 0;
	bInteractionForceActive = false;
	InteractionForceEndTime = 0.0f;
	bAttackWakeActive = false;
	AttackWakeEndTime = 0.0f;
	LastAttackWakeTarget = FVector::ZeroVector;
	LastInteractionForceOrigin = FVector::ZeroVector;
	LastInteractionForceRadius = 0.0f;
	ConfigureAmbientEffect();
	if (UWorldInteractionSubsystem* Subsystem = GetWorld()->GetSubsystem<UWorldInteractionSubsystem>())
	{
		Subsystem->OnLightweightInteractionPublished.AddDynamic(
			this,
			&AWorldLooseDebrisRegion::HandleLightweightInteractionField);
	}
}

void AWorldLooseDebrisRegion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldInteractionSubsystem* Subsystem = World->GetSubsystem<UWorldInteractionSubsystem>())
		{
			Subsystem->OnLightweightInteractionPublished.RemoveDynamic(
				this,
				&AWorldLooseDebrisRegion::HandleLightweightInteractionField);
		}
	}
	if (AmbientDebris)
	{
		AmbientDebris->DeactivateImmediate();
	}
	Super::EndPlay(EndPlayReason);
}

void AWorldLooseDebrisRegion::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	if (!World)
	{
		SetActorTickEnabled(false);
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();
	if (bInteractionForceActive && CurrentTime >= InteractionForceEndTime)
	{
		if (AmbientDebris)
		{
			AmbientDebris->SetVariableFloat(TEXT("User.InteractionForceStrength"), 0.0f);
		}
		bInteractionForceActive = false;
	}
	if (bAttackWakeActive && CurrentTime >= AttackWakeEndTime)
	{
		if (AmbientDebris)
		{
			AmbientDebris->SetVariableFloat(TEXT("User.AttackWakeStrength"), 0.0f);
		}
		bAttackWakeActive = false;
	}
	SetActorTickEnabled(bInteractionForceActive || bAttackWakeActive);
}

const FWorldLooseDebrisSettings& AWorldLooseDebrisRegion::GetSettings() const
{
	static const FWorldLooseDebrisSettings FallbackSettings;
	return LooseDebrisConfig ? LooseDebrisConfig->Settings : FallbackSettings;
}

FVector AWorldLooseDebrisRegion::GetResolvedRegionExtent() const
{
	const FVector Extent = bOverrideRegionExtent
		? OverrideRegionExtent
		: GetSettings().RegionExtent;
	return FVector(
		FMath::Max(100.0f, Extent.X),
		FMath::Max(100.0f, Extent.Y),
		FMath::Max(100.0f, Extent.Z));
}

void AWorldLooseDebrisRegion::ConfigureAmbientEffect()
{
	if (!AmbientDebris || !GetSettings().bEnabled)
	{
		return;
	}
	UNiagaraSystem* AmbientSystem = GetSettings().AmbientEffect.LoadSynchronous();
	if (!AmbientSystem)
	{
		return;
	}

	AmbientDebris->SetAsset(AmbientSystem);
	const FVector Extent = GetResolvedRegionExtent();
	const float AuthoredRadius = FMath::Max(1.0f, GetSettings().AuthoredAmbientRadius);
	AmbientDebris->SetWorldScale3D(FVector(
		Extent.X / AuthoredRadius,
		Extent.Y / AuthoredRadius,
		Extent.Z / AuthoredRadius));
	AmbientDebris->SetVariableVec3(TEXT("User.RegionExtent"), Extent);
	ApplyNiagaraRuntimeParameters(
		*AmbientDebris,
		EWorldLightweightInteractionSource::Movement,
		true);
	AmbientDebris->SetVariablePosition(
		TEXT("User.InteractionForcePosition"),
		GetActorLocation());
	AmbientDebris->SetVariableFloat(TEXT("User.InteractionForceStrength"), 0.0f);
	AmbientDebris->SetVariableFloat(TEXT("User.InteractionForceRadius"), 1.0f);
	AmbientDebris->SetVariableFloat(
		TEXT("User.InteractionForceFalloffExponent"),
		FMath::Clamp(GetSettings().InteractionForceFalloffExponent, 0.01f, 8.0f));
	AmbientDebris->SetVariablePosition(TEXT("User.AttackWakePosition"), GetActorLocation());
	AmbientDebris->SetVariableFloat(TEXT("User.AttackWakeStrength"), 0.0f);
	AmbientDebris->SetVariableFloat(TEXT("User.AttackWakeRadius"), 1.0f);
	AmbientDebris->SetVariableFloat(
		TEXT("User.AttackWakeFalloffExponent"),
		FMath::Clamp(GetSettings().AttackWakeFalloffExponent, 0.01f, 8.0f));
	AmbientDebris->Activate(true);
}

bool AWorldLooseDebrisRegion::IntersectsField(
	const FWorldLightweightInteractionField& Field) const
{
	if (!RegionBounds)
	{
		return false;
	}
	const FBox Bounds = RegionBounds->Bounds.GetBox();
	const FVector ClosestPoint = Bounds.GetClosestPointTo(Field.GetCenter());
	return FVector::DistSquared(ClosestPoint, Field.GetCenter()) <= FMath::Square(Field.Radius);
}

FVector AWorldLooseDebrisRegion::ResolveInteractionSpawnLocation(
	const FWorldLightweightInteractionField& Field,
	bool& bOutGroundProjected) const
{
	bOutGroundProjected = false;
	const FWorldLooseDebrisSettings& Settings = GetSettings();
	const FVector FieldCenter = Field.GetCenter();
	const float SpawnOffset = FMath::Max(0.0f, Settings.GroundSpawnOffset);
	const FVector TraceStart = FieldCenter + FVector(
		0.0f,
		0.0f,
		FMath::Max(0.0f, Settings.GroundTraceUpDistance));
	const FVector TraceEnd = FieldCenter - FVector(
		0.0f,
		0.0f,
		FMath::Max(1.0f, Settings.GroundTraceDownDistance));

	FHitResult GroundHit;
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LooseDebrisGroundProjection), false, this);
	if (const UWorld* World = GetWorld();
		World && World->LineTraceSingleByObjectType(
			GroundHit,
			TraceStart,
			TraceEnd,
			ObjectQueryParams,
			QueryParams))
	{
		bOutGroundProjected = true;
		return GroundHit.ImpactPoint + GroundHit.ImpactNormal.GetSafeNormal() * SpawnOffset;
	}

	// The region is authored at ground height, so this keeps the fallback on the floor
	// instead of leaving interaction particles suspended at weapon or capsule height.
	FVector FallbackLocation = FieldCenter;
	FallbackLocation.Z = GetActorLocation().Z + SpawnOffset;
	return FallbackLocation;
}

void AWorldLooseDebrisRegion::ApplyNiagaraRuntimeParameters(
	UNiagaraComponent& Component,
	const EWorldLightweightInteractionSource SourceType,
	const bool bAmbient) const
{
	const FWorldLooseDebrisSettings& Settings = GetSettings();
	float TotalSpawnRate = Settings.MovementInteractionSpawnRate;
	float SpawnRadius = Settings.MovementSpawnRadius;
	float InitialSpeed = Settings.MovementInitialSpeed;
	if (bAmbient)
	{
		TotalSpawnRate = Settings.AmbientParticleBudget /
			FMath::Max(1.0f, Settings.AmbientParticleLifetime);
		SpawnRadius = FMath::Max(1.0f, Settings.AuthoredAmbientRadius);
		InitialSpeed = 0.0f;
	}
	else
	{
		switch (SourceType)
		{
		case EWorldLightweightInteractionSource::Attack:
			TotalSpawnRate = Settings.AttackInteractionSpawnRate;
			SpawnRadius = Settings.AttackSpawnRadius;
			InitialSpeed = Settings.AttackInitialSpeed;
			break;
		case EWorldLightweightInteractionSource::Jump:
			TotalSpawnRate = Settings.JumpInteractionSpawnRate;
			SpawnRadius = Settings.JumpSpawnRadius;
			InitialSpeed = Settings.JumpInitialSpeed;
			break;
		case EWorldLightweightInteractionSource::Landing:
			TotalSpawnRate = Settings.LandingInteractionSpawnRate;
			SpawnRadius = Settings.LandingSpawnRadius;
			InitialSpeed = Settings.LandingInitialSpeed;
			break;
		case EWorldLightweightInteractionSource::Explosion:
			TotalSpawnRate = Settings.ExplosionInteractionSpawnRate;
			SpawnRadius = Settings.ExplosionSpawnRadius;
			InitialSpeed = Settings.ExplosionInitialSpeed;
			break;
		case EWorldLightweightInteractionSource::Movement:
		default:
			break;
		}
	}

	const float PaperFraction = FMath::Clamp(Settings.PaperParticleFraction, 0.0f, 1.0f);
	const float Lifetime = FMath::Max(
		1.0f,
		bAmbient ? Settings.AmbientParticleLifetime : Settings.InteractionParticleLifetime);
	const FVector InitialVelocityAxis = FVector(
		1.0f,
		0.0f,
		FMath::Clamp(Settings.InitialVelocityUpwardRatio, 0.0f, 2.0f)).GetSafeNormal();
	const float GravityZ = FMath::Min(
		-1.0f,
		bAmbient ? Settings.AmbientGravityZ : Settings.InteractionGravityZ);
	const float DragScale = FMath::Max(0.0f, Settings.InteractionAerodynamicDragScale);
	const float LiftScale = FMath::Clamp(Settings.InteractionLiftContributionScale, 0.0f, 2.0f);

	Component.SetVariableFloat(TEXT("User.LeafSpawnRate"), TotalSpawnRate * (1.0f - PaperFraction));
	Component.SetVariableFloat(TEXT("User.PaperSpawnRate"), TotalSpawnRate * PaperFraction);
	Component.SetVariableFloat(TEXT("User.LifetimeMin"), Lifetime * 0.9f);
	Component.SetVariableFloat(TEXT("User.LifetimeMax"), Lifetime * 1.1f);
	Component.SetVariableFloat(TEXT("User.SpawnRadius"), FMath::Max(1.0f, SpawnRadius));
	Component.SetVariableFloat(TEXT("User.InitialSpeed"), FMath::Max(0.0f, InitialSpeed));
	Component.SetVariableFloat(
		TEXT("User.InitialVelocityConeAngle"),
		FMath::Clamp(Settings.InitialVelocityConeAngle, 0.0f, 89.0f));
	Component.SetVariableVec3(TEXT("User.InitialVelocityAxis"), InitialVelocityAxis);
	Component.SetVariableVec3(TEXT("User.Gravity"), FVector(0.0f, 0.0f, GravityZ));
	Component.SetVariableFloat(
		TEXT("User.LeafAerodynamicDrag"),
		bAmbient ? Settings.AmbientAerodynamicDrag : 1.15f * DragScale);
	Component.SetVariableFloat(
		TEXT("User.PaperAerodynamicDrag"),
		bAmbient ? Settings.AmbientAerodynamicDrag : 1.6f * DragScale);
	Component.SetVariableFloat(
		TEXT("User.RotationalDrag"),
		bAmbient
			? FMath::Max(0.0f, Settings.AmbientRotationalDrag)
			: FMath::Max(0.0f, Settings.InteractionRotationalDrag));
	Component.SetVariableFloat(
		TEXT("User.LeafRotationStrength"),
		bAmbient ? FMath::Max(0.0f, Settings.AmbientLeafRotationStrength) : 0.85f);
	Component.SetVariableFloat(
		TEXT("User.PaperRotationStrength"),
		bAmbient ? FMath::Max(0.0f, Settings.AmbientPaperRotationStrength) : 1.2f);
	Component.SetVariableFloat(
		TEXT("User.LeafLiftContribution"),
		bAmbient ? 0.0f : 0.85f * LiftScale);
	Component.SetVariableFloat(
		TEXT("User.PaperLiftContribution"),
		bAmbient ? 0.0f : 1.4f * LiftScale);
	Component.SetVariableFloat(
		TEXT("User.Restitution"),
		bAmbient
			? FMath::Clamp(Settings.AmbientRestitution, 0.0f, 1.0f)
			: FMath::Clamp(Settings.InteractionRestitution, 0.0f, 1.0f));
	Component.SetVariableFloat(
		TEXT("User.Friction"),
		bAmbient ? 0.72f : FMath::Clamp(Settings.InteractionFriction, 0.0f, 1.0f));
	Component.SetVariableFloat(
		TEXT("User.StaticFriction"),
		bAmbient ? 0.88f : FMath::Clamp(Settings.InteractionStaticFriction, 0.0f, 1.0f));
	Component.SetVariableFloat(
		TEXT("User.BounceFriction"),
		bAmbient ? 0.82f : FMath::Clamp(Settings.InteractionBounceFriction, 0.0f, 1.0f));
	Component.SetVariableFloat(
		TEXT("User.RestStateTime"),
		bAmbient ? 0.35f : FMath::Max(0.0f, Settings.InteractionRestStateTime));
	Component.SetVariableFloat(
		TEXT("User.StaticFrictionEngagementSpeed"),
		bAmbient ? 1.0f : FMath::Max(0.0f, Settings.InteractionStaticFrictionEngagementSpeed));
	Component.SetVariableFloat(
		TEXT("User.RestNormalAlignment"),
		bAmbient ? 0.5f : FMath::Clamp(Settings.InteractionRestNormalAlignment, 0.0f, 1.0f));
	Component.SetVariableFloat(
		TEXT("User.PenetrationBeforeRest"),
		bAmbient ? 1.0f : FMath::Clamp(Settings.InteractionPenetrationBeforeRest, 0.0f, 1.0f));
	Component.SetVariableFloat(
		TEXT("User.RestingCalmingRate"),
		bAmbient
			? FMath::Max(0.0f, Settings.AmbientRestingCalmingRate)
			: FMath::Max(0.0f, Settings.InteractionRestingCalmingRate));
	Component.SetVariableFloat(
		TEXT("User.BouncingCalmingRate"),
		bAmbient
			? FMath::Max(0.0f, Settings.AmbientBouncingCalmingRate)
			: FMath::Max(0.0f, Settings.InteractionBouncingCalmingRate));
}

void AWorldLooseDebrisRegion::HandleLightweightInteractionField(
	const FWorldLightweightInteractionField& Field)
{
	const FWorldLooseDebrisSettings& Settings = GetSettings();
	UWorld* World = GetWorld();
	if (!Settings.bEnabled || !World || !IntersectsField(Field))
	{
		return;
	}

	++HandledFieldCount;
	bool bGroundProjected = false;
	const FVector GroundLocation = ResolveInteractionSpawnLocation(Field, bGroundProjected);
	LastInteractionSpawnLocation = GroundLocation;
	bLastInteractionGroundProjected = bGroundProjected;
	if (!AmbientDebris || !AmbientDebris->IsActive())
	{
		return;
	}

	const float Radius = FMath::Max(
		1.0f,
		Field.Radius * FMath::Max(0.1f, Settings.InteractionForceRadiusScale));
	const FVector HorizontalDirection = Field.Direction.GetSafeNormal2D();
	const FVector DirectionOffset = HorizontalDirection.IsNearlyZero()
		? FVector::ZeroVector
		: HorizontalDirection * Radius *
			FMath::Clamp(Settings.InteractionDirectionalBias, 0.0f, 2.0f);
	const float LiftRatio = Field.UpwardLift /
		FMath::Max(1.0f, FMath::Abs(Field.Strength));
	const float VerticalOffset = FMath::Max(
		FMath::Max(0.0f, Settings.MinimumGroundReleaseOffset),
		Radius * FMath::Clamp(
			LiftRatio * Settings.InteractionUpwardBiasScale,
			0.0f,
			3.0f));
	const float MaxForceOriginOffset = Radius * FMath::Clamp(
		Settings.MaxForceOriginOffsetRatio,
		0.1f,
		0.95f);
	const FVector ForceOffset = (
		DirectionOffset + FVector(0.0f, 0.0f, VerticalOffset))
		.GetClampedToMaxSize(MaxForceOriginOffset);
	const FVector ForceOrigin = GroundLocation - ForceOffset;
	const float RepulsionStrength = -FMath::Max(0.0f, Field.Strength) *
		FMath::Max(0.0f, Settings.InteractionForceScale);
	LastInteractionForceOrigin = ForceOrigin;
	LastInteractionForceRadius = Radius;

	AmbientDebris->SetVariablePosition(TEXT("User.InteractionForcePosition"), ForceOrigin);
	AmbientDebris->SetVariableFloat(TEXT("User.InteractionForceRadius"), Radius);
	AmbientDebris->SetVariableFloat(TEXT("User.InteractionForceStrength"), RepulsionStrength);
	AmbientDebris->SetVariableFloat(
		TEXT("User.InteractionForceFalloffExponent"),
		FMath::Clamp(Settings.InteractionForceFalloffExponent, 0.01f, 8.0f));
	InteractionForceEndTime = World->GetTimeSeconds() + FMath::Max(
		Settings.MinimumInteractionForceDuration,
		Field.Duration);
	bInteractionForceActive = true;

	if (Field.SourceType == EWorldLightweightInteractionSource::Attack)
	{
		FVector WakeDirection = HorizontalDirection;
		if (WakeDirection.IsNearlyZero())
		{
			WakeDirection = (Field.End - Field.Start).GetSafeNormal2D();
		}
		if (WakeDirection.IsNearlyZero())
		{
			WakeDirection = GetActorForwardVector().GetSafeNormal2D();
		}

		const float WakeRadius = Radius * FMath::Max(0.1f, Settings.AttackWakeRadiusScale);
		LastAttackWakeTarget = GroundLocation +
			WakeDirection * Radius * FMath::Clamp(
				Settings.AttackWakeForwardOffsetScale,
				0.0f,
				3.0f) +
			FVector(0.0f, 0.0f, FMath::Max(0.0f, Settings.AttackWakeHeight));
		const float WakeStrength = FMath::Max(0.0f, Field.Strength) *
			FMath::Max(0.0f, Settings.AttackWakeForceScale);

		AmbientDebris->SetVariablePosition(TEXT("User.AttackWakePosition"), LastAttackWakeTarget);
		AmbientDebris->SetVariableFloat(TEXT("User.AttackWakeRadius"), WakeRadius);
		AmbientDebris->SetVariableFloat(TEXT("User.AttackWakeStrength"), WakeStrength);
		AmbientDebris->SetVariableFloat(
			TEXT("User.AttackWakeFalloffExponent"),
			FMath::Clamp(Settings.AttackWakeFalloffExponent, 0.01f, 8.0f));
		AttackWakeEndTime = World->GetTimeSeconds() + FMath::Max(
			FMath::Max(0.01f, Settings.AttackWakeDuration),
			Field.Duration);
		bAttackWakeActive = true;
	}
	SetActorTickEnabled(true);
}

int32 AWorldLooseDebrisRegion::GetActiveBurstSystemCount() const
{
	return 0;
}

int32 AWorldLooseDebrisRegion::GetEmittingInteractionSystemCount() const
{
	return bInteractionForceActive || bAttackWakeActive ? 1 : 0;
}

bool AWorldLooseDebrisRegion::IsAmbientEffectActive() const
{
	return AmbientDebris && AmbientDebris->IsActive();
}

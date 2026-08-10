#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WorldLooseDebrisConfig.generated.h"

class UNiagaraDataChannelAsset;
class UNiagaraSystem;

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FWorldLooseDebrisSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "General")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "General")
	bool bWriteNiagaraDataChannel = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Niagara")
	TSoftObjectPtr<UNiagaraDataChannelAsset> InteractionDataChannel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Niagara|Systems")
	TSoftObjectPtr<UNiagaraSystem> AmbientEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Niagara|Systems")
	TSoftObjectPtr<UNiagaraSystem> MovementEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Niagara|Systems")
	TSoftObjectPtr<UNiagaraSystem> AttackEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Niagara|Systems")
	TSoftObjectPtr<UNiagaraSystem> LandingEffect;

	// Uses the landing Niagara asset by default, but receives independent runtime parameters.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Niagara|Systems")
	TSoftObjectPtr<UNiagaraSystem> JumpEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Niagara|Systems")
	TSoftObjectPtr<UNiagaraSystem> ExplosionEffect;

	// [PLACEHOLDER] World-space half extent authored by BP_LooseDebrisRegion.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region", meta = (ClampMin = "100.0", Units = "cm"))
	FVector RegionExtent = FVector(1400.0f, 1400.0f, 350.0f);

	// Matches the Shape Location radius baked into the generated ambient Niagara asset.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region", meta = (ClampMin = "1.0", Units = "cm"))
	float AuthoredAmbientRadius = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MovementMinSpeed = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "1.0", Units = "cm/s"))
	float MovementReferenceSpeed = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "1.0", Units = "cm"))
	float MovementRadius = 180.0f;

	// [PLACEHOLDER] Visual response units, independent from Chaos impulse units.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float WalkStrength = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float RunStrength = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float MovementUpwardLift = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.01"))
	float MovementFalloffExponent = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", Units = "s"))
	float MovementFieldDuration = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float MovementPublishDistance = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "1.0", Units = "cm"))
	float AttackInteractionRadius = 150.0f;

	// [PLACEHOLDER] Expands the visual field for fast frame-to-frame weapon travel.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.0"))
	float AttackSweepPaddingScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackMaxSweepPadding = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.0"))
	float AttackStrength = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.0"))
	float AttackUpwardLift = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.01"))
	float AttackFalloffExponent = 1.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.0"))
	float AttackSwirlStrength = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.0", Units = "s"))
	float AttackFieldDuration = 0.12f;

	// A localized pull in front of the blade, layered over radial repulsion to
	// make nearby debris follow the attack direction like a short wake.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Wake", meta = (ClampMin = "0.0"))
	float AttackWakeForceScale = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Wake", meta = (ClampMin = "0.1"))
	float AttackWakeRadiusScale = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Wake", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float AttackWakeForwardOffsetScale = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Wake", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackWakeHeight = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Wake", meta = (ClampMin = "0.01", ClampMax = "8.0"))
	float AttackWakeFalloffExponent = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Wake", meta = (ClampMin = "0.01", Units = "s"))
	float AttackWakeDuration = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0", Units = "cm/s"))
	float LandingMinVerticalSpeed = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "1.0", Units = "cm/s"))
	float LandingReferenceSpeed = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "1.0", Units = "cm"))
	float LandingRadius = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float LandingMinStrength = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float LandingMaxStrength = 1100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float LandingUpwardLift = 380.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0", Units = "s"))
	float LandingFieldDuration = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "1.0", Units = "cm"))
	float JumpRadius = 170.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0.0"))
	float JumpStrength = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0.0"))
	float JumpUpwardLift = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0.0", Units = "s"))
	float JumpFieldDuration = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion", meta = (ClampMin = "0.0"))
	float ExplosionStrengthScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion", meta = (ClampMin = "0.0"))
	float ExplosionUpwardLiftScale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion", meta = (ClampMin = "0.0", Units = "s"))
	float ExplosionFieldDuration = 0.25f;

	// Converts the standardized interaction strength into Niagara point-repulsion force.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Force", meta = (ClampMin = "0.0"))
	float InteractionForceScale = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Force", meta = (ClampMin = "0.1"))
	float InteractionForceRadiusScale = 2.0f;

	// Places the repulsion origin behind the interaction direction to bias debris forward.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Force", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float InteractionDirectionalBias = 0.35f;

	// Places the force origin below the ground so the repulsion also lifts settled debris.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Force", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float InteractionUpwardBiasScale = 1.0f;

	// Keeps the force origin below the floor even for fields with little or no
	// authored upward lift, allowing grounded debris to break static contact.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Force", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumGroundReleaseOffset = 45.0f;

	// [PLACEHOLDER] Keeps the shifted force origin inside its own influence
	// radius so grounded particles remain covered even with a strong lift bias.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Force", meta = (ClampMin = "0.1", ClampMax = "0.95"))
	float MaxForceOriginOffsetRatio = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Force", meta = (ClampMin = "0.01", ClampMax = "8.0"))
	float InteractionForceFalloffExponent = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Force", meta = (ClampMin = "0.01", Units = "s"))
	float MinimumInteractionForceDuration = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxFieldsPerFrame = 12;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxMovementFieldsPerSourcePerFrame = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxAttackFieldsPerSourcePerFrame = 1;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MaxPersistentInteractionSystemsPerRegion."))
	int32 MaxActiveBurstSystemsPerRegion = 16;

	// Maximum number of continuously running interaction systems in one region.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "1", ClampMax = "5"))
	int32 MaxPersistentInteractionSystemsPerRegion = 5;

	// Approximate steady-state particle counts. Re-run ConfigurePhysicsWorldLooseDebris.ps1 after editing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "1"))
	int32 AmbientParticleBudget = 450;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "1"))
	int32 MovementParticleBudget = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "1"))
	int32 AttackParticleBudget = 90;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "1"))
	int32 LandingParticleBudget = 90;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "1"))
	int32 ExplosionParticleBudget = 120;

	// Runtime spawn rates for short interaction windows. These are intentionally
	// independent from particle lifetime so a 15-second lifetime does not make events invisible.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Interaction Spawn Rate", meta = (ClampMin = "0.0"))
	float MovementInteractionSpawnRate = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Interaction Spawn Rate", meta = (ClampMin = "0.0"))
	float AttackInteractionSpawnRate = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Interaction Spawn Rate", meta = (ClampMin = "0.0"))
	float JumpInteractionSpawnRate = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Interaction Spawn Rate", meta = (ClampMin = "0.0"))
	float LandingInteractionSpawnRate = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Interaction Spawn Rate", meta = (ClampMin = "0.0"))
	float ExplosionInteractionSpawnRate = 130.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PaperParticleFraction = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "1.0", Units = "s"))
	float AmbientParticleLifetime = 30.0f;

	// Ambient particles also settle onto collision surfaces instead of remaining suspended after spawn.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient|Settling", meta = (ClampMax = "-1.0", Units = "cm/s^2"))
	float AmbientGravityZ = -980.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient|Settling", meta = (ClampMin = "0.0"))
	float AmbientAerodynamicDrag = 0.35f;

	// [PLACEHOLDER] Ambient particles stay awake for later interaction, so their
	// aerodynamic rotation needs enough damping to settle without Niagara sleep.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient|Settling|Rotation", meta = (ClampMin = "0.0"))
	float AmbientRotationalDrag = 8.0f;

	// [PLACEHOLDER] Keep at zero for grounded leaves. Raise slightly only when
	// airborne flutter is more important than a completely still resting pose.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient|Settling|Rotation", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AmbientLeafRotationStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient|Settling|Rotation", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AmbientPaperRotationStrength = 0.0f;

	// [PLACEHOLDER] Quickly removes collision-induced angular jitter while the
	// translational Point Forces remain able to lift the same particle again.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient|Settling|Rotation", meta = (ClampMin = "0.0"))
	float AmbientRestingCalmingRate = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient|Settling|Rotation", meta = (ClampMin = "0.0"))
	float AmbientBouncingCalmingRate = 12.0f;

	// [PLACEHOLDER] Zero prevents gravity/contact micro-bounces from continuously
	// feeding the rotational solver while particles remain interaction-ready.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient|Settling|Ground Contact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AmbientRestitution = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "1.0", Units = "s"))
	float InteractionParticleLifetime = 15.0f;

	// Stops adding response particles after the source goes quiet; existing particles finish their lifetime.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0.05", Units = "s"))
	float InteractionEmissionTail = 0.45f;

	// Interaction effects are projected onto static, movable, or simulated world surfaces before spawning.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Placement", meta = (ClampMin = "0.0", Units = "cm"))
	float GroundTraceUpDistance = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Placement", meta = (ClampMin = "1.0", Units = "cm"))
	float GroundTraceDownDistance = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Placement", meta = (ClampMin = "0.0", Units = "cm"))
	float GroundSpawnOffset = 3.0f;

	// Niagara spawn radii are authored into the generated systems. Re-run the configure script after editing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Placement|Spawn Radius", meta = (ClampMin = "1.0", Units = "cm"))
	float MovementSpawnRadius = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Placement|Spawn Radius", meta = (ClampMin = "1.0", Units = "cm"))
	float AttackSpawnRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Placement|Spawn Radius", meta = (ClampMin = "1.0", Units = "cm"))
	float LandingSpawnRadius = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Placement|Spawn Radius", meta = (ClampMin = "1.0", Units = "cm"))
	float JumpSpawnRadius = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground Placement|Spawn Radius", meta = (ClampMin = "1.0", Units = "cm"))
	float ExplosionSpawnRadius = 100.0f;

	// One-shot spawn velocity. Unlike Wind Force, this does not keep airborne particles aloft indefinitely.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling|Initial Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MovementInitialSpeed = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling|Initial Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AttackInitialSpeed = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling|Initial Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float LandingInitialSpeed = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling|Initial Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float JumpInitialSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling|Initial Push", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ExplosionInitialSpeed = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling|Initial Push", meta = (ClampMin = "0.0", ClampMax = "89.0", Units = "deg"))
	float InitialVelocityConeAngle = 32.0f;

	// Z/X ratio of the local push axis. The system component rotates X into the interaction direction.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling|Initial Push", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float InitialVelocityUpwardRatio = 0.35f;

	// Interaction particles receive one short push, then use these values to settle and sleep on contact.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMax = "-1.0", Units = "cm/s^2"))
	float InteractionGravityZ = -1350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0"))
	float InteractionAerodynamicDragScale = 1.35f;

	// Multiplies the template's aerodynamic lift. Raise for longer gliding, lower for faster landing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float InteractionLiftContributionScale = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0"))
	float InteractionRotationalDrag = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InteractionRestitution = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InteractionFriction = 0.92f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InteractionStaticFriction = 0.98f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InteractionBounceFriction = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0", Units = "s"))
	float InteractionRestStateTime = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0", Units = "cm/s"))
	float InteractionStaticFrictionEngagementSpeed = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InteractionRestNormalAlignment = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InteractionPenetrationBeforeRest = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0"))
	float InteractionRestingCalmingRate = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settling", meta = (ClampMin = "0.0"))
	float InteractionBouncingCalmingRate = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "0.0", Units = "s"))
	float MovementBurstInterval = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "0.0", Units = "s"))
	float AttackBurstInterval = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "0.0", Units = "s"))
	float LandingBurstInterval = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "0.0", Units = "s"))
	float JumpBurstInterval = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "0.0", Units = "s"))
	float ExplosionBurstInterval = 0.12f;

	// Deprecated: interaction systems now keep unit scale so strength cannot expand their spawn footprint.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Interaction Niagara systems always use unit scale."))
	float StrengthToSystemScale = 0.0012f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Interaction Niagara systems always use unit scale."))
	float MinSystemScale = 0.65f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Interaction Niagara systems always use unit scale."))
	float MaxSystemScale = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bDrawDebugFields = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug", meta = (ClampMin = "0.01", Units = "s"))
	float DebugDrawDuration = 0.16f;
};

UCLASS(BlueprintType)
class ROVERREPLICA_API UWorldLooseDebrisConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Incremented by the asset configuration script after one-time migrations.
	// Keeping this separate from Settings lets later generator runs preserve
	// designer-authored tuning instead of reapplying defaults.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loose Debris|Internal", meta = (ClampMin = "0", AdvancedDisplay))
	int32 AssetSchemaVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loose Debris")
	FWorldLooseDebrisSettings Settings;
};

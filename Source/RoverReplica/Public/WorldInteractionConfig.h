#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "WorldInteractionConfig.generated.h"

class AWorldFireballProjectile;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FWorldRopeBridgeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "12"))
	int32 PlankCount = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1.0", Units = "cm"))
	float PlankWidth = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1.0", Units = "cm"))
	float PlankDepth = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1.0", Units = "cm"))
	float PlankHeight = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float PlankGap = 3.0f;

	// A locked chain needs slack; a perfectly straight bridge cannot sag under load.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float BridgeSag = 120.0f;

	// Distance from each end-plank center to its pier-top constraint anchor.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float AnchorExtension = 60.0f;

	// Distance measured inward from each plank side to its endpoint and internal constraint lines.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float AnchorLateralInset = 15.0f;

	// Safety floor for the distance between the two endpoint anchors.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumAnchorSeparation = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supports", meta = (ClampMin = "1.0", Units = "cm"))
	float SupportRadius = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supports", meta = (ClampMin = "1.0", Units = "cm"))
	float SupportHeight = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.1", Units = "kg"))
	float PlankMassKg = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0"))
	float LinearDamping = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0"))
	float AngularDamping = 4.0f;

	// [PLACEHOLDER] Restores the unloaded bridge arc only after every interacting character leaves.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery")
	bool bEnableUnloadedAngularRecovery = true;

	// [PLACEHOLDER] Keeps recovery from fighting a character who has just stepped or jumped off.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0.0", Units = "s"))
	float UnloadedRecoveryDelay = 0.50f;

	// [PLACEHOLDER] Blends the angular drive in instead of snapping the chain to its rest pose.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0.0", Units = "s"))
	float UnloadedRecoveryBlendInTime = 1.00f;

	// [PLACEHOLDER] Acceleration-mode angular correction strength for returning to the unloaded rest pose.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0.0"))
	float UnloadedRecoveryAngularStiffness = 1200.0f;

	// [PLACEHOLDER] Angular velocity damping used while the unloaded bridge returns to rest.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0.0"))
	float UnloadedRecoveryAngularDamping = 75.0f;

	// Maximum recovery angular acceleration; zero means unlimited.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0.0"))
	float UnloadedRecoveryAngularForceLimit = 0.0f;

	// [PLACEHOLDER] Recovery stops inside this angular error so the bridge can sleep naturally.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0.0", Units = "deg"))
	float UnloadedRecoveryRestToleranceDegrees = 2.0f;

	// [PLACEHOLDER] Recovery stops only after residual angular motion is also quiet.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0.0", Units = "deg/s"))
	float UnloadedRecoveryStopAngularSpeedDegrees = 5.0f;

	// [PLACEHOLDER] Horizontal movement becomes a periodic downward footstep impulse.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinimumMovementSpeed = 25.0f;

	// [PLACEHOLDER] Movement impulses reach their authored magnitude at sprint speed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Movement", meta = (ClampMin = "1.0", Units = "cm/s"))
	float MovementReferenceSpeed = 600.0f;

	// [PLACEHOLDER] Impulse at MovementReferenceSpeed, in Chaos kg*cm/s units.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Movement", meta = (ClampMin = "0.0"))
	float MovementImpulseAtReferenceSpeed = 90.0f;

	// [PLACEHOLDER] Approximate cadence used to turn continuous velocity into footfalls.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Movement", meta = (ClampMin = "0.05", Units = "s"))
	float MovementImpulseInterval = 0.20f;

	// Attack-advance Root Motion Sources are not footsteps and must not add periodic load impulses.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Movement")
	bool bSuppressRootMotionMovementImpulses = true;

	// [PLACEHOLDER] Keeps residual attack-advance velocity from becoming a footstep after crossing a plank seam.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Movement", meta = (ClampMin = "0.0", Units = "s"))
	float RootMotionMovementImpulseSuppressionGraceTime = 0.60f;

	// [PLACEHOLDER] Ignores tiny vertical base changes that are not intentional jumps.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Jump", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinimumJumpTakeoffSpeed = 120.0f;

	// [PLACEHOLDER] Fraction of character takeoff momentum transferred to the last plank.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Jump", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JumpTakeoffImpulseScale = 0.004f;

	// [PLACEHOLDER] Keeps an accepted jump visibly readable after its load is distributed across the deck.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Jump", meta = (ClampMin = "0.0"))
	float MinimumJumpTakeoffImpulse = 800.0f;

	// [PLACEHOLDER] Prevents extreme movement values from destabilizing the chain.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Jump", meta = (ClampMin = "0.0"))
	float MaximumJumpTakeoffImpulse = 1200.0f;

	// [PLACEHOLDER] Only real falls create an additional landing impulse.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Landing", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinimumLandingSpeed = 180.0f;

	// [PLACEHOLDER] Fraction of pre-impact character momentum transferred to the landing plank.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Landing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LandingImpulseScale = 0.006f;

	// [PLACEHOLDER] Keeps qualifying landings stronger than takeoff while retaining a stable distributed load.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Landing", meta = (ClampMin = "0.0"))
	float MinimumLandingImpulse = 2200.0f;

	// [PLACEHOLDER] Caps hard landings so constraints cannot receive unbounded impulses.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Landing", meta = (ClampMin = "0.0"))
	float MaximumLandingImpulse = 3200.0f;

	// [PLACEHOLDER] Relative share for the supported plank. Active weights are normalized before use.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Jump and Landing", meta = (ClampMin = "0.01"))
	float JumpLandingImpulseCenterPlankWeight = 0.60f;

	// [PLACEHOLDER] Relative share for each valid plank immediately before/after the supported plank.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Jump and Landing", meta = (ClampMin = "0.0"))
	float JumpLandingImpulseAdjacentPlankWeight = 0.20f;

	// [PLACEHOLDER] Query volume height used to retain falling characters until landing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Load|Detection", meta = (ClampMin = "100.0", Units = "cm"))
	float CharacterTrackingHeight = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float Swing1LimitDegrees = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float Swing2LimitDegrees = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float TwistLimitDegrees = 0.0f;

	// [PLACEHOLDER] Creates a primary and stabilization joint on opposite sides of every seam.
	// Disable only when comparing against the legacy centerline-joint layout.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint")
	bool bUseDualSideInternalConstraints = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint|Projection", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ProjectionLinearAlpha = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint|Projection", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ProjectionAngularAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint|Projection", meta = (ClampMin = "0.0", Units = "cm"))
	float ProjectionLinearTolerance = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint|Projection", meta = (ClampMin = "0.0", Units = "deg"))
	float ProjectionAngularTolerance = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability")
	bool bEnableMassConditioning = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability")
	bool bUseContinuousCollisionDetection = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "1", ClampMax = "255"))
	int32 PositionSolverIterations = 32;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "1", ClampMax = "255"))
	int32 VelocitySolverIterations = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0.0", Units = "deg/s"))
	float MaxAngularVelocityDegrees = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxDepenetrationVelocity = 200.0f;
};

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FWorldSurfaceResponse
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	TEnumAsByte<EPhysicalSurface> SurfaceType = SurfaceType_Default;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TSoftObjectPtr<UNiagaraSystem> ImpactEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "0.01"))
	float ImpactEffectScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TSoftObjectPtr<USoundBase> ImpactSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TSoftObjectPtr<UMaterialInterface> BurnDecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	bool bAllowBurnDecal = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "1.0"))
	FVector BurnDecalSize = FVector(8.0f, 70.0f, 70.0f);
};

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FWorldInteractionSettings
{
	GENERATED_BODY()

	FWorldInteractionSettings();

	// [PLACEHOLDER] One project-level value drives characters, rigid bodies, and Chaos debris.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|World")
	bool bOverrideWorldGravity = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics|World", meta = (ClampMax = "0.0", Units = "cm/s^2"))
	float WorldGravityZ = -980.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rope Bridge")
	FWorldRopeBridgeSettings RopeBridge;

	// [PLACEHOLDER] Fireball values establish a measurable P0 baseline and belong in the DataAsset.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Spawn", meta = (ClampMin = "0.0"))
	float FireballSpawnDistance = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Spawn")
	float FireballSpawnHeight = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Aim", meta = (ClampMin = "100.0"))
	float FireballAimDistance = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Movement", meta = (ClampMin = "1.0"))
	float FireballSpeed = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Movement", meta = (ClampMin = "0.0"))
	float FireballGravityScale = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Collision", meta = (ClampMin = "1.0"))
	float FireballCollisionRadius = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Lifecycle", meta = (ClampMin = "0.1"))
	float FireballLifetime = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Input", meta = (ClampMin = "0.0"))
	float FireballCooldown = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball")
	TSoftClassPtr<AWorldFireballProjectile> FireballClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Feedback")
	TSoftObjectPtr<UNiagaraSystem> FireballEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fireball|Feedback", meta = (ClampMin = "0.01"))
	float FireballEffectScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion|Feedback")
	TSoftObjectPtr<UNiagaraSystem> ExplosionEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion|Feedback", meta = (ClampMin = "0.01"))
	float ExplosionEffectScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion|Feedback")
	TSoftObjectPtr<USoundBase> ExplosionSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion", meta = (ClampMin = "0.0"))
	float ExplosionDamage = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion", meta = (ClampMin = "1.0"))
	float ExplosionRadius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion", meta = (ClampMin = "0.0"))
	float ExplosionImpulseStrength = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumRadialFalloff = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible", meta = (ClampMin = "1.0"))
	float DestructibleBoxMaxHealth = 25.0f;

	// [PLACEHOLDER] Used by crates whose per-instance BoxMassKg override is zero.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Physics", meta = (ClampMin = "0.001", Units = "kg"))
	float DestructibleBoxDefaultMassKg = 80.0f;

	// [PLACEHOLDER] Damping gives the intact rigid body deliberate, heavy movement without changing gravity.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Physics", meta = (ClampMin = "0.0"))
	float DestructibleIntactLinearDamping = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Physics", meta = (ClampMin = "0.0"))
	float DestructibleIntactAngularDamping = 2.0f;

	// [PLACEHOLDER] Debris uses stronger damping so pieces settle instead of sliding or spinning like paper.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Physics", meta = (ClampMin = "0.0"))
	float DestructibleDebrisLinearDamping = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Physics", meta = (ClampMin = "0.0"))
	float DestructibleDebrisAngularDamping = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible", meta = (ClampMin = "0.0"))
	float DestructibleBreakStrain = 500000.0f;

	// [PLACEHOLDER] Propagates a lethal hit through the crate's single root cluster.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Strain", meta = (ClampMin = "0"))
	int32 DestructibleStrainPropagationDepth = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Strain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DestructibleStrainPropagationFactor = 1.0f;

	// [PLACEHOLDER] Initial velocity follows the melee swing/projectile direction after the cluster breaks.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Impulse", meta = (ClampMin = "0.0"))
	float DestructibleDirectionalBreakVelocity = 60.0f;

	// [PLACEHOLDER] Wood consumes only part of the standardized attack/explosion impulse.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Impulse", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DestructibleBreakImpulseScale = 0.03f;

	// [PLACEHOLDER] Direct melee hits have no area radius, so destruction supplies a minimum debris impulse radius.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Impulse", meta = (ClampMin = "1.0"))
	float DestructibleMinimumBreakRadius = 180.0f;

	// [PLACEHOLDER] Guarantees that the finishing melee hit visibly separates the Geometry Collection pieces.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Impulse", meta = (ClampMin = "0.0", DisplayName = "Minimum Direct-Hit Break Impulse"))
	float DestructibleMinimumBreakImpulse = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Impulse")
	bool bDestructibleBreakImpulseIgnoresMass = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible", meta = (ClampMin = "0.0"))
	float DestructibleDebrisLifetime = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Feedback")
	TSoftObjectPtr<UNiagaraSystem> ChaosBreakEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Feedback", meta = (ClampMin = "0.01"))
	float ChaosBreakEffectScale = 1.0f;

	// Filters tiny solver events so one crate cannot flood the scene with secondary effects.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Feedback", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ChaosBreakEffectMinSpeed = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Feedback", meta = (ClampMin = "0"))
	int32 MaxChaosBreakEffectBurstsPerActor = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "0.0"))
	float BurnDecalFadeStartDelay = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "0.0"))
	float BurnDecalFadeDuration = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "0"))
	int32 MaxActiveFeedbackDecals = 32;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	TArray<FWorldSurfaceResponse> SurfaceResponses;
};

UCLASS(BlueprintType)
class ROVERREPLICA_API UWorldInteractionConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction")
	FWorldInteractionSettings Settings;
};

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
struct ROVERREPLICA_API FWorldSurfaceResponse
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	TEnumAsByte<EPhysicalSurface> SurfaceType = SurfaceType_Default;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TSoftObjectPtr<UNiagaraSystem> ImpactEffect;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion|Feedback")
	TSoftObjectPtr<UNiagaraSystem> ExplosionEffect;

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
	float DestructibleBoxDefaultMassKg = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible", meta = (ClampMin = "0.0"))
	float DestructibleBreakStrain = 500000.0f;

	// [PLACEHOLDER] Propagates a lethal hit through the crate's single root cluster.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Strain", meta = (ClampMin = "0"))
	int32 DestructibleStrainPropagationDepth = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Strain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DestructibleStrainPropagationFactor = 1.0f;

	// [PLACEHOLDER] Initial velocity follows the melee swing/projectile direction after the cluster breaks.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Impulse", meta = (ClampMin = "0.0"))
	float DestructibleDirectionalBreakVelocity = 450.0f;

	// [PLACEHOLDER] Direct melee hits have no area radius, so destruction supplies a minimum debris impulse radius.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible", meta = (ClampMin = "1.0"))
	float DestructibleMinimumBreakRadius = 180.0f;

	// [PLACEHOLDER] Guarantees that the finishing melee hit visibly separates the Geometry Collection pieces.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible", meta = (ClampMin = "0.0"))
	float DestructibleMinimumBreakImpulse = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible")
	bool bDestructibleBreakImpulseIgnoresMass = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible", meta = (ClampMin = "0.0"))
	float DestructibleDebrisLifetime = 2.0f;

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

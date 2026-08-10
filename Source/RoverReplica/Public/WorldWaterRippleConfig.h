#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WorldWaterRippleConfig.generated.h"

class UMaterialInterface;
class UNiagaraSystem;

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FWorldWaterRippleSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "General")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Domain", meta = (ClampMin = "256.0", Units = "cm"))
	FVector2D WorldSize = FVector2D(2000.0f, 2000.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Domain", meta = (ClampMin = "100.0", Units = "cm"))
	float RegionVerticalExtent = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Domain", meta = (Units = "cm"))
	float WaterSurfaceZOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "64", ClampMax = "2048"))
	int32 RenderTargetResolution = 512;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "0.004166", ClampMax = "0.05", Units = "s"))
	float FixedStepSeconds = 1.0f / 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxSubstepsPerFrame = 2;

	// [PLACEHOLDER] Tuned for a quiet pond; the runtime clamps the finite-difference
	// coefficients to the stable range for the configured texel dimensions.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "1.0", Units = "cm/s"))
	float WaveSpeed = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "0.0"))
	float DampingPerSecond = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "0.0", ClampMax = "0.49"))
	float EdgeDampingWidth = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation|Materials")
	TSoftObjectPtr<UMaterialInterface> SimulationMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation|Materials")
	TSoftObjectPtr<UMaterialInterface> WaterSurfaceMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Color")
	FLinearColor ShallowWaterColor = FLinearColor(0.16f, 0.42f, 0.46f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Color")
	FLinearColor DeepWaterColor = FLinearColor(0.015f, 0.09f, 0.14f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Color", meta = (ClampMin = "1.0", Units = "cm"))
	float DepthColorDistance = 420.0f;

	// Single Layer Water coefficients use inverse centimeters.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Single Layer Water")
	FLinearColor ScatteringCoefficients = FLinearColor(0.0030f, 0.0060f, 0.0075f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Single Layer Water")
	FLinearColor AbsorptionCoefficients = FLinearColor(0.0015f, 0.0008f, 0.00045f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Single Layer Water", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float PhaseG = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Single Layer Water")
	FLinearColor ColorScaleBehindWater = FLinearColor(0.82f, 0.96f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SurfaceRoughness = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SurfaceOpacity = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Ripple", meta = (ClampMin = "0.0"))
	float RippleNormalStrength = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Wave", meta = (ClampMin = "0.0", Units = "cm"))
	float WpoAmplitude = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Wave", meta = (ClampMin = "0.0"))
	float WpoSpatialFrequency = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Wave")
	float WpoSpeed = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Foam", meta = (ClampMin = "1.0", Units = "cm"))
	float FoamWidth = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Foam", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FoamIntensity = 0.42f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MaxQueuedImpulses = 32;

	// The simulation material exposes eight fixed impulse parameter slots.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxImpulsesPerStep = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (ClampMin = "1.0", Units = "cm"))
	float MinimumImpulseRadius = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (ClampMin = "1.0", Units = "cm"))
	float MaximumImpulseRadius = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (ClampMin = "0.0"))
	float MinimumImpulseStrength = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (ClampMin = "0.01"))
	float MaximumImpulseStrength = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Filtering", meta = (ClampMin = "0.0", Units = "cm"))
	float SurfaceContactTolerance = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Filtering", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumSurfaceProjectionDistance = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Filtering")
	bool bAcceptMovement = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Filtering")
	bool bAcceptJump = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Filtering")
	bool bAcceptLanding = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Filtering")
	bool bAcceptAttack = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Filtering")
	bool bAcceptExplosion = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float MovementStrengthScale = 0.001f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.01"))
	float MovementRadiusScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", Units = "s"))
	float MovementImpulseInterval = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float MovementMinimumTravelDistance = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0.0"))
	float JumpStrengthScale = 0.0015f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0.01"))
	float JumpRadiusScale = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float LandingStrengthScale = 0.0015f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.01"))
	float LandingRadiusScale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.0"))
	float AttackStrengthScale = 0.001f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.01"))
	float AttackRadiusScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.0", Units = "s"))
	float AttackImpulseInterval = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion", meta = (ClampMin = "0.0"))
	float ExplosionStrengthScale = 0.002f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion", meta = (ClampMin = "0.01"))
	float ExplosionRadiusScale = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heavy Interaction", meta = (ClampMin = "0.0"))
	float HeavyInteractionStrengthScale = 0.001f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heavy Interaction", meta = (ClampMin = "1.0", Units = "cm"))
	float HeavyInteractionDefaultRadius = 70.0f;

	// [PLACEHOLDER] WaterAdvanced consumes collision velocity rather than a normalized wave
	// strength. These scales map the project's standardized interaction fields
	// into the Niagara shallow-water collision data channel.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Advanced|Velocity", meta = (ClampMin = "0.0"))
	float AdvancedMovementVelocityScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Advanced|Velocity", meta = (ClampMin = "0.0"))
	float AdvancedJumpVelocityScale = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Advanced|Velocity", meta = (ClampMin = "0.0"))
	float AdvancedLandingVelocityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Advanced|Velocity", meta = (ClampMin = "0.0"))
	float AdvancedAttackVelocityScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Advanced|Velocity", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AdvancedExplosionImpactSpeed = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Advanced|Velocity", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AdvancedMinimumImpactSpeed = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Advanced|Velocity", meta = (ClampMin = "1.0", Units = "cm/s"))
	float AdvancedMaximumImpactSpeed = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Entry", meta = (ClampMin = "0.0", Units = "cm/s"))
	float WaterEntryMinDownwardSpeed = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Entry", meta = (ClampMin = "0.0", Units = "cm"))
	float WaterEntryRearmHeight = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Entry", meta = (ClampMin = "1.0", Units = "cm"))
	float WaterEntryRadius = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Entry", meta = (ClampMin = "0.0"))
	float WaterEntryMinimumStrength = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Entry", meta = (ClampMin = "0.0"))
	float WaterEntrySpeedStrengthScale = 0.00045f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Entry|Niagara")
	TSoftObjectPtr<UNiagaraSystem> WaterEntrySplashEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Entry|Niagara", meta = (ClampMin = "0.0"))
	float WaterEntrySplashBaseScale = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Entry|Niagara", meta = (ClampMin = "0.0"))
	float WaterEntrySplashStrengthScale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Entry|Niagara", meta = (ClampMin = "0.01"))
	float WaterEntrySplashMaximumScale = 2.5f;
};

UCLASS(BlueprintType)
class ROVERREPLICA_API UWorldWaterRippleConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Ripple")
	FWorldWaterRippleSettings Settings;
};

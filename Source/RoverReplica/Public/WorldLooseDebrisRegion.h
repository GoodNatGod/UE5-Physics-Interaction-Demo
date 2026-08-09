#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldInteractionTypes.h"
#include "WorldLooseDebrisRegion.generated.h"

class UBoxComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UWorldLooseDebrisConfig;

UCLASS(BlueprintType, Blueprintable)
class ROVERREPLICA_API AWorldLooseDebrisRegion : public AActor
{
	GENERATED_BODY()

public:
	AWorldLooseDebrisRegion();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	int32 GetHandledFieldCount() const { return HandledFieldCount; }

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	int32 GetSpawnedBurstCount() const { return SpawnedBurstCount; }

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	int32 GetActiveBurstSystemCount() const;

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	int32 GetEmittingInteractionSystemCount() const;

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	bool WasLastInteractionGroundProjected() const { return bLastInteractionGroundProjected; }

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	FVector GetLastInteractionSpawnLocation() const { return LastInteractionSpawnLocation; }

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	bool IsAmbientEffectActive() const;

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	bool IsAttackWakeActive() const { return bAttackWakeActive; }

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	FVector GetLastAttackWakeTarget() const { return LastAttackWakeTarget; }

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	FVector GetLastInteractionForceOrigin() const { return LastInteractionForceOrigin; }

	UFUNCTION(BlueprintPure, Category = "Loose Debris|Debug")
	float GetLastInteractionForceRadius() const { return LastInteractionForceRadius; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loose Debris")
	TObjectPtr<UBoxComponent> RegionBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loose Debris")
	TObjectPtr<UNiagaraComponent> AmbientDebris;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loose Debris|Config")
	TObjectPtr<UWorldLooseDebrisConfig> LooseDebrisConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loose Debris|Region", meta = (ScriptName = "region_extent_override_enabled"))
	bool bOverrideRegionExtent = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loose Debris|Region", meta = (EditCondition = "bOverrideRegionExtent", ClampMin = "100.0", Units = "cm"))
	FVector OverrideRegionExtent = FVector(1400.0f, 1400.0f, 350.0f);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleLightweightInteractionField(const FWorldLightweightInteractionField& Field);

	const struct FWorldLooseDebrisSettings& GetSettings() const;
	FVector GetResolvedRegionExtent() const;
	bool IntersectsField(const FWorldLightweightInteractionField& Field) const;
	FVector ResolveInteractionSpawnLocation(
		const FWorldLightweightInteractionField& Field,
		bool& bOutGroundProjected) const;
	void ApplyNiagaraRuntimeParameters(
		UNiagaraComponent& Component,
		EWorldLightweightInteractionSource SourceType,
		bool bAmbient) const;
	void ConfigureAmbientEffect();

	int32 HandledFieldCount = 0;
	int32 SpawnedBurstCount = 0;
	FVector LastInteractionSpawnLocation = FVector::ZeroVector;
	bool bLastInteractionGroundProjected = false;
	bool bInteractionForceActive = false;
	float InteractionForceEndTime = 0.0f;
	bool bAttackWakeActive = false;
	float AttackWakeEndTime = 0.0f;
	FVector LastAttackWakeTarget = FVector::ZeroVector;
	FVector LastInteractionForceOrigin = FVector::ZeroVector;
	float LastInteractionForceRadius = 0.0f;
};

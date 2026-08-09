#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldInteractionConfig.h"
#include "WorldInteractionTypes.h"
#include "WorldLooseDebrisConfig.h"
#include "WorldInteractionSubsystem.generated.h"

class UDecalComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FWorldInteractionProcessedSignature,
	const FWorldInteractionRequest&,
	Request,
	const FWorldInteractionResult&,
	Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FWorldLightweightInteractionPublishedSignature,
	const FWorldLightweightInteractionField&,
	Field);

UCLASS()
class ROVERREPLICA_API UWorldInteractionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	UFUNCTION(BlueprintCallable, Category = "World Interaction")
	FWorldInteractionResult SubmitWorldInteraction(FWorldInteractionRequest Request);

	UFUNCTION(BlueprintCallable, Category = "World Interaction|Lightweight")
	bool PublishLightweightInteractionField(FWorldLightweightInteractionField Field);

	bool PublishCharacterMovementField(
		AActor* SourceActor,
		const FVector& Start,
		const FVector& End,
		const FVector& Velocity);

	bool PublishWeaponSweepField(
		AActor* SourceActor,
		const FVector& BladeBase,
		const FVector& BladeTip,
		const FVector& Direction,
		float MaxEndpointTravel);

	bool PublishLandingField(
		AActor* SourceActor,
		const FVector& Location,
		float ImpactSpeed);

	bool PublishJumpField(
		AActor* SourceActor,
		const FVector& Location,
		const FVector& SourceVelocity);

	bool PublishExplosionField(const FWorldInteractionRequest& Request);

	UFUNCTION(BlueprintPure, Category = "World Interaction")
	const FWorldInteractionSettings& GetSettings() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Lightweight")
	const FWorldLooseDebrisSettings& GetLooseDebrisSettings() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Physics")
	float GetAppliedWorldGravityZ() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Debug")
	int32 GetProcessedRequestCount() const { return ProcessedRequestCount; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Debug")
	int32 GetDispatchedReceiverCount() const { return DispatchedReceiverCount; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Debug")
	EPhysicalSurface GetLastResolvedSurfaceType() const { return LastResolvedSurfaceType; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Debug")
	FWorldInteractionResult GetLastInteractionResult() const { return LastInteractionResult; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Debug")
	int32 GetActiveFeedbackDecalCount() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Debug")
	int32 GetSpawnedNiagaraSystemCount() const { return SpawnedNiagaraSystemCount; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Lightweight|Debug")
	int32 GetPublishedLightweightFieldCount() const { return PublishedLightweightFieldCount; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Lightweight|Debug")
	int32 GetRejectedLightweightFieldCount() const { return RejectedLightweightFieldCount; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Lightweight|Debug")
	int32 GetDroppedLightweightFieldCount() const { return DroppedLightweightFieldCount; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Lightweight|Debug")
	int32 GetNiagaraDataChannelWriteCount() const { return NiagaraDataChannelWriteCount; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Lightweight|Debug")
	FWorldLightweightInteractionField GetLastLightweightField() const { return LastLightweightField; }

	UFUNCTION(BlueprintCallable, Category = "World Interaction|Debug")
	void ResetDebugStats();

	UPROPERTY(BlueprintAssignable, Category = "World Interaction")
	FWorldInteractionProcessedSignature OnInteractionProcessed;

	UPROPERTY(BlueprintAssignable, Category = "World Interaction|Lightweight")
	FWorldLightweightInteractionPublishedSignature OnLightweightInteractionPublished;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction|Config")
	TObjectPtr<UWorldInteractionConfig> InteractionConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction|Config")
	TObjectPtr<UWorldLooseDebrisConfig> LooseDebrisConfig;

private:
	void ApplyWorldPhysicsSettings();
	void PrepareLightweightFrameBudget();
	bool TryWriteLightweightFieldToDataChannel(const FWorldLightweightInteractionField& Field);
	void DrawLightweightInteractionField(const FWorldLightweightInteractionField& Field) const;
	int32 ResolveLightweightSourceId(const FWorldLightweightInteractionField& Field) const;
	EPhysicalSurface ResolveSurfaceType(const FWorldInteractionRequest& Request) const;
	const FWorldSurfaceResponse* FindSurfaceResponse(EPhysicalSurface SurfaceType) const;
	bool SpawnSurfaceFeedback(const FWorldInteractionRequest& Request, EPhysicalSurface SurfaceType);
	void TrimDecalPool();

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UDecalComponent>> ActiveFeedbackDecals;

	FWorldInteractionSettings FallbackSettings;
	int32 ProcessedRequestCount = 0;
	int32 DispatchedReceiverCount = 0;
	int32 SpawnedNiagaraSystemCount = 0;
	int32 PublishedLightweightFieldCount = 0;
	int32 RejectedLightweightFieldCount = 0;
	int32 DroppedLightweightFieldCount = 0;
	int32 NiagaraDataChannelWriteCount = 0;
	int32 LightweightEventSerial = 0;
	int32 LightweightFieldsThisFrame = 0;
	uint64 LightweightBudgetFrame = MAX_uint64;
	TMap<int32, int32> MovementFieldsBySourceThisFrame;
	TMap<int32, int32> AttackFieldsBySourceThisFrame;
	EPhysicalSurface LastResolvedSurfaceType = SurfaceType_Default;
	FWorldInteractionResult LastInteractionResult;
	FWorldLightweightInteractionField LastLightweightField;
};

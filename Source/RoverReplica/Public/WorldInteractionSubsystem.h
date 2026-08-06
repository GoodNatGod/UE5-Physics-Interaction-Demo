#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldInteractionConfig.h"
#include "WorldInteractionTypes.h"
#include "WorldInteractionSubsystem.generated.h"

class UDecalComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FWorldInteractionProcessedSignature,
	const FWorldInteractionRequest&,
	Request,
	const FWorldInteractionResult&,
	Result);

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

	UFUNCTION(BlueprintPure, Category = "World Interaction")
	const FWorldInteractionSettings& GetSettings() const;

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

	UFUNCTION(BlueprintCallable, Category = "World Interaction|Debug")
	void ResetDebugStats();

	UPROPERTY(BlueprintAssignable, Category = "World Interaction")
	FWorldInteractionProcessedSignature OnInteractionProcessed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction|Config")
	TObjectPtr<UWorldInteractionConfig> InteractionConfig;

private:
	void ApplyWorldPhysicsSettings();
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
	EPhysicalSurface LastResolvedSurfaceType = SurfaceType_Default;
	FWorldInteractionResult LastInteractionResult;
};

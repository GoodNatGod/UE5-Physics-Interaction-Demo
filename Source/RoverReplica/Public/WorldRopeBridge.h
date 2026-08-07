#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldInteractionConfig.h"
#include "WorldRopeBridge.generated.h"

class UMaterialInterface;
class UPhysicalMaterial;
class UBoxComponent;
class ACharacter;
class UPhysicsConstraintComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class ROVERREPLICA_API AWorldRopeBridge : public AActor
{
	GENERATED_BODY()

public:
	AWorldRopeBridge();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Rope Bridge")
	void RebuildBridge();

	UFUNCTION(BlueprintPure, Category = "Rope Bridge")
	FWorldRopeBridgeSettings GetResolvedBridgeSettings() const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	int32 GetGeneratedPlankCount() const { return GeneratedPlanks.Num(); }

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	int32 GetGeneratedConstraintCount() const { return GeneratedConstraints.Num(); }

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	int32 GetValidSupportCount() const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	UStaticMeshComponent* GetPlankComponent(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	UPhysicsConstraintComponent* GetConstraintComponent(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	bool AreAllPlanksSimulatingPhysics() const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	bool HasValidConstraintConfiguration() const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetMaximumPlankLinearSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetMaximumPlankAngularSpeedDegrees() const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetMaximumPlankRestAngularErrorDegrees() const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetPlankRestAngularErrorDegrees(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	FRotator GetPlankNaturalRestRotation(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	bool IsUnloadedRecoveryArmed() const { return bUnloadedRecoveryArmed; }

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetMaximumEndpointPositionError() const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetEndpointPositionError(int32 AnchorIndex) const;

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetMaximumAdjacentPlankDistanceError() const;

	UFUNCTION(BlueprintCallable, Category = "Rope Bridge|Debug")
	bool ApplyImpulseToCenterPlank(const FVector& WorldImpulse);

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	bool IsCharacterSupportedByBridge(const ACharacter* Character) const;

	UFUNCTION(BlueprintCallable, Category = "Rope Bridge|Debug")
	void ResetCharacterResponseDebug();

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetLastMovementImpulseMagnitude() const { return LastMovementImpulseMagnitude; }

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetLastJumpTakeoffImpulseMagnitude() const { return LastJumpTakeoffImpulseMagnitude; }

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	int32 GetLastJumpTakeoffImpulseAffectedPlankCount() const { return LastJumpTakeoffImpulseAffectedPlankCount; }

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	float GetLastLandingImpulseMagnitude() const { return LastLandingImpulseMagnitude; }

	UFUNCTION(BlueprintPure, Category = "Rope Bridge|Debug")
	int32 GetLastLandingImpulseAffectedPlankCount() const { return LastLandingImpulseAffectedPlankCount; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rope Bridge|Config")
	TObjectPtr<UWorldInteractionConfig> InteractionConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rope Bridge|Config")
	bool bOverrideSharedSettings = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rope Bridge|Config", meta = (EditCondition = "bOverrideSharedSettings", ShowOnlyInnerProperties))
	FWorldRopeBridgeSettings OverrideSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rope Bridge|Assets")
	TObjectPtr<UStaticMesh> PlankMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rope Bridge|Assets")
	TObjectPtr<UStaticMesh> SupportMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rope Bridge|Assets")
	TObjectPtr<UMaterialInterface> PlankMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rope Bridge|Assets")
	TObjectPtr<UPhysicalMaterial> PlankPhysicalMaterial;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope Bridge")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope Bridge")
	TObjectPtr<UStaticMeshComponent> LeftSupport;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope Bridge")
	TObjectPtr<UStaticMeshComponent> RightSupport;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope Bridge")
	TObjectPtr<UStaticMeshComponent> LeftSupportSecondary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope Bridge")
	TObjectPtr<UStaticMeshComponent> RightSupportSecondary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope Bridge")
	TObjectPtr<UBoxComponent> CharacterTrackingVolume;

private:
	struct FCharacterContactState
	{
		TWeakObjectPtr<UStaticMeshComponent> PreviousPlank;
		FVector PreviousVelocity = FVector::ZeroVector;
		float PeakAirborneDownwardSpeed = 0.0f;
		float MovementImpulseElapsed = 0.0f;
		int32 PreviousJumpCount = 0;
		bool bWasMoving = false;
		bool bWasFalling = false;
		bool bAcceptedJumpPending = false;
		bool bHasTouchedBridge = false;
	};

	void ClearGeneratedComponents();
	void RefreshGeneratedComponentReferences();
	void RebuildRestState(const FWorldRopeBridgeSettings& Settings);
	void UpdateUnloadedRecovery(
		float DeltaSeconds,
		bool bAnyBridgeInteraction,
		const FWorldRopeBridgeSettings& Settings);
	void ApplyUnloadedRecoveryTorque(
		float Alpha,
		const FWorldRopeBridgeSettings& Settings);
	FQuat GetNaturalRestRotationForPlank(int32 Index) const;
	UStaticMeshComponent* FindBridgePlank(UObject* Component) const;
	void ApplyCharacterImpulse(
		UStaticMeshComponent* Plank,
		const ACharacter* Character,
		float Magnitude);
	int32 ApplyDistributedVerticalCharacterImpulse(
		UStaticMeshComponent* CenterPlank,
		float Magnitude,
		const FWorldRopeBridgeSettings& Settings);
	UStaticMeshComponent* CreatePlank(
		int32 Index,
		const FTransform& RelativeTransform,
		const FWorldRopeBridgeSettings& Settings);
	UPhysicsConstraintComponent* CreateConstraint(
		const FString& Name,
		UStaticMeshComponent* Child,
		UStaticMeshComponent* Parent,
		const FVector& RelativeLocation,
		const FWorldRopeBridgeSettings& Settings,
		bool bEndpointAnchor = false,
		bool bSecondaryConstraint = false,
		int32 AnchorIndex = INDEX_NONE);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> GeneratedPlanks;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPhysicsConstraintComponent>> GeneratedConstraints;

	TArray<FVector> RestPlankLocations;
	TArray<FVector> RestAnchorLocations;
	TArray<float> RestAdjacentPlankDistances;
	TMap<TWeakObjectPtr<ACharacter>, FCharacterContactState> CharacterContactStates;
	bool bUnloadedRecoveryArmed = false;
	float UnloadedRecoveryElapsed = 0.0f;
	float LastMovementImpulseMagnitude = 0.0f;
	float LastJumpTakeoffImpulseMagnitude = 0.0f;
	int32 LastJumpTakeoffImpulseAffectedPlankCount = 0;
	float LastLandingImpulseMagnitude = 0.0f;
	int32 LastLandingImpulseAffectedPlankCount = 0;
	FWorldRopeBridgeSettings FallbackSettings;
};

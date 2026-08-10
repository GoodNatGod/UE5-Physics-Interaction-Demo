#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Physics/Experimental/ChaosEventType.h"
#include "PhysicsInteractable.h"
#include "WorldInteractionConfig.h"
#include "WorldDestructibleBox.generated.h"

class UGeometryCollection;
class UGeometryCollectionComponent;
class UPhysicalMaterial;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FWorldDestructibleDamagedSignature,
	float,
	RemainingHealth,
	const FWorldInteractionRequest&,
	Request);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FWorldDestructibleDestroyedSignature,
	const FWorldInteractionRequest&,
	Request);

UCLASS(Blueprintable)
class ROVERREPLICA_API AWorldDestructibleBox : public AActor, public IPhysicsInteractable
{
	GENERATED_BODY()

public:
	AWorldDestructibleBox();

	virtual bool CanHandleWorldInteraction_Implementation(
		const FWorldInteractionRequest& Request) const override;
	virtual void HandleWorldInteraction_Implementation(
		const FWorldInteractionRequest& Request) override;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	bool IsDestroyed() const { return bDestroyed; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	bool IsBroken() const { return bDestroyed; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	UStaticMeshComponent* GetIntactMesh() const { return IntactMesh; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	UGeometryCollectionComponent* GetGeometryCollection() const { return GeometryCollection; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	bool HasGeometryCollectionAsset() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	bool IsGeometryCollectionActive() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Physics")
	bool IsGeometryCollectionGravityEnabled() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	bool HasAppliedBreakImpulse() const { return bBreakImpulseApplied; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	bool HasAppliedBreakStrain() const { return bBreakStrainApplied; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	int32 GetSpawnedChaosBreakEffectCount() const { return SpawnedChaosBreakEffectCount; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	float GetDebrisExpansionDistance() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	float GetBreakTransformTransferError() const { return BreakTransformTransferError; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	float GetBreakRotationTransferErrorDegrees() const { return BreakRotationTransferErrorDegrees; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Destructible")
	float GetBreakScaleTransferError() const { return BreakScaleTransferError; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Physics")
	bool IsIntactPhysicsSimulating() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Physics")
	bool IsIntactMeshActorRoot() const { return GetRootComponent() == IntactMesh; }

	UFUNCTION(BlueprintCallable, Category = "World Interaction|Physics")
	bool EnsureIntactMeshActorRoot();

	UFUNCTION(BlueprintPure, Category = "World Interaction|Physics")
	float GetIntactPhysicsMassKg() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Physics")
	float GetConfiguredBoxMassKg() const;

	UFUNCTION(BlueprintPure, Category = "World Interaction|Physics")
	float GetGeometryCollectionMassKg() const;

	UFUNCTION(BlueprintCallable, Category = "World Interaction|Physics")
	void SetBoxMassKg(float NewMassKg);

	UPROPERTY(BlueprintAssignable, Category = "World Interaction|Destructible")
	FWorldDestructibleDamagedSignature OnDamaged;

	UPROPERTY(BlueprintAssignable, Category = "World Interaction|Destructible")
	FWorldDestructibleDestroyedSignature OnDestroyedByInteraction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction|Config")
	TObjectPtr<UWorldInteractionConfig> InteractionConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction|Destructible|Assets")
	TSoftObjectPtr<UStaticMesh> IntactMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction|Destructible|Assets")
	TSoftObjectPtr<UGeometryCollection> FracturedGeometryCollectionAsset;

	// Per-instance override. Zero uses DestructibleBoxDefaultMassKg from the shared config.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction|Physics", meta = (ClampMin = "0.0", Units = "kg"))
	float BoxMassKg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction|Physics")
	bool bEnableIntactPhysics = true;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Interaction|Destructible")
	TObjectPtr<UStaticMeshComponent> IntactMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Interaction|Destructible")
	TObjectPtr<UGeometryCollectionComponent> GeometryCollection;

private:
	const FWorldInteractionSettings& GetSettings() const;
	void ConfigureGeometryCollectionMass();
	void BreakBox(const FWorldInteractionRequest& Request);
	void ApplyBreakImpulse();
	float CalculateDebrisSpread() const;

	UFUNCTION()
	void HandleChaosBreak(const FChaosBreakEvent& BreakEvent);

	FWorldInteractionSettings FallbackSettings;
	float CurrentHealth = 0.0f;
	float InitialDebrisSpread = 0.0f;
	FVector PendingBreakOrigin = FVector::ZeroVector;
	FVector PendingBreakDirection = FVector::ForwardVector;
	FVector PendingInheritedVelocity = FVector::ZeroVector;
	FVector PendingInheritedAngularVelocityRadians = FVector::ZeroVector;
	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> GeometryCollectionPhysicalMaterial;
	float GeometryCollectionAssetMassKg = 0.0f;
	float PendingBreakRadius = 0.0f;
	float PendingBreakImpulse = 0.0f;
	float BreakTransformTransferError = 0.0f;
	float BreakRotationTransferErrorDegrees = 0.0f;
	float BreakScaleTransferError = 0.0f;
	int32 BreakImpulseRetryCount = 0;
	int32 SpawnedChaosBreakEffectCount = 0;
	TSet<int32> SpawnedChaosBreakEffectIndices;
	static constexpr int32 MaxBreakImpulseRetries = 8;
	bool bDestroyed = false;
	bool bBreakStrainApplied = false;
	bool bBreakImpulseApplied = false;
};

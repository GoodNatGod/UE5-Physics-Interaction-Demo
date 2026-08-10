#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicsInteractable.h"
#include "WorldWaterRippleConfig.h"
#include "WorldWaterRippleRegion.generated.h"

class ACharacter;
class AWaterBody;
class UBasicShallowWaterSubsystem;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UWaterBodyComponent;

UENUM(BlueprintType)
enum class EWorldWaterRippleImpulseSource : uint8
{
	Movement,
	Landing,
	Attack,
	Explosion,
	WaterEntry,
	Jump,
};

struct FWorldWaterRippleQueuedImpulse
{
	FVector WorldLocation = FVector::ZeroVector;
	float Radius = 0.0f;
	float Strength = 0.0f;
	EWorldWaterRippleImpulseSource SourceType = EWorldWaterRippleImpulseSource::Movement;
	TWeakObjectPtr<AActor> SourceActor;
};

struct FWorldWaterCrossingState
{
	float PreviousSignedDistance = 0.0f;
	bool bEntryArmed = false;
};

struct FWorldWaterSourceRateState
{
	FVector LastMovementLocation = FVector::ZeroVector;
	float LastMovementTime = -BIG_NUMBER;
	float LastAttackTime = -BIG_NUMBER;
	bool bHasMovementSample = false;
};

UCLASS(BlueprintType, Blueprintable)
class ROVERREPLICA_API AWorldWaterRippleRegion : public AActor, public IPhysicsInteractable
{
	GENERATED_BODY()

public:
	AWorldWaterRippleRegion();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual bool CanHandleWorldInteraction_Implementation(
		const FWorldInteractionRequest& Request) const override;
	virtual void HandleWorldInteraction_Implementation(
		const FWorldInteractionRequest& Request) override;

	UFUNCTION(BlueprintCallable, Category = "Water Ripple")
	bool QueueRippleImpulse(
		FVector WorldLocation,
		float Radius,
		float Strength,
		EWorldWaterRippleImpulseSource SourceType,
		AActor* SourceActor = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Water Ripple")
	void ClearRippleSimulation();

	UFUNCTION(BlueprintCallable, Category = "Water Ripple|Debug")
	void ResetDebugStats();

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	bool IsSimulationReady() const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	bool IsUsingWaterAdvancedShallowWater() const
	{
		return bUseWaterAdvancedShallowWater;
	}

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetForwardedAdvancedImpactCount() const
	{
		return ForwardedAdvancedImpactCount;
	}

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	float GetAdvancedGridSize() const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetAdvancedGridResolution() const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	bool IsAdvancedImpactLocationOnTargetWaterBody(FVector WorldLocation) const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	FString GetAdvancedTargetCollisionDebugString() const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	UTextureRenderTarget2D* GetCurrentStateRenderTarget() const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	UTextureRenderTarget2D* GetPreviousStateRenderTarget() const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetRenderTargetResolution() const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetSimulationStepCount() const { return SimulationStepCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetBufferSwapCount() const { return BufferSwapCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetPendingImpulseCount() const { return PendingImpulses.Num(); }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetAcceptedImpulseCount() const { return AcceptedImpulseCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetDroppedImpulseCount() const { return DroppedImpulseCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetHandledLightweightFieldCount() const { return HandledLightweightFieldCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetHandledHeavyInteractionCount() const { return HandledHeavyInteractionCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetMovementImpulseCount() const { return MovementImpulseCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetJumpImpulseCount() const { return JumpImpulseCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetLandingImpulseCount() const { return LandingImpulseCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetAttackImpulseCount() const { return AttackImpulseCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetExplosionImpulseCount() const { return ExplosionImpulseCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetWaterEntryImpulseCount() const { return WaterEntryImpulseCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetSpawnedWaterEntrySplashCount() const { return SpawnedWaterEntrySplashCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	int32 GetSuppressedDuplicateImpulseCount() const { return SuppressedDuplicateImpulseCount; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	FVector GetLastImpulseWorldLocation() const { return LastImpulseWorldLocation; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	float GetLastImpulseStrength() const { return LastImpulseStrength; }

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	float GetWaterSurfaceZ() const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	FVector GetResolvedDomainCenter() const;

	UFUNCTION(BlueprintPure, Category = "Water Ripple|Debug")
	FVector2D GetResolvedDomainWorldSize() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water Ripple")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water Ripple")
	TObjectPtr<UBoxComponent> RegionBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water Ripple")
	TObjectPtr<UStaticMeshComponent> WaterSurfaceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Ripple|Config")
	TObjectPtr<UWorldWaterRippleConfig> WaterRippleConfig;

	// Preferred target for official Water plugin lakes. Use one Region per body so
	// distant lakes keep independent simulation domains and Render Target detail.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water Ripple|Surface")
	TObjectPtr<AWaterBody> TargetWaterBody;

	// Optional fallback for a plain StaticMesh water plane. Ignored when a
	// TargetWaterBody is assigned.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water Ripple|Surface")
	TObjectPtr<AActor> TargetWaterSurfaceActor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Ripple|Surface")
	bool bFitDomainToTargetSurfaceBounds = true;

	// Uses UE5 WaterAdvanced's moving Niagara Grid2D simulation. In this mode
	// the Region only filters and forwards interaction requests; it never
	// replaces the WaterBody material or creates a custom ripple solver.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water Ripple|Surface")
	bool bUseWaterAdvancedShallowWater = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleLightweightInteractionField(const FWorldLightweightInteractionField& Field);

	UFUNCTION()
	void HandleInteractionProcessed(
		const FWorldInteractionRequest& Request,
		const FWorldInteractionResult& Result);

	UFUNCTION()
	void HandleRegionBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleRegionEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	const FWorldWaterRippleSettings& GetSettings() const;
	void ConfigureRegionComponents();
	void InitializeRenderTargetsAndMaterials();
	void ReleaseRenderTargets();
	void RunSimulationStep();
	void UpdateWaterSurfaceMaterial();
	void SpawnWaterEntrySplash(const FVector& WorldLocation, float Strength);
	void UpdateWaterlineCrossings();
	void InitializeWaterCrossingState(AActor* SourceActor, bool bAllowEstimatedCrossing);
	bool GetCapsuleBottomZ(const AActor& SourceActor, float& OutBottomZ) const;
	bool ResolveFieldSurfacePoint(
		const FWorldLightweightInteractionField& Field,
		FVector& OutSurfacePoint) const;
	bool IsSourceActorOverlappingTargetWaterBody(const AActor* SourceActor) const;
	bool ResolveRequestSurfacePoint(
		const FWorldInteractionRequest& Request,
		FVector& OutSurfacePoint) const;
	bool ProjectPointIntoDomain(
		const FVector& WorldPoint,
		float ExpansionRadius,
		FVector& OutSurfacePoint) const;
	bool PassesSourceRateLimit(
		const FWorldLightweightInteractionField& Field,
		const FVector& SurfacePoint);
	bool ProcessHeavyInteraction(const FWorldInteractionRequest& Request);
	bool ForwardAdvancedImpact(
		const FVector& SurfacePoint,
		const FVector& ImpactVelocity,
		float Radius,
		EWorldWaterRippleImpulseSource SourceType,
		AActor* SourceActor);
	FVector ResolveAdvancedImpactVelocity(
		const FWorldLightweightInteractionField& Field,
		EWorldWaterRippleImpulseSource SourceType) const;
	bool WasExplosionQueuedByLightweightPath(const FWorldInteractionRequest& Request) const;
	bool IsRecentHeavyRequest(const FGuid& RequestId) const;
	void RememberHeavyRequest(const FGuid& RequestId);
	FLinearColor GetDomainMaterialParameter() const;
	UBasicShallowWaterSubsystem* ResolveAdvancedShallowWaterSubsystem() const;
	UWaterBodyComponent* ResolveTargetWaterBodyComponent() const;
	UStaticMeshComponent* ResolveTargetWaterSurfaceMesh() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> StateRenderTargets;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SimulationMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WaterSurfaceMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> TargetWaterSurfaceMesh;

	TArray<FWorldWaterRippleQueuedImpulse> PendingImpulses;
	TMap<TWeakObjectPtr<AActor>, FWorldWaterCrossingState> WaterCrossingStates;
	TMap<TWeakObjectPtr<AActor>, FWorldWaterSourceRateState> SourceRateStates;
	TArray<FGuid> RecentHeavyRequestIds;
	FWorldWaterRippleSettings FallbackSettings;
	TWeakObjectPtr<AActor> LastLightweightExplosionSource;
	FVector LastLightweightExplosionOrigin = FVector::ZeroVector;
	uint64 LastLightweightExplosionFrame = MAX_uint64;
	float FixedStepAccumulator = 0.0f;
	int32 CurrentStateIndex = 0;
	int32 SimulationStepCount = 0;
	int32 BufferSwapCount = 0;
	int32 AcceptedImpulseCount = 0;
	int32 DroppedImpulseCount = 0;
	int32 HandledLightweightFieldCount = 0;
	int32 HandledHeavyInteractionCount = 0;
	int32 MovementImpulseCount = 0;
	int32 JumpImpulseCount = 0;
	int32 LandingImpulseCount = 0;
	int32 AttackImpulseCount = 0;
	int32 ExplosionImpulseCount = 0;
	int32 WaterEntryImpulseCount = 0;
	int32 SpawnedWaterEntrySplashCount = 0;
	int32 SuppressedDuplicateImpulseCount = 0;
	int32 ForwardedAdvancedImpactCount = 0;
	FVector LastImpulseWorldLocation = FVector::ZeroVector;
	float LastImpulseStrength = 0.0f;

	static constexpr int32 MaxShaderImpulseSlots = 8;
	static constexpr int32 MaxRememberedHeavyRequests = 32;
};

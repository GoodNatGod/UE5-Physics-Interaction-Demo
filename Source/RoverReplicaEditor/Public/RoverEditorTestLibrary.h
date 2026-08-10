#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RoverEditorTestLibrary.generated.h"

class UAnimInstance;
class UWorldInteractionSubsystem;
class AWorldFireballProjectile;

USTRUCT(BlueprintType)
struct ROVERREPLICAEDITOR_API FRoverGeometryCollectionStructureStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 AssetVersion = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 TransformCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 RigidLeafCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 ClusterCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 RootCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 InternalFaceCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 SimulatableParticleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 ImplicitCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 ConvexHullCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	int32 RigidLeafWithConvexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	bool bHasSingleRootCluster = false;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	bool bHasSimulationData = false;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	bool bHasConvexData = false;

	UPROPERTY(BlueprintReadOnly, Category = "Rover|Editor|Physics World")
	bool bIsExpectedDemoFracture = false;
};

UCLASS()
class ROVERREPLICAEDITOR_API URoverEditorTestLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Test")
	static bool RequestPlayInNewWindow();

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Test", meta = (WorldContext = "WorldContextObject"))
	static bool CaptureGameViewportBitmap(
		const UObject* WorldContextObject,
		const FString& AbsoluteFilename);

	UFUNCTION(BlueprintPure, Category = "Rover|Editor|Test")
	static FName GetCurrentAnimationStateName(
		const UAnimInstance* AnimInstance,
		int32 MachineIndex = 0);

	UFUNCTION(BlueprintPure, Category = "Rover|Editor|Test")
	static FName GetCurrentRelevantAnimationAssetName(
		const UAnimInstance* AnimInstance,
		int32 MachineIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Physics World")
	static bool CreateP0GeometryCollectionAsset(
		const FString& PackagePath = TEXT("/Game/PhysicsWorldDemo/GeometryCollections/GC_P0_WoodenBox"));

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Physics World")
	static bool CreateDemoWoodenCrateFracturedGeometryCollection(
		const FString& PackagePath = TEXT("/Game/PhysicsWorldDemo/GeometryCollections/GC_Demo_WoodenCrate_Fractured"),
		const FString& SourceMeshPath = TEXT("/Game/PhysicsWorldDemo/Meshes/SM_Demo_WoodenCrate"),
		const FString& ExteriorMaterialPath = TEXT("/Game/PhysicsWorldDemo/Materials/M_Demo_WoodCrate"),
		const FString& InteriorMaterialPath = TEXT("/Game/PhysicsWorldDemo/Materials/M_Demo_WoodInterior"));

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Physics World")
	static FRoverGeometryCollectionStructureStats GetGeometryCollectionStructureStats(
		const FString& PackagePath = TEXT("/Game/PhysicsWorldDemo/GeometryCollections/GC_Demo_WoodenCrate_Fractured"));

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Physics World|Niagara")
	static FString DumpNiagaraSystem(
		const FString& SystemPath);

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Physics World|Niagara")
	static bool ConfigurePhysicsWorldNiagaraAssets();

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Physics World|Niagara")
	static bool ConfigurePhysicsWorldLooseDebrisAssets();

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Physics World|Water")
	static bool ConfigurePhysicsWorldWaterAssets();

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Physics World|Water")
	static bool ConfigureRoverWaterAdvancedPhysicsAsset(
		const FString& SkeletalMeshPath = TEXT("/Game/Rover/Character/SK_Rover_Male.SK_Rover_Male"),
		const FString& PhysicsAssetPackagePath = TEXT("/Game/Rover/Character/PHYS_Rover_Male"));

	UFUNCTION(BlueprintPure, Category = "Rover|Editor|Physics World|Water")
	static int32 GetPhysicsAssetBodyCount(
		const FString& PhysicsAssetPath = TEXT("/Game/Rover/Character/PHYS_Rover_Male.PHYS_Rover_Male"));

	UFUNCTION(BlueprintPure, Category = "Rover|Editor|Physics World", meta = (WorldContext = "WorldContextObject"))
	static UWorldInteractionSubsystem* GetWorldInteractionSubsystem(
		const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Physics World")
	static bool TriggerP0FireballImpact(
		AWorldFireballProjectile* Projectile,
		AActor* TargetActor);
};

#include "RoverEditorTestLibrary.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_RelevantAssetPlayerBase.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimationAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PrimitiveComponent.h"
#include "Chaos/ErrorReporter.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "FractureEngineFracturing.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionFactory.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollectionProxyData.h"
#include "Misc/PackageName.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PlayInEditorDataTypes.h"
#include "UnrealEdGlobals.h"
#include "UObject/Package.h"
#include "WorldFireballProjectile.h"
#include "WorldInteractionSubsystem.h"

namespace
{
constexpr int32 DemoWoodenCrateAssetVersion = 2;
constexpr int32 DemoWoodenCrateVoronoiSiteCount = 16;
constexpr int32 DemoWoodenCrateRandomSeed = 20260806;
constexpr float DemoWoodenCrateDamageThreshold = 5000.0f;
constexpr float DemoWoodenCrateInternalSurfacePointSpacing = 100.0f;
const FName DemoWoodenCrateVersionAttribute(TEXT("RoverDemoWoodenCrateFractureVersion"));

FString MakeObjectPath(const FString& PackagePath)
{
	return FString::Printf(
		TEXT("%s.%s"),
		*PackagePath,
		*FPackageName::GetLongPackageAssetName(PackagePath));
}

int32 ResolveStateMachineIndex(const UAnimInstance* AnimInstance, const int32 FallbackIndex)
{
	const int32 RoverLocomotionIndex = AnimInstance->GetStateMachineIndex(TEXT("RoverLocomotion"));
	return RoverLocomotionIndex != INDEX_NONE ? RoverLocomotionIndex : FallbackIndex;
}

void LogGeometryCollectionSimulationData(const UGeometryCollection& Collection)
{
	const FGeometryCollection* CollectionData = Collection.GetGeometryCollection().Get();
	const TManagedArray<bool>* SimulatableParticles = CollectionData
		? CollectionData->FindAttribute<bool>(
			FGeometryCollection::SimulatableParticlesAttribute,
			FTransformCollection::TransformGroup)
		: nullptr;
	int32 SimulatableCount = 0;
	int32 ImplicitCount = 0;
	if (SimulatableParticles)
	{
		for (int32 TransformIndex = 0; TransformIndex < SimulatableParticles->Num(); ++TransformIndex)
		{
			SimulatableCount += (*SimulatableParticles)[TransformIndex] ? 1 : 0;
		}
	}
	const TManagedArray<Chaos::FImplicitObjectPtr>* Implicits = CollectionData
		? CollectionData->FindAttribute<Chaos::FImplicitObjectPtr>(
			FGeometryDynamicCollection::ImplicitsAttribute,
			FTransformCollection::TransformGroup)
		: nullptr;
	if (Implicits)
	{
		for (const Chaos::FImplicitObjectPtr& Implicit : *Implicits)
		{
			ImplicitCount += Implicit.IsValid() ? 1 : 0;
		}
	}
	UE_LOG(
		LogTemp,
		Display,
		TEXT("P0 Geometry Collection simulation data transforms=%d simulatable=%d implicits=%d"),
		CollectionData ? CollectionData->NumElements(FTransformCollection::TransformGroup) : 0,
		SimulatableCount,
		ImplicitCount);
}

FRoverGeometryCollectionStructureStats BuildGeometryCollectionStructureStats(
	const UGeometryCollection* Collection)
{
	FRoverGeometryCollectionStructureStats Stats;
	const FGeometryCollection* CollectionData = Collection
		? Collection->GetGeometryCollection().Get()
		: nullptr;
	if (!CollectionData)
	{
		return Stats;
	}

	Stats.TransformCount = CollectionData->NumElements(FTransformCollection::TransformGroup);
	const TManagedArray<int32>* SimulationTypes = CollectionData->FindAttribute<int32>(
		FGeometryCollection::SimulationTypeAttribute,
		FTransformCollection::TransformGroup);
	const TManagedArray<int32>* Parents = CollectionData->FindAttribute<int32>(
		FTransformCollection::ParentAttribute,
		FTransformCollection::TransformGroup);
	const TManagedArray<TSet<int32>>* Children = CollectionData->FindAttribute<TSet<int32>>(
		FTransformCollection::ChildrenAttribute,
		FTransformCollection::TransformGroup);
	int32 RootIndex = INDEX_NONE;
	if (SimulationTypes && Parents && Children)
	{
		for (int32 TransformIndex = 0; TransformIndex < Stats.TransformCount; ++TransformIndex)
		{
			if ((*Parents)[TransformIndex] == INDEX_NONE)
			{
				++Stats.RootCount;
				RootIndex = TransformIndex;
			}
			if ((*SimulationTypes)[TransformIndex] == FGeometryCollection::FST_Clustered)
			{
				++Stats.ClusterCount;
			}
			else if ((*SimulationTypes)[TransformIndex] == FGeometryCollection::FST_Rigid &&
				(*Children)[TransformIndex].IsEmpty())
			{
				++Stats.RigidLeafCount;
			}
		}
	}
	Stats.bHasSingleRootCluster = Stats.RootCount == 1 &&
		SimulationTypes && SimulationTypes->IsValidIndex(RootIndex) &&
		(*SimulationTypes)[RootIndex] == FGeometryCollection::FST_Clustered;

	if (const TManagedArray<bool>* InternalFaces = CollectionData->FindAttribute<bool>(
		TEXT("Internal"),
		FGeometryCollection::FacesGroup))
	{
		for (int32 FaceIndex = 0; FaceIndex < InternalFaces->Num(); ++FaceIndex)
		{
			Stats.InternalFaceCount += (*InternalFaces)[FaceIndex] ? 1 : 0;
		}
	}

	if (const TManagedArray<int32>* Versions = CollectionData->FindAttribute<int32>(
		DemoWoodenCrateVersionAttribute,
		FTransformCollection::TransformGroup))
	{
		if (Versions->Num() > 0)
		{
			Stats.AssetVersion = (*Versions)[0];
			for (const int32 Version : *Versions)
			{
				if (Version != Stats.AssetVersion)
				{
					Stats.AssetVersion = INDEX_NONE;
					break;
				}
			}
		}
	}

	if (const TManagedArray<bool>* SimulatableParticles = CollectionData->FindAttribute<bool>(
		FGeometryCollection::SimulatableParticlesAttribute,
		FTransformCollection::TransformGroup))
	{
		for (int32 TransformIndex = 0; TransformIndex < SimulatableParticles->Num(); ++TransformIndex)
		{
			Stats.SimulatableParticleCount += (*SimulatableParticles)[TransformIndex] ? 1 : 0;
		}
	}
	if (const TManagedArray<Chaos::FImplicitObjectPtr>* Implicits = CollectionData->FindAttribute<Chaos::FImplicitObjectPtr>(
		FGeometryDynamicCollection::ImplicitsAttribute,
		FTransformCollection::TransformGroup))
	{
		for (const Chaos::FImplicitObjectPtr& Implicit : *Implicits)
		{
			Stats.ImplicitCount += Implicit.IsValid() ? 1 : 0;
		}
	}
	Stats.ConvexHullCount = CollectionData->NumElements(FGeometryCollection::ConvexGroup);
	if (const TManagedArray<TSet<int32>>* TransformToConvexIndices =
		CollectionData->FindAttribute<TSet<int32>>(
			TEXT("TransformToConvexIndices"),
			FTransformCollection::TransformGroup))
	{
		for (int32 TransformIndex = 0; TransformIndex < Stats.TransformCount; ++TransformIndex)
		{
			const bool bIsRigidLeaf = SimulationTypes && Children &&
				(*SimulationTypes)[TransformIndex] == FGeometryCollection::FST_Rigid &&
				(*Children)[TransformIndex].IsEmpty();
			if (bIsRigidLeaf && !(*TransformToConvexIndices)[TransformIndex].IsEmpty())
			{
				++Stats.RigidLeafWithConvexCount;
			}
		}
	}

	Stats.bHasConvexData = Stats.ConvexHullCount > 0 &&
		Stats.RigidLeafWithConvexCount == Stats.RigidLeafCount;
	Stats.bHasSimulationData = Stats.SimulatableParticleCount == Stats.RigidLeafCount &&
		Stats.ImplicitCount >= Stats.RigidLeafCount;
	Stats.bIsExpectedDemoFracture = Stats.AssetVersion == DemoWoodenCrateAssetVersion &&
		Stats.RigidLeafCount >= 12 && Stats.RigidLeafCount <= 20 &&
		Stats.ClusterCount >= 1 && Stats.InternalFaceCount > 0 &&
		Stats.bHasSingleRootCluster && Stats.bHasConvexData;
	return Stats;
}

void LogGeometryCollectionStructureStats(
	const UGeometryCollection& Collection,
	const FRoverGeometryCollectionStructureStats& Stats)
{
	UE_LOG(
		LogTemp,
		Display,
		TEXT("Geometry Collection %s version=%d transforms=%d rigid_leaves=%d clusters=%d roots=%d internal_faces=%d convex_hulls=%d convex_leaves=%d simulatable=%d implicits=%d valid_demo_fracture=%s"),
		*Collection.GetPathName(),
		Stats.AssetVersion,
		Stats.TransformCount,
		Stats.RigidLeafCount,
		Stats.ClusterCount,
		Stats.RootCount,
		Stats.InternalFaceCount,
		Stats.ConvexHullCount,
		Stats.RigidLeafWithConvexCount,
		Stats.SimulatableParticleCount,
		Stats.ImplicitCount,
		Stats.bIsExpectedDemoFracture ? TEXT("true") : TEXT("false"));
}

void BuildGeometryCollectionSimulationData(
	UGeometryCollection& Collection,
	const EImplicitTypeEnum ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box)
{
	FGeometryCollection& CollectionData = *Collection.GetGeometryCollection();
	Collection.SizeSpecificData = {
		UGeometryCollection::GeometryCollectionSizeSpecificDataDefaults()};
	Collection.SizeSpecificData[0].CollisionShapes[0].ImplicitType = ImplicitType;
	FSharedSimulationParameters SharedParameters;
	Collection.GetSharedSimulationParams(SharedParameters);
	Chaos::FErrorReporter ErrorReporter(Collection.GetName());
	BuildSimulationData(ErrorReporter, CollectionData, SharedParameters);
	Collection.CreateSimulationData();
}

void ApplyDemoWoodenCrateAssetSettings(UGeometryCollection& Collection)
{
	Collection.EnableClustering = true;
	Collection.MaxClusterLevel = 1;
	Collection.DamageModel = EDamageModelTypeEnum::Chaos_Damage_Model_UserDefined_Damage_Threshold;
	Collection.DamageThreshold = {DemoWoodenCrateDamageThreshold};
	Collection.bUseSizeSpecificDamageThreshold = false;
	Collection.PerClusterOnlyDamageThreshold = false;
}
}

bool URoverEditorTestLibrary::RequestPlayInNewWindow()
{
	if (!GUnrealEd || GUnrealEd->IsPlaySessionInProgress())
	{
		return false;
	}

	FRequestPlaySessionParams SessionParams;
	SessionParams.SessionDestination = EPlaySessionDestinationType::InProcess;
	SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;
	SessionParams.bAllowOnlineSubsystem = false;
	GUnrealEd->RequestPlaySession(SessionParams);
	return true;
}

FName URoverEditorTestLibrary::GetCurrentAnimationStateName(
	const UAnimInstance* AnimInstance,
	const int32 MachineIndex)
{
	if (!AnimInstance)
	{
		return NAME_None;
	}

	const int32 ResolvedMachineIndex = ResolveStateMachineIndex(AnimInstance, MachineIndex);
	const FAnimNode_StateMachine* StateMachine = AnimInstance->GetStateMachineInstance(ResolvedMachineIndex);
	return StateMachine && StateMachine->GetCurrentState() != INDEX_NONE
		? StateMachine->GetCurrentStateName()
		: NAME_None;
}

FName URoverEditorTestLibrary::GetCurrentRelevantAnimationAssetName(
	const UAnimInstance* AnimInstance,
	const int32 MachineIndex)
{
	if (!AnimInstance)
	{
		return NAME_None;
	}

	const int32 ResolvedMachineIndex = ResolveStateMachineIndex(AnimInstance, MachineIndex);
	const FAnimNode_StateMachine* StateMachine = AnimInstance->GetStateMachineInstance(ResolvedMachineIndex);
	if (!StateMachine || StateMachine->GetCurrentState() == INDEX_NONE)
	{
		return NAME_None;
	}

	const FAnimNode_AssetPlayerRelevancyBase* AssetPlayer =
		AnimInstance->GetRelevantAssetPlayerInterfaceFromState(ResolvedMachineIndex, StateMachine->GetCurrentState());
	const UAnimationAsset* Asset = AssetPlayer ? AssetPlayer->GetAnimAsset() : nullptr;
	return Asset ? Asset->GetFName() : NAME_None;
}

bool URoverEditorTestLibrary::CreateP0GeometryCollectionAsset(const FString& PackagePath)
{
	if (!FPackageName::IsValidLongPackageName(PackagePath))
	{
		return false;
	}
	if (UGeometryCollection* ExistingCollection = LoadObject<UGeometryCollection>(
		nullptr,
		*FString::Printf(
			TEXT("%s.%s"),
			*PackagePath,
			*FPackageName::GetLongPackageAssetName(PackagePath))))
	{
		ExistingCollection->InvalidateCollection();
		BuildGeometryCollectionSimulationData(*ExistingCollection);
		LogGeometryCollectionSimulationData(*ExistingCollection);
		ExistingCollection->MarkPackageDirty();
		return true;
	}

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Cube || !Package)
	{
		return false;
	}

	const FName AssetName(*FPackageName::GetLongPackageAssetName(PackagePath));
	UGeometryCollectionFactory* Factory = NewObject<UGeometryCollectionFactory>();
	UGeometryCollection* Collection = Cast<UGeometryCollection>(Factory->FactoryCreateNew(
		UGeometryCollection::StaticClass(),
		Package,
		AssetName,
		RF_Standalone | RF_Public | RF_Transactional,
		nullptr,
		GWarn));
	if (!Collection)
	{
		return false;
	}

	for (int32 X = -1; X <= 1; X += 2)
	{
		for (int32 Y = -1; Y <= 1; Y += 2)
		{
			for (int32 Z = -1; Z <= 1; Z += 2)
			{
				const FTransform PieceTransform(
					FQuat::Identity,
					FVector(25.0f * X, 25.0f * Y, 25.0f * Z),
					FVector(0.5f));
				FGeometryCollectionEngineConversion::AppendStaticMesh(
					Cube,
					nullptr,
					PieceTransform,
					Collection,
					false);
			}
		}
	}

	Collection->InitializeMaterials(false);
	Collection->InvalidateCollection();
	BuildGeometryCollectionSimulationData(*Collection);
	LogGeometryCollectionSimulationData(*Collection);
	FAssetRegistryModule::AssetCreated(Collection);
	Collection->MarkPackageDirty();
	Package->SetDirtyFlag(true);
	return true;
}

bool URoverEditorTestLibrary::CreateDemoWoodenCrateFracturedGeometryCollection(
	const FString& PackagePath,
	const FString& SourceMeshPath,
	const FString& ExteriorMaterialPath,
	const FString& InteriorMaterialPath)
{
	if (!FPackageName::IsValidLongPackageName(PackagePath))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid demo wooden crate package path: %s"), *PackagePath);
		return false;
	}

	UStaticMesh* SourceMesh = LoadObject<UStaticMesh>(nullptr, *MakeObjectPath(SourceMeshPath));
	UMaterialInterface* ExteriorMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		*MakeObjectPath(ExteriorMaterialPath));
	UMaterialInterface* InteriorMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		*MakeObjectPath(InteriorMaterialPath));
	if (!SourceMesh || !ExteriorMaterial || !InteriorMaterial)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Demo wooden crate source assets are missing mesh=%s exterior=%s interior=%s"),
			*GetNameSafe(SourceMesh),
			*GetNameSafe(ExteriorMaterial),
			*GetNameSafe(InteriorMaterial));
		return false;
	}

	UGeometryCollection* Collection = LoadObject<UGeometryCollection>(nullptr, *MakeObjectPath(PackagePath));
	bool bCreatedAsset = false;
	if (!Collection)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		UGeometryCollectionFactory* Factory = NewObject<UGeometryCollectionFactory>();
		Collection = Package ? Cast<UGeometryCollection>(Factory->FactoryCreateNew(
			UGeometryCollection::StaticClass(),
			Package,
			FName(*FPackageName::GetLongPackageAssetName(PackagePath)),
			RF_Standalone | RF_Public | RF_Transactional,
			nullptr,
			GWarn)) : nullptr;
		bCreatedAsset = Collection != nullptr;
	}
	if (!Collection)
	{
		return false;
	}

	FRoverGeometryCollectionStructureStats ExistingStats = BuildGeometryCollectionStructureStats(Collection);
	if (ExistingStats.bIsExpectedDemoFracture)
	{
		ApplyDemoWoodenCrateAssetSettings(*Collection);
		if (!ExistingStats.bHasSimulationData)
		{
			Collection->InvalidateCollection();
			BuildGeometryCollectionSimulationData(*Collection, EImplicitTypeEnum::Chaos_Implicit_Convex);
			ExistingStats = BuildGeometryCollectionStructureStats(Collection);
		}
		LogGeometryCollectionStructureStats(*Collection, ExistingStats);
		if (!ExistingStats.bHasSimulationData || !ExistingStats.bHasConvexData)
		{
			UE_LOG(LogTemp, Error, TEXT("Existing demo wooden crate has invalid Chaos collision data"));
			return false;
		}
		Collection->MarkPackageDirty();
		return true;
	}

	Collection->Reset();
#if WITH_EDITORONLY_DATA
	Collection->GeometrySource.Reset();
#endif
	TArray<UMaterialInterface*> SourceMaterials{ExteriorMaterial};
	if (!FGeometryCollectionEngineConversion::AppendStaticMesh(
		SourceMesh,
		SourceMaterials,
		FTransform::Identity,
		Collection,
		false,
		true,
		false,
		false))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to append %s into %s"), *SourceMeshPath, *PackagePath);
		return false;
	}

	if (Collection->Materials.Num() < 2)
	{
		Collection->Materials.SetNum(2);
	}
	Collection->Materials[0] = ExteriorMaterial;
	Collection->Materials[1] = InteriorMaterial;

	FGeometryCollection* CollectionData = Collection->GetGeometryCollection().Get();
	if (!CollectionData || CollectionData->NumElements(FTransformCollection::TransformGroup) != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Demo wooden crate must start as exactly one source transform"));
		return false;
	}

	FDataflowTransformSelection SourceSelection;
	SourceSelection.InitializeFromCollection(*CollectionData, false);
	SourceSelection.SetSelected(0);
	FUniformFractureSettings FractureSettings{};
	FractureSettings.Transform = FTransform::Identity;
	FractureSettings.MinVoronoiSites = DemoWoodenCrateVoronoiSiteCount;
	FractureSettings.MaxVoronoiSites = DemoWoodenCrateVoronoiSiteCount;
	FractureSettings.InternalMaterialID = 1;
	FractureSettings.RandomSeed = DemoWoodenCrateRandomSeed;
	FractureSettings.ChanceToFracture = 1.0f;
	FractureSettings.GroupFracture = false;
	FractureSettings.SplitIslands = true;
	FractureSettings.CloseVertexDistance = 1.e-3;
	FractureSettings.VertexToSurfaceBridgeDistance = 0.0;
	FractureSettings.Grout = 0.0f;
	FractureSettings.NoiseSettings.Amplitude = 0.0f;
	FractureSettings.NoiseSettings.PointSpacing = DemoWoodenCrateInternalSurfacePointSpacing;
	FractureSettings.AddSamplesForCollision = false;
	FractureSettings.CollisionSampleSpacing = 10.0f;
	if (FFractureEngineFracturing::UniformFracture(
		*CollectionData,
		SourceSelection,
		FractureSettings) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("Uniform fracture failed for %s"), *PackagePath);
		return false;
	}

	CollectionData = Collection->GetGeometryCollection().Get();
	if (FGeometryCollectionClusteringUtility::ContainsMultipleRootBones(CollectionData))
	{
		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(
			CollectionData,
			TEXT("DemoWoodenCrateRoot"),
			true);
	}
	FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(CollectionData, INDEX_NONE);
	TManagedArray<int32>& Versions = CollectionData->AddAttribute<int32>(
		DemoWoodenCrateVersionAttribute,
		FTransformCollection::TransformGroup);
	for (int32& Version : Versions)
	{
		Version = DemoWoodenCrateAssetVersion;
	}

	Collection->UpdateGeometryDependentProperties();
	Collection->InitializeMaterials(false);
	ApplyDemoWoodenCrateAssetSettings(*Collection);
	Collection->InvalidateCollection();
	Collection->RebuildRenderData();
	BuildGeometryCollectionSimulationData(*Collection, EImplicitTypeEnum::Chaos_Implicit_Convex);

	const FRoverGeometryCollectionStructureStats BuiltStats = BuildGeometryCollectionStructureStats(Collection);
	LogGeometryCollectionStructureStats(*Collection, BuiltStats);
	if (!BuiltStats.bIsExpectedDemoFracture || !BuiltStats.bHasSimulationData)
	{
		UE_LOG(LogTemp, Error, TEXT("Generated demo wooden crate failed structural validation"));
		return false;
	}

	if (bCreatedAsset)
	{
		FAssetRegistryModule::AssetCreated(Collection);
	}
	Collection->MarkPackageDirty();
	Collection->GetOutermost()->SetDirtyFlag(true);
	return true;
}

FRoverGeometryCollectionStructureStats URoverEditorTestLibrary::GetGeometryCollectionStructureStats(
	const FString& PackagePath)
{
	if (!FPackageName::IsValidLongPackageName(PackagePath))
	{
		return {};
	}
	return BuildGeometryCollectionStructureStats(
		LoadObject<UGeometryCollection>(nullptr, *MakeObjectPath(PackagePath)));
}

UWorldInteractionSubsystem* URoverEditorTestLibrary::GetWorldInteractionSubsystem(
	const UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UWorldInteractionSubsystem>() : nullptr;
}

bool URoverEditorTestLibrary::TriggerP0FireballImpact(
	AWorldFireballProjectile* Projectile,
	AActor* TargetActor)
{
	if (!IsValid(Projectile) || !IsValid(TargetActor) || Projectile->HasDetonated() ||
		Projectile->GetWorld() != TargetActor->GetWorld())
	{
		return false;
	}

	UPrimitiveComponent* TargetComponent = TargetActor->FindComponentByClass<UPrimitiveComponent>();
	if (!TargetComponent)
	{
		return false;
	}

	const FVector TraceCenter = TargetComponent->Bounds.Origin;
	const float TraceHalfLength = FMath::Max(200.0f, TargetComponent->Bounds.BoxExtent.Z + 100.0f);
	const FVector TraceStart = TraceCenter + FVector::UpVector * TraceHalfLength;
	const FVector TraceEnd = TraceCenter - FVector::UpVector * TraceHalfLength;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PhysicsWorldP0Impact), true, Projectile);
	QueryParams.bReturnPhysicalMaterial = true;

	FHitResult Hit;
	if (!TargetComponent->LineTraceComponent(Hit, TraceStart, TraceEnd, QueryParams) ||
		Hit.GetActor() != TargetActor)
	{
		return false;
	}

	// DetonateAtHit uses bBlockingHit to choose the impact point, matching the
	// FHitResult delivered by a real OnComponentHit callback.
	Hit.bBlockingHit = true;
	Projectile->DetonateAtHit(Hit);
	return true;
}

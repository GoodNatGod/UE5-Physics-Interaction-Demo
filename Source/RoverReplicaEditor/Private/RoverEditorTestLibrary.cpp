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
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "FractureEngineFracturing.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionFactory.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollectionProxyData.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PlayInEditorDataTypes.h"
#include "UnrealEdGlobals.h"
#include "UObject/Package.h"
#include "UnrealClient.h"
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

UNiagaraSystem* CreateOrLoadNiagaraSystemFromTemplate(
	const FString& DestinationPackagePath,
	const FString& SourcePackagePath)
{
	if (UNiagaraSystem* Existing = LoadObject<UNiagaraSystem>(
		nullptr,
		*MakeObjectPath(DestinationPackagePath)))
	{
		return Existing;
	}

	UNiagaraSystem* Source = LoadObject<UNiagaraSystem>(
		nullptr,
		*MakeObjectPath(SourcePackagePath));
	UPackage* Package = CreatePackage(*DestinationPackagePath);
	if (!Source || !Package)
	{
		return nullptr;
	}

	const FName AssetName(*FPackageName::GetLongPackageAssetName(DestinationPackagePath));
	UNiagaraSystem* System = Cast<UNiagaraSystem>(StaticDuplicateObject(
		Source,
		Package,
		AssetName,
		RF_Public | RF_Standalone | RF_Transactional,
		UNiagaraSystem::StaticClass()));
	if (System)
	{
		FAssetRegistryModule::AssetCreated(System);
		Package->MarkPackageDirty();
	}
	return System;
}

UNiagaraSystem* CreateOrLoadNiagaraSystemFromEmitters(
	const FString& DestinationPackagePath,
	const TArray<FString>& EmitterPackagePaths)
{
	if (UNiagaraSystem* Existing = LoadObject<UNiagaraSystem>(
		nullptr,
		*MakeObjectPath(DestinationPackagePath)))
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*DestinationPackagePath);
	if (!Package)
	{
		return nullptr;
	}
	const FName AssetName(*FPackageName::GetLongPackageAssetName(DestinationPackagePath));
	UNiagaraSystem* System = NewObject<UNiagaraSystem>(
		Package,
		UNiagaraSystem::StaticClass(),
		AssetName,
		RF_Public | RF_Standalone | RF_Transactional);
	UNiagaraSystemFactoryNew::InitializeSystem(System, true);

	for (const FString& EmitterPackagePath : EmitterPackagePaths)
	{
		UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(
			nullptr,
			*MakeObjectPath(EmitterPackagePath));
		if (!Emitter)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Missing Niagara emitter template %s"),
				*EmitterPackagePath);
			return nullptr;
		}
		FNiagaraEditorUtilities::AddEmitterToSystem(
			*System,
			*Emitter,
			Emitter->GetExposedVersion().VersionGuid);
	}

	FAssetRegistryModule::AssetCreated(System);
	Package->MarkPackageDirty();
	return System;
}

void GetAllEmitterScripts(
	const FVersionedNiagaraEmitterData& EmitterData,
	TArray<UNiagaraScript*>& OutScripts)
{
	OutScripts.AddUnique(EmitterData.EmitterSpawnScriptProps.Script);
	OutScripts.AddUnique(EmitterData.EmitterUpdateScriptProps.Script);
	EmitterData.ForEachScript(
		[&OutScripts](UNiagaraScript* Script)
		{
			OutScripts.AddUnique(Script);
		});
	OutScripts.Remove(nullptr);
}

template <typename TValue>
int32 SetRapidIterationValue(
	FVersionedNiagaraEmitterData& EmitterData,
	const FString& ParameterSuffix,
	const TValue& Value)
{
	int32 MatchCount = 0;
	TArray<UNiagaraScript*> Scripts;
	GetAllEmitterScripts(EmitterData, Scripts);
	for (UNiagaraScript* Script : Scripts)
	{
		FNiagaraParameterStore& Store = Script->RapidIterationParameters;
		for (const FNiagaraVariableWithOffset& Variable : Store.ReadParameterVariables())
		{
			if (Variable.GetType() != FNiagaraTypeDefinition::Get<TValue>() ||
				!Variable.GetName().ToString().EndsWith(ParameterSuffix))
			{
				continue;
			}

			const FNiagaraVariable Parameter(Variable.GetType(), Variable.GetName());
			if (Store.SetParameterValue(Value, Parameter))
			{
				++MatchCount;
			}
		}
	}
	return MatchCount;
}

void ConfigureFireballNiagaraSystem(UNiagaraSystem& System)
{
	for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data)
		{
			continue;
		}
		const FString EmitterName = Handle.GetName().ToString();
		const bool bCore = EmitterName.Contains(TEXT("SingleLoopingParticle"));
		Data->bLocalSpace = bCore;
		if (bCore)
		{
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Color"), FLinearColor(6.0f, 0.65f, 0.035f, 1.0f));
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime"), 1.0f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size"), 34.0f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Min"), 28.0f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Max"), 38.0f);
		}
		else
		{
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Color"), FLinearColor(3.5f, 0.22f, 0.012f, 0.9f));
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Min"), 0.08f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Max"), 0.22f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Min"), 4.0f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Max"), 13.0f);
			SetRapidIterationValue(*Data, TEXT(".SpawnRate.SpawnRate"), 95.0f);
			SetRapidIterationValue(*Data, TEXT(".AddVelocity.Cone Axis"), FVector3f(-1.0f, 0.0f, 0.0f));
			SetRapidIterationValue(*Data, TEXT(".AddVelocity.Velocity Speed Scale"), 0.18f);
			SetRapidIterationValue(*Data, TEXT(".GravityForce.Gravity"), FVector3f(0.0f, 0.0f, 90.0f));
			SetRapidIterationValue(*Data, TEXT(".Drag.Drag"), 4.0f);
		}
	}
}

void ConfigureExplosionNiagaraSystem(UNiagaraSystem& System)
{
	for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data)
		{
			continue;
		}
		const FString Name = Handle.GetName().ToString();
		if (Name.Contains(TEXT("OmnidirectionalBurst")))
		{
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Color"), FLinearColor(5.0f, 0.32f, 0.015f, 1.0f));
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Min"), 0.22f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Max"), 0.72f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Min"), 2.0f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Max"), 7.0f);
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat.Minimum"), 160.0f);
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat.Maximum"), 760.0f);
			SetRapidIterationValue(*Data, TEXT(".SpawnBurst_Instantaneous.Spawn Count"), 36);
		}
		else if (Name.Contains(TEXT("UpwardMeshBurst")))
		{
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Color"), FLinearColor(1.4f, 0.12f, 0.012f, 1.0f));
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Min"), 0.35f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Max"), 1.05f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Mesh Uniform Scale Min"), 0.25f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Mesh Uniform Scale Max"), 0.8f);
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat.Minimum"), 80.0f);
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat.Maximum"), 430.0f);
			SetRapidIterationValue(*Data, TEXT(".SpawnBurst_Instantaneous.Spawn Count"), 12);
		}
		else if (Name.Contains(TEXT("SimpleSpriteBurst")))
		{
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Color"), FLinearColor(1.2f, 0.12f, 0.008f, 0.58f));
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime"), 0.42f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Min"), 58.0f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Max"), 125.0f);
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat001.Minimum"), 15.0f);
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat001.Maximum"), 85.0f);
			SetRapidIterationValue(*Data, TEXT(".ShapeLocation.Sphere Radius"), 12.0f);
			SetRapidIterationValue(*Data, TEXT(".SpawnBurst_Instantaneous.Spawn Count"), 4);
		}
	}
}

void ConfigureDirectionalImpactNiagaraSystem(
	UNiagaraSystem& System,
	const bool bChaosBreak)
{
	for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data)
		{
			continue;
		}
		const FString Name = Handle.GetName().ToString();
		if (Name.Contains(TEXT("LocationBasedRibbon")))
		{
			if (bChaosBreak)
			{
				Handle.SetIsEnabled(false, System, false);
				continue;
			}
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Color"), FLinearColor(2.2f, 0.28f, 0.025f, 0.78f));
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime"), 0.24f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Ribbon Width"), 0.65f);
			continue;
		}
		if (!Name.Contains(TEXT("DirectionalBurst")))
		{
			continue;
		}

		const FLinearColor Color = bChaosBreak
			? FLinearColor(0.48f, 0.095f, 0.012f, 1.0f)
			: FLinearColor(1.8f, 0.22f, 0.018f, 1.0f);
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Color"), Color);
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Min"), bChaosBreak ? 0.3f : 0.18f);
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Max"), bChaosBreak ? 0.8f : 0.55f);
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Sprite Size Min"), bChaosBreak ? FVector2f(3.0f, 7.0f) : FVector2f(1.5f, 5.0f));
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Sprite Size Max"), bChaosBreak ? FVector2f(5.0f, 14.0f) : FVector2f(3.0f, 11.0f));
		SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat002.Minimum"), bChaosBreak ? 90.0f : 220.0f);
		SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat002.Maximum"), bChaosBreak ? 360.0f : 680.0f);
		SetRapidIterationValue(*Data, TEXT(".AddVelocityInCone.Cone Angle"), bChaosBreak ? 72.0f : 52.0f);
		SetRapidIterationValue(*Data, TEXT(".AddVelocityInCone.Cone Axis"), FVector3f(0.95f, 0.0f, 0.3f));
		SetRapidIterationValue(*Data, TEXT(".SpawnBurst_Instantaneous.Spawn Count"), bChaosBreak ? 5 : 14);
	}
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

bool URoverEditorTestLibrary::CaptureGameViewportBitmap(
	const UObject* WorldContextObject,
	const FString& AbsoluteFilename)
{
	if (!GEngine
		|| !WorldContextObject
		|| AbsoluteFilename.IsEmpty()
		|| FPaths::IsRelative(AbsoluteFilename)
		|| !AbsoluteFilename.EndsWith(TEXT(".bmp"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(
		WorldContextObject,
		EGetWorldErrorMode::ReturnNull);
	UGameViewportClient* GameViewport = World ? World->GetGameViewport() : nullptr;
	FViewport* Viewport = GameViewport ? GameViewport->Viewport : nullptr;
	if (!Viewport)
	{
		return false;
	}

	const FIntPoint Size = Viewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}

	TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(true);
	if (!GetViewportScreenShot(
		Viewport,
		Pixels,
		FIntRect(0, 0, Size.X, Size.Y),
		ReadFlags)
		|| Pixels.Num() != Size.X * Size.Y)
	{
		return false;
	}

	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}

	const FString Directory = FPaths::GetPath(AbsoluteFilename);
	if (!Directory.IsEmpty() && !IFileManager::Get().MakeDirectory(*Directory, true))
	{
		return false;
	}

	return FFileHelper::CreateBitmap(
		*AbsoluteFilename,
		Size.X,
		Size.Y,
		Pixels.GetData());
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

FString URoverEditorTestLibrary::DumpNiagaraSystem(const FString& SystemPath)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *MakeObjectPath(SystemPath));
	if (!System)
	{
		return FString::Printf(TEXT("ERROR missing Niagara system %s"), *SystemPath);
	}

	TArray<FString> Lines;
	Lines.Add(FString::Printf(
		TEXT("SYSTEM %s exposed={%s}"),
		*System->GetPathName(),
		*System->GetExposedParameters().ToString()));
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		Lines.Add(FString::Printf(
			TEXT("EMITTER name=%s unique=%s enabled=%s mode=%d renderers=%d"),
			*Handle.GetName().ToString(),
			*Handle.GetUniqueInstanceName(),
			Handle.GetIsEnabled() ? TEXT("true") : TEXT("false"),
			static_cast<int32>(Handle.GetEmitterMode()),
			EmitterData ? EmitterData->GetRenderers().Num() : 0));
		if (!EmitterData)
		{
			continue;
		}

		for (const UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
		{
			Lines.Add(FString::Printf(
				TEXT("  RENDERER class=%s enabled=%s"),
				Renderer ? *Renderer->GetClass()->GetName() : TEXT("None"),
				Renderer && Renderer->GetIsEnabled() ? TEXT("true") : TEXT("false")));
		}

		TArray<UNiagaraScript*> Scripts;
		GetAllEmitterScripts(*EmitterData, Scripts);
		for (UNiagaraScript* Script : Scripts)
		{
			Lines.Add(FString::Printf(
				TEXT("  SCRIPT usage=%d name=%s rapid={%s}"),
				static_cast<int32>(Script->GetUsage()),
				*Script->GetName(),
				*Script->RapidIterationParameters.ToString()));
		}
	}

	const FString Result = FString::Join(Lines, TEXT("\n"));
	UE_LOG(LogTemp, Display, TEXT("NIAGARA_SYSTEM_DUMP\n%s"), *Result);
	return Result;
}

bool URoverEditorTestLibrary::ConfigurePhysicsWorldNiagaraAssets()
{
	UNiagaraSystem* Fireball = CreateOrLoadNiagaraSystemFromEmitters(
		TEXT("/Game/PhysicsWorldDemo/Niagara/NS_PW_Fireball"),
		{
			TEXT("/Niagara/DefaultAssets/Templates/Emitters/SingleLoopingParticle"),
			TEXT("/Niagara/DefaultAssets/Templates/Emitters/Fountain"),
		});
	UNiagaraSystem* Explosion = CreateOrLoadNiagaraSystemFromTemplate(
		TEXT("/Game/PhysicsWorldDemo/Niagara/NS_PW_Explosion"),
		TEXT("/Niagara/DefaultAssets/Templates/Systems/SimpleExplosion"));
	UNiagaraSystem* SurfaceImpact = CreateOrLoadNiagaraSystemFromTemplate(
		TEXT("/Game/PhysicsWorldDemo/Niagara/NS_PW_SurfaceImpact"),
		TEXT("/Niagara/DefaultAssets/Templates/Systems/DirectionalBurst"));
	UNiagaraSystem* ChaosBreak = CreateOrLoadNiagaraSystemFromTemplate(
		TEXT("/Game/PhysicsWorldDemo/Niagara/NS_PW_ChaosBreak"),
		TEXT("/Niagara/DefaultAssets/Templates/Systems/DirectionalBurst"));
	if (!Fireball || !Explosion || !SurfaceImpact || !ChaosBreak)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to create all Physics World Niagara assets"));
		return false;
	}

	ConfigureFireballNiagaraSystem(*Fireball);
	ConfigureExplosionNiagaraSystem(*Explosion);
	ConfigureDirectionalImpactNiagaraSystem(*SurfaceImpact, false);
	ConfigureDirectionalImpactNiagaraSystem(*ChaosBreak, true);

	for (UNiagaraSystem* System : {Fireball, Explosion, SurfaceImpact, ChaosBreak})
	{
		System->RequestCompile(false);
		System->MarkPackageDirty();
		System->GetOutermost()->SetDirtyFlag(true);
	}
	for (UNiagaraSystem* System : {Fireball, Explosion, SurfaceImpact, ChaosBreak})
	{
		System->WaitForCompilationComplete();
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("PHYSICS_WORLD_NIAGARA_ASSETS_OK fireball=%s explosion=%s impact=%s chaos_break=%s"),
		*Fireball->GetPathName(),
		*Explosion->GetPathName(),
		*SurfaceImpact->GetPathName(),
		*ChaosBreak->GetPathName());
	return true;
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

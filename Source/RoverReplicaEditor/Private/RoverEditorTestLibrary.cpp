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
#include "Engine/SkeletalMesh.h"
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
#include "NiagaraConstants.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelAsset.h"
#include "NiagaraDataChannel_Global.h"
#include "NiagaraDataChannelVariable.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEffectType.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PlayInEditorDataTypes.h"
#include "UnrealEdGlobals.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "WorldFireballProjectile.h"
#include "WorldInteractionSubsystem.h"
#include "WorldLooseDebrisConfig.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace
{
constexpr int32 DemoWoodenCrateAssetVersion = 2;
constexpr int32 DemoWoodenCrateVoronoiSiteCount = 16;
constexpr int32 DemoWoodenCrateMinimumRigidLeafCount = 12;
// Authored plank meshes can split each Voronoi cell into several disconnected islands.
constexpr int32 DemoWoodenCrateMaximumRigidLeafCount = 64;
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

UNiagaraEffectType* CreateOrLoadNiagaraEffectType(const FString& PackagePath)
{
	if (UNiagaraEffectType* Existing = LoadObject<UNiagaraEffectType>(
		nullptr,
		*MakeObjectPath(PackagePath)))
	{
		return Existing;
	}
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return nullptr;
	}
	UNiagaraEffectType* EffectType = NewObject<UNiagaraEffectType>(
		Package,
		UNiagaraEffectType::StaticClass(),
		FName(*FPackageName::GetLongPackageAssetName(PackagePath)),
		RF_Public | RF_Standalone | RF_Transactional);
	if (EffectType)
	{
		FAssetRegistryModule::AssetCreated(EffectType);
		Package->MarkPackageDirty();
	}
	return EffectType;
}

UNiagaraDataChannelAsset* CreateOrLoadLooseDebrisDataChannel(const FString& PackagePath)
{
	UNiagaraDataChannelAsset* Asset = LoadObject<UNiagaraDataChannelAsset>(
		nullptr,
		*MakeObjectPath(PackagePath));
	if (!Asset)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			return nullptr;
		}
		Asset = NewObject<UNiagaraDataChannelAsset>(
			Package,
			UNiagaraDataChannelAsset::StaticClass(),
			FName(*FPackageName::GetLongPackageAssetName(PackagePath)),
			RF_Public | RF_Standalone | RF_Transactional);
		if (!Asset)
		{
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Asset);
		Package->MarkPackageDirty();
	}

	UNiagaraDataChannel_Global* Channel = Cast<UNiagaraDataChannel_Global>(Asset->Get());
	if (!Channel)
	{
		Channel = NewObject<UNiagaraDataChannel_Global>(
			Asset,
			UNiagaraDataChannel_Global::StaticClass(),
			TEXT("LooseDebrisGlobalChannel"),
			RF_Transactional);
		FObjectProperty* DataChannelProperty = FindFProperty<FObjectProperty>(
			UNiagaraDataChannelAsset::StaticClass(),
			TEXT("DataChannel"));
		if (!DataChannelProperty || !Channel)
		{
			return nullptr;
		}
		DataChannelProperty->SetObjectPropertyValue_InContainer(Asset, Channel);
	}

	FArrayProperty* VariablesProperty = FindFProperty<FArrayProperty>(
		UNiagaraDataChannel::StaticClass(),
		TEXT("ChannelVariables"));
	if (!VariablesProperty)
	{
		return nullptr;
	}
	FScriptArrayHelper Variables(VariablesProperty, VariablesProperty->ContainerPtrToValuePtr<void>(Channel));
	Variables.EmptyValues();
	const auto AddVariable = [&Variables](const FNiagaraTypeDefinition& Type, const FName Name)
	{
		const int32 Index = Variables.AddValue();
		FNiagaraDataChannelVariable* Variable =
			reinterpret_cast<FNiagaraDataChannelVariable*>(Variables.GetRawPtr(Index));
		Variable->SetType(Type);
		Variable->SetName(Name);
	};
	AddVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("EventId"));
	AddVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("SourceId"));
	AddVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("SourceType"));
	AddVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ShapeType"));
	AddVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Start"));
	AddVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("End"));
	AddVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Direction"));
	AddVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SourceVelocity"));
	AddVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Radius"));
	AddVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strength"));
	AddVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("UpwardLift"));
	AddVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Duration"));
	AddVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("FalloffExponent"));
	AddVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SwirlStrength"));

	if (FBoolProperty* KeepPreviousProperty = FindFProperty<FBoolProperty>(
		UNiagaraDataChannel::StaticClass(),
		TEXT("bKeepPreviousFrameData")))
	{
		KeepPreviousProperty->SetPropertyValue_InContainer(Channel, true);
	}
	Channel->PostEditChange();
	Asset->PostEditChange();
	Asset->MarkPackageDirty();
	Asset->GetOutermost()->SetDirtyFlag(true);
	return Asset;
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

template <typename TValue>
void SetNiagaraUserParameter(
	UNiagaraSystem& System,
	const TCHAR* ParameterName,
	const TValue& Value)
{
	const FNiagaraVariable Parameter(
		FNiagaraTypeDefinition::Get<TValue>(),
		FName(*FString::Printf(TEXT("User.%s"), ParameterName)));
	System.GetExposedParameters().SetParameterValue(Value, Parameter, true);
}

void SetNiagaraUserPositionParameter(
	UNiagaraSystem& System,
	const TCHAR* ParameterName,
	const FVector& Value)
{
	System.GetExposedParameters().SetPositionParameterValue(
		Value,
		FName(*FString::Printf(TEXT("User.%s"), ParameterName)),
		true);
}

bool BindNiagaraModuleInputToUserParameter(
	UNiagaraSystem& System,
	FVersionedNiagaraEmitterData& EmitterData,
	const TCHAR* ModuleToken,
	const TCHAR* InputName,
	const FNiagaraTypeDefinition& InputType,
	const TCHAR* UserParameterName,
	UNiagaraNodeFunctionCall* ExplicitFunctionCall = nullptr)
{
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData.GraphSource);
	if (!Source || !Source->NodeGraph)
	{
		return false;
	}

	TArray<UNiagaraNodeFunctionCall*> FunctionCalls;
	Source->NodeGraph->GetNodesOfClass(FunctionCalls);
	UNiagaraNodeFunctionCall* FunctionCall = ExplicitFunctionCall;
	for (UNiagaraNodeFunctionCall* Node : FunctionCalls)
	{
		if (FunctionCall)
		{
			break;
		}
		if (!Node || !Node->FunctionScript)
		{
			continue;
		}
		const FString Token(ModuleToken);
		if (Node->GetFunctionName().Equals(Token) ||
			Node->FunctionScript->GetName().Equals(Token) ||
			Node->FunctionScript->GetPathName().Contains(Token) ||
			Node->FunctionScript->GetPathName().Contains(
				FString::Printf(TEXT("/%s.%s"), ModuleToken, ModuleToken)))
		{
			FunctionCall = Node;
			break;
		}
	}
	if (!FunctionCall)
	{
		return false;
	}

	const FNiagaraParameterHandle ModuleInput =
		FNiagaraParameterHandle::CreateModuleParameterHandle(FName(InputName));
	const FNiagaraParameterHandle AliasedInput =
		FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(ModuleInput, FunctionCall);
	const FNiagaraVariable InputVariable(InputType, ModuleInput.GetParameterHandleString());
	const TOptional<FNiagaraVariableMetaData> InputMetaData =
		FunctionCall->GetNiagaraGraph()->GetMetaData(InputVariable);
	const FGuid InputVariableGuid = InputMetaData.IsSet()
		? InputMetaData->GetVariableGuid()
		: FGuid();
	UEdGraphPin& OverridePin =
		FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*FunctionCall,
			AliasedInput,
			InputType,
			InputVariableGuid,
			FGuid());
	if (!OverridePin.LinkedTo.IsEmpty())
	{
		TArray<UEdGraphNode*> LinkedNodes;
		for (UEdGraphPin* LinkedPin : OverridePin.LinkedTo)
		{
			if (LinkedPin && LinkedPin->GetOwningNode())
			{
				LinkedNodes.AddUnique(LinkedPin->GetOwningNode());
			}
		}
		OverridePin.BreakAllPinLinks();
		for (UEdGraphNode* LinkedNode : LinkedNodes)
		{
			if (LinkedNode && LinkedNode != OverridePin.GetOwningNode() &&
				LinkedNode->GetGraph() == OverridePin.GetOwningNode()->GetGraph())
			{
				LinkedNode->GetGraph()->RemoveNode(LinkedNode);
			}
		}
	}

	const FNiagaraVariableBase UserParameter(
		InputType,
		FName(*FString::Printf(TEXT("User.%s"), UserParameterName)));
	TSet<FNiagaraVariableBase> KnownParameters;
	KnownParameters.Add(UserParameter);
	FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(
		OverridePin,
		UserParameter,
		KnownParameters);
	return OverridePin.LinkedTo.Num() == 1 && OverridePin.LinkedTo[0] &&
		OverridePin.LinkedTo[0]->PinName == UserParameter.GetName();
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

void ConfigureWaterSplashNiagaraSystem(UNiagaraSystem& System)
{
	for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data)
		{
			continue;
		}

		Data->bLocalSpace = false;
		const FString Name = Handle.GetName().ToString();
		if (Name.Contains(TEXT("OmnidirectionalBurst")))
		{
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Color"), FLinearColor(0.32f, 0.72f, 0.92f, 0.58f));
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Min"), 0.35f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Max"), 0.9f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Min"), 2.5f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Max"), 7.5f);
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat.Minimum"), 90.0f);
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat.Maximum"), 340.0f);
			SetRapidIterationValue(*Data, TEXT(".SpawnBurst_Instantaneous.Spawn Count"), 28);
		}
		else if (Name.Contains(TEXT("DirectionalBurst")))
		{
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Color"), FLinearColor(0.5f, 0.82f, 0.98f, 0.72f));
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Min"), 0.42f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Max"), 1.05f);
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Sprite Size Min"), FVector2f(2.0f, 6.0f));
			SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Sprite Size Max"), FVector2f(4.0f, 13.0f));
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat002.Minimum"), 180.0f);
			SetRapidIterationValue(*Data, TEXT(".RandomRangeFloat002.Maximum"), 520.0f);
			SetRapidIterationValue(*Data, TEXT(".AddVelocityInCone.Cone Angle"), 34.0f);
			SetRapidIterationValue(*Data, TEXT(".AddVelocityInCone.Cone Axis"), FVector3f(0.0f, 0.0f, 1.0f));
			SetRapidIterationValue(*Data, TEXT(".SpawnBurst_Instantaneous.Spawn Count"), 18);
		}
	}
}

enum class ELooseDebrisNiagaraPreset : uint8
{
	Ambient,
	Movement,
	Attack,
	Landing,
	Explosion,
};

bool EnsureLooseDebrisInitialVelocityModule(FVersionedNiagaraEmitterData& EmitterData)
{
	UNiagaraScript* VelocityModule = LoadObject<UNiagaraScript>(
		nullptr,
		TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocityInCone.AddVelocityInCone"));
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData.GraphSource);
	if (!VelocityModule || !Source || !Source->NodeGraph)
	{
		return false;
	}

	TArray<UNiagaraNodeFunctionCall*> FunctionCalls;
	Source->NodeGraph->GetNodesOfClass(FunctionCalls);
	if (FunctionCalls.ContainsByPredicate(
		[VelocityModule](const UNiagaraNodeFunctionCall* Node)
		{
			return Node && Node->FunctionScript == VelocityModule;
		}))
	{
		return true;
	}

	UNiagaraNodeOutput* SpawnOutput = Source->NodeGraph->FindEquivalentOutputNode(
		ENiagaraScriptUsage::ParticleSpawnScript);
	if (!SpawnOutput)
	{
		SpawnOutput = Source->NodeGraph->FindEquivalentOutputNode(
			ENiagaraScriptUsage::ParticleSpawnScriptInterpolated);
	}
	if (!SpawnOutput)
	{
		return false;
	}

	return FNiagaraStackGraphUtilities::AddScriptModuleToStack(
		VelocityModule,
		*SpawnOutput,
		INDEX_NONE,
		TEXT("LooseDebrisInitialPush")) != nullptr;
}

struct FLooseDebrisInteractionForceModules
{
	UNiagaraNodeFunctionCall* Repulsion = nullptr;
	UNiagaraNodeFunctionCall* AttackWake = nullptr;
};

bool EnsureLooseDebrisInteractionForceModules(
	UNiagaraSystem& System,
	FNiagaraEmitterHandle& Handle,
	FLooseDebrisInteractionForceModules& OutModules)
{
	OutModules = FLooseDebrisInteractionForceModules();
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	UNiagaraScript* PointForceModule = LoadObject<UNiagaraScript>(
		nullptr,
		TEXT("/Niagara/Modules/Update/Forces/V2/PointAttractionForce.PointAttractionForce"));
	UNiagaraScriptSource* Source = EmitterData
		? Cast<UNiagaraScriptSource>(EmitterData->GraphSource)
		: nullptr;
	if (!EmitterData || !PointForceModule || !Source || !Source->NodeGraph)
	{
		return false;
	}

	TArray<UNiagaraNodeFunctionCall*> FunctionCalls;
	Source->NodeGraph->GetNodesOfClass(FunctionCalls);
	TArray<UNiagaraNodeFunctionCall*> UntaggedV2Modules;
	for (UNiagaraNodeFunctionCall* Node : FunctionCalls)
	{
		if (!Node || !Node->FunctionScript)
		{
			continue;
		}
		const FString ModulePath = Node->FunctionScript->GetPathName();
		if (ModulePath.Contains(TEXT("/V2/PointAttractionForce.")))
		{
			if (Node->NodeComment.Equals(TEXT("LooseDebrisRepulsionForce")))
			{
				OutModules.Repulsion = Node;
			}
			else if (Node->NodeComment.Equals(TEXT("LooseDebrisAttackWakeForce")))
			{
				OutModules.AttackWake = Node;
			}
			else
			{
				UntaggedV2Modules.Add(Node);
			}
		}
		else if (ModulePath.Contains(TEXT("/PointAttractionForce.")))
		{
			FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Node, false);
		}
	}

	if (!OutModules.Repulsion && !UntaggedV2Modules.IsEmpty())
	{
		OutModules.Repulsion = UntaggedV2Modules[0];
		UntaggedV2Modules.RemoveAt(0);
		OutModules.Repulsion->Modify();
		OutModules.Repulsion->NodeComment = TEXT("LooseDebrisRepulsionForce");
	}
	if (!OutModules.AttackWake && !UntaggedV2Modules.IsEmpty())
	{
		OutModules.AttackWake = UntaggedV2Modules[0];
		OutModules.AttackWake->Modify();
		OutModules.AttackWake->NodeComment = TEXT("LooseDebrisAttackWakeForce");
	}

	UNiagaraNodeOutput* UpdateOutput = Source->NodeGraph->FindEquivalentOutputNode(
		ENiagaraScriptUsage::ParticleUpdateScript);
	if (!UpdateOutput)
	{
		return false;
	}

	if (!OutModules.Repulsion)
	{
		OutModules.Repulsion = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
			PointForceModule,
			*UpdateOutput,
			0,
			TEXT("LooseDebrisInteractionForceV2"));
		if (OutModules.Repulsion)
		{
			OutModules.Repulsion->NodeComment = TEXT("LooseDebrisRepulsionForce");
		}
	}
	if (!OutModules.AttackWake)
	{
		OutModules.AttackWake = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
			PointForceModule,
			*UpdateOutput,
			1,
			TEXT("LooseDebrisAttackWakeForceV2"));
		if (OutModules.AttackWake)
		{
			OutModules.AttackWake->NodeComment = TEXT("LooseDebrisAttackWakeForce");
		}
	}
	return OutModules.Repulsion && OutModules.AttackWake;
}

bool DisableLooseDebrisWindModule(FVersionedNiagaraEmitterData& EmitterData)
{
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData.GraphSource);
	if (!Source || !Source->NodeGraph)
	{
		return false;
	}

	bool bFoundWindModule = false;
	TArray<UNiagaraNodeFunctionCall*> FunctionCalls;
	Source->NodeGraph->GetNodesOfClass(FunctionCalls);
	for (UNiagaraNodeFunctionCall* Node : FunctionCalls)
	{
		if (!Node || !Node->FunctionScript ||
			!Node->FunctionScript->GetPathName().Contains(TEXT("/WindForce.WindForce")))
		{
			continue;
		}
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Node, false);
		bFoundWindModule = true;
	}
	return bFoundWindModule;
}

bool ConfigureLooseDebrisNiagaraSystem(
	UNiagaraSystem& System,
	const ELooseDebrisNiagaraPreset Preset,
	const FWorldLooseDebrisSettings& Settings,
	UNiagaraEffectType* EffectType,
	UMaterialInterface* LeafMaterial,
	UMaterialInterface* PaperMaterial)
{
	bool bConfigurationValid = true;
	const bool bAmbient = Preset == ELooseDebrisNiagaraPreset::Ambient;
	const float ParticleLifetime = FMath::Max(
		1.0f,
		bAmbient ? Settings.AmbientParticleLifetime : Settings.InteractionParticleLifetime);
	const float TotalSpawnRate = FMath::Max(
		0.0f,
		bAmbient ? Settings.AmbientParticleBudget / ParticleLifetime
		: Preset == ELooseDebrisNiagaraPreset::Movement ? Settings.MovementInteractionSpawnRate
		: Preset == ELooseDebrisNiagaraPreset::Attack ? Settings.AttackInteractionSpawnRate
		: Preset == ELooseDebrisNiagaraPreset::Landing ? Settings.LandingInteractionSpawnRate
		: Settings.ExplosionInteractionSpawnRate);
	const float PaperFraction = FMath::Clamp(Settings.PaperParticleFraction, 0.0f, 1.0f);
	const float ShapeRadius = bAmbient ? Settings.AuthoredAmbientRadius
		: Preset == ELooseDebrisNiagaraPreset::Movement ? Settings.MovementSpawnRadius
		: Preset == ELooseDebrisNiagaraPreset::Attack ? Settings.AttackSpawnRadius
		: Preset == ELooseDebrisNiagaraPreset::Landing ? Settings.LandingSpawnRadius
		: Settings.ExplosionSpawnRadius;
	const float InitialSpeed = bAmbient ? 0.0f
		: Preset == ELooseDebrisNiagaraPreset::Movement ? Settings.MovementInitialSpeed
		: Preset == ELooseDebrisNiagaraPreset::Attack ? Settings.AttackInitialSpeed
		: Preset == ELooseDebrisNiagaraPreset::Landing ? Settings.LandingInitialSpeed
		: Settings.ExplosionInitialSpeed;
	const FVector3f InitialVelocityAxis = FVector3f(
		1.0f,
		0.0f,
		FMath::Clamp(Settings.InitialVelocityUpwardRatio, 0.0f, 2.0f)).GetSafeNormal();
	const FVector3f Gravity = FVector3f(
		0.0f,
		0.0f,
		FMath::Min(
			-1.0f,
			bAmbient ? Settings.AmbientGravityZ : Settings.InteractionGravityZ));
	const float LifetimeMin = ParticleLifetime * 0.9f;
	const float LifetimeMax = ParticleLifetime * 1.1f;
	SetNiagaraUserParameter(System, TEXT("LeafSpawnRate"), TotalSpawnRate * (1.0f - PaperFraction));
	SetNiagaraUserParameter(System, TEXT("PaperSpawnRate"), TotalSpawnRate * PaperFraction);
	SetNiagaraUserParameter(System, TEXT("LifetimeMin"), LifetimeMin);
	SetNiagaraUserParameter(System, TEXT("LifetimeMax"), LifetimeMax);
	SetNiagaraUserParameter(System, TEXT("SpawnRadius"), ShapeRadius);
	SetNiagaraUserParameter(System, TEXT("InitialSpeed"), InitialSpeed);
	SetNiagaraUserParameter(
		System,
		TEXT("InitialVelocityConeAngle"),
		FMath::Clamp(Settings.InitialVelocityConeAngle, 0.0f, 89.0f));
	SetNiagaraUserParameter(System, TEXT("InitialVelocityAxis"), InitialVelocityAxis);
	SetNiagaraUserParameter(System, TEXT("Gravity"), Gravity);
	SetNiagaraUserPositionParameter(
		System,
		TEXT("InteractionForcePosition"),
		FVector::ZeroVector);
	SetNiagaraUserParameter(System, TEXT("InteractionForceStrength"), 0.0f);
	SetNiagaraUserParameter(System, TEXT("InteractionForceRadius"), 1.0f);
	SetNiagaraUserParameter(
		System,
		TEXT("InteractionForceFalloffExponent"),
		FMath::Clamp(Settings.InteractionForceFalloffExponent, 0.01f, 8.0f));
	SetNiagaraUserPositionParameter(System, TEXT("AttackWakePosition"), FVector::ZeroVector);
	SetNiagaraUserParameter(System, TEXT("AttackWakeStrength"), 0.0f);
	SetNiagaraUserParameter(System, TEXT("AttackWakeRadius"), 1.0f);
	SetNiagaraUserParameter(
		System,
		TEXT("AttackWakeFalloffExponent"),
		FMath::Clamp(Settings.AttackWakeFalloffExponent, 0.01f, 8.0f));
	const float InteractionDragScale =
		FMath::Max(0.0f, Settings.InteractionAerodynamicDragScale);
	const float InteractionLiftScale =
		FMath::Clamp(Settings.InteractionLiftContributionScale, 0.0f, 2.0f);
	SetNiagaraUserParameter(
		System,
		TEXT("LeafAerodynamicDrag"),
		bAmbient ? Settings.AmbientAerodynamicDrag : 1.15f * InteractionDragScale);
	SetNiagaraUserParameter(
		System,
		TEXT("PaperAerodynamicDrag"),
		bAmbient ? Settings.AmbientAerodynamicDrag : 1.6f * InteractionDragScale);
	SetNiagaraUserParameter(
		System,
		TEXT("LeafLiftContribution"),
		bAmbient ? 0.0f : 0.85f * InteractionLiftScale);
	SetNiagaraUserParameter(
		System,
		TEXT("PaperLiftContribution"),
		bAmbient ? 0.0f : 1.4f * InteractionLiftScale);
	SetNiagaraUserParameter(
		System,
		TEXT("RotationalDrag"),
		bAmbient
			? FMath::Max(0.0f, Settings.AmbientRotationalDrag)
			: FMath::Max(0.0f, Settings.InteractionRotationalDrag));
	SetNiagaraUserParameter(
		System,
		TEXT("LeafRotationStrength"),
		bAmbient ? FMath::Max(0.0f, Settings.AmbientLeafRotationStrength) : 0.85f);
	SetNiagaraUserParameter(
		System,
		TEXT("PaperRotationStrength"),
		bAmbient ? FMath::Max(0.0f, Settings.AmbientPaperRotationStrength) : 1.2f);
	SetNiagaraUserParameter(
		System,
		TEXT("Restitution"),
		bAmbient
			? FMath::Clamp(Settings.AmbientRestitution, 0.0f, 1.0f)
			: FMath::Clamp(Settings.InteractionRestitution, 0.0f, 1.0f));
	SetNiagaraUserParameter(
		System,
		TEXT("Friction"),
		bAmbient ? 0.72f : FMath::Clamp(Settings.InteractionFriction, 0.0f, 1.0f));
	SetNiagaraUserParameter(
		System,
		TEXT("StaticFriction"),
		bAmbient ? 0.88f : FMath::Clamp(Settings.InteractionStaticFriction, 0.0f, 1.0f));
	SetNiagaraUserParameter(
		System,
		TEXT("BounceFriction"),
		bAmbient ? 0.82f : FMath::Clamp(Settings.InteractionBounceFriction, 0.0f, 1.0f));
	SetNiagaraUserParameter(
		System,
		TEXT("RestStateTime"),
		bAmbient ? 0.35f : FMath::Max(0.0f, Settings.InteractionRestStateTime));
	SetNiagaraUserParameter(
		System,
		TEXT("StaticFrictionEngagementSpeed"),
		bAmbient ? 1.0f : FMath::Max(0.0f, Settings.InteractionStaticFrictionEngagementSpeed));
	SetNiagaraUserParameter(
		System,
		TEXT("RestNormalAlignment"),
		bAmbient ? 0.5f : FMath::Clamp(Settings.InteractionRestNormalAlignment, 0.0f, 1.0f));
	SetNiagaraUserParameter(
		System,
		TEXT("PenetrationBeforeRest"),
		bAmbient ? 1.0f : FMath::Clamp(Settings.InteractionPenetrationBeforeRest, 0.0f, 1.0f));
	SetNiagaraUserParameter(
		System,
		TEXT("RestingCalmingRate"),
		bAmbient
			? FMath::Max(0.0f, Settings.AmbientRestingCalmingRate)
			: FMath::Max(0.0f, Settings.InteractionRestingCalmingRate));
	SetNiagaraUserParameter(
		System,
		TEXT("BouncingCalmingRate"),
		bAmbient
			? FMath::Max(0.0f, Settings.AmbientBouncingCalmingRate)
			: FMath::Max(0.0f, Settings.InteractionBouncingCalmingRate));

	int32 EmitterIndex = 0;
	for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data)
		{
			continue;
		}
		const bool bPaper = (EmitterIndex++ % 2) == 1;
		FLooseDebrisInteractionForceModules InteractionForceModules;
		const bool bWindModuleDisabled = DisableLooseDebrisWindModule(*Data);
		ensureMsgf(
			bWindModuleDisabled,
			TEXT("Unable to disable the loose-debris Wind Force module."));
		bConfigurationValid &= bWindModuleDisabled;
		if (bAmbient)
		{
			const bool bInteractionForceModulesValid = EnsureLooseDebrisInteractionForceModules(
				System,
				Handle,
				InteractionForceModules);
			ensureMsgf(
				bInteractionForceModulesValid,
				TEXT("Unable to add the loose-debris interaction force modules."));
			bConfigurationValid &= bInteractionForceModulesValid;
		}
		if (!bAmbient)
		{
			const bool bInitialVelocityModuleValid = EnsureLooseDebrisInitialVelocityModule(*Data);
			ensureMsgf(
				bInitialVelocityModuleValid,
				TEXT("Unable to add the loose-debris initial velocity module."));
			bConfigurationValid &= bInitialVelocityModuleValid;
		}
		const float TypeFraction = bPaper ? PaperFraction : 1.0f - PaperFraction;
		const float SpawnRate = FMath::Max(0.01f, TotalSpawnRate * TypeFraction);
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			if (UNiagaraSpriteRendererProperties* SpriteRenderer =
				Cast<UNiagaraSpriteRendererProperties>(Renderer))
			{
				SpriteRenderer->Material = bPaper ? PaperMaterial : LeafMaterial;
				SpriteRenderer->PostEditChange();
			}
		}
		// Interaction systems are repositioned as fields move. World-space particles
		// stay where they were emitted instead of being dragged with the component.
		Data->bLocalSpace = false;
		Data->SimTarget = ENiagaraSimTarget::CPUSim;
		// These CPU particles remain in world space and can be pushed beyond their
		// authored spawn area. Dynamic bounds prevent the whole emitter from being
		// frustum-culled when the camera no longer sees the original fixed box.
		Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Dynamic;
		Data->MaxGPUParticlesSpawnPerFrame = bAmbient ? 256 : 128;

		SetRapidIterationValue(
			*Data,
			TEXT(".InitializeParticle.Color"),
			bPaper
				? FLinearColor(0.82f, 0.76f, 0.60f, 1.0f)
				: FLinearColor(1.0f, 0.82f, 0.55f, 1.0f));
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Min"), LifetimeMin);
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Lifetime Max"), LifetimeMax);
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Mass Min"), bPaper ? 0.25f : 0.45f);
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Mass Max"), bPaper ? 0.45f : 0.75f);
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Min"), bPaper ? 8.0f : 5.0f);
		SetRapidIterationValue(*Data, TEXT(".InitializeParticle.Uniform Sprite Size Max"), bPaper ? 14.0f : 10.0f);
		SetRapidIterationValue(*Data, TEXT(".ShapeLocation.Sphere Radius"), ShapeRadius);
		SetRapidIterationValue(
			*Data,
			TEXT(".ShapeLocation.Non Uniform Scale"),
			bAmbient ? FVector3f(1.0f, 1.0f, 0.02f) : FVector3f(1.0f, 1.0f, 0.04f));
		const int32 SpawnRateMatches =
			SetRapidIterationValue(*Data, TEXT(".SpawnRate.SpawnRate"), SpawnRate);
		const int32 SpawnProbabilityMatches =
			SetRapidIterationValue(*Data, TEXT(".SpawnRate.Spawn Probability"), 1.0f);
		ensureMsgf(
			SpawnRateMatches > 0 && SpawnProbabilityMatches > 0,
			TEXT("Loose-debris population parameters were not found for preset %d."),
			static_cast<int32>(Preset));
		SetRapidIterationValue(*Data, TEXT(".GravityForce.Gravity"), Gravity);

		// Interaction particles receive a one-shot spawn push. Continuous wind would
		// fight gravity for the full 15-second lifetime and leave debris in the sky.
		SetRapidIterationValue(*Data, TEXT(".WindForce.Wind Speed"), 0.0f);
		SetRapidIterationValue(*Data, TEXT(".WindForce.Wind Speed"), FVector3f::ZeroVector);
		SetRapidIterationValue(*Data, TEXT(".WindForce.Wind Speed Scale"), 0.0f);
		SetRapidIterationValue(*Data, TEXT(".WindForce.Scale"), 0.0f);
		if (!bAmbient)
		{
			const int32 VelocityStrengthMatches = SetRapidIterationValue(
				*Data,
				TEXT(".AddVelocityInCone.Velocity Strength"),
				FMath::Max(0.0f, InitialSpeed));
			const int32 ConeAngleMatches = SetRapidIterationValue(
				*Data,
				TEXT(".AddVelocityInCone.Cone Angle"),
				FMath::Clamp(Settings.InitialVelocityConeAngle, 0.0f, 89.0f));
			const int32 ConeAxisMatches = SetRapidIterationValue(
				*Data,
				TEXT(".AddVelocityInCone.Cone Axis"),
				InitialVelocityAxis);
			if (VelocityStrengthMatches <= 0 || ConeAngleMatches <= 0 || ConeAxisMatches <= 0)
			{
				TArray<UNiagaraScript*> Scripts;
				GetAllEmitterScripts(*Data, Scripts);
				for (const UNiagaraScript* Script : Scripts)
				{
					for (const FNiagaraVariableWithOffset& Variable :
						Script->RapidIterationParameters.ReadParameterVariables())
					{
						if (Variable.GetName().ToString().Contains(TEXT("AddVelocityInCone")))
						{
							UE_LOG(
								LogTemp,
								Warning,
								TEXT("Loose debris velocity parameter %s type=%s"),
								*Variable.GetName().ToString(),
								*Variable.GetType().GetName());
						}
					}
				}
			}
		}
		const float AerodynamicDrag = bAmbient
			? FMath::Max(0.0f, Settings.AmbientAerodynamicDrag)
			: (bPaper ? 1.6f : 1.15f) *
				FMath::Max(0.0f, Settings.InteractionAerodynamicDragScale);
		SetRapidIterationValue(*Data, TEXT(".AerodynamicDrag.Aerodynamic Drag"), AerodynamicDrag);
		SetRapidIterationValue(
			*Data,
			TEXT(".AerodynamicDrag.Aerodynamic Rotational Drag"),
			bAmbient
				? FMath::Max(0.0f, Settings.AmbientRotationalDrag)
				: FMath::Max(0.0f, Settings.InteractionRotationalDrag));
		SetRapidIterationValue(
			*Data,
			TEXT(".AerodynamicDrag.Lift Contribution"),
			bAmbient
				? 0.0f
				: (bPaper ? 1.4f : 0.85f) *
					FMath::Clamp(Settings.InteractionLiftContributionScale, 0.0f, 2.0f));
		const float RotationStrength = bAmbient
			? (bPaper
				? FMath::Max(0.0f, Settings.AmbientPaperRotationStrength)
				: FMath::Max(0.0f, Settings.AmbientLeafRotationStrength))
			: (bPaper ? 1.2f : 0.85f);
		SetRapidIterationValue(*Data, TEXT(".AerodynamicDrag.Rotation Strength"), RotationStrength);
		SetRapidIterationValue(
			*Data,
			TEXT(".Collision.Enable Rest State"),
			FNiagaraBool(!bAmbient));
		SetRapidIterationValue(*Data, TEXT(".Collision.Kill On Collision"), FNiagaraBool(false));
		SetRapidIterationValue(
			*Data,
			TEXT(".Collision.Restitution"),
			bAmbient
				? FMath::Clamp(Settings.AmbientRestitution, 0.0f, 1.0f)
				: FMath::Clamp(Settings.InteractionRestitution, 0.0f, 1.0f));
		SetRapidIterationValue(
			*Data,
			TEXT(".Collision.Friction"),
			bAmbient ? 0.72f : FMath::Clamp(Settings.InteractionFriction, 0.0f, 1.0f));
		SetRapidIterationValue(
			*Data,
			TEXT(".Collision.Static Friction"),
			bAmbient ? 0.88f : FMath::Clamp(Settings.InteractionStaticFriction, 0.0f, 1.0f));
		SetRapidIterationValue(
			*Data,
			TEXT(".Collision.Friction During a Bounce"),
			bAmbient ? 0.82f : FMath::Clamp(Settings.InteractionBounceFriction, 0.0f, 1.0f));
		SetRapidIterationValue(
			*Data,
			TEXT(".Collision.Rest State Time Range"),
			bAmbient ? 0.35f : FMath::Max(0.0f, Settings.InteractionRestStateTime));
		SetRapidIterationValue(
			*Data,
			TEXT(".Collision.Static Friction Engagement Speed"),
			bAmbient ? 1.0f : FMath::Max(0.0f, Settings.InteractionStaticFrictionEngagementSpeed));
		SetRapidIterationValue(
			*Data,
			TEXT(".Collision.Minimum Collision Normal/Rest Normal Alignment Percentage"),
			bAmbient ? 0.5f : FMath::Clamp(Settings.InteractionRestNormalAlignment, 0.0f, 1.0f));
		SetRapidIterationValue(
			*Data,
			TEXT(".Collision.Percentage of Penetration Before Rest"),
			bAmbient ? 1.0f : FMath::Clamp(Settings.InteractionPenetrationBeforeRest, 0.0f, 1.0f));
		SetRapidIterationValue(
			*Data,
			TEXT(".AlignParticlesWithCollisionPlane.Calming Rate When Resting"),
			bAmbient
				? FMath::Max(0.0f, Settings.AmbientRestingCalmingRate)
				: FMath::Max(0.0f, Settings.InteractionRestingCalmingRate));
		SetRapidIterationValue(
			*Data,
			TEXT(".AlignParticlesWithCollisionPlane.Calming Rate When Bouncing"),
			bAmbient
				? FMath::Max(0.0f, Settings.AmbientBouncingCalmingRate)
				: FMath::Max(0.0f, Settings.InteractionBouncingCalmingRate));
		SetRapidIterationValue(*Data, TEXT(".Collision.Max Number Of Collisions"), 12);

		const TCHAR* SpawnRateParameter = bPaper
			? TEXT("PaperSpawnRate")
			: TEXT("LeafSpawnRate");
		const TCHAR* DragParameter = bPaper
			? TEXT("PaperAerodynamicDrag")
			: TEXT("LeafAerodynamicDrag");
		const TCHAR* LiftParameter = bPaper
			? TEXT("PaperLiftContribution")
			: TEXT("LeafLiftContribution");
		const TCHAR* RotationStrengthParameter = bPaper
			? TEXT("PaperRotationStrength")
			: TEXT("LeafRotationStrength");
		bool bRuntimeBindingsValid = true;
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("SpawnRate"), TEXT("SpawnRate"),
			FNiagaraTypeDefinition::GetFloatDef(), SpawnRateParameter);
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("InitializeParticle"), TEXT("Lifetime Min"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("LifetimeMin"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("InitializeParticle"), TEXT("Lifetime Max"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("LifetimeMax"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("ShapeLocation"), TEXT("Sphere Radius"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("SpawnRadius"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("GravityForce"), TEXT("Gravity"),
			FNiagaraTypeDefinition::GetVec3Def(), TEXT("Gravity"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("AerodynamicDrag"), TEXT("Aerodynamic Drag"),
			FNiagaraTypeDefinition::GetFloatDef(), DragParameter);
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("AerodynamicDrag"), TEXT("Aerodynamic Rotational Drag"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("RotationalDrag"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("AerodynamicDrag"), TEXT("Lift Contribution"),
			FNiagaraTypeDefinition::GetFloatDef(), LiftParameter);
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("AerodynamicDrag"), TEXT("Rotation Strength"),
			FNiagaraTypeDefinition::GetFloatDef(), RotationStrengthParameter);
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("Collision"), TEXT("Restitution"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("Restitution"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("Collision"), TEXT("Friction"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("Friction"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("Collision"), TEXT("Static Friction"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("StaticFriction"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("Collision"), TEXT("Friction During a Bounce"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("BounceFriction"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("Collision"), TEXT("Rest State Time Range"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("RestStateTime"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("Collision"), TEXT("Static Friction Engagement Speed"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("StaticFrictionEngagementSpeed"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("Collision"),
			TEXT("Minimum Collision Normal/Rest Normal Alignment Percentage"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("RestNormalAlignment"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("Collision"), TEXT("Percentage of Penetration Before Rest"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("PenetrationBeforeRest"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("AlignParticlesWithCollisionPlane"),
			TEXT("Calming Rate When Resting"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("RestingCalmingRate"));
		bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
			System, *Data, TEXT("AlignParticlesWithCollisionPlane"),
			TEXT("Calming Rate When Bouncing"),
			FNiagaraTypeDefinition::GetFloatDef(), TEXT("BouncingCalmingRate"));
		if (!bAmbient)
		{
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("AddVelocityInCone"), TEXT("Velocity Strength"),
				FNiagaraTypeDefinition::GetFloatDef(), TEXT("InitialSpeed"));
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("AddVelocityInCone"), TEXT("Cone Angle"),
				FNiagaraTypeDefinition::GetFloatDef(), TEXT("InitialVelocityConeAngle"));
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("AddVelocityInCone"), TEXT("Cone Axis"),
				FNiagaraTypeDefinition::GetVec3Def(), TEXT("InitialVelocityAxis"));
		}
		else
		{
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("/V2/PointAttractionForce."), TEXT("Attractor Position"),
				FNiagaraTypeDefinition::GetPositionDef(), TEXT("InteractionForcePosition"),
				InteractionForceModules.Repulsion);
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("/V2/PointAttractionForce."), TEXT("Attraction Strength"),
				FNiagaraTypeDefinition::GetFloatDef(), TEXT("InteractionForceStrength"),
				InteractionForceModules.Repulsion);
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("/V2/PointAttractionForce."), TEXT("Attraction Radius"),
				FNiagaraTypeDefinition::GetFloatDef(), TEXT("InteractionForceRadius"),
				InteractionForceModules.Repulsion);
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("/V2/PointAttractionForce."), TEXT("Falloff Exponent"),
				FNiagaraTypeDefinition::GetFloatDef(), TEXT("InteractionForceFalloffExponent"),
				InteractionForceModules.Repulsion);

			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("/V2/PointAttractionForce."), TEXT("Attractor Position"),
				FNiagaraTypeDefinition::GetPositionDef(), TEXT("AttackWakePosition"),
				InteractionForceModules.AttackWake);
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("/V2/PointAttractionForce."), TEXT("Attraction Strength"),
				FNiagaraTypeDefinition::GetFloatDef(), TEXT("AttackWakeStrength"),
				InteractionForceModules.AttackWake);
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("/V2/PointAttractionForce."), TEXT("Attraction Radius"),
				FNiagaraTypeDefinition::GetFloatDef(), TEXT("AttackWakeRadius"),
				InteractionForceModules.AttackWake);
			bRuntimeBindingsValid &= BindNiagaraModuleInputToUserParameter(
				System, *Data, TEXT("/V2/PointAttractionForce."), TEXT("Falloff Exponent"),
				FNiagaraTypeDefinition::GetFloatDef(), TEXT("AttackWakeFalloffExponent"),
				InteractionForceModules.AttackWake);
		}
		ensureMsgf(
			bRuntimeBindingsValid,
			TEXT("One or more loose-debris runtime Niagara bindings failed for preset %d emitter %d."),
			static_cast<int32>(Preset),
			EmitterIndex - 1);
		bConfigurationValid &= bRuntimeBindingsValid;
	}

	System.SetEffectType(EffectType);
	System.SetWarmupTickDelta(1.0f / 30.0f);
	// Reach the configured steady-state population before the first rendered
	// frame, so interaction fields disturb existing ground debris immediately.
	System.SetWarmupTime(bAmbient ? ParticleLifetime : 0.0f);
	System.ResolveWarmupTickCount();
	return bConfigurationValid;
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
		Stats.RigidLeafCount >= DemoWoodenCrateMinimumRigidLeafCount &&
		Stats.RigidLeafCount <= DemoWoodenCrateMaximumRigidLeafCount &&
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

bool URoverEditorTestLibrary::ConfigurePhysicsWorldWaterAssets()
{
	UNiagaraSystem* Splash = CreateOrLoadNiagaraSystemFromEmitters(
		TEXT("/Game/PhysicsWorldDemo/Water/Niagara/NS_RoverWaterSplash"),
		{
			TEXT("/Niagara/DefaultAssets/Templates/Emitters/OmnidirectionalBurst"),
			TEXT("/Niagara/DefaultAssets/Templates/Emitters/DirectionalBurst"),
		});
	if (!Splash)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to create the Physics World water splash Niagara asset"));
		return false;
	}

	ConfigureWaterSplashNiagaraSystem(*Splash);
	Splash->RequestCompile(false);
	Splash->MarkPackageDirty();
	Splash->GetOutermost()->SetDirtyFlag(true);
	Splash->WaitForCompilationComplete();
	UE_LOG(
		LogTemp,
		Display,
		TEXT("PHYSICS_WORLD_WATER_NIAGARA_OK splash=%s"),
		*Splash->GetPathName());
	return true;
}

bool URoverEditorTestLibrary::ConfigurePhysicsWorldLooseDebrisAssets()
{
	const FString EmitterTemplate = TEXT("/Niagara/DefaultAssets/Templates/Emitters/BlowingParticles");
	UNiagaraDataChannelAsset* DataChannel = CreateOrLoadLooseDebrisDataChannel(
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/DataChannels/NDC_LooseDebrisInteraction"));
	UNiagaraEffectType* EffectType = CreateOrLoadNiagaraEffectType(
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/EffectTypes/NET_LooseDebris"));
	UMaterialInterface* LeafMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/Materials/M_LooseDebris_Leaf.M_LooseDebris_Leaf"));
	UMaterialInterface* PaperMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/Materials/M_LooseDebris_Paper.M_LooseDebris_Paper"));
	UNiagaraSystem* Ambient = CreateOrLoadNiagaraSystemFromEmitters(
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/Niagara/Systems/NS_LooseDebris_Ambient"),
		{EmitterTemplate, EmitterTemplate});
	UNiagaraSystem* Movement = CreateOrLoadNiagaraSystemFromEmitters(
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/Niagara/Systems/NS_LooseDebris_Movement"),
		{EmitterTemplate, EmitterTemplate});
	UNiagaraSystem* Attack = CreateOrLoadNiagaraSystemFromEmitters(
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/Niagara/Systems/NS_LooseDebris_Attack"),
		{EmitterTemplate, EmitterTemplate});
	UNiagaraSystem* Landing = CreateOrLoadNiagaraSystemFromEmitters(
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/Niagara/Systems/NS_LooseDebris_Landing"),
		{EmitterTemplate, EmitterTemplate});
	UNiagaraSystem* Explosion = CreateOrLoadNiagaraSystemFromEmitters(
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/Niagara/Systems/NS_LooseDebris_Explosion"),
		{EmitterTemplate, EmitterTemplate});
	if (!DataChannel || !EffectType || !LeafMaterial || !PaperMaterial ||
		!Ambient || !Movement || !Attack || !Landing || !Explosion)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to create all loose debris Niagara assets"));
		return false;
	}
	const UWorldLooseDebrisConfig* LooseDebrisConfig = LoadObject<UWorldLooseDebrisConfig>(
		nullptr,
		TEXT("/Game/PhysicsWorldDemo/LooseDebris/Config/DA_WorldLooseDebrisConfig.DA_WorldLooseDebrisConfig"));
	const FWorldLooseDebrisSettings DefaultSettings;
	const FWorldLooseDebrisSettings& Settings = LooseDebrisConfig
		? LooseDebrisConfig->Settings
		: DefaultSettings;

	bool bNiagaraConfigurationValid = true;
	bNiagaraConfigurationValid &= ConfigureLooseDebrisNiagaraSystem(
		*Ambient, ELooseDebrisNiagaraPreset::Ambient, Settings, EffectType, LeafMaterial, PaperMaterial);
	bNiagaraConfigurationValid &= ConfigureLooseDebrisNiagaraSystem(
		*Movement, ELooseDebrisNiagaraPreset::Movement, Settings, EffectType, LeafMaterial, PaperMaterial);
	bNiagaraConfigurationValid &= ConfigureLooseDebrisNiagaraSystem(
		*Attack, ELooseDebrisNiagaraPreset::Attack, Settings, EffectType, LeafMaterial, PaperMaterial);
	bNiagaraConfigurationValid &= ConfigureLooseDebrisNiagaraSystem(
		*Landing, ELooseDebrisNiagaraPreset::Landing, Settings, EffectType, LeafMaterial, PaperMaterial);
	bNiagaraConfigurationValid &= ConfigureLooseDebrisNiagaraSystem(
		*Explosion, ELooseDebrisNiagaraPreset::Explosion, Settings, EffectType, LeafMaterial, PaperMaterial);
	if (!bNiagaraConfigurationValid)
	{
		UE_LOG(LogTemp, Error, TEXT("PHYSICS_WORLD_LOOSE_DEBRIS_ASSETS_FAILED runtime Niagara binding validation failed"));
		return false;
	}

	for (UNiagaraSystem* System : {Ambient, Movement, Attack, Landing, Explosion})
	{
		System->RequestCompile(false);
		System->MarkPackageDirty();
		System->GetOutermost()->SetDirtyFlag(true);
	}
	for (UNiagaraSystem* System : {Ambient, Movement, Attack, Landing, Explosion})
	{
		System->WaitForCompilationComplete();
	}
	EffectType->MarkPackageDirty();
	DataChannel->MarkPackageDirty();

	UE_LOG(
		LogTemp,
		Display,
		TEXT("PHYSICS_WORLD_LOOSE_DEBRIS_ASSETS_OK channel=%s ambient=%s movement=%s attack=%s landing=%s explosion=%s"),
		*DataChannel->GetPathName(),
		*Ambient->GetPathName(),
		*Movement->GetPathName(),
		*Attack->GetPathName(),
		*Landing->GetPathName(),
		*Explosion->GetPathName());
	return true;
}

bool URoverEditorTestLibrary::ConfigureRoverWaterAdvancedPhysicsAsset(
	const FString& SkeletalMeshPath,
	const FString& PhysicsAssetPackagePath)
{
	if (!FPackageName::IsValidLongPackageName(PhysicsAssetPackagePath))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Invalid Rover Physics Asset package path: %s"),
			*PhysicsAssetPackagePath);
		return false;
	}

	USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
	if (!SkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to load Rover Skeletal Mesh: %s"), *SkeletalMeshPath);
		return false;
	}

	const FString PhysicsAssetObjectPath = MakeObjectPath(PhysicsAssetPackagePath);
	UPhysicsAsset* PhysicsAsset = LoadObject<UPhysicsAsset>(nullptr, *PhysicsAssetObjectPath);
	bool bCreatedAsset = false;
	if (!PhysicsAsset)
	{
		UPackage* Package = CreatePackage(*PhysicsAssetPackagePath);
		if (!Package)
		{
			return false;
		}
		PhysicsAsset = NewObject<UPhysicsAsset>(
			Package,
			*FPackageName::GetLongPackageAssetName(PhysicsAssetPackagePath),
			RF_Public | RF_Standalone | RF_Transactional);
		bCreatedAsset = PhysicsAsset != nullptr;
	}
	if (!PhysicsAsset)
	{
		return false;
	}

	if (PhysicsAsset->SkeletalBodySetups.IsEmpty())
	{
		FPhysAssetCreateParams CreateParams;
		CreateParams.MinBoneSize = 25.0f;
		CreateParams.GeomType = EFG_Sphyl;
		CreateParams.VertWeight = EVW_DominantWeight;
		CreateParams.bAlwaysUseVertices = true;
		CreateParams.bIncludeChildBones = true;
		CreateParams.bAutoOrientToBone = true;
		CreateParams.bCreateConstraints = false;
		CreateParams.bWalkPastSmall = true;
		CreateParams.bBodyForAll = false;
		CreateParams.bDisableCollisionsByDefault = true;

		FText ErrorMessage;
		if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(
			PhysicsAsset,
			SkeletalMesh,
			CreateParams,
			ErrorMessage,
			false,
			false))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Unable to generate Rover Physics Asset: %s"),
				*ErrorMessage.ToString());
			if (bCreatedAsset)
			{
				PhysicsAsset->ClearFlags(RF_Public | RF_Standalone);
			}
			return false;
		}
	}

	if (bCreatedAsset)
	{
		FAssetRegistryModule::AssetCreated(PhysicsAsset);
	}
	SkeletalMesh->SetPhysicsAsset(PhysicsAsset);
	PhysicsAsset->MarkPackageDirty();
	PhysicsAsset->GetOutermost()->SetDirtyFlag(true);
	SkeletalMesh->MarkPackageDirty();
	SkeletalMesh->GetOutermost()->SetDirtyFlag(true);

	UE_LOG(
		LogTemp,
		Display,
		TEXT("ROVER_WATER_ADVANCED_PHYSICS_ASSET_OK mesh=%s physics_asset=%s bodies=%d constraints=%d"),
		*SkeletalMesh->GetPathName(),
		*PhysicsAsset->GetPathName(),
		PhysicsAsset->SkeletalBodySetups.Num(),
		PhysicsAsset->ConstraintSetup.Num());
	return !PhysicsAsset->SkeletalBodySetups.IsEmpty();
}

int32 URoverEditorTestLibrary::GetPhysicsAssetBodyCount(const FString& PhysicsAssetPath)
{
	const UPhysicsAsset* PhysicsAsset = LoadObject<UPhysicsAsset>(nullptr, *PhysicsAssetPath);
	return PhysicsAsset ? PhysicsAsset->SkeletalBodySetups.Num() : 0;
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

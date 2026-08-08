#include "RoverAnimationEditorLibrary.h"

#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_TwoWayBlend.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/DataAssetFactory.h"
#include "IAssetTools.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "RoverAnimInstance.h"
#include "RoverAnimNotifyState_ComboWindow.h"
#include "RoverAnimNotifyState_ResonanceWindow.h"
#include "RoverCombatConfig.h"
#include "RoverMovementConfig.h"

namespace
{
constexpr TCHAR AnimationRoot[] = TEXT("/Game/Rover/Animations/P0");
constexpr TCHAR BlendSpaceRoot[] = TEXT("/Game/Rover/Animations/BlendSpaces");
constexpr TCHAR CombatMontageRoot[] = TEXT("/Game/Rover/Combat/Montages");
constexpr TCHAR CombatConfigAssetPath[] = TEXT("/Game/Rover/Combat/DA_RoverCombatConfig");
constexpr TCHAR MovementConfigObjectPath[] = TEXT("/Game/Rover/Config/DA_RoverMovementConfig.DA_RoverMovementConfig");
const FName MoveStopRootBoneName(TEXT("root"));
const FName MoveStopBodyBoneName(TEXT("Bip001"));
constexpr double MoveStopSignificantTravel = 1.0;
constexpr double MoveStopResidualTolerance = 0.5;
constexpr double MoveStopBacktrackTolerance = 0.5;
constexpr double MoveStopPoseTolerance = 0.01;

struct FMoveStopRootMotionData
{
	TArray<FTransform> RootLocalTransforms;
	TArray<FTransform> BodyLocalTransforms;
	TArray<FTransform> BodyComponentTransforms;
	double RootTravelXY = 0.0;
	double BodyTravelXY = 0.0;
	double ResidualTravelXY = 0.0;
	double RootTerminalReboundXY = 0.0;
	double RootMaxBackwardStepXY = 0.0;
	double BodyTerminalReboundXY = 0.0;
	double BodyMaxBackwardStepXY = 0.0;
};

enum class EMoveStopRootMotionState : uint8
{
	NeedsBake,
	AlreadyBaked,
	Ambiguous
};

FVector PlanarVector(const FVector& Vector)
{
	return FVector(Vector.X, Vector.Y, 0.0);
}

void CalculatePlanarTrajectoryMetrics(
	const TArray<FTransform>& Transforms,
	double& OutTravelXY,
	double& OutTerminalReboundXY,
	double& OutMaxBackwardStepXY)
{
	OutTravelXY = 0.0;
	OutTerminalReboundXY = 0.0;
	OutMaxBackwardStepXY = 0.0;
	if (Transforms.Num() < 2)
	{
		return;
	}

	const FVector FirstLocation = Transforms[0].GetTranslation();
	const FVector FinalDelta = PlanarVector(Transforms.Last().GetTranslation() - FirstLocation);
	const FVector TravelDirection = FinalDelta.GetSafeNormal();
	if (TravelDirection.IsNearlyZero())
	{
		return;
	}

	double PreviousForward = 0.0;
	double PeakForward = 0.0;
	for (int32 KeyIndex = 1; KeyIndex < Transforms.Num(); ++KeyIndex)
	{
		const FVector Delta = PlanarVector(Transforms[KeyIndex].GetTranslation() - FirstLocation);
		const double Forward = FVector::DotProduct(Delta, TravelDirection);
		OutTravelXY = FMath::Max(OutTravelXY, Delta.Size());
		PeakForward = FMath::Max(PeakForward, Forward);
		OutMaxBackwardStepXY = FMath::Max(OutMaxBackwardStepXY, PreviousForward - Forward);
		PreviousForward = Forward;
	}
	OutTerminalReboundXY = FMath::Max(0.0, PeakForward - FVector::DotProduct(FinalDelta, TravelDirection));
}

FString MoveStopMetricReport(
	const UAnimSequence& AnimationSequence,
	const FMoveStopRootMotionData& Data)
{
	return FString::Printf(
		TEXT("asset=%s keys=%d root_xy_travel=%.3f body_xy_travel=%.3f bip001_residual_xy=%.3f root_rebound_xy=%.3f root_backstep_xy=%.3f body_rebound_xy=%.3f body_backstep_xy=%.3f"),
		*AnimationSequence.GetPathName(),
		Data.RootLocalTransforms.Num(),
		Data.RootTravelXY,
		Data.BodyTravelXY,
		Data.ResidualTravelXY,
		Data.RootTerminalReboundXY,
		Data.RootMaxBackwardStepXY,
		Data.BodyTerminalReboundXY,
		Data.BodyMaxBackwardStepXY);
}

bool ReadMoveStopRootMotionData(
	const UAnimSequence* AnimationSequence,
	FMoveStopRootMotionData& OutData,
	FString& ValidationReport)
{
	ValidationReport.Reset();
	OutData = FMoveStopRootMotionData();
	if (!IsValid(AnimationSequence))
	{
		ValidationReport = TEXT("A valid Animation Sequence is required.");
		return false;
	}

	const USkeleton* Skeleton = AnimationSequence->GetSkeleton();
	if (!IsValid(Skeleton))
	{
		ValidationReport = FString::Printf(
			TEXT("asset=%s has no valid skeleton."),
			*AnimationSequence->GetPathName());
		return false;
	}

	const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
	if (ReferenceSkeleton.GetNum() == 0 || ReferenceSkeleton.GetBoneName(0) != MoveStopRootBoneName)
	{
		ValidationReport = FString::Printf(
			TEXT("asset=%s requires top-level skeleton bone root at index 0."),
			*AnimationSequence->GetPathName());
		return false;
	}

	const int32 BodyBoneIndex = ReferenceSkeleton.FindBoneIndex(MoveStopBodyBoneName);
	if (BodyBoneIndex == INDEX_NONE || ReferenceSkeleton.GetParentIndex(BodyBoneIndex) != 0)
	{
		ValidationReport = FString::Printf(
			TEXT("asset=%s requires Bip001 as a direct child of root."),
			*AnimationSequence->GetPathName());
		return false;
	}

	IAnimationDataModel* DataModel = AnimationSequence->GetDataModel();
	if (!DataModel)
	{
		ValidationReport = FString::Printf(
			TEXT("asset=%s has no animation data model."),
			*AnimationSequence->GetPathName());
		return false;
	}
	if (!DataModel->IsValidBoneTrackName(MoveStopRootBoneName) ||
		!DataModel->IsValidBoneTrackName(MoveStopBodyBoneName))
	{
		ValidationReport = FString::Printf(
			TEXT("asset=%s requires raw tracks for root and Bip001."),
			*AnimationSequence->GetPathName());
		return false;
	}

	const int32 NumberOfKeys = DataModel->GetNumberOfKeys();
	DataModel->GetBoneTrackTransforms(MoveStopRootBoneName, OutData.RootLocalTransforms);
	DataModel->GetBoneTrackTransforms(MoveStopBodyBoneName, OutData.BodyLocalTransforms);
	if (NumberOfKeys < 2 ||
		OutData.RootLocalTransforms.Num() != NumberOfKeys ||
		OutData.BodyLocalTransforms.Num() != NumberOfKeys)
	{
		ValidationReport = FString::Printf(
			TEXT("asset=%s has invalid root/Bip001 key counts (model=%d root=%d Bip001=%d)."),
			*AnimationSequence->GetPathName(),
			NumberOfKeys,
			OutData.RootLocalTransforms.Num(),
			OutData.BodyLocalTransforms.Num());
		return false;
	}

	OutData.BodyComponentTransforms.Reserve(NumberOfKeys);
	for (int32 KeyIndex = 0; KeyIndex < NumberOfKeys; ++KeyIndex)
	{
		const FTransform& RootTransform = OutData.RootLocalTransforms[KeyIndex];
		const FTransform& BodyTransform = OutData.BodyLocalTransforms[KeyIndex];
		if (RootTransform.ContainsNaN() || BodyTransform.ContainsNaN())
		{
			ValidationReport = FString::Printf(
				TEXT("asset=%s contains a non-finite root/Bip001 transform at key %d."),
				*AnimationSequence->GetPathName(),
				KeyIndex);
			return false;
		}
		OutData.BodyComponentTransforms.Add(BodyTransform * RootTransform);
	}

	CalculatePlanarTrajectoryMetrics(
		OutData.RootLocalTransforms,
		OutData.RootTravelXY,
		OutData.RootTerminalReboundXY,
		OutData.RootMaxBackwardStepXY);
	CalculatePlanarTrajectoryMetrics(
		OutData.BodyComponentTransforms,
		OutData.BodyTravelXY,
		OutData.BodyTerminalReboundXY,
		OutData.BodyMaxBackwardStepXY);

	const FVector FirstRootLocation = OutData.RootLocalTransforms[0].GetTranslation();
	const FVector FirstBodyLocation = OutData.BodyComponentTransforms[0].GetTranslation();
	for (int32 KeyIndex = 1; KeyIndex < NumberOfKeys; ++KeyIndex)
	{
		const FVector RootDelta = PlanarVector(
			OutData.RootLocalTransforms[KeyIndex].GetTranslation() - FirstRootLocation);
		const FVector BodyDelta = PlanarVector(
			OutData.BodyComponentTransforms[KeyIndex].GetTranslation() - FirstBodyLocation);
		OutData.ResidualTravelXY = FMath::Max(
			OutData.ResidualTravelXY,
			(BodyDelta - RootDelta).Size());
	}

	return true;
}

EMoveStopRootMotionState ClassifyMoveStopRootMotion(
	const FMoveStopRootMotionData& Data,
	FString& ValidationReport,
	const UAnimSequence& AnimationSequence)
{
	const bool bRootSignificant = Data.RootTravelXY > MoveStopSignificantTravel;
	const bool bResidualAcceptable = Data.ResidualTravelXY <= MoveStopResidualTolerance;

	// Imported clips may already carry their main trajectory on root while a
	// smaller residual remains on Bip001. The residual bake is idempotent and
	// handles both fully unbaked and partially promoted source tracks.
	if (!bResidualAcceptable)
	{
		return EMoveStopRootMotionState::NeedsBake;
	}
	if (bRootSignificant && bResidualAcceptable)
	{
		return EMoveStopRootMotionState::AlreadyBaked;
	}

	const FString Metrics = MoveStopMetricReport(AnimationSequence, Data);
	ValidationReport = FString::Printf(
		TEXT("%s state=ambiguous; the root trajectory is too small for a move-stop clip."),
		*Metrics);
	return EMoveStopRootMotionState::Ambiguous;
}

void AppendTransformKeys(
	const FTransform& Transform,
	TArray<FVector3f>& PositionKeys,
	TArray<FQuat4f>& RotationKeys,
	TArray<FVector3f>& ScaleKeys)
{
	PositionKeys.Add(FVector3f(Transform.GetTranslation()));
	RotationKeys.Add(FQuat4f(Transform.GetRotation()));
	ScaleKeys.Add(FVector3f(Transform.GetScale3D()));
}

template <typename AssetType>
AssetType* LoadRoverAsset(const TCHAR* Root, const TCHAR* Name)
{
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), Root, Name, Name);
	AssetType* Asset = LoadObject<AssetType>(nullptr, *ObjectPath);
	if (!Asset)
	{
		UE_LOG(LogTemp, Error, TEXT("Missing Rover animation asset: %s"), *ObjectPath);
	}
	return Asset;
}

bool ConnectPins(UEdGraphPin* OutputPin, UEdGraphPin* InputPin)
{
	if (!OutputPin || !InputPin || !OutputPin->GetOwningNode() || !InputPin->GetOwningNode())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot connect invalid animation graph pins."));
		return false;
	}

	UEdGraph* Graph = OutputPin->GetOwningNode()->GetGraph();
	if (!Graph || Graph != InputPin->GetOwningNode()->GetGraph())
	{
		UE_LOG(LogTemp, Error, TEXT("Animation graph pins belong to different graphs."));
		return false;
	}

	if (!Graph->GetSchema()->TryCreateConnection(OutputPin, InputPin))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Failed to connect animation graph pins %s.%s -> %s.%s"),
			*OutputPin->GetOwningNode()->GetName(),
			*OutputPin->PinName.ToString(),
			*InputPin->GetOwningNode()->GetName(),
			*InputPin->PinName.ToString());
		return false;
	}

	return true;
}

UK2Node_VariableGet* AddVariableGetter(UEdGraph& Graph, const FName PropertyName, const int32 X, const int32 Y)
{
	FGraphNodeCreator<UK2Node_VariableGet> Creator(Graph);
	UK2Node_VariableGet* Getter = Creator.CreateNode(false);
	Getter->VariableReference.SetSelfMember(PropertyName);
	Getter->NodePosX = X;
	Getter->NodePosY = Y;
	Creator.Finalize();
	return Getter;
}

UAnimGraphNode_SequencePlayer* AddSequencePlayer(
	UEdGraph& Graph,
	UAnimSequence* Sequence,
	const bool bLoop,
	const int32 X,
	const int32 Y)
{
	if (!Sequence)
	{
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(Graph);
	UAnimGraphNode_SequencePlayer* Player = Creator.CreateNode(false);
	Player->Node.SetSequence(Sequence);
	Player->Node.SetLoopAnimation(bLoop);
	Player->NodePosX = X;
	Player->NodePosY = Y;
	Creator.Finalize();
	return Player;
}

UAnimGraphNode_BlendSpacePlayer* AddBlendSpacePlayer(
	UEdGraph& Graph,
	UBlendSpace* BlendSpace,
	const int32 X,
	const int32 Y)
{
	if (!BlendSpace)
	{
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> Creator(Graph);
	UAnimGraphNode_BlendSpacePlayer* Player = Creator.CreateNode(false);
	Player->Node.SetBlendSpace(BlendSpace);
	Player->Node.SetLoop(true);
	Player->NodePosX = X;
	Player->NodePosY = Y;
	Creator.Finalize();

	UK2Node_VariableGet* DirectionGetter = AddVariableGetter(Graph, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, Direction), X - 220, Y + 110);
	if (!ConnectPins(DirectionGetter->GetValuePin(), Player->FindPin(TEXT("X"))))
	{
		return nullptr;
	}

	return Player;
}

UAnimGraphNode_BlendListByBool* AddBoolBlend(
	UEdGraph& Graph,
	UEdGraphPin* TruePose,
	UEdGraphPin* FalsePose,
	const FName BoolProperty,
	const int32 X,
	const int32 Y,
	const float BlendTime = 0.1f)
{
	if (!TruePose || !FalsePose)
	{
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_BlendListByBool> Creator(Graph);
	UAnimGraphNode_BlendListByBool* Blend = Creator.CreateNode(false);
	Blend->NodePosX = X;
	Blend->NodePosY = Y;
	Creator.Finalize();
	const FString BlendTimeValue = FString::SanitizeFloat(FMath::Max(0.0f, BlendTime));
	if (UEdGraphPin* TrueBlendTimePin = Blend->FindPin(TEXT("BlendTime_0")))
	{
		Graph.GetSchema()->TrySetDefaultValue(*TrueBlendTimePin, BlendTimeValue);
	}
	if (UEdGraphPin* FalseBlendTimePin = Blend->FindPin(TEXT("BlendTime_1")))
	{
		Graph.GetSchema()->TrySetDefaultValue(*FalseBlendTimePin, BlendTimeValue);
	}

	UK2Node_VariableGet* BoolGetter = AddVariableGetter(Graph, BoolProperty, X - 220, Y + 220);
	const bool bConnected =
		ConnectPins(TruePose, Blend->FindPin(TEXT("BlendPose_0"))) &&
		ConnectPins(FalsePose, Blend->FindPin(TEXT("BlendPose_1"))) &&
		ConnectPins(BoolGetter->GetValuePin(), Blend->FindPin(TEXT("bActiveValue")));
	return bConnected ? Blend : nullptr;
}

UAnimGraphNode_TwoWayBlend* AddFloatBlend(
	UEdGraph& Graph,
	UEdGraphPin* PoseA,
	UEdGraphPin* PoseB,
	const FName FloatProperty,
	const int32 X,
	const int32 Y)
{
	if (!PoseA || !PoseB)
	{
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_TwoWayBlend> Creator(Graph);
	UAnimGraphNode_TwoWayBlend* Blend = Creator.CreateNode(false);
	Blend->BlendNode.AlphaInputType = EAnimAlphaInputType::Float;
	Blend->NodePosX = X;
	Blend->NodePosY = Y;
	Creator.Finalize();

	UK2Node_VariableGet* FloatGetter = AddVariableGetter(Graph, FloatProperty, X - 220, Y + 220);
	const bool bConnected =
		ConnectPins(PoseA, Blend->FindPin(TEXT("A"))) &&
		ConnectPins(PoseB, Blend->FindPin(TEXT("B"))) &&
		ConnectPins(FloatGetter->GetValuePin(), Blend->FindPin(TEXT("Alpha")));
	return bConnected ? Blend : nullptr;
}

UAnimStateNode* AddState(UAnimationStateMachineGraph& MachineGraph, const TCHAR* Name, const int32 X, const int32 Y)
{
	FGraphNodeCreator<UAnimStateNode> Creator(MachineGraph);
	UAnimStateNode* State = Creator.CreateNode(false);
	State->NodePosX = X;
	State->NodePosY = Y;
	State->bAlwaysResetOnEntry = true;
	Creator.Finalize();
	FBlueprintEditorUtils::RenameGraph(State->BoundGraph, Name);
	return State;
}

bool SetStatePose(UAnimStateNode* State, UEdGraphPin* PosePin)
{
	return State && ConnectPins(PosePin, State->GetPoseSinkPinInsideState());
}

UAnimStateTransitionNode* AddTransition(
	UAnimationStateMachineGraph& MachineGraph,
	UAnimStateNode* Source,
	UAnimStateNode* Target,
	const FName RuleProperty,
	const int32 Priority,
	const bool bAutomatic = false,
	const ETransitionLogicType::Type LogicType = ETransitionLogicType::TLT_StandardBlend,
	const float CrossfadeDuration = 0.12f,
	const EAlphaBlendOption BlendMode = EAlphaBlendOption::Linear)
{
	FGraphNodeCreator<UAnimStateTransitionNode> Creator(MachineGraph);
	UAnimStateTransitionNode* Transition = Creator.CreateNode(false);
	Creator.Finalize();
	Transition->CreateConnections(Source, Target);
	// PostPlacedNewNode enables self-transition inertialization during Finalize.
	// Apply the project transition policy only after the node is initialized.
	Transition->PriorityOrder = Priority;
	Transition->CrossfadeDuration = FMath::Max(0.0f, CrossfadeDuration);
	Transition->BlendMode = BlendMode;
	Transition->LogicType = LogicType;
	Transition->bAllowInertializationForSelfTransitions = false;
	Transition->bAutomaticRuleBasedOnSequencePlayerInState = bAutomatic;

	if (!bAutomatic)
	{
		UAnimationTransitionGraph* TransitionGraph = Cast<UAnimationTransitionGraph>(Transition->BoundGraph);
		UAnimGraphNode_TransitionResult* Result = TransitionGraph ? TransitionGraph->GetResultNode() : nullptr;
		if (!Result)
		{
			UE_LOG(LogTemp, Error, TEXT("Transition graph has no result node."));
			return nullptr;
		}

		UK2Node_VariableGet* RuleGetter = AddVariableGetter(*TransitionGraph, RuleProperty, -300, 0);
		if (!ConnectPins(RuleGetter->GetValuePin(), Result->FindPin(TEXT("bCanEnterTransition"))))
		{
			return nullptr;
		}
	}

	return Transition;
}

float ResolveRunTurnbackBlendOutDuration()
{
	if (const URoverMovementConfig* MovementConfig =
		LoadObject<URoverMovementConfig>(nullptr, MovementConfigObjectPath))
	{
		return FMath::Clamp(
			MovementConfig->Settings.RunTurnbackBlendOutDuration,
			0.05f,
			0.5f);
	}
	return 0.22f;
}

bool BuildGroundedPose(UAnimStateNode* State)
{
	UEdGraph& Graph = *State->BoundGraph;
	UAnimGraphNode_SequencePlayer* Stand1 = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stand1")), true, -1500, -500);
	UAnimGraphNode_SequencePlayer* Stand2 = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stand2")), true, -1500, -250);
	UAnimGraphNode_BlendSpacePlayer* Walk = AddBlendSpacePlayer(Graph, LoadRoverAsset<UBlendSpace>(BlendSpaceRoot, TEXT("BS_Rover_Walk")), -1500, 100);
	UAnimGraphNode_BlendSpacePlayer* Run = AddBlendSpacePlayer(Graph, LoadRoverAsset<UBlendSpace>(BlendSpaceRoot, TEXT("BS_Rover_Run")), -1500, 400);
	UAnimGraphNode_SequencePlayer* Sprint = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Sprint_F")), true, -1500, 700);
	if (!Stand1 || !Stand2 || !Walk || !Run || !Sprint)
	{
		return false;
	}

	UAnimGraphNode_TwoWayBlend* IdleBlend = AddFloatBlend(
		Graph,
		Stand1->FindPin(TEXT("Pose")),
		Stand2->FindPin(TEXT("Pose")),
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, IdleStanceAlpha),
		-1150,
		-375);
	UAnimGraphNode_BlendListByBool* WalkBlend = AddBoolBlend(
		Graph,
		Walk->FindPin(TEXT("Pose")),
		IdleBlend ? IdleBlend->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsWalking),
		-850,
		-150);
	UAnimGraphNode_BlendListByBool* RunBlend = AddBoolBlend(
		Graph,
		Run->FindPin(TEXT("Pose")),
		WalkBlend ? WalkBlend->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsRunning),
		-500,
		100);
	UAnimGraphNode_BlendListByBool* SprintBlend = AddBoolBlend(
		Graph,
		Sprint->FindPin(TEXT("Pose")),
		RunBlend ? RunBlend->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsSprinting),
		-200,
		250);

	return SetStatePose(State, SprintBlend ? SprintBlend->FindPin(TEXT("Pose")) : nullptr);
}

bool BuildTurnInPlacePose(UAnimStateNode* State)
{
	UEdGraph& Graph = *State->BoundGraph;
	UAnimGraphNode_SequencePlayer* Left = AddSequencePlayer(
		Graph,
		LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stand1_Turn_L90D")),
		false,
		-700,
		-150);
	UAnimGraphNode_SequencePlayer* Right = AddSequencePlayer(
		Graph,
		LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stand1_Turn_R90D")),
		false,
		-700,
		150);
	UAnimGraphNode_BlendListByBool* DirectionBlend = AddBoolBlend(
		Graph,
		Right ? Right->FindPin(TEXT("Pose")) : nullptr,
		Left ? Left->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bGroundTurnRight),
		-250,
		0);
	return SetStatePose(State, DirectionBlend ? DirectionBlend->FindPin(TEXT("Pose")) : nullptr);
}

bool BuildMoveStopPose(UAnimStateNode* State)
{
	UEdGraph& Graph = *State->BoundGraph;
	UAnimGraphNode_SequencePlayer* WalkLeft = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stop_Walk_L")), false, -1650, -550);
	UAnimGraphNode_SequencePlayer* WalkRight = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stop_Walk_R")), false, -1650, -350);
	UAnimGraphNode_SequencePlayer* RunLeft = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stop_Run_L")), false, -1650, -50);
	UAnimGraphNode_SequencePlayer* RunRight = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stop_Run_R")), false, -1650, 150);
	UAnimGraphNode_SequencePlayer* SprintLeft = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stop_Sprint_L")), false, -1650, 450);
	UAnimGraphNode_SequencePlayer* SprintRight = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Stop_Sprint_R")), false, -1650, 650);
	if (!WalkLeft || !WalkRight || !RunLeft || !RunRight || !SprintLeft || !SprintRight)
	{
		return false;
	}

	UAnimGraphNode_BlendListByBool* WalkFootBlend = AddBoolBlend(
		Graph,
		WalkLeft->FindPin(TEXT("Pose")),
		WalkRight->FindPin(TEXT("Pose")),
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bMoveStopUseLeftVariant),
		-1250,
		-450);
	UAnimGraphNode_BlendListByBool* RunFootBlend = AddBoolBlend(
		Graph,
		RunLeft->FindPin(TEXT("Pose")),
		RunRight->FindPin(TEXT("Pose")),
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bMoveStopUseLeftVariant),
		-1250,
		50);
	UAnimGraphNode_BlendListByBool* SprintFootBlend = AddBoolBlend(
		Graph,
		SprintLeft->FindPin(TEXT("Pose")),
		SprintRight->FindPin(TEXT("Pose")),
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bMoveStopUseLeftVariant),
		-1250,
		550);
	UAnimGraphNode_BlendListByBool* RunOrWalkBlend = AddBoolBlend(
		Graph,
		RunFootBlend ? RunFootBlend->FindPin(TEXT("Pose")) : nullptr,
		WalkFootBlend ? WalkFootBlend->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bMoveStopWasRunning),
		-800,
		-150);
	UAnimGraphNode_BlendListByBool* GaitBlend = AddBoolBlend(
		Graph,
		SprintFootBlend ? SprintFootBlend->FindPin(TEXT("Pose")) : nullptr,
		RunOrWalkBlend ? RunOrWalkBlend->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bMoveStopWasSprinting),
		-350,
		100);
	return SetStatePose(State, GaitBlend ? GaitBlend->FindPin(TEXT("Pose")) : nullptr);
}

bool BuildAirbornePose(UAnimStateNode* State)
{
	UEdGraph& Graph = *State->BoundGraph;
	UAnimGraphNode_SequencePlayer* Jump = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Jump_Loop")), true, -1000, -200);
	UAnimGraphNode_SequencePlayer* Fall = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Fall_Loop")), true, -1000, 100);
	UAnimGraphNode_SequencePlayer* FastFall = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Fall_Loop_Fast")), true, -1000, 400);
	UAnimGraphNode_TwoWayBlend* FallBlend = AddFloatBlend(
		Graph,
		Fall ? Fall->FindPin(TEXT("Pose")) : nullptr,
		FastFall ? FastFall->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, FastFallAlpha),
		-600,
		250);
	UAnimGraphNode_BlendListByBool* AirBlend = AddBoolBlend(
		Graph,
		FallBlend ? FallBlend->FindPin(TEXT("Pose")) : nullptr,
		Jump ? Jump->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsDescending),
		-200,
		50);
	return SetStatePose(State, AirBlend ? AirBlend->FindPin(TEXT("Pose")) : nullptr);
}

bool BuildJumpStartPose(UAnimStateNode* State)
{
	UEdGraph& Graph = *State->BoundGraph;
	UAnimGraphNode_SequencePlayer* WalkLeft = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Jump_Walk_LF")), false, -1200, -350);
	UAnimGraphNode_SequencePlayer* WalkRight = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Jump_Walk_RF")), false, -1200, -100);
	UAnimGraphNode_SequencePlayer* RunLeft = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Jump_Run_LF")), false, -1200, 200);
	UAnimGraphNode_SequencePlayer* RunRight = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Jump_Run_RF")), false, -1200, 450);

	UAnimGraphNode_BlendListByBool* WalkFootBlend = AddBoolBlend(
		Graph,
		WalkLeft ? WalkLeft->FindPin(TEXT("Pose")) : nullptr,
		WalkRight ? WalkRight->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpOffLeftFoot),
		-800,
		-225);
	UAnimGraphNode_BlendListByBool* RunFootBlend = AddBoolBlend(
		Graph,
		RunLeft ? RunLeft->FindPin(TEXT("Pose")) : nullptr,
		RunRight ? RunRight->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpOffLeftFoot),
		-800,
		325);
	UAnimGraphNode_BlendListByBool* GaitBlend = AddBoolBlend(
		Graph,
		RunFootBlend ? RunFootBlend->FindPin(TEXT("Pose")) : nullptr,
		WalkFootBlend ? WalkFootBlend->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpStartedFromRun),
		-350,
		50);
	return SetStatePose(State, GaitBlend ? GaitBlend->FindPin(TEXT("Pose")) : nullptr);
}

bool BuildSecondJumpPose(UAnimStateNode* State)
{
	UEdGraph& Graph = *State->BoundGraph;
	UAnimGraphNode_SequencePlayer* Forward = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Jump_Second_F")), false, -700, -100);
	UAnimGraphNode_SequencePlayer* Backward = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, TEXT("Jump_Second_B")), false, -700, 200);
	UAnimGraphNode_BlendListByBool* DirectionBlend = AddBoolBlend(
		Graph,
		Backward ? Backward->FindPin(TEXT("Pose")) : nullptr,
		Forward ? Forward->FindPin(TEXT("Pose")) : nullptr,
		GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bSecondJumpBackward),
		-250,
		50);
	return SetStatePose(State, DirectionBlend ? DirectionBlend->FindPin(TEXT("Pose")) : nullptr);
}

bool BuildSequenceState(UAnimStateNode* State, const TCHAR* SequenceName, const bool bLoop = false)
{
	UEdGraph& Graph = *State->BoundGraph;
	UAnimGraphNode_SequencePlayer* Player = AddSequencePlayer(Graph, LoadRoverAsset<UAnimSequence>(AnimationRoot, SequenceName), bLoop, -350, 0);
	return SetStatePose(State, Player ? Player->FindPin(TEXT("Pose")) : nullptr);
}

USkeletalMeshSocket* CreateOrUpdateSkeletonSocket(
	USkeleton& Skeleton,
	const FName SocketName,
	const FName BoneName)
{
	if (Skeleton.GetReferenceSkeleton().FindBoneIndex(BoneName) == INDEX_NONE)
	{
		return nullptr;
	}

	Skeleton.Modify();
	USkeletalMeshSocket* Socket = Skeleton.FindSocket(SocketName);
	if (!Socket)
	{
		Socket = NewObject<USkeletalMeshSocket>(&Skeleton, NAME_None, RF_Transactional);
		Skeleton.Sockets.Add(Socket);
	}
	Socket->Modify();
	Socket->SocketName = SocketName;
	Socket->BoneName = BoneName;
	Socket->RelativeLocation = FVector::ZeroVector;
	Socket->RelativeRotation = FRotator::ZeroRotator;
	Socket->RelativeScale = FVector::OneVector;
	Skeleton.MarkPackageDirty();
	return Socket;
}

FName ResolveCharacterWeaponBone(const USkeleton& Skeleton)
{
	const FName Candidates[] = {
		TEXT("Bip001RHand"),
		TEXT("hand_r"),
		TEXT("RightHand")};
	for (const FName Candidate : Candidates)
	{
		if (Skeleton.GetReferenceSkeleton().FindBoneIndex(Candidate) != INDEX_NONE)
		{
			return Candidate;
		}
	}
	return NAME_None;
}

void AddNamedMontageNotify(UAnimMontage& Montage, USkeleton& Skeleton, const FName NotifyName, const float Time)
{
	FAnimNotifyEvent& Notify = Montage.Notifies.AddDefaulted_GetRef();
	Notify.NotifyName = NotifyName;
#if WITH_EDITORONLY_DATA
	Notify.Guid = FGuid::NewGuid();
#endif
	Notify.Link(&Montage, FMath::Clamp(Time, 0.0f, Montage.GetPlayLength()));
	Notify.TrackIndex = 0;
	Skeleton.AddNewAnimationNotify(NotifyName);
}

void AddComboWindowNotifyState(UAnimMontage& Montage, const float StartNormalized)
{
	const float MontageLength = Montage.GetPlayLength();
	const float StartTime = MontageLength * FMath::Clamp(StartNormalized, 0.0f, 1.0f);
	FAnimNotifyEvent& Notify = Montage.Notifies.AddDefaulted_GetRef();
	Notify.NotifyStateClass = NewObject<URoverAnimNotifyState_ComboWindow>(&Montage);
	Notify.NotifyName = TEXT("ComboWindow");
#if WITH_EDITORONLY_DATA
	Notify.Guid = FGuid::NewGuid();
#endif
	Notify.Link(&Montage, StartTime);
	Notify.TrackIndex = 0;
	Notify.SetDuration(FMath::Max(0.0f, MontageLength - StartTime));
	Notify.EndLink.Link(&Montage, Notify.EndLink.GetTime());
}

void AddResonanceWindowNotifyState(UAnimMontage& Montage, const float StartNormalized)
{
	const float MontageLength = Montage.GetPlayLength();
	const float StartTime = MontageLength * FMath::Clamp(StartNormalized, 0.0f, 1.0f);
	FAnimNotifyEvent& Notify = Montage.Notifies.AddDefaulted_GetRef();
	Notify.NotifyStateClass = NewObject<URoverAnimNotifyState_ResonanceWindow>(&Montage);
	Notify.NotifyName = TEXT("ResonanceWindow");
#if WITH_EDITORONLY_DATA
	Notify.Guid = FGuid::NewGuid();
#endif
	Notify.Link(&Montage, StartTime);
	Notify.TrackIndex = 0;
	Notify.SetDuration(FMath::Max(0.0f, MontageLength - StartTime));
	Notify.EndLink.Link(&Montage, Notify.EndLink.GetTime());
}

UAnimMontage* CreateOrUpdateMontage(
	IAssetTools& AssetTools,
	UAnimSequence& SourceSequence,
	const TCHAR* AssetName,
	const TArray<TPair<FName, float>>& Notifies,
	const FRoverAttackDefinition* AttackDefinition = nullptr)
{
	USkeleton* Skeleton = SourceSequence.GetSkeleton();
	if (!Skeleton || SourceSequence.GetPlayLength() <= UE_SMALL_NUMBER)
	{
		return nullptr;
	}

	const FString AssetPath = FString::Printf(TEXT("%s/%s"), CombatMontageRoot, AssetName);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, AssetName);
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *ObjectPath);
	if (!Montage)
	{
		UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
		Factory->TargetSkeleton = Skeleton;
		Factory->SourceAnimation = &SourceSequence;
		Montage = Cast<UAnimMontage>(AssetTools.CreateAsset(
			AssetName,
			CombatMontageRoot,
			UAnimMontage::StaticClass(),
			Factory));
	}
	if (!Montage)
	{
		return nullptr;
	}

	Montage->Modify();
	Montage->SetSkeleton(Skeleton);
	Montage->SlotAnimTracks.Reset();
	FSlotAnimationTrack SlotTrack;
	SlotTrack.SlotName = TEXT("DefaultSlot");
	FAnimSegment Segment;
	Segment.SetAnimReference(&SourceSequence, true);
	SlotTrack.AnimTrack.AnimSegments.Add(Segment);
	Montage->SlotAnimTracks.Add(MoveTemp(SlotTrack));
	Montage->SetCompositeLength(SourceSequence.GetPlayLength());
	Montage->CompositeSections.Reset();
	UAnimMontageFactory::EnsureStartingSection(Montage);
	if (AttackDefinition)
	{
		Montage->BlendModeIn = EMontageBlendMode::Standard;
		Montage->BlendModeOut = EMontageBlendMode::Standard;
		Montage->BlendIn.SetBlendTime(FMath::Max(0.0f, AttackDefinition->MontageBlendInTime));
		Montage->BlendOut.SetBlendTime(FMath::Max(0.0f, AttackDefinition->MontageBlendOutTime));
		Montage->BlendOutTriggerTime = FMath::Max(0.0f, AttackDefinition->MontageBlendOutTriggerTime);
		Montage->bEnableAutoBlendOut = true;
	}
	Montage->Notifies.Reset();
	for (const TPair<FName, float>& Notify : Notifies)
	{
		AddNamedMontageNotify(*Montage, *Skeleton, Notify.Key, Notify.Value);
	}
	if (AttackDefinition)
	{
		AddComboWindowNotifyState(*Montage, AttackDefinition->ComboWindowStartNormalized);
	}
	Montage->SortNotifies();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	Skeleton->MarkPackageDirty();
	return Montage;
}

URoverCombatConfig* CreateOrLoadCombatConfig(IAssetTools& AssetTools)
{
	const FString ObjectPath = FString::Printf(
		TEXT("%s.%s"),
		CombatConfigAssetPath,
		*FPackageName::GetLongPackageAssetName(CombatConfigAssetPath));
	if (URoverCombatConfig* Existing = LoadObject<URoverCombatConfig>(nullptr, *ObjectPath))
	{
		return Existing;
	}

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = URoverCombatConfig::StaticClass();
	return Cast<URoverCombatConfig>(AssetTools.CreateAsset(
		FPackageName::GetLongPackageAssetName(CombatConfigAssetPath),
		FPackageName::GetLongPackagePath(CombatConfigAssetPath),
		URoverCombatConfig::StaticClass(),
		Factory));
}

bool HasNamedNotifies(const UAnimMontage& Montage, const TArray<FName>& ExpectedNames)
{
	if (Montage.Notifies.Num() != ExpectedNames.Num())
	{
		return false;
	}
	for (const FName ExpectedName : ExpectedNames)
	{
		if (!Montage.Notifies.ContainsByPredicate(
			[ExpectedName](const FAnimNotifyEvent& Notify)
			{
				return Notify.NotifyName == ExpectedName;
			}))
		{
			return false;
		}
	}
	return true;
}

const FAnimNotifyEvent* FindNamedNotify(const UAnimMontage& Montage, const FName NotifyName)
{
	return Montage.Notifies.FindByPredicate(
		[NotifyName](const FAnimNotifyEvent& Notify)
		{
			return Notify.NotifyName == NotifyName;
		});
}

bool HasValidAttackNotifyOrder(const UAnimMontage& Montage)
{
	if (Montage.Notifies.Num() != 6)
	{
		return false;
	}
	const FAnimNotifyEvent* Started = FindNamedNotify(Montage, TEXT("RoverAttackStarted"));
	const FAnimNotifyEvent* ActiveBegin = FindNamedNotify(Montage, TEXT("RoverAttackActiveBegin"));
	const FAnimNotifyEvent* ActiveEnd = FindNamedNotify(Montage, TEXT("RoverAttackActiveEnd"));
	const FAnimNotifyEvent* ComboBegin = FindNamedNotify(Montage, TEXT("ComboWindow"));
	const FAnimNotifyEvent* RecoveryBegin = FindNamedNotify(Montage, TEXT("RoverRecoveryBegin"));
	const FAnimNotifyEvent* Finished = FindNamedNotify(Montage, TEXT("RoverAttackFinished"));
	if (!Started || !ActiveBegin || !ActiveEnd || !ComboBegin || !RecoveryBegin || !Finished)
	{
		return false;
	}
	return Started->GetTriggerTime() < ActiveBegin->GetTriggerTime() &&
		ActiveBegin->GetTriggerTime() < ActiveEnd->GetTriggerTime() &&
		ActiveEnd->GetTriggerTime() <= ComboBegin->GetTriggerTime() &&
		ComboBegin->GetTriggerTime() < Finished->GetTriggerTime() &&
		ActiveEnd->GetTriggerTime() < RecoveryBegin->GetTriggerTime() &&
		RecoveryBegin->GetTriggerTime() < Finished->GetTriggerTime();
}

bool HasValidComboWindowState(
	const UAnimMontage& Montage,
	const FRoverAttackDefinition& Definition)
{
	const FAnimNotifyEvent* ComboWindow = FindNamedNotify(Montage, TEXT("ComboWindow"));
	if (!ComboWindow)
	{
		return false;
	}
	const float MontageLength = Montage.GetPlayLength();
	const float ExpectedStart = MontageLength *
		FMath::Clamp(Definition.ComboWindowStartNormalized, 0.0f, 1.0f);
	return ComboWindow->NotifyStateClass &&
		ComboWindow->NotifyStateClass->IsA<URoverAnimNotifyState_ComboWindow>() &&
		FMath::IsNearlyEqual(ComboWindow->GetTriggerTime(), ExpectedStart, 0.002f) &&
		FMath::IsNearlyEqual(ComboWindow->GetEndTriggerTime(), MontageLength, 0.002f);
}

bool HasValidResonanceWindowState(const UAnimMontage& Montage, const float StartNormalized)
{
	const FAnimNotifyEvent* ResonanceWindow = FindNamedNotify(Montage, TEXT("ResonanceWindow"));
	if (!ResonanceWindow)
	{
		return false;
	}
	const float MontageLength = Montage.GetPlayLength();
	const float ExpectedStart = MontageLength * FMath::Clamp(StartNormalized, 0.0f, 1.0f);
	return ResonanceWindow->NotifyStateClass &&
		ResonanceWindow->NotifyStateClass->IsA<URoverAnimNotifyState_ResonanceWindow>() &&
		FMath::IsNearlyEqual(ResonanceWindow->GetTriggerTime(), ExpectedStart, 0.002f) &&
		FMath::IsNearlyEqual(ResonanceWindow->GetEndTriggerTime(), MontageLength, 0.002f);
}

bool HasValidAttackAssetSettings(
	const UAnimSequence& Sequence,
	const UAnimMontage& Montage,
	const FRoverAttackDefinition& Definition)
{
	const float MontageLength = Montage.GetPlayLength();
	const float BlendOutTriggerNormalized = MontageLength > UE_SMALL_NUMBER
		? 1.0f - (Montage.BlendOutTriggerTime / MontageLength)
		: 0.0f;
	return !Sequence.bEnableRootMotion &&
		Sequence.bForceRootLock &&
		Sequence.RootMotionRootLock == ERootMotionRootLock::RefPose &&
		Montage.bEnableAutoBlendOut &&
		Montage.BlendModeIn == EMontageBlendMode::Standard &&
		Montage.BlendModeOut == EMontageBlendMode::Standard &&
		FMath::IsNearlyEqual(Montage.GetDefaultBlendInTime(), Definition.MontageBlendInTime) &&
		FMath::IsNearlyEqual(Montage.GetDefaultBlendOutTime(), Definition.MontageBlendOutTime) &&
		FMath::IsNearlyEqual(Montage.BlendOutTriggerTime, Definition.MontageBlendOutTriggerTime) &&
		BlendOutTriggerNormalized >= 0.80f;
}
}

bool URoverAnimationEditorLibrary::ResampleBlendSpace(UBlendSpace* BlendSpace)
{
	if (!IsValid(BlendSpace))
	{
		return false;
	}

	BlendSpace->Modify();
	BlendSpace->ResampleData();
	BlendSpace->MarkPackageDirty();
	return true;
}

int32 URoverAnimationEditorLibrary::GetBlendSpaceRuntimeSegmentCount(const UBlendSpace* BlendSpace)
{
	return IsValid(BlendSpace) ? BlendSpace->GetBlendSpaceData().Segments.Num() : 0;
}

bool URoverAnimationEditorLibrary::BakeMoveStopRootMotion(
	UAnimSequence* AnimationSequence,
	FString& ValidationReport)
{
	FMoveStopRootMotionData SourceData;
	if (!ReadMoveStopRootMotionData(AnimationSequence, SourceData, ValidationReport))
	{
		UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion bake rejected: %s"), *ValidationReport);
		return false;
	}

	const EMoveStopRootMotionState SourceState =
		ClassifyMoveStopRootMotion(SourceData, ValidationReport, *AnimationSequence);
	if (SourceState == EMoveStopRootMotionState::Ambiguous)
	{
		UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion bake rejected: %s"), *ValidationReport);
		return false;
	}

	const bool bHasExpectedSettings =
		AnimationSequence->bEnableRootMotion &&
		AnimationSequence->RootMotionRootLock == ERootMotionRootLock::AnimFirstFrame &&
		AnimationSequence->bForceRootLock &&
		!AnimationSequence->bUseNormalizedRootMotionScale;
	const bool bNeedsTrajectoryStabilization =
		SourceData.RootTerminalReboundXY > MoveStopBacktrackTolerance ||
		SourceData.RootMaxBackwardStepXY > MoveStopBacktrackTolerance ||
		SourceData.BodyTerminalReboundXY > MoveStopBacktrackTolerance ||
		SourceData.BodyMaxBackwardStepXY > MoveStopBacktrackTolerance;
	const bool bNeedsTrackRewrite =
		SourceState == EMoveStopRootMotionState::NeedsBake || bNeedsTrajectoryStabilization;
	if (SourceState == EMoveStopRootMotionState::AlreadyBaked &&
		!bNeedsTrajectoryStabilization &&
		bHasExpectedSettings)
	{
		ValidationReport = FString::Printf(
			TEXT("%s state=already_baked root_motion=true root_lock=AnimFirstFrame force_root_lock=true normalized_scale=false"),
			*MoveStopMetricReport(*AnimationSequence, SourceData));
		return true;
	}

	TArray<FVector3f> RootPositionKeys;
	TArray<FQuat4f> RootRotationKeys;
	TArray<FVector3f> RootScaleKeys;
	TArray<FVector3f> BodyPositionKeys;
	TArray<FQuat4f> BodyRotationKeys;
	TArray<FVector3f> BodyScaleKeys;
	if (bNeedsTrackRewrite)
	{
		const int32 NumberOfKeys = SourceData.RootLocalTransforms.Num();
		RootPositionKeys.Reserve(NumberOfKeys);
		RootRotationKeys.Reserve(NumberOfKeys);
		RootScaleKeys.Reserve(NumberOfKeys);
		BodyPositionKeys.Reserve(NumberOfKeys);
		BodyRotationKeys.Reserve(NumberOfKeys);
		BodyScaleKeys.Reserve(NumberOfKeys);

		const FVector FirstRootLocation = SourceData.RootLocalTransforms[0].GetTranslation();
		const FVector FirstBodyLocation = SourceData.BodyComponentTransforms[0].GetTranslation();
		const FVector FinalBodyDelta = PlanarVector(
			SourceData.BodyComponentTransforms.Last().GetTranslation() - FirstBodyLocation);
		const FVector TravelDirection = FinalBodyDelta.GetSafeNormal();
		if (TravelDirection.IsNearlyZero())
		{
			ValidationReport = FString::Printf(
				TEXT("%s state=bake_rejected; Bip001 has no usable final planar travel direction."),
				*MoveStopMetricReport(*AnimationSequence, SourceData));
			UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion bake rejected: %s"), *ValidationReport);
			return false;
		}
		const double FinalForward = FVector::DotProduct(FinalBodyDelta, TravelDirection);
		double StableForward = 0.0;
		for (int32 KeyIndex = 0; KeyIndex < NumberOfKeys; ++KeyIndex)
		{
			const FTransform& OldRootTransform = SourceData.RootLocalTransforms[KeyIndex];
			const FTransform& OldBodyComponentTransform = SourceData.BodyComponentTransforms[KeyIndex];
			const FVector RootDelta = PlanarVector(
				OldRootTransform.GetTranslation() - FirstRootLocation);
			const FVector BodyDelta = PlanarVector(
				OldBodyComponentTransform.GetTranslation() - FirstBodyLocation);
			const double OriginalForward = FVector::DotProduct(BodyDelta, TravelDirection);
			StableForward = FMath::Min(
				FinalForward,
				FMath::Max(StableForward, OriginalForward));
			const FVector StabilizedBodyDelta =
				BodyDelta + TravelDirection * (StableForward - OriginalForward);
			const FVector TrajectoryCorrection = StabilizedBodyDelta - BodyDelta;

			FTransform NewRootTransform = OldRootTransform;
			NewRootTransform.AddToTranslation(StabilizedBodyDelta - RootDelta);
			FTransform CorrectedBodyComponentTransform = OldBodyComponentTransform;
			CorrectedBodyComponentTransform.AddToTranslation(TrajectoryCorrection);
			const FTransform NewBodyLocalTransform =
				CorrectedBodyComponentTransform.GetRelativeTransform(NewRootTransform);
			const FTransform ReconstructedBodyComponent =
				NewBodyLocalTransform * NewRootTransform;
			if (NewRootTransform.ContainsNaN() ||
				NewBodyLocalTransform.ContainsNaN() ||
				!ReconstructedBodyComponent.Equals(
					CorrectedBodyComponentTransform,
					MoveStopPoseTolerance))
			{
				ValidationReport = FString::Printf(
					TEXT("%s state=bake_rejected; Bip001 component pose was not preserved at key %d."),
					*MoveStopMetricReport(*AnimationSequence, SourceData),
					KeyIndex);
				UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion bake rejected: %s"), *ValidationReport);
				return false;
			}

			AppendTransformKeys(
				NewRootTransform,
				RootPositionKeys,
				RootRotationKeys,
				RootScaleKeys);
			AppendTransformKeys(
				NewBodyLocalTransform,
				BodyPositionKeys,
				BodyRotationKeys,
				BodyScaleKeys);
		}
	}

	AnimationSequence->WaitOnExistingCompression();
	AnimationSequence->Modify();
	bool bTracksWritten = true;
	if (bNeedsTrackRewrite)
	{
		IAnimationDataController& Controller = AnimationSequence->GetController();
		IAnimationDataController::FScopedBracket Bracket(
			Controller,
			FText::FromString(TEXT("Bake and stabilize Rover move-stop root motion")),
			false);
		AnimationSequence->bEnableRootMotion = true;
		AnimationSequence->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
		AnimationSequence->bForceRootLock = true;
		AnimationSequence->bUseNormalizedRootMotionScale = false;
		bTracksWritten = Controller.SetBoneTrackKeys(
			MoveStopRootBoneName,
			RootPositionKeys,
			RootRotationKeys,
			RootScaleKeys,
			false);
		bTracksWritten = Controller.SetBoneTrackKeys(
			MoveStopBodyBoneName,
			BodyPositionKeys,
			BodyRotationKeys,
			BodyScaleKeys,
			false) && bTracksWritten;
	}
	else
	{
		AnimationSequence->bEnableRootMotion = true;
		AnimationSequence->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
		AnimationSequence->bForceRootLock = true;
		AnimationSequence->bUseNormalizedRootMotionScale = false;
	}

	AnimationSequence->PostEditChange();
	AnimationSequence->MarkPackageDirty();
	AnimationSequence->CacheDerivedDataForCurrentPlatform();
	if (!bTracksWritten)
	{
		ValidationReport = FString::Printf(
			TEXT("%s state=bake_failed; the animation data controller rejected a root or Bip001 track."),
			*MoveStopMetricReport(*AnimationSequence, SourceData));
		UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion bake failed: %s"), *ValidationReport);
		return false;
	}

	FString PostBakeReport;
	if (!ValidateMoveStopRootMotion(AnimationSequence, PostBakeReport))
	{
		ValidationReport = FString::Printf(
			TEXT("state=post_bake_validation_failed before=(%s) after=(%s)"),
			*MoveStopMetricReport(*AnimationSequence, SourceData),
			*PostBakeReport);
		UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion bake failed: %s"), *ValidationReport);
		return false;
	}

	ValidationReport = FString::Printf(
		TEXT("state=%s before=(%s) after=(%s)"),
		SourceState == EMoveStopRootMotionState::NeedsBake
			? (bNeedsTrajectoryStabilization ? TEXT("baked_stabilized") : TEXT("baked"))
			: (bNeedsTrajectoryStabilization ? TEXT("stabilized_existing_bake") : TEXT("configured_existing_bake")),
		*MoveStopMetricReport(*AnimationSequence, SourceData),
		*PostBakeReport);
	return true;
}

bool URoverAnimationEditorLibrary::ValidateMoveStopRootMotion(
	const UAnimSequence* AnimationSequence,
	FString& ValidationReport)
{
	FMoveStopRootMotionData Data;
	if (!ReadMoveStopRootMotionData(AnimationSequence, Data, ValidationReport))
	{
		UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion validation failed: %s"), *ValidationReport);
		return false;
	}

	const EMoveStopRootMotionState State =
		ClassifyMoveStopRootMotion(Data, ValidationReport, *AnimationSequence);
	if (State == EMoveStopRootMotionState::Ambiguous)
	{
		UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion validation failed: %s"), *ValidationReport);
		return false;
	}
	if (State == EMoveStopRootMotionState::NeedsBake)
	{
		ValidationReport = FString::Printf(
			TEXT("%s state=needs_bake; Bip001 still owns the accumulated planar trajectory."),
			*MoveStopMetricReport(*AnimationSequence, Data));
		UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion validation failed: %s"), *ValidationReport);
		return false;
	}
	if (Data.RootTerminalReboundXY > MoveStopBacktrackTolerance ||
		Data.RootMaxBackwardStepXY > MoveStopBacktrackTolerance ||
		Data.BodyTerminalReboundXY > MoveStopBacktrackTolerance ||
		Data.BodyMaxBackwardStepXY > MoveStopBacktrackTolerance)
	{
		ValidationReport = FString::Printf(
			TEXT("%s state=trajectory_unstable; move-stop planar travel reverses before the final pose."),
			*MoveStopMetricReport(*AnimationSequence, Data));
		UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion validation failed: %s"), *ValidationReport);
		return false;
	}
	if (!AnimationSequence->bEnableRootMotion ||
		AnimationSequence->RootMotionRootLock != ERootMotionRootLock::AnimFirstFrame ||
		!AnimationSequence->bForceRootLock ||
		AnimationSequence->bUseNormalizedRootMotionScale)
	{
		ValidationReport = FString::Printf(
			TEXT("%s state=settings_invalid root_motion=%s root_lock=%d force_root_lock=%s normalized_scale=%s"),
			*MoveStopMetricReport(*AnimationSequence, Data),
			AnimationSequence->bEnableRootMotion ? TEXT("true") : TEXT("false"),
			static_cast<int32>(AnimationSequence->RootMotionRootLock.GetValue()),
			AnimationSequence->bForceRootLock ? TEXT("true") : TEXT("false"),
			AnimationSequence->bUseNormalizedRootMotionScale ? TEXT("true") : TEXT("false"));
		UE_LOG(LogTemp, Error, TEXT("Move-stop root-motion validation failed: %s"), *ValidationReport);
		return false;
	}

	ValidationReport = FString::Printf(
		TEXT("%s state=valid root_motion=true root_lock=AnimFirstFrame force_root_lock=true normalized_scale=false"),
		*MoveStopMetricReport(*AnimationSequence, Data));
	return true;
}

UAnimBlueprint* URoverAnimationEditorLibrary::CreateRoverAnimBlueprint(
	USkeletalMesh* PreviewMesh,
	const FString& AssetPath)
{
	if (!IsValid(PreviewMesh) || !IsValid(PreviewMesh->GetSkeleton()))
	{
		UE_LOG(LogTemp, Error, TEXT("A Rover preview mesh with a valid skeleton is required."));
		return nullptr;
	}
	if (!FPackageName::IsValidLongPackageName(AssetPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid AnimBlueprint asset path: %s"), *AssetPath);
		return nullptr;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
	if (LoadObject<UAnimBlueprint>(nullptr, *ObjectPath))
	{
		UE_LOG(LogTemp, Error, TEXT("AnimBlueprint already exists; delete it before regeneration: %s"), *ObjectPath);
		return nullptr;
	}

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->ParentClass = URoverAnimInstance::StaticClass();
	Factory->TargetSkeleton = PreviewMesh->GetSkeleton();
	Factory->PreviewSkeletalMesh = PreviewMesh;
	Factory->bTemplate = false;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetTools.CreateAsset(
		AssetName,
		PackagePath,
		UAnimBlueprint::StaticClass(),
		Factory));
	if (!AnimBlueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create Rover AnimBlueprint: %s"), *AssetPath);
		return nullptr;
	}

	UAnimationGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
		{
			AnimGraph = Cast<UAnimationGraph>(Graph);
			break;
		}
	}
	if (!AnimGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("New AnimBlueprint has no AnimGraph."));
		return nullptr;
	}

	UAnimGraphNode_Root* Root = nullptr;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if ((Root = Cast<UAnimGraphNode_Root>(Node)) != nullptr)
		{
			break;
		}
	}
	if (!Root)
	{
		UE_LOG(LogTemp, Error, TEXT("New AnimBlueprint has no output pose node."));
		return nullptr;
	}

	FGraphNodeCreator<UAnimGraphNode_StateMachine> MachineCreator(*AnimGraph);
	UAnimGraphNode_StateMachine* StateMachine = MachineCreator.CreateNode(false);
	StateMachine->NodePosX = -1900;
	StateMachine->NodePosY = 0;
	MachineCreator.Finalize();
	FBlueprintEditorUtils::RenameGraph(StateMachine->EditorStateMachineGraph, TEXT("RoverLocomotion"));

	FGraphNodeCreator<UAnimGraphNode_Slot> SlotCreator(*AnimGraph);
	UAnimGraphNode_Slot* CombatSlot = SlotCreator.CreateNode(false);
	CombatSlot->NodePosX = -1600;
	CombatSlot->NodePosY = 0;
	CombatSlot->Node.SlotName = TEXT("DefaultSlot");
	CombatSlot->Node.bAlwaysUpdateSourcePose = true;
	SlotCreator.Finalize();

	FGraphNodeCreator<UAnimGraphNode_Inertialization> InertializationCreator(*AnimGraph);
	UAnimGraphNode_Inertialization* Inertialization = InertializationCreator.CreateNode(false);
	Inertialization->NodePosX = -1300;
	Inertialization->NodePosY = 0;
	InertializationCreator.Finalize();

	if (!ConnectPins(StateMachine->FindPin(TEXT("Pose")), CombatSlot->FindPin(TEXT("Source"))) ||
		!ConnectPins(CombatSlot->FindPin(TEXT("Pose")), Inertialization->FindPin(TEXT("Source"))) ||
		!ConnectPins(Inertialization->FindPin(TEXT("Pose")), Root->FindPin(TEXT("Result"))))
	{
		return nullptr;
	}

	UAnimationStateMachineGraph* MachineGraph = StateMachine->EditorStateMachineGraph;
	UAnimStateNode* Grounded = AddState(*MachineGraph, TEXT("Grounded"), 0, 0);
	UAnimStateNode* SprintImpulse = AddState(*MachineGraph, TEXT("SprintImpulse"), 300, -250);
	UAnimStateNode* JumpStart = AddState(*MachineGraph, TEXT("JumpStart"), 600, -250);
	UAnimStateNode* Airborne = AddState(*MachineGraph, TEXT("Airborne"), 900, 0);
	UAnimStateNode* SecondJump = AddState(*MachineGraph, TEXT("SecondJump"), 1200, -250);
	UAnimStateNode* LandLight = AddState(*MachineGraph, TEXT("LandLight"), 1200, 0);
	UAnimStateNode* LandHeavy = AddState(*MachineGraph, TEXT("LandHeavy"), 1200, 250);
	UAnimStateNode* LandRoll = AddState(*MachineGraph, TEXT("LandRoll"), 1200, 500);
	UAnimStateNode* TurnInPlace = AddState(*MachineGraph, TEXT("TurnInPlace"), 300, 250);
	UAnimStateNode* RunTurnback = AddState(*MachineGraph, TEXT("RunTurnback"), 300, 500);
	UAnimStateNode* MoveStop = AddState(*MachineGraph, TEXT("MoveStop"), 300, 750);
	TurnInPlace->StateEntered.NotifyName = TEXT("RoverGroundTurnEntered");
	TurnInPlace->StateLeft.NotifyName = TEXT("RoverGroundTurnExited");
	RunTurnback->StateEntered.NotifyName = TEXT("RoverGroundTurnEntered");
	RunTurnback->StateLeft.NotifyName = TEXT("RoverGroundTurnExited");
	MoveStop->StateEntered.NotifyName = TEXT("RoverMoveStopEntered");
	MoveStop->StateLeft.NotifyName = TEXT("RoverMoveStopExited");

	const bool bPosesBuilt =
		BuildGroundedPose(Grounded) &&
		BuildSequenceState(SprintImpulse, TEXT("Sprint_Impulse_F")) &&
		BuildJumpStartPose(JumpStart) &&
		BuildAirbornePose(Airborne) &&
		BuildSecondJumpPose(SecondJump) &&
		BuildSequenceState(LandLight, TEXT("Land_Light")) &&
		BuildSequenceState(LandHeavy, TEXT("Land_Heavy")) &&
		BuildSequenceState(LandRoll, TEXT("Land_Roll")) &&
		BuildTurnInPlacePose(TurnInPlace) &&
		BuildSequenceState(RunTurnback, TEXT("Run_Turnback")) &&
		BuildMoveStopPose(MoveStop);
	if (!bPosesBuilt)
	{
		return nullptr;
	}

	if (!ConnectPins(MachineGraph->EntryNode->GetOutputPin(), Grounded->GetInputPin()))
	{
		return nullptr;
	}

	const bool bTransitionsBuilt =
		AddTransition(*MachineGraph, Grounded, JumpStart, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpStartRequested), 1) &&
		AddTransition(*MachineGraph, Grounded, SprintImpulse, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bSprintImpulseRequested), 2) &&
		AddTransition(*MachineGraph, Grounded, Airborne, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsFalling), 3) &&
		AddTransition(*MachineGraph, Grounded, MoveStop, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bMoveStopRequested), 4) &&
		AddTransition(*MachineGraph, Grounded, RunTurnback, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bRunTurnbackRequested), 5) &&
		AddTransition(*MachineGraph, Grounded, TurnInPlace, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bTurnInPlaceRequested), 6) &&
		AddTransition(*MachineGraph, SprintImpulse, JumpStart, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpStartRequested), 1) &&
		AddTransition(*MachineGraph, SprintImpulse, Airborne, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsFalling), 2) &&
		AddTransition(*MachineGraph, SprintImpulse, Grounded, NAME_None, 3, true) &&
		AddTransition(*MachineGraph, JumpStart, SecondJump, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bSecondJumpRequested), 1) &&
		AddTransition(*MachineGraph, JumpStart, LandRoll, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bLandingRoll), 2) &&
		AddTransition(*MachineGraph, JumpStart, LandHeavy, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bLandingHeavy), 3) &&
		AddTransition(*MachineGraph, JumpStart, LandLight, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bLandingLight), 4) &&
		AddTransition(*MachineGraph, JumpStart, Grounded, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bGroundedWithoutLanding), 5) &&
		AddTransition(*MachineGraph, JumpStart, Airborne, NAME_None, 6, true) &&
		AddTransition(*MachineGraph, Airborne, SecondJump, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bSecondJumpRequested), 1) &&
		AddTransition(*MachineGraph, Airborne, LandRoll, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bLandingRoll), 2) &&
		AddTransition(*MachineGraph, Airborne, LandHeavy, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bLandingHeavy), 3) &&
		AddTransition(*MachineGraph, Airborne, LandLight, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bLandingLight), 4) &&
		AddTransition(*MachineGraph, Airborne, Grounded, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bGroundedWithoutLanding), 5) &&
		AddTransition(*MachineGraph, SecondJump, LandRoll, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bLandingRoll), 1) &&
		AddTransition(*MachineGraph, SecondJump, LandHeavy, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bLandingHeavy), 2) &&
		AddTransition(*MachineGraph, SecondJump, LandLight, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bLandingLight), 3) &&
		AddTransition(*MachineGraph, SecondJump, Grounded, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bGroundedWithoutLanding), 4) &&
		AddTransition(*MachineGraph, SecondJump, Airborne, NAME_None, 5, true) &&
		AddTransition(*MachineGraph, LandLight, JumpStart, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpStartRequested), 1) &&
		AddTransition(*MachineGraph, LandLight, SprintImpulse, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bSprintImpulseRequested), 2) &&
		AddTransition(*MachineGraph, LandLight, Airborne, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsFalling), 3) &&
		AddTransition(*MachineGraph, LandLight, Grounded, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bGroundedWithoutLanding), 4) &&
		AddTransition(*MachineGraph, LandHeavy, JumpStart, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpStartRequested), 1) &&
		AddTransition(*MachineGraph, LandHeavy, SprintImpulse, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bSprintImpulseRequested), 2) &&
		AddTransition(*MachineGraph, LandHeavy, Airborne, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsFalling), 3) &&
		AddTransition(*MachineGraph, LandHeavy, Grounded, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bGroundedWithoutLanding), 4) &&
		AddTransition(*MachineGraph, LandRoll, JumpStart, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpStartRequested), 1) &&
		AddTransition(*MachineGraph, LandRoll, SprintImpulse, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bSprintImpulseRequested), 2) &&
		AddTransition(*MachineGraph, LandRoll, Airborne, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsFalling), 3) &&
		AddTransition(*MachineGraph, LandRoll, Grounded, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bGroundedWithoutLanding), 4) &&
		AddTransition(*MachineGraph, TurnInPlace, JumpStart, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpStartRequested), 1) &&
		AddTransition(*MachineGraph, TurnInPlace, Airborne, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsFalling), 2) &&
		AddTransition(*MachineGraph, TurnInPlace, Grounded, NAME_None, 3, true) &&
		AddTransition(*MachineGraph, RunTurnback, JumpStart, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpStartRequested), 1) &&
		AddTransition(*MachineGraph, RunTurnback, Airborne, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsFalling), 2) &&
		AddTransition(
			*MachineGraph,
			RunTurnback,
			Grounded,
			GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bRunTurnbackShouldExit),
			3,
			false,
			ETransitionLogicType::TLT_StandardBlend,
			ResolveRunTurnbackBlendOutDuration(),
			EAlphaBlendOption::Cubic) &&
		AddTransition(*MachineGraph, MoveStop, JumpStart, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bJumpStartRequested), 1) &&
		AddTransition(*MachineGraph, MoveStop, Airborne, GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bIsFalling), 2) &&
		AddTransition(
			*MachineGraph,
			MoveStop,
			Grounded,
			GET_MEMBER_NAME_CHECKED(URoverAnimInstance, bMoveStopShouldExit),
			3,
			false,
			ETransitionLogicType::TLT_Inertialization);
	if (!bTransitionsBuilt)
	{
		return nullptr;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	AnimBlueprint->MarkPackageDirty();
	return AnimBlueprint;
}

bool URoverAnimationEditorLibrary::CreateRoverCombatP0Assets(
	USkeletalMesh* RoverMesh,
	UAnimSequence* Attack01Sequence,
	UAnimSequence* Attack02Sequence,
	UAnimSequence* Attack03Sequence,
	UAnimSequence* LightHitLeftSequence,
	UAnimSequence* LightHitRightSequence,
	USkeletalMesh* WeaponMesh,
	FString& ValidationReport)
{
	ValidationReport.Reset();
	if (!IsValid(RoverMesh) || !IsValid(RoverMesh->GetSkeleton()) ||
		!IsValid(Attack01Sequence) || !IsValid(Attack02Sequence) ||
		!IsValid(Attack03Sequence) || !IsValid(LightHitLeftSequence) ||
		!IsValid(LightHitRightSequence) || !IsValid(WeaponMesh) ||
		!IsValid(WeaponMesh->GetSkeleton()))
	{
		ValidationReport = TEXT("Rover mesh, Attack01-03, two light-hit sequences, and the skeletal weapon are required.");
		return false;
	}

	USkeleton* RoverSkeleton = RoverMesh->GetSkeleton();
	if (Attack01Sequence->GetSkeleton() != RoverSkeleton ||
		Attack02Sequence->GetSkeleton() != RoverSkeleton ||
		Attack03Sequence->GetSkeleton() != RoverSkeleton ||
		LightHitLeftSequence->GetSkeleton() != RoverSkeleton ||
		LightHitRightSequence->GetSkeleton() != RoverSkeleton)
	{
		ValidationReport = TEXT("All P0 combat sequences must use the Rover skeleton.");
		return false;
	}

	const FName CharacterWeaponBone = ResolveCharacterWeaponBone(*RoverSkeleton);
	if (CharacterWeaponBone.IsNone() ||
		!CreateOrUpdateSkeletonSocket(*RoverSkeleton, TEXT("RoverWeapon"), CharacterWeaponBone))
	{
		ValidationReport = TEXT("Could not resolve a right-hand weapon anchor on the Rover skeleton.");
		return false;
	}

	USkeleton* WeaponSkeleton = WeaponMesh->GetSkeleton();
	if (!CreateOrUpdateSkeletonSocket(*WeaponSkeleton, TEXT("WeaponTraceBase"), TEXT("Sword_Bone01")) ||
		!CreateOrUpdateSkeletonSocket(*WeaponSkeleton, TEXT("WeaponTraceTip"), TEXT("Sword_Bone03")))
	{
		ValidationReport = TEXT("The weapon skeleton is missing Sword_Bone01 or Sword_Bone03.");
		return false;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	URoverCombatConfig* CombatConfig = CreateOrLoadCombatConfig(AssetTools);
	if (!CombatConfig)
	{
		ValidationReport = TEXT("Failed to create or load the Rover combat config asset.");
		return false;
	}
	CombatConfig->Modify();
	CombatConfig->Settings.LightAttackChain.SetNum(FMath::Max(
		3,
		CombatConfig->Settings.LightAttackChain.Num()));
	for (FRoverAttackDefinition& AttackDefinition : CombatConfig->Settings.LightAttackChain)
	{
		AttackDefinition.AnimPlayRate = 1.3f; // [PLACEHOLDER] Tune after animation review.
		AttackDefinition.MontageBlendInTime = 0.05f;
		AttackDefinition.MontageBlendOutTime = 0.10f;
		// UE interprets this property as seconds remaining. 0.85 would stop these
		// long clips before Recovery/Finished instead of meaning normalized 85%.
		AttackDefinition.MontageBlendOutTriggerTime = AttackDefinition.MontageBlendOutTime;
		AttackDefinition.ComboWindowStartNormalized = 0.5f; // [PLACEHOLDER] Tune per attack after animation review.
	}
	CombatConfig->Settings.LightAttackChain[0].WeaponHand = ERoverWeaponHand::Left;
	CombatConfig->Settings.LightAttackChain[1].WeaponHand = ERoverWeaponHand::Right;
	CombatConfig->Settings.LightAttackChain[2].WeaponHand = ERoverWeaponHand::Right;
	auto CreateAttackMontage = [&AssetTools](
		UAnimSequence& Sequence,
		const TCHAR* AssetName,
		const float ActiveBegin,
		const float ActiveEnd,
		const FRoverAttackDefinition& AttackDefinition)
	{
		Sequence.Modify();
		Sequence.bEnableRootMotion = false;
		Sequence.bForceRootLock = true;
		Sequence.RootMotionRootLock = ERootMotionRootLock::RefPose;
		Sequence.PostEditChange();
		Sequence.MarkPackageDirty();
		const float Length = Sequence.GetPlayLength();
		const float StartedTime = FMath::Min(0.01f, Length * 0.02f);
		return CreateOrUpdateMontage(
			AssetTools,
			Sequence,
			AssetName,
			{
				{TEXT("RoverAttackStarted"), StartedTime},
				{TEXT("RoverAttackActiveBegin"), Length * ActiveBegin},
				{TEXT("RoverAttackActiveEnd"), Length * ActiveEnd},
				{TEXT("RoverRecoveryBegin"), Length * 0.75f},
				{TEXT("RoverAttackFinished"), FMath::Max(StartedTime, Length - 0.01f)}
			},
			&AttackDefinition);
	};
	UAnimMontage* Attack01Montage = CreateAttackMontage(
		*Attack01Sequence, TEXT("AM_Rover_Attack01"), 0.12f, 0.35f, CombatConfig->Settings.LightAttackChain[0]);
	UAnimMontage* Attack02Montage = CreateAttackMontage(
		*Attack02Sequence, TEXT("AM_Rover_Attack02"), 0.12f, 0.38f, CombatConfig->Settings.LightAttackChain[1]);
	UAnimMontage* Attack03Montage = CreateAttackMontage(
		*Attack03Sequence, TEXT("AM_Rover_Attack03"), 0.10f, 0.45f, CombatConfig->Settings.LightAttackChain[2]);

	auto CreateHitMontage = [&AssetTools](UAnimSequence& Sequence, const TCHAR* AssetName)
	{
		const float Length = Sequence.GetPlayLength();
		const float StartTime = FMath::Min(0.01f, Length * 0.02f);
		return CreateOrUpdateMontage(
			AssetTools,
			Sequence,
			AssetName,
			{
				{TEXT("RoverHitReactionStarted"), StartTime},
				{TEXT("RoverHitReactionFinished"), FMath::Max(StartTime, Length - 0.01f)}
			});
	};
	UAnimMontage* HitLeftMontage = CreateHitMontage(*LightHitLeftSequence, TEXT("AM_Rover_Behit_S_L"));
	UAnimMontage* HitRightMontage = CreateHitMontage(*LightHitRightSequence, TEXT("AM_Rover_Behit_S_R"));
	if (!Attack01Montage || !Attack02Montage || !Attack03Montage ||
		!HitLeftMontage || !HitRightMontage || !CombatConfig)
	{
		ValidationReport = TEXT("Failed to create a P0 combat Montage or combat config asset.");
		return false;
	}

	CombatConfig->Settings.LightAttackChain[0].Montage = Attack01Montage;
	CombatConfig->Settings.LightAttackChain[0].Damage = 25.0f;
	CombatConfig->Settings.LightAttackChain[0].PoiseDamage = 15.0f;
	CombatConfig->Settings.LightAttackChain[0].TraceRadius = 10.0f; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[0].TraceSampleCount = 7; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[0].TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[0].MaxTraceSubsteps = 8; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[0].AdvanceDistance = 70.0f;
	CombatConfig->Settings.LightAttackChain[0].AdvanceDuration = 0.28f;
	CombatConfig->Settings.LightAttackChain[1].Montage = Attack02Montage;
	CombatConfig->Settings.LightAttackChain[1].Damage = 30.0f;
	CombatConfig->Settings.LightAttackChain[1].PoiseDamage = 20.0f;
	CombatConfig->Settings.LightAttackChain[1].TraceRadius = 11.0f; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[1].TraceSampleCount = 7; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[1].TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[1].MaxTraceSubsteps = 8; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[1].AdvanceDistance = 85.0f;
	CombatConfig->Settings.LightAttackChain[1].AdvanceDuration = 0.30f;
	CombatConfig->Settings.LightAttackChain[2].Montage = Attack03Montage;
	CombatConfig->Settings.LightAttackChain[2].Damage = 45.0f;
	CombatConfig->Settings.LightAttackChain[2].PoiseDamage = 35.0f;
	CombatConfig->Settings.LightAttackChain[2].TraceRadius = 12.0f; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[2].TraceSampleCount = 7; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[2].TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[2].MaxTraceSubsteps = 8; // [PLACEHOLDER]
	CombatConfig->Settings.LightAttackChain[2].AdvanceDistance = 110.0f;
	CombatConfig->Settings.LightAttackChain[2].AdvanceDuration = 0.34f;
	CombatConfig->Settings.AttackInputBufferDuration = 0.25f;
	CombatConfig->Settings.ComboResetDuration = 0.55f;
	CombatConfig->Settings.LightHitLeftMontage = HitLeftMontage;
	CombatConfig->Settings.LightHitRightMontage = HitRightMontage;
	CombatConfig->Settings.WeaponMesh = WeaponMesh;
	CombatConfig->Settings.CharacterWeaponSocket = TEXT("RoverWeapon");
	CombatConfig->Settings.WeaponTraceBaseSocket = TEXT("WeaponTraceBase");
	CombatConfig->Settings.WeaponTraceTipSocket = TEXT("WeaponTraceTip");
	CombatConfig->Settings.ScabbardBone = TEXT("Scabbard_Bone001");
	CombatConfig->Settings.WeaponRelativeLocation = FVector::ZeroVector;
	CombatConfig->Settings.WeaponRelativeRotation = FRotator::ZeroRotator;
	CombatConfig->Settings.WeaponRelativeScale = FVector(0.09f);
	CombatConfig->Settings.LeftHandWeaponSocket = TEXT("Bip001LHand");
	CombatConfig->Settings.LeftHandWeaponRelativeLocation = FVector::ZeroVector;
	CombatConfig->Settings.LeftHandWeaponRelativeRotation = FRotator::ZeroRotator;
	CombatConfig->Settings.LeftHandWeaponRelativeScale = FVector(0.09f);
	CombatConfig->PostEditChange();
	CombatConfig->MarkPackageDirty();

	const TArray<FName> ExpectedAttackNotifies = {
		TEXT("RoverAttackStarted"),
		TEXT("RoverAttackActiveBegin"),
		TEXT("RoverAttackActiveEnd"),
		TEXT("ComboWindow"),
		TEXT("RoverRecoveryBegin"),
		TEXT("RoverAttackFinished")};
	const TArray<FName> ExpectedHitNotifies = {
		TEXT("RoverHitReactionStarted"),
		TEXT("RoverHitReactionFinished")};
	UE_LOG(
		LogTemp,
		Log,
		TEXT("Rover attack asset validation: A1 names=%d order=%d window=%d settings=%d A2 names=%d order=%d window=%d settings=%d A3 names=%d order=%d window=%d settings=%d"),
		HasNamedNotifies(*Attack01Montage, ExpectedAttackNotifies),
		HasValidAttackNotifyOrder(*Attack01Montage),
		HasValidComboWindowState(*Attack01Montage, CombatConfig->Settings.LightAttackChain[0]),
		HasValidAttackAssetSettings(*Attack01Sequence, *Attack01Montage, CombatConfig->Settings.LightAttackChain[0]),
		HasNamedNotifies(*Attack02Montage, ExpectedAttackNotifies),
		HasValidAttackNotifyOrder(*Attack02Montage),
		HasValidComboWindowState(*Attack02Montage, CombatConfig->Settings.LightAttackChain[1]),
		HasValidAttackAssetSettings(*Attack02Sequence, *Attack02Montage, CombatConfig->Settings.LightAttackChain[1]),
		HasNamedNotifies(*Attack03Montage, ExpectedAttackNotifies),
		HasValidAttackNotifyOrder(*Attack03Montage),
		HasValidComboWindowState(*Attack03Montage, CombatConfig->Settings.LightAttackChain[2]),
		HasValidAttackAssetSettings(*Attack03Sequence, *Attack03Montage, CombatConfig->Settings.LightAttackChain[2]));
	for (const UAnimMontage* AttackMontage : {Attack01Montage, Attack02Montage, Attack03Montage})
	{
		FString NotifyTimeline;
		for (const FAnimNotifyEvent& Notify : AttackMontage->Notifies)
		{
			NotifyTimeline += FString::Printf(
				TEXT(" %s=%.3f"),
				*Notify.NotifyName.ToString(),
				Notify.GetTriggerTime());
		}
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Rover attack montage timeline: asset=%s length=%.3f%s"),
			*AttackMontage->GetPathName(),
			AttackMontage->GetPlayLength(),
			*NotifyTimeline);
	}
	if (!HasNamedNotifies(*Attack01Montage, ExpectedAttackNotifies) ||
		!HasNamedNotifies(*Attack02Montage, ExpectedAttackNotifies) ||
		!HasNamedNotifies(*Attack03Montage, ExpectedAttackNotifies) ||
		!HasValidAttackNotifyOrder(*Attack01Montage) ||
		!HasValidAttackNotifyOrder(*Attack02Montage) ||
		!HasValidAttackNotifyOrder(*Attack03Montage) ||
		!HasValidComboWindowState(*Attack01Montage, CombatConfig->Settings.LightAttackChain[0]) ||
		!HasValidComboWindowState(*Attack02Montage, CombatConfig->Settings.LightAttackChain[1]) ||
		!HasValidComboWindowState(*Attack03Montage, CombatConfig->Settings.LightAttackChain[2]) ||
		!HasValidAttackAssetSettings(*Attack01Sequence, *Attack01Montage, CombatConfig->Settings.LightAttackChain[0]) ||
		!HasValidAttackAssetSettings(*Attack02Sequence, *Attack02Montage, CombatConfig->Settings.LightAttackChain[1]) ||
		!HasValidAttackAssetSettings(*Attack03Sequence, *Attack03Montage, CombatConfig->Settings.LightAttackChain[2]) ||
		!HasNamedNotifies(*HitLeftMontage, ExpectedHitNotifies) ||
		!HasNamedNotifies(*HitRightMontage, ExpectedHitNotifies) ||
		Attack01Montage->SlotAnimTracks.Num() != 1 ||
		Attack02Montage->SlotAnimTracks.Num() != 1 ||
		Attack03Montage->SlotAnimTracks.Num() != 1 ||
		Attack01Montage->SlotAnimTracks[0].SlotName != TEXT("DefaultSlot") ||
		Attack02Montage->SlotAnimTracks[0].SlotName != TEXT("DefaultSlot") ||
		Attack03Montage->SlotAnimTracks[0].SlotName != TEXT("DefaultSlot"))
	{
		ValidationReport = TEXT("Generated combat Montages failed slot or Notify validation.");
		return false;
	}

	ValidationReport = FString::Printf(
		TEXT("rover_socket=RoverWeapon:%s weapon_sockets=WeaponTraceBase:Sword_Bone01,WeaponTraceTip:Sword_Bone03 attacks=%s,%s,%s hit_left=%s hit_right=%s config=%s"),
		*CharacterWeaponBone.ToString(),
		*Attack01Montage->GetPathName(),
		*Attack02Montage->GetPathName(),
		*Attack03Montage->GetPathName(),
		*HitLeftMontage->GetPathName(),
		*HitRightMontage->GetPathName(),
		*CombatConfig->GetPathName());
	return true;
}

bool URoverAnimationEditorLibrary::CreateRoverLightAttackAsset(
	UAnimSequence* AttackSequence,
	const int32 ComboIndex,
	FString& ValidationReport)
{
	ValidationReport.Reset();
	if (!IsValid(AttackSequence) || !IsValid(AttackSequence->GetSkeleton()) || ComboIndex < 1)
	{
		ValidationReport = TEXT("A valid Rover attack sequence and a positive combo index are required.");
		return false;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	URoverCombatConfig* CombatConfig = CreateOrLoadCombatConfig(AssetTools);
	if (!CombatConfig)
	{
		ValidationReport = TEXT("Failed to load the Rover combat config asset.");
		return false;
	}

	CombatConfig->Modify();
	const bool bNewDefinition = CombatConfig->Settings.LightAttackChain.Num() < ComboIndex;
	CombatConfig->Settings.LightAttackChain.SetNum(FMath::Max(
		ComboIndex,
		CombatConfig->Settings.LightAttackChain.Num()));
	FRoverAttackDefinition& Definition = CombatConfig->Settings.LightAttackChain[ComboIndex - 1];
	if (bNewDefinition)
	{
		Definition.WeaponHand = ERoverWeaponHand::Right;
		Definition.AnimPlayRate = 1.3f; // [PLACEHOLDER]
		Definition.MontageBlendInTime = 0.05f;
		Definition.MontageBlendOutTime = ComboIndex >= 4 ? 0.25f : 0.10f; // [PLACEHOLDER]
		Definition.MontageBlendOutTriggerTime = Definition.MontageBlendOutTime;
		Definition.ComboWindowStartNormalized = 0.5f;
		Definition.Damage = 40.0f; // [PLACEHOLDER]
		Definition.PoiseDamage = 30.0f; // [PLACEHOLDER]
		Definition.EnvironmentImpulseStrength = 900.0f; // [PLACEHOLDER]
		Definition.TraceRadius = 12.0f; // [PLACEHOLDER]
		Definition.TraceSampleCount = 7; // [PLACEHOLDER]
		Definition.TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
		Definition.MaxTraceSubsteps = 8; // [PLACEHOLDER]
		Definition.AdvanceDistance = 60.0f; // [PLACEHOLDER]
		Definition.AdvanceDuration = 0.28f; // [PLACEHOLDER]
	}

	AttackSequence->Modify();
	AttackSequence->bEnableRootMotion = false;
	AttackSequence->bForceRootLock = true;
	AttackSequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
	AttackSequence->PostEditChange();
	AttackSequence->MarkPackageDirty();
	const float Length = AttackSequence->GetPlayLength();
	const float StartedTime = FMath::Min(0.01f, Length * 0.02f);
	const FString MontageName = FString::Printf(TEXT("AM_Rover_Attack%02d"), ComboIndex);
	UAnimMontage* Montage = CreateOrUpdateMontage(
		AssetTools,
		*AttackSequence,
		*MontageName,
		{
			{TEXT("RoverAttackStarted"), StartedTime},
			{TEXT("RoverAttackActiveBegin"), Length * 0.12f},
			{TEXT("RoverAttackActiveEnd"), Length * 0.42f},
			{TEXT("RoverRecoveryBegin"), Length * 0.75f},
			{TEXT("RoverAttackFinished"), FMath::Max(StartedTime, Length - 0.01f)}
		},
		&Definition);
	if (!Montage)
	{
		ValidationReport = TEXT("Failed to create the Rover light-attack Montage.");
		return false;
	}

	Definition.Montage = Montage;
	CombatConfig->PostEditChange();
	CombatConfig->MarkPackageDirty();
	const TArray<FName> ExpectedNotifies = {
		TEXT("RoverAttackStarted"),
		TEXT("RoverAttackActiveBegin"),
		TEXT("RoverAttackActiveEnd"),
		TEXT("ComboWindow"),
		TEXT("RoverRecoveryBegin"),
		TEXT("RoverAttackFinished")};
	const bool bValid = HasNamedNotifies(*Montage, ExpectedNotifies) &&
		HasValidAttackNotifyOrder(*Montage) &&
		HasValidComboWindowState(*Montage, Definition) &&
		HasValidAttackAssetSettings(*AttackSequence, *Montage, Definition) &&
		Montage->SlotAnimTracks.Num() == 1 &&
		Montage->SlotAnimTracks[0].SlotName == TEXT("DefaultSlot");
	ValidationReport = FString::Printf(
		TEXT("combo=%d sequence=%s montage=%s length=%.3f notifies=%d order=%d window=%d settings=%d"),
		ComboIndex,
		*AttackSequence->GetPathName(),
		*Montage->GetPathName(),
		Length,
		HasNamedNotifies(*Montage, ExpectedNotifies),
		HasValidAttackNotifyOrder(*Montage),
		HasValidComboWindowState(*Montage, Definition),
		HasValidAttackAssetSettings(*AttackSequence, *Montage, Definition));
	return bValid;
}

bool URoverAnimationEditorLibrary::CreateRoverHeavyAttackAsset(
	UAnimSequence* AttackSequence,
	FString& ValidationReport)
{
	ValidationReport.Reset();
	if (!IsValid(AttackSequence) || !IsValid(AttackSequence->GetSkeleton()))
	{
		ValidationReport = TEXT("A valid Rover heavy-attack sequence is required.");
		return false;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	URoverCombatConfig* CombatConfig = CreateOrLoadCombatConfig(AssetTools);
	if (!CombatConfig)
	{
		ValidationReport = TEXT("Failed to load the Rover combat config asset.");
		return false;
	}

	CombatConfig->Modify();
	FRoverAttackDefinition& Definition = CombatConfig->Settings.HeavyAttackDefinition;
	AttackSequence->Modify();
	AttackSequence->bEnableRootMotion = false;
	AttackSequence->bForceRootLock = true;
	AttackSequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
	AttackSequence->PostEditChange();
	AttackSequence->MarkPackageDirty();

	const float Length = AttackSequence->GetPlayLength();
	const float StartedTime = FMath::Min(0.01f, Length * 0.02f);
	UAnimMontage* Montage = CreateOrUpdateMontage(
		AssetTools,
		*AttackSequence,
		TEXT("AM_Rover_Attack05"),
		{
			{TEXT("RoverAttackStarted"), StartedTime},
			{TEXT("RoverAttackActiveBegin"), Length * 0.12f},
			{TEXT("RoverAttackActiveEnd"), Length * 0.42f},
			{TEXT("RoverRecoveryBegin"), Length * 0.75f},
			{TEXT("RoverAttackFinished"), FMath::Max(StartedTime, Length - 0.01f)}
		},
		&Definition);
	if (!Montage)
	{
		ValidationReport = TEXT("Failed to create the Rover heavy-attack Montage.");
		return false;
	}

	Definition.Montage = Montage;
	CombatConfig->PostEditChange();
	CombatConfig->MarkPackageDirty();
	const TArray<FName> ExpectedNotifies = {
		TEXT("RoverAttackStarted"),
		TEXT("RoverAttackActiveBegin"),
		TEXT("RoverAttackActiveEnd"),
		TEXT("ComboWindow"),
		TEXT("RoverRecoveryBegin"),
		TEXT("RoverAttackFinished")};
	const bool bValid = HasNamedNotifies(*Montage, ExpectedNotifies) &&
		HasValidAttackNotifyOrder(*Montage) &&
		HasValidComboWindowState(*Montage, Definition) &&
		HasValidAttackAssetSettings(*AttackSequence, *Montage, Definition) &&
		Montage->SlotAnimTracks.Num() == 1 &&
		Montage->SlotAnimTracks[0].SlotName == TEXT("DefaultSlot");
	ValidationReport = FString::Printf(
		TEXT("type=heavy sequence=%s montage=%s length=%.3f notifies=%d order=%d window=%d settings=%d"),
		*AttackSequence->GetPathName(),
		*Montage->GetPathName(),
		Length,
		HasNamedNotifies(*Montage, ExpectedNotifies),
		HasValidAttackNotifyOrder(*Montage),
		HasValidComboWindowState(*Montage, Definition),
		HasValidAttackAssetSettings(*AttackSequence, *Montage, Definition));
	return bValid;
}

bool URoverAnimationEditorLibrary::CreateRoverHeavyResonanceAsset(
	UAnimSequence* AttackSequence,
	FString& ValidationReport)
{
	ValidationReport.Reset();
	if (!IsValid(AttackSequence) || !IsValid(AttackSequence->GetSkeleton()))
	{
		ValidationReport = TEXT("A valid Rover heavy-resonance sequence is required.");
		return false;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	URoverCombatConfig* CombatConfig = CreateOrLoadCombatConfig(AssetTools);
	if (!CombatConfig || !CombatConfig->Settings.LightAttackChain.IsValidIndex(2))
	{
		ValidationReport = TEXT("The Rover combat config or Attack03 definition is missing.");
		return false;
	}

	CombatConfig->Modify();
	FRoverAttackDefinition& Definition = CombatConfig->Settings.HeavyResonanceDefinition;
	AttackSequence->Modify();
	AttackSequence->bEnableRootMotion = false;
	AttackSequence->bForceRootLock = true;
	AttackSequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
	AttackSequence->PostEditChange();
	AttackSequence->MarkPackageDirty();

	const float Length = AttackSequence->GetPlayLength();
	const float StartedTime = FMath::Min(0.01f, Length * 0.02f);
	UAnimMontage* Montage = CreateOrUpdateMontage(
		AssetTools,
		*AttackSequence,
		TEXT("AM_Rover_Attack_EX01"),
		{
			{TEXT("RoverAttackStarted"), StartedTime},
			{TEXT("RoverAttackActiveBegin"), Length * 0.12f},
			{TEXT("RoverAttackActiveEnd"), Length * 0.42f},
			{TEXT("RoverRecoveryBegin"), Length * 0.75f},
			{TEXT("RoverAttackFinished"), FMath::Max(StartedTime, Length - 0.01f)}
		},
		&Definition);
	if (!Montage)
	{
		ValidationReport = TEXT("Failed to create the Rover heavy-resonance Montage.");
		return false;
	}
	Definition.Montage = Montage;

	UAnimMontage* Attack03Montage = CombatConfig->Settings.LightAttackChain[2].Montage.LoadSynchronous();
	if (!Attack03Montage)
	{
		ValidationReport = TEXT("Attack03 Montage is missing; cannot install the ResonanceWindow.");
		return false;
	}
	Attack03Montage->Modify();
	Attack03Montage->Notifies.RemoveAll([](const FAnimNotifyEvent& Notify)
	{
		return Notify.NotifyName == TEXT("ResonanceWindow") ||
			(Notify.NotifyStateClass && Notify.NotifyStateClass->IsA<URoverAnimNotifyState_ResonanceWindow>());
	});
	const float ComboStart = FMath::Clamp(
		CombatConfig->Settings.LightAttackChain[2].ComboWindowStartNormalized,
		0.0f,
		1.0f);
	const float ResonanceStart = FMath::Lerp(
		ComboStart,
		1.0f,
		FMath::Clamp(CombatConfig->Settings.ResonanceHalfWindowNormalized, 0.0f, 1.0f));
	AddResonanceWindowNotifyState(*Attack03Montage, ResonanceStart);
	Attack03Montage->SortNotifies();
	Attack03Montage->PostEditChange();
	Attack03Montage->MarkPackageDirty();

	CombatConfig->PostEditChange();
	CombatConfig->MarkPackageDirty();
	const TArray<FName> ExpectedNotifies = {
		TEXT("RoverAttackStarted"),
		TEXT("RoverAttackActiveBegin"),
		TEXT("RoverAttackActiveEnd"),
		TEXT("ComboWindow"),
		TEXT("RoverRecoveryBegin"),
		TEXT("RoverAttackFinished")};
	const bool bValid = HasNamedNotifies(*Montage, ExpectedNotifies) &&
		HasValidAttackNotifyOrder(*Montage) &&
		HasValidComboWindowState(*Montage, Definition) &&
		HasValidAttackAssetSettings(*AttackSequence, *Montage, Definition) &&
		HasValidResonanceWindowState(*Attack03Montage, ResonanceStart) &&
		Montage->SlotAnimTracks.Num() == 1 &&
		Montage->SlotAnimTracks[0].SlotName == TEXT("DefaultSlot");
	ValidationReport = FString::Printf(
		TEXT("type=resonance sequence=%s montage=%s length=%.3f attack03_window=%.3f definition=%d window=%d"),
		*AttackSequence->GetPathName(),
		*Montage->GetPathName(),
		Length,
		ResonanceStart,
		HasValidAttackAssetSettings(*AttackSequence, *Montage, Definition),
		HasValidResonanceWindowState(*Attack03Montage, ResonanceStart));
	return bValid;
}

bool URoverAnimationEditorLibrary::CreateRoverAirAttackAsset(
	UAnimSequence* StartSequence,
	UAnimSequence* LoopSequence,
	UAnimSequence* EndSequence,
	FString& ValidationReport)
{
	ValidationReport.Reset();
	if (!IsValid(StartSequence) || !IsValid(LoopSequence) || !IsValid(EndSequence) ||
		!IsValid(StartSequence->GetSkeleton()) ||
		StartSequence->GetSkeleton() != LoopSequence->GetSkeleton() ||
		StartSequence->GetSkeleton() != EndSequence->GetSkeleton())
	{
		ValidationReport = TEXT("AirAttack Start, Loop, and End sequences must use the same valid Rover skeleton.");
		return false;
	}
	if (StartSequence->GetPlayLength() <= UE_SMALL_NUMBER ||
		LoopSequence->GetPlayLength() <= UE_SMALL_NUMBER ||
		EndSequence->GetPlayLength() <= UE_SMALL_NUMBER)
	{
		ValidationReport = TEXT("AirAttack sequences must all contain animation data.");
		return false;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	URoverCombatConfig* CombatConfig = CreateOrLoadCombatConfig(AssetTools);
	if (!CombatConfig)
	{
		ValidationReport = TEXT("Failed to load the Rover combat config asset.");
		return false;
	}

	CombatConfig->Modify();
	FRoverCombatSettings& Settings = CombatConfig->Settings;
	FRoverAttackDefinition& Definition = Settings.AirAttackDefinition;
	for (UAnimSequence* Sequence : {StartSequence, LoopSequence, EndSequence})
	{
		Sequence->Modify();
		Sequence->bEnableRootMotion = false;
		Sequence->bForceRootLock = true;
		Sequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
		Sequence->PostEditChange();
		Sequence->MarkPackageDirty();
	}

	constexpr TCHAR MontageName[] = TEXT("AM_Rover_AirAttack");
	const FString MontagePath = FString::Printf(TEXT("%s/%s"), CombatMontageRoot, MontageName);
	const FString MontageObjectPath = FString::Printf(TEXT("%s.%s"), *MontagePath, MontageName);
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontageObjectPath);
	if (!Montage)
	{
		UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
		Factory->TargetSkeleton = StartSequence->GetSkeleton();
		Factory->SourceAnimation = StartSequence;
		Montage = Cast<UAnimMontage>(AssetTools.CreateAsset(
			MontageName,
			CombatMontageRoot,
			UAnimMontage::StaticClass(),
			Factory));
	}
	if (!Montage)
	{
		ValidationReport = TEXT("Failed to create the Rover AirAttack Montage.");
		return false;
	}

	const float StartLength = StartSequence->GetPlayLength();
	const float LoopLength = LoopSequence->GetPlayLength();
	const float EndLength = EndSequence->GetPlayLength();
	const float EndStartTime = StartLength + LoopLength;
	const float TotalLength = EndStartTime + EndLength;
	Montage->Modify();
	Montage->SetSkeleton(StartSequence->GetSkeleton());
	Montage->SlotAnimTracks.Reset();
	FSlotAnimationTrack SlotTrack;
	SlotTrack.SlotName = TEXT("DefaultSlot");
	auto AddSegment = [&SlotTrack](UAnimSequence& Sequence, const float StartPosition)
	{
		FAnimSegment Segment;
		Segment.SetAnimReference(&Sequence, true);
		Segment.StartPos = StartPosition;
		SlotTrack.AnimTrack.AnimSegments.Add(Segment);
	};
	AddSegment(*StartSequence, 0.0f);
	AddSegment(*LoopSequence, StartLength);
	AddSegment(*EndSequence, EndStartTime);
	Montage->SlotAnimTracks.Add(MoveTemp(SlotTrack));
	Montage->SetCompositeLength(TotalLength);
	Montage->CompositeSections.Reset();
	const int32 StartSectionIndex = Montage->AddAnimCompositeSection(TEXT("Start"), 0.0f);
	const int32 LoopSectionIndex = Montage->AddAnimCompositeSection(TEXT("Loop"), StartLength);
	const int32 EndSectionIndex = Montage->AddAnimCompositeSection(TEXT("End"), EndStartTime);
	if (!Montage->CompositeSections.IsValidIndex(StartSectionIndex) ||
		!Montage->CompositeSections.IsValidIndex(LoopSectionIndex) ||
		!Montage->CompositeSections.IsValidIndex(EndSectionIndex))
	{
		ValidationReport = TEXT("Failed to create AirAttack Montage sections.");
		return false;
	}
	Montage->CompositeSections[StartSectionIndex].NextSectionName = TEXT("Loop");
	Montage->CompositeSections[LoopSectionIndex].NextSectionName = TEXT("Loop");
	Montage->CompositeSections[EndSectionIndex].NextSectionName = NAME_None;
	Montage->BlendModeIn = EMontageBlendMode::Standard;
	Montage->BlendModeOut = EMontageBlendMode::Standard;
	Montage->BlendIn.SetBlendTime(FMath::Max(0.0f, Definition.MontageBlendInTime));
	Montage->BlendOut.SetBlendTime(FMath::Max(0.0f, Definition.MontageBlendOutTime));
	Montage->BlendOutTriggerTime = FMath::Max(0.0f, Definition.MontageBlendOutTriggerTime);
	Montage->bEnableAutoBlendOut = true;
	Montage->Notifies.Reset();
	USkeleton* Skeleton = StartSequence->GetSkeleton();
	const float StartedTime = FMath::Min(0.01f, StartLength * 0.02f);
	const IAnimationDataModel* StartDataModel = StartSequence->GetDataModel();
	const FFrameRate StartFrameRate = StartDataModel
		? StartDataModel->GetFrameRate()
		: StartSequence->GetSamplingFrameRate();
	const int32 MaximumApexFrame = StartDataModel
		? FMath::Max(1, StartDataModel->GetNumberOfFrames())
		: FMath::Max(1, StartSequence->GetNumberOfSampledKeys() - 1);
	const int32 ApexFrame = FMath::Clamp(Settings.AirAttackApexFrame, 1, MaximumApexFrame);
	const float ApexTime = FMath::Clamp(
		static_cast<float>(StartFrameRate.AsSeconds(FFrameTime(ApexFrame))),
		StartedTime + UE_KINDA_SMALL_NUMBER,
		StartLength - UE_KINDA_SMALL_NUMBER);
	AddNamedMontageNotify(*Montage, *Skeleton, TEXT("RoverAttackStarted"), StartedTime);
	AddNamedMontageNotify(*Montage, *Skeleton, TEXT("RoverAirAttackApex"), ApexTime);
	AddNamedMontageNotify(*Montage, *Skeleton, TEXT("RoverAttackActiveBegin"), StartLength * 0.35f);
	AddNamedMontageNotify(*Montage, *Skeleton, TEXT("RoverAttackActiveEnd"), EndStartTime + EndLength * 0.55f);
	AddNamedMontageNotify(*Montage, *Skeleton, TEXT("RoverRecoveryBegin"), EndStartTime + EndLength * 0.72f);
	AddNamedMontageNotify(*Montage, *Skeleton, TEXT("RoverAttackFinished"), FMath::Max(StartedTime, TotalLength - 0.01f));
	Montage->SortNotifies();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	Skeleton->MarkPackageDirty();

	Definition.Montage = Montage;
	CombatConfig->PostEditChange();
	CombatConfig->MarkPackageDirty();
	const TArray<FName> ExpectedNotifies = {
		TEXT("RoverAttackStarted"),
		TEXT("RoverAirAttackApex"),
		TEXT("RoverAttackActiveBegin"),
		TEXT("RoverAttackActiveEnd"),
		TEXT("RoverRecoveryBegin"),
		TEXT("RoverAttackFinished")};
	const FAnimNotifyEvent* Started = FindNamedNotify(*Montage, TEXT("RoverAttackStarted"));
	const FAnimNotifyEvent* Apex = FindNamedNotify(*Montage, TEXT("RoverAirAttackApex"));
	const FAnimNotifyEvent* ActiveBegin = FindNamedNotify(*Montage, TEXT("RoverAttackActiveBegin"));
	const FAnimNotifyEvent* ActiveEnd = FindNamedNotify(*Montage, TEXT("RoverAttackActiveEnd"));
	const FAnimNotifyEvent* Recovery = FindNamedNotify(*Montage, TEXT("RoverRecoveryBegin"));
	const FAnimNotifyEvent* Finished = FindNamedNotify(*Montage, TEXT("RoverAttackFinished"));
	const bool bValidNotifyOrder = Started && Apex && ActiveBegin && ActiveEnd && Recovery && Finished &&
		Started->GetTriggerTime() < Apex->GetTriggerTime() &&
		Apex->GetTriggerTime() < StartLength &&
		Started->GetTriggerTime() < ActiveBegin->GetTriggerTime() &&
		ActiveBegin->GetTriggerTime() < ActiveEnd->GetTriggerTime() &&
		ActiveEnd->GetTriggerTime() < Recovery->GetTriggerTime() &&
		Recovery->GetTriggerTime() < Finished->GetTriggerTime();
	const bool bValidSections = Montage->CompositeSections.Num() == 3 &&
		Montage->CompositeSections[StartSectionIndex].NextSectionName == TEXT("Loop") &&
		Montage->CompositeSections[LoopSectionIndex].NextSectionName == TEXT("Loop") &&
		Montage->CompositeSections[EndSectionIndex].NextSectionName.IsNone();
	const bool bValid = HasNamedNotifies(*Montage, ExpectedNotifies) && bValidNotifyOrder &&
		bValidSections && HasValidAttackAssetSettings(*StartSequence, *Montage, Definition) &&
		Montage->SlotAnimTracks.Num() == 1 &&
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 3 &&
		Montage->SlotAnimTracks[0].SlotName == TEXT("DefaultSlot");
	ValidationReport = FString::Printf(
		TEXT("type=air sections=%d loop=%d notifies=%d order=%d apex_frame=%d apex_time=%.3f fps=%.3f montage=%s length=%.3f"),
		Montage->CompositeSections.Num(),
		bValidSections,
		HasNamedNotifies(*Montage, ExpectedNotifies),
		bValidNotifyOrder,
		ApexFrame,
		ApexTime,
		StartFrameRate.AsDecimal(),
		*Montage->GetPathName(),
		TotalLength);
	return bValid;
}

bool URoverAnimationEditorLibrary::ValidateRoverAttackMontage(
	const UAnimSequence* AttackSequence,
	const UAnimMontage* AttackMontage,
	FString& ValidationReport)
{
	if (!AttackSequence || !AttackMontage)
	{
		ValidationReport = TEXT("Attack Sequence and Montage are required.");
		return false;
	}

	const URoverCombatConfig* CombatConfig = LoadObject<URoverCombatConfig>(
		nullptr,
		TEXT("/Game/Rover/Combat/DA_RoverCombatConfig.DA_RoverCombatConfig"));
	if (!CombatConfig)
	{
		ValidationReport = TEXT("Rover combat config is missing.");
		return false;
	}

	const FRoverAttackDefinition* MatchingDefinition = nullptr;
	for (const FRoverAttackDefinition& Definition : CombatConfig->Settings.LightAttackChain)
	{
		if (Definition.Montage.LoadSynchronous() == AttackMontage)
		{
			MatchingDefinition = &Definition;
			break;
		}
	}
	if (!MatchingDefinition &&
		CombatConfig->Settings.HeavyAttackDefinition.Montage.LoadSynchronous() == AttackMontage)
	{
		MatchingDefinition = &CombatConfig->Settings.HeavyAttackDefinition;
	}
	if (!MatchingDefinition &&
		CombatConfig->Settings.HeavyResonanceDefinition.Montage.LoadSynchronous() == AttackMontage)
	{
		MatchingDefinition = &CombatConfig->Settings.HeavyResonanceDefinition;
	}
	if (!MatchingDefinition)
	{
		ValidationReport = FString::Printf(
			TEXT("Montage is not configured in the Rover light attack chain: %s"),
			*AttackMontage->GetPathName());
		return false;
	}

	const TArray<FName> ExpectedNotifies = {
		TEXT("RoverAttackStarted"),
		TEXT("RoverAttackActiveBegin"),
		TEXT("RoverAttackActiveEnd"),
		TEXT("ComboWindow"),
		TEXT("RoverRecoveryBegin"),
		TEXT("RoverAttackFinished")};
	const bool bValid = HasNamedNotifies(*AttackMontage, ExpectedNotifies) &&
		HasValidAttackNotifyOrder(*AttackMontage) &&
		HasValidComboWindowState(*AttackMontage, *MatchingDefinition) &&
		HasValidAttackAssetSettings(*AttackSequence, *AttackMontage, *MatchingDefinition);
	const float MontageLength = AttackMontage->GetPlayLength();
	const float NormalizedBlendOutTrigger = MontageLength > UE_SMALL_NUMBER
		? 1.0f - (AttackMontage->BlendOutTriggerTime / MontageLength)
		: 0.0f;
	ValidationReport = FString::Printf(
		TEXT("asset=%s notifies=%d order=%d combo_window=%d root_motion=%s force_root_lock=%s root_lock=%d blend_in=%.3f blend_out=%.3f normalized_trigger=%.3f mode_in=%d mode_out=%d"),
		*AttackMontage->GetPathName(),
		HasNamedNotifies(*AttackMontage, ExpectedNotifies),
		HasValidAttackNotifyOrder(*AttackMontage),
		HasValidComboWindowState(*AttackMontage, *MatchingDefinition),
		AttackSequence->bEnableRootMotion ? TEXT("true") : TEXT("false"),
		AttackSequence->bForceRootLock ? TEXT("true") : TEXT("false"),
		static_cast<int32>(AttackSequence->RootMotionRootLock.GetValue()),
		AttackMontage->GetDefaultBlendInTime(),
		AttackMontage->GetDefaultBlendOutTime(),
		NormalizedBlendOutTrigger,
		static_cast<int32>(AttackMontage->BlendModeIn),
		static_cast<int32>(AttackMontage->BlendModeOut));
	return bValid;
}

bool URoverAnimationEditorLibrary::ValidateRoverAnimBlueprint(
	const UAnimBlueprint* AnimBlueprint,
	const USkeletalMesh* PreviewMesh,
	FString& ValidationReport)
{
	ValidationReport.Reset();
	bool bValidationSucceeded = false;
	ON_SCOPE_EXIT
	{
		if (!bValidationSucceeded)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Rover AnimBlueprint validation failed: %s"),
				ValidationReport.IsEmpty() ? TEXT("No validation report was produced.") : *ValidationReport);
		}
	};
	if (!IsValid(AnimBlueprint) || !IsValid(PreviewMesh) || !IsValid(PreviewMesh->GetSkeleton()))
	{
		ValidationReport = TEXT("AnimBlueprint and preview mesh must both be valid.");
		return false;
	}
	if (AnimBlueprint->ParentClass != URoverAnimInstance::StaticClass())
	{
		ValidationReport = TEXT("AnimBlueprint parent class is not URoverAnimInstance.");
		return false;
	}
	if (AnimBlueprint->TargetSkeleton != PreviewMesh->GetSkeleton())
	{
		ValidationReport = TEXT("AnimBlueprint target skeleton does not match the Rover mesh.");
		return false;
	}
	if (AnimBlueprint->Status != BS_UpToDate)
	{
		ValidationReport = FString::Printf(TEXT("AnimBlueprint compile status is not clean: %d"), static_cast<int32>(AnimBlueprint->Status.GetValue()));
		return false;
	}
	if (!AnimBlueprint->GeneratedClass || !AnimBlueprint->GeneratedClass->IsChildOf(URoverAnimInstance::StaticClass()))
	{
		ValidationReport = TEXT("AnimBlueprint generated class is missing or has the wrong parent.");
		return false;
	}

	const TSet<FString> ExpectedLoopingSequences = {
		TEXT("Stand1"),
		TEXT("Stand2"),
		TEXT("Sprint_F"),
		TEXT("Jump_Loop"),
		TEXT("Fall_Loop"),
		TEXT("Fall_Loop_Fast")};

	TSet<FString> StateNames;
	TSet<FString> MachineNames;
	TMap<FString, TSet<FString>> StateSequences;
	TMap<FString, TSet<FString>> StateBlendSpaces;
	TMap<FString, int32> StateTwoWayBlendCounts;
	TMap<FString, int32> StateBoolBlendCounts;
	TMap<FString, FName> StateEnteredNotifies;
	TMap<FString, FName> StateLeftNotifies;
	UAnimGraphNode_StateMachine* FoundMachine = nullptr;
	UAnimGraphNode_Inertialization* FoundInertialization = nullptr;
	UAnimGraphNode_Slot* FoundCombatSlot = nullptr;
	UAnimGraphNode_Root* FoundRoot = nullptr;
	int32 InertializationNodeCount = 0;
	int32 CombatSlotNodeCount = 0;
	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UAnimStateNode* State = Cast<UAnimStateNode>(Node))
			{
				const FString StateName = State->GetStateName();
				StateNames.Add(StateName);
				TSet<FString> SequenceNames;
				TSet<FString> BlendSpaceNames;
				int32 TwoWayBlendCount = 0;
				int32 BoolBlendCount = 0;
				if (!State->BoundGraph)
				{
					ValidationReport = FString::Printf(TEXT("State %s has no bound animation graph."), *StateName);
					return false;
				}
				for (UEdGraphNode* StateGraphNode : State->BoundGraph->Nodes)
				{
					if (const UAnimGraphNode_SequencePlayer* SequencePlayer = Cast<UAnimGraphNode_SequencePlayer>(StateGraphNode))
					{
						const UAnimSequenceBase* Sequence = SequencePlayer->Node.GetSequence();
						if (!Sequence)
						{
							ValidationReport = FString::Printf(TEXT("State %s contains an empty sequence player."), *StateName);
							return false;
						}
						const FString SequenceName = Sequence->GetName();
						SequenceNames.Add(SequenceName);
						const bool bExpectedLooping = ExpectedLoopingSequences.Contains(SequenceName);
						if (SequencePlayer->Node.IsLooping() != bExpectedLooping)
						{
							ValidationReport = FString::Printf(
								TEXT("Sequence %s in state %s has the wrong looping flag."),
								*SequenceName,
								*StateName);
							return false;
						}
					}
					else if (const UAnimGraphNode_BlendSpacePlayer* BlendSpacePlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(StateGraphNode))
					{
						const UBlendSpace* BlendSpace = BlendSpacePlayer->Node.GetBlendSpace();
						if (!BlendSpace || !BlendSpacePlayer->Node.IsLooping())
						{
							ValidationReport = FString::Printf(TEXT("State %s has an invalid or non-looping BlendSpace player."), *StateName);
							return false;
						}
						BlendSpaceNames.Add(BlendSpace->GetName());
					}
					else if (Cast<UAnimGraphNode_TwoWayBlend>(StateGraphNode))
					{
						++TwoWayBlendCount;
					}
					else if (Cast<UAnimGraphNode_BlendListByBool>(StateGraphNode))
					{
						++BoolBlendCount;
					}
				}
				StateSequences.Add(StateName, MoveTemp(SequenceNames));
				StateBlendSpaces.Add(StateName, MoveTemp(BlendSpaceNames));
				StateTwoWayBlendCounts.Add(StateName, TwoWayBlendCount);
				StateBoolBlendCounts.Add(StateName, BoolBlendCount);
				StateEnteredNotifies.Add(StateName, State->StateEntered.NotifyName);
				StateLeftNotifies.Add(StateName, State->StateLeft.NotifyName);
			}
			else if (UAnimGraphNode_StateMachine* Machine = Cast<UAnimGraphNode_StateMachine>(Node))
			{
				MachineNames.Add(Machine->GetStateMachineName());
				FoundMachine = Machine;
			}
			else if (UAnimGraphNode_Inertialization* Inertialization = Cast<UAnimGraphNode_Inertialization>(Node))
			{
				FoundInertialization = Inertialization;
				++InertializationNodeCount;
			}
			else if (UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(Node))
			{
				FoundCombatSlot = Slot;
				++CombatSlotNodeCount;
			}
			else if (UAnimGraphNode_Root* Root = Cast<UAnimGraphNode_Root>(Node))
			{
				FoundRoot = Root;
			}
		}
	}
	UE_LOG(
		LogTemp,
		Log,
		TEXT("Rover AnimBlueprint graph scan: status=%d states=%d machines=%d slots=%d inertialization=%d root=%s"),
		static_cast<int32>(AnimBlueprint->Status.GetValue()),
		StateNames.Num(),
		MachineNames.Num(),
		CombatSlotNodeCount,
		InertializationNodeCount,
		FoundRoot ? TEXT("true") : TEXT("false"));

	const TSet<FString> ExpectedStates = {
		TEXT("Grounded"),
		TEXT("SprintImpulse"),
		TEXT("JumpStart"),
		TEXT("Airborne"),
		TEXT("SecondJump"),
		TEXT("LandLight"),
		TEXT("LandHeavy"),
		TEXT("LandRoll"),
		TEXT("TurnInPlace"),
		TEXT("RunTurnback"),
		TEXT("MoveStop")};
	if (StateNames.Num() != ExpectedStates.Num() || !StateNames.Includes(ExpectedStates))
	{
		ValidationReport = FString::Printf(TEXT("Unexpected state set; found %d of %d required states."), StateNames.Num(), ExpectedStates.Num());
		return false;
	}
	if (MachineNames.Num() != 1 || !MachineNames.Contains(TEXT("RoverLocomotion")) || !FoundMachine)
	{
		ValidationReport = TEXT("Expected exactly one RoverLocomotion state machine.");
		return false;
	}
	if (InertializationNodeCount != 1 || CombatSlotNodeCount != 1 ||
		!FoundInertialization || !FoundCombatSlot || !FoundRoot)
	{
		ValidationReport = TEXT("AnimGraph must contain one combat Slot and one Inertialization node.");
		return false;
	}

	const UEdGraphPin* MachinePosePin = FoundMachine->FindPin(TEXT("Pose"));
	const UEdGraphPin* CombatSlotSourcePin = FoundCombatSlot->FindPin(TEXT("Source"));
	const UEdGraphPin* CombatSlotPosePin = FoundCombatSlot->FindPin(TEXT("Pose"));
	const UEdGraphPin* InertializationSourcePin = FoundInertialization->FindPin(TEXT("Source"));
	const UEdGraphPin* InertializationPosePin = FoundInertialization->FindPin(TEXT("Pose"));
	const UEdGraphPin* RootResultPin = FoundRoot->FindPin(TEXT("Result"));
	if (!MachinePosePin || !CombatSlotSourcePin || !CombatSlotPosePin ||
		!InertializationSourcePin || !InertializationPosePin || !RootResultPin ||
		FoundCombatSlot->Node.SlotName != TEXT("DefaultSlot") || !FoundCombatSlot->Node.bAlwaysUpdateSourcePose ||
		MachinePosePin->LinkedTo.Num() != 1 || MachinePosePin->LinkedTo[0] != CombatSlotSourcePin ||
		CombatSlotPosePin->LinkedTo.Num() != 1 || CombatSlotPosePin->LinkedTo[0] != InertializationSourcePin ||
		InertializationPosePin->LinkedTo.Num() != 1 || InertializationPosePin->LinkedTo[0] != RootResultPin)
	{
		ValidationReport = TEXT("AnimGraph must connect RoverLocomotion -> DefaultSlot -> Inertialization -> Output Pose.");
		UE_LOG(
			LogTemp,
			Error,
			TEXT("%s machine_slot=%s slot_inertial=%s inertial_root=%s"),
			*ValidationReport,
			MachinePosePin && CombatSlotSourcePin && MachinePosePin->LinkedTo.Contains(CombatSlotSourcePin) ? TEXT("true") : TEXT("false"),
			CombatSlotPosePin && InertializationSourcePin && CombatSlotPosePin->LinkedTo.Contains(InertializationSourcePin) ? TEXT("true") : TEXT("false"),
			InertializationPosePin && RootResultPin && InertializationPosePin->LinkedTo.Contains(RootResultPin) ? TEXT("true") : TEXT("false"));
		return false;
	}

	const TMap<FString, TSet<FString>> ExpectedStateSequences = {
		{TEXT("Grounded"), {TEXT("Stand1"), TEXT("Stand2"), TEXT("Sprint_F")}},
		{TEXT("SprintImpulse"), {TEXT("Sprint_Impulse_F")}},
		{TEXT("JumpStart"), {TEXT("Jump_Walk_LF"), TEXT("Jump_Walk_RF"), TEXT("Jump_Run_LF"), TEXT("Jump_Run_RF")}},
		{TEXT("Airborne"), {TEXT("Jump_Loop"), TEXT("Fall_Loop"), TEXT("Fall_Loop_Fast")}},
		{TEXT("SecondJump"), {TEXT("Jump_Second_F"), TEXT("Jump_Second_B")}},
		{TEXT("LandLight"), {TEXT("Land_Light")}},
		{TEXT("LandHeavy"), {TEXT("Land_Heavy")}},
		{TEXT("LandRoll"), {TEXT("Land_Roll")}},
		{TEXT("TurnInPlace"), {TEXT("Stand1_Turn_L90D"), TEXT("Stand1_Turn_R90D")}},
		{TEXT("RunTurnback"), {TEXT("Run_Turnback")}},
		{TEXT("MoveStop"), {TEXT("Stop_Walk_L"), TEXT("Stop_Walk_R"), TEXT("Stop_Run_L"), TEXT("Stop_Run_R"), TEXT("Stop_Sprint_L"), TEXT("Stop_Sprint_R")}}};
	const TMap<FString, TSet<FString>> ExpectedStateBlendSpaces = {
		{TEXT("Grounded"), {TEXT("BS_Rover_Walk"), TEXT("BS_Rover_Run")}},
		{TEXT("SprintImpulse"), {}},
		{TEXT("JumpStart"), {}},
		{TEXT("Airborne"), {}},
		{TEXT("SecondJump"), {}},
		{TEXT("LandLight"), {}},
		{TEXT("LandHeavy"), {}},
		{TEXT("LandRoll"), {}},
		{TEXT("TurnInPlace"), {}},
		{TEXT("RunTurnback"), {}},
		{TEXT("MoveStop"), {}}};
	for (const TPair<FString, TSet<FString>>& Expected : ExpectedStateSequences)
	{
		const TSet<FString>* Found = StateSequences.Find(Expected.Key);
		if (!Found || Found->Num() != Expected.Value.Num() || !Found->Includes(Expected.Value))
		{
			ValidationReport = FString::Printf(TEXT("State %s has an unexpected animation sequence set."), *Expected.Key);
			return false;
		}
	}
	for (const TPair<FString, TSet<FString>>& Expected : ExpectedStateBlendSpaces)
	{
		const TSet<FString>* Found = StateBlendSpaces.Find(Expected.Key);
		if (!Found || Found->Num() != Expected.Value.Num() || !Found->Includes(Expected.Value))
		{
			ValidationReport = FString::Printf(TEXT("State %s has an unexpected BlendSpace set."), *Expected.Key);
			return false;
		}
	}
	if (StateTwoWayBlendCounts.FindRef(TEXT("Grounded")) != 1 ||
		StateTwoWayBlendCounts.FindRef(TEXT("Airborne")) != 1)
	{
		ValidationReport = TEXT("Grounded and Airborne must each contain exactly one float-driven two-way blend.");
		return false;
	}
	const TMap<FString, int32> ExpectedBoolBlendCounts = {
		{TEXT("Grounded"), 3},
		{TEXT("SprintImpulse"), 0},
		{TEXT("JumpStart"), 3},
		{TEXT("Airborne"), 1},
		{TEXT("SecondJump"), 1},
		{TEXT("LandLight"), 0},
		{TEXT("LandHeavy"), 0},
		{TEXT("LandRoll"), 0},
		{TEXT("TurnInPlace"), 1},
		{TEXT("RunTurnback"), 0},
		{TEXT("MoveStop"), 5}};
	for (const TPair<FString, int32>& Expected : ExpectedBoolBlendCounts)
	{
		if (StateBoolBlendCounts.FindRef(Expected.Key) != Expected.Value)
		{
			ValidationReport = FString::Printf(TEXT("State %s has an unexpected bool blend count."), *Expected.Key);
			return false;
		}
	}
	for (const FString& StateName : ExpectedStates)
	{
		const bool bGroundTurnState = StateName == TEXT("TurnInPlace") || StateName == TEXT("RunTurnback");
		const bool bMoveStopState = StateName == TEXT("MoveStop");
		const FName ExpectedEntered = bGroundTurnState
			? FName(TEXT("RoverGroundTurnEntered"))
			: (bMoveStopState ? FName(TEXT("RoverMoveStopEntered")) : NAME_None);
		const FName ExpectedLeft = bGroundTurnState
			? FName(TEXT("RoverGroundTurnExited"))
			: (bMoveStopState ? FName(TEXT("RoverMoveStopExited")) : NAME_None);
		if (StateEnteredNotifies.FindRef(StateName) != ExpectedEntered || StateLeftNotifies.FindRef(StateName) != ExpectedLeft)
		{
			ValidationReport = FString::Printf(TEXT("State %s has unexpected entry or exit notifies."), *StateName);
			return false;
		}
	}
	if (!URoverAnimInstance::StaticClass()->FindFunctionByName(TEXT("AnimNotify_RoverGroundTurnEntered")) ||
		!URoverAnimInstance::StaticClass()->FindFunctionByName(TEXT("AnimNotify_RoverGroundTurnExited")) ||
		!URoverAnimInstance::StaticClass()->FindFunctionByName(TEXT("AnimNotify_RoverMoveStopEntered")) ||
		!URoverAnimInstance::StaticClass()->FindFunctionByName(TEXT("AnimNotify_RoverMoveStopExited")))
	{
		ValidationReport = TEXT("URoverAnimInstance is missing locomotion state notify handlers.");
		return false;
	}

	UAnimationStateMachineGraph* MachineGraph = FoundMachine->EditorStateMachineGraph;
	if (!MachineGraph || !MachineGraph->EntryNode ||
		MachineGraph->EntryNode->GetOutputPin()->LinkedTo.Num() != 1)
	{
		ValidationReport = TEXT("RoverLocomotion has an invalid entry connection.");
		return false;
	}
	const UAnimStateNode* EntryState = Cast<UAnimStateNode>(
		MachineGraph->EntryNode->GetOutputPin()->LinkedTo[0]->GetOwningNode());
	if (!EntryState || EntryState->GetStateName() != TEXT("Grounded"))
	{
		ValidationReport = TEXT("RoverLocomotion entry must connect directly to Grounded.");
		return false;
	}

	const TSet<FString> ExpectedTransitions = {
		TEXT("Grounded->JumpStart"), TEXT("Grounded->SprintImpulse"), TEXT("Grounded->Airborne"), TEXT("Grounded->MoveStop"), TEXT("Grounded->RunTurnback"), TEXT("Grounded->TurnInPlace"),
		TEXT("SprintImpulse->JumpStart"), TEXT("SprintImpulse->Airborne"), TEXT("SprintImpulse->Grounded"),
		TEXT("JumpStart->SecondJump"), TEXT("JumpStart->LandRoll"), TEXT("JumpStart->LandHeavy"), TEXT("JumpStart->LandLight"), TEXT("JumpStart->Grounded"), TEXT("JumpStart->Airborne"),
		TEXT("Airborne->SecondJump"), TEXT("Airborne->LandRoll"), TEXT("Airborne->LandHeavy"), TEXT("Airborne->LandLight"), TEXT("Airborne->Grounded"),
		TEXT("SecondJump->LandRoll"), TEXT("SecondJump->LandHeavy"), TEXT("SecondJump->LandLight"), TEXT("SecondJump->Grounded"), TEXT("SecondJump->Airborne"),
		TEXT("LandLight->JumpStart"), TEXT("LandLight->SprintImpulse"), TEXT("LandLight->Airborne"), TEXT("LandLight->Grounded"),
		TEXT("LandHeavy->JumpStart"), TEXT("LandHeavy->SprintImpulse"), TEXT("LandHeavy->Airborne"), TEXT("LandHeavy->Grounded"),
		TEXT("LandRoll->JumpStart"), TEXT("LandRoll->SprintImpulse"), TEXT("LandRoll->Airborne"), TEXT("LandRoll->Grounded"),
		TEXT("TurnInPlace->JumpStart"), TEXT("TurnInPlace->Airborne"), TEXT("TurnInPlace->Grounded"),
		TEXT("RunTurnback->JumpStart"), TEXT("RunTurnback->Airborne"), TEXT("RunTurnback->Grounded"),
		TEXT("MoveStop->JumpStart"), TEXT("MoveStop->Airborne"), TEXT("MoveStop->Grounded")};
	const TSet<FString> ExpectedAutomaticTransitions = {
		TEXT("SprintImpulse->Grounded"),
		TEXT("JumpStart->Airborne"),
		TEXT("SecondJump->Airborne"),
		TEXT("TurnInPlace->Grounded")};
	const TMap<FString, int32> ExpectedTransitionPriorities = {
		{TEXT("Grounded->JumpStart"), 1}, {TEXT("Grounded->SprintImpulse"), 2}, {TEXT("Grounded->Airborne"), 3}, {TEXT("Grounded->MoveStop"), 4}, {TEXT("Grounded->RunTurnback"), 5}, {TEXT("Grounded->TurnInPlace"), 6},
		{TEXT("SprintImpulse->JumpStart"), 1}, {TEXT("SprintImpulse->Airborne"), 2}, {TEXT("SprintImpulse->Grounded"), 3},
		{TEXT("JumpStart->SecondJump"), 1}, {TEXT("JumpStart->LandRoll"), 2}, {TEXT("JumpStart->LandHeavy"), 3}, {TEXT("JumpStart->LandLight"), 4}, {TEXT("JumpStart->Grounded"), 5}, {TEXT("JumpStart->Airborne"), 6},
		{TEXT("Airborne->SecondJump"), 1}, {TEXT("Airborne->LandRoll"), 2}, {TEXT("Airborne->LandHeavy"), 3}, {TEXT("Airborne->LandLight"), 4}, {TEXT("Airborne->Grounded"), 5},
		{TEXT("SecondJump->LandRoll"), 1}, {TEXT("SecondJump->LandHeavy"), 2}, {TEXT("SecondJump->LandLight"), 3}, {TEXT("SecondJump->Grounded"), 4}, {TEXT("SecondJump->Airborne"), 5},
		{TEXT("LandLight->JumpStart"), 1}, {TEXT("LandLight->SprintImpulse"), 2}, {TEXT("LandLight->Airborne"), 3}, {TEXT("LandLight->Grounded"), 4},
		{TEXT("LandHeavy->JumpStart"), 1}, {TEXT("LandHeavy->SprintImpulse"), 2}, {TEXT("LandHeavy->Airborne"), 3}, {TEXT("LandHeavy->Grounded"), 4},
		{TEXT("LandRoll->JumpStart"), 1}, {TEXT("LandRoll->SprintImpulse"), 2}, {TEXT("LandRoll->Airborne"), 3}, {TEXT("LandRoll->Grounded"), 4},
		{TEXT("TurnInPlace->JumpStart"), 1}, {TEXT("TurnInPlace->Airborne"), 2}, {TEXT("TurnInPlace->Grounded"), 3},
		{TEXT("RunTurnback->JumpStart"), 1}, {TEXT("RunTurnback->Airborne"), 2}, {TEXT("RunTurnback->Grounded"), 3},
		{TEXT("MoveStop->JumpStart"), 1}, {TEXT("MoveStop->Airborne"), 2}, {TEXT("MoveStop->Grounded"), 3}};
	TSet<FString> FoundTransitions;
	TSet<FString> FoundAutomaticTransitions;
	int32 TransitionNodeCount = 0;
	for (UEdGraphNode* Node : MachineGraph->Nodes)
	{
		const UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
		if (!Transition)
		{
			continue;
		}
		++TransitionNodeCount;
		const UAnimStateNodeBase* PreviousState = Transition->GetPreviousState();
		const UAnimStateNodeBase* NextState = Transition->GetNextState();
		if (!PreviousState || !NextState)
		{
			ValidationReport = TEXT("RoverLocomotion contains a disconnected transition.");
			return false;
		}
		const FString Edge = PreviousState->GetStateName() + TEXT("->") + NextState->GetStateName();
		FoundTransitions.Add(Edge);
		const int32* ExpectedPriority = ExpectedTransitionPriorities.Find(Edge);
		const ETransitionLogicType::Type ExpectedLogicType =
			Edge == TEXT("MoveStop->Grounded")
			? ETransitionLogicType::TLT_Inertialization
			: ETransitionLogicType::TLT_StandardBlend;
		const float ExpectedCrossfadeDuration = Edge == TEXT("RunTurnback->Grounded")
			? ResolveRunTurnbackBlendOutDuration()
			: 0.12f;
		const EAlphaBlendOption ExpectedBlendMode = Edge == TEXT("RunTurnback->Grounded")
			? EAlphaBlendOption::Cubic
			: EAlphaBlendOption::Linear;
		if (!ExpectedPriority ||
			Transition->PriorityOrder != *ExpectedPriority ||
			!FMath::IsNearlyEqual(Transition->CrossfadeDuration, ExpectedCrossfadeDuration) ||
			Transition->BlendMode != ExpectedBlendMode ||
			Transition->LogicType != ExpectedLogicType ||
			Transition->bAllowInertializationForSelfTransitions)
		{
			ValidationReport = FString::Printf(TEXT("Transition %s has unexpected crossfade settings."), *Edge);
			UE_LOG(
				LogTemp,
				Error,
				TEXT("%s priority=%d expected_priority=%d crossfade=%.3f logic_type=%d allow_self_inertialization=%s"),
				*ValidationReport,
				Transition->PriorityOrder,
				ExpectedPriority ? *ExpectedPriority : INDEX_NONE,
				Transition->CrossfadeDuration,
				static_cast<int32>(Transition->LogicType.GetValue()),
				Transition->bAllowInertializationForSelfTransitions ? TEXT("true") : TEXT("false"));
			return false;
		}
		if (Transition->bAutomaticRuleBasedOnSequencePlayerInState)
		{
			FoundAutomaticTransitions.Add(Edge);
		}
	}
	if (TransitionNodeCount != ExpectedTransitions.Num() ||
		FoundTransitions.Num() != ExpectedTransitions.Num() ||
		!FoundTransitions.Includes(ExpectedTransitions))
	{
		ValidationReport = FString::Printf(
			TEXT("Unexpected transition set; found %d nodes and %d unique of %d required."),
			TransitionNodeCount,
			FoundTransitions.Num(),
			ExpectedTransitions.Num());
		return false;
	}
	if (FoundAutomaticTransitions.Num() != ExpectedAutomaticTransitions.Num() ||
		!FoundAutomaticTransitions.Includes(ExpectedAutomaticTransitions))
	{
		ValidationReport = TEXT("RoverLocomotion has an unexpected automatic transition set.");
		return false;
	}

	ValidationReport = FString::Printf(
		TEXT("parent=URoverAnimInstance skeleton=%s machine=RoverLocomotion combat_slot=DefaultSlot_full_body recovery_cancel=montage_blendout states=%d transitions=%d run_turnback_exit=configured_full_body_cubic_crossfade move_stop_exit=inertialized resources=verified compile=clean"),
		*PreviewMesh->GetSkeleton()->GetName(),
		StateNames.Num(),
		FoundTransitions.Num());
	bValidationSucceeded = true;
	return true;
}

FString URoverAnimationEditorLibrary::GetRoverAnimBlueprintValidationReport(
	const UAnimBlueprint* AnimBlueprint,
	const USkeletalMesh* PreviewMesh)
{
	FString ValidationReport;
	const bool bValid = ValidateRoverAnimBlueprint(AnimBlueprint, PreviewMesh, ValidationReport);
	if (ValidationReport.IsEmpty())
	{
		ValidationReport = bValid
			? TEXT("AnimBlueprint validation succeeded without a report.")
			: TEXT("AnimBlueprint validation failed without a report.");
	}
	return ValidationReport;
}

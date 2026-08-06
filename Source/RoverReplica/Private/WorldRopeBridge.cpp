#include "WorldRopeBridge.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FName GeneratedBridgeComponentTag(TEXT("RopeBridgeGenerated"));
const FName PrimaryAnchorConstraintTag(TEXT("RopeBridgePrimaryAnchor"));
const FName SecondaryAnchorConstraintTag(TEXT("RopeBridgeSecondaryAnchor"));
constexpr int32 BridgeAnchorCount = 4;
const FName AnchorConstraintTags[BridgeAnchorCount] = {
	TEXT("RopeBridgeAnchor0"),
	TEXT("RopeBridgeAnchor1"),
	TEXT("RopeBridgeAnchor2"),
	TEXT("RopeBridgeAnchor3")};

float CalculateSagHeight(const float Alpha, const float Sag)
{
	return -4.0f * Sag * Alpha * (1.0f - Alpha);
}

TArray<FVector> CalculateAnchorLocations(
	const FWorldRopeBridgeSettings& Settings,
	const float Span,
	const float DeckCenterZ)
{
	const float AnchorHalfSpan = Span * 0.5f + Settings.AnchorExtension;
	const float AnchorLateralOffset = FMath::Max(
		0.0f,
		Settings.PlankWidth * 0.5f - Settings.AnchorLateralInset);
	TArray<FVector> Locations;
	Locations.Reserve(BridgeAnchorCount);
	Locations.Emplace(-AnchorHalfSpan, -AnchorLateralOffset, DeckCenterZ);
	Locations.Emplace(-AnchorHalfSpan, AnchorLateralOffset, DeckCenterZ);
	Locations.Emplace(AnchorHalfSpan, -AnchorLateralOffset, DeckCenterZ);
	Locations.Emplace(AnchorHalfSpan, AnchorLateralOffset, DeckCenterZ);
	return Locations;
}

bool TryGetConstraintFrameSeparation(
	UPhysicsConstraintComponent* Constraint,
	float& OutSeparation)
{
	OutSeparation = TNumericLimits<float>::Max();
	if (!IsValid(Constraint))
	{
		return false;
	}

	UPrimitiveComponent* Component1 = nullptr;
	UPrimitiveComponent* Component2 = nullptr;
	FName BoneName1 = NAME_None;
	FName BoneName2 = NAME_None;
	Constraint->GetConstrainedComponents(
		Component1,
		BoneName1,
		Component2,
		BoneName2);
	if (!IsValid(Component1) || !IsValid(Component2))
	{
		return false;
	}

	const float ConstraintScale = FMath::Max(
		Constraint->ConstraintInstance.GetLastKnownScale(),
		0.01f);
	auto GetWorldFrame = [Constraint, ConstraintScale](const EConstraintFrame::Type Frame)
	{
		FTransform LocalFrame = Constraint->ConstraintInstance.GetRefFrame(Frame);
		LocalFrame.ScaleTranslation(FVector(ConstraintScale));
		FTransform BodyTransform = Constraint->GetBodyTransform(Frame);
		BodyTransform.RemoveScaling();
		return LocalFrame * BodyTransform;
	};

	const FVector Frame1Location = GetWorldFrame(EConstraintFrame::Frame1).GetLocation();
	const FVector Frame2Location = GetWorldFrame(EConstraintFrame::Frame2).GetLocation();
	if (Frame1Location.ContainsNaN() || Frame2Location.ContainsNaN())
	{
		return false;
	}
	OutSeparation = FVector::Distance(Frame1Location, Frame2Location);
	return FMath::IsFinite(OutSeparation);
}
}

AWorldRopeBridge::AWorldRopeBridge()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	LeftSupport = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftSupport"));
	LeftSupport->SetupAttachment(SceneRoot);
	RightSupport = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightSupport"));
	RightSupport->SetupAttachment(SceneRoot);
	LeftSupportSecondary = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftSupportSecondary"));
	LeftSupportSecondary->SetupAttachment(SceneRoot);
	RightSupportSecondary = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightSupportSecondary"));
	RightSupportSecondary->SetupAttachment(SceneRoot);
	CharacterTrackingVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("CharacterTrackingVolume"));
	CharacterTrackingVolume->SetupAttachment(SceneRoot);
	CharacterTrackingVolume->SetMobility(EComponentMobility::Movable);
	CharacterTrackingVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CharacterTrackingVolume->SetCollisionObjectType(ECC_WorldDynamic);
	CharacterTrackingVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	CharacterTrackingVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CharacterTrackingVolume->SetGenerateOverlapEvents(true);
	CharacterTrackingVolume->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		PlankMesh = CubeFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		SupportMesh = CylinderFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlankMaterialFinder(
		TEXT("/Game/PhysicsWorldDemo/Materials/M_Demo_WoodCrate.M_Demo_WoodCrate"));
	if (PlankMaterialFinder.Succeeded())
	{
		PlankMaterial = PlankMaterialFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> WoodPhysicalMaterialFinder(
		TEXT("/Game/PhysicsWorldDemo/Materials/PhysicalMaterials/PM_Wood.PM_Wood"));
	if (WoodPhysicalMaterialFinder.Succeeded())
	{
		PlankPhysicalMaterial = WoodPhysicalMaterialFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UWorldInteractionConfig> ConfigFinder(
		TEXT("/Game/PhysicsWorldDemo/Config/DA_WorldInteractionConfig.DA_WorldInteractionConfig"));
	if (ConfigFinder.Succeeded())
	{
		InteractionConfig = ConfigFinder.Object;
	}

	for (UStaticMeshComponent* Support : {
		LeftSupport,
		LeftSupportSecondary,
		RightSupport,
		RightSupportSecondary})
	{
		Support->SetStaticMesh(SupportMesh);
		Support->SetMobility(EComponentMobility::Movable);
		Support->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Support->SetSimulatePhysics(false);
		Support->CanCharacterStepUpOn = ECB_Yes;
	}
}

void AWorldRopeBridge::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildBridge();
}

void AWorldRopeBridge::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (GeneratedPlanks.IsEmpty() || GeneratedConstraints.IsEmpty())
	{
		RefreshGeneratedComponentReferences();
	}
}

void AWorldRopeBridge::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !CharacterTrackingVolume || GeneratedPlanks.IsEmpty())
	{
		return;
	}

	const FWorldRopeBridgeSettings Settings = GetResolvedBridgeSettings();
	TArray<AActor*> OverlappingActors;
	CharacterTrackingVolume->GetOverlappingActors(OverlappingActors, ACharacter::StaticClass());
	TSet<TWeakObjectPtr<ACharacter>> ActiveCharacters;
	ActiveCharacters.Reserve(OverlappingActors.Num());

	for (AActor* Actor : OverlappingActors)
	{
		ACharacter* Character = Cast<ACharacter>(Actor);
		if (!IsValid(Character))
		{
			continue;
		}
		ActiveCharacters.Add(Character);

		FCharacterContactState& State = CharacterContactStates.FindOrAdd(Character);
		const FVector CurrentVelocity = Character->GetVelocity();
		UStaticMeshComponent* CurrentPlank = FindBridgePlank(Character->GetMovementBaseObject());
		UStaticMeshComponent* PreviousPlank = State.PreviousPlank.Get();
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		const float CharacterMass = Movement ? FMath::Max(Movement->Mass, 0.0f) : 0.0f;
		if (Character->JumpCurrentCount > State.PreviousJumpCount)
		{
			State.bAcceptedJumpPending = true;
		}

		if (CurrentPlank && !PreviousPlank)
		{
			const float LandingSpeed = FMath::Max(0.0f, -State.PreviousVelocity.Z);
			if (LandingSpeed >= Settings.MinimumLandingSpeed)
			{
				LastLandingImpulseMagnitude = FMath::Min(
					CharacterMass * LandingSpeed * Settings.LandingImpulseScale,
					Settings.MaximumLandingImpulse);
				ApplyCharacterImpulse(CurrentPlank, Character, LastLandingImpulseMagnitude);
			}
			State.MovementImpulseElapsed = 0.0f;
		}
		else if (!CurrentPlank && PreviousPlank && State.bAcceptedJumpPending &&
			CurrentVelocity.Z >= Settings.MinimumJumpTakeoffSpeed)
		{
			LastJumpTakeoffImpulseMagnitude = FMath::Min(
				CharacterMass * CurrentVelocity.Z * Settings.JumpTakeoffImpulseScale,
				Settings.MaximumJumpTakeoffImpulse);
			ApplyCharacterImpulse(PreviousPlank, Character, LastJumpTakeoffImpulseMagnitude);
			State.MovementImpulseElapsed = 0.0f;
			State.bAcceptedJumpPending = false;
		}

		if (CurrentPlank)
		{
			const float HorizontalSpeed = CurrentVelocity.Size2D();
			if (HorizontalSpeed >= Settings.MinimumMovementSpeed &&
				Settings.MovementImpulseAtReferenceSpeed > 0.0f)
			{
				if (!State.bWasMoving)
				{
					State.MovementImpulseElapsed = Settings.MovementImpulseInterval;
				}
				State.MovementImpulseElapsed += DeltaSeconds;
				if (State.MovementImpulseElapsed >= Settings.MovementImpulseInterval)
				{
					const float SpeedAlpha = FMath::Clamp(
						HorizontalSpeed / Settings.MovementReferenceSpeed,
						0.0f,
						1.0f);
					LastMovementImpulseMagnitude =
						Settings.MovementImpulseAtReferenceSpeed * SpeedAlpha;
					ApplyCharacterImpulse(CurrentPlank, Character, LastMovementImpulseMagnitude);
					State.MovementImpulseElapsed = FMath::Fmod(
						State.MovementImpulseElapsed,
						Settings.MovementImpulseInterval);
				}
				State.bWasMoving = true;
			}
			else
			{
				State.MovementImpulseElapsed = 0.0f;
				State.bWasMoving = false;
			}
		}
		else
		{
			State.bWasMoving = false;
		}

		State.PreviousPlank = CurrentPlank;
		State.PreviousVelocity = CurrentVelocity;
		State.PreviousJumpCount = Character->JumpCurrentCount;
		if (!CurrentPlank && !PreviousPlank)
		{
			State.bAcceptedJumpPending = false;
		}
	}

	for (auto Iterator = CharacterContactStates.CreateIterator(); Iterator; ++Iterator)
	{
		if (!ActiveCharacters.Contains(Iterator.Key()))
		{
			Iterator.RemoveCurrent();
		}
	}
}

FWorldRopeBridgeSettings AWorldRopeBridge::GetResolvedBridgeSettings() const
{
	FWorldRopeBridgeSettings Settings = bOverrideSharedSettings
		? OverrideSettings
		: InteractionConfig
			? InteractionConfig->Settings.RopeBridge
			: FallbackSettings;
	Settings.PlankCount = FMath::Max(Settings.PlankCount, 12);
	Settings.PlankWidth = FMath::Max(1.0f, Settings.PlankWidth);
	Settings.PlankDepth = FMath::Max(1.0f, Settings.PlankDepth);
	Settings.PlankHeight = FMath::Max(1.0f, Settings.PlankHeight);
	Settings.PlankGap = FMath::Max(0.0f, Settings.PlankGap);
	Settings.BridgeSag = FMath::Max(0.0f, Settings.BridgeSag);
	Settings.AnchorExtension = FMath::Max(0.0f, Settings.AnchorExtension);
	Settings.MinimumAnchorSeparation = FMath::Clamp(
		Settings.MinimumAnchorSeparation,
		UE_KINDA_SMALL_NUMBER,
		Settings.PlankWidth);
	const float MinimumAnchorHalfSeparation = Settings.MinimumAnchorSeparation * 0.5f;
	Settings.AnchorLateralInset = FMath::Clamp(
		Settings.AnchorLateralInset,
		0.0f,
		Settings.PlankWidth * 0.5f - MinimumAnchorHalfSeparation);
	Settings.SupportRadius = FMath::Max(1.0f, Settings.SupportRadius);
	Settings.SupportHeight = FMath::Max(1.0f, Settings.SupportHeight);
	Settings.PlankMassKg = FMath::Max(0.1f, Settings.PlankMassKg);
	Settings.LinearDamping = FMath::Max(0.0f, Settings.LinearDamping);
	Settings.AngularDamping = FMath::Max(0.0f, Settings.AngularDamping);
	Settings.MinimumMovementSpeed = FMath::Max(0.0f, Settings.MinimumMovementSpeed);
	Settings.MovementReferenceSpeed = FMath::Max(1.0f, Settings.MovementReferenceSpeed);
	Settings.MovementImpulseAtReferenceSpeed = FMath::Max(
		0.0f,
		Settings.MovementImpulseAtReferenceSpeed);
	Settings.MovementImpulseInterval = FMath::Max(0.05f, Settings.MovementImpulseInterval);
	Settings.MinimumJumpTakeoffSpeed = FMath::Max(0.0f, Settings.MinimumJumpTakeoffSpeed);
	Settings.JumpTakeoffImpulseScale = FMath::Clamp(
		Settings.JumpTakeoffImpulseScale,
		0.0f,
		1.0f);
	Settings.MaximumJumpTakeoffImpulse = FMath::Max(
		0.0f,
		Settings.MaximumJumpTakeoffImpulse);
	Settings.MinimumLandingSpeed = FMath::Max(0.0f, Settings.MinimumLandingSpeed);
	Settings.LandingImpulseScale = FMath::Clamp(Settings.LandingImpulseScale, 0.0f, 1.0f);
	Settings.MaximumLandingImpulse = FMath::Max(0.0f, Settings.MaximumLandingImpulse);
	Settings.CharacterTrackingHeight = FMath::Max(100.0f, Settings.CharacterTrackingHeight);
	Settings.Swing1LimitDegrees = FMath::Clamp(Settings.Swing1LimitDegrees, 0.0f, 90.0f);
	Settings.Swing2LimitDegrees = FMath::Clamp(Settings.Swing2LimitDegrees, 0.0f, 90.0f);
	Settings.TwistLimitDegrees = FMath::Clamp(Settings.TwistLimitDegrees, 0.0f, 90.0f);
	Settings.ProjectionLinearAlpha = FMath::Clamp(Settings.ProjectionLinearAlpha, 0.0f, 1.0f);
	Settings.ProjectionAngularAlpha = FMath::Clamp(Settings.ProjectionAngularAlpha, 0.0f, 1.0f);
	Settings.ProjectionLinearTolerance = FMath::Max(0.0f, Settings.ProjectionLinearTolerance);
	Settings.ProjectionAngularTolerance = FMath::Max(0.0f, Settings.ProjectionAngularTolerance);
	Settings.PositionSolverIterations = FMath::Clamp(Settings.PositionSolverIterations, 1, 255);
	Settings.VelocitySolverIterations = FMath::Clamp(Settings.VelocitySolverIterations, 1, 255);
	Settings.MaxAngularVelocityDegrees = FMath::Max(0.0f, Settings.MaxAngularVelocityDegrees);
	Settings.MaxDepenetrationVelocity = FMath::Max(0.0f, Settings.MaxDepenetrationVelocity);
	return Settings;
}

void AWorldRopeBridge::RebuildBridge()
{
	const FWorldRopeBridgeSettings Settings = GetResolvedBridgeSettings();
	const int64 ConstraintCount = static_cast<int64>(Settings.PlankCount) + 3;
	if (ConstraintCount > MAX_int32)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Rope bridge '%s' cannot represent %lld constraints for %d planks."),
			*GetName(),
			ConstraintCount,
			Settings.PlankCount);
		return;
	}

	ClearGeneratedComponents();
	const float Step = Settings.PlankDepth + Settings.PlankGap;
	const float Span = Step * static_cast<float>(Settings.PlankCount - 1);
	const float Sag = FMath::Min(Settings.BridgeSag, Span * 0.25f);
	const float DeckCenterZ = Settings.SupportHeight + Settings.PlankHeight * 0.5f;
	const TArray<FVector> AnchorLocations = CalculateAnchorLocations(
		Settings,
		Span,
		DeckCenterZ);

	const FVector SupportScale(
		Settings.SupportRadius / 50.0f,
		Settings.SupportRadius / 50.0f,
		Settings.SupportHeight / 100.0f);
	const TArray<UStaticMeshComponent*> Supports = {
		LeftSupport,
		LeftSupportSecondary,
		RightSupport,
		RightSupportSecondary};
	for (int32 AnchorIndex = 0; AnchorIndex < BridgeAnchorCount; ++AnchorIndex)
	{
		UStaticMeshComponent* Support = Supports[AnchorIndex];
		Support->SetStaticMesh(SupportMesh);
		Support->SetRelativeScale3D(SupportScale);
		Support->SetRelativeLocation(FVector(
			AnchorLocations[AnchorIndex].X,
			AnchorLocations[AnchorIndex].Y,
			Settings.SupportHeight * 0.5f));
		Support->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Support->SetSimulatePhysics(false);
	}
	RestAnchorLocations = AnchorLocations;
	CharacterTrackingVolume->SetBoxExtent(FVector(
		Span * 0.5f + Step,
		Settings.PlankWidth * 0.5f + 30.0f,
		Settings.CharacterTrackingHeight * 0.5f));
	CharacterTrackingVolume->SetRelativeLocation(FVector(
		0.0f,
		0.0f,
		DeckCenterZ + Settings.CharacterTrackingHeight * 0.5f - Settings.PlankHeight));

	GeneratedPlanks.Reserve(Settings.PlankCount);
	RestPlankLocations.Reserve(Settings.PlankCount);
	for (int32 Index = 0; Index < Settings.PlankCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index) /
			static_cast<float>(Settings.PlankCount - 1);
		const float X = FMath::Lerp(-Span * 0.5f, Span * 0.5f, Alpha);
		const float Z = DeckCenterZ + CalculateSagHeight(Alpha, Sag);
		const float Slope = Span > UE_KINDA_SMALL_NUMBER
			? (-4.0f * Sag * (1.0f - 2.0f * Alpha)) / Span
			: 0.0f;
		const FQuat BaseRotation(FVector::UpVector, -HALF_PI);
		const FQuat SlopeRotation(FVector::RightVector, FMath::Atan(Slope));
		const FTransform PlankTransform(
			SlopeRotation * BaseRotation,
			FVector(X, 0.0f, Z),
			FVector(
				Settings.PlankWidth / 100.0f,
				Settings.PlankDepth / 100.0f,
				Settings.PlankHeight / 100.0f));
		GeneratedPlanks.Add(CreatePlank(Index, PlankTransform, Settings));
		RestPlankLocations.Add(PlankTransform.GetLocation());
	}

	RestAdjacentPlankDistances.Reserve(Settings.PlankCount - 1);
	for (int32 Index = 0; Index + 1 < RestPlankLocations.Num(); ++Index)
	{
		RestAdjacentPlankDistances.Add(
			FVector::Distance(RestPlankLocations[Index], RestPlankLocations[Index + 1]));
	}

	GeneratedConstraints.Reserve(static_cast<int32>(ConstraintCount));
	GeneratedConstraints.Add(CreateConstraint(
		TEXT("Constraint_LeftAnchor_NegativeY"),
		GeneratedPlanks[0],
		LeftSupport,
		AnchorLocations[0],
		Settings,
		true,
		false,
		0));
	GeneratedConstraints.Add(CreateConstraint(
		TEXT("Constraint_LeftAnchor_PositiveY"),
		GeneratedPlanks[0],
		LeftSupportSecondary,
		AnchorLocations[1],
		Settings,
		true,
		true,
		1));
	for (int32 Index = 0; Index + 1 < GeneratedPlanks.Num(); ++Index)
	{
		GeneratedConstraints.Add(CreateConstraint(
			FString::Printf(TEXT("Constraint_%02d_%02d"), Index, Index + 1),
			GeneratedPlanks[Index + 1],
			GeneratedPlanks[Index],
			(RestPlankLocations[Index] + RestPlankLocations[Index + 1]) * 0.5f,
			Settings));
	}
	GeneratedConstraints.Add(CreateConstraint(
		TEXT("Constraint_RightAnchor_NegativeY"),
		GeneratedPlanks.Last(),
		RightSupport,
		AnchorLocations[2],
		Settings,
		true,
		false,
		2));
	GeneratedConstraints.Add(CreateConstraint(
		TEXT("Constraint_RightAnchor_PositiveY"),
		GeneratedPlanks.Last(),
		RightSupportSecondary,
		AnchorLocations[3],
		Settings,
		true,
		true,
		3));
}

void AWorldRopeBridge::ClearGeneratedComponents()
{
	CharacterContactStates.Reset();
	GeneratedConstraints.Reset();
	GeneratedPlanks.Reset();
	RestPlankLocations.Reset();
	RestAnchorLocations.Reset();
	RestAdjacentPlankDistances.Reset();

	TInlineComponentArray<UActorComponent*> Components(this);
	for (UActorComponent* Component : Components)
	{
		if (IsValid(Component) && Component->ComponentTags.Contains(GeneratedBridgeComponentTag))
		{
			Component->DestroyComponent();
		}
	}
}

void AWorldRopeBridge::RefreshGeneratedComponentReferences()
{
	GeneratedConstraints.Reset();
	GeneratedPlanks.Reset();

	TInlineComponentArray<UActorComponent*> Components(this);
	for (UActorComponent* Component : Components)
	{
		if (!IsValid(Component) ||
			!Component->ComponentTags.Contains(GeneratedBridgeComponentTag))
		{
			continue;
		}
		if (UStaticMeshComponent* Plank = Cast<UStaticMeshComponent>(Component))
		{
			GeneratedPlanks.Add(Plank);
		}
		else if (UPhysicsConstraintComponent* Constraint =
			Cast<UPhysicsConstraintComponent>(Component))
		{
			GeneratedConstraints.Add(Constraint);
		}
	}
	const FVector BridgeOrigin = GetActorLocation();
	const FVector BridgeForward = GetActorForwardVector();
	GeneratedPlanks.Sort(
		[BridgeOrigin, BridgeForward](
			const UStaticMeshComponent& Left,
			const UStaticMeshComponent& Right)
		{
			return FVector::DotProduct(
				Left.GetComponentLocation() - BridgeOrigin,
				BridgeForward) < FVector::DotProduct(
				Right.GetComponentLocation() - BridgeOrigin,
				BridgeForward);
		});
	GeneratedConstraints.Sort(
		[](const UPhysicsConstraintComponent& Left, const UPhysicsConstraintComponent& Right)
		{
			return Left.GetName() < Right.GetName();
		});
	RebuildRestState(GetResolvedBridgeSettings());
}

void AWorldRopeBridge::RebuildRestState(const FWorldRopeBridgeSettings& Settings)
{
	RestPlankLocations.Reset();
	RestAdjacentPlankDistances.Reset();
	const float Step = Settings.PlankDepth + Settings.PlankGap;
	const float Span = Step * static_cast<float>(Settings.PlankCount - 1);
	const float Sag = FMath::Min(Settings.BridgeSag, Span * 0.25f);
	const float DeckCenterZ = Settings.SupportHeight + Settings.PlankHeight * 0.5f;
	RestAnchorLocations = CalculateAnchorLocations(Settings, Span, DeckCenterZ);
	RestPlankLocations.Reserve(Settings.PlankCount);
	for (int32 Index = 0; Index < Settings.PlankCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index) /
			static_cast<float>(Settings.PlankCount - 1);
		RestPlankLocations.Add(FVector(
			FMath::Lerp(-Span * 0.5f, Span * 0.5f, Alpha),
			0.0f,
			DeckCenterZ + CalculateSagHeight(Alpha, Sag)));
	}
	RestAdjacentPlankDistances.Reserve(Settings.PlankCount - 1);
	for (int32 Index = 0; Index + 1 < RestPlankLocations.Num(); ++Index)
	{
		RestAdjacentPlankDistances.Add(
			FVector::Distance(RestPlankLocations[Index], RestPlankLocations[Index + 1]));
	}
}

UStaticMeshComponent* AWorldRopeBridge::CreatePlank(
	const int32 Index,
	const FTransform& RelativeTransform,
	const FWorldRopeBridgeSettings& Settings)
{
	const FName ComponentName = MakeUniqueObjectName(
		this,
		UStaticMeshComponent::StaticClass(),
		FName(*FString::Printf(TEXT("BridgePlank_%02d_Generated"), Index)));
	UStaticMeshComponent* Plank = NewObject<UStaticMeshComponent>(
		this,
		ComponentName,
		RF_Transactional);
	PostCreateBlueprintComponent(Plank);
	Plank->ComponentTags.Add(GeneratedBridgeComponentTag);
	Plank->SetupAttachment(SceneRoot);
	Plank->SetRelativeTransform(RelativeTransform);
	Plank->SetMobility(EComponentMobility::Movable);
	Plank->SetStaticMesh(PlankMesh);
	if (PlankMaterial)
	{
		Plank->SetMaterial(0, PlankMaterial);
	}
	if (PlankPhysicalMaterial)
	{
		Plank->SetPhysMaterialOverride(PlankPhysicalMaterial);
	}
	Plank->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Plank->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	Plank->CanCharacterStepUpOn = ECB_Yes;
	Plank->SetGenerateOverlapEvents(false);
	Plank->SetEnableGravity(true);
	Plank->SetLinearDamping(Settings.LinearDamping);
	Plank->SetAngularDamping(Settings.AngularDamping);
	Plank->BodyInstance.SetOverrideIterationCounts(true);
	Plank->BodyInstance.SetPositionSolverIterationCount(
		static_cast<uint8>(Settings.PositionSolverIterations));
	Plank->BodyInstance.SetVelocitySolverIterationCount(
		static_cast<uint8>(Settings.VelocitySolverIterations));
	Plank->BodyInstance.SetProjectionSolverIterationCount(1);
	Plank->BodyInstance.SetMaxDepenetrationVelocity(Settings.MaxDepenetrationVelocity);
	Plank->RegisterComponent();
	Plank->SetUseCCD(Settings.bUseContinuousCollisionDetection);
	Plank->SetSimulatePhysics(true);
	Plank->SetMassOverrideInKg(NAME_None, Settings.PlankMassKg, true);
	Plank->SetPhysicsMaxAngularVelocityInDegrees(Settings.MaxAngularVelocityDegrees);
	return Plank;
}

UPhysicsConstraintComponent* AWorldRopeBridge::CreateConstraint(
	const FString& Name,
	UStaticMeshComponent* Child,
	UStaticMeshComponent* Parent,
	const FVector& RelativeLocation,
	const FWorldRopeBridgeSettings& Settings,
	const bool bEndpointAnchor,
	const bool bSecondaryAnchor,
	const int32 AnchorIndex)
{
	const FName ComponentName = MakeUniqueObjectName(
		this,
		UPhysicsConstraintComponent::StaticClass(),
		FName(*FString::Printf(TEXT("%s_Generated"), *Name)));
	UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(
		this,
		ComponentName,
		RF_Transactional);
	PostCreateBlueprintComponent(Constraint);
	Constraint->ComponentTags.Add(GeneratedBridgeComponentTag);
	if (bEndpointAnchor)
	{
		Constraint->ComponentTags.Add(
			bSecondaryAnchor
				? SecondaryAnchorConstraintTag
				: PrimaryAnchorConstraintTag);
		if (AnchorIndex >= 0 && AnchorIndex < BridgeAnchorCount)
		{
			Constraint->ComponentTags.Add(AnchorConstraintTags[AnchorIndex]);
		}
	}
	Constraint->SetupAttachment(SceneRoot);
	// Local X=Up, Y=bridge direction, Z=plank width. This maps Swing1 to
	// fore/aft pitch and Swing2 to side roll while Twist locks horizontal yaw.
	const FRotator ConstraintRotation = FRotationMatrix::MakeFromXY(
		FVector::UpVector,
		FVector::ForwardVector).Rotator();
	Constraint->SetRelativeLocationAndRotation(RelativeLocation, ConstraintRotation);
	Constraint->RegisterComponent();
	Constraint->SetLinearXLimit(LCM_Locked, 0.0f);
	Constraint->SetLinearYLimit(LCM_Locked, 0.0f);
	Constraint->SetLinearZLimit(
		bSecondaryAnchor ? LCM_Free : LCM_Locked,
		0.0f);
	if (bSecondaryAnchor)
	{
		Constraint->SetAngularSwing1Limit(ACM_Free, 0.0f);
		Constraint->SetAngularSwing2Limit(ACM_Free, 0.0f);
		Constraint->SetAngularTwistLimit(ACM_Free, 0.0f);
	}
	else if (bEndpointAnchor)
	{
		Constraint->SetAngularSwing1Limit(ACM_Limited, Settings.Swing1LimitDegrees);
		Constraint->SetAngularSwing2Limit(ACM_Free, 0.0f);
		Constraint->SetAngularTwistLimit(ACM_Free, 0.0f);
	}
	else
	{
		Constraint->SetAngularSwing1Limit(ACM_Limited, Settings.Swing1LimitDegrees);
		Constraint->SetAngularSwing2Limit(ACM_Limited, Settings.Swing2LimitDegrees);
		Constraint->SetAngularTwistLimit(
			Settings.TwistLimitDegrees > UE_KINDA_SMALL_NUMBER ? ACM_Limited : ACM_Locked,
			Settings.TwistLimitDegrees);
	}
	Constraint->SetDisableCollision(true);
	Constraint->SetLinearBreakable(false, 0.0f);
	Constraint->SetAngularBreakable(false, 0.0f);
	Constraint->SetProjectionEnabled(!bSecondaryAnchor);
	if (!bSecondaryAnchor)
	{
		Constraint->SetProjectionParams(
			Settings.ProjectionLinearAlpha,
			Settings.ProjectionAngularAlpha,
			Settings.ProjectionLinearTolerance,
			Settings.ProjectionAngularTolerance);
	}
	Constraint->ConstraintInstance.ProfileInstance.bUseLinearJointSolver = false;
	Constraint->ConstraintInstance.SetShockPropagationParams(false, 0.0f);
	if (Settings.bEnableMassConditioning)
	{
		Constraint->ConstraintInstance.EnableMassConditioning();
	}
	else
	{
		Constraint->ConstraintInstance.DisableMassConditioning();
	}
	Constraint->SetConstrainedComponents(Child, NAME_None, Parent, NAME_None);
	return Constraint;
}

UStaticMeshComponent* AWorldRopeBridge::GetPlankComponent(const int32 Index) const
{
	return GeneratedPlanks.IsValidIndex(Index) ? GeneratedPlanks[Index].Get() : nullptr;
}

UPhysicsConstraintComponent* AWorldRopeBridge::GetConstraintComponent(const int32 Index) const
{
	return GeneratedConstraints.IsValidIndex(Index) ? GeneratedConstraints[Index].Get() : nullptr;
}

bool AWorldRopeBridge::AreAllPlanksSimulatingPhysics() const
{
	return !GeneratedPlanks.IsEmpty() && GeneratedPlanks.ContainsByPredicate(
		[](const UStaticMeshComponent* Plank)
		{
			return !IsValid(Plank) || !Plank->IsSimulatingPhysics();
		}) == false;
}

int32 AWorldRopeBridge::GetValidSupportCount() const
{
	int32 Count = 0;
	for (const UStaticMeshComponent* Support : {
		LeftSupport.Get(),
		LeftSupportSecondary.Get(),
		RightSupport.Get(),
		RightSupportSecondary.Get()})
	{
		if (IsValid(Support) && Support->GetStaticMesh() && !Support->IsSimulatingPhysics())
		{
			++Count;
		}
	}
	return Count;
}

bool AWorldRopeBridge::HasValidConstraintConfiguration() const
{
	const FWorldRopeBridgeSettings Settings = GetResolvedBridgeSettings();
	if (GeneratedPlanks.Num() < 2 ||
		static_cast<int64>(GeneratedConstraints.Num()) !=
		static_cast<int64>(GeneratedPlanks.Num()) + 3 ||
		GetValidSupportCount() != BridgeAnchorCount)
	{
		return false;
	}
	int32 InternalConstraintCount = 0;
	int32 PrimaryAnchorCount = 0;
	int32 SecondaryAnchorCount = 0;
	int32 AnchorTagCounts[BridgeAnchorCount] = {};
	for (UPhysicsConstraintComponent* Constraint : GeneratedConstraints)
	{
		if (!IsValid(Constraint) || Constraint->IsBroken())
		{
			return false;
		}
		const bool bPrimaryAnchor =
			Constraint->ComponentTags.Contains(PrimaryAnchorConstraintTag);
		const bool bSecondaryAnchor =
			Constraint->ComponentTags.Contains(SecondaryAnchorConstraintTag);
		if (bPrimaryAnchor && bSecondaryAnchor)
		{
			return false;
		}
		const bool bEndpointAnchor = bPrimaryAnchor || bSecondaryAnchor;
		int32 AnchorIndex = INDEX_NONE;
		for (int32 CandidateIndex = 0; CandidateIndex < BridgeAnchorCount; ++CandidateIndex)
		{
			if (Constraint->ComponentTags.Contains(AnchorConstraintTags[CandidateIndex]))
			{
				if (AnchorIndex != INDEX_NONE)
				{
					return false;
				}
				AnchorIndex = CandidateIndex;
				++AnchorTagCounts[CandidateIndex];
			}
		}
		if (bEndpointAnchor != (AnchorIndex != INDEX_NONE) ||
			(bSecondaryAnchor != (AnchorIndex == 1 || AnchorIndex == 3)))
		{
			return false;
		}
		if (bEndpointAnchor)
		{
			UPrimitiveComponent* Component1 = nullptr;
			UPrimitiveComponent* Component2 = nullptr;
			FName BoneName1 = NAME_None;
			FName BoneName2 = NAME_None;
			Constraint->GetConstrainedComponents(
				Component1,
				BoneName1,
				Component2,
				BoneName2);
			const UStaticMeshComponent* ExpectedPlank = AnchorIndex < 2
				? GeneratedPlanks[0].Get()
				: GeneratedPlanks.Last().Get();
			const UStaticMeshComponent* ExpectedSupport = nullptr;
			switch (AnchorIndex)
			{
			case 0:
				ExpectedSupport = LeftSupport;
				break;
			case 1:
				ExpectedSupport = LeftSupportSecondary;
				break;
			case 2:
				ExpectedSupport = RightSupport;
				break;
			case 3:
				ExpectedSupport = RightSupportSecondary;
				break;
			default:
				return false;
			}
			if (Component1 != ExpectedPlank || Component2 != ExpectedSupport)
			{
				return false;
			}
		}
		InternalConstraintCount += !bEndpointAnchor ? 1 : 0;
		PrimaryAnchorCount += bPrimaryAnchor ? 1 : 0;
		SecondaryAnchorCount += bSecondaryAnchor ? 1 : 0;
		const EAngularConstraintMotion ExpectedTwistMotion =
			Settings.TwistLimitDegrees > UE_KINDA_SMALL_NUMBER ? ACM_Limited : ACM_Locked;
		if (Constraint->ConstraintInstance.GetLinearXMotion() != LCM_Locked ||
			Constraint->ConstraintInstance.GetLinearYMotion() != LCM_Locked ||
			Constraint->ConstraintInstance.GetLinearZMotion() !=
				(bSecondaryAnchor ? LCM_Free : LCM_Locked) ||
			Constraint->ConstraintInstance.GetAngularSwing1Motion() !=
				(bSecondaryAnchor ? ACM_Free : ACM_Limited) ||
			Constraint->ConstraintInstance.GetAngularSwing2Motion() !=
				(bEndpointAnchor ? ACM_Free : ACM_Limited) ||
			Constraint->ConstraintInstance.GetAngularTwistMotion() !=
				(bEndpointAnchor ? ACM_Free : ExpectedTwistMotion) ||
			(!bSecondaryAnchor && !FMath::IsNearlyEqual(
				Constraint->ConstraintInstance.GetAngularSwing1Limit(),
				Settings.Swing1LimitDegrees,
				0.01f)) ||
			(!bEndpointAnchor && !FMath::IsNearlyEqual(
				Constraint->ConstraintInstance.GetAngularSwing2Limit(),
				Settings.Swing2LimitDegrees,
				0.01f)) ||
			!Constraint->ConstraintInstance.IsCollisionDisabled() ||
			Constraint->ConstraintInstance.IsProjectionEnabled() == bSecondaryAnchor)
		{
			return false;
		}
	}
	if (InternalConstraintCount != GeneratedPlanks.Num() - 1 ||
		PrimaryAnchorCount != 2 || SecondaryAnchorCount != 2)
	{
		return false;
	}
	for (const int32 TagCount : AnchorTagCounts)
	{
		if (TagCount != 1)
		{
			return false;
		}
	}
	return true;
}

float AWorldRopeBridge::GetMaximumPlankLinearSpeed() const
{
	float MaximumSpeed = 0.0f;
	for (const UStaticMeshComponent* Plank : GeneratedPlanks)
	{
		if (IsValid(Plank))
		{
			MaximumSpeed = FMath::Max(MaximumSpeed, Plank->GetPhysicsLinearVelocity().Size());
		}
	}
	return MaximumSpeed;
}

float AWorldRopeBridge::GetMaximumPlankAngularSpeedDegrees() const
{
	float MaximumSpeed = 0.0f;
	for (const UStaticMeshComponent* Plank : GeneratedPlanks)
	{
		if (IsValid(Plank))
		{
			MaximumSpeed = FMath::Max(
				MaximumSpeed,
				Plank->GetPhysicsAngularVelocityInDegrees().Size());
		}
	}
	return MaximumSpeed;
}

float AWorldRopeBridge::GetMaximumEndpointPositionError() const
{
	float MaximumError = 0.0f;
	for (int32 AnchorIndex = 0; AnchorIndex < BridgeAnchorCount; ++AnchorIndex)
	{
		const float Error = GetEndpointPositionError(AnchorIndex);
		if (!FMath::IsFinite(Error))
		{
			return TNumericLimits<float>::Max();
		}
		MaximumError = FMath::Max(MaximumError, Error);
	}
	return MaximumError;
}

float AWorldRopeBridge::GetEndpointPositionError(const int32 AnchorIndex) const
{
	if (AnchorIndex < 0 || AnchorIndex >= BridgeAnchorCount)
	{
		return TNumericLimits<float>::Max();
	}
	for (UPhysicsConstraintComponent* Constraint : GeneratedConstraints)
	{
		if (IsValid(Constraint) &&
			Constraint->ComponentTags.Contains(AnchorConstraintTags[AnchorIndex]))
		{
			float Separation = TNumericLimits<float>::Max();
			return TryGetConstraintFrameSeparation(Constraint, Separation)
				? Separation
				: TNumericLimits<float>::Max();
		}
	}
	return TNumericLimits<float>::Max();
}

float AWorldRopeBridge::GetMaximumAdjacentPlankDistanceError() const
{
	if (GeneratedPlanks.Num() < 2 ||
		RestAdjacentPlankDistances.Num() != GeneratedPlanks.Num() - 1)
	{
		return 0.0f;
	}
	float MaximumError = 0.0f;
	for (int32 Index = 0; Index + 1 < GeneratedPlanks.Num(); ++Index)
	{
		MaximumError = FMath::Max(
			MaximumError,
			FMath::Abs(
				FVector::Distance(
					GeneratedPlanks[Index]->GetComponentLocation(),
					GeneratedPlanks[Index + 1]->GetComponentLocation()) -
				RestAdjacentPlankDistances[Index]));
	}
	return MaximumError;
}

bool AWorldRopeBridge::ApplyImpulseToCenterPlank(const FVector& WorldImpulse)
{
	UStaticMeshComponent* CenterPlank = GetPlankComponent(GeneratedPlanks.Num() / 2);
	if (!CenterPlank || !CenterPlank->IsSimulatingPhysics() || WorldImpulse.ContainsNaN())
	{
		return false;
	}
	CenterPlank->AddImpulseAtLocation(WorldImpulse, CenterPlank->GetComponentLocation());
	return true;
}

UStaticMeshComponent* AWorldRopeBridge::FindBridgePlank(UObject* Component) const
{
	UStaticMeshComponent* Plank = Cast<UStaticMeshComponent>(Component);
	return Plank && GeneratedPlanks.Contains(Plank) ? Plank : nullptr;
}

bool AWorldRopeBridge::IsCharacterSupportedByBridge(const ACharacter* Character) const
{
	return Character && FindBridgePlank(Character->GetMovementBaseObject()) != nullptr;
}

void AWorldRopeBridge::ApplyCharacterImpulse(
	UStaticMeshComponent* Plank,
	const ACharacter* Character,
	const float Magnitude)
{
	if (!IsValid(Plank) || !Plank->IsSimulatingPhysics() || !IsValid(Character) ||
		!FMath::IsFinite(Magnitude) || Magnitude <= 0.0f)
	{
		return;
	}

	FVector ImpulseLocation = Plank->GetComponentLocation();
	Plank->GetClosestPointOnCollision(Character->GetActorLocation(), ImpulseLocation);
	Plank->AddImpulseAtLocation(FVector::DownVector * Magnitude, ImpulseLocation);
}

void AWorldRopeBridge::ResetCharacterResponseDebug()
{
	LastMovementImpulseMagnitude = 0.0f;
	LastJumpTakeoffImpulseMagnitude = 0.0f;
	LastLandingImpulseMagnitude = 0.0f;
}

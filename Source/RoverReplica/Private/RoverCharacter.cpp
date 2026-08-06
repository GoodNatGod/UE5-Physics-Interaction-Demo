#include "RoverCharacter.h"

#include "Camera/CameraComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "MotionWarpingComponent.h"
#include "Misc/PackageName.h"
#include "RoverAnimInstance.h"
#include "RoverCombatComponent.h"
#include "RoverHealthComponent.h"
#include "RoverLocomotionComponent.h"
#include "RoverWorldSkillComponent.h"
#include "UObject/ConstructorHelpers.h"

ARoverCharacter::ARoverCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(34.0f, 90.0f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
	Movement->JumpZVelocity = 630.0f;
	Movement->GravityScale = 1.45f;
	Movement->AirControl = 0.45f;
	Movement->MaxWalkSpeed = 400.0f;
	Movement->MaxAcceleration = 1800.0f;
	Movement->BrakingDecelerationWalking = 1400.0f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 50.0f, 80.0f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 14.0f;
	CameraBoom->CameraLagMaxDistance = 35.0f;
	CameraBoom->bUseCameraLagSubstepping = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetFieldOfView(90.0f);

	LocomotionComponent = CreateDefaultSubobject<URoverLocomotionComponent>(TEXT("RoverLocomotion"));
	CombatComponent = CreateDefaultSubobject<URoverCombatComponent>(TEXT("RoverCombat"));
	HealthComponent = CreateDefaultSubobject<URoverHealthComponent>(TEXT("RoverHealth"));
	WorldSkillComponent = CreateDefaultSubobject<URoverWorldSkillComponent>(TEXT("RoverWorldSkill"));
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarping"));

	constexpr TCHAR MovementConfigPackagePath[] = TEXT("/Game/Rover/Config/DA_RoverMovementConfig");
	constexpr TCHAR MovementConfigObjectPath[] = TEXT("/Game/Rover/Config/DA_RoverMovementConfig.DA_RoverMovementConfig");
	if (FPackageName::DoesPackageExist(MovementConfigPackagePath))
	{
		LocomotionComponent->MovementConfig = LoadObject<URoverMovementConfig>(nullptr, MovementConfigObjectPath);
	}

	CombatWeapon = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CombatWeapon"));
	CombatWeapon->SetupAttachment(GetMesh(), TEXT("RoverWeapon"));
	CombatWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CombatWeapon->SetGenerateOverlapEvents(false);
	CombatWeapon->SetVisibility(false, true);
	CombatWeapon->SetHiddenInGame(true, true);

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultContextFinder(TEXT("/Game/Input/IMC_Default.IMC_Default"));
	DefaultMappingContext = DefaultContextFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseContextFinder(TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"));
	MouseLookMappingContext = MouseContextFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> MoveActionFinder(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	MoveAction = MoveActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> LookActionFinder(TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	LookAction = LookActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> MouseLookActionFinder(TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
	MouseLookAction = MouseLookActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> JumpActionFinder(TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	JumpAction = JumpActionFinder.Object;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> RoverMeshFinder(TEXT("/Game/Rover/Character/SK_Rover_Male.SK_Rover_Male"));
	if (RoverMeshFinder.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(RoverMeshFinder.Object);
		GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
		GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
		// Match the 1568.3 cm imported mesh bounds to the 180 cm character capsule.
		GetMesh()->SetRelativeScale3D(FVector(0.1148f));
	}

	static const TCHAR RoverAnimPackage[] = TEXT("/Game/Rover/Animations/ABP_Rover");
	if (FPackageName::DoesPackageExist(RoverAnimPackage))
	{
		UClass* RoverAnimClass = LoadClass<UAnimInstance>(
			nullptr,
			TEXT("/Game/Rover/Animations/ABP_Rover.ABP_Rover_C"));
		if (RoverAnimClass)
		{
			GetMesh()->SetAnimInstanceClass(RoverAnimClass);
		}
	}
}

void ARoverCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnsureRuntimeInputObjects();
}

void ARoverCharacter::BeginPlay()
{
	Super::BeginPlay();
	ConfigureCombatWeapon();
	AddInputMappingContexts();
}

void ARoverCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	AddInputMappingContexts();
}

void ARoverCharacter::NotifyControllerChanged()
{
	RemoveInputMappingContexts();
	Super::NotifyControllerChanged();
	AddInputMappingContexts();
}

void ARoverCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!LocomotionComponent || !CameraBoom || !FollowCamera)
	{
		return;
	}

	UpdateCamera(DeltaSeconds);
}

void ARoverCharacter::UpdateCamera(const float DeltaSeconds)
{
	const FRoverMovementSettings& Settings = LocomotionComponent->GetSettings();
	const bool bSprintCamera = LocomotionComponent->IsSprinting();
	const float TargetFOV = bSprintCamera ? Settings.SprintFOV : Settings.DefaultFOV;
	const float TargetArmLength = bSprintCamera ? Settings.SprintArmLength : Settings.DefaultArmLength;
	FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, TargetFOV, DeltaSeconds, Settings.CameraTransitionSpeed));
	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArmLength, DeltaSeconds, Settings.CameraTransitionSpeed);
	CameraBoom->CameraLagSpeed = Settings.CameraLagSpeed;
	CameraBoom->CameraLagMaxDistance = Settings.CameraLagMaxDistance;

	CameraAutoFollowSuppressionRemaining = FMath::Max(0.0f, CameraAutoFollowSuppressionRemaining - DeltaSeconds);
	if (CombatComponent && CombatComponent->IsAttacking())
	{
		CancelCameraAutoFollow();
		return;
	}

	const FVector2D MoveInput = LocomotionComponent->GetMoveInput();
	if (!MoveInput.IsNearlyZero())
	{
		const FRotator ControlYaw(0.0f, Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw, 0.0f);
		const FVector ForwardDirection = FRotationMatrix(ControlYaw).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(ControlYaw).GetUnitAxis(EAxis::Y);
		const FVector WorldDirection =
			(ForwardDirection * MoveInput.Y + RightDirection * MoveInput.X).GetSafeNormal2D();
		LockCameraForMove(MoveInput, WorldDirection);
		return;
	}

	if (CameraFollowState == ERoverCameraFollowState::MoveLocked)
	{
		QueueCameraRecenter();
	}

	if (CameraFollowState == ERoverCameraFollowState::RecenterDelay)
	{
		CameraRecenterDelayRemaining = FMath::Max(0.0f, CameraRecenterDelayRemaining - DeltaSeconds);
		if (CameraRecenterDelayRemaining > 0.0f)
		{
			return;
		}
		CameraFollowState = ERoverCameraFollowState::Recentering;
	}

	if (!Controller || CameraAutoFollowSuppressionRemaining > 0.0f ||
		Settings.CameraAutoFollowInterpSpeed <= 0.0f || Settings.CameraAutoFollowMaxYawSpeed <= 0.0f)
	{
		return;
	}

	FRotator ControlRotation = Controller->GetControlRotation();
	const bool bFinishingRecenter = CameraFollowState == ERoverCameraFollowState::Recentering;
	const float TargetYaw = bFinishingRecenter ? CameraRecenterTargetYaw : GetActorRotation().Yaw;
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(ControlRotation.Yaw, TargetYaw);
	const float YawTolerance = FMath::Max(0.0f, Settings.CameraAutoFollowYawTolerance);
	if (FMath::Abs(DeltaYaw) <= YawTolerance)
	{
		ControlRotation.Yaw = TargetYaw;
		Controller->SetControlRotation(ControlRotation);
		if (bFinishingRecenter)
		{
			CameraFollowState = ERoverCameraFollowState::Free;
		}
		return;
	}

	const float InterpAlpha = 1.0f - FMath::Exp(-Settings.CameraAutoFollowInterpSpeed * DeltaSeconds);
	const float InterpolatedStep = DeltaYaw * InterpAlpha;
	const float MaxYawStep = Settings.CameraAutoFollowMaxYawSpeed * DeltaSeconds;
	const float YawStep = FMath::Clamp(InterpolatedStep, -MaxYawStep, MaxYawStep);
	ControlRotation.Yaw = FRotator::NormalizeAxis(ControlRotation.Yaw + YawStep);
	Controller->SetControlRotation(ControlRotation);
}

void ARoverCharacter::CancelCameraAutoFollow()
{
	CameraFollowState = ERoverCameraFollowState::Free;
	CameraRecenterDelayRemaining = 0.0f;
}

void ARoverCharacter::LockCameraForMove(const FVector2D& MoveInput, const FVector& WorldDirection)
{
	if (MoveInput.IsNearlyZero() || WorldDirection.IsNearlyZero())
	{
		return;
	}

	CancelCameraAutoFollow();
	CameraFollowState = ERoverCameraFollowState::MoveLocked;
	LastCameraMoveWorldDirection = WorldDirection.GetSafeNormal2D();
}

void ARoverCharacter::QueueCameraRecenter()
{
	if (!LocomotionComponent || LastCameraMoveWorldDirection.IsNearlyZero())
	{
		CancelCameraAutoFollow();
		return;
	}

	CameraRecenterTargetYaw = LastCameraMoveWorldDirection.Rotation().Yaw;
	CameraRecenterDelayRemaining = FMath::Max(0.0f, LocomotionComponent->GetSettings().CameraAutoFollowDelay);
	CameraFollowState = CameraRecenterDelayRemaining > 0.0f
		? ERoverCameraFollowState::RecenterDelay
		: ERoverCameraFollowState::Recentering;
}

void ARoverCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureRuntimeInputObjects();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogTemp, Error, TEXT("RoverCharacter requires an EnhancedInputComponent."));
		return;
	}

	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ARoverCharacter::HandleMove);
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Completed, this, &ARoverCharacter::HandleMoveCompleted);
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Canceled, this, &ARoverCharacter::HandleMoveCompleted);
	}
	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ARoverCharacter::HandleLook);
	}
	if (MouseLookAction)
	{
		EnhancedInput->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ARoverCharacter::HandleLook);
	}
	if (JumpAction)
	{
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ARoverCharacter::HandleJumpStarted);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ARoverCharacter::HandleJumpCompleted);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Canceled, this, &ARoverCharacter::HandleJumpCompleted);
	}
	if (SprintAction)
	{
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &ARoverCharacter::HandleSprintStarted);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &ARoverCharacter::HandleSprintCompleted);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Canceled, this, &ARoverCharacter::HandleSprintCompleted);
	}
	if (CrouchAction)
	{
		EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Started, this, &ARoverCharacter::HandleCrouchStarted);
		EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ARoverCharacter::HandleCrouchCompleted);
		EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Canceled, this, &ARoverCharacter::HandleCrouchCompleted);
	}
	if (AttackAction)
	{
		EnhancedInput->BindAction(AttackAction, ETriggerEvent::Started, this, &ARoverCharacter::HandleAttackStarted);
		EnhancedInput->BindAction(AttackAction, ETriggerEvent::Completed, this, &ARoverCharacter::HandleAttackCompleted);
		EnhancedInput->BindAction(AttackAction, ETriggerEvent::Canceled, this, &ARoverCharacter::HandleAttackCompleted);
	}
	if (FireballAction)
	{
		EnhancedInput->BindAction(FireballAction, ETriggerEvent::Started, this, &ARoverCharacter::HandleFireballStarted);
	}

	AddInputMappingContexts();
}

void ARoverCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (LocomotionComponent)
	{
		LocomotionComponent->HandleGroundJumped();
	}
}

void ARoverCharacter::Landed(const FHitResult& Hit)
{
	const float ImpactSpeed = FMath::Max(0.0f, -GetVelocity().Z);
	Super::Landed(Hit);
	if (LocomotionComponent)
	{
		LocomotionComponent->HandleLanded(ImpactSpeed);
	}
}

void ARoverCharacter::OnMovementModeChanged(const EMovementMode PrevMovementMode, const uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
	if (LocomotionComponent)
	{
		LocomotionComponent->HandleMovementModeChanged();
	}
}

void ARoverCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetCombatWeaponVisible(false);
	RemoveInputMappingContexts();
	Super::EndPlay(EndPlayReason);
}

void ARoverCharacter::EnsureRuntimeInputObjects()
{
	if (!SprintAction)
	{
		SprintAction = NewObject<UInputAction>(this, TEXT("IA_RuntimeSprint"), RF_Transient);
		SprintAction->ValueType = EInputActionValueType::Boolean;
	}
	if (!CrouchAction)
	{
		CrouchAction = NewObject<UInputAction>(this, TEXT("IA_RuntimeCrouch"), RF_Transient);
		CrouchAction->ValueType = EInputActionValueType::Boolean;
	}
	if (!AttackAction)
	{
		AttackAction = NewObject<UInputAction>(this, TEXT("IA_RuntimeLightAttack"), RF_Transient);
		AttackAction->ValueType = EInputActionValueType::Boolean;
	}
	if (!FireballAction)
	{
		FireballAction = NewObject<UInputAction>(this, TEXT("IA_RuntimeFireball"), RF_Transient);
		FireballAction->ValueType = EInputActionValueType::Boolean;
	}
	if (!RuntimeMappingContext)
	{
		RuntimeMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_RuntimeRover"), RF_Transient);
		RuntimeMappingContext->MapKey(SprintAction, EKeys::LeftShift);
		RuntimeMappingContext->MapKey(SprintAction, EKeys::Gamepad_LeftThumbstick);
		RuntimeMappingContext->MapKey(CrouchAction, EKeys::C);
		RuntimeMappingContext->MapKey(CrouchAction, EKeys::Gamepad_FaceButton_Right);
		RuntimeMappingContext->MapKey(AttackAction, EKeys::LeftMouseButton);
		RuntimeMappingContext->MapKey(AttackAction, EKeys::Gamepad_FaceButton_Bottom);
		RuntimeMappingContext->MapKey(FireballAction, EKeys::Q);
		RuntimeMappingContext->MapKey(FireballAction, EKeys::Gamepad_RightShoulder);
	}
}

void ARoverCharacter::AddInputMappingContexts()
{
	EnsureRuntimeInputObjects();

	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!InputSubsystem)
	{
		return;
	}

	if (InputSubsystemWithMappings.IsValid() && InputSubsystemWithMappings.Get() != InputSubsystem)
	{
		RemoveInputMappingContexts();
	}
	InputSubsystemWithMappings = InputSubsystem;

	auto AddOwnedMapping = [InputSubsystem](
		const UInputMappingContext* MappingContext,
		const int32 Priority,
		bool& bAddedByRover)
	{
		if (!MappingContext)
		{
			bAddedByRover = false;
			return;
		}

		if (bAddedByRover && !InputSubsystem->HasMappingContext(MappingContext))
		{
			bAddedByRover = false;
		}
		if (!InputSubsystem->HasMappingContext(MappingContext))
		{
			InputSubsystem->AddMappingContext(MappingContext, Priority);
			bAddedByRover = true;
		}
	};

	AddOwnedMapping(DefaultMappingContext, 0, bAddedDefaultMappingContext);
	AddOwnedMapping(MouseLookMappingContext, 0, bAddedMouseLookMappingContext);
	AddOwnedMapping(RuntimeMappingContext, 1, bAddedRuntimeMappingContext);
}

void ARoverCharacter::RemoveInputMappingContexts()
{
	CancelCameraAutoFollow();
	LastCameraMoveWorldDirection = FVector::ZeroVector;
	CameraAutoFollowSuppressionRemaining = 0.0f;

	if (LocomotionComponent)
	{
		LocomotionComponent->SetMoveInput(FVector2D::ZeroVector, FVector::ZeroVector);
		LocomotionComponent->SetSprintHeld(false);
		LocomotionComponent->SetCrouchHeld(false);
		LocomotionComponent->StopJump();
	}
	if (CombatComponent)
	{
		CombatComponent->SetLightAttackHeld(false);
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = InputSubsystemWithMappings.Get();
	if (InputSubsystem)
	{
		if (bAddedDefaultMappingContext && DefaultMappingContext)
		{
			InputSubsystem->RemoveMappingContext(DefaultMappingContext);
		}
		if (bAddedMouseLookMappingContext && MouseLookMappingContext)
		{
			InputSubsystem->RemoveMappingContext(MouseLookMappingContext);
		}
		if (bAddedRuntimeMappingContext && RuntimeMappingContext)
		{
			InputSubsystem->RemoveMappingContext(RuntimeMappingContext);
		}
	}

	bAddedDefaultMappingContext = false;
	bAddedMouseLookMappingContext = false;
	bAddedRuntimeMappingContext = false;
	InputSubsystemWithMappings.Reset();
}

bool ARoverCharacter::HasActiveInputMappings() const
{
	const UEnhancedInputLocalPlayerSubsystem* InputSubsystem = InputSubsystemWithMappings.Get();
	if (!InputSubsystem)
	{
		return false;
	}

	const auto HasRequiredMapping = [InputSubsystem](const UInputMappingContext* MappingContext)
	{
		return MappingContext && InputSubsystem->HasMappingContext(MappingContext);
	};
	return HasRequiredMapping(DefaultMappingContext) &&
		HasRequiredMapping(MouseLookMappingContext) &&
		HasRequiredMapping(RuntimeMappingContext);
}

void ARoverCharacter::HandleMove(const FInputActionValue& Value)
{
	const FVector2D RawMovementInput = Value.Get<FVector2D>();
	const FRotator ControlYaw(0.0f, Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(ControlYaw).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(ControlYaw).GetUnitAxis(EAxis::Y);
	const FVector WorldDirection = (ForwardDirection * RawMovementInput.Y + RightDirection * RawMovementInput.X).GetSafeNormal();

	LockCameraForMove(RawMovementInput, WorldDirection);
	LocomotionComponent->SetMoveInput(RawMovementInput, WorldDirection);
	if (CombatComponent && !RawMovementInput.IsNearlyZero())
	{
		CombatComponent->RequestRecoveryMovementInterrupt();
	}
	if (!LocomotionComponent->CanAcceptMovementInput())
	{
		return;
	}

	const FVector2D MovementInput = LocomotionComponent->GetMoveInput();
	AddMovementInput(ForwardDirection, MovementInput.Y);
	AddMovementInput(RightDirection, MovementInput.X);
}

void ARoverCharacter::HandleMoveCompleted(const FInputActionValue& Value)
{
	LocomotionComponent->SetMoveInput(FVector2D::ZeroVector, FVector::ZeroVector);
	if (CameraFollowState == ERoverCameraFollowState::MoveLocked)
	{
		QueueCameraRecenter();
	}
}

void ARoverCharacter::HandleLook(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	if (!LookInput.IsNearlyZero() && LocomotionComponent)
	{
		CancelCameraAutoFollow();
		CameraAutoFollowSuppressionRemaining = LocomotionComponent->GetSettings().CameraManualLookHoldTime;
	}
	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

void ARoverCharacter::HandleJumpStarted()
{
	if (CombatComponent)
	{
		CombatComponent->RequestRecoveryMovementInterrupt();
	}
	LocomotionComponent->TryJump();
}

void ARoverCharacter::HandleJumpCompleted()
{
	LocomotionComponent->StopJump();
}

void ARoverCharacter::HandleSprintStarted()
{
	LocomotionComponent->SetSprintHeld(true);
}

void ARoverCharacter::HandleSprintCompleted()
{
	LocomotionComponent->SetSprintHeld(false);
}

void ARoverCharacter::HandleCrouchStarted()
{
	LocomotionComponent->SetCrouchHeld(true);
}

void ARoverCharacter::HandleCrouchCompleted()
{
	LocomotionComponent->SetCrouchHeld(false);
}

void ARoverCharacter::HandleAttackStarted()
{
	if (CombatComponent)
	{
		CombatComponent->SetLightAttackHeld(true);
		CombatComponent->RequestLightAttack();
	}
}

void ARoverCharacter::HandleAttackCompleted()
{
	if (CombatComponent)
	{
		CombatComponent->SetLightAttackHeld(false);
	}
}

void ARoverCharacter::HandleFireballStarted()
{
	if (WorldSkillComponent)
	{
		WorldSkillComponent->RequestFireball();
	}
}

void ARoverCharacter::ConfigureCombatWeapon()
{
	if (!CombatWeapon || !CombatComponent || !GetMesh())
	{
		return;
	}

	const FRoverCombatSettings& Settings = CombatComponent->GetSettings();
	if (USkeletalMesh* WeaponMesh = Settings.WeaponMesh.LoadSynchronous())
	{
		CombatWeapon->SetSkeletalMeshAsset(WeaponMesh);
	}
	SetCombatWeaponHand(ERoverWeaponHand::Right);
	CombatWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CombatWeapon->GetBoneIndex(Settings.ScabbardBone) != INDEX_NONE)
	{
		CombatWeapon->HideBoneByName(Settings.ScabbardBone, EPhysBodyOp::PBO_None);
	}
	SetCombatWeaponVisible(false);
}

void ARoverCharacter::SetCombatWeaponHand(const ERoverWeaponHand WeaponHand)
{
	if (!CombatWeapon || !CombatComponent || !GetMesh())
	{
		return;
	}

	const FRoverCombatSettings& Settings = CombatComponent->GetSettings();
	const bool bUseLeftHand = WeaponHand == ERoverWeaponHand::Left;
	FName AttachmentSocket = bUseLeftHand
		? Settings.LeftHandWeaponSocket
		: Settings.CharacterWeaponSocket;
	FVector RelativeLocation = bUseLeftHand
		? Settings.LeftHandWeaponRelativeLocation
		: Settings.WeaponRelativeLocation;
	FRotator RelativeRotation = bUseLeftHand
		? Settings.LeftHandWeaponRelativeRotation
		: Settings.WeaponRelativeRotation;
	FVector RelativeScale = bUseLeftHand
		? Settings.LeftHandWeaponRelativeScale
		: Settings.WeaponRelativeScale;

	if (AttachmentSocket.IsNone() || !GetMesh()->DoesSocketExist(AttachmentSocket))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Rover weapon attachment %s is unavailable; falling back to %s."),
			*AttachmentSocket.ToString(),
			*Settings.CharacterWeaponSocket.ToString());
		AttachmentSocket = Settings.CharacterWeaponSocket;
		RelativeLocation = Settings.WeaponRelativeLocation;
		RelativeRotation = Settings.WeaponRelativeRotation;
		RelativeScale = Settings.WeaponRelativeScale;
	}

	CombatWeapon->AttachToComponent(
		GetMesh(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		AttachmentSocket);
	CombatWeapon->SetRelativeTransform(FTransform(
		RelativeRotation,
		RelativeLocation,
		RelativeScale));
}

bool ARoverCharacter::TryBeginCombatMovementRestriction(const int32 RequestId)
{
	return LocomotionComponent && LocomotionComponent->TryBeginCombatMovementRestriction(RequestId);
}

bool ARoverCharacter::TransferCombatMovementRestriction(const int32 PreviousRequestId, const int32 NewRequestId)
{
	return LocomotionComponent &&
		LocomotionComponent->TransferCombatMovementRestriction(PreviousRequestId, NewRequestId);
}

void ARoverCharacter::EndCombatMovementRestriction(const int32 RequestId)
{
	if (LocomotionComponent)
	{
		LocomotionComponent->EndCombatMovementRestriction(RequestId);
	}
}

bool ARoverCharacter::StartCombatAttackAdvance(
	const int32 RequestId,
	const float Distance,
	const float Duration)
{
	return LocomotionComponent &&
		LocomotionComponent->StartCombatAttackAdvance(RequestId, Distance, Duration);
}

void ARoverCharacter::CancelCombatAttackAdvance(const int32 RequestId)
{
	if (LocomotionComponent)
	{
		LocomotionComponent->CancelCombatAttackAdvance(RequestId);
	}
}

void ARoverCharacter::PlayCombatAttackImmediately(const int32 RequestId)
{
	if (URoverAnimInstance* AnimInstance = GetMesh() ? Cast<URoverAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
	{
		AnimInstance->PlayAttackRequestImmediately(RequestId);
	}
}

void ARoverCharacter::StopCombatAttack(const int32 RequestId, const float BlendOutTime)
{
	if (URoverAnimInstance* AnimInstance = GetMesh() ? Cast<URoverAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
	{
		AnimInstance->StopAttackRequest(RequestId, BlendOutTime);
	}
}

void ARoverCharacter::SetCombatWeaponVisible(const bool bVisible)
{
	if (!CombatWeapon)
	{
		return;
	}
	const bool bCanShow = bVisible && CombatWeapon->GetSkeletalMeshAsset() != nullptr;
	CombatWeapon->SetVisibility(bCanShow, true);
	CombatWeapon->SetHiddenInGame(!bCanShow, true);
}

bool ARoverCharacter::IsCombatWeaponVisible() const
{
	return CombatWeapon && CombatWeapon->GetSkeletalMeshAsset() &&
		CombatWeapon->IsVisible() && !CombatWeapon->bHiddenInGame;
}

float ARoverCharacter::GetCombatWeaponWorldLength() const
{
	if (!CombatWeapon || !CombatWeapon->GetSkeletalMeshAsset())
	{
		return 0.0f;
	}

	FBoxSphereBounds LocalBounds;
	CombatWeapon->GetPreSkinnedLocalBounds(LocalBounds);
	const FVector ScaledExtent = LocalBounds.BoxExtent * CombatWeapon->GetComponentScale().GetAbs();
	return 2.0f * ScaledExtent.GetMax();
}

bool ARoverCharacter::GetWeaponTraceLocations(FVector& OutBase, FVector& OutTip) const
{
	if (!CombatWeapon || !CombatComponent || !CombatWeapon->GetSkeletalMeshAsset())
	{
		return false;
	}
	const FRoverCombatSettings& Settings = CombatComponent->GetSettings();
	if (!CombatWeapon->DoesSocketExist(Settings.WeaponTraceBaseSocket) ||
		!CombatWeapon->DoesSocketExist(Settings.WeaponTraceTipSocket))
	{
		return false;
	}
	OutBase = CombatWeapon->GetSocketLocation(Settings.WeaponTraceBaseSocket);
	OutTip = CombatWeapon->GetSocketLocation(Settings.WeaponTraceTipSocket);
	return !OutBase.ContainsNaN() && !OutTip.ContainsNaN();
}

void ARoverCharacter::ReceiveCombatHit(const FRoverCombatHit& Hit)
{
	if (!HealthComponent || !CombatComponent)
	{
		return;
	}
	const float AppliedDamage = HealthComponent->ApplyCombatDamage(Hit.Damage);
	if (AppliedDamage <= 0.0f)
	{
		return;
	}
	if (HealthComponent->IsDead())
	{
		CombatComponent->HandleDeath();
		return;
	}
	CombatComponent->HandleReceivedHit(Hit);
}

void ARoverCharacter::HandleAttackStartedNotify(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->AcknowledgeAttackStarted(RequestId);
}

void ARoverCharacter::HandleAttackActiveBeginNotify(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->BeginAttackActive(RequestId);
}

void ARoverCharacter::HandleAttackActiveEndNotify(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->EndAttackActive(RequestId);
}

void ARoverCharacter::HandleComboWindowBeginNotify(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->BeginComboWindow(RequestId);
}

void ARoverCharacter::HandleComboWindowEndNotify(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->EndComboWindow(RequestId);
}

void ARoverCharacter::HandleAttackRecoveryBeginNotify(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->BeginAttackRecovery(RequestId);
}

void ARoverCharacter::HandleAttackFinishedNotify(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->FinishAttack(RequestId);
}

void ARoverCharacter::HandleAttackMontageEnded(const int32 RequestId, const bool bInterrupted)
{
	if (CombatComponent) CombatComponent->HandleAttackMontageEnded(RequestId, bInterrupted);
}

void ARoverCharacter::HandleAttackAnimationRejected(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->RejectAttackAnimation(RequestId);
}

void ARoverCharacter::HandleHitReactionStartedNotify(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->AcknowledgeHitReactionStarted(RequestId);
}

void ARoverCharacter::HandleHitReactionFinishedNotify(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->FinishHitReaction(RequestId);
}

void ARoverCharacter::HandleHitReactionMontageEnded(const int32 RequestId, const bool bInterrupted)
{
	if (CombatComponent) CombatComponent->HandleHitReactionMontageEnded(RequestId, bInterrupted);
}

void ARoverCharacter::HandleHitReactionAnimationRejected(const int32 RequestId)
{
	if (CombatComponent) CombatComponent->RejectHitReactionAnimation(RequestId);
}

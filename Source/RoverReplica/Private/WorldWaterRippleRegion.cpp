#include "WorldWaterRippleRegion.h"

#include "BasicShallowWaterSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WorldInteractionSubsystem.h"

namespace WorldWaterRippleParameters
{
	const FName StateTexture(TEXT("RippleStateTexture"));
	const FName Domain(TEXT("RippleDomain"));
	const FName DomainOrigin(TEXT("RippleDomainOrigin"));
	const FName DomainSize(TEXT("RippleDomainSize"));
	const FName Resolution(TEXT("RippleResolution"));
	const FName Simulation(TEXT("RippleSimulationParameters"));
	const FName FixedDeltaTime(TEXT("RippleFixedDeltaTime"));
	const FName ImpulseCount(TEXT("RippleImpulseCount"));
	const FName ShallowWaterColor(TEXT("ShallowWaterColor"));
	const FName DeepWaterColor(TEXT("DeepWaterColor"));
	const FName DepthColorDistance(TEXT("DepthColorDistance"));
	const FName ScatteringCoefficients(TEXT("WaterScatteringCoefficients"));
	const FName AbsorptionCoefficients(TEXT("WaterAbsorptionCoefficients"));
	const FName PhaseG(TEXT("WaterPhaseG"));
	const FName ColorScaleBehindWater(TEXT("ColorScaleBehindWater"));
	const FName SurfaceRoughness(TEXT("SurfaceRoughness"));
	const FName SurfaceOpacity(TEXT("SurfaceOpacity"));
	const FName RippleNormalStrength(TEXT("RippleNormalStrength"));
	const FName WpoAmplitude(TEXT("WpoAmplitude"));
	const FName WpoSpatialFrequency(TEXT("WpoSpatialFrequency"));
	const FName WpoSpeed(TEXT("WpoSpeed"));
	const FName FoamWidth(TEXT("FoamWidth"));
	const FName FoamIntensity(TEXT("FoamIntensity"));
}

AWorldWaterRippleRegion::AWorldWaterRippleRegion()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	RegionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RegionBounds"));
	RegionBounds->SetupAttachment(SceneRoot);
	RegionBounds->SetBoxExtent(FVector(1000.0f, 1000.0f, 500.0f));
	RegionBounds->SetCollisionObjectType(ECC_WorldDynamic);
	RegionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RegionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	RegionBounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	RegionBounds->SetGenerateOverlapEvents(true);
	RegionBounds->SetHiddenInGame(true);

	WaterSurfaceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterSurfaceMesh"));
	WaterSurfaceMesh->SetupAttachment(SceneRoot);
	WaterSurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterSurfaceMesh->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		WaterSurfaceMesh->SetStaticMesh(PlaneMesh.Object);
	}

	RegionBounds->OnComponentBeginOverlap.AddDynamic(
		this,
		&AWorldWaterRippleRegion::HandleRegionBeginOverlap);
	RegionBounds->OnComponentEndOverlap.AddDynamic(
		this,
		&AWorldWaterRippleRegion::HandleRegionEndOverlap);
	Tags.AddUnique(TEXT("PhysicsWorldWaterRippleRegion"));
}

void AWorldWaterRippleRegion::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigureRegionComponents();
}

void AWorldWaterRippleRegion::BeginPlay()
{
	Super::BeginPlay();
	if (!WaterRippleConfig)
	{
		WaterRippleConfig = LoadObject<UWorldWaterRippleConfig>(
			nullptr,
			TEXT("/Game/PhysicsWorldDemo/Water/Config/DA_WorldWaterRippleConfig.DA_WorldWaterRippleConfig"));
	}

	ConfigureRegionComponents();
	ResetDebugStats();
	if (bUseWaterAdvancedShallowWater)
	{
		if (UBasicShallowWaterSubsystem* ShallowWater =
			ResolveAdvancedShallowWaterSubsystem())
		{
			ShallowWater->SetWaterBodyMIDParameters(TargetWaterBody);
		}
	}
	else
	{
		InitializeRenderTargetsAndMaterials();
	}

	if (UWorldInteractionSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UWorldInteractionSubsystem>() : nullptr)
	{
		Subsystem->OnLightweightInteractionPublished.AddDynamic(
			this,
			&AWorldWaterRippleRegion::HandleLightweightInteractionField);
		Subsystem->OnInteractionProcessed.AddDynamic(
			this,
			&AWorldWaterRippleRegion::HandleInteractionProcessed);
	}

	if (!bUseWaterAdvancedShallowWater)
	{
		TArray<AActor*> OverlappingActors;
		RegionBounds->GetOverlappingActors(OverlappingActors, ACharacter::StaticClass());
		for (AActor* Actor : OverlappingActors)
		{
			InitializeWaterCrossingState(Actor, false);
		}
	}
	SetActorTickEnabled(GetSettings().bEnabled && !bUseWaterAdvancedShallowWater);
}

void AWorldWaterRippleRegion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldInteractionSubsystem* Subsystem =
			World->GetSubsystem<UWorldInteractionSubsystem>())
		{
			Subsystem->OnLightweightInteractionPublished.RemoveDynamic(
				this,
				&AWorldWaterRippleRegion::HandleLightweightInteractionField);
			Subsystem->OnInteractionProcessed.RemoveDynamic(
				this,
				&AWorldWaterRippleRegion::HandleInteractionProcessed);
		}
	}

	PendingImpulses.Reset();
	WaterCrossingStates.Reset();
	SourceRateStates.Reset();
	RecentHeavyRequestIds.Reset();
	ReleaseRenderTargets();
	Super::EndPlay(EndPlayReason);
}

void AWorldWaterRippleRegion::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetSettings().bEnabled)
	{
		return;
	}

	UpdateWaterlineCrossings();
	if (!IsSimulationReady() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float FixedStep = FMath::Clamp(GetSettings().FixedStepSeconds, 1.0f / 240.0f, 0.05f);
	const int32 MaxSubsteps = FMath::Clamp(GetSettings().MaxSubstepsPerFrame, 1, 8);
	FixedStepAccumulator = FMath::Min(
		FixedStepAccumulator + DeltaSeconds,
		FixedStep * static_cast<float>(MaxSubsteps + 1));

	int32 SubstepCount = 0;
	while (FixedStepAccumulator >= FixedStep && SubstepCount < MaxSubsteps)
	{
		RunSimulationStep();
		FixedStepAccumulator -= FixedStep;
		++SubstepCount;
	}
	if (FixedStepAccumulator >= FixedStep)
	{
		FixedStepAccumulator = FMath::Fmod(FixedStepAccumulator, FixedStep);
	}
}

const FWorldWaterRippleSettings& AWorldWaterRippleRegion::GetSettings() const
{
	return WaterRippleConfig ? WaterRippleConfig->Settings : FallbackSettings;
}

void AWorldWaterRippleRegion::ConfigureRegionComponents()
{
	const FWorldWaterRippleSettings& Settings = GetSettings();
	const FVector2D WorldSize = GetResolvedDomainWorldSize();
	const FVector DomainCenter = GetResolvedDomainCenter();
	TargetWaterSurfaceMesh = ResolveTargetWaterSurfaceMesh();
	if (RegionBounds)
	{
		RegionBounds->SetWorldLocation(DomainCenter);
		RegionBounds->SetBoxExtent(FVector(
			WorldSize.X * 0.5f,
			WorldSize.Y * 0.5f,
			FMath::Max(100.0f, Settings.RegionVerticalExtent)));
	}
	if (WaterSurfaceMesh)
	{
		const bool bUsesExternalSurface = ResolveTargetWaterBodyComponent() != nullptr ||
			TargetWaterSurfaceMesh != nullptr;
		WaterSurfaceMesh->SetVisibility(!bUsesExternalSurface, true);
		if (!bUsesExternalSurface)
		{
			WaterSurfaceMesh->SetWorldLocation(DomainCenter);
			WaterSurfaceMesh->SetWorldScale3D(FVector(
				WorldSize.X / 100.0f,
				WorldSize.Y / 100.0f,
				1.0f));
		}
	}
}

void AWorldWaterRippleRegion::InitializeRenderTargetsAndMaterials()
{
	ReleaseRenderTargets();
	if (bUseWaterAdvancedShallowWater || !GetSettings().bEnabled || !GetWorld())
	{
		return;
	}

	const int32 Resolution = FMath::Clamp(GetSettings().RenderTargetResolution, 64, 2048);
	StateRenderTargets.SetNum(2);
	for (TObjectPtr<UTextureRenderTarget2D>& RenderTarget : StateRenderTargets)
	{
		RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
			this,
			Resolution,
			Resolution,
			RTF_RG16f,
			FLinearColor::Transparent,
			false,
			false);
	}

	if (UMaterialInterface* SimulationMaterial =
		GetSettings().SimulationMaterial.LoadSynchronous())
	{
		SimulationMaterialInstance = UMaterialInstanceDynamic::Create(
			SimulationMaterial,
			this);
	}
	UMaterialInterface* WaterMaterial =
		GetSettings().WaterSurfaceMaterial.LoadSynchronous();
	if (UWaterBodyComponent* WaterBodyComponent = ResolveTargetWaterBodyComponent())
	{
		if (WaterMaterial)
		{
			WaterBodyComponent->SetWaterMaterial(WaterMaterial);
		}
		WaterSurfaceMaterialInstance = WaterBodyComponent->GetWaterMaterialInstance();
	}
	else if (WaterMaterial)
	{
		TargetWaterSurfaceMesh = ResolveTargetWaterSurfaceMesh();
		UStaticMeshComponent* PresentationMesh = TargetWaterSurfaceMesh
			? TargetWaterSurfaceMesh.Get()
			: WaterSurfaceMesh.Get();
		if (PresentationMesh)
		{
			WaterSurfaceMaterialInstance = PresentationMesh->CreateDynamicMaterialInstance(
				0,
				WaterMaterial);
			PresentationMesh->SetVisibility(true, true);
		}
		if (WaterSurfaceMesh)
		{
			WaterSurfaceMesh->SetVisibility(
				ResolveTargetWaterBodyComponent() == nullptr &&
				TargetWaterSurfaceMesh == nullptr,
				true);
		}
	}

	CurrentStateIndex = 0;
	FixedStepAccumulator = 0.0f;
	ClearRippleSimulation();
	UpdateWaterSurfaceMaterial();
}

void AWorldWaterRippleRegion::ReleaseRenderTargets()
{
	for (UTextureRenderTarget2D* RenderTarget : StateRenderTargets)
	{
		if (RenderTarget)
		{
			UKismetRenderingLibrary::ReleaseRenderTarget2D(RenderTarget);
		}
	}
	StateRenderTargets.Reset();
	SimulationMaterialInstance = nullptr;
	WaterSurfaceMaterialInstance = nullptr;
	TargetWaterSurfaceMesh = nullptr;
}

bool AWorldWaterRippleRegion::IsSimulationReady() const
{
	if (bUseWaterAdvancedShallowWater)
	{
		const UBasicShallowWaterSubsystem* ShallowWater =
			ResolveAdvancedShallowWaterSubsystem();
		return ShallowWater && ShallowWater->IsShallowWaterInitialized();
	}
	return SimulationMaterialInstance &&
		StateRenderTargets.Num() == 2 &&
		StateRenderTargets[0] &&
		StateRenderTargets[1];
}

UTextureRenderTarget2D* AWorldWaterRippleRegion::GetCurrentStateRenderTarget() const
{
	return StateRenderTargets.IsValidIndex(CurrentStateIndex)
		? StateRenderTargets[CurrentStateIndex]
		: nullptr;
}

UTextureRenderTarget2D* AWorldWaterRippleRegion::GetPreviousStateRenderTarget() const
{
	const int32 PreviousIndex = 1 - CurrentStateIndex;
	return StateRenderTargets.IsValidIndex(PreviousIndex)
		? StateRenderTargets[PreviousIndex]
		: nullptr;
}

int32 AWorldWaterRippleRegion::GetRenderTargetResolution() const
{
	const UTextureRenderTarget2D* RenderTarget = GetCurrentStateRenderTarget();
	return RenderTarget ? RenderTarget->SizeX : 0;
}

float AWorldWaterRippleRegion::GetAdvancedGridSize() const
{
	const UBasicShallowWaterSubsystem* ShallowWater =
		ResolveAdvancedShallowWaterSubsystem();
	return ShallowWater ? ShallowWater->GetGridSize() : 0.0f;
}

int32 AWorldWaterRippleRegion::GetAdvancedGridResolution() const
{
	const UBasicShallowWaterSubsystem* ShallowWater =
		ResolveAdvancedShallowWaterSubsystem();
	return ShallowWater ? ShallowWater->GetGridResolution() : 0;
}

float AWorldWaterRippleRegion::GetWaterSurfaceZ() const
{
	if (const UWaterBodyComponent* WaterBodyComponent = ResolveTargetWaterBodyComponent())
	{
		FVector SurfaceLocation = FVector::ZeroVector;
		FVector SurfaceNormal = FVector::UpVector;
		FVector SurfaceVelocity = FVector::ZeroVector;
		float WaterDepth = 0.0f;
		if (WaterBodyComponent->GetWaterSurfaceInfoAtLocation(
			GetActorLocation(),
			SurfaceLocation,
			SurfaceNormal,
			SurfaceVelocity,
			WaterDepth,
			false))
		{
			return SurfaceLocation.Z + GetSettings().WaterSurfaceZOffset;
		}
		return WaterBodyComponent->GetComponentLocation().Z +
			GetSettings().WaterSurfaceZOffset;
	}
	if (const UStaticMeshComponent* SurfaceMesh = ResolveTargetWaterSurfaceMesh())
	{
		return SurfaceMesh->GetComponentLocation().Z + GetSettings().WaterSurfaceZOffset;
	}
	return GetActorLocation().Z + GetSettings().WaterSurfaceZOffset;
}

FVector AWorldWaterRippleRegion::GetResolvedDomainCenter() const
{
	if (const UWaterBodyComponent* WaterBodyComponent = ResolveTargetWaterBodyComponent())
	{
		const FVector BoundsOrigin = WaterBodyComponent->Bounds.Origin;
		return FVector(BoundsOrigin.X, BoundsOrigin.Y, GetWaterSurfaceZ());
	}
	if (const UStaticMeshComponent* SurfaceMesh = ResolveTargetWaterSurfaceMesh())
	{
		const FVector BoundsOrigin = SurfaceMesh->Bounds.Origin;
		return FVector(BoundsOrigin.X, BoundsOrigin.Y, GetWaterSurfaceZ());
	}
	const FVector ActorLocation = GetActorLocation();
	return FVector(ActorLocation.X, ActorLocation.Y, GetWaterSurfaceZ());
}

FVector2D AWorldWaterRippleRegion::GetResolvedDomainWorldSize() const
{
	if (bFitDomainToTargetSurfaceBounds)
	{
		if (const UWaterBodyComponent* WaterBodyComponent = ResolveTargetWaterBodyComponent())
		{
			return FVector2D(
				FMath::Max(256.0f, WaterBodyComponent->Bounds.BoxExtent.X * 2.0f),
				FMath::Max(256.0f, WaterBodyComponent->Bounds.BoxExtent.Y * 2.0f));
		}
		if (const UStaticMeshComponent* SurfaceMesh = ResolveTargetWaterSurfaceMesh())
		{
			return FVector2D(
				FMath::Max(256.0f, SurfaceMesh->Bounds.BoxExtent.X * 2.0f),
				FMath::Max(256.0f, SurfaceMesh->Bounds.BoxExtent.Y * 2.0f));
		}
	}
	return FVector2D(
		FMath::Max(256.0f, GetSettings().WorldSize.X),
		FMath::Max(256.0f, GetSettings().WorldSize.Y));
}

UWaterBodyComponent* AWorldWaterRippleRegion::ResolveTargetWaterBodyComponent() const
{
	return IsValid(TargetWaterBody)
		? TargetWaterBody->GetWaterBodyComponent()
		: nullptr;
}

UBasicShallowWaterSubsystem* AWorldWaterRippleRegion::ResolveAdvancedShallowWaterSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UBasicShallowWaterSubsystem>() : nullptr;
}

bool AWorldWaterRippleRegion::IsAdvancedImpactLocationOnTargetWaterBody(
	const FVector WorldLocation) const
{
	if (!GetWorld() || !IsValid(TargetWaterBody))
	{
		return false;
	}

	FHitResult WaterHit;
	GetWorld()->LineTraceSingleByChannel(
		WaterHit,
		WorldLocation + FVector(0.0f, 0.0f, 10.0f),
		WorldLocation - FVector(0.0f, 0.0f, 10.0f),
		ECC_WorldDynamic);
	return WaterHit.GetActor() == TargetWaterBody;
}

FString AWorldWaterRippleRegion::GetAdvancedTargetCollisionDebugString() const
{
	const UWaterBodyComponent* WaterBodyComponent = ResolveTargetWaterBodyComponent();
	if (!WaterBodyComponent)
	{
		return TEXT("WaterBodyComponent=null");
	}

	FString Result = FString::Printf(
		TEXT("body_enabled=%d body_profile=%s body_world_dynamic=%d body_overlap=%d"),
		static_cast<int32>(WaterBodyComponent->GetCollisionEnabled()),
		*WaterBodyComponent->GetCollisionProfileName().ToString(),
		static_cast<int32>(WaterBodyComponent->GetCollisionResponseToChannel(ECC_WorldDynamic)),
		WaterBodyComponent->GetGenerateOverlapEvents() ? 1 : 0);
	const TArray<UPrimitiveComponent*> CollisionComponents =
		WaterBodyComponent->GetCollisionComponents(false);
	Result += FString::Printf(TEXT(" components=%d"), CollisionComponents.Num());
	for (const UPrimitiveComponent* Component : CollisionComponents)
	{
		if (!Component)
		{
			continue;
		}
		Result += FString::Printf(
			TEXT(" [%s enabled=%d profile=%s world_dynamic=%d overlap=%d]"),
			*Component->GetName(),
			static_cast<int32>(Component->GetCollisionEnabled()),
			*Component->GetCollisionProfileName().ToString(),
			static_cast<int32>(Component->GetCollisionResponseToChannel(ECC_WorldDynamic)),
			Component->GetGenerateOverlapEvents() ? 1 : 0);
	}
	return Result;
}

UStaticMeshComponent* AWorldWaterRippleRegion::ResolveTargetWaterSurfaceMesh() const
{
	return !IsValid(TargetWaterBody) && IsValid(TargetWaterSurfaceActor)
		? TargetWaterSurfaceActor->FindComponentByClass<UStaticMeshComponent>()
		: nullptr;
}

FLinearColor AWorldWaterRippleRegion::GetDomainMaterialParameter() const
{
	const FVector2D WorldSize = GetResolvedDomainWorldSize();
	const FVector Origin = GetResolvedDomainCenter() - FVector(
		WorldSize.X * 0.5f,
		WorldSize.Y * 0.5f,
		0.0f);
	return FLinearColor(Origin.X, Origin.Y, WorldSize.X, WorldSize.Y);
}

void AWorldWaterRippleRegion::RunSimulationStep()
{
	if (!IsSimulationReady())
	{
		return;
	}

	UTextureRenderTarget2D* Source = GetCurrentStateRenderTarget();
	const int32 DestinationIndex = 1 - CurrentStateIndex;
	UTextureRenderTarget2D* Destination = StateRenderTargets[DestinationIndex];
	if (!Source || !Destination)
	{
		return;
	}

	const FWorldWaterRippleSettings& Settings = GetSettings();
	const float FixedStep = FMath::Clamp(Settings.FixedStepSeconds, 1.0f / 240.0f, 0.05f);
	const FVector2D DomainWorldSize = GetResolvedDomainWorldSize();
	const float SizeX = DomainWorldSize.X;
	const float SizeY = DomainWorldSize.Y;
	const float Resolution = static_cast<float>(FMath::Max(64, Settings.RenderTargetResolution));
	float CoefficientX = FMath::Square(Settings.WaveSpeed * FixedStep / (SizeX / Resolution));
	float CoefficientY = FMath::Square(Settings.WaveSpeed * FixedStep / (SizeY / Resolution));
	const float CoefficientSum = CoefficientX + CoefficientY;
	if (CoefficientSum > 0.99f)
	{
		const float StableScale = 0.99f / CoefficientSum;
		CoefficientX *= StableScale;
		CoefficientY *= StableScale;
	}
	const float StepDamping = FMath::Exp(-FMath::Max(0.0f, Settings.DampingPerSecond) * FixedStep);

	SimulationMaterialInstance->SetTextureParameterValue(
		WorldWaterRippleParameters::StateTexture,
		Source);
	const FLinearColor Domain = GetDomainMaterialParameter();
	SimulationMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::Domain,
		Domain);
	SimulationMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::DomainOrigin,
		FLinearColor(Domain.R, Domain.G, 0.0f, 0.0f));
	SimulationMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::DomainSize,
		FLinearColor(Domain.B, Domain.A, 0.0f, 0.0f));
	SimulationMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::Resolution,
		Resolution);
	SimulationMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::Simulation,
		FLinearColor(
			CoefficientX,
			CoefficientY,
			StepDamping,
			FMath::Clamp(Settings.EdgeDampingWidth, 0.0f, 0.49f)));
	SimulationMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::FixedDeltaTime,
		FixedStep);

	const int32 ImpulseCount = FMath::Min3(
		PendingImpulses.Num(),
		FMath::Clamp(Settings.MaxImpulsesPerStep, 1, MaxShaderImpulseSlots),
		MaxShaderImpulseSlots);
	SimulationMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::ImpulseCount,
		static_cast<float>(ImpulseCount));
	for (int32 Index = 0; Index < MaxShaderImpulseSlots; ++Index)
	{
		const FName ParameterName(*FString::Printf(TEXT("RippleImpulse%d"), Index));
		const FLinearColor Value = Index < ImpulseCount
			? FLinearColor(
				PendingImpulses[Index].WorldLocation.X,
				PendingImpulses[Index].WorldLocation.Y,
				PendingImpulses[Index].Radius,
				PendingImpulses[Index].Strength)
			: FLinearColor::Transparent;
		SimulationMaterialInstance->SetVectorParameterValue(ParameterName, Value);
		const FName StrengthParameterName(
			*FString::Printf(TEXT("RippleImpulseStrength%d"), Index));
		SimulationMaterialInstance->SetScalarParameterValue(
			StrengthParameterName,
			Index < ImpulseCount ? PendingImpulses[Index].Strength : 0.0f);
	}

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(
		this,
		Destination,
		SimulationMaterialInstance);
	if (ImpulseCount > 0)
	{
		PendingImpulses.RemoveAt(0, ImpulseCount, EAllowShrinking::No);
	}
	CurrentStateIndex = DestinationIndex;
	++SimulationStepCount;
	++BufferSwapCount;
	UpdateWaterSurfaceMaterial();
}

void AWorldWaterRippleRegion::UpdateWaterSurfaceMaterial()
{
	if (!WaterSurfaceMaterialInstance)
	{
		return;
	}
	WaterSurfaceMaterialInstance->SetTextureParameterValue(
		WorldWaterRippleParameters::StateTexture,
		GetCurrentStateRenderTarget());
	const FLinearColor Domain = GetDomainMaterialParameter();
	WaterSurfaceMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::Domain,
		Domain);
	WaterSurfaceMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::DomainOrigin,
		FLinearColor(Domain.R, Domain.G, 0.0f, 0.0f));
	WaterSurfaceMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::DomainSize,
		FLinearColor(Domain.B, Domain.A, 0.0f, 0.0f));
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::Resolution,
		static_cast<float>(GetRenderTargetResolution()));

	const FWorldWaterRippleSettings& Settings = GetSettings();
	WaterSurfaceMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::ShallowWaterColor,
		Settings.ShallowWaterColor);
	WaterSurfaceMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::DeepWaterColor,
		Settings.DeepWaterColor);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::DepthColorDistance,
		Settings.DepthColorDistance);
	WaterSurfaceMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::ScatteringCoefficients,
		Settings.ScatteringCoefficients);
	WaterSurfaceMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::AbsorptionCoefficients,
		Settings.AbsorptionCoefficients);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::PhaseG,
		Settings.PhaseG);
	WaterSurfaceMaterialInstance->SetVectorParameterValue(
		WorldWaterRippleParameters::ColorScaleBehindWater,
		Settings.ColorScaleBehindWater);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::SurfaceRoughness,
		Settings.SurfaceRoughness);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::SurfaceOpacity,
		Settings.SurfaceOpacity);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::RippleNormalStrength,
		Settings.RippleNormalStrength);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::WpoAmplitude,
		Settings.WpoAmplitude);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::WpoSpatialFrequency,
		Settings.WpoSpatialFrequency);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::WpoSpeed,
		Settings.WpoSpeed);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::FoamWidth,
		Settings.FoamWidth);
	WaterSurfaceMaterialInstance->SetScalarParameterValue(
		WorldWaterRippleParameters::FoamIntensity,
		Settings.FoamIntensity);
}

void AWorldWaterRippleRegion::SpawnWaterEntrySplash(
	const FVector& WorldLocation,
	const float Strength)
{
	UNiagaraSystem* Effect = GetSettings().WaterEntrySplashEffect.LoadSynchronous();
	if (!Effect || !GetWorld())
	{
		return;
	}

	const float Scale = FMath::Clamp(
		GetSettings().WaterEntrySplashBaseScale +
			Strength * GetSettings().WaterEntrySplashStrengthScale,
		0.01f,
		FMath::Max(0.01f, GetSettings().WaterEntrySplashMaximumScale));
	if (UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		Effect,
		WorldLocation,
		FRotator::ZeroRotator,
		FVector::OneVector * Scale))
	{
		++SpawnedWaterEntrySplashCount;
	}
}

void AWorldWaterRippleRegion::ClearRippleSimulation()
{
	for (UTextureRenderTarget2D* RenderTarget : StateRenderTargets)
	{
		if (RenderTarget)
		{
			UKismetRenderingLibrary::ClearRenderTarget2D(
				this,
				RenderTarget,
				FLinearColor::Transparent);
		}
	}
	PendingImpulses.Reset();
	CurrentStateIndex = 0;
	FixedStepAccumulator = 0.0f;
	UpdateWaterSurfaceMaterial();
}

void AWorldWaterRippleRegion::ResetDebugStats()
{
	SimulationStepCount = 0;
	BufferSwapCount = 0;
	AcceptedImpulseCount = 0;
	DroppedImpulseCount = 0;
	HandledLightweightFieldCount = 0;
	HandledHeavyInteractionCount = 0;
	MovementImpulseCount = 0;
	JumpImpulseCount = 0;
	LandingImpulseCount = 0;
	AttackImpulseCount = 0;
	ExplosionImpulseCount = 0;
	WaterEntryImpulseCount = 0;
	SpawnedWaterEntrySplashCount = 0;
	SuppressedDuplicateImpulseCount = 0;
	ForwardedAdvancedImpactCount = 0;
	LastImpulseWorldLocation = FVector::ZeroVector;
	LastImpulseStrength = 0.0f;
}

bool AWorldWaterRippleRegion::QueueRippleImpulse(
	FVector WorldLocation,
	const float Radius,
	const float Strength,
	const EWorldWaterRippleImpulseSource SourceType,
	AActor* SourceActor)
{
	const FWorldWaterRippleSettings& Settings = GetSettings();
	if (!Settings.bEnabled || WorldLocation.ContainsNaN() ||
		!FMath::IsFinite(Radius) || !FMath::IsFinite(Strength) ||
		Radius <= 0.0f || Strength <= 0.0f)
	{
		return false;
	}

	FVector SurfacePoint;
	if (!ProjectPointIntoDomain(WorldLocation, Radius, SurfacePoint))
	{
		return false;
	}
	if (bUseWaterAdvancedShallowWater)
	{
		FVector ImpactVelocity = SourceActor
			? SourceActor->GetVelocity()
			: FVector::UpVector * Settings.AdvancedMinimumImpactSpeed;
		if (SourceType == EWorldWaterRippleImpulseSource::Explosion)
		{
			ImpactVelocity = FVector::UpVector * Settings.AdvancedExplosionImpactSpeed;
		}
		return ForwardAdvancedImpact(
			SurfacePoint,
			ImpactVelocity,
			Radius,
			SourceType,
			SourceActor);
	}
	if (PendingImpulses.Num() >= FMath::Max(1, Settings.MaxQueuedImpulses))
	{
		++DroppedImpulseCount;
		return false;
	}

	FWorldWaterRippleQueuedImpulse& Impulse = PendingImpulses.AddDefaulted_GetRef();
	Impulse.WorldLocation = SurfacePoint;
	Impulse.Radius = FMath::Clamp(
		Radius,
		FMath::Max(1.0f, Settings.MinimumImpulseRadius),
		FMath::Max(Settings.MinimumImpulseRadius, Settings.MaximumImpulseRadius));
	Impulse.Strength = FMath::Clamp(
		Strength,
		FMath::Max(0.0f, Settings.MinimumImpulseStrength),
		FMath::Max(Settings.MinimumImpulseStrength, Settings.MaximumImpulseStrength));
	Impulse.SourceType = SourceType;
	Impulse.SourceActor = SourceActor;
	WorldLocation = SurfacePoint;

	++AcceptedImpulseCount;
	LastImpulseWorldLocation = WorldLocation;
	LastImpulseStrength = Impulse.Strength;
	switch (SourceType)
	{
	case EWorldWaterRippleImpulseSource::Movement:
		++MovementImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::Jump:
		++JumpImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::Landing:
		++LandingImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::Attack:
		++AttackImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::Explosion:
		++ExplosionImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::WaterEntry:
		++WaterEntryImpulseCount;
		SpawnWaterEntrySplash(Impulse.WorldLocation, Impulse.Strength);
		break;
	default:
		break;
	}
	return true;
}

FVector AWorldWaterRippleRegion::ResolveAdvancedImpactVelocity(
	const FWorldLightweightInteractionField& Field,
	const EWorldWaterRippleImpulseSource SourceType) const
{
	const FWorldWaterRippleSettings& Settings = GetSettings();
	FVector Velocity = Field.SourceVelocity;
	switch (SourceType)
	{
	case EWorldWaterRippleImpulseSource::Movement:
		Velocity *= Settings.AdvancedMovementVelocityScale;
		break;
	case EWorldWaterRippleImpulseSource::Jump:
		Velocity *= Settings.AdvancedJumpVelocityScale;
		break;
	case EWorldWaterRippleImpulseSource::Landing:
		Velocity *= Settings.AdvancedLandingVelocityScale;
		break;
	case EWorldWaterRippleImpulseSource::Attack:
		Velocity *= Settings.AdvancedAttackVelocityScale;
		break;
	case EWorldWaterRippleImpulseSource::Explosion:
	{
		const FVector Direction = Field.Direction.IsNearlyZero()
			? FVector::UpVector
			: Field.Direction.GetSafeNormal();
		Velocity = Direction * Settings.AdvancedExplosionImpactSpeed +
			FVector::UpVector * FMath::Max(0.0f, Field.UpwardLift);
		break;
	}
	case EWorldWaterRippleImpulseSource::WaterEntry:
		break;
	default:
		break;
	}

	if (Velocity.IsNearlyZero())
	{
		const FVector Direction = Field.Direction.IsNearlyZero()
			? FVector::UpVector
			: Field.Direction.GetSafeNormal();
		Velocity = Direction * Settings.AdvancedMinimumImpactSpeed;
	}
	return Velocity;
}

bool AWorldWaterRippleRegion::ForwardAdvancedImpact(
	const FVector& SurfacePoint,
	const FVector& ImpactVelocity,
	const float Radius,
	const EWorldWaterRippleImpulseSource SourceType,
	AActor* SourceActor)
{
	UBasicShallowWaterSubsystem* ShallowWater =
		ResolveAdvancedShallowWaterSubsystem();
	if (!ShallowWater || !ShallowWater->IsShallowWaterInitialized() ||
		!IsValid(TargetWaterBody))
	{
		++DroppedImpulseCount;
		return false;
	}
	if (!IsAdvancedImpactLocationOnTargetWaterBody(SurfacePoint))
	{
		return false;
	}

	const FWorldWaterRippleSettings& Settings = GetSettings();
	const float ClampedRadius = FMath::Clamp(
		Radius,
		FMath::Max(1.0f, Settings.MinimumImpulseRadius),
		FMath::Max(Settings.MinimumImpulseRadius, Settings.MaximumImpulseRadius));
	FVector ClampedVelocity = ImpactVelocity;
	const float Speed = ClampedVelocity.Size();
	const float MinimumSpeed = FMath::Max(0.0f, Settings.AdvancedMinimumImpactSpeed);
	const float MaximumSpeed = FMath::Max(MinimumSpeed, Settings.AdvancedMaximumImpactSpeed);
	if (Speed <= UE_SMALL_NUMBER)
	{
		ClampedVelocity = FVector::UpVector * MinimumSpeed;
	}
	else
	{
		ClampedVelocity *= FMath::Clamp(Speed, MinimumSpeed, MaximumSpeed) / Speed;
	}

	ShallowWater->SetWaterBodyMIDParameters(TargetWaterBody);
	ShallowWater->RegisterImpact(SurfacePoint, ClampedVelocity, ClampedRadius);
	++ForwardedAdvancedImpactCount;
	++AcceptedImpulseCount;
	LastImpulseWorldLocation = SurfacePoint;
	LastImpulseStrength = ClampedVelocity.Size();
	switch (SourceType)
	{
	case EWorldWaterRippleImpulseSource::Movement:
		++MovementImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::Jump:
		++JumpImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::Landing:
		++LandingImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::Attack:
		++AttackImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::Explosion:
		++ExplosionImpulseCount;
		break;
	case EWorldWaterRippleImpulseSource::WaterEntry:
		++WaterEntryImpulseCount;
		break;
	default:
		break;
	}
	return true;
}

bool AWorldWaterRippleRegion::ProjectPointIntoDomain(
	const FVector& WorldPoint,
	const float ExpansionRadius,
	FVector& OutSurfacePoint) const
{
	const FVector2D HalfSize = GetResolvedDomainWorldSize() * 0.5f;
	const FVector Center = GetResolvedDomainCenter();
	const FVector2D Point(WorldPoint.X, WorldPoint.Y);
	const FVector2D Minimum(Center.X - HalfSize.X, Center.Y - HalfSize.Y);
	const FVector2D Maximum(Center.X + HalfSize.X, Center.Y + HalfSize.Y);
	const FVector2D Closest(
		FMath::Clamp(Point.X, Minimum.X, Maximum.X),
		FMath::Clamp(Point.Y, Minimum.Y, Maximum.Y));
	if (FVector2D::DistSquared(Point, Closest) > FMath::Square(FMath::Max(0.0f, ExpansionRadius)))
	{
		return false;
	}
	OutSurfacePoint = FVector(Closest.X, Closest.Y, GetWaterSurfaceZ());
	return true;
}

bool AWorldWaterRippleRegion::ResolveFieldSurfacePoint(
	const FWorldLightweightInteractionField& Field,
	FVector& OutSurfacePoint) const
{
	const float SurfaceZ = GetWaterSurfaceZ();
	const float StartDistance = Field.Start.Z - SurfaceZ;
	const float EndDistance = Field.End.Z - SurfaceZ;
	FVector Candidate = FMath::Abs(StartDistance) <= FMath::Abs(EndDistance)
		? Field.Start
		: Field.End;
	if (StartDistance * EndDistance <= 0.0f &&
		!FMath::IsNearlyEqual(StartDistance, EndDistance))
	{
		const float Alpha = FMath::Clamp(
			StartDistance / (StartDistance - EndDistance),
			0.0f,
			1.0f);
		Candidate = FMath::Lerp(Field.Start, Field.End, Alpha);
	}

	const float VerticalDistance = FMath::Abs(Candidate.Z - SurfaceZ);
	const float AllowedDistance = Field.SourceType == EWorldLightweightInteractionSource::Explosion
		? FMath::Max(GetSettings().SurfaceContactTolerance, Field.Radius)
		: FMath::Max(
			GetSettings().SurfaceContactTolerance,
			FMath::Min(Field.Radius, GetSettings().MaximumSurfaceProjectionDistance));
	const bool bCharacterSurfaceEvent =
		Field.SourceType == EWorldLightweightInteractionSource::Movement ||
		Field.SourceType == EWorldLightweightInteractionSource::Jump ||
		Field.SourceType == EWorldLightweightInteractionSource::Landing;
	if (VerticalDistance > AllowedDistance &&
		(!bCharacterSurfaceEvent ||
		!IsSourceActorOverlappingTargetWaterBody(Field.SourceActor)))
	{
		return false;
	}
	return ProjectPointIntoDomain(Candidate, Field.Radius, OutSurfacePoint);
}

bool AWorldWaterRippleRegion::IsSourceActorOverlappingTargetWaterBody(
	const AActor* SourceActor) const
{
	if (!IsValid(SourceActor))
	{
		return false;
	}

	const UWaterBodyComponent* WaterBodyComponent = ResolveTargetWaterBodyComponent();
	if (!WaterBodyComponent)
	{
		return false;
	}

	for (const UPrimitiveComponent* CollisionComponent :
		WaterBodyComponent->GetCollisionComponents(true))
	{
		if (IsValid(CollisionComponent) &&
			CollisionComponent->IsOverlappingActor(SourceActor))
		{
			return true;
		}
	}
	return false;
}

bool AWorldWaterRippleRegion::ResolveRequestSurfacePoint(
	const FWorldInteractionRequest& Request,
	FVector& OutSurfacePoint) const
{
	FVector Candidate = Request.Origin;
	if (Request.Hit.bBlockingHit)
	{
		Candidate = Request.Hit.ImpactPoint;
	}
	const float Radius = Request.Kind == EWorldInteractionKind::Explosion
		? Request.Radius
		: FMath::Max(Request.Radius, GetSettings().HeavyInteractionDefaultRadius);
	const float AllowedDistance = Request.Kind == EWorldInteractionKind::Explosion
		? FMath::Max(GetSettings().SurfaceContactTolerance, Radius)
		: GetSettings().MaximumSurfaceProjectionDistance;
	if (FMath::Abs(Candidate.Z - GetWaterSurfaceZ()) > AllowedDistance)
	{
		return false;
	}
	return ProjectPointIntoDomain(Candidate, Radius, OutSurfacePoint);
}

bool AWorldWaterRippleRegion::PassesSourceRateLimit(
	const FWorldLightweightInteractionField& Field,
	const FVector& SurfacePoint)
{
	if (!Field.SourceActor || !GetWorld())
	{
		return true;
	}
	FWorldWaterSourceRateState& State = SourceRateStates.FindOrAdd(Field.SourceActor);
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (Field.SourceType == EWorldLightweightInteractionSource::Movement)
	{
		if (State.bHasMovementSample &&
			(CurrentTime - State.LastMovementTime < GetSettings().MovementImpulseInterval ||
			FVector::Dist2D(State.LastMovementLocation, SurfacePoint) <
				GetSettings().MovementMinimumTravelDistance))
		{
			return false;
		}
		State.LastMovementLocation = SurfacePoint;
		State.LastMovementTime = CurrentTime;
		State.bHasMovementSample = true;
	}
	else if (Field.SourceType == EWorldLightweightInteractionSource::Attack)
	{
		if (CurrentTime - State.LastAttackTime < GetSettings().AttackImpulseInterval)
		{
			return false;
		}
		State.LastAttackTime = CurrentTime;
	}
	return true;
}

void AWorldWaterRippleRegion::HandleLightweightInteractionField(
	const FWorldLightweightInteractionField& Field)
{
	const FWorldWaterRippleSettings& Settings = GetSettings();
	EWorldWaterRippleImpulseSource SourceType;
	float StrengthScale = 0.0f;
	float RadiusScale = 1.0f;
	switch (Field.SourceType)
	{
	case EWorldLightweightInteractionSource::Movement:
		if (!Settings.bAcceptMovement) return;
		SourceType = EWorldWaterRippleImpulseSource::Movement;
		StrengthScale = Settings.MovementStrengthScale;
		RadiusScale = Settings.MovementRadiusScale;
		break;
	case EWorldLightweightInteractionSource::Jump:
		if (!Settings.bAcceptJump) return;
		SourceType = EWorldWaterRippleImpulseSource::Jump;
		StrengthScale = Settings.JumpStrengthScale;
		RadiusScale = Settings.JumpRadiusScale;
		break;
	case EWorldLightweightInteractionSource::Landing:
		if (!Settings.bAcceptLanding) return;
		SourceType = EWorldWaterRippleImpulseSource::Landing;
		StrengthScale = Settings.LandingStrengthScale;
		RadiusScale = Settings.LandingRadiusScale;
		break;
	case EWorldLightweightInteractionSource::Attack:
		if (!Settings.bAcceptAttack) return;
		SourceType = EWorldWaterRippleImpulseSource::Attack;
		StrengthScale = Settings.AttackStrengthScale;
		RadiusScale = Settings.AttackRadiusScale;
		break;
	case EWorldLightweightInteractionSource::Explosion:
		if (!Settings.bAcceptExplosion) return;
		SourceType = EWorldWaterRippleImpulseSource::Explosion;
		StrengthScale = Settings.ExplosionStrengthScale;
		RadiusScale = Settings.ExplosionRadiusScale;
		break;
	default:
		return;
	}

	FVector SurfacePoint;
	if (!ResolveFieldSurfacePoint(Field, SurfacePoint) ||
		!PassesSourceRateLimit(Field, SurfacePoint))
	{
		return;
	}
	const bool bAccepted = bUseWaterAdvancedShallowWater
		? ForwardAdvancedImpact(
			SurfacePoint,
			ResolveAdvancedImpactVelocity(Field, SourceType),
			Field.Radius * RadiusScale,
			SourceType,
			Field.SourceActor)
		: QueueRippleImpulse(
			SurfacePoint,
			Field.Radius * RadiusScale,
			Field.Strength * StrengthScale,
			SourceType,
			Field.SourceActor);
	if (!bAccepted)
	{
		return;
	}

	++HandledLightweightFieldCount;
	if (SourceType == EWorldWaterRippleImpulseSource::Explosion)
	{
		LastLightweightExplosionFrame = GFrameCounter;
		LastLightweightExplosionOrigin = Field.GetCenter();
		LastLightweightExplosionSource = Field.SourceActor;
	}
}

bool AWorldWaterRippleRegion::CanHandleWorldInteraction_Implementation(
	const FWorldInteractionRequest& Request) const
{
	if (!GetSettings().bEnabled)
	{
		return false;
	}
	if (Request.Kind != EWorldInteractionKind::DirectHit &&
		Request.Kind != EWorldInteractionKind::ProjectileImpact &&
		Request.Kind != EWorldInteractionKind::Explosion)
	{
		return false;
	}
	FVector SurfacePoint;
	return ResolveRequestSurfacePoint(Request, SurfacePoint);
}

void AWorldWaterRippleRegion::HandleWorldInteraction_Implementation(
	const FWorldInteractionRequest& Request)
{
	ProcessHeavyInteraction(Request);
}

void AWorldWaterRippleRegion::HandleInteractionProcessed(
	const FWorldInteractionRequest& Request,
	const FWorldInteractionResult& Result)
{
	if (Result.bAccepted)
	{
		ProcessHeavyInteraction(Request);
	}
}

bool AWorldWaterRippleRegion::ProcessHeavyInteraction(
	const FWorldInteractionRequest& Request)
{
	if (IsRecentHeavyRequest(Request.RequestId))
	{
		return false;
	}
	if (Request.Kind == EWorldInteractionKind::Explosion &&
		WasExplosionQueuedByLightweightPath(Request))
	{
		RememberHeavyRequest(Request.RequestId);
		++SuppressedDuplicateImpulseCount;
		return false;
	}

	FVector SurfacePoint;
	if (!CanHandleWorldInteraction_Implementation(Request) ||
		!ResolveRequestSurfacePoint(Request, SurfacePoint))
	{
		return false;
	}
	RememberHeavyRequest(Request.RequestId);

	const bool bExplosion = Request.Kind == EWorldInteractionKind::Explosion;
	const float SourceStrength = FMath::Max(Request.ImpulseStrength, Request.Damage);
	const float StrengthScale = bExplosion
		? GetSettings().ExplosionStrengthScale
		: GetSettings().HeavyInteractionStrengthScale;
	const float BaseRadius = Request.Radius > 0.0f
		? Request.Radius
		: GetSettings().HeavyInteractionDefaultRadius;
	const float RadiusScale = bExplosion
		? GetSettings().ExplosionRadiusScale
		: GetSettings().AttackRadiusScale;
	const EWorldWaterRippleImpulseSource SourceType = bExplosion
		? EWorldWaterRippleImpulseSource::Explosion
		: EWorldWaterRippleImpulseSource::Attack;
	bool bAccepted = false;
	if (bUseWaterAdvancedShallowWater)
	{
		const FVector Direction = Request.Direction.IsNearlyZero()
			? FVector::UpVector
			: Request.Direction.GetSafeNormal();
		const float ImpactSpeed = bExplosion
			? GetSettings().AdvancedExplosionImpactSpeed
			: FMath::Max(
				GetSettings().AdvancedMinimumImpactSpeed,
				SourceStrength * GetSettings().AdvancedAttackVelocityScale);
		bAccepted = ForwardAdvancedImpact(
			SurfacePoint,
			Direction * ImpactSpeed,
			BaseRadius * RadiusScale,
			SourceType,
			Request.SourceActor);
	}
	else
	{
		bAccepted = QueueRippleImpulse(
			SurfacePoint,
			BaseRadius * RadiusScale,
			SourceStrength * StrengthScale,
			SourceType,
			Request.SourceActor);
	}
	if (!bAccepted)
	{
		return false;
	}
	++HandledHeavyInteractionCount;
	return true;
}

bool AWorldWaterRippleRegion::WasExplosionQueuedByLightweightPath(
	const FWorldInteractionRequest& Request) const
{
	return LastLightweightExplosionFrame == GFrameCounter &&
		LastLightweightExplosionSource.Get() == Request.SourceActor &&
		FVector::DistSquared(LastLightweightExplosionOrigin, Request.Origin) <= 1.0f;
}

bool AWorldWaterRippleRegion::IsRecentHeavyRequest(const FGuid& RequestId) const
{
	return RequestId.IsValid() && RecentHeavyRequestIds.Contains(RequestId);
}

void AWorldWaterRippleRegion::RememberHeavyRequest(const FGuid& RequestId)
{
	if (!RequestId.IsValid())
	{
		return;
	}
	RecentHeavyRequestIds.AddUnique(RequestId);
	if (RecentHeavyRequestIds.Num() > MaxRememberedHeavyRequests)
	{
		RecentHeavyRequestIds.RemoveAt(
			0,
			RecentHeavyRequestIds.Num() - MaxRememberedHeavyRequests,
			EAllowShrinking::No);
	}
}

bool AWorldWaterRippleRegion::GetCapsuleBottomZ(
	const AActor& SourceActor,
	float& OutBottomZ) const
{
	const UCapsuleComponent* Capsule = SourceActor.FindComponentByClass<UCapsuleComponent>();
	if (!Capsule)
	{
		return false;
	}
	OutBottomZ = Capsule->GetComponentLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	return FMath::IsFinite(OutBottomZ);
}

void AWorldWaterRippleRegion::InitializeWaterCrossingState(
	AActor* SourceActor,
	const bool bAllowEstimatedCrossing)
{
	if (!IsValid(SourceActor) || WaterCrossingStates.Contains(SourceActor))
	{
		return;
	}
	float BottomZ = 0.0f;
	if (!GetCapsuleBottomZ(*SourceActor, BottomZ))
	{
		return;
	}

	const float SignedDistance = BottomZ - GetWaterSurfaceZ();
	const FVector Velocity = SourceActor->GetVelocity();
	const float DeltaSeconds = GetWorld()
		? FMath::Max(GetWorld()->GetDeltaSeconds(), UE_SMALL_NUMBER)
		: 0.0f;
	const float EstimatedPreviousDistance = SignedDistance - Velocity.Z * DeltaSeconds;
	FWorldWaterCrossingState State;
	State.PreviousSignedDistance = bAllowEstimatedCrossing
		? EstimatedPreviousDistance
		: SignedDistance;
	State.bEntryArmed = State.PreviousSignedDistance > 0.0f;
	WaterCrossingStates.Add(SourceActor, State);

	if (bAllowEstimatedCrossing && State.bEntryArmed && SignedDistance <= 0.0f &&
		-Velocity.Z >= GetSettings().WaterEntryMinDownwardSpeed)
	{
		const float Strength = GetSettings().WaterEntryMinimumStrength +
			(-Velocity.Z - GetSettings().WaterEntryMinDownwardSpeed) *
			GetSettings().WaterEntrySpeedStrengthScale;
		QueueRippleImpulse(
			FVector(SourceActor->GetActorLocation().X, SourceActor->GetActorLocation().Y, GetWaterSurfaceZ()),
			GetSettings().WaterEntryRadius,
			Strength,
			EWorldWaterRippleImpulseSource::WaterEntry,
			SourceActor);
		WaterCrossingStates.FindChecked(SourceActor).bEntryArmed = false;
		WaterCrossingStates.FindChecked(SourceActor).PreviousSignedDistance = SignedDistance;
	}
}

void AWorldWaterRippleRegion::UpdateWaterlineCrossings()
{
	for (auto Iterator = WaterCrossingStates.CreateIterator(); Iterator; ++Iterator)
	{
		AActor* SourceActor = Iterator.Key().Get();
		if (!IsValid(SourceActor))
		{
			Iterator.RemoveCurrent();
			continue;
		}
		float BottomZ = 0.0f;
		if (!GetCapsuleBottomZ(*SourceActor, BottomZ))
		{
			Iterator.RemoveCurrent();
			continue;
		}

		FWorldWaterCrossingState& State = Iterator.Value();
		const float SignedDistance = BottomZ - GetWaterSurfaceZ();
		const float DownwardSpeed = FMath::Max(0.0f, -SourceActor->GetVelocity().Z);
		if (SignedDistance >= GetSettings().WaterEntryRearmHeight)
		{
			State.bEntryArmed = true;
		}
		if (State.bEntryArmed && State.PreviousSignedDistance > 0.0f &&
			SignedDistance <= 0.0f &&
			DownwardSpeed >= GetSettings().WaterEntryMinDownwardSpeed)
		{
			const float Strength = GetSettings().WaterEntryMinimumStrength +
				(DownwardSpeed - GetSettings().WaterEntryMinDownwardSpeed) *
				GetSettings().WaterEntrySpeedStrengthScale;
			QueueRippleImpulse(
				FVector(SourceActor->GetActorLocation().X, SourceActor->GetActorLocation().Y, GetWaterSurfaceZ()),
				GetSettings().WaterEntryRadius,
				Strength,
				EWorldWaterRippleImpulseSource::WaterEntry,
				SourceActor);
			State.bEntryArmed = false;
		}
		State.PreviousSignedDistance = SignedDistance;
	}
}

void AWorldWaterRippleRegion::HandleRegionBeginOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32,
	bool,
	const FHitResult&)
{
	const UCapsuleComponent* Capsule = OtherActor
		? OtherActor->FindComponentByClass<UCapsuleComponent>()
		: nullptr;
	if (Capsule && OtherComponent == Capsule)
	{
		InitializeWaterCrossingState(OtherActor, true);
	}
}

void AWorldWaterRippleRegion::HandleRegionEndOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32)
{
	const UCapsuleComponent* Capsule = OtherActor
		? OtherActor->FindComponentByClass<UCapsuleComponent>()
		: nullptr;
	if (Capsule && OtherComponent == Capsule)
	{
		WaterCrossingStates.Remove(OtherActor);
		SourceRateStates.Remove(OtherActor);
	}
}

#include "WorldInteractionSubsystem.h"

#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraDataChannelAccessor.h"
#include "NiagaraDataChannelAsset.h"
#include "NiagaraDataChannelFunctionLibrary.h"
#include "PhysicsInteractable.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Sound/SoundBase.h"

#if WITH_EDITOR
static constexpr int32 DefaultDrawLooseDebrisFields = 1;
#else
static constexpr int32 DefaultDrawLooseDebrisFields = 0;
#endif

static TAutoConsoleVariable<int32> CVarDrawLooseDebrisFields(
	TEXT("pw.LooseDebris.DrawFields"),
	DefaultDrawLooseDebrisFields,
	TEXT("0=Off  1=Draw lightweight movement, weapon, landing, and explosion fields."),
	ECVF_Cheat);

void UWorldInteractionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!InteractionConfig)
	{
		InteractionConfig = LoadObject<UWorldInteractionConfig>(
			nullptr,
			TEXT("/Game/PhysicsWorldDemo/Config/DA_WorldInteractionConfig.DA_WorldInteractionConfig"));
	}
	if (!LooseDebrisConfig)
	{
		LooseDebrisConfig = LoadObject<UWorldLooseDebrisConfig>(
			nullptr,
			TEXT("/Game/PhysicsWorldDemo/LooseDebris/Config/DA_WorldLooseDebrisConfig.DA_WorldLooseDebrisConfig"));
	}
	ApplyWorldPhysicsSettings();
	ResetDebugStats();
}

void UWorldInteractionSubsystem::Deinitialize()
{
	for (const TWeakObjectPtr<UDecalComponent>& Decal : ActiveFeedbackDecals)
	{
		if (Decal.IsValid())
		{
			Decal->DestroyComponent();
		}
	}
	ActiveFeedbackDecals.Reset();
	OnInteractionProcessed.Clear();
	OnLightweightInteractionPublished.Clear();
	MovementFieldsBySourceThisFrame.Reset();
	AttackFieldsBySourceThisFrame.Reset();
	Super::Deinitialize();
}

bool UWorldInteractionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

const FWorldInteractionSettings& UWorldInteractionSubsystem::GetSettings() const
{
	return InteractionConfig ? InteractionConfig->Settings : FallbackSettings;
}

const FWorldLooseDebrisSettings& UWorldInteractionSubsystem::GetLooseDebrisSettings() const
{
	static const FWorldLooseDebrisSettings FallbackLooseDebrisSettings;
	return LooseDebrisConfig ? LooseDebrisConfig->Settings : FallbackLooseDebrisSettings;
}

void UWorldInteractionSubsystem::ApplyWorldPhysicsSettings()
{
	UWorld* World = GetWorld();
	AWorldSettings* WorldSettings = World ? World->GetWorldSettings() : nullptr;
	const FWorldInteractionSettings& Settings = GetSettings();
	if (!WorldSettings || !Settings.bOverrideWorldGravity)
	{
		return;
	}

	WorldSettings->bGlobalGravitySet = true;
	WorldSettings->GlobalGravityZ = Settings.WorldGravityZ;
	WorldSettings->bWorldGravitySet = false;
	const float AppliedGravityZ = WorldSettings->GetGravityZ();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("Physics World gravity applied: %.1f cm/s^2"),
		AppliedGravityZ);
}

float UWorldInteractionSubsystem::GetAppliedWorldGravityZ() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetGravityZ() : 0.0f;
}

void UWorldInteractionSubsystem::ResetDebugStats()
{
	ProcessedRequestCount = 0;
	DispatchedReceiverCount = 0;
	SpawnedNiagaraSystemCount = 0;
	PublishedLightweightFieldCount = 0;
	RejectedLightweightFieldCount = 0;
	DroppedLightweightFieldCount = 0;
	NiagaraDataChannelWriteCount = 0;
	LightweightEventSerial = 0;
	LightweightFieldsThisFrame = 0;
	LightweightBudgetFrame = MAX_uint64;
	MovementFieldsBySourceThisFrame.Reset();
	AttackFieldsBySourceThisFrame.Reset();
	LastResolvedSurfaceType = SurfaceType_Default;
	LastInteractionResult = FWorldInteractionResult();
	LastLightweightField = FWorldLightweightInteractionField();
}

void UWorldInteractionSubsystem::PrepareLightweightFrameBudget()
{
	if (LightweightBudgetFrame == GFrameCounter)
	{
		return;
	}
	LightweightBudgetFrame = GFrameCounter;
	LightweightFieldsThisFrame = 0;
	MovementFieldsBySourceThisFrame.Reset();
	AttackFieldsBySourceThisFrame.Reset();
}

int32 UWorldInteractionSubsystem::ResolveLightweightSourceId(
	const FWorldLightweightInteractionField& Field) const
{
	if (Field.SourceId != 0)
	{
		return Field.SourceId;
	}
	return IsValid(Field.SourceActor) ? Field.SourceActor->GetUniqueID() : 0;
}

bool UWorldInteractionSubsystem::PublishLightweightInteractionField(
	FWorldLightweightInteractionField Field)
{
	const FWorldLooseDebrisSettings& Settings = GetLooseDebrisSettings();
	if (!Settings.bEnabled || !GetWorld() || Field.Start.ContainsNaN() ||
		Field.End.ContainsNaN() || Field.Direction.ContainsNaN() ||
		Field.SourceVelocity.ContainsNaN() || !FMath::IsFinite(Field.Radius) ||
		!FMath::IsFinite(Field.Strength) || !FMath::IsFinite(Field.UpwardLift) ||
		!FMath::IsFinite(Field.Duration) || !FMath::IsFinite(Field.FalloffExponent) ||
		Field.Radius <= 0.0f || Field.Strength < 0.0f || Field.UpwardLift < 0.0f ||
		Field.Duration < 0.0f || Field.FalloffExponent <= 0.0f)
	{
		++RejectedLightweightFieldCount;
		return false;
	}

	PrepareLightweightFrameBudget();
	if (LightweightFieldsThisFrame >= FMath::Max(1, Settings.MaxFieldsPerFrame))
	{
		++DroppedLightweightFieldCount;
		return false;
	}

	Field.SourceId = ResolveLightweightSourceId(Field);
	if (Field.SourceType == EWorldLightweightInteractionSource::Movement)
	{
		int32& Count = MovementFieldsBySourceThisFrame.FindOrAdd(Field.SourceId);
		if (Count >= FMath::Max(1, Settings.MaxMovementFieldsPerSourcePerFrame))
		{
			++DroppedLightweightFieldCount;
			return false;
		}
		++Count;
	}
	else if (Field.SourceType == EWorldLightweightInteractionSource::Attack)
	{
		int32& Count = AttackFieldsBySourceThisFrame.FindOrAdd(Field.SourceId);
		if (Count >= FMath::Max(1, Settings.MaxAttackFieldsPerSourcePerFrame))
		{
			++DroppedLightweightFieldCount;
			return false;
		}
		++Count;
	}

	LightweightEventSerial = LightweightEventSerial == MAX_int32
		? 1
		: LightweightEventSerial + 1;
	if (Field.EventId <= 0)
	{
		Field.EventId = LightweightEventSerial;
	}
	Field.Direction = Field.Direction.GetSafeNormal();
	if (Field.Direction.IsNearlyZero())
	{
		Field.Direction = Field.SourceVelocity.GetSafeNormal();
	}
	if (Field.Direction.IsNearlyZero())
	{
		Field.Direction = FVector::ForwardVector;
	}

	++LightweightFieldsThisFrame;
	++PublishedLightweightFieldCount;
	LastLightweightField = Field;
	if (TryWriteLightweightFieldToDataChannel(Field))
	{
		++NiagaraDataChannelWriteCount;
	}
	DrawLightweightInteractionField(Field);
	OnLightweightInteractionPublished.Broadcast(Field);
	return true;
}

bool UWorldInteractionSubsystem::PublishCharacterMovementField(
	AActor* SourceActor,
	const FVector& Start,
	const FVector& End,
	const FVector& Velocity)
{
	const FWorldLooseDebrisSettings& Settings = GetLooseDebrisSettings();
	const float Speed = Velocity.Size2D();
	if (Speed < Settings.MovementMinSpeed ||
		FVector::Dist2D(Start, End) < Settings.MovementPublishDistance)
	{
		return false;
	}

	const float SpeedAlpha = FMath::Clamp(
		(Speed - Settings.MovementMinSpeed) /
		FMath::Max(1.0f, Settings.MovementReferenceSpeed - Settings.MovementMinSpeed),
		0.0f,
		1.0f);
	FWorldLightweightInteractionField Field;
	Field.SourceActor = SourceActor;
	Field.SourceType = EWorldLightweightInteractionSource::Movement;
	Field.ShapeType = EWorldLightweightInteractionShape::Capsule;
	Field.Start = Start;
	Field.End = End;
	Field.Direction = Velocity.GetSafeNormal2D();
	Field.SourceVelocity = Velocity;
	Field.Radius = Settings.MovementRadius;
	Field.Strength = FMath::Lerp(Settings.WalkStrength, Settings.RunStrength, SpeedAlpha);
	Field.UpwardLift = Settings.MovementUpwardLift * FMath::Lerp(0.45f, 1.0f, SpeedAlpha);
	Field.Duration = Settings.MovementFieldDuration;
	Field.FalloffExponent = Settings.MovementFalloffExponent;
	return PublishLightweightInteractionField(Field);
}

bool UWorldInteractionSubsystem::PublishWeaponSweepField(
	AActor* SourceActor,
	const FVector& BladeBase,
	const FVector& BladeTip,
	const FVector& Direction,
	const float MaxEndpointTravel)
{
	const FWorldLooseDebrisSettings& Settings = GetLooseDebrisSettings();
	FWorldLightweightInteractionField Field;
	Field.SourceActor = SourceActor;
	Field.SourceType = EWorldLightweightInteractionSource::Attack;
	Field.ShapeType = EWorldLightweightInteractionShape::Capsule;
	Field.Start = BladeBase;
	Field.End = BladeTip;
	Field.Direction = Direction;
	const float DeltaSeconds = GetWorld() ? FMath::Max(GetWorld()->GetDeltaSeconds(), UE_SMALL_NUMBER) : 1.0f;
	Field.SourceVelocity = Direction.GetSafeNormal() * MaxEndpointTravel / DeltaSeconds;
	Field.Radius = Settings.AttackInteractionRadius + FMath::Min(
		Settings.AttackMaxSweepPadding,
		FMath::Max(0.0f, MaxEndpointTravel) * Settings.AttackSweepPaddingScale);
	Field.Strength = Settings.AttackStrength;
	Field.UpwardLift = Settings.AttackUpwardLift;
	Field.Duration = Settings.AttackFieldDuration;
	Field.FalloffExponent = Settings.AttackFalloffExponent;
	Field.SwirlStrength = Settings.AttackSwirlStrength;
	return PublishLightweightInteractionField(Field);
}

bool UWorldInteractionSubsystem::PublishLandingField(
	AActor* SourceActor,
	const FVector& Location,
	const float ImpactSpeed)
{
	const FWorldLooseDebrisSettings& Settings = GetLooseDebrisSettings();
	if (ImpactSpeed < Settings.LandingMinVerticalSpeed)
	{
		return false;
	}
	const float SpeedAlpha = FMath::Clamp(
		(ImpactSpeed - Settings.LandingMinVerticalSpeed) /
		FMath::Max(1.0f, Settings.LandingReferenceSpeed - Settings.LandingMinVerticalSpeed),
		0.0f,
		1.0f);
	FWorldLightweightInteractionField Field;
	Field.SourceActor = SourceActor;
	Field.SourceType = EWorldLightweightInteractionSource::Landing;
	Field.ShapeType = EWorldLightweightInteractionShape::Sphere;
	Field.Start = Location;
	Field.End = Location;
	Field.Direction = FVector::UpVector;
	Field.SourceVelocity = FVector(0.0f, 0.0f, -ImpactSpeed);
	Field.Radius = Settings.LandingRadius;
	Field.Strength = FMath::Lerp(Settings.LandingMinStrength, Settings.LandingMaxStrength, SpeedAlpha);
	Field.UpwardLift = Settings.LandingUpwardLift * FMath::Lerp(0.55f, 1.0f, SpeedAlpha);
	Field.Duration = Settings.LandingFieldDuration;
	Field.FalloffExponent = 1.35f;
	return PublishLightweightInteractionField(Field);
}

bool UWorldInteractionSubsystem::PublishJumpField(
	AActor* SourceActor,
	const FVector& Location,
	const FVector& SourceVelocity)
{
	const FWorldLooseDebrisSettings& Settings = GetLooseDebrisSettings();
	FWorldLightweightInteractionField Field;
	Field.SourceActor = SourceActor;
	Field.SourceType = EWorldLightweightInteractionSource::Jump;
	Field.ShapeType = EWorldLightweightInteractionShape::Sphere;
	Field.Start = Location;
	Field.End = Location;
	Field.Direction = FVector::UpVector;
	Field.SourceVelocity = SourceVelocity;
	Field.Radius = Settings.JumpRadius;
	Field.Strength = Settings.JumpStrength;
	Field.UpwardLift = Settings.JumpUpwardLift;
	Field.Duration = Settings.JumpFieldDuration;
	Field.FalloffExponent = 1.35f;
	return PublishLightweightInteractionField(Field);
}

bool UWorldInteractionSubsystem::PublishExplosionField(const FWorldInteractionRequest& Request)
{
	if (Request.Kind != EWorldInteractionKind::Explosion || Request.Radius <= 0.0f)
	{
		return false;
	}
	const FWorldLooseDebrisSettings& Settings = GetLooseDebrisSettings();
	FWorldLightweightInteractionField Field;
	Field.SourceActor = Request.SourceActor;
	Field.SourceType = EWorldLightweightInteractionSource::Explosion;
	Field.ShapeType = EWorldLightweightInteractionShape::Sphere;
	Field.Start = Request.Origin;
	Field.End = Request.Origin;
	Field.Direction = Request.Direction;
	Field.Radius = Request.Radius;
	Field.Strength = Request.ImpulseStrength * Settings.ExplosionStrengthScale;
	Field.UpwardLift = Request.ImpulseStrength * Settings.ExplosionUpwardLiftScale;
	Field.Duration = Settings.ExplosionFieldDuration;
	Field.FalloffExponent = 1.0f;
	return PublishLightweightInteractionField(Field);
}

bool UWorldInteractionSubsystem::TryWriteLightweightFieldToDataChannel(
	const FWorldLightweightInteractionField& Field)
{
	const FWorldLooseDebrisSettings& Settings = GetLooseDebrisSettings();
	if (!Settings.bWriteNiagaraDataChannel)
	{
		return false;
	}
	UNiagaraDataChannelAsset* Channel = Settings.InteractionDataChannel.LoadSynchronous();
	if (!Channel || !Channel->Get())
	{
		return false;
	}

	FNiagaraDataChannelSearchParameters SearchParameters;
	SearchParameters.Location = Field.GetCenter();
	UNiagaraDataChannelWriter* Writer = UNiagaraDataChannelLibrary::WriteToNiagaraDataChannel(
		this,
		Channel,
		SearchParameters,
		1,
		true,
		false,
		true,
		TEXT("WorldLooseDebris"));
	if (!Writer || Writer->Num() < 1)
	{
		return false;
	}

	Writer->WriteInt(TEXT("EventId"), 0, Field.EventId);
	Writer->WriteInt(TEXT("SourceId"), 0, Field.SourceId);
	Writer->WriteInt(TEXT("SourceType"), 0, static_cast<int32>(Field.SourceType));
	Writer->WriteInt(TEXT("ShapeType"), 0, static_cast<int32>(Field.ShapeType));
	Writer->WritePosition(TEXT("Start"), 0, Field.Start);
	Writer->WritePosition(TEXT("End"), 0, Field.End);
	Writer->WriteVector(TEXT("Direction"), 0, Field.Direction);
	Writer->WriteVector(TEXT("SourceVelocity"), 0, Field.SourceVelocity);
	Writer->WriteFloat(TEXT("Radius"), 0, Field.Radius);
	Writer->WriteFloat(TEXT("Strength"), 0, Field.Strength);
	Writer->WriteFloat(TEXT("UpwardLift"), 0, Field.UpwardLift);
	Writer->WriteFloat(TEXT("Duration"), 0, Field.Duration);
	Writer->WriteFloat(TEXT("FalloffExponent"), 0, Field.FalloffExponent);
	Writer->WriteFloat(TEXT("SwirlStrength"), 0, Field.SwirlStrength);
	return true;
}

void UWorldInteractionSubsystem::DrawLightweightInteractionField(
	const FWorldLightweightInteractionField& Field) const
{
	const FWorldLooseDebrisSettings& Settings = GetLooseDebrisSettings();
	if (!Settings.bDrawDebugFields && CVarDrawLooseDebrisFields.GetValueOnGameThread() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FColor Color = FColor::Green;
	switch (Field.SourceType)
	{
	case EWorldLightweightInteractionSource::Attack:
		Color = FColor::Cyan;
		break;
	case EWorldLightweightInteractionSource::Landing:
		Color = FColor::Yellow;
		break;
	case EWorldLightweightInteractionSource::Explosion:
		Color = FColor(255, 128, 0);
		break;
	case EWorldLightweightInteractionSource::Wind:
		Color = FColor::Blue;
		break;
	default:
		break;
	}
	const float Duration = FMath::Max(0.01f, Settings.DebugDrawDuration);
	if (Field.ShapeType == EWorldLightweightInteractionShape::Sphere ||
		Field.Start.Equals(Field.End, 0.1f))
	{
		DrawDebugSphere(World, Field.GetCenter(), Field.Radius, 20, Color, false, Duration, 0, 1.5f);
	}
	else
	{
		const FVector Delta = Field.End - Field.Start;
		const float Length = Delta.Size();
		const FQuat Rotation = Length > UE_KINDA_SMALL_NUMBER
			? FQuat::FindBetweenNormals(FVector::UpVector, Delta / Length)
			: FQuat::Identity;
		DrawDebugCapsule(
			World,
			Field.GetCenter(),
			Field.Radius + Length * 0.5f,
			Field.Radius,
			Rotation,
			Color,
			false,
			Duration,
			0,
			1.5f);
	}
	DrawDebugDirectionalArrow(
		World,
		Field.GetCenter(),
		Field.GetCenter() + Field.Direction * FMath::Clamp(Field.Strength * 0.15f, 40.0f, 240.0f),
		18.0f,
		Color,
		false,
		Duration,
		0,
		2.0f);
}

int32 UWorldInteractionSubsystem::GetActiveFeedbackDecalCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<UDecalComponent>& Decal : ActiveFeedbackDecals)
	{
		Count += Decal.IsValid() ? 1 : 0;
	}
	return Count;
}

FWorldInteractionResult UWorldInteractionSubsystem::SubmitWorldInteraction(
	FWorldInteractionRequest Request)
{
	FWorldInteractionResult Result;
	UWorld* World = GetWorld();
	if (!World || Request.Origin.ContainsNaN() || Request.Damage < 0.0f ||
		Request.ImpulseStrength < 0.0f || Request.Radius < 0.0f)
	{
		return Result;
	}

	if (!Request.RequestId.IsValid())
	{
		Request.RequestId = FGuid::NewGuid();
	}
	Request.Direction = Request.Direction.GetSafeNormal();
	if (Request.Direction.IsNearlyZero())
	{
		Request.Direction = FVector::ForwardVector;
	}
	if (Request.Kind == EWorldInteractionKind::Explosion)
	{
		PublishExplosionField(Request);
	}

	Request.SurfaceType = ResolveSurfaceType(Request);
	Result.SurfaceType = Request.SurfaceType;
	LastResolvedSurfaceType = Request.SurfaceType;

	TSet<TWeakObjectPtr<AActor>> ReceiverActors;
	if (AActor* HitActor = Request.Hit.GetActor(); HitActor &&
		HitActor->GetClass()->ImplementsInterface(UPhysicsInteractable::StaticClass()))
	{
		ReceiverActors.Add(HitActor);
	}

	TSet<TWeakObjectPtr<UPrimitiveComponent>> PhysicsComponents;
	if (Request.Radius > UE_KINDA_SMALL_NUMBER)
	{
		FCollisionObjectQueryParams ObjectQuery;
		ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjectQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
		ObjectQuery.AddObjectTypesToQuery(ECC_PhysicsBody);
		ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
		ObjectQuery.AddObjectTypesToQuery(ECC_Destructible);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WorldInteractionOverlap), false);
		if (Request.SourceActor)
		{
			QueryParams.AddIgnoredActor(Request.SourceActor);
		}

		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(
			Overlaps,
			Request.Origin,
			FQuat::Identity,
			ObjectQuery,
			FCollisionShape::MakeSphere(Request.Radius),
			QueryParams);

		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (AActor* Actor = Overlap.GetActor(); Actor &&
				Actor->GetClass()->ImplementsInterface(UPhysicsInteractable::StaticClass()))
			{
				ReceiverActors.Add(Actor);
			}
			if (UPrimitiveComponent* Component = Overlap.GetComponent(); Component &&
				Component->IsSimulatingPhysics())
			{
				PhysicsComponents.Add(Component);
			}
		}
	}
	else if (Request.ImpulseStrength > 0.0f)
	{
		if (UPrimitiveComponent* HitComponent = Request.Hit.GetComponent();
			HitComponent && HitComponent->IsSimulatingPhysics())
		{
			PhysicsComponents.Add(HitComponent);
		}
	}

	const FWorldInteractionSettings& Settings = GetSettings();
	TSet<TWeakObjectPtr<AActor>> CustomPhysicsImpulseReceiverActors;
	for (const TWeakObjectPtr<AActor>& ReceiverActor : ReceiverActors)
	{
		AActor* Actor = ReceiverActor.Get();
		if (!Actor)
		{
			continue;
		}

		FWorldInteractionRequest ReceiverRequest = Request;
		if (Request.Radius > UE_KINDA_SMALL_NUMBER && Actor != Request.Hit.GetActor())
		{
			const float Distance = FVector::Distance(Request.Origin, Actor->GetActorLocation());
			const float LinearFalloff = 1.0f - FMath::Clamp(Distance / Request.Radius, 0.0f, 1.0f);
			const float Falloff = FMath::Max(Settings.MinimumRadialFalloff, LinearFalloff);
			ReceiverRequest.Damage *= Falloff;
			ReceiverRequest.ImpulseStrength *= Falloff;
		}

		if (!IPhysicsInteractable::Execute_CanHandleWorldInteraction(Actor, ReceiverRequest))
		{
			continue;
		}
		IPhysicsInteractable::Execute_HandleWorldInteraction(Actor, ReceiverRequest);
		if (IPhysicsInteractable::Execute_HandlesWorldInteractionPhysicsImpulse(
			Actor,
			ReceiverRequest))
		{
			CustomPhysicsImpulseReceiverActors.Add(Actor);
		}
		++Result.ReceiverCount;
	}

	for (const TWeakObjectPtr<UPrimitiveComponent>& PhysicsComponent : PhysicsComponents)
	{
		if (UPrimitiveComponent* Component = PhysicsComponent.Get())
		{
			if (CustomPhysicsImpulseReceiverActors.Contains(Component->GetOwner()))
			{
				continue;
			}
			// An interactable may replace or disable its intact body while handling the request.
			if (!Component->IsSimulatingPhysics())
			{
				continue;
			}
			if (Request.Radius > UE_KINDA_SMALL_NUMBER)
			{
				Component->AddRadialImpulse(
					Request.Origin,
					Request.Radius,
					Request.ImpulseStrength,
					RIF_Linear,
					false);
			}
			else
			{
				const FVector ImpactPoint = Request.Hit.bBlockingHit
					? FVector(Request.Hit.ImpactPoint)
					: Request.Origin;
				Component->AddImpulseAtLocation(
					Request.Direction * Request.ImpulseStrength,
					ImpactPoint);
			}
			++Result.PhysicsBodyCount;
		}
	}

	Result.bSpawnedSurfaceFeedback = SpawnSurfaceFeedback(Request, Request.SurfaceType);
	Result.bAccepted = true;
	LastInteractionResult = Result;
	++ProcessedRequestCount;
	DispatchedReceiverCount += Result.ReceiverCount;
	OnInteractionProcessed.Broadcast(Request, Result);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("WorldInteraction Request=%s Kind=%d Element=%d Surface=%d Receivers=%d Bodies=%d Feedback=%s"),
		*Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
		static_cast<int32>(Request.Kind),
		static_cast<int32>(Request.Element),
		static_cast<int32>(Request.SurfaceType.GetValue()),
		Result.ReceiverCount,
		Result.PhysicsBodyCount,
		Result.bSpawnedSurfaceFeedback ? TEXT("true") : TEXT("false"));
	return Result;
}

EPhysicalSurface UWorldInteractionSubsystem::ResolveSurfaceType(
	const FWorldInteractionRequest& Request) const
{
	if (const UPhysicalMaterial* PhysicalMaterial = Request.Hit.PhysMaterial.Get())
	{
		return UPhysicalMaterial::DetermineSurfaceType(PhysicalMaterial);
	}

	const UPrimitiveComponent* HitComponent = Request.Hit.GetComponent();
	const FBodyInstance* BodyInstance = HitComponent ? HitComponent->GetBodyInstance() : nullptr;
	const UPhysicalMaterial* SimpleMaterial = BodyInstance ? BodyInstance->GetSimplePhysicalMaterial() : nullptr;
	return SimpleMaterial
		? UPhysicalMaterial::DetermineSurfaceType(SimpleMaterial)
		: Request.SurfaceType.GetValue();
}

const FWorldSurfaceResponse* UWorldInteractionSubsystem::FindSurfaceResponse(
	const EPhysicalSurface SurfaceType) const
{
	const TArray<FWorldSurfaceResponse>& Responses = GetSettings().SurfaceResponses;
	const FWorldSurfaceResponse* DefaultResponse = nullptr;
	for (const FWorldSurfaceResponse& Response : Responses)
	{
		if (Response.SurfaceType == SurfaceType)
		{
			return &Response;
		}
		if (Response.SurfaceType == SurfaceType_Default)
		{
			DefaultResponse = &Response;
		}
	}
	return DefaultResponse;
}

bool UWorldInteractionSubsystem::SpawnSurfaceFeedback(
	const FWorldInteractionRequest& Request,
	const EPhysicalSurface SurfaceType)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	bool bSpawned = false;
	const FWorldInteractionSettings& Settings = GetSettings();
	const FVector FeedbackLocation = Request.Hit.bBlockingHit
		? FVector(Request.Hit.ImpactPoint)
		: Request.Origin;
	const FVector ImpactNormal = Request.Hit.bBlockingHit
		? FVector(Request.Hit.ImpactNormal).GetSafeNormal()
		: -Request.Direction;

	if (Request.Kind == EWorldInteractionKind::Explosion)
	{
		if (UNiagaraSystem* ExplosionEffect = Settings.ExplosionEffect.LoadSynchronous())
		{
			if (UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				World,
				ExplosionEffect,
				FeedbackLocation,
				ImpactNormal.Rotation(),
				FVector::OneVector * Settings.ExplosionEffectScale))
			{
				++SpawnedNiagaraSystemCount;
				bSpawned = true;
			}
		}
		if (USoundBase* ExplosionSound = Settings.ExplosionSound.LoadSynchronous())
		{
			UGameplayStatics::PlaySoundAtLocation(World, ExplosionSound, FeedbackLocation);
			bSpawned = true;
		}
	}

	const FWorldSurfaceResponse* Response = FindSurfaceResponse(SurfaceType);
	if (!Response)
	{
		return bSpawned;
	}

	if (UNiagaraSystem* ImpactEffect = Response->ImpactEffect.LoadSynchronous())
	{
		if (UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World,
			ImpactEffect,
			FeedbackLocation,
			ImpactNormal.Rotation(),
			FVector::OneVector * Response->ImpactEffectScale))
		{
			++SpawnedNiagaraSystemCount;
			bSpawned = true;
		}
	}
	if (USoundBase* ImpactSound = Response->ImpactSound.LoadSynchronous())
	{
		UGameplayStatics::PlaySoundAtLocation(World, ImpactSound, FeedbackLocation);
		bSpawned = true;
	}

	if (Request.Element != EWorldElementType::Fire || !Response->bAllowBurnDecal ||
		Settings.MaxActiveFeedbackDecals <= 0)
	{
		return bSpawned;
	}

	UMaterialInterface* DecalMaterial = Response->BurnDecalMaterial.LoadSynchronous();
	if (!DecalMaterial)
	{
		return bSpawned;
	}

	FRandomStream RandomStream(GetTypeHash(Request.RequestId));
	FRotator DecalRotation = (-ImpactNormal).Rotation();
	DecalRotation.Roll = RandomStream.FRandRange(0.0f, 360.0f);
	const float Scale = RandomStream.FRandRange(0.85f, 1.15f);
	UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(
		World,
		DecalMaterial,
		Response->BurnDecalSize * Scale,
		FeedbackLocation + ImpactNormal,
		DecalRotation);
	if (Decal)
	{
		Decal->SetFadeOut(
			Settings.BurnDecalFadeStartDelay,
			Settings.BurnDecalFadeDuration,
			true);
		ActiveFeedbackDecals.Add(Decal);
		TrimDecalPool();
		bSpawned = true;
	}
	return bSpawned;
}

void UWorldInteractionSubsystem::TrimDecalPool()
{
	ActiveFeedbackDecals.RemoveAll(
		[](const TWeakObjectPtr<UDecalComponent>& Decal)
		{
			return !Decal.IsValid();
		});

	const int32 MaxDecals = FMath::Max(0, GetSettings().MaxActiveFeedbackDecals);
	while (ActiveFeedbackDecals.Num() > MaxDecals)
	{
		if (UDecalComponent* OldestDecal = ActiveFeedbackDecals[0].Get())
		{
			OldestDecal->DestroyComponent();
		}
		ActiveFeedbackDecals.RemoveAt(0);
	}
}

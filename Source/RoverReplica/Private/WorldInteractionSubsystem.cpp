#include "WorldInteractionSubsystem.h"

#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicsInteractable.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Sound/SoundBase.h"

void UWorldInteractionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!InteractionConfig)
	{
		InteractionConfig = LoadObject<UWorldInteractionConfig>(
			nullptr,
			TEXT("/Game/PhysicsWorldDemo/Config/DA_WorldInteractionConfig.DA_WorldInteractionConfig"));
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
	LastResolvedSurfaceType = SurfaceType_Default;
	LastInteractionResult = FWorldInteractionResult();
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
		++Result.ReceiverCount;
	}

	for (const TWeakObjectPtr<UPrimitiveComponent>& PhysicsComponent : PhysicsComponents)
	{
		if (UPrimitiveComponent* Component = PhysicsComponent.Get())
		{
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
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				World,
				ExplosionEffect,
				FeedbackLocation,
				ImpactNormal.Rotation());
			bSpawned = true;
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
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World,
			ImpactEffect,
			FeedbackLocation,
			ImpactNormal.Rotation());
		bSpawned = true;
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

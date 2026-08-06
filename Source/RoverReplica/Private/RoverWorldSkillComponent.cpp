#include "RoverWorldSkillComponent.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "WorldFireballProjectile.h"

URoverWorldSkillComponent::URoverWorldSkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URoverWorldSkillComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!InteractionConfig)
	{
		InteractionConfig = LoadObject<UWorldInteractionConfig>(
			nullptr,
			TEXT("/Game/PhysicsWorldDemo/Config/DA_WorldInteractionConfig.DA_WorldInteractionConfig"));
	}
}

const FWorldInteractionSettings& URoverWorldSkillComponent::GetSettings() const
{
	return InteractionConfig ? InteractionConfig->Settings : FallbackSettings;
}

bool URoverWorldSkillComponent::RequestFireball()
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !World)
	{
		return false;
	}

	const FWorldInteractionSettings& Settings = GetSettings();
	const float CurrentTime = World->GetTimeSeconds();
	if (bHasFiredFireball && CurrentTime - LastFireballTimeSeconds < Settings.FireballCooldown)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (OwnerPawn && OwnerPawn->GetController())
	{
		OwnerPawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		OwnerActor->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	const FVector ViewDirection = ViewRotation.Vector().GetSafeNormal();
	const FVector SpawnLocation = OwnerActor->GetActorLocation() +
		FVector::UpVector * Settings.FireballSpawnHeight +
		ViewDirection * Settings.FireballSpawnDistance;
	FVector AimPoint = ViewLocation + ViewDirection * Settings.FireballAimDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RoverFireballAim), true, OwnerActor);
	QueryParams.bReturnPhysicalMaterial = true;
	FHitResult AimHit;
	if (World->LineTraceSingleByChannel(
		AimHit,
		ViewLocation,
		AimPoint,
		ECC_Visibility,
		QueryParams))
	{
		AimPoint = AimHit.ImpactPoint;
	}
	const FVector LaunchDirection = (AimPoint - SpawnLocation).GetSafeNormal();

	UClass* FireballClass = Settings.FireballClass.LoadSynchronous();
	if (!FireballClass)
	{
		FireballClass = AWorldFireballProjectile::StaticClass();
	}
	const FTransform SpawnTransform(LaunchDirection.Rotation(), SpawnLocation);
	AWorldFireballProjectile* Projectile = World->SpawnActorDeferred<AWorldFireballProjectile>(
		FireballClass,
		SpawnTransform,
		OwnerActor,
		const_cast<APawn*>(OwnerPawn),
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
	{
		return false;
	}

	Projectile->InitializeProjectile(OwnerActor, LaunchDirection, InteractionConfig);
	UGameplayStatics::FinishSpawningActor(Projectile, SpawnTransform);
	LastSpawnedFireball = Projectile;
	LastFireballTimeSeconds = CurrentTime;
	bHasFiredFireball = true;
	return true;
}

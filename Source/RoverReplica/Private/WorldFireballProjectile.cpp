#include "WorldFireballProjectile.h"

#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "WorldInteractionConfig.h"
#include "WorldInteractionSubsystem.h"

AWorldFireballProjectile::AWorldFireballProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	const FWorldInteractionSettings Defaults;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(Defaults.FireballCollisionRadius);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	CollisionSphere->SetNotifyRigidBodyCollision(true);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionSphere;
	ProjectileMovement->InitialSpeed = Defaults.FireballSpeed;
	ProjectileMovement->MaxSpeed = Defaults.FireballSpeed;
	ProjectileMovement->ProjectileGravityScale = Defaults.FireballGravityScale;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;

	FireballEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FireballEffect"));
	FireballEffect->SetupAttachment(CollisionSphere);
	FireballEffect->SetAutoActivate(false);
}

void AWorldFireballProjectile::InitializeProjectile(
	AActor* InSourceActor,
	const FVector& InLaunchDirection,
	UWorldInteractionConfig* InInteractionConfig)
{
	SourceActor = InSourceActor;
	InteractionConfig = InInteractionConfig;
	LaunchDirection = InLaunchDirection.GetSafeNormal();
	if (LaunchDirection.IsNearlyZero())
	{
		LaunchDirection = FVector::ForwardVector;
	}
	SetOwner(InSourceActor);
	if (const APawn* SourcePawn = Cast<APawn>(InSourceActor))
	{
		SetInstigator(const_cast<APawn*>(SourcePawn));
	}
}

void AWorldFireballProjectile::BeginPlay()
{
	Super::BeginPlay();
	if (!InteractionConfig)
	{
		InteractionConfig = LoadObject<UWorldInteractionConfig>(
			nullptr,
			TEXT("/Game/PhysicsWorldDemo/Config/DA_WorldInteractionConfig.DA_WorldInteractionConfig"));
	}

	const FWorldInteractionSettings Defaults;
	const FWorldInteractionSettings& Settings = InteractionConfig
		? InteractionConfig->Settings
		: Defaults;
	CollisionSphere->SetSphereRadius(Settings.FireballCollisionRadius);
	if (SourceActor)
	{
		CollisionSphere->IgnoreActorWhenMoving(SourceActor, true);
	}
	CollisionSphere->OnComponentHit.AddDynamic(this, &AWorldFireballProjectile::HandleCollisionHit);

	ProjectileMovement->InitialSpeed = Settings.FireballSpeed;
	ProjectileMovement->MaxSpeed = Settings.FireballSpeed;
	ProjectileMovement->ProjectileGravityScale = Settings.FireballGravityScale;
	ProjectileMovement->Velocity = LaunchDirection * Settings.FireballSpeed;
	ProjectileMovement->Activate(true);

	if (UNiagaraSystem* Effect = Settings.FireballEffect.LoadSynchronous())
	{
		FireballEffect->SetAsset(Effect);
		FireballEffect->SetRelativeScale3D(FVector::OneVector * Settings.FireballEffectScale);
		FireballEffect->Activate(true);
	}
	SetLifeSpan(Settings.FireballLifetime);
}

void AWorldFireballProjectile::HandleCollisionHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (OtherActor != SourceActor)
	{
		DetonateAtHit(Hit);
	}
}

void AWorldFireballProjectile::DetonateAtHit(const FHitResult& Hit)
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMovement->StopMovementImmediately();
	FireballEffect->DeactivateImmediate();

	const FWorldInteractionSettings Defaults;
	const FWorldInteractionSettings& Settings = InteractionConfig
		? InteractionConfig->Settings
		: Defaults;
	FWorldInteractionRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Kind = EWorldInteractionKind::Explosion;
	Request.Element = EWorldElementType::Fire;
	Request.SourceActor = SourceActor ? SourceActor : this;
	Request.InstigatorController = GetInstigatorController();
	Request.Hit = Hit;
	Request.Origin = Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : GetActorLocation();
	Request.Direction = ProjectileMovement->Velocity.GetSafeNormal();
	if (Request.Direction.IsNearlyZero())
	{
		Request.Direction = LaunchDirection;
	}
	Request.Damage = Settings.ExplosionDamage;
	Request.ImpulseStrength = Settings.ExplosionImpulseStrength;
	Request.Radius = Settings.ExplosionRadius;

	if (UWorld* World = GetWorld())
	{
		if (UWorldInteractionSubsystem* Subsystem = World->GetSubsystem<UWorldInteractionSubsystem>())
		{
			Subsystem->SubmitWorldInteraction(Request);
		}
	}
	Destroy();
}

bool AWorldFireballProjectile::IsFireballEffectActive() const
{
	return FireballEffect && FireballEffect->GetAsset() && FireballEffect->IsActive();
}

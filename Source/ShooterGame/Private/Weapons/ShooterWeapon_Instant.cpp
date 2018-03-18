// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "ShooterGame.h"
#include "Weapons/ShooterWeapon_Instant.h"
#include "Particles/ParticleSystemComponent.h"
#include "Effects/ShooterImpactEffect.h"
#include "UI/ShooterHUD.h"
#include "SShooterScoreboardWidget.h"
#include "SChatWidget.h"
#include "Engine/ViewportSplitScreen.h"
#include "ShooterWeapon.h"
#include "ShooterDamageType.h"
#include "Online/ShooterPlayerState.h"
#include "Misc/NetworkVersion.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Runtime/Engine/Classes/GameFramework/HUD.h"
AShooterWeapon_Instant::AShooterWeapon_Instant(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	CurrentFiringSpread = 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Weapon usage

void AShooterWeapon_Instant::FireWeapon()
{
	const int32 RandomSeed = FMath::Rand();
	FRandomStream WeaponRandomStream(RandomSeed);
	const float CurrentSpread = GetCurrentSpread();
	const float ConeHalfAngle = FMath::DegreesToRadians(CurrentSpread * 0.5f);

	const FVector AimDir = GetAdjustedAim();
	const FVector StartTrace = GetCameraDamageStartLocation(AimDir);
	const FVector ShootDir = WeaponRandomStream.VRandCone(AimDir, ConeHalfAngle, ConeHalfAngle);
	const FVector EndTrace = StartTrace + ShootDir * InstantConfig.WeaponRange;

	const FHitResult Impact = WeaponTrace(StartTrace, EndTrace);
	ProcessInstantHit(Impact, StartTrace, ShootDir, RandomSeed, CurrentSpread);

	CurrentFiringSpread = FMath::Min(InstantConfig.FiringSpreadMax, CurrentFiringSpread + InstantConfig.FiringSpreadIncrement);
}

bool AShooterWeapon_Instant::ServerNotifyHit_Validate(const FHitResult& Impact, FVector_NetQuantizeNormal ShootDir, int32 RandomSeed, float ReticleSpread)
{
	//UE_LOG(LogTemp, Display, TEXT("Hit"));
	return true;
}

void AShooterWeapon_Instant::ServerNotifyHit_Implementation(const FHitResult& Impact, FVector_NetQuantizeNormal ShootDir, int32 RandomSeed, float ReticleSpread)
{
	const float WeaponAngleDot = FMath::Abs(FMath::Sin(ReticleSpread * PI / 180.f));
	UE_LOG(LogTemp, Display, TEXT("Notifying Server"));

	// if we have an instigator, calculate dot between the view and the shot
	if (Instigator && (Impact.GetActor() || Impact.bBlockingHit))
	{
		const FVector Origin = GetMuzzleLocation();
		const FVector ViewDir = (Impact.Location - Origin).GetSafeNormal();

		// is the angle between the hit and the view within allowed limits (limit + weapon max angle)
		const float ViewDotHitDir = FVector::DotProduct(Instigator->GetViewRotation().Vector(), ViewDir);
		if (ViewDotHitDir > InstantConfig.AllowedViewDotHitDir - WeaponAngleDot)
		{
			if (CurrentState != EWeaponState::Idle)
			{
				if (Impact.GetActor() == NULL) //Did not hit a player
				{
					if (Impact.bBlockingHit) //But it did hit an actor object
					{
						UE_LOG(LogTemp, Display, TEXT("Hit Non Player"));
						ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);
					}
				}
				// assume it told the truth about static things because they don't move and the hit 
				// usually doesn't have significant gameplay implications
				else if (Impact.GetActor()->IsRootComponentStatic() || Impact.GetActor()->IsRootComponentStationary())
				{
					UE_LOG(LogTemp, Display, TEXT("Hit Static Object"));
					ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);
				}
				else
				{
					//// Get the component bounding box
					//const FBox HitBox = Impact.GetActor()->GetComponentsBoundingBox();

					//// calculate the box extent, and increase by a leeway
					//FVector BoxExtent = 0.5 * (HitBox.Max - HitBox.Min);
					//BoxExtent *= InstantConfig.ClientSideHitLeeway;

					//// avoid precision errors with really thin objects
					//BoxExtent.X = FMath::Max(20.0f, BoxExtent.X);
					//BoxExtent.Y = FMath::Max(20.0f, BoxExtent.Y);
					//BoxExtent.Z = FMath::Max(20.0f, BoxExtent.Z);

					//// Get the box center
					//const FVector BoxCenter = (HitBox.Min + HitBox.Max) * 0.5;

					//// if we are within client tolerance
					//if (FMath::Abs(Impact.Location.Z - BoxCenter.Z) < BoxExtent.Z &&
					//	FMath::Abs(Impact.Location.X - BoxCenter.X) < BoxExtent.X &&
					//	FMath::Abs(Impact.Location.Y - BoxCenter.Y) < BoxExtent.Y)
					//{
					//	ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);
					//}
					//else
					//{
					//	UE_LOG(LogShooterWeapon, Log, TEXT("%s Rejected client side hit of %s (outside bounding box tolerance)"), *GetNameSafe(this), *GetNameSafe(Impact.GetActor()));
					//}
					AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(GetWorld()->GetFirstPlayerController());
					for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
					{
						// all local players get death messages so they can update their huds.
						AShooterPlayerController* TestPC = Cast<AShooterPlayerController>(*It);
						//UE_LOG(LogShooterWeapon, Log, TEXT("Finding Controller %s"), *GetNameSafe(TestPC));
						if (TestPC && TestPC->IsLocalController())
						{
							//UE_LOG(LogShooterWeapon, Log, TEXT("%s Is the local controller"), *GetNameSafe(MyPC));
							MyPC = TestPC;
						}
						else if (TestPC && TestPC->HasAuthority())
						{
							//UE_LOG(LogShooterWeapon, Log, TEXT("%s Is the Player controller"), *GetNameSafe(MyPC));
							MyPC = TestPC;
						}
					}
					AShooterPlayerState* ShooterPlayer = Cast<AShooterPlayerState>(MyPC->PlayerState);
					//UE_LOG(LogShooterWeapon, Log, TEXT("%s Too laggy, Rejected client side hit of %s"), *GetNameSafe(MyPC), *GetNameSafe(Impact.GetActor()));

					if (MyPC == NULL)
					{
						UE_LOG(LogTemp, Display, TEXT("Player Controller Missing also Player State"));
					}

					//if (MyPlayerState == NULL)
					//{
					//	UE_LOG(LogTemp, Display, TEXT("Player Controller Missing also Player State"));
					//}
					FString Text = FString::FromInt(ShooterPlayer->Ping * 4);
					GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, Text);
					UE_LOG(LogTemp, Display, TEXT("Hit Player Object"));
					

					UE_LOG(LogTemp, Display, TEXT("Process Instant Hit"));
					ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);

					
				}
			}
			else if (ViewDotHitDir <= InstantConfig.AllowedViewDotHitDir)
			{
				UE_LOG(LogShooterWeapon, Log, TEXT("%s Rejected client side hit of %s (facing too far from the hit direction)"), *GetNameSafe(this), *GetNameSafe(Impact.GetActor()));
			}
			else
			{
				UE_LOG(LogShooterWeapon, Log, TEXT("%s Rejected client side hit of %s"), *GetNameSafe(this), *GetNameSafe(Impact.GetActor()));
			}
		}
	}
}
bool AShooterWeapon_Instant::ServerNotifyMiss_Validate(FVector_NetQuantizeNormal ShootDir, int32 RandomSeed, float ReticleSpread)
{
	UE_LOG(LogTemp, Display, TEXT("Miss"));
	return true;
}

void AShooterWeapon_Instant::ServerNotifyMiss_Implementation(FVector_NetQuantizeNormal ShootDir, int32 RandomSeed, float ReticleSpread)
{
	const FVector Origin = GetMuzzleLocation();

	// play FX on remote clients
	HitNotify.Origin = Origin;
	HitNotify.RandomSeed = RandomSeed;
	HitNotify.ReticleSpread = ReticleSpread;

	// play FX locally
	if (GetNetMode() != NM_DedicatedServer)
	{
		const FVector EndTrace = Origin + ShootDir * InstantConfig.WeaponRange;
		SpawnTrailEffect(EndTrace);
	}
}
uint8 AShooterWeapon_Instant::CalcAveragePing(float timestamp)
{
	int sum = 0;
	for (int32 Index = 0; Index != pingrecord.Num(); ++Index)
	{
		sum += pingrecord[Index];
	}
	int average = sum / pingrecord.Num();
	if (pingrecord.Num() > 1)
	{
		pingrecord.Empty();
		UE_LOG(LogTemp, Display, TEXT("Emptying because full"));
	}
	float time = timestamp - timeElapsed;
	if (time > 10)
	{
		pingrecord.Empty();
		UE_LOG(LogTemp, Display, TEXT("Emptying records because time"));
		GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, "Emptying records because time");
	}
	timeElapsed = timestamp;
	return average;
}
void AShooterWeapon_Instant::ProcessInstantHit(const FHitResult& Impact, const FVector& Origin, const FVector& ShootDir, int32 RandomSeed, float ReticleSpread)
{
	if (MyPawn && MyPawn->IsLocallyControlled() && GetNetMode() == NM_Client)
	{
		// if we're a client and we've hit something that is being controlled by the server
		AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(GetWorld()->GetFirstPlayerController());
		AShooterPlayerState* ShooterPlayer = Cast<AShooterPlayerState>(MyPC->PlayerState);
		
		float timestamp = GetWorld()->GetTimeSeconds();
		
		//uint8 recent = CalcAveragePing(timestamp);
		int diff = ShooterPlayer->Ping - lastPing;
		float m = abs(diff);
		FString Text = "My diff: " + FString::FromInt(diff * 4) +  " vs Recent Ping: " + FString::FromInt(lastPing * 4);
		GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, Text);
		if (m * 4 > 50 )
		{
			UE_LOG(LogShooterWeapon, Log, TEXT("%s Too laggy, Rejected client side hit of %s"), *GetNameSafe(MyPC), *GetNameSafe(Impact.GetActor()));
			lastPing = ShooterPlayer->Ping;
			return;
		}
		if (Impact.GetActor() && Impact.GetActor()->GetRemoteRole() == ROLE_Authority)
		{
			// notify the server of the hit
			//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, "Notify Server Hit Authority");
			if (Impact.GetActor() && Impact.GetActor()->GetRemoteRole() == ROLE_Authority)
			{
				// notify the server of the hit
				ServerNotifyHit(Impact, ShootDir, RandomSeed, ReticleSpread);
			}
			else if (Impact.GetActor() == NULL)
			{
				if (Impact.bBlockingHit)
				{
					// notify the server of the hit
					//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, "Notify Server Hit Non Authority");
					ServerNotifyHit(Impact, ShootDir, RandomSeed, ReticleSpread);
				}
				else
				{
					// notify server of the miss
					GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, "Notify Server of Miss");

					ServerNotifyMiss(ShootDir, RandomSeed, ReticleSpread);
				}
			}
		}
		lastPing = ShooterPlayer->Ping;
		// process a confirmed hit
		UE_LOG(LogTemp, Display, TEXT("Process Instant Hit Complete"));
		//ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);
		UE_LOG(LogTemp, Display, TEXT("Process Instant Hit Anyways"));
		ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);
	}
}

void AShooterWeapon_Instant::ProcessInstantHit_Confirmed(const FHitResult& Impact, const FVector& Origin, const FVector& ShootDir, int32 RandomSeed, float ReticleSpread)
{
	// handle damage
	
	UE_LOG(LogTemp, Display, TEXT("Confirmed Hit"));
	UE_LOG(LogTemp, Display, TEXT("%s"), *GetNameSafe(Impact.GetActor()));
	if (ShouldDealDamage(Impact.GetActor())) //If the actor is a player. Deal Damage
	{
		UE_LOG(LogTemp, Display, TEXT("Deadly Hit"));
		DealDamage(Impact, ShootDir);
	}

	// play FX on remote clients
	if (Role == ROLE_Authority)
	{
		HitNotify.Origin = Origin;
		HitNotify.RandomSeed = RandomSeed;
		HitNotify.ReticleSpread = ReticleSpread;
	}

	// play FX locally
	if (GetNetMode() != NM_DedicatedServer)
	{
		const FVector EndTrace = Origin + ShootDir * InstantConfig.WeaponRange;
		const FVector EndPoint = Impact.GetActor() ? Impact.ImpactPoint : EndTrace;

		SpawnTrailEffect(EndPoint);
		SpawnImpactEffects(Impact);
	}
}

bool AShooterWeapon_Instant::ShouldDealDamage(AActor* TestActor) const
{
	// if we're an actor on the server, or the actor's role is authoritative, we should register damage
	if (TestActor) //If the actor is a player
	{
		UE_LOG(LogTemp, Display, TEXT("%s"), *GetNameSafe(TestActor));
		if (GetNetMode() != NM_Client ||
			TestActor->Role == ROLE_Authority ||
			TestActor->bTearOff)
		{
			return true;
		}
	}

	return false;
}

void AShooterWeapon_Instant::DealDamage(const FHitResult& Impact, const FVector& ShootDir)
{
	FPointDamageEvent PointDmg;
	PointDmg.DamageTypeClass = InstantConfig.DamageType;
	PointDmg.HitInfo = Impact;
	PointDmg.ShotDirection = ShootDir;
	PointDmg.Damage = InstantConfig.HitDamage;

	Impact.GetActor()->TakeDamage(PointDmg.Damage, PointDmg, MyPawn->Controller, this);
}

void AShooterWeapon_Instant::OnBurstFinished()
{
	Super::OnBurstFinished();

	CurrentFiringSpread = 0.0f;
}


//////////////////////////////////////////////////////////////////////////
// Weapon usage helpers

float AShooterWeapon_Instant::GetCurrentSpread() const
{
	float FinalSpread = InstantConfig.WeaponSpread + CurrentFiringSpread;
	if (MyPawn && MyPawn->IsTargeting())
	{
		FinalSpread *= InstantConfig.TargetingSpreadMod;
	}

	return FinalSpread;
}


//////////////////////////////////////////////////////////////////////////
// Replication & effects

void AShooterWeapon_Instant::OnRep_HitNotify()
{
	SimulateInstantHit(HitNotify.Origin, HitNotify.RandomSeed, HitNotify.ReticleSpread);
}

void AShooterWeapon_Instant::SimulateInstantHit(const FVector& ShotOrigin, int32 RandomSeed, float ReticleSpread)
{
	FRandomStream WeaponRandomStream(RandomSeed);
	const float ConeHalfAngle = FMath::DegreesToRadians(ReticleSpread * 0.5f);

	const FVector StartTrace = ShotOrigin;
	const FVector AimDir = GetAdjustedAim();
	const FVector ShootDir = WeaponRandomStream.VRandCone(AimDir, ConeHalfAngle, ConeHalfAngle);
	const FVector EndTrace = StartTrace + ShootDir * InstantConfig.WeaponRange;

	FHitResult Impact = WeaponTrace(StartTrace, EndTrace);
	if (Impact.bBlockingHit)
	{
		SpawnImpactEffects(Impact);
		SpawnTrailEffect(Impact.ImpactPoint);
	}
	else
	{
		SpawnTrailEffect(EndTrace);
	}
}

void AShooterWeapon_Instant::SpawnImpactEffects(const FHitResult& Impact)
{
	if (ImpactTemplate && Impact.bBlockingHit)
	{
		FHitResult UseImpact = Impact;

		// trace again to find component lost during replication
		if (!Impact.Component.IsValid())
		{
			const FVector StartTrace = Impact.ImpactPoint + Impact.ImpactNormal * 10.0f;
			const FVector EndTrace = Impact.ImpactPoint - Impact.ImpactNormal * 10.0f;
			FHitResult Hit = WeaponTrace(StartTrace, EndTrace);
			UseImpact = Hit;
		}

		FTransform const SpawnTransform(Impact.ImpactNormal.Rotation(), Impact.ImpactPoint);
		AShooterImpactEffect* EffectActor = GetWorld()->SpawnActorDeferred<AShooterImpactEffect>(ImpactTemplate, SpawnTransform);
		if (EffectActor)
		{
			EffectActor->SurfaceHit = UseImpact;
			UGameplayStatics::FinishSpawningActor(EffectActor, SpawnTransform);
		}
	}
}

void AShooterWeapon_Instant::SpawnTrailEffect(const FVector& EndPoint)
{
	if (TrailFX)
	{
		const FVector Origin = GetMuzzleLocation();

		UParticleSystemComponent* TrailPSC = UGameplayStatics::SpawnEmitterAtLocation(this, TrailFX, Origin);
		if (TrailPSC)
		{
			TrailPSC->SetVectorParameter(TrailTargetParam, EndPoint);
		}
	}
}

void AShooterWeapon_Instant::GetLifetimeReplicatedProps( TArray< FLifetimeProperty > & OutLifetimeProps ) const
{
	Super::GetLifetimeReplicatedProps( OutLifetimeProps );

	DOREPLIFETIME_CONDITION( AShooterWeapon_Instant, HitNotify, COND_SkipOwner );
}
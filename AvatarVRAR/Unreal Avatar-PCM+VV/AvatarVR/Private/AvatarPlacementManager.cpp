#include "AvatarPlacementManager.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "MRUtilityKitRoom.h"

AAvatarPlacementManager::AAvatarPlacementManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AAvatarPlacementManager::BeginPlay()
{
	Super::BeginPlay();

	if (bPlaceOnBeginPlay)
	{
		FindSceneReferences();
		PlaceAvatarInFrontOfUser();
	}
}

AActor* AAvatarPlacementManager::FindActorByTag(FName InTag) const
{
	if (!GetWorld() || InTag.IsNone())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor) && Actor->ActorHasTag(InTag))
		{
			return Actor;
		}
	}

	return nullptr;
}

void AAvatarPlacementManager::CacheInitialAvatarTransform()
{
	if (!bInitialTransformCached && AvatarActor)
	{
		InitialAvatarTransform = AvatarActor->GetActorTransform();
		bInitialTransformCached = true;
	}
}

void AAvatarPlacementManager::FindSceneReferences()
{
	if (!AvatarActor)
	{
		AvatarActor = FindActorByTag(AvatarTag);
	}

	if (!UserActor)
	{
		UserActor = FindActorByTag(UserTag);
	}

	if (!UserActor)
	{
		ResolveRuntimeUserActor();
	}

	if (AvatarActor)
	{
		CacheInitialAvatarTransform();
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("AvatarPlacementManager - AvatarActor: %s | UserActor: %s"),
		AvatarActor ? *AvatarActor->GetName() : TEXT("NULL"),
		UserActor ? *UserActor->GetName() : TEXT("NULL")
	);
}

void AAvatarPlacementManager::ResolveRuntimeUserActor()
{
	if (UserActor)
	{
		return;
	}

	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		UserActor = PlayerPawn;
		UE_LOG(LogTemp, Log, TEXT("ResolveRuntimeUserActor - Using player pawn: %s"), *UserActor->GetName());
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("ResolveRuntimeUserActor - No player pawn found"));
}

void AAvatarPlacementManager::PlaceAvatarInFrontOfUser()
{
	if (!AvatarActor)
	{
		FindSceneReferences();
	}

	if (!UserActor)
	{
		ResolveRuntimeUserActor();
	}

	if (!AvatarActor || !UserActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceAvatarInFrontOfUser - Missing AvatarActor or UserActor"));
		return;
	}

	CacheInitialAvatarTransform();

	const FVector UserLocation = UserActor->GetActorLocation();

	FVector UserForward = UserActor->GetActorForwardVector();
	UserForward.Z = 0.0f;

	if (!UserForward.Normalize())
	{
		UserForward = FVector::ForwardVector;
	}

	FVector NewLocation = UserLocation + (UserForward * DistanceFromUser);

	// Mantiene la quota corrente dell'avatar come fallback "manuale"
	NewLocation.Z = AvatarActor->GetActorLocation().Z + HeightOffset;

	FVector LookDirection = UserLocation - NewLocation;
	if (bYawOnly)
	{
		LookDirection.Z = 0.0f;
	}

	FRotator NewRotation = UKismetMathLibrary::MakeRotFromX(LookDirection);

	if (bYawOnly)
	{
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
	}

	NewRotation.Yaw += FacingYawOffset;

	AvatarActor->SetActorLocationAndRotation(NewLocation, NewRotation);

	UE_LOG(LogTemp, Log, TEXT("PlaceAvatarInFrontOfUser - Avatar moved to X=%f Y=%f Z=%f"),
		NewLocation.X, NewLocation.Y, NewLocation.Z);
}

void AAvatarPlacementManager::ResetAvatarTransform()
{
	if (!AvatarActor)
	{
		FindSceneReferences();
	}

	if (!AvatarActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("ResetAvatarTransform - Missing AvatarActor"));
		return;
	}

	if (!bInitialTransformCached)
	{
		CacheInitialAvatarTransform();
	}

	if (!bInitialTransformCached)
	{
		UE_LOG(LogTemp, Warning, TEXT("ResetAvatarTransform - Initial transform not cached"));
		return;
	}

	AvatarActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	AvatarActor->SetActorTransform(InitialAvatarTransform);

	UE_LOG(LogTemp, Log, TEXT("ResetAvatarTransform - Avatar reset"));
}

void AAvatarPlacementManager::PlaceAvatarUsingFloorZ(float FloorZ)
{
	if (!AvatarActor)
	{
		FindSceneReferences();
	}

	if (!UserActor)
	{
		ResolveRuntimeUserActor();
	}

	if (!AvatarActor || !UserActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceAvatarUsingFloorZ - Missing AvatarActor or UserActor"));
		return;
	}

	CacheInitialAvatarTransform();

	const FVector UserLocation = UserActor->GetActorLocation();

	FVector UserForward = UserActor->GetActorForwardVector();
	UserForward.Z = 0.0f;

	if (!UserForward.Normalize())
	{
		UserForward = FVector::ForwardVector;
	}

	FVector NewLocation = UserLocation + (UserForward * DistanceFromUser);
	NewLocation.Z = FloorZ + HeightOffset;

	FVector LookDirection = UserLocation - NewLocation;
	if (bYawOnly)
	{
		LookDirection.Z = 0.0f;
	}

	FRotator NewRotation = UKismetMathLibrary::MakeRotFromX(LookDirection);

	if (bYawOnly)
	{
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
	}

	NewRotation.Yaw += FacingYawOffset;

	AvatarActor->SetActorLocationAndRotation(NewLocation, NewRotation);

	UE_LOG(LogTemp, Log, TEXT("PlaceAvatarUsingFloorZ - Avatar moved to %s using FloorZ=%f"),
		*NewLocation.ToString(), FloorZ);
}

void AAvatarPlacementManager::PlaceAvatarUsingFloorAnchor(AActor* FloorAnchorActor)
{
	if (!AvatarActor)
	{
		FindSceneReferences();
	}

	if (!UserActor)
	{
		ResolveRuntimeUserActor();
	}

	if (!AvatarActor || !UserActor || !FloorAnchorActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceAvatarUsingFloorAnchor - Missing AvatarActor, UserActor or FloorAnchorActor"));
		return;
	}

	CacheInitialAvatarTransform();

	const FVector UserLocation = UserActor->GetActorLocation();

	FVector UserForward = UserActor->GetActorForwardVector();
	UserForward.Z = 0.0f;

	if (!UserForward.Normalize())
	{
		UserForward = FVector::ForwardVector;
	}

	FVector NewLocation = UserLocation + (UserForward * DistanceFromUser);
	NewLocation.Z = FloorAnchorActor->GetActorLocation().Z + HeightOffset;

	FVector LookDirection = UserLocation - NewLocation;
	if (bYawOnly)
	{
		LookDirection.Z = 0.0f;
	}

	FRotator NewRotation = UKismetMathLibrary::MakeRotFromX(LookDirection);

	if (bYawOnly)
	{
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
	}

	NewRotation.Yaw += FacingYawOffset;

	AvatarActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	AvatarActor->SetActorLocationAndRotation(NewLocation, NewRotation);

	if (bAttachAvatarToFloorAnchor)
	{
		AvatarActor->AttachToActor(FloorAnchorActor, FAttachmentTransformRules::KeepWorldTransform);
	}

	UE_LOG(LogTemp, Log, TEXT("PlaceAvatarUsingFloorAnchor - Avatar moved using floor anchor %s"),
		*FloorAnchorActor->GetName());
}

bool AAvatarPlacementManager::PlaceAvatarNearUserAvoidingObstacles(AActor* RoomActor, AActor* FloorAnchorActor)
{
	if (!AvatarActor)
	{
		FindSceneReferences();
	}

	if (!UserActor)
	{
		ResolveRuntimeUserActor();
	}

	if (!AvatarActor || !UserActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceAvatarNearUserAvoidingObstacles - Missing AvatarActor or UserActor"));
		return false;
	}

	AMRUKRoom* Room = Cast<AMRUKRoom>(RoomActor);
	if (!Room)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceAvatarNearUserAvoidingObstacles - Invalid room, fallback"));
		if (FloorAnchorActor)
		{
			PlaceAvatarUsingFloorAnchor(FloorAnchorActor);
			return true;
		}

		PlaceAvatarInFrontOfUser();
		return false;
	}

	CacheInitialAvatarTransform();

	const FVector UserLocation = UserActor->GetActorLocation();

	FVector UserForward = UserActor->GetActorForwardVector();
	UserForward.Z = 0.0f;
	if (!UserForward.Normalize())
	{
		UserForward = FVector::ForwardVector;
	}

	FVector UserRight = FVector::CrossProduct(FVector::UpVector, UserForward);
	UserRight.Z = 0.0f;
	if (!UserRight.Normalize())
	{
		UserRight = FVector::RightVector;
	}

	float FloorZ = AvatarActor->GetActorLocation().Z + HeightOffset;
	if (FloorAnchorActor)
	{
		FloorZ = FloorAnchorActor->GetActorLocation().Z + HeightOffset;
	}

	bool bFoundFreeSpot = false;
	FVector BestLocation = FVector::ZeroVector;

	TArray<float> DistanceOffsets;
	DistanceOffsets.Add(0.0f);
	for (int32 Step = 1; Step <= MaxDistanceProbeSteps; ++Step)
	{
		const float Delta = DistanceProbeStepCm * Step;
		DistanceOffsets.Add(-Delta);
		DistanceOffsets.Add(Delta);
	}

	TArray<float> LateralOffsets;
	LateralOffsets.Add(0.0f);
	for (int32 Step = 1; Step <= MaxLateralProbeSteps; ++Step)
	{
		const float Delta = LateralProbeStepCm * Step;
		LateralOffsets.Add(Delta);
		LateralOffsets.Add(-Delta);
	}

	for (float DistanceOffset : DistanceOffsets)
	{
		const float ProbeDistance = DistanceFromUser + DistanceOffset;

		for (float LateralOffset : LateralOffsets)
		{
			FVector Candidate = UserLocation
				+ (UserForward * ProbeDistance)
				+ (UserRight * LateralOffset);

			Candidate.Z = FloorZ;

			const bool bInsideRoom = Room->IsPositionInRoom(Candidate, true);
			if (!bInsideRoom)
			{
				continue;
			}

			auto* BlockingVolume = Room->IsPositionInSceneVolume(Candidate, true, OccupancyToleranceCm);
			if (BlockingVolume != nullptr)
			{
				continue;
			}

			BestLocation = Candidate;
			bFoundFreeSpot = true;
			break;
		}

		if (bFoundFreeSpot)
		{
			break;
		}
	}

	if (!bFoundFreeSpot)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceAvatarNearUserAvoidingObstacles - No free spot found, fallback"));

		if (FloorAnchorActor)
		{
			PlaceAvatarUsingFloorAnchor(FloorAnchorActor);
			return true;
		}

		PlaceAvatarInFrontOfUser();
		return false;
	}

	FVector LookDirection = UserLocation - BestLocation;
	if (bYawOnly)
	{
		LookDirection.Z = 0.0f;
	}

	FRotator NewRotation = UKismetMathLibrary::MakeRotFromX(LookDirection);

	if (bYawOnly)
	{
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
	}

	NewRotation.Yaw += FacingYawOffset;

	AvatarActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	AvatarActor->SetActorLocationAndRotation(BestLocation, NewRotation);

	if (bAttachAvatarToFloorAnchor && FloorAnchorActor)
	{
		AvatarActor->AttachToActor(FloorAnchorActor, FAttachmentTransformRules::KeepWorldTransform);
	}

	UE_LOG(LogTemp, Log, TEXT("PlaceAvatarNearUserAvoidingObstacles - Avatar moved to free spot %s"),
		*BestLocation.ToString());

	return true;
}
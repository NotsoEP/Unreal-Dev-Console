// Copyright Notso Entertaining Productions. All Rights Reserved.

#include "DeveloperDebugConsole.h"

#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"
#include "TimerManager.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

ADeveloperDebugConsole::ADeveloperDebugConsole()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void ADeveloperDebugConsole::BeginPlay()
{
	Super::BeginPlay();

	if (bRegisterConsoleCommands && !IsReferencedAsTeleportMarker())
	{
		RegisterCommands();
	}
}

void ADeveloperDebugConsole::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bGodModeEnabled)
	{
		RefreshGodMode();
	}
}

void ADeveloperDebugConsole::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetGodModeEnabled(false);
	FinishDeferredGroundSnap(true);
	ClearPendingTeleport();
	UnregisterCommands();
	Super::EndPlay(EndPlayReason);
}

bool ADeveloperDebugConsole::TeleportPlayerToNamedLocation(FName LocationName)
{
	FTransform Destination;
	if (!TryFindTeleportLocation(LocationName, Destination))
	{
		UE_LOG(LogTemp, Warning, TEXT("Debug teleport location '%s' was not found."), *LocationName.ToString());
		return false;
	}

	return StartStreamingAwareTeleport(LocationName, Destination);
}

TArray<FName> ADeveloperDebugConsole::GetTeleportLocationNames() const
{
	TArray<FName> Names;
	for (const TObjectPtr<AActor>& MarkerActor : TeleportMarkerActors)
	{
		if (!MarkerActor)
		{
			continue;
		}

		const FString Label = MarkerActor->GetActorNameOrLabel();
		if (!Label.IsEmpty())
		{
			Names.AddUnique(FName(*Label));
		}

		for (const FName& Tag : MarkerActor->Tags)
		{
			if (!Tag.IsNone())
			{
				Names.AddUnique(Tag);
			}
		}
	}

	for (const TPair<FName, FTransform>& Pair : NamedTeleportLocations)
	{
		if (!Pair.Key.IsNone())
		{
			Names.AddUnique(Pair.Key);
		}
	}

	Names.Sort([](const FName& Left, const FName& Right)
	{
		return Left.LexicalLess(Right);
	});
	return Names;
}

void ADeveloperDebugConsole::RegisterCommands()
{
#if UE_BUILD_SHIPPING
	if (!bEnableInShippingBuilds)
	{
		return;
	}
#endif

	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	if (ConsoleManager.FindConsoleObject(TEXT("dev.tp"), false) ||
		ConsoleManager.FindConsoleObject(TEXT("dev.teleport"), false) ||
		ConsoleManager.FindConsoleObject(TEXT("dev.listteleports"), false) ||
		ConsoleManager.FindConsoleObject(TEXT("dev.god"), false) ||
		ConsoleManager.FindConsoleObject(TEXT("dev.stat"), false) ||
		ConsoleManager.FindConsoleObject(TEXT("dev.cmd"), false))
	{
		UE_LOG(LogTemp, Warning, TEXT("Developer debug console commands are already registered."));
		return;
	}

	RegisteredRawConsoleObjects.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("dev.tp"),
		TEXT("Teleport the local player to a named debug location. Example: dev.tp motel"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &ADeveloperDebugConsole::HandleTeleportCommand),
		ECVF_Default));

	RegisteredRawConsoleObjects.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("dev.teleport"),
		TEXT("Teleport the local player to a named debug location. Example: dev.teleport gas station"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &ADeveloperDebugConsole::HandleTeleportCommand),
		ECVF_Default));

	RegisteredRawConsoleObjects.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("dev.listteleports"),
		TEXT("List available named debug teleport locations."),
		FConsoleCommandDelegate::CreateUObject(this, &ADeveloperDebugConsole::HandleListTeleportsCommand),
		ECVF_Default));

	RegisteredRawConsoleObjects.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("dev.god"),
		TEXT("Toggle local god mode. Optional args: on/off"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &ADeveloperDebugConsole::HandleGodCommand),
		ECVF_Default));

	RegisteredRawConsoleObjects.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("dev.stat"),
		TEXT("Set a local player stat by percentage or normalized value. Example: dev.stat stamina 50"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &ADeveloperDebugConsole::HandleStatCommand),
		ECVF_Default));

	RegisteredRawConsoleObjects.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("dev.cmd"),
		TEXT("Forward a custom debug command to the DeveloperDebugConsole Blueprint."),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &ADeveloperDebugConsole::HandleGenericCommand),
		ECVF_Default));
}

void ADeveloperDebugConsole::UnregisterCommands()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject* ConsoleObject : RegisteredRawConsoleObjects)
	{
		if (ConsoleObject)
		{
			ConsoleManager.UnregisterConsoleObject(ConsoleObject, false);
		}
	}
	RegisteredRawConsoleObjects.Reset();
}

void ADeveloperDebugConsole::HandleTeleportCommand(const TArray<FString>& Arguments)
{
	if (Arguments.Num() == 0)
	{
		HandleListTeleportsCommand();
		return;
	}

	const FString RequestedLocation = FString::Join(Arguments, TEXT(" "));
	TeleportPlayerToNamedLocation(FName(*RequestedLocation));
}

void ADeveloperDebugConsole::HandleGodCommand(const TArray<FString>& Arguments)
{
	if (Arguments.Num() == 0)
	{
		SetGodModeEnabled(!bGodModeEnabled);
		return;
	}

	const FString Mode = NormalizeTeleportName(Arguments[0]);
	if (Mode == TEXT("on") || Mode == TEXT("true") || Mode == TEXT("1"))
	{
		SetGodModeEnabled(true);
		return;
	}

	if (Mode == TEXT("off") || Mode == TEXT("false") || Mode == TEXT("0"))
	{
		SetGodModeEnabled(false);
		return;
	}

	OutputConsoleText(TEXT("Usage: dev.god [on|off]"));
}

void ADeveloperDebugConsole::HandleStatCommand(const TArray<FString>& Arguments)
{
	if (Arguments.Num() < 2)
	{
		OutputConsoleText(TEXT("Usage: dev.stat <health|stamina> <percent or normalized value>"));
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		OutputConsoleText(TEXT("dev.stat failed: no local player pawn."));
		return;
	}

	const FString RequestedStat = Arguments[0];
	float NormalizedValue = 0.0f;
	if (!TryParseNormalizedStatValue(Arguments[1], NormalizedValue))
	{
		OutputConsoleText(FString::Printf(TEXT("dev.stat failed: '%s' is not a valid stat value."), *Arguments[1]));
		return;
	}

	FString AppliedPath;
	double NewValue = 0.0;
	double MaxValue = 0.0;
	if (TryApplyStatToObject(Pawn, RequestedStat, NormalizedValue, AppliedPath, NewValue, MaxValue))
	{
		OutputConsoleText(FString::Printf(
			TEXT("%s set to %.2f / %.2f on %s"),
			*RequestedStat,
			NewValue,
			MaxValue,
			*AppliedPath));
		return;
	}

	TArray<UActorComponent*> Components;
	Pawn->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}

		if (TryApplyStatToObject(Component, RequestedStat, NormalizedValue, AppliedPath, NewValue, MaxValue))
		{
			OutputConsoleText(FString::Printf(
				TEXT("%s set to %.2f / %.2f on %s"),
				*RequestedStat,
				NewValue,
				MaxValue,
				*AppliedPath));
			return;
		}
	}

	OutputConsoleText(FString::Printf(
		TEXT("dev.stat failed: could not find %s and matching max property on the local pawn or its components."),
		*RequestedStat));
}

void ADeveloperDebugConsole::HandleListTeleportsCommand()
{
	const TArray<FName> Names = GetTeleportLocationNames();
	if (Names.Num() == 0)
	{
		OutputConsoleText(TEXT("No debug teleport locations are configured."));
		return;
	}

	OutputConsoleText(TEXT("Debug teleport locations:"));
	for (const FName& Name : Names)
	{
		OutputConsoleText(FString::Printf(TEXT("  %s"), *Name.ToString()));
	}
}

void ADeveloperDebugConsole::HandleGenericCommand(const TArray<FString>& Arguments)
{
	if (Arguments.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Usage: dev.cmd <CommandName> [args...]"));
		return;
	}

	TArray<FString> RemainingArguments = Arguments;
	const FName Command(*RemainingArguments[0]);
	RemainingArguments.RemoveAt(0);

	OnDebugCommand(Command, RemainingArguments, FString::Join(Arguments, TEXT(" ")));
}

bool ADeveloperDebugConsole::GetStreamingSource(FWorldPartitionStreamingSource& StreamingSource) const
{
	if (PendingTeleportName.IsNone())
	{
		return false;
	}

	StreamingSource = FWorldPartitionStreamingSource(
		PendingTeleportName,
		PendingTeleportDestination.GetLocation(),
		PendingTeleportDestination.GetRotation().Rotator(),
		EStreamingSourceTargetState::Activated,
		true,
		EStreamingSourcePriority::Highest,
		false);
	return true;
}

bool ADeveloperDebugConsole::TryFindTeleportLocation(FName RequestedName, FTransform& OutTransform) const
{
	for (const TObjectPtr<AActor>& MarkerActor : TeleportMarkerActors)
	{
		if (!MarkerActor)
		{
			continue;
		}

		const FString RequestedText = RequestedName.ToString();
		if (DoesTeleportNameMatch(MarkerActor->GetActorNameOrLabel(), RequestedText) ||
			DoesTeleportNameMatch(MarkerActor->GetName(), RequestedText))
		{
			OutTransform = MarkerActor->GetActorTransform();
			return true;
		}

		for (const FName& Tag : MarkerActor->Tags)
		{
			if (DoesTeleportNameMatch(Tag.ToString(), RequestedText))
			{
				OutTransform = MarkerActor->GetActorTransform();
				return true;
			}
		}
	}

	if (const FTransform* ExactTransform = NamedTeleportLocations.Find(RequestedName))
	{
		OutTransform = *ExactTransform;
		return true;
	}

	const FString RequestedText = RequestedName.ToString();
	const FString UnderscoreText = RequestedText.Replace(TEXT(" "), TEXT("_"));
	const FString SpaceText = RequestedText.Replace(TEXT("_"), TEXT(" "));

	for (const TPair<FName, FTransform>& Pair : NamedTeleportLocations)
	{
		const FString CandidateText = Pair.Key.ToString();
		if (CandidateText.Equals(RequestedText, ESearchCase::IgnoreCase) ||
			CandidateText.Equals(UnderscoreText, ESearchCase::IgnoreCase) ||
			CandidateText.Equals(SpaceText, ESearchCase::IgnoreCase))
		{
			OutTransform = Pair.Value;
			return true;
		}
	}

	return false;
}

bool ADeveloperDebugConsole::StartStreamingAwareTeleport(FName RequestedName, const FTransform& Destination)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	OnDebugTeleportPauseExternalSystems();

	if (!bUseWorldStreaming)
	{
		const bool bTeleported = CompleteTeleport(Destination, RequestedName);
		if (!bTeleported)
		{
			OnDebugTeleportResumeExternalSystems(0.0f);
		}
		return bTeleported;
	}

	UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
	if (!WorldPartitionSubsystem)
	{
		const bool bTeleported = CompleteTeleport(Destination, RequestedName);
		if (!bTeleported)
		{
			OnDebugTeleportResumeExternalSystems(0.0f);
		}
		return bTeleported;
	}

	ClearPendingTeleport();

	PendingTeleportName = RequestedName;
	PendingTeleportDestination = Destination;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			FMath::Max(0.5f, TeleportStreamingTimeoutSeconds),
			FColor::Yellow,
			FString::Printf(TEXT("Loading %s..."), *RequestedName.ToString()));
	}

	UE_LOG(LogTemp, Display, TEXT("Debug teleport '%s' is waiting for streamed world data."), *RequestedName.ToString());

	WorldPartitionSubsystem->RegisterStreamingSourceProvider(this);
	World->FlushLevelStreaming(EFlushLevelStreamingType::Visibility);

	if (bBlockOnWorldPartitionStreaming)
	{
		World->BlockTillLevelStreamingCompleted();
	}

	const bool bStreamingReady = WorldPartitionSubsystem->IsStreamingCompleted(this);
	if (!bStreamingReady && bRequireFullyStreamedTeleport)
	{
		ClearPendingTeleport();
		OnDebugTeleportResumeExternalSystems(0.0f);
		UE_LOG(LogTemp, Warning, TEXT("Debug teleport '%s' did not reach a fully streamed state."), *RequestedName.ToString());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				2.0f,
				FColor::Red,
				FString::Printf(TEXT("Teleport failed: %s is not fully streamed."), *RequestedName.ToString()));
		}
		return false;
	}

	if (!bStreamingReady)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Debug teleport '%s' is proceeding before world partition reports a fully streamed state."), *RequestedName.ToString());
	}

	const bool bTeleported = CompleteTeleport(Destination, RequestedName);
	if (!bTeleported)
	{
		OnDebugTeleportResumeExternalSystems(0.0f);
	}
	ClearPendingTeleport();
	return bTeleported;
}

bool ADeveloperDebugConsole::CompleteTeleport(const FTransform& Destination, FName RequestedName)
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("Debug teleport failed because there is no local player pawn."));
		return false;
	}

	float PawnHeightOffset = 88.0f;
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			PawnHeightOffset = Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	const FVector DestinationLocation = Destination.GetLocation();
	FHitResult GroundHit;
	const FVector TraceStart = DestinationLocation + FVector(0.0f, 0.0f, 5000.0f);
	const FVector TraceEnd = DestinationLocation - FVector(0.0f, 0.0f, 5000.0f);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DebugTeleportGroundCheck), false, Pawn);
	const bool bFoundGround = World->LineTraceSingleByChannel(
		GroundHit,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		QueryParams);

	FVector Location = DestinationLocation + FVector(0.0f, 0.0f, PawnHeightOffset + 10.0f);
	if (bFoundGround)
	{
		Location = GroundHit.Location + FVector(0.0f, 0.0f, PawnHeightOffset + 10.0f);
	}
	else if (bRequireGroundCollisionForTeleport)
	{
		UE_LOG(LogTemp, Warning, TEXT("Debug teleport '%s' was cancelled because no loaded ground collision was found near the destination."), *RequestedName.ToString());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				2.0f,
				FColor::Red,
				FString::Printf(TEXT("Teleport failed: no loaded ground found for %s."), *RequestedName.ToString()));
		}
		return false;
	}
	else
	{
		ACharacter* Character = Cast<ACharacter>(Pawn);
		if (bUseDeferredGroundSnap && Character)
		{
			StartDeferredGroundSnap(Character, DestinationLocation, Destination.GetRotation().Rotator(), PawnHeightOffset, RequestedName);
			return true;
		}

		UE_LOG(LogTemp, Warning, TEXT("Debug teleport '%s' is proceeding without loaded ground collision; using destination transform as fallback."), *RequestedName.ToString());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				1.5f,
				FColor::Yellow,
				FString::Printf(TEXT("Teleporting to %s before ground collision is loaded"), *RequestedName.ToString()));
		}
	}

	const FRotator Rotation = Destination.GetRotation().Rotator();
	const bool bTeleported = Pawn->TeleportTo(Location, Rotation, false, true);
	if (bTeleported && PlayerController)
	{
		PlayerController->SetControlRotation(Rotation);
		OnDebugTeleportResumeExternalSystems(1.0f);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Debug teleport '%s' failed during Pawn->TeleportTo."), *RequestedName.ToString());
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			bTeleported ? 1.5f : 2.0f,
			bTeleported ? FColor::Green : FColor::Red,
			bTeleported
				? FString::Printf(TEXT("Teleported to %s"), *RequestedName.ToString())
				: FString::Printf(TEXT("Teleport failed: %s"), *RequestedName.ToString()));
	}

	return bTeleported;
}

void ADeveloperDebugConsole::ClearPendingTeleport()
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>())
		{
			WorldPartitionSubsystem->UnregisterStreamingSourceProvider(this);
		}
	}

	PendingTeleportDestination = FTransform::Identity;
	PendingTeleportName = NAME_None;
}

void ADeveloperDebugConsole::StartDeferredGroundSnap(ACharacter* Character, const FVector& DestinationLocation, const FRotator& Rotation, float PawnHeightOffset, FName RequestedName)
{
	if (!Character)
	{
		return;
	}

	UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();
	if (!CharacterMovement)
	{
		return;
	}

	if (!bDeferredGroundSnapCapturedMovementState)
	{
		DeferredGroundSnapSavedGravityScale = CharacterMovement->GravityScale;
		DeferredGroundSnapSavedMovementMode = static_cast<uint8>(CharacterMovement->MovementMode);
		bDeferredGroundSnapCapturedMovementState = true;
	}

	const float HeightOffset = FMath::Max(DeferredGroundSnapHeight, PawnHeightOffset + 100.0f);
	const FVector AirlockLocation = DestinationLocation + FVector(0.0f, 0.0f, HeightOffset);
	const bool bTeleported = Character->TeleportTo(AirlockLocation, Rotation, false, true);
	if (!bTeleported)
	{
		UE_LOG(LogTemp, Warning, TEXT("Debug teleport '%s' failed while moving to deferred ground snap airlock position."), *RequestedName.ToString());
		return;
	}

	CharacterMovement->StopMovementImmediately();
	CharacterMovement->GravityScale = 0.0f;
	CharacterMovement->SetMovementMode(MOVE_Flying);

	if (APlayerController* PlayerController = Cast<APlayerController>(Character->GetController()))
	{
		PlayerController->SetControlRotation(Rotation);
	}

	DeferredGroundSnapCharacter = Character;
	DeferredGroundSnapDestination = DestinationLocation;
	DeferredGroundSnapPawnHeightOffset = PawnHeightOffset;
	DeferredGroundSnapName = RequestedName;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DeferredGroundSnapTimerHandle,
			this,
			&ADeveloperDebugConsole::HandleDeferredGroundSnapTick,
			FMath::Max(0.01f, DeferredGroundSnapIntervalSeconds),
			true);
	}

	UE_LOG(LogTemp, Warning, TEXT("Debug teleport '%s' is waiting for ground collision and will snap down once it loads."), *RequestedName.ToString());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			2.0f,
			FColor::Yellow,
			FString::Printf(TEXT("Waiting for ground collision at %s"), *RequestedName.ToString()));
	}
}

void ADeveloperDebugConsole::HandleDeferredGroundSnapTick()
{
	ACharacter* Character = DeferredGroundSnapCharacter.Get();
	UWorld* World = GetWorld();
	if (!Character || !World)
	{
		FinishDeferredGroundSnap(false);
		return;
	}

	FHitResult GroundHit;
	const FVector TraceStart = DeferredGroundSnapDestination + FVector(0.0f, 0.0f, 5000.0f);
	const FVector TraceEnd = DeferredGroundSnapDestination - FVector(0.0f, 0.0f, 5000.0f);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DebugTeleportGroundCheck), false, Character);
	const bool bFoundGround = World->LineTraceSingleByChannel(
		GroundHit,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		QueryParams);

	if (!bFoundGround)
	{
		return;
	}

	const FVector GroundedLocation = GroundHit.Location + FVector(0.0f, 0.0f, DeferredGroundSnapPawnHeightOffset + 10.0f);
	const FRotator Rotation = Character->GetActorRotation();
	const bool bTeleported = Character->TeleportTo(GroundedLocation, Rotation, false, true);

	if (bTeleported)
	{
		UE_LOG(LogTemp, Display, TEXT("Debug teleport '%s' snapped to loaded ground collision."), *DeferredGroundSnapName.ToString());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				1.5f,
				FColor::Green,
				FString::Printf(TEXT("Teleported to %s"), *DeferredGroundSnapName.ToString()));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Debug teleport '%s' found ground collision but failed during deferred snap."), *DeferredGroundSnapName.ToString());
	}

	FinishDeferredGroundSnap(bTeleported);
}

void ADeveloperDebugConsole::FinishDeferredGroundSnap(bool bRestoreMovement)
{
	if (bRestoreMovement)
	{
		OnDebugTeleportResumeExternalSystems(1.0f);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredGroundSnapTimerHandle);
	}

	ACharacter* Character = DeferredGroundSnapCharacter.Get();
	if (bRestoreMovement && Character)
	{
		if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
		{
			CharacterMovement->GravityScale = DeferredGroundSnapSavedGravityScale;
			CharacterMovement->SetMovementMode(static_cast<EMovementMode>(DeferredGroundSnapSavedMovementMode));
		}
	}

	if (bRestoreMovement)
	{
		bDeferredGroundSnapCapturedMovementState = false;
	}

	DeferredGroundSnapCharacter.Reset();
	DeferredGroundSnapDestination = FVector::ZeroVector;
	DeferredGroundSnapPawnHeightOffset = 0.0f;
	DeferredGroundSnapName = NAME_None;
}

void ADeveloperDebugConsole::SetGodModeEnabled(bool bEnabled)
{
	bGodModeEnabled = bEnabled;

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (Pawn)
	{
		Pawn->SetCanBeDamaged(!bEnabled);
	}

	if (!World)
	{
		return;
	}

	if (bEnabled)
	{
		RefreshGodMode();
		SetActorTickEnabled(true);
		OutputConsoleText(TEXT("God mode enabled"));
	}
	else
	{
		World->GetTimerManager().ClearTimer(GodModeRefreshTimerHandle);
		SetActorTickEnabled(false);
		OutputConsoleText(TEXT("God mode disabled"));
	}
}

void ADeveloperDebugConsole::RefreshGodMode()
{
	if (!bGodModeEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	Pawn->SetCanBeDamaged(false);

	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
		{
			if (CharacterMovement->IsFalling())
			{
				FVector Velocity = CharacterMovement->Velocity;
				Velocity.Z = FMath::Max(Velocity.Z, -FMath::Max(0.0f, GodModeMaxDownwardSpeed));
				CharacterMovement->Velocity = Velocity;
			}
		}
	}

	FString AppliedPath;
	double NewValue = 0.0;
	double MaxValue = 0.0;
	TryApplyStatToObject(Pawn, TEXT("health"), 1.0f, AppliedPath, NewValue, MaxValue);
	TryApplyStatToObject(Pawn, TEXT("stamina"), 1.0f, AppliedPath, NewValue, MaxValue);

	TArray<UActorComponent*> Components;
	Pawn->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}

		TryApplyStatToObject(Component, TEXT("health"), 1.0f, AppliedPath, NewValue, MaxValue);
		TryApplyStatToObject(Component, TEXT("stamina"), 1.0f, AppliedPath, NewValue, MaxValue);
	}
}

bool ADeveloperDebugConsole::IsReferencedAsTeleportMarker() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (TActorIterator<ADeveloperDebugConsole> It(World); It; ++It)
	{
		const ADeveloperDebugConsole* OtherConsole = *It;
		if (!OtherConsole || OtherConsole == this)
		{
			continue;
		}

		for (const TObjectPtr<AActor>& MarkerActor : OtherConsole->TeleportMarkerActors)
		{
			if (MarkerActor == this)
			{
				return true;
			}
		}
	}

	return false;
}

void ADeveloperDebugConsole::OutputConsoleText(const FString& Text) const
{
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->ViewportConsole)
	{
		GEngine->GameViewport->ViewportConsole->OutputText(Text);
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("%s"), *Text);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan, Text);
	}
}

bool ADeveloperDebugConsole::TryParseNormalizedStatValue(const FString& RawValue, float& OutNormalizedValue)
{
	if (RawValue.IsEmpty())
	{
		return false;
	}

	const double ParsedValue = FCString::Atod(*RawValue);
	if (!FMath::IsFinite(ParsedValue))
	{
		return false;
	}

	if (ParsedValue < 0.0)
	{
		return false;
	}

	OutNormalizedValue = ParsedValue <= 1.0
		? static_cast<float>(ParsedValue)
		: static_cast<float>(ParsedValue / 100.0);
	OutNormalizedValue = FMath::Clamp(OutNormalizedValue, 0.0f, 1.0f);
	return true;
}

bool ADeveloperDebugConsole::TryGetNumericPropertyValue(const UObject* Object, const FProperty* Property, double& OutValue)
{
	if (!Object || !Property)
	{
		return false;
	}

	const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	if (const FFloatProperty* FloatProperty = CastField<const FFloatProperty>(Property))
	{
		OutValue = FloatProperty->GetPropertyValue(ValuePtr);
		return true;
	}

	if (const FDoubleProperty* DoubleProperty = CastField<const FDoubleProperty>(Property))
	{
		OutValue = DoubleProperty->GetPropertyValue(ValuePtr);
		return true;
	}

	if (const FIntProperty* IntProperty = CastField<const FIntProperty>(Property))
	{
		OutValue = static_cast<double>(IntProperty->GetPropertyValue(ValuePtr));
		return true;
	}

	if (const FInt64Property* Int64Property = CastField<const FInt64Property>(Property))
	{
		OutValue = static_cast<double>(Int64Property->GetPropertyValue(ValuePtr));
		return true;
	}

	if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
	{
		OutValue = static_cast<double>(ByteProperty->GetPropertyValue(ValuePtr));
		return true;
	}

	return false;
}

bool ADeveloperDebugConsole::TrySetNumericPropertyValue(UObject* Object, FProperty* Property, double InValue)
{
	if (!Object || !Property)
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
	{
		FloatProperty->SetPropertyValue(ValuePtr, static_cast<float>(InValue));
		return true;
	}

	if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
	{
		DoubleProperty->SetPropertyValue(ValuePtr, InValue);
		return true;
	}

	if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
	{
		IntProperty->SetPropertyValue(ValuePtr, FMath::RoundToInt(InValue));
		return true;
	}

	if (FInt64Property* Int64Property = CastField<FInt64Property>(Property))
	{
		Int64Property->SetPropertyValue(ValuePtr, static_cast<int64>(FMath::RoundToInt64(InValue)));
		return true;
	}

	if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		ByteProperty->SetPropertyValue(ValuePtr, static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(InValue), 0, 255)));
		return true;
	}

	return false;
}

FProperty* ADeveloperDebugConsole::FindNamedNumericProperty(UObject* Object, const TArray<FString>& CandidateNames)
{
	if (!Object)
	{
		return nullptr;
	}

	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property)
		{
			continue;
		}

		const bool bNumeric = Property->IsA<FFloatProperty>() ||
			Property->IsA<FDoubleProperty>() ||
			Property->IsA<FIntProperty>() ||
			Property->IsA<FInt64Property>() ||
			Property->IsA<FByteProperty>();
		if (!bNumeric)
		{
			continue;
		}

		const FString PropertyName = NormalizeTeleportName(Property->GetName());
		for (const FString& CandidateName : CandidateNames)
		{
			if (PropertyName == NormalizeTeleportName(CandidateName))
			{
				return Property;
			}
		}
	}

	return nullptr;
}

bool ADeveloperDebugConsole::TryApplyStatToObject(UObject* Object, const FString& StatName, float NormalizedValue, FString& OutAppliedPath, double& OutNewValue, double& OutMaxValue) const
{
	if (!Object)
	{
		return false;
	}

	const FString NormalizedStatName = NormalizeTeleportName(StatName);
	TArray<FString> CurrentCandidates;
	TArray<FString> MaxCandidates;

	if (NormalizedStatName == TEXT("health"))
	{
		CurrentCandidates = { TEXT("Health"), TEXT("CurrentHealth"), TEXT("HealthCurrent") };
		MaxCandidates = { TEXT("MaxHealth"), TEXT("HealthMax"), TEXT("MaximumHealth") };
	}
	else if (NormalizedStatName == TEXT("stamina"))
	{
		CurrentCandidates = { TEXT("Stamina"), TEXT("CurrentStamina"), TEXT("StaminaCurrent") };
		MaxCandidates = { TEXT("MaxStamina"), TEXT("StaminaMax"), TEXT("MaximumStamina") };
	}
	else
	{
		return false;
	}

	FProperty* CurrentProperty = FindNamedNumericProperty(Object, CurrentCandidates);
	FProperty* MaxProperty = FindNamedNumericProperty(Object, MaxCandidates);
	if (!CurrentProperty || !MaxProperty)
	{
		return false;
	}

	double MaxValue = 0.0;
	if (!TryGetNumericPropertyValue(Object, MaxProperty, MaxValue) || MaxValue <= 0.0)
	{
		return false;
	}

	const double NewValue = FMath::Clamp(static_cast<double>(NormalizedValue) * MaxValue, 0.0, MaxValue);
	if (!TrySetNumericPropertyValue(Object, CurrentProperty, NewValue))
	{
		return false;
	}

	OutAppliedPath = Object->GetPathName();
	OutNewValue = NewValue;
	OutMaxValue = MaxValue;
	return true;
}

bool ADeveloperDebugConsole::DoesTeleportNameMatch(const FString& Candidate, const FString& Requested)
{
	if (Candidate.IsEmpty() || Requested.IsEmpty())
	{
		return false;
	}

	const FString RequestedUnderscore = Requested.Replace(TEXT(" "), TEXT("_"));
	const FString RequestedSpace = Requested.Replace(TEXT("_"), TEXT(" "));
	const FString NormalizedCandidate = NormalizeTeleportName(Candidate);
	const FString NormalizedRequested = NormalizeTeleportName(Requested);

	return Candidate.Equals(Requested, ESearchCase::IgnoreCase) ||
		Candidate.Equals(RequestedUnderscore, ESearchCase::IgnoreCase) ||
		Candidate.Equals(RequestedSpace, ESearchCase::IgnoreCase) ||
		(!NormalizedCandidate.IsEmpty() && NormalizedCandidate.Equals(NormalizedRequested, ESearchCase::IgnoreCase));
}

FString ADeveloperDebugConsole::NormalizeTeleportName(const FString& Value)
{
	FString Normalized;
	Normalized.Reserve(Value.Len());

	for (const TCHAR Character : Value)
	{
		if (FChar::IsAlnum(Character))
		{
			Normalized.AppendChar(FChar::ToLower(Character));
		}
	}

	return Normalized;
}

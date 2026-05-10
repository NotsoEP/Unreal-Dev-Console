// Copyright Notso Entertaining Productions. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "DeveloperDebugConsole.generated.h"

class IConsoleObject;
class ACharacter;
class UObject;
class FProperty;

UCLASS(Blueprintable, meta=(DisplayName="DeveloperDebugConsole"))
class FLOP_API ADeveloperDebugConsole : public AActor, public IWorldPartitionStreamingSourceProvider
{
	GENERATED_BODY()

public:
	ADeveloperDebugConsole();

	UFUNCTION(BlueprintCallable, Category="Developer Debug Console")
	bool TeleportPlayerToNamedLocation(FName LocationName);

	UFUNCTION(BlueprintCallable, Category="Developer Debug Console")
	TArray<FName> GetTeleportLocationNames() const;

	UFUNCTION(BlueprintImplementableEvent, Category="Developer Debug Console")
	void OnDebugCommand(FName Command, const TArray<FString>& Arguments, const FString& RawCommandLine);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& StreamingSource) const override;
	virtual const UObject* GetStreamingSourceOwner() const override { return this; }

	UFUNCTION(BlueprintImplementableEvent, Category="Developer Debug Console|Teleport")
	void OnDebugTeleportPauseExternalSystems();

	UFUNCTION(BlueprintImplementableEvent, Category="Developer Debug Console|Teleport")
	void OnDebugTeleportResumeExternalSystems(float DelaySeconds);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console")
	bool bRegisterConsoleCommands = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console")
	bool bEnableInShippingBuilds = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console")
	TMap<FName, FTransform> NamedTeleportLocations;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console")
	TArray<TObjectPtr<AActor>> TeleportMarkerActors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Teleport")
	bool bUseWorldStreaming = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Teleport")
	bool bBlockOnWorldPartitionStreaming = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Teleport")
	bool bRequireFullyStreamedTeleport = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Teleport")
	bool bRequireGroundCollisionForTeleport = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Teleport")
	bool bUseDeferredGroundSnap = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Teleport", meta=(ClampMin="100.0"))
	float DeferredGroundSnapHeight = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Teleport", meta=(ClampMin="0.01"))
	float DeferredGroundSnapIntervalSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Teleport")
	float TeleportStreamingTimeoutSeconds = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Stats", meta=(ClampMin="0.01"))
	float GodModeRefreshIntervalSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Developer Debug Console|Stats", meta=(ClampMin="0.0"))
	float GodModeMaxDownwardSpeed = 200.0f;

private:
	void RegisterCommands();
	void UnregisterCommands();
	void HandleTeleportCommand(const TArray<FString>& Arguments);
	void HandleGodCommand(const TArray<FString>& Arguments);
	void HandleStatCommand(const TArray<FString>& Arguments);
	void HandleListTeleportsCommand();
	void HandleGenericCommand(const TArray<FString>& Arguments);
	bool TryFindTeleportLocation(FName RequestedName, FTransform& OutTransform) const;
	bool StartStreamingAwareTeleport(FName RequestedName, const FTransform& Destination);
	bool CompleteTeleport(const FTransform& Destination, FName RequestedName);
	void ClearPendingTeleport();
	void StartDeferredGroundSnap(ACharacter* Character, const FVector& DestinationLocation, const FRotator& Rotation, float PawnHeightOffset, FName RequestedName);
	void HandleDeferredGroundSnapTick();
	void FinishDeferredGroundSnap(bool bRestoreMovement);
	void SetGodModeEnabled(bool bEnabled);
	void RefreshGodMode();
	bool IsReferencedAsTeleportMarker() const;
	void OutputConsoleText(const FString& Text) const;
	static bool TryParseNormalizedStatValue(const FString& RawValue, float& OutNormalizedValue);
	static bool TryGetNumericPropertyValue(const UObject* Object, const FProperty* Property, double& OutValue);
	static bool TrySetNumericPropertyValue(UObject* Object, FProperty* Property, double InValue);
	static FProperty* FindNamedNumericProperty(UObject* Object, const TArray<FString>& CandidateNames);
	bool TryApplyStatToObject(UObject* Object, const FString& StatName, float NormalizedValue, FString& OutAppliedPath, double& OutNewValue, double& OutMaxValue) const;
	static bool DoesTeleportNameMatch(const FString& Candidate, const FString& Requested);
	static FString NormalizeTeleportName(const FString& Value);

	TArray<IConsoleObject*> RegisteredRawConsoleObjects;
	FTransform PendingTeleportDestination;
	FName PendingTeleportName;
	TWeakObjectPtr<ACharacter> DeferredGroundSnapCharacter;
	FVector DeferredGroundSnapDestination = FVector::ZeroVector;
	FName DeferredGroundSnapName;
	float DeferredGroundSnapPawnHeightOffset = 0.0f;
	float DeferredGroundSnapSavedGravityScale = 1.0f;
	uint8 DeferredGroundSnapSavedMovementMode = 0;
	bool bDeferredGroundSnapCapturedMovementState = false;
	FTimerHandle DeferredGroundSnapTimerHandle;
	bool bGodModeEnabled = false;
	FTimerHandle GodModeRefreshTimerHandle;
};

// Copyright Notso Entertaining Productions. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DeveloperTeleportMarker.generated.h"

class USceneComponent;

UCLASS(Blueprintable, meta=(DisplayName="BP_LocationTPMarker"))
class FLOP_API ADeveloperTeleportMarker : public AActor
{
	GENERATED_BODY()

public:
	ADeveloperTeleportMarker();

protected:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Teleport Marker")
	TObjectPtr<USceneComponent> Root;
};

// Copyright Notso Entertaining Productions. All Rights Reserved.

#include "DeveloperTeleportMarker.h"

#include "Components/SceneComponent.h"

ADeveloperTeleportMarker::ADeveloperTeleportMarker()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

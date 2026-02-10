#pragma once

#include "CoreMinimal.h"
#include "MocapRecorderTypes.generated.h"

/**
 * One captured frame of mocap data.
 * Stored in skeleton bone order.
 */
USTRUCT()
struct FMocapFrame
{
    GENERATED_BODY()

    UPROPERTY()
    float Time = 0.f;

    UPROPERTY()
    TArray<FVector> Translations;

    UPROPERTY()
    TArray<FQuat> Rotations;

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TArray<FVector> HeadWorld;

    UPROPERTY()
    TArray<FVector> TailWorld;
#endif
};

// ------------------------------------------------------------
// Transform-only capture (bullets/casings/props)
// ------------------------------------------------------------
USTRUCT()
struct FMocapTransformFrame
{
    GENERATED_BODY()

    UPROPERTY()
    float Time = 0.f;

    // Full world transform (includes scale)
    UPROPERTY()
    FTransform World = FTransform::Identity;
};

#pragma once

#include "CoreMinimal.h"
#include "MocapCaptureMode.generated.h"

UENUM(BlueprintType)
enum class EMocapCaptureMode : uint8
{
    Skeletal UMETA(DisplayName = "Skeletal"),

    // Deprecated: VirtualMoCap is skeletal-only now.
    // Kept ONLY so old assets that serialized this value still load.
    TransformOnly UMETA(Hidden, DisplayName = "Transform Only (Deprecated)")
};


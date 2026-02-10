#pragma once

#include "CoreMinimal.h"

struct FReferenceSkeleton;

namespace MocapRecorderPoseUtils
{
    // Build component-space transforms from parent-relative local transforms.
    // Bone order is skeleton bone index order.
    void BuildComponentSpaceFromLocalPose(
        const FReferenceSkeleton& RefSkel,
        const TArray<FTransform>& LocalBySkel,
        TArray<FTransform>& OutComponentBySkel);
}

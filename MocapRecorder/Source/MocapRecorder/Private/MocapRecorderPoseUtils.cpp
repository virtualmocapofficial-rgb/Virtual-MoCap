#include "MocapRecorderPoseUtils.h"

#include "ReferenceSkeleton.h"

namespace MocapRecorderPoseUtils
{
    void BuildComponentSpaceFromLocalPose(
        const FReferenceSkeleton& RefSkel,
        const TArray<FTransform>& LocalBySkel,
        TArray<FTransform>& OutComponentBySkel)
    {
        const int32 Num = RefSkel.GetNum();
        OutComponentBySkel.SetNum(Num);

        for (int32 i = 0; i < Num; ++i)
        {
            const int32 Parent = RefSkel.GetParentIndex(i);

            if (Parent == INDEX_NONE)
            {
                OutComponentBySkel[i] =
                    LocalBySkel.IsValidIndex(i)
                    ? LocalBySkel[i]
                    : FTransform::Identity;
            }
            else
            {
                OutComponentBySkel[i] =
                    (LocalBySkel.IsValidIndex(i)
                        ? LocalBySkel[i]
                        : FTransform::Identity)
                    * OutComponentBySkel[Parent];
            }
        }
    }
}

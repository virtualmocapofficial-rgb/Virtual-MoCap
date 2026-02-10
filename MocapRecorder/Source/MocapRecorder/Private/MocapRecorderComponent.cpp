#include "MocapRecorderComponent.h"
#include "MocapRecorderModule.h" // for LogMocapRecorder (DECLARE_LOG_CATEGORY_EXTERN)

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MocapRecorderExportUtils.h"
#include "MocapRecorderPoseUtils.h"

// IWYU: include what you use; do not rely on transitive includes.

namespace
{  
  // unnamed namespace
} 

// ============================================================================
// Constructor / lifecycle
// ============================================================================

UMocapRecorderComponent::UMocapRecorderComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

    TargetSkeletalMesh = nullptr;
    SampleRate = 60.f;
    bAutoExportOnStop = true;
    
   

    bIsRecording = false;
    RecordedFrameCount = 0;
    TimeAccumulator = 0.f;
    TimeFromStart = 0.f;
        
}

void UMocapRecorderComponent::SetSessionWorldOrigin(const FTransform& InOrigin)
{
    SessionWorldOrigin = InOrigin;
}

void UMocapRecorderComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!TargetSkeletalMesh)
    {
        if (AActor* Owner = GetOwner())
        {
            TargetSkeletalMesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
        }
    }

    PrimaryComponentTick.bCanEverTick = true;

    // Sample AFTER animation has been evaluated
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

    if (TargetSkeletalMesh)
    {
        // Ensure the skeletal mesh ticks before we tick (and therefore before we sample)
        AddTickPrerequisiteComponent(TargetSkeletalMesh);
    }

    // Build bone lists once (best effort; if it fails you’ll log and can retry later)
    BuildSkeletonInfo();
        
}

void UMocapRecorderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(SampleTimerHandle);
    }

    Super::EndPlay(EndPlayReason);
}

void UMocapRecorderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    if (bExternalSampling)
    {
        return;
    }

    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bExternalSampling)
    {
        return;
    }


    if (!bIsRecording || !TargetSkeletalMesh || SampleRate <= 0.f)
    {
        return;
    }

    TimeAccumulator += DeltaTime;
    const float FrameInterval = 1.f / SampleRate;

    while (TimeAccumulator >= FrameInterval)
    {
        TimeAccumulator -= FrameInterval;
        SampleFrame();
    }
}

// ============================================================================
// Recording control
// ============================================================================

UMocapRecorderComponent* UMocapRecorderComponent::CreateBakeSnapshot() const
{
    UMocapRecorderComponent* Snapshot = NewObject<UMocapRecorderComponent>(GetTransientPackage());

    // Copy recorded data needed for baking
    Snapshot->SampleRate = SampleRate;
    Snapshot->bPreserveStartingLocation = bPreserveStartingLocation;
    Snapshot->SessionWorldOrigin = SessionWorldOrigin;

    Snapshot->RecordedSkeleton = RecordedSkeleton;
    Snapshot->RecordedMeshAsset = RecordedMeshAsset;

    Snapshot->Frames = Frames;
    Snapshot->RecordedBoneNames = RecordedBoneNames;

    Snapshot->BoneParentIndices = BoneParentIndices;
    Snapshot->BoneSkeletonIndices = BoneSkeletonIndices;
    Snapshot->BoneNames = BoneNames;

    // Ensure snapshot is not tied to the live world
    Snapshot->TargetSkeletalMesh = nullptr;
    Snapshot->bExternalSampling = false;
    Snapshot->bAutoExportOnStop = false;
    Snapshot->bIsRecording = false;

    return Snapshot;
}

void UMocapRecorderComponent::StartRecording()
{
    bHasWorldBakeBaseline = false;
    WorldBakeBaselineRoot = FTransform::Identity;


    if (bIsRecording)
        return;

    if (!TargetSkeletalMesh)
    {
        // Try to resolve automatically (prevents “no TargetSkeletalMesh” false negatives)
        TargetSkeletalMesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
        if (!TargetSkeletalMesh)
        {
            UE_LOG(LogMocapRecorder, Warning, TEXT("StartRecording() – no TargetSkeletalMesh."));
            return;
        }
        RecordedMeshAsset = TargetSkeletalMesh ? TargetSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
        RecordedSkeleton = RecordedMeshAsset ? RecordedMeshAsset->GetSkeleton() : nullptr;
                
    }

    UE_LOG(LogMocapRecorder, Warning, TEXT("MocapRecorder: Cached Mesh=%s Skeleton=%s TargetComp=%s"),
        RecordedMeshAsset ? *RecordedMeshAsset->GetName() : TEXT("NULL"),
        RecordedSkeleton ? *RecordedSkeleton->GetName() : TEXT("NULL"),
        TargetSkeletalMesh ? *TargetSkeletalMesh->GetName() : TEXT("NULL"));


    // Force full animation evaluation (diagnostic / recording mode)
    if (USkeletalMeshComponent* SkelComp = TargetSkeletalMesh)
    {
        SkelComp->bEnableUpdateRateOptimizations = false;
        SkelComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        SkelComp->SetComponentTickEnabled(true);
        SkelComp->ForcedLodModel = 1;
    }

    if (!BuildSkeletonInfo())
    {
        UE_LOG(LogMocapRecorder, Warning, TEXT("StartRecording: failed to build skeleton info."));
        return;
    }

    if (bExternalSampling)
    {
        return;
    }


    Frames.Reset();
    RecordedFrameCount = 0;
    TimeAccumulator = 0.f;
    TimeFromStart = 0.f;
    bIsRecording = true;
    

    // ---- Start sampling ----
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogMocapRecorder, Warning, TEXT("StartRecording: no World."));
        bIsRecording = false;
        return;
    }

    // Clear any stale timer first
    World->GetTimerManager().ClearTimer(SampleTimerHandle);

    const float EffectiveRate = (SampleRate > 0.f) ? SampleRate : 60.f;
    const float Interval = 1.f / EffectiveRate;

    // Capture one frame immediately (avoids “0 frames” if user stops quickly)
    SampleFrame();

    // Then continue at fixed interval
    World->GetTimerManager().SetTimer(
        SampleTimerHandle,
        this,
        &UMocapRecorderComponent::SampleFrame,
        Interval,
        true
    );

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, TEXT("Mocap Recording Started"));
    }

    // Cache stable asset references for editor bake (PIE-safe)
    RecordedMeshAsset = TargetSkeletalMesh
        ? TargetSkeletalMesh->GetSkeletalMeshAsset()
        : nullptr;

    RecordedSkeleton = RecordedMeshAsset
        ? RecordedMeshAsset->GetSkeleton()
        : nullptr;

    UE_LOG(LogTemp, Warning,
        TEXT("MocapRecorder: Cached Mesh=%s Skeleton=%s"),
        RecordedMeshAsset ? *RecordedMeshAsset->GetName() : TEXT("NULL"),
        RecordedSkeleton ? *RecordedSkeleton->GetName() : TEXT("NULL"));

}

void UMocapRecorderComponent::StartRecording_External()
{
    bExternalSampling = true;

    // Bullets/casings/etc: no skeletal mesh required
    if (CaptureMode == EMocapCaptureMode::TransformOnly)
    {
        StartRecording_ExternalTransformOnly(StartSampleIndex);
        return;
    }

    if (CaptureMode == EMocapCaptureMode::Skeletal && !IsValid(TargetSkeletalMesh))
    {
        UE_LOG(LogMocapRecorder, Warning, TEXT("StartRecording: Skeletal mode requires TargetSkeletalMesh."));
        return;
    }


    // Normal skeletal path
    StartRecording_ExternalWithPreRoll(0);
}

void UMocapRecorderComponent::StartRecording_ExternalWithPreRoll(int32 PreRollFrames)
{
    bExternalSampling = true;

    bHasWorldBakeBaseline = false;
    WorldBakeBaselineRoot = FTransform::Identity;

    if (bIsRecording)
        return;

    if (!TargetSkeletalMesh)
    {
        TargetSkeletalMesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
        if (!TargetSkeletalMesh)
        {
            UE_LOG(LogMocapRecorder, Warning, TEXT("StartRecording_ExternalWithPreRoll: no TargetSkeletalMesh."));
            return;
        }
    }

    if (USkeletalMeshComponent* SkelComp = TargetSkeletalMesh)
    {
        SkelComp->bEnableUpdateRateOptimizations = false;
        SkelComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        SkelComp->SetComponentTickEnabled(true);
        SkelComp->ForcedLodModel = 1;
    }

    if (!BuildSkeletonInfo())
    {
        UE_LOG(LogMocapRecorder, Warning, TEXT("StartRecording_ExternalWithPreRoll: failed to build skeleton info."));
        return;
    }

    Frames.Reset();
    RecordedFrameCount = 0;
    TimeAccumulator = 0.f;
    TimeFromStart = 0.f;

    bIsRecording = true;

    RecordedMeshAsset = TargetSkeletalMesh ? TargetSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    RecordedSkeleton = RecordedMeshAsset ? RecordedMeshAsset->GetSkeleton() : nullptr;

    // ------------------------------------------------------------
    // Static preroll padding (align late-spawned actors to session timeline)
    // ------------------------------------------------------------
    PreRollFrames = FMath::Max(0, PreRollFrames);
    if (PreRollFrames > 0)
    {
        FMocapFrame StaticFrame;
        if (CaptureCurrentPoseToFrame(StaticFrame))
        {
            const float Dt = (SampleRate > 0.f) ? (1.f / SampleRate) : (1.f / 60.f);

            for (int32 i = 0; i < PreRollFrames; ++i)
            {
                FMocapFrame& F = Frames.AddDefaulted_GetRef();
                F.Time = TimeFromStart;

                F.Translations = StaticFrame.Translations;
                F.Rotations = StaticFrame.Rotations;

#if WITH_EDITORONLY_DATA
                F.HeadWorld = StaticFrame.HeadWorld;
                F.TailWorld = StaticFrame.TailWorld;
#endif
                TimeFromStart += Dt;
            }
        }
        else
        {
            UE_LOG(LogMocapRecorder, Warning, TEXT("preroll requested (%d) but CaptureCurrentPoseToFrame failed."), PreRollFrames);
        }
    }
}

void UMocapRecorderComponent::StartRecording_ExternalTransformOnly(int32 InStartSampleIndex)
{
    bExternalSampling = true;
    CaptureMode = EMocapCaptureMode::TransformOnly;
    StartSampleIndex = FMath::Max(0, InStartSampleIndex);

    if (bIsRecording)
        return;

    // Reset buffers
    Frames.Reset();            // <-- we WILL use this (1-bone skeletal frames)
    TransformFrames.Reset();   // <-- still record full transform (including scale)
    RecordedFrameCount = 0;
    TimeAccumulator = 0.f;
    TimeFromStart = 0.f;

    // Reset baseline so transform-only uses the same policy as skeletal
    bHasWorldBakeBaseline = false;
    WorldBakeBaselineRoot = FTransform::Identity;

    bIsRecording = true;

    // Provide a real skeleton so the bake pipeline can produce UAnimSequence.
    RecordedSkeleton = TransformOnlyBakeSkeleton;
    RecordedMeshAsset = nullptr;

    // Build a 1-bone "recording skeleton description" for the baker.
    RecordedBoneNames.Reset();
    BoneParentIndices.Reset();
    BoneSkeletonIndices.Reset();
    BoneNames.Reset();

    RecordedBoneNames.Add(TransformOnlyRootBoneName);
    BoneNames.Add(TransformOnlyRootBoneName);

    BoneParentIndices.Add(INDEX_NONE);   // root has no parent
    BoneSkeletonIndices.Add(0);          // single bone

    if (!IsValid(RecordedSkeleton))
    {
        // NOTE:
        // TransformOnly recorders do not require a pre-made Skeleton anymore.
        // The Editor bake path will generate a 1-bone ("root") skeleton at bake time.
        UE_LOG(LogMocapRecorder, Warning,
            TEXT("TransformOnly: RecordedSkeleton is NULL (allowed). Bake will generate a 1-bone skeleton with bone '%s'."),
            *TransformOnlyRootBoneName.ToString());

        // IMPORTANT: Do NOT early-out here.
    }

}

void UMocapRecorderComponent::StopRecording_External()
{
    if (!bIsRecording)
        return;

    bIsRecording = false;

   if (USkeletalMeshComponent* SkelComp = TargetSkeletalMesh)
    {
        SkelComp->bEnableUpdateRateOptimizations = true;
        SkelComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
        SkelComp->ForcedLodModel = 0;
    }

   RecordedFrameCount =
       (CaptureMode == EMocapCaptureMode::TransformOnly)
       ? TransformFrames.Num()
       : Frames.Num();

}

void UMocapRecorderComponent::StopRecording()
{
    if (!bIsRecording)
        return;

    bIsRecording = false;

    // Stop sampling timer
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SampleTimerHandle);
    }

    
    // Restore animation settings after recording
    if (USkeletalMeshComponent* SkelComp = TargetSkeletalMesh)
    {
        SkelComp->bEnableUpdateRateOptimizations = true;
        SkelComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
        SkelComp->ForcedLodModel = 0;
    }

    const int32 NumFrames = Frames.Num();
    RecordedFrameCount = NumFrames;

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1, 2.f, FColor::Yellow,
            FString::Printf(TEXT("Mocap Recording Stopped – Frames: %d"), NumFrames));
    }

    // if (bAutoExportOnStop && NumFrames > 0)
    // {
    //     ExportBVH(TEXT(""));
    // }
        
}

// ============================================================================
// Skeleton description – high-level wrapper
// ============================================================================

bool UMocapRecorderComponent::BuildSkeletonInfo()
{
    RecordedBoneNames.Reset();
    BoneParentIndices.Reset();
    BoneSkeletonIndices.Reset();

    if (!TargetSkeletalMesh)
        return false;

    USkeletalMesh* Mesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!Mesh)
        return false;

    const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
    const int32 NumBones = RefSkeleton.GetNum();
    if (NumBones <= 0)
        return false;

    RecordedBoneNames.Reserve(NumBones);
    BoneParentIndices.Reserve(NumBones);
    BoneSkeletonIndices.Reserve(NumBones);

    for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
    {
        RecordedBoneNames.Add(RefSkeleton.GetBoneName(BoneIndex));
        BoneSkeletonIndices.Add(BoneIndex);
        BoneParentIndices.Add(RefSkeleton.GetParentIndex(BoneIndex));
    }

    UE_LOG(LogTemp, Log, TEXT("MocapRecorder: Export bones=%d (from %d), INCLUDED ALL bones"), NumBones, NumBones);
    return true;
}

// ============================================================================
// Frame sampling – per-bone local transforms
// ============================================================================

static FVector QuatToEuler_ZYX_Degrees(const FQuat& Q_in)
{
    // Intrinsic ZYX Euler (BVH: Zrotation Yrotation Xrotation)
    // Returns FVector(Zdeg, Ydeg, Xdeg)

    const FQuat Q = Q_in.GetNormalized();

    const double x = (double)Q.X;
    const double y = (double)Q.Y;
    const double z = (double)Q.Z;
    const double w = (double)Q.W;

    // Z (yaw)
    const double t0 = 2.0 * (w * z + x * y);
    const double t1 = 1.0 - 2.0 * (y * y + z * z);
    const double Z = FMath::Atan2((float)t0, (float)t1);

    // Y (pitch)
    double t2 = 2.0 * (w * y - z * x);
    t2 = FMath::Clamp(t2, -1.0, 1.0);
    const double Y = FMath::Asin((float)t2);

    // X (roll)
    const double t3 = 2.0 * (w * x + y * z);
    const double t4 = 1.0 - 2.0 * (x * x + y * y);
    const double X = FMath::Atan2((float)t3, (float)t4);

    return FVector(
        FMath::RadiansToDegrees((float)Z),
        FMath::RadiansToDegrees((float)Y),
        FMath::RadiansToDegrees((float)X)
    );
}

bool UMocapRecorderComponent::CaptureCurrentPoseToFrame(FMocapFrame& OutFrame)
{
    if (!TargetSkeletalMesh)
    {
        TargetSkeletalMesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
        if (!TargetSkeletalMesh)
            return false;
    }

    USkeletalMesh* Mesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!Mesh)
        return false;

    const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
    const int32 NumSkelBones = RefSkel.GetNum();
    if (NumSkelBones <= 0)
        return false;

    const int32 NumExportBones = RecordedBoneNames.Num();
    if (NumExportBones != NumSkelBones ||
        BoneSkeletonIndices.Num() != NumExportBones ||
        BoneParentIndices.Num() != NumExportBones)
    {
        UE_LOG(LogTemp, Error, TEXT("MocapRecorder: CaptureCurrentPoseToFrame export arrays not aligned. Rebuild skeleton info."));
        return false;
    }

    // Force final pose evaluation
    TargetSkeletalMesh->TickAnimation(0.f, false);
    TargetSkeletalMesh->RefreshBoneTransforms();

    const TArray<FTransform>& CSTransforms = TargetSkeletalMesh->GetComponentSpaceTransforms();
    if (CSTransforms.Num() != NumSkelBones)
    {
        UE_LOG(LogTemp, Error, TEXT("MocapRecorder: CSTransforms mismatch: got=%d expected=%d"),
            CSTransforms.Num(), NumSkelBones);
        return false;
    }

    const FTransform ComponentToWorld = TargetSkeletalMesh->GetComponentTransform();

    // WORLD transforms per skeleton bone
    TArray<FTransform> WorldBySkel;
    WorldBySkel.SetNum(NumSkelBones);
    for (int32 SkelIdx = 0; SkelIdx < NumSkelBones; ++SkelIdx)
    {
        WorldBySkel[SkelIdx] = CSTransforms[SkelIdx] * ComponentToWorld;
    }

    // Root skeleton index (bone with no parent)
    int32 RootSkelIdx = 0;
    for (int32 i = 0; i < NumSkelBones; ++i)
    {
        if (RefSkel.GetParentIndex(i) == INDEX_NONE)
        {
            RootSkelIdx = i;
            break;
        }
    }

    // Establish baseline ONCE.
// Policy:
// - bPreserveStartingLocation=true  => baseline = SessionWorldOrigin (session-relative world)
// - bPreserveStartingLocation=false => baseline = actor root at frame 0 (rebase-to-actor-start)
    if (!bHasWorldBakeBaseline)
    {
        if (bPreserveStartingLocation)
        {
            WorldBakeBaselineRoot = SessionWorldOrigin; // IMPORTANT: session origin, not identity
        }
        else
        {
            WorldBakeBaselineRoot = WorldBySkel[RootSkelIdx]; // actor-start baseline
        }

        bHasWorldBakeBaseline = true;
    }

    const FTransform InvBaseline = WorldBakeBaselineRoot.Inverse();

    // Rebased world transforms (CORRECT ORDER: InvBaseline * World)
    TArray<FTransform> WorldRelBySkel;
    WorldRelBySkel.SetNum(NumSkelBones);
    for (int32 SkelIdx = 0; SkelIdx < NumSkelBones; ++SkelIdx)
    {
        WorldRelBySkel[SkelIdx] = InvBaseline * WorldBySkel[SkelIdx];
    }

    // Parent-relative locals (safe)
    TArray<FTransform> LocalRelBySkel;
    LocalRelBySkel.SetNum(NumSkelBones);
    for (int32 SkelIdx = 0; SkelIdx < NumSkelBones; ++SkelIdx)
    {
        const int32 Parent = RefSkel.GetParentIndex(SkelIdx);
        if (Parent == INDEX_NONE)
        {
            LocalRelBySkel[SkelIdx] = WorldRelBySkel[SkelIdx];
        }
        else
        {
            LocalRelBySkel[SkelIdx] = WorldRelBySkel[SkelIdx].GetRelativeTransform(WorldRelBySkel[Parent]);
        }
    }



    OutFrame.Translations.SetNum(NumExportBones);
    OutFrame.Rotations.SetNum(NumExportBones);

#if WITH_EDITORONLY_DATA
    OutFrame.HeadWorld.SetNum(NumExportBones);
    OutFrame.TailWorld.SetNum(NumExportBones);
#endif

    for (int32 BoneIdx = 0; BoneIdx < NumExportBones; ++BoneIdx)
    {
        const int32 SkelIdx = BoneIdx;

        const FTransform& Local = LocalRelBySkel[SkelIdx];
        OutFrame.Translations[BoneIdx] = Local.GetTranslation();
        OutFrame.Rotations[BoneIdx] = Local.GetRotation().GetNormalized();

#if WITH_EDITORONLY_DATA
        const FVector Head = WorldRelBySkel[SkelIdx].GetTranslation(); // session-relative world (not absolute world)
        OutFrame.HeadWorld[BoneIdx] = Head;

        int32 FirstChild = INDEX_NONE;
        for (int32 j = 0; j < NumSkelBones; ++j)
        {
            if (RefSkel.GetParentIndex(j) == SkelIdx)
            {
                FirstChild = j;
                break;
            }
        }

        OutFrame.TailWorld[BoneIdx] = (FirstChild != INDEX_NONE)
            ? WorldRelBySkel[FirstChild].GetTranslation()
            : Head;
#endif
    }

    return true;
}

void UMocapRecorderComponent::SampleFrame()
{
    if (!bIsRecording)
        return;

    if (CaptureMode == EMocapCaptureMode::TransformOnly)
    {
        AActor* Owner = GetOwner();
        if (!Owner)
            return;

        const float Dt = (SampleRate > 0.f) ? (1.f / SampleRate) : (1.f / 60.f);

        const FTransform WorldXf = Owner->GetActorTransform();

        // Match skeletal baseline policy:
        // - bPreserveStartingLocation=true  => baseline = SessionWorldOrigin
        // - bPreserveStartingLocation=false => baseline = actor world at frame 0 (rebase-to-actor-start)
        if (!bHasWorldBakeBaseline)
        {
            if (bPreserveStartingLocation)
            {
                WorldBakeBaselineRoot = SessionWorldOrigin;
            }
            else
            {
                WorldBakeBaselineRoot = WorldXf;
            }

            bHasWorldBakeBaseline = true;
        }

        const FTransform InvBaseline = WorldBakeBaselineRoot.Inverse();

        // Session-relative world (same convention as skeletal CaptureCurrentPoseToFrame):
        // Rel = InvBaseline * World
        const FTransform RelXf = InvBaseline * WorldXf;

        // 1) Record full transform (includes scale) for transform-only bookkeeping
        FMocapTransformFrame& TF = TransformFrames.AddDefaulted_GetRef();
        TF.Time = TimeFromStart;
        TF.World = RelXf;

        // 2) ALSO record as a 1-bone skeletal frame so it bakes to UAnimSequence
        FMocapFrame& Frame = Frames.AddDefaulted_GetRef();
        Frame.Time = TimeFromStart;

        Frame.Translations.SetNum(1);
        Frame.Rotations.SetNum(1);

        Frame.Translations[0] = RelXf.GetLocation();
        Frame.Rotations[0] = RelXf.GetRotation().GetNormalized();

#if WITH_EDITORONLY_DATA
        Frame.HeadWorld.SetNum(1);
        Frame.TailWorld.SetNum(1);
        Frame.HeadWorld[0] = RelXf.GetLocation();
        Frame.TailWorld[0] = RelXf.GetLocation();
#endif

        TimeFromStart += Dt;
        return;
    }

    if (!TargetSkeletalMesh)
    {
        TargetSkeletalMesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
        if (!TargetSkeletalMesh)
            return;
    }

    USkeletalMesh* Mesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!Mesh)
        return;

    const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
    const int32 NumSkelBones = RefSkel.GetNum();
    if (NumSkelBones <= 0)
        return;

    const int32 NumExportBones = RecordedBoneNames.Num();
    if (NumExportBones != NumSkelBones ||
        BoneSkeletonIndices.Num() != NumExportBones ||
        BoneParentIndices.Num() != NumExportBones)
    {
        UE_LOG(LogTemp, Error, TEXT("MocapRecorder: SampleFrame export arrays not aligned to full skeleton. Rebuild skeleton info."));
        return;
    }

    FMocapFrame Captured;
    if (!CaptureCurrentPoseToFrame(Captured))
        return;

    FMocapFrame& Frame = Frames.AddDefaulted_GetRef();
    Frame.Time = TimeFromStart;
    Frame.Translations = MoveTemp(Captured.Translations);
    Frame.Rotations = MoveTemp(Captured.Rotations);

#if WITH_EDITORONLY_DATA
    Frame.HeadWorld = MoveTemp(Captured.HeadWorld);
    Frame.TailWorld = MoveTemp(Captured.TailWorld);
#endif

    TimeFromStart += (SampleRate > 0.f) ? (1.f / SampleRate) : (1.f / 60.f);

}

const TArray<FMocapFrame>& UMocapRecorderComponent::GetRecordedFrames() const
{
    return Frames;
}

void UMocapRecorderComponent::OverrideRecordedSkeleton(USkeleton* InSkeleton)
{
    RecordedSkeleton = InSkeleton;
}

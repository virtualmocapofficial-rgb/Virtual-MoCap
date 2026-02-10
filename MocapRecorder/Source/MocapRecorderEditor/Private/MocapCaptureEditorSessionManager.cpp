#include "MocapCaptureEditorSessionManager.h"

// Engine/Core
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"
#include "Misc/ScopedSlowTask.h"
#include "Engine/EngineTypes.h"

// Gameplay helpers used in this file
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"


// Plugin
#include "MocapRecorderComponent.h"
#include "MocapRecorderEditorModule.h"
#include "MocapCaptureMode.h"
#include "Misc/Optional.h"



// Editor subsystems
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "FileHelpers.h"
#include "AssetExportTask.h"
#include "Exporters/Exporter.h"
#include "UObject/SoftObjectPath.h"

// Forward declaration
static bool ExportMeshAssetToFbx_IfMissing(const FString& MeshAssetPath, FString& OutMeshFbxPath);

#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"



// IWYU: include what you use; do not rely on transitive includes.


static const TCHAR* SESSIONMANAGER_FINGERPRINT = TEXT("MocapSession: VERSION_FINGERPRINT 2026-01-31 SESSIONMANAGER_SYNC_A");

void UMocapCaptureEditorSessionManager::Initialize(UWorld* InWorld)
{
    World = InWorld;

    if (!BeginPIEHandle.IsValid())
    {
        BeginPIEHandle = FEditorDelegates::BeginPIE.AddUObject(this, &UMocapCaptureEditorSessionManager::OnBeginPIE);
    }
    if (!EndPIEHandle.IsValid())
    {
        EndPIEHandle = FEditorDelegates::EndPIE.AddUObject(this, &UMocapCaptureEditorSessionManager::OnEndPIE);
    }

    ResolveTargetsForWorld(World);
}

void UMocapCaptureEditorSessionManager::BeginDestroy()
{
    if (BeginPIEHandle.IsValid())
    {
        FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
        BeginPIEHandle.Reset();
    }

    if (EndPIEHandle.IsValid())
    {
        FEditorDelegates::EndPIE.Remove(EndPIEHandle);
        EndPIEHandle.Reset();
    }

    if (PostPIEBakeKickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PostPIEBakeKickHandle);
        PostPIEBakeKickHandle.Reset();
    }


    UnbindSpawnHook();
    EndBakeQueue();

    Super::BeginDestroy();
}

// ------------------------------------------------------------
// PIE lifecycle
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::OnBeginPIE(const bool bIsSimulating)
{
    if (GEditor && GEditor->PlayWorld)
    {
        ResolveTargetsForWorld(GEditor->PlayWorld.Get());
    }
}

void UMocapCaptureEditorSessionManager::OnEndPIE(const bool bIsSimulating)
{
    // IMPORTANT: do NOT early-return just because bIsRecording is false.
    // We may have deferred baking during PIE and need to kick it now.

    TickBakeQueue(0.f);

    const bool bWasRecording = bIsRecording;

    if (bWasRecording)
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("%s"), SESSIONMANAGER_FINGERPRINT);
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: PIE ended while recording; auto-stopping session now to avoid stale-world shutdown."));
        StopSession();
    }

    // Restore editor world
    if (GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }
    else
    {
        World = nullptr;
    }

    // Start deferred bake now that PIE is over (ONLY if we deferred during PIE)
    if (bBakeDeferredUntilEndPIE && PendingBakeJobs.Num() > 0)
        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("StopSession: Added bake job. PendingBakeJobs=%d"),
            PendingBakeJobs.Num());

    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: EndPIE -> scheduling post-PIE bake kick (%d jobs)."), PendingBakeJobs.Num());

        bBakeDeferredUntilEndPIE = false;

        // EndPIE can occur before PlayWorld is nulled; retry on ticker until safe.
        if (!PostPIEBakeKickHandle.IsValid())
        {
            PostPIEBakeKickHandle = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateUObject(this, &UMocapCaptureEditorSessionManager::TickPostPIEBakeKick),
                0.0f
            );
        }    
        else
        {
            // If there are stale jobs but we didn't defer, don't kick them accidentally.
            // (Use ClearBakeQueue to purge.)
        }
    }
        
}

// ------------------------------------------------------------
// Utilities
// ------------------------------------------------------------

USkeletalMeshComponent* UMocapCaptureEditorSessionManager::FindFirstSkeletalMeshComponent(AActor* Actor)
{
    return Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

FString UMocapCaptureEditorSessionManager::MakeDefaultAssetName(AActor* Actor)
{
    const FString ActorName = Actor ? Actor->GetName() : TEXT("Actor");
    return FString::Printf(TEXT("Mocap_%s_%s"), *ActorName, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
}

AActor* UMocapCaptureEditorSessionManager::FindActorByGuid(UWorld* InWorld, const FGuid& Guid)
{
#if WITH_EDITOR
    if (!InWorld || !Guid.IsValid())
        return nullptr;

    for (TActorIterator<AActor> It(InWorld); It; ++It)
    {
        AActor* A = *It;
        if (A && A->GetActorGuid() == Guid)
        {
            return A;
        }
    }
#endif
    return nullptr;
}

void UMocapCaptureEditorSessionManager::ResolveTargetsForWorld(UWorld* InWorld)
{
    if (!InWorld)
        return;

    for (FMocapEditorSessionTarget& T : Targets)
    {
        AActor* Resolved = FindActorByGuid(InWorld, T.ActorGuid);
        T.Actor = Resolved;

        if (Resolved)
        {
            T.LastKnownLabel = Resolved->GetActorLabel();
            T.SkelComp = FindFirstSkeletalMeshComponent(Resolved);
        }
        else
        {
            T.SkelComp = nullptr;
            T.Recorder = nullptr;
        }
    }
}

// ------------------------------------------------------------
// Target management
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::AddSelectedActorsFromOutliner()
{
    if (!GEditor)
        return;

    USelection* Sel = GEditor->GetSelectedActors();
    if (!Sel)
        return;

    for (FSelectionIterator It(*Sel); It; ++It)
    {
        AActor* Actor = Cast<AActor>(*It);
        if (!Actor)
            continue;

#if WITH_EDITOR
        const FGuid Guid = Actor->GetActorGuid();
#else
        const FGuid Guid;
#endif

        if (!Guid.IsValid())
            continue;

        const bool bExists = Targets.ContainsByPredicate([&](const FMocapEditorSessionTarget& T) { return T.ActorGuid == Guid; });
        if (bExists)
            continue;

        USkeletalMeshComponent* Skel = FindFirstSkeletalMeshComponent(Actor);
        if (!Skel)
            continue;

        FMocapEditorSessionTarget T;
        T.ActorGuid = Guid;
        T.LastKnownLabel = Actor->GetActorLabel();
        T.Actor = Actor;
        T.SkelComp = Skel;
        T.bEnabled = true;

        Targets.Add(MoveTemp(T));
    }
}

void UMocapCaptureEditorSessionManager::ClearTargets()
{
    Targets.Reset();
}

void UMocapCaptureEditorSessionManager::SetTargetEnabled(int32 Index, bool bEnabled)
{
    if (Targets.IsValidIndex(Index))
    {
        Targets[Index].bEnabled = bEnabled;
    }
}

// ------------------------------------------------------------
// Class Rule API (called by panel)
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::AddClassRule()
{
    FMocapClassCaptureRule NewRule;
    ClassRules.Add(NewRule);
}

void UMocapCaptureEditorSessionManager::RemoveClassRule(int32 Index)
{
    if (ClassRules.IsValidIndex(Index))
        ClassRules.RemoveAt(Index);
}

void UMocapCaptureEditorSessionManager::ClearClassRules()
{
    ClassRules.Reset();
}

void UMocapCaptureEditorSessionManager::SetClassRuleEnabled(int32 Index, bool bEnabled)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].bEnabled = bEnabled;
}

void UMocapCaptureEditorSessionManager::SetClassRuleClass(int32 Index, UClass* InClass)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].ActorClass = InClass;
}

void UMocapCaptureEditorSessionManager::SetClassRuleRequiredTag(int32 Index, FName InTag)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].RequiredTag = InTag;
}

void UMocapCaptureEditorSessionManager::SetClassRuleTransformOnly(int32 Index, bool bIn)
{
    if (!ClassRules.IsValidIndex(Index))
        return;

    // Skeletal-only policy: transform-only is not supported.
    ClassRules[Index].bTransformOnly = false;
    ClassRules[Index].bRequireSkeletalMesh = true;
    ClassRules[Index].CaptureMode = EMocapCaptureMode::Skeletal;

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("Rule[%d] skeletal-only policy: TransformOnly is disabled (ignoring requested=%d)."),
        Index, bIn ? 1 : 0);
}

void UMocapCaptureEditorSessionManager::SetRule_StopWhenNearlyStationary(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bStopWhenNearlyStationary = bIn;
}

void UMocapCaptureEditorSessionManager::SetRule_LinearSpeedThreshold(int32 Index, float V)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.LinearSpeedThreshold = FMath::Max(0.f, V);
}

void UMocapCaptureEditorSessionManager::SetRule_StationaryHoldSeconds(int32 Index, float V)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.StationaryHoldSeconds = FMath::Max(0.f, V);
}

void UMocapCaptureEditorSessionManager::SetRule_StopWhenOutOfPlayerRadius(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bStopWhenOutOfPlayerRadius = bIn;
}

void UMocapCaptureEditorSessionManager::SetRule_PlayerRadius(int32 Index, float V)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.PlayerRadius = FMath::Max(0.f, V);
}

void UMocapCaptureEditorSessionManager::SetRule_StopOnHitEvent(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bStopOnHitEvent = bIn;
}

void UMocapCaptureEditorSessionManager::SetRule_StopOnDestroyed(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bStopOnDestroyed = bIn;
}

void UMocapCaptureEditorSessionManager::SetRule_AutoBakeOnAutoStop(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bAutoBakeOnAutoStop = bIn;
}

// ------------------------------------------------------------
// Recorder attach
// ------------------------------------------------------------

bool UMocapCaptureEditorSessionManager::ResolveOrAttachRecorder(FMocapEditorSessionTarget& T)
{
    AActor* Actor = T.Actor.Get();
    USkeletalMeshComponent* Skel = T.SkelComp.Get();
    if (!Actor || !Skel)
        return false;

    UMocapRecorderComponent* Recorder = T.Recorder.Get();
    if (!Recorder)
    {
        Recorder = Actor->FindComponentByClass<UMocapRecorderComponent>();
        if (!Recorder)
        {
            Recorder = NewObject<UMocapRecorderComponent>(Actor, UMocapRecorderComponent::StaticClass(), NAME_None, RF_Transactional);
            Recorder->RegisterComponent();
        }
        T.Recorder = Recorder;
    }

    Recorder->TargetSkeletalMesh = Skel;
    Recorder->SampleRate = CaptureSampleRateHz;
    Recorder->bExternalSampling = true;
    Recorder->bAutoExportOnStop = false;

    return true;
}

// ------------------------------------------------------------
// Session control
// ------------------------------------------------------------

bool UMocapCaptureEditorSessionManager::StartSession()
{
    if (bIsRecording)
        return false;

    SessionSampleCounter = 0;

    // Always choose the correct world for the current mode
#if WITH_EDITOR
    if (GEditor && GEditor->PlayWorld)
    {
        World = GEditor->PlayWorld;
    }
    else if (GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }
#endif

    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("MocapSession: StartSession failed - no valid World."));
        return false;
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("%s"), SESSIONMANAGER_FINGERPRINT);

    ResolveTargetsForWorld(World);

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: ClassRules dump Num=%d"), ClassRules.Num());
    for (int32 i = 0; i < ClassRules.Num(); ++i)
    {
        const FMocapClassCaptureRule& R = ClassRules[i];
        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("  Rule[%d] Enabled=%d Class=%s TransformOnly=%d RequireSkel=%d Tag=%s"),
            i,
            R.bEnabled ? 1 : 0,
            *GetNameSafe(R.ActorClass.Get()),
            R.bTransformOnly ? 1 : 0,
            R.bRequireSkeletalMesh ? 1 : 0,
            *R.RequiredTag.ToString());
    }

    // Reset per-session tracking ONCE (and do NOT log "StopSession" strings here)
    SeenAutoCaptureActors.Reset();
    PendingAutoCaptureActors.Reset();
    ActiveInstances.Reset();

    // Load rules BEFORE we tick so OnActorSpawned can match immediately
    for (FMocapClassCaptureRule& Rule : ClassRules)
    {
        if (Rule.bEnabled && !Rule.ActorClass.IsNull())
        {
            Rule.ActorClass.LoadSynchronous();
        }
    }

    int32 StartedManual = 0;

    // Start manual targets
    for (FMocapEditorSessionTarget& T : Targets)
    {
        if (!T.bEnabled)
            continue;

        AActor* Actor = T.Actor.Get();
        if (!IsValid(Actor))
            continue;

        if (!ResolveOrAttachRecorder(T))
            continue;

        UMocapRecorderComponent* Recorder = T.Recorder.Get();
        if (!IsValid(Recorder))
            continue;

        Recorder->StartRecording_External();
        T.Recorder = Recorder;
        ++StartedManual;
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: Started manual targets=%d"), StartedManual);

    const float Interval = 1.f / FMath::Max(1.f, CaptureSampleRateHz);

    bIsRecording = true;

    // Start ticking + bind spawn hook
    World->GetTimerManager().SetTimer(SessionTimerHandle, this, &UMocapCaptureEditorSessionManager::SampleAll, Interval, true);
    BindSpawnHook();

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: StartSession summary World=%s Interval=%f"),
        *GetNameSafe(World), Interval);

    return true;
}

void UMocapCaptureEditorSessionManager::StopSession()
{
    if (!bIsRecording)
    {
        return;
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: StopSession begin"));

    bIsRecording = false;

    // Stop sampling timer
    if (World)
    {
        World->GetTimerManager().ClearTimer(SessionTimerHandle);
    }

    // Unbind spawn hook once
    UnbindSpawnHook();
    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: Spawn hook unbound."));

    // ------------------------------------------------------------
    // Stop selected/manual targets
    // ------------------------------------------------------------
    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: stopping manual targets (Targets=%d)"), Targets.Num());

    for (FMocapEditorSessionTarget& T : Targets)
    {
        if (!T.bEnabled)
        {
            continue;
        }

        UMocapRecorderComponent* Recorder = T.Recorder.Get();
        if (!IsValid(Recorder))
        {
            continue;
        }

        // Stop recording if still active
        if (Recorder->bIsRecording)
        {
            Recorder->StopRecording_External();
        }

        const int32 NumFrames = Recorder->GetRecordedFrames().Num();

        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("Session: Target %s stopped. Frames=%d Skeleton=%s"),
            *GetNameSafe(Recorder->GetOwner()),
            NumFrames,
            *GetNameSafe(Recorder->GetRecordedSkeleton()));

        // Enqueue bake job (manager-level toggle)
        if (bAutoBakeOnStop && NumFrames > 0 && IsValid(Recorder->GetOwner()))
        {
            UMocapRecorderComponent* Snapshot = Recorder->CreateBakeSnapshot();
            if (IsValid(Snapshot))
            {
                FMocapBakeJob Job;
                Job.RecorderSnapshot = TStrongObjectPtr<UMocapRecorderComponent>(Snapshot);

                Job.AssetName =
                    !T.OutputNameOverride.IsEmpty()
                    ? T.OutputNameOverride
                    : MakeDefaultAssetName(Recorder->GetOwner());

                PendingBakeJobs.Add(MoveTemp(Job));

                UE_LOG(LogMocapRecorderEditor, Warning,
                    TEXT("StopSession: Added bake job (manual). PendingBakeJobs=%d"),
                    PendingBakeJobs.Num());
            }
        }

        // Optional hygiene
        T.Recorder.Reset();
    }

    // ------------------------------------------------------------
    // Stop auto-captured instances
    // ------------------------------------------------------------
    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("StopSession: PRE-CLEANUP PendingBakeJobs=%d ActiveInstances=%d Targets=%d"),
        PendingBakeJobs.Num(),
        ActiveInstances.Num(),
        Targets.Num());

    for (FMocapInstanceState& S : ActiveInstances)
    {
        UMocapRecorderComponent* Recorder = S.Recorder.Get();
        if (!IsValid(Recorder))
        {
            continue;
        }

        if (Recorder->bIsRecording)
        {
            Recorder->StopRecording_External();
        }

        const int32 NumFrames = Recorder->GetRecordedFrames().Num();

        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("Session: AutoInstance %s stopped. Frames=%d Skeleton=%s"),
            *GetNameSafe(Recorder->GetOwner()),
            NumFrames,
            *GetNameSafe(Recorder->GetRecordedSkeleton()));

        // Enqueue bake job
        if (bAutoBakeOnStop && NumFrames > 0 && IsValid(Recorder->GetOwner()))
        {
            UMocapRecorderComponent* Snapshot = Recorder->CreateBakeSnapshot();
            if (IsValid(Snapshot))
            {
                FMocapBakeJob Job;
                Job.RecorderSnapshot = TStrongObjectPtr<UMocapRecorderComponent>(Snapshot);

                Job.AssetName =
                    !S.OutputNameOverride.IsEmpty()
                    ? S.OutputNameOverride
                    : MakeDefaultAssetName(Recorder->GetOwner());

                PendingBakeJobs.Add(MoveTemp(Job));

                UE_LOG(LogMocapRecorderEditor, Warning,
                    TEXT("StopSession: Added bake job (auto). PendingBakeJobs=%d"),
                    PendingBakeJobs.Num());
            }
        }

        // If these recorder components were dynamically created for auto-capture, clean them up.
        Recorder->DestroyComponent();
    }

    // ------------------------------------------------------------
    // Cleanup auto-capture state AFTER we stopped/enqueued
    // ------------------------------------------------------------
    ActiveInstances.Reset();
    PendingAutoCaptureActors.Reset();
    SeenAutoCaptureActors.Reset();

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("StopSession: DONE PendingBakeJobs=%d"),
        PendingBakeJobs.Num());

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("Session: StopSession complete. PendingBakeJobs=%d PlayWorld=%s"),
        PendingBakeJobs.Num(),
        (GEditor && GEditor->PlayWorld) ? TEXT("YES") : TEXT("NO"));
}

void UMocapCaptureEditorSessionManager::SampleAll()
{
    if (!bIsRecording)
        return;    

    // Deterministic discovery: if spawn hook misses, we still capture everything.
    SweepWorldForAutoCapture(SweepBudgetPerTick);
    ProcessPendingAutoCaptures(MaxAutoCapturePerTick);

    int32 Processed = 0;

    // Sample selected targets
    for (FMocapEditorSessionTarget& T : Targets)
    {
        if (!T.bEnabled)
            continue;

        UMocapRecorderComponent* Recorder = T.Recorder.Get();
        if (Recorder && Recorder->bIsRecording)
        {
            Recorder->SampleFrame();
        }
    }

    // Sample instances
    for (FMocapInstanceState& S : ActiveInstances)
    {
        UMocapRecorderComponent* R = S.Recorder.Get();
        if (R && R->bIsRecording)
        {
            R->SampleFrame();
        }
    }


    const float Interval = 1.f / FMath::Max(1.f, CaptureSampleRateHz);
    TickAutoStop(Interval);

    ++SessionSampleCounter;

}

// ------------------------------------------------------------
// Spawn hook
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::BindSpawnHook()
{
    if (!World || ActorSpawnedHandle.IsValid())
        return;

    ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
        FOnActorSpawned::FDelegate::CreateUObject(this, &UMocapCaptureEditorSessionManager::OnActorSpawned)
    );

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Resolve: Spawn hook bound (World=%s)."), *GetNameSafe(World));
}

void UMocapCaptureEditorSessionManager::UnbindSpawnHook()
{
    if (World && ActorSpawnedHandle.IsValid())
    {
        World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
        ActorSpawnedHandle.Reset();
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Resolve: Spawn hook unbound."));
    }
}

void UMocapCaptureEditorSessionManager::OnActorSpawned(AActor* SpawnedActor)
{
    if (!bIsRecording || !IsValid(SpawnedActor))
    {
        return;
    }

    // Skeletal-only gate: spawned actor must have a SkeletalMeshComponent with a mesh
    USkeletalMeshComponent* SkelComp = SpawnedActor->FindComponentByClass<USkeletalMeshComponent>();
    if (!IsValid(SkelComp) || !IsValid(SkelComp->GetSkeletalMeshAsset()))
    {
        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("AutoCapture: Skipping %s (no valid SkeletalMeshComponent/SkeletalMesh)"),
            *GetNameSafe(SpawnedActor));
        return;
    }

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("AutoCapture: OnActorSpawned actor=%s class=%s"),
        *GetNameSafe(SpawnedActor),
        *GetNameSafe(SpawnedActor->GetClass()));

    // Match against enabled rules (no TransformOnly path)
    for (const FMocapClassCaptureRule& Rule : ClassRules)
    {
        if (!Rule.bEnabled)
        {
            continue;
        }

        UClass* RuleClass = Rule.ActorClass.Get();
        if (!RuleClass)
        {
            continue;
        }

        if (!SpawnedActor->IsA(RuleClass))
        {
            continue;
        }

        if (Rule.RequiredTag != NAME_None && !SpawnedActor->ActorHasTag(Rule.RequiredTag))
        {
            continue;
        }

        // If your UI still has this flag, enforce it here (we are skeletal-only anyway)
        if (Rule.bRequireSkeletalMesh)
        {
            // already guaranteed above, but keep explicit for clarity
        }

        PendingAutoCaptureActors.Add(SpawnedActor);

        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("AutoCapture: Spawn matched rule class=%s Tag=%s (Pending=%d)"),
            *GetNameSafe(RuleClass),
            *Rule.RequiredTag.ToString(),
            PendingAutoCaptureActors.Num());

        return;
    }
}

void UMocapCaptureEditorSessionManager::ProcessPendingAutoCaptures(int32 MaxPerTick)
{
    if (PendingAutoCaptureActors.Num() <= 0)
        return;

    int32 Processed = 0;

    for (int32 i = PendingAutoCaptureActors.Num() - 1; i >= 0; --i)
    {
        if (Processed >= MaxPerTick)
            break;

        AActor* Actor = PendingAutoCaptureActors[i].Get();
        PendingAutoCaptureActors.RemoveAtSwap(i);

        if (!IsValid(Actor))
            continue;

        for (const FMocapClassCaptureRule& Rule : ClassRules)
        {
            if (!Rule.bEnabled)
                continue;

            UClass* RuleClass = Rule.ActorClass.Get();
            if (!RuleClass)
                continue;

            if (!Actor->IsA(RuleClass))
                continue;

            if (Rule.RequiredTag != NAME_None && !Actor->ActorHasTag(Rule.RequiredTag))
                continue;

            TryAutoCaptureActor(Actor, Rule);
            break;
        }

        ++Processed;
    }
}

void UMocapCaptureEditorSessionManager::SweepWorldForAutoCapture(int32 MaxToQueueThisTick)
{
    if (!bIsRecording)
        return;

    UWorld* W = nullptr;

#if WITH_EDITOR
    if (GEditor && GEditor->PlayWorld)
    {
        W = GEditor->PlayWorld;
    }
#endif

    if (!W)
    {
        W = GetWorld();
    }

    if (!IsValid(W))
        return;

    const int32 MaxToQueue = FMath::Max(0, MaxToQueueThisTick);
    if (MaxToQueue == 0)
        return;

    // If no enabled rules have a loaded class, don’t sweep.
    bool bAnyRuleEnabled = false;
    for (const FMocapClassCaptureRule& Rule : ClassRules)
    {
        if (!Rule.bEnabled)
            continue;

        // ActorClass is TSoftClassPtr<AActor>
        UClass* RuleClass = Rule.ActorClass.Get(); // non-loading; null if not loaded
        if (RuleClass != nullptr)
        {
            bAnyRuleEnabled = true;
            break;
        }
    }
    if (!bAnyRuleEnabled)
        return;

    int32 Queued = 0;

    for (TActorIterator<AActor> It(W); It; ++It)
    {
        if (Queued >= MaxToQueue)
            break;

        AActor* A = *It;
        if (!IsValid(A))
            continue;

        // Already captured or pending?
        if (SeenAutoCaptureActors.Contains(A))
            continue;

        const FMocapClassCaptureRule* MatchRule = nullptr;

        for (const FMocapClassCaptureRule& Rule : ClassRules)
        {
            if (!Rule.bEnabled)
                continue;

            UClass* RuleClass = Rule.ActorClass.Get(); // non-loading
            if (RuleClass == nullptr)
                continue;

            if (A->IsA(RuleClass))
            {
                MatchRule = &Rule;
                break;
            }
        }

        if (!MatchRule)
            continue;

        PendingAutoCaptureActors.Add(A);
        SeenAutoCaptureActors.Add(A);
        ++Queued;
    }
}

bool UMocapCaptureEditorSessionManager::TryAutoCaptureActor(AActor* Actor, const FMocapClassCaptureRule& Rule)
{
    if (!Actor)
        return false;

    if (ActiveInstances.Num() >= MaxActiveAutoInstances)
        return false;

    // Avoid duplicates
    for (const FMocapInstanceState& Existing : ActiveInstances)
    {
        if (Existing.Actor.Get() == Actor)
            return false;
    }

    // Skeletal-only policy: MUST have a SkeletalMeshComponent.
    USkeletalMeshComponent* Skel = FindFirstSkeletalMeshComponent(Actor);
    if (!Skel)
        return false;

    UMocapRecorderComponent* Recorder = Actor->FindComponentByClass<UMocapRecorderComponent>();
    if (!Recorder)
    {
        Recorder = NewObject<UMocapRecorderComponent>(Actor, UMocapRecorderComponent::StaticClass(), NAME_None, RF_Transactional);
        Recorder->RegisterComponent();
    }

    // Configure recorder (skeletal-only)
    Recorder->TargetSkeletalMesh = Skel;
    Recorder->SampleRate = CaptureSampleRateHz;
    Recorder->bExternalSampling = true;
    Recorder->bAutoExportOnStop = false;

    // IMPORTANT: set start index BEFORE starting capture
    Recorder->StartSampleIndex = SessionSampleCounter;

    // Track instance state (skeletal-only)
    FMocapInstanceState S;
    S.Actor = Actor;
    S.SkelComp = Skel;
    S.Recorder = Recorder;
    S.LastLocation = Actor->GetActorLocation();
    S.StationarySeconds = 0.f;
    S.bStopRequested = false;
    S.Settings = Rule.AutoStop;

    // Used for consistent timing + stop logic
    S.SpawnSampleIndex = SessionSampleCounter;

    // Start recording (skeletal-only)
    Recorder->StartRecording_ExternalWithPreRoll(SessionSampleCounter);

    // Store name now so we still have it even if actor gets destroyed
    S.OutputNameOverride = MakeDefaultAssetName(Actor);

    // Capture a stable mesh asset identity now (actor may be destroyed later)
    // Prefer StaticMesh if present; fallback to SkeletalMesh.
    {
        if (UStaticMeshComponent* SM = Actor->FindComponentByClass<UStaticMeshComponent>())
        {
            if (SM->GetStaticMesh())
            {
                S.SourceMeshAssetPath = SM->GetStaticMesh()->GetPathName();
            }
        }

        if (S.SourceMeshAssetPath.IsEmpty() && Skel)
        {
            if (USkeletalMesh* SkelMesh = Skel->GetSkeletalMeshAsset())
            {
                S.SourceMeshAssetPath = SkelMesh->GetPathName();
            }
        }
    }

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("Session: AutoCapture START %s SkeletalOnly SampleStart=%d"),
        *GetNameSafe(Actor),
        SessionSampleCounter);

    ActiveInstances.Add(S);

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("AutoCapture: Added instance. ActiveInstances=%d Actor=%s"),
        ActiveInstances.Num(),
        *GetNameSafe(Actor));

    // Bind auto-stop events
    if (Rule.AutoStop.bStopOnDestroyed)
    {
        Actor->OnDestroyed.AddUniqueDynamic(this, &UMocapCaptureEditorSessionManager::HandleAutoCapturedActorDestroyed);
    }
    if (Rule.AutoStop.bStopOnHitEvent)
    {
        Actor->OnActorHit.AddUniqueDynamic(this, &UMocapCaptureEditorSessionManager::HandleAutoCapturedActorHit);
    }

    UE_LOG(LogTemp, Warning, TEXT("MocapSession: Auto-captured spawned actor %s"), *GetNameSafe(Actor));
    return true;
}

// ------------------------------------------------------------
// Auto-stop policies
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::RequestStopForActor(AActor* Actor)
{
    if (!Actor)
        return;

    for (FMocapInstanceState& S : ActiveInstances)
    {
        if (S.Actor.Get() == Actor)
        {
            S.bStopRequested = true;
            return;
        }
    }
}

void UMocapCaptureEditorSessionManager::HandleAutoCapturedActorDestroyed(AActor* DestroyedActor)
{
    RequestStopForActor(DestroyedActor);
}

void UMocapCaptureEditorSessionManager::HandleAutoCapturedActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
    RequestStopForActor(SelfActor);
}

APawn* UMocapCaptureEditorSessionManager::GetPrimaryPlayerPawn() const
{
    return (World) ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
}

bool UMocapCaptureEditorSessionManager::IsOutOfPlayerRadius(AActor* Actor, float Radius) const
{
    if (!Actor || Radius <= 0.f)
        return false;

    APawn* Player = GetPrimaryPlayerPawn();
    if (!Player)
        return false;

    return FVector::DistSquared(Actor->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(Radius);
}

static bool ExportMeshAssetToFbx_IfMissing(const FString& MeshAssetPath, FString& OutMeshFbxPath)
{
#if !WITH_EDITOR
    return false;
#else
    OutMeshFbxPath.Reset();

    if (MeshAssetPath.IsEmpty())
        return false;

    UObject* Asset = LoadObject<UObject>(nullptr, *MeshAssetPath);
    if (!IsValid(Asset))
        return false;

    const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MocapExports"), TEXT("Meshes"));
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    PF.CreateDirectoryTree(*OutDir);

    const FString CleanName = Asset->GetName();
    const FString OutFile = FPaths::Combine(OutDir, CleanName + TEXT(".fbx"));
    OutMeshFbxPath = OutFile;

    // Don’t re-export if it already exists
    if (PF.FileExists(*OutFile))
        return true;

    UAssetExportTask* Task = NewObject<UAssetExportTask>();
    Task->Object = Asset;
    Task->Filename = OutFile;
    Task->bSelected = false;
    Task->bReplaceIdentical = true;
    Task->bPrompt = false;
    Task->bAutomated = true;

    const bool bOk = UExporter::RunAssetExportTask(Task);

    if (!bOk)
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Mesh export FAILED for asset: %s"), *MeshAssetPath);
        OutMeshFbxPath.Reset();
        return false;
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Mesh export OK: %s -> %s"), *MeshAssetPath, *OutFile);
    return true;
#endif
}

void UMocapCaptureEditorSessionManager::FinalizeAutoInstanceOutput(const FMocapInstanceState& S, UMocapRecorderComponent* Recorder)
{
    if (!IsValid(Recorder))
        return;

    const bool bTransformOnly = Recorder->IsTransformOnly();

    const int32 NumFrames =
        bTransformOnly
        ? Recorder->GetRecordedTransformFrames().Num()
        : Recorder->GetRecordedFrames().Num();

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("FinalizeAutoInstanceOutput: Actor=%s Frames=%d TransformOnly=%d Skeleton=%s"),
        *GetNameSafe(Recorder->GetOwner()),
        NumFrames,
        bTransformOnly ? 1 : 0,
        *GetNameSafe(Recorder->GetRecordedSkeleton()));

    if (NumFrames <= 0 || !IsValid(Recorder->GetOwner()))
    {
        return;
    }

    // IMPORTANT: TransformOnly should bake like skeletal (no TransformOnlyBakeSkeleton dependency)
    if (bAutoBakeOnStop)
    {
        UMocapRecorderComponent* Snapshot = Recorder->CreateBakeSnapshot();
        if (IsValid(Snapshot))
        {
            FMocapBakeJob Job;
            Job.RecorderSnapshot = TStrongObjectPtr<UMocapRecorderComponent>(Snapshot);

            Job.AssetName =
                !S.OutputNameOverride.IsEmpty()
                ? S.OutputNameOverride
                : MakeDefaultAssetName(Recorder->GetOwner());

            PendingBakeJobs.Add(MoveTemp(Job));

            UE_LOG(LogMocapRecorderEditor, Warning,
                TEXT("FinalizeAutoInstanceOutput: Enqueued BAKE job. PendingBakeJobs=%d"),
                PendingBakeJobs.Num());
        }
    }
}

void UMocapCaptureEditorSessionManager::TickAutoStop(float DeltaTime)
{
    // 1) Process active instances: stop/remove as needed.
    for (int32 i = ActiveInstances.Num() - 1; i >= 0; --i)
    {
        FMocapInstanceState& S = ActiveInstances[i];

        AActor* Actor = S.Actor.Get();
        UMocapRecorderComponent* R = S.Recorder.Get();

        // If recorder is gone, drop instance
        if (!IsValid(R))
        {
            ActiveInstances.RemoveAtSwap(i);
            continue;
        }

        // If actor is gone, finalize what we have and drop
        if (!IsValid(Actor))
        {
            FinalizeAutoInstanceOutput(S, R);
            ActiveInstances.RemoveAtSwap(i);
            continue;
        }

        // If already stopped, finalize once and drop
        if (!R->bIsRecording)
        {
            FinalizeAutoInstanceOutput(S, R);
            ActiveInstances.RemoveAtSwap(i);
            continue;
        }

        const bool bStopNow = S.bStopRequested;
        if (bStopNow)
        {
            R->StopRecording_External();

            // enqueue bake job (including transform-only)
            FinalizeAutoInstanceOutput(S, R);

            ActiveInstances.RemoveAtSwap(i);
        }
    }

    // 2) Decide whether the session should end (ONCE per tick).
    int32 EnabledTargetsCount = 0;
    for (const FMocapEditorSessionTarget& T : Targets)
    {
        if (T.bEnabled)
        {
            ++EnabledTargetsCount;
        }
    }

    bool bAnyRuleEnabled = false;
    for (const FMocapClassCaptureRule& Rule : ClassRules)
    {
        if (Rule.bEnabled && !Rule.ActorClass.IsNull())
        {
            bAnyRuleEnabled = true;
            break;
        }
    }

    if (EnabledTargetsCount == 0 && !bAnyRuleEnabled)
    {
        StopSession();
    }
}

// ------------------------------------------------------------
// Bake queue (deferred)
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::MaybeBeginBakeQueue()
{
    if (PendingBakeJobs.Num() <= 0)
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: MaybeBeginBakeQueue: no pending jobs."));
        return;
    }

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("StopSession: Added bake job. PendingBakeJobs=%d"),
        PendingBakeJobs.Num());


    // If PIE is active, defer baking until EndPIE
    if (GEditor && GEditor->PlayWorld)
    {
        bBakeDeferredUntilEndPIE = true;
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: Deferring bake until PIE ends (%d jobs)."), PendingBakeJobs.Num());
        return;
    }

    bBakeDeferredUntilEndPIE = false;
    BeginBakeQueue();
}

void UMocapCaptureEditorSessionManager::BeginBakeQueue()
{
    if (PendingBakeJobs.Num() <= 0)
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: BeginBakeQueue called with 0 jobs."));
        return;
    }

    // NEVER rely on "IsValid" to mean "registered" – handles can become stale.
    if (BakeTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(BakeTickerHandle);
        BakeTickerHandle.Reset();
    }

    bIsBaking = true;
    AddToRoot();


    if (NextBakeJobIndex < 0 || NextBakeJobIndex >= PendingBakeJobs.Num())
    {
        NextBakeJobIndex = 0;
    }

    BakeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMocapCaptureEditorSessionManager::TickBakeQueue),
        0.2f
    );

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: BeginBakeQueue (%d jobs). TickerValid=%d"),
        PendingBakeJobs.Num(),
        BakeTickerHandle.IsValid() ? 1 : 0);
}

bool UMocapCaptureEditorSessionManager::TickBakeQueue(float DeltaTime)
{
    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: TickBakeQueue Next=%d/%d bIsBaking=%d"),
        NextBakeJobIndex, PendingBakeJobs.Num(), bIsBaking ? 1 : 0);

    if (!bIsBaking)
        return false;

    if (NextBakeJobIndex >= PendingBakeJobs.Num())
    {
        EndBakeQueue();

        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: Baking complete. Saving dirty packages..."));
        FEditorFileUtils::SaveDirtyPackages(false, true, true, false, false, false);
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: SaveDirtyPackages finished."));

        return false;
    }

    FMocapBakeJob& Job = PendingBakeJobs[NextBakeJobIndex];

    UMocapRecorderComponent* SnapshotRecorder = Job.RecorderSnapshot.Get();
    if (!IsValid(SnapshotRecorder))
    {
        UE_LOG(LogMocapRecorderEditor, Error,
            TEXT("BakeQueue: Job idx=%d has invalid snapshot - SKIPPING"),
            NextBakeJobIndex);

        ++NextBakeJobIndex;
        return true; // keep ticking to continue queue
    }

    const bool bTO = SnapshotRecorder->IsTransformOnly();
    const int32 FrameCount =
        bTO
        ? SnapshotRecorder->GetRecordedTransformFrames().Num()
        : SnapshotRecorder->GetRecordedFrames().Num();

    if (FrameCount <= 0)
    {
        UE_LOG(LogMocapRecorderEditor, Error,
            TEXT("BakeQueue: Job idx=%d snapshot has 0 frames - SKIPPING"),
            NextBakeJobIndex);

        ++NextBakeJobIndex;
        return true;
    }

    if (!AssetPath.StartsWith(TEXT("/Game")))
    {
        UE_LOG(LogTemp, Error,
            TEXT("Session: Invalid AssetPath '%s' (must start with /Game). Forcing /Game/MocapCaptures"),
            *AssetPath);

        AssetPath = TEXT("/Game/MocapCaptures");
    }

    UE_LOG(
        LogMocapRecorderEditor,
        Warning,
        TEXT("BakeQueue: BakeCall idx=%d/%d path=%s name=%s owner=%s frames=%d skel=%s"),
        NextBakeJobIndex + 1,
        PendingBakeJobs.Num(),
        *AssetPath,
        *Job.AssetName,
        *GetNameSafe(SnapshotRecorder->GetOwner()),
        FrameCount,
        *GetNameSafe(SnapshotRecorder->GetRecordedSkeleton())
    );

    FMocapRecorderEditorModule& Mod =
        FModuleManager::LoadModuleChecked<FMocapRecorderEditorModule>("MocapRecorderEditor");

    UAnimSequence* Anim = Mod.BakeAnimSequenceFromRecorder(
        SnapshotRecorder,
        AssetPath,
        Job.AssetName,
        ExportFrameRateFps
    );

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: BakeReturn idx=%d name=%s -> %s"),
        NextBakeJobIndex + 1,
        *Job.AssetName,
        Anim ? *Anim->GetPathName() : TEXT("NULL"));

    if (!Anim)
    {
        UE_LOG(LogTemp, Error, TEXT("BakeQueue: Bake FAILED for %s (returned nullptr)."), *Job.AssetName);
    }
    else
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: Bake OK -> %s"), *GetNameSafe(Anim));
    }

    ++NextBakeJobIndex;
    return true;
}

void UMocapCaptureEditorSessionManager::EndBakeQueue()
{
    if (BakeTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(BakeTickerHandle);
        BakeTickerHandle.Reset();
    }

    bIsBaking = false;
    RemoveFromRoot();

}

void UMocapCaptureEditorSessionManager::ClearBakeQueue()
{
    // Stop any running bake ticker
    EndBakeQueue();

    // Stop post-PIE kick ticker too (prevents old queue being started later)
    if (PostPIEBakeKickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PostPIEBakeKickHandle);
        PostPIEBakeKickHandle.Reset();
    }

    PendingBakeJobs.Reset();
    NextBakeJobIndex = 0;
    bIsBaking = false;
    bBakeDeferredUntilEndPIE = false;

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: ClearBakeQueue -> cleared."));
}

void UMocapCaptureEditorSessionManager::GetBakeQueueStatus(int32& OutDone, int32& OutTotal, FString& OutCurrentAssetName, bool& bOutWaitingForCompilation) const
{
    OutDone = NextBakeJobIndex;
    OutTotal = PendingBakeJobs.Num();
    OutCurrentAssetName = (PendingBakeJobs.IsValidIndex(NextBakeJobIndex)) ? PendingBakeJobs[NextBakeJobIndex].AssetName : FString();
    bOutWaitingForCompilation = false;
}

bool UMocapCaptureEditorSessionManager::TickPostPIEBakeKick(float DeltaTime)
{
    // Stop if nothing to do
    if (PendingBakeJobs.Num() <= 0)
    {
        PostPIEBakeKickHandle.Reset();
        return false;
    }

    // Wait until PIE is fully down
    if (GEditor && GEditor->PlayWorld)
    {
        return true; // keep ticking
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: PostPIE bake kick -> starting bake queue (%d jobs)."), PendingBakeJobs.Num());

    PostPIEBakeKickHandle.Reset();
    BeginBakeQueue();
    return false; // stop ticking
}



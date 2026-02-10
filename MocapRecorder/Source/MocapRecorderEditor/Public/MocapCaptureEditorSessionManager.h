#pragma once

#include "CoreMinimal.h"
#include "MocapCaptureMode.h"

#include "MocapCaptureEditorSessionManager.generated.h"

class AActor;
class APawn;
class UWorld;
class UMocapRecorderComponent;
class USkeletalMeshComponent;
class USkeleton;
class UAnimSequence;

struct FHitResult;


/**
 * Editor-only target record used by Slate/UI.
 * Not a USTRUCT intentionally: avoids global UHT name collisions and keeps UI data lightweight.
 */
struct FMocapEditorSessionTarget
{
    FGuid ActorGuid;
    FString LastKnownLabel;

    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<USkeletalMeshComponent> SkelComp;

    bool bEnabled = true;
    FString OutputNameOverride;

    TWeakObjectPtr<UMocapRecorderComponent> Recorder;
};

// ============================================================
// Instance / class-based capture rules ("record all instances")
// ============================================================

USTRUCT(BlueprintType)
struct FMocapAutoStopSettings

{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    bool bStopWhenNearlyStationary = true;

    UPROPERTY(EditAnywhere)
    float LinearSpeedThreshold = 5.f;

    UPROPERTY(EditAnywhere)
    float StationaryHoldSeconds = 0.25f;

    UPROPERTY(EditAnywhere)
    bool bStopWhenOutOfPlayerRadius = false;

    UPROPERTY(EditAnywhere)
    float PlayerRadius = 5000.f;

    UPROPERTY(EditAnywhere)
    bool bStopOnHitEvent = false;

    UPROPERTY(EditAnywhere)
    bool bStopOnDestroyed = true;

    UPROPERTY(EditAnywhere)
    bool bAutoBakeOnAutoStop = true;
};

USTRUCT(BlueprintType)
struct FMocapClassCaptureRule
{
    GENERATED_BODY()   

    UPROPERTY(EditAnywhere)
    TSoftClassPtr<AActor> ActorClass;

    UPROPERTY(EditAnywhere)
    bool bEnabled = true;

    UPROPERTY(EditAnywhere)
    FName RequiredTag = NAME_None;

    UPROPERTY(EditAnywhere)
    bool bRequireSkeletalMesh = false;

    UPROPERTY(EditAnywhere)
    bool bTransformOnly = false;    

    UPROPERTY(EditAnywhere)
    EMocapCaptureMode CaptureMode = EMocapCaptureMode::Skeletal;

    UPROPERTY(EditAnywhere)
    FMocapAutoStopSettings AutoStop;
};


struct FMocapInstanceState
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<USkeletalMeshComponent> SkelComp;
    TWeakObjectPtr<UMocapRecorderComponent> Recorder;
    FVector LastLocation = FVector::ZeroVector;
    float StationarySeconds = 0.f;
    bool bStopRequested = false;
    // Session frame index when this actor was spawned/capture-started
    int32 SpawnSampleIndex = 0;
    bool bTransformOnly = false;
    // transform-only (no skeleton)
    EMocapCaptureMode CaptureMode = EMocapCaptureMode::Skeletal;
    FMocapAutoStopSettings Settings;
    FString OutputNameOverride;

    // Asset path for the mesh that visually represents this instance.
    // Example:
    //  - StaticMesh: /Game/Weapons/SM_Bullet.SM_Bullet
    //  - SkeletalMesh: /Game/Characters/SK_Bullet.SK_Bullet
    FString SourceMeshAssetPath;
};



UCLASS()
class MOCAPRECORDEREDITOR_API UMocapCaptureEditorSessionManager : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(UWorld* InWorld);
    virtual void BeginDestroy() override;

    // ------------------------------------------------------------
    // Targets (selected actors)
    // ------------------------------------------------------------
    void AddSelectedActorsFromOutliner();
    void ClearTargets();
    const TArray<FMocapEditorSessionTarget>& GetTargets() const { return Targets; }
    void SetTargetEnabled(int32 Index, bool bEnabled);

    // ------------------------------------------------------------
    // Class Rules (spawned instances)
    // ------------------------------------------------------------
    const TArray<FMocapClassCaptureRule>& GetClassRules() const { return ClassRules; }

    void AddClassRule();
    void RemoveClassRule(int32 Index);
    void ClearClassRules();
    void SetClassRuleEnabled(int32 Index, bool bEnabled);
    void SetClassRuleClass(int32 Index, UClass* InClass);
    void SetClassRuleRequiredTag(int32 Index, FName InTag);
    void SetClassRuleRequireSkeletalMesh(int32 Index, bool bIn);
    void SetClassRuleTransformOnly(int32 Index, bool bIn);


    void SetRule_StopWhenNearlyStationary(int32 Index, bool bIn);
    void SetRule_LinearSpeedThreshold(int32 Index, float V);
    void SetRule_StationaryHoldSeconds(int32 Index, float V);

    void SetRule_StopWhenOutOfPlayerRadius(int32 Index, bool bIn);
    void SetRule_PlayerRadius(int32 Index, float V);

    void SetRule_StopOnHitEvent(int32 Index, bool bIn);
    void SetRule_StopOnDestroyed(int32 Index, bool bIn);

    void SetRule_AutoBakeOnAutoStop(int32 Index, bool bIn);

    // ------------------------------------------------------------
    // Session settings
    // ------------------------------------------------------------
    void SetCaptureSampleRateHz(float InHz) { CaptureSampleRateHz = FMath::Max(1.f, InHz); }
    void SetExportFrameRateFps(int32 InFps) { ExportFrameRateFps = FMath::Clamp(InFps, 1, 240); }
    void SetAssetPath(const FString& InPath) { AssetPath = InPath; }
    void SetAutoBakeOnStop(bool bIn) { bAutoBakeOnStop = bIn; }

    float GetCaptureSampleRateHz() const { return CaptureSampleRateHz; }
    int32 GetExportFrameRateFps() const { return ExportFrameRateFps; }
    const FString& GetAssetPath() const { return AssetPath; }
    bool GetAutoBakeOnStop() const { return bAutoBakeOnStop; }

    // ------------------------------------------------------------
    // Control
    // ------------------------------------------------------------
    bool StartSession();
    void StopSession();
    void SampleAll();

    bool IsRecording() const { return bIsRecording; }
    bool IsBaking() const { return bIsBaking; }

    void GetBakeQueueStatus(int32& OutDone, int32& OutTotal, FString& OutCurrentAssetName, bool& bOutWaitingForCompilation) const;

    // Clears pending bake jobs and stops any active bake ticker.
    // Use between recording sessions to prevent old jobs baking on later PIE closes.
    UFUNCTION()
    void ClearBakeQueue();


private:
    // Dynamic delegate handlers (no AddLambda on dynamic delegates)
    UFUNCTION()
    void HandleAutoCapturedActorDestroyed(AActor* DestroyedActor);

    UFUNCTION()
    void HandleAutoCapturedActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);
    void FinalizeAutoInstanceOutput(const FMocapInstanceState& S, UMocapRecorderComponent* Recorder);

private:
    // One deferred bake task (processed incrementally to avoid editor freeze)
    struct FMocapBakeJob
    {
        // Snapshot that survives PIE teardown
        TStrongObjectPtr<UMocapRecorderComponent> RecorderSnapshot;
        FString AssetName;
    };




    // World + targets
    UWorld* World = nullptr;


    TArray<FMocapEditorSessionTarget> Targets;

    // Class rules + instances
    UPROPERTY()
    TArray<FMocapClassCaptureRule> ClassRules;

    TArray<FMocapInstanceState> ActiveInstances;

    // Spawn hook
    FDelegateHandle ActorSpawnedHandle;

    // Spawn queue (do not attach components inside spawn callback)
    TArray<TWeakObjectPtr<AActor>> PendingAutoCaptureActors;

    // Limits (avoid runaway bullets)

    // Session timeline sample counter (increments once per SampleAll tick)
    int32 SessionSampleCounter = 0;

    int32 MaxAutoCapturePerTick = 512;
    int32 MaxActiveAutoInstances = 2048;

    // Session params
    float CaptureSampleRateHz = 60.f;
    int32 ExportFrameRateFps = 30;
    FString AssetPath = TEXT("/Game/MocapCaptures");
    bool bAutoBakeOnStop = true;

    bool bIsRecording = false;
    FTimerHandle SessionTimerHandle;

    // Bake queue
    TArray<FMocapBakeJob> PendingBakeJobs;
    int32 NextBakeJobIndex = 0;
    bool bIsBaking = false;
    FTSTicker::FDelegateHandle BakeTickerHandle;
       
private:

    void SweepWorldForAutoCapture(int32 MaxToQueueThisTick);


    // Actors we've already decided to capture (active OR pending), to avoid duplicates.
    TSet<TWeakObjectPtr<AActor>> SeenAutoCaptureActors;

    // Optional: if you want to sweep every tick but with a budget
    int32 SweepBudgetPerTick = 512;


    // Post-PIE bake kick (EndPIE can fire before PlayWorld is nulled)
    FTSTicker::FDelegateHandle PostPIEBakeKickHandle;
    bool TickPostPIEBakeKick(float DeltaTime);

    // Bake deferral (PIE safety)
    bool bBakeDeferredUntilEndPIE = false;
    
    // Core helpers
    bool ResolveOrAttachRecorder(FMocapEditorSessionTarget& T);
    void ResolveTargetsForWorld(UWorld* InWorld);
    static AActor* FindActorByGuid(UWorld* InWorld, const FGuid& Guid);

    static USkeletalMeshComponent* FindFirstSkeletalMeshComponent(AActor* Actor);
    static FString MakeDefaultAssetName(AActor* Actor);

    // PIE lifecycle
    void OnBeginPIE(const bool bIsSimulating);
    void OnEndPIE(const bool bIsSimulating);

    // Spawn hook + processing
    void BindSpawnHook();
    void UnbindSpawnHook();
    void OnActorSpawned(AActor* SpawnedActor);

    // PIE lifecycle delegate handles
    FDelegateHandle BeginPIEHandle;
    FDelegateHandle EndPIEHandle;


    void ProcessPendingAutoCaptures(int32 MaxPerTick);
    bool TryAutoCaptureActor(AActor* Actor, const FMocapClassCaptureRule& Rule);

    void RequestStopForActor(AActor* Actor);
    void TickAutoStop(float DeltaTime);
    APawn* GetPrimaryPlayerPawn() const;
    bool IsOutOfPlayerRadius(AActor* Actor, float Radius) const;

    // Baking control
    void MaybeBeginBakeQueue();
    void BeginBakeQueue();
    bool TickBakeQueue(float DeltaTime);
    void EndBakeQueue();

    // Transform-only export stub (for Blender-oriented bullets/casings)
    void ExportTransformOnly(UMocapRecorderComponent* Recorder, const FString& AssetName, int32 SpawnSampleIndex, const FString& SourceMeshFbxPath);



};

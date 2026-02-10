#include "MocapRecorderEditorModule.h"
#include "SMocapRecorderPanel.h"
#include "MocapRecorderVersion.h"

#include "MocapRecorderComponent.h"

#include "Misc/CoreDelegates.h"

#include "ToolMenus.h"

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"

#include "AssetRegistry/AssetRegistryModule.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"

#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "AssetToolsModule.h"
#include "Factories/AnimSequenceFactory.h"
#include "MocapCaptureEditorSessionManager.h"
#include "Editor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

static const TCHAR* GMocapRecorderEditor_VersionFingerprint = TEXT("2026-02-02 Step1-HygieneLock P");


DEFINE_LOG_CATEGORY(LogMocapRecorderEditor);

#ifndef MOCAP_BAKE_STAGE
// 0 = full bake
// 1 = create asset only (no controller work)
// 2 = create + controller open bracket only
// 3 = create + set frame rate/num frames/remove tracks (no keys)
// 4 = full keys (default)
#define MOCAP_BAKE_STAGE 0
#endif




#define LOCTEXT_NAMESPACE "MocapRecorderEditor"

static const FName MocapRecorderTabName("MocapRecorder");

FMocapRecorderEditorModule& FMocapRecorderEditorModule::Get()
{
    return FModuleManager::LoadModuleChecked<FMocapRecorderEditorModule>("MocapRecorderEditor");
}

UMocapCaptureEditorSessionManager* FMocapRecorderEditorModule::GetOrCreateSessionManager(UWorld* InWorld)
{
    if (!SessionManager.IsValid())
    {
        UMocapCaptureEditorSessionManager* NewMgr = NewObject<UMocapCaptureEditorSessionManager>();
        NewMgr->AddToRoot(); // keep alive across PIE/widget teardown
        SessionManager = TStrongObjectPtr<UMocapCaptureEditorSessionManager>(NewMgr);
    }

    // Prefer PIE world if present; otherwise editor world.
    UWorld* WorldToUse = InWorld;
    if (GEditor && GEditor->PlayWorld)
    {
        WorldToUse = GEditor->PlayWorld.Get();
    }
    else if (GEditor)
    {
        WorldToUse = GEditor->GetEditorWorldContext().World();
    }

    SessionManager->Initialize(WorldToUse);
    return SessionManager.Get();
}

void FMocapRecorderEditorModule::StartupModule()
{
    if (IsRunningCommandlet())
    {
        return;
    }
    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("MocapRecorderEditor: Startup (%s)"), GMocapRecorderEditor_VersionFingerprint);


    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("MocapRecorderEditor: Startup (VERSION_FINGERPRINT 2026-02-01 J)"));


    // Do NOT touch Slate/ToolMenus/TabManager during StartupModule.
    // Defer until engine init is complete.
    FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMocapRecorderEditorModule::OnPostEngineInit);
}

void FMocapRecorderEditorModule::ShutdownModule()
{
    if (FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MocapRecorderTabName);
    }

    if (UToolMenus* Menus = UToolMenus::Get())
    {
        Menus->UnregisterOwner(this);
    }

    // IMPORTANT: During module shutdown, UObjects may already be tearing down.
    // Do not call RemoveFromRoot() here (it can touch dead UObject memory).
    SessionManager.Reset();



}

void FMocapRecorderEditorModule::OnPostEngineInit()
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }

    // Register the dockable tab
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        MocapRecorderTabName,
        FOnSpawnTab::CreateStatic(&FMocapRecorderEditorModule::SpawnMocapRecorderTab)
    )
        .SetDisplayName(LOCTEXT("MocapRecorderTabTitle", "Mocap Recorder"))
        .SetMenuType(ETabSpawnerMenuType::Enabled);

    // Register menus at a safe time
    if (UToolMenus* Menus = UToolMenus::Get())
    {
        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMocapRecorderEditorModule::RegisterMenus)
        );
    }
    else
    {
        // If ToolMenus isn't up yet, RegisterStartupCallback is still safe to call directly on the class in many builds.
        // If your build errors on this, remove the else branch.
        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMocapRecorderEditorModule::RegisterMenus)
        );
    }

}

TSharedRef<SDockTab> FMocapRecorderEditorModule::SpawnMocapRecorderTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SMocapRecorderPanel)
        ];
}

void FMocapRecorderEditorModule::RegisterMenus()
{
    UToolMenus* Menus = UToolMenus::Get();
    if (!Menus)
    {
        return;
    }


    // 1) Viewport right-click
    if (UToolMenu* ViewportMenu = Menus->ExtendMenu("LevelEditor.LevelViewportContextMenu"))
    {
        FToolMenuSection& Section =
            ViewportMenu->AddSection("MocapRecorderViewportSection", LOCTEXT("MocapRecorderSection", "Mocap Recorder"));

        Section.AddMenuEntry(
            "MocapRecorder_OpenViewport",
            LOCTEXT("OpenMocapRecorder", "Open Mocap Recorder"),
            LOCTEXT("OpenMocapRecorder_Tooltip", "Open the Mocap Recorder window."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(this, &FMocapRecorderEditorModule::OnOpenMocapRecorder))
        );
    }

    // 2) Actor right-click
    if (UToolMenu* ActorMenu = Menus->ExtendMenu("LevelEditor.ActorContextMenu"))
    {
        FToolMenuSection& Section =
            ActorMenu->AddSection("MocapRecorderActorSection", LOCTEXT("MocapRecorderSection2", "Mocap Recorder"));

        Section.AddMenuEntry(
            "MocapRecorder_OpenActor",
            LOCTEXT("OpenMocapRecorder2", "Open Mocap Recorder"),
            LOCTEXT("OpenMocapRecorder2_Tooltip", "Open the Mocap Recorder window."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(this, &FMocapRecorderEditorModule::OnOpenMocapRecorder))
        );
    }

    // 3) Toolbar button
    if (UToolMenu* Toolbar = Menus->ExtendMenu("LevelEditor.LevelEditorToolBar"))
    {
        FToolMenuSection& Section = Toolbar->FindOrAddSection("Settings");

        FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
            "MocapRecorder_ToolbarButton",
            FUIAction(FExecuteAction::CreateRaw(this, &FMocapRecorderEditorModule::OnOpenMocapRecorder)),
            LOCTEXT("MocapToolbarLabel", "Mocap"),
            LOCTEXT("MocapToolbarTooltip", "Open the Mocap Recorder window."),
            FSlateIcon()
        );

        Section.AddEntry(Entry);
    }

    // 4) Window menu
    if (UToolMenu* WindowMenu = Menus->ExtendMenu("LevelEditor.MainMenu.Window"))
    {
        FToolMenuSection& Section =
            WindowMenu->AddSection("MocapRecorderWindowSection", LOCTEXT("MocapRecorderSection3", "Mocap Recorder"));

        Section.AddMenuEntry(
            "MocapRecorder_OpenWindowMenu",
            LOCTEXT("OpenMocapRecorder3", "Open Mocap Recorder"),
            LOCTEXT("OpenMocapRecorder3_Tooltip", "Open the Mocap Recorder window."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(this, &FMocapRecorderEditorModule::OnOpenMocapRecorder))
        );
    }
}

void FMocapRecorderEditorModule::OnOpenMocapRecorder()
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }

    FGlobalTabmanager::Get()->TryInvokeTab(MocapRecorderTabName);
}

// ------------------------------------------------------------------
// Baking
// ------------------------------------------------------------------

// ============================================================================
// Bake stage gate for freeze isolation
// 1 = Create asset only
// 2 = + GetController + OpenBracket
// 3 = + Set frame rate / num frames / remove tracks
// 4 = Full bake (keys)
// ============================================================================
#define MOCAP_BAKE_STAGE 4

// ------------------------------------------------------------
// TransformOnly: generate / reuse a single-bone skeleton ("root")
// ------------------------------------------------------------
// ------------------------------------------------------------
// TransformOnly: generate / reuse a single-bone skeleton ("root")
// UE 5.6 SAFE
// ------------------------------------------------------------
// ------------------------------------------------------------
// TransformOnly: generate / reuse a single-bone skeleton ("root")
// UE5.6-safe: MergeAllBonesToBoneTree now expects a USkinnedAsset*.
// We create a transient USkeletalMesh containing a 1-bone RefSkeleton,
// then merge that into a transient USkeleton.
// ------------------------------------------------------------
static USkeleton* Mocap_GetOrCreateTransformOnlyRootSkeleton()
{
    static TObjectPtr<USkeleton> CachedSkeleton = nullptr;
    static TObjectPtr<USkeletalMesh> CachedDummyMesh = nullptr;

    if (IsValid(CachedSkeleton))
    {
        return CachedSkeleton.Get();
    }

    // Create transient skeleton
    USkeleton* Skel = NewObject<USkeleton>(
        GetTransientPackage(),
        USkeleton::StaticClass(),
        NAME_None,
        RF_Transient
    );

    if (!IsValid(Skel))
    {
        return nullptr;
    }

    // Create transient skeletal mesh (USkinnedAsset) holding a 1-bone ref skeleton
    USkeletalMesh* DummyMesh = NewObject<USkeletalMesh>(
        GetTransientPackage(),
        USkeletalMesh::StaticClass(),
        NAME_None,
        RF_Transient
    );  

    if (!IsValid(DummyMesh))
    {
        return nullptr;
    }

    // Build the mesh's ReferenceSkeleton with one bone: "root"
    {
        const FName RootName(TEXT("root"));

        FReferenceSkeleton BuiltRefSkel;
        {
            // IMPORTANT: constructor expects USkeleton* owner in your build
            FReferenceSkeletonModifier Mod(BuiltRefSkel, Skel);

            FMeshBoneInfo RootInfo(RootName, RootName.ToString(), INDEX_NONE);
            Mod.Add(RootInfo, FTransform::Identity);
        }

        // Assign the ref skeleton onto the dummy mesh (SetRefSkeleton not public in some builds)
        FReferenceSkeleton& Ref = const_cast<FReferenceSkeleton&>(DummyMesh->GetRefSkeleton());
        Ref = BuiltRefSkel;
    }



    // Merge bones from the dummy skinned asset into the skeleton bone tree
    const bool bShowProgress = false;
    // Merge bones from the dummy skinned asset into the skeleton bone tree
    const bool bOK = Skel->MergeAllBonesToBoneTree(DummyMesh, false);
    if (!bOK)
    {
        UE_LOG(LogTemp, Error, TEXT("Mocap: MergeAllBonesToBoneTree failed for TransformOnly root skeleton"));
        return nullptr;
    }   

    // Optional: attach preview mesh so editor tools are happier
    Skel->SetPreviewMesh(DummyMesh);

    CachedDummyMesh = DummyMesh;
    CachedSkeleton = Skel;
    return Skel;
}

UAnimSequence* FMocapRecorderEditorModule::BakeAnimSequenceFromRecorder(
    UMocapRecorderComponent* Recorder,
    const FString& PackagePath,
    const FString& AssetName,
    int32 ExportFPS)
{
    if (!IsValid(Recorder))
        return nullptr;

    USkeleton* Skeleton = Recorder->GetRecordedSkeleton();
    if (!IsValid(Skeleton))
    {
        UE_LOG(LogTemp, Error,
            TEXT("Bake FAILED: RecordedSkeleton invalid for %s"),
            *GetNameSafe(Recorder->GetOwner()));
        return nullptr;
    }

    const TArray<FMocapFrame>& Frames = Recorder->GetRecordedFrames();
    const TArray<FName>& BoneNames = Recorder->GetRecordedBoneNames();

    if (Frames.Num() == 0 || BoneNames.Num() == 0)
        return nullptr;

    const int32 SourceFPS = FMath::Max(1, FMath::RoundToInt(Recorder->GetRecordedSampleRate()));
    ExportFPS = FMath::Clamp(ExportFPS, 1, 240);

    // Use time-based stepping (more stable than integer division when rates don’t divide cleanly).
    const double SrcDt = 1.0 / (double)SourceFPS;
    const double OutDt = 1.0 / (double)ExportFPS;

    // Duration based on source frames
    const double Duration = (Frames.Num() - 1) * SrcDt;

    // Number of output frames including both endpoints
    const int32 OutFrames = FMath::Max(1, (int32)FMath::FloorToInt(Duration / OutDt) + 1);


    UAnimSequenceFactory* Factory = NewObject<UAnimSequenceFactory>();
    Factory->TargetSkeleton = Skeleton;

    FAssetToolsModule& AssetTools =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

    UAnimSequence* Anim =
        Cast<UAnimSequence>(AssetTools.Get().CreateAsset(
            AssetName,
            PackagePath,
            UAnimSequence::StaticClass(),
            Factory));

    if (!IsValid(Anim))
        return nullptr;

    IAnimationDataController& Controller = Anim->GetController();
    Controller.OpenBracket(LOCTEXT("Bake", "Mocap Bake"));

#if VMC_UE_AT_LEAST(5, 3)
    // 5.3+ is consistent with these names
    Controller.SetFrameRate(FFrameRate(ExportFPS, 1));
    Controller.SetNumberOfFrames(OutFrames);
#else
    // 5.2 and earlier UE5 minors can be pickier; SetFrameRate exists, but we keep the pattern conservative.
    Controller.SetFrameRate(FFrameRate(ExportFPS, 1));
    Controller.SetNumberOfFrames(OutFrames);
#endif

    Controller.RemoveAllBoneTracks();


    for (int32 BoneIdx = 0; BoneIdx < BoneNames.Num(); ++BoneIdx)
    {
        const FName BoneName = BoneNames[BoneIdx];
        Controller.AddBoneTrack(BoneName);

        TArray<FVector3f> Pos;
        TArray<FQuat4f> Rot;
        TArray<FVector3f> Scale;

        Pos.SetNum(OutFrames);
        Rot.SetNum(OutFrames);
        Scale.SetNum(OutFrames);

        for (int32 OutIdx = 0; OutIdx < OutFrames; ++OutIdx)
        {
            const double TSec = OutIdx * OutDt;
            const int32 SrcIdx = FMath::Clamp((int32)FMath::RoundToInt(TSec / SrcDt), 0, Frames.Num() - 1);

            const FMocapFrame& F = Frames[SrcIdx];

            const FVector T =
                F.Translations.IsValidIndex(BoneIdx)
                ? F.Translations[BoneIdx]
                : FVector::ZeroVector;

            const FQuat Q =
                F.Rotations.IsValidIndex(BoneIdx)
                ? F.Rotations[BoneIdx]
                : FQuat::Identity;

            Pos[OutIdx] = FVector3f(T);
            Rot[OutIdx] = FQuat4f(Q);
            Scale[OutIdx] = FVector3f(1, 1, 1);
        }

        Controller.SetBoneTrackKeys(BoneName, Pos, Rot, Scale);
    }

    Controller.CloseBracket();
    Anim->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Anim);

    UE_LOG(LogTemp, Warning,
        TEXT("Bake SUCCESS: %s"), *Anim->GetPathName());

    return Anim;
}



IMPLEMENT_MODULE(FMocapRecorderEditorModule, MocapRecorderEditor)

#undef LOCTEXT_NAMESPACE

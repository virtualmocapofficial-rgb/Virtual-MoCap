#include "SMocapRecorderPanel.h"

#include "Editor.h"
#include "Engine/World.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SBorder.h"
#include "PropertyCustomizationHelpers.h" // SClassPropertyEntryBox
#include "Widgets/Input/SEditableTextBox.h"
#include "MocapCaptureEditorSessionManager.h"
#include "MocapRecorderEditorModule.h"
#include "Misc/Optional.h"



// ------------------------------------------------------------
// Construct
// ------------------------------------------------------------

void SMocapRecorderPanel::Construct(const FArguments& InArgs)
{
    SessionManager = FMocapRecorderEditorModule::Get().GetOrCreateSessionManager(nullptr);

    RefreshTargetList();
    RefreshClassRuleList();


    ChildSlot
        [
            SNew(SVerticalBox)

                // ----------------------------
                // Controls
                // ----------------------------
                +SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(SHorizontalBox)

                        + SHorizontalBox::Slot().AutoWidth().Padding(2)
                        [
                            SNew(SButton)
                                .Text(FText::FromString(TEXT("Add Selected Actors")))
                                .OnClicked(this, &SMocapRecorderPanel::OnAddSelectedActors)
                        ]

                        + SHorizontalBox::Slot().AutoWidth().Padding(2)
                        [
                            SNew(SButton)
                                .Text(FText::FromString(TEXT("Clear")))
                                .OnClicked(this, &SMocapRecorderPanel::OnClearTargets)
                        ]

                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 2)
                        [
                            SNew(SButton)
                                .Text(FText::FromString(TEXT("REC")))
                                .IsEnabled_Lambda([this]() { return !IsRecording(); })
                                .OnClicked(this, &SMocapRecorderPanel::OnStartSession)
                        ]

                        + SHorizontalBox::Slot().AutoWidth().Padding(2)
                        [
                            SNew(SButton)
                                .Text(FText::FromString(TEXT("STOP")))
                                .IsEnabled_Lambda([this]() { return IsRecording(); })
                                .OnClicked(this, &SMocapRecorderPanel::OnStopSession)
                        ]
                ]

            // ----------------------------
            // Settings
            // ----------------------------
            +SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    BuildSettingsPanel()
                ]

            + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    BuildClassRulesPanel()
                ]

                                            
            // ----------------------------
            // Bake Progress
            // ----------------------------
            +SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                SNew(SBorder)
                .Padding(6)
                [
                    SNew(SVerticalBox)

                        + SVerticalBox::Slot().AutoHeight().Padding(2)
                        [
                            SNew(SHorizontalBox)

                                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                                [
                                    SNew(STextBlock)
                                        .Text(FText::FromString(TEXT("Bake Queue")))
                                ]

                                + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
                                [
                                    SNew(SButton)
                                        .Text(FText::FromString(TEXT("Clear Bake Queue")))
                                        .OnClicked(this, &SMocapRecorderPanel::OnClearBakeQueue)
                                ]
                        ]


                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        SNew(SProgressBar)
                        .Percent_Lambda([this]()
                        {
                            if (!SessionManager)
                                return 0.0f;

                            int32 Done = 0, Total = 0;
                            FString Current;
                            bool bWaiting = false;
                            SessionManager->GetBakeQueueStatus(Done, Total, Current, bWaiting);

                            if (Total <= 0)
                                return 0.0f;

                            return (float)Done / (float)Total;
                        })
                    ]

                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            if (!SessionManager)
                                return FText::FromString(TEXT("Idle"));

                            int32 Done = 0, Total = 0;
                            FString Current;
                            bool bWaiting = false;
                            SessionManager->GetBakeQueueStatus(Done, Total, Current, bWaiting);

                            if (Total <= 0)
                                return FText::FromString(TEXT("Idle"));

                            const int32 Remaining = FMath::Max(0, Total - Done);

                            FString Line = FString::Printf(TEXT("Baking %d/%d  (Remaining: %d)"), Done, Total, Remaining);
                            if (!Current.IsEmpty())
                            {
                                Line += FString::Printf(TEXT("  |  Current: %s"), *Current);
                            }
                            if (bWaiting)
                            {
                                Line += TEXT("  |  Waiting for UE async compile...");
                            }

                            return FText::FromString(Line);
                        })
                    ]
                ]
            ]

                // ----------------------------
                // Target list
                // ----------------------------
                +SVerticalBox::Slot().FillHeight(1.f).Padding(4)
                [
                    BuildTargetList()
                ]
        ];
}

void SMocapRecorderPanel::RefreshTargetList()
{
    TargetIndexItems.Reset();

    if (SessionManager)
    {
        const TArray<FMocapEditorSessionTarget>& Targets = SessionManager->GetTargets();
        TargetIndexItems.Reserve(Targets.Num());

        for (int32 i = 0; i < Targets.Num(); ++i)
        {
            TargetIndexItems.Add(MakeShared<int32>(i));
        }
    }

    if (TargetListView.IsValid())
    {
        TargetListView->RequestListRefresh();
    }
}

void SMocapRecorderPanel::RefreshClassRuleList()
{
    ClassRuleIndexItems.Reset();

    if (SessionManager)
    {
        const TArray<FMocapClassCaptureRule>& Rules = SessionManager->GetClassRules();
        ClassRuleIndexItems.Reserve(Rules.Num());
        for (int32 i = 0; i < Rules.Num(); ++i)
        {
            ClassRuleIndexItems.Add(MakeShared<int32>(i));
        }
    }

    if (ClassRuleListView.IsValid())
    {
        ClassRuleListView->RequestListRefresh();
    }
}

FReply SMocapRecorderPanel::OnAddClassRule()
{
    if (SessionManager)
    {
        SessionManager->AddClassRule();
        RefreshClassRuleList();
    }
    return FReply::Handled();
}

FReply SMocapRecorderPanel::OnClearClassRules()
{
    if (SessionManager)
    {
        SessionManager->ClearClassRules();
        RefreshClassRuleList();
    }
    return FReply::Handled();
}

SMocapRecorderPanel::~SMocapRecorderPanel()
{
    // Do NOT destroy the SessionManager here.
    // PIE teardown invalidates widgets; we must allow the bake queue to continue in editor world.
    if (SessionManager && SessionManager->IsRecording())
    {
        SessionManager->StopSession();
    }

    SessionManager = nullptr;
}

TSharedRef<SWidget> SMocapRecorderPanel::BuildSettingsPanel()
{
    return
        SNew(SHorizontalBox)

        + SHorizontalBox::Slot().AutoWidth().Padding(2)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("Capture Hz")))
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(2)
        [
            SNew(SNumericEntryBox<float>)
                .MinValue(1.f)
                .MaxValue(240.f)
                .Value_Lambda([this]() { return SessionManager->GetCaptureSampleRateHz(); })
                .OnValueChanged_Lambda([this](float V)
                    {
                        SessionManager->SetCaptureSampleRateHz(V);
                    })
        ]

    + SHorizontalBox::Slot().AutoWidth().Padding(10, 2)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("Export FPS")))
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(2)
        [
            SNew(SNumericEntryBox<int32>)
                .MinValue(1)
                .MaxValue(240)
                .Value_Lambda([this]() { return SessionManager->GetExportFrameRateFps(); })
                .OnValueChanged_Lambda([this](int32 V)
                    {
                        SessionManager->SetExportFrameRateFps(V);
                    })
        ];
}

TSharedRef<SWidget> SMocapRecorderPanel::BuildClassRulesPanel()
{
    return
        SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)

                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Class / Instance Capture Rules")))
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [
                    SNew(SButton)
                        .Text(FText::FromString(TEXT("Add Rule")))
                        .OnClicked(this, &SMocapRecorderPanel::OnAddClassRule)
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [
                    SNew(SButton)
                        .Text(FText::FromString(TEXT("Clear Rules")))
                        .OnClicked(this, &SMocapRecorderPanel::OnClearClassRules)
                ]
        ]

    + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(STextBlock)
                .Text(FText::FromString(TEXT("Record all spawned instances of a Blueprint/Class (bullets, casings, limbs, etc.)")))
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SAssignNew(ClassRuleListView, SListView<TSharedPtr<int32>>)
                .ListItemsSource(&ClassRuleIndexItems)
                .SelectionMode(ESelectionMode::None)
                .OnGenerateRow_Lambda([this](TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner)
                    {
                        const int32 Index = Item.IsValid() ? *Item : INDEX_NONE;

                        return SNew(STableRow<TSharedPtr<int32>>, Owner)
                            [
                                SNew(SVerticalBox)

                                    // Row 1: Enabled + Class picker + tag + require skeletal
                                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                                    [
                                        SNew(SHorizontalBox)

                                            + SHorizontalBox::Slot().AutoWidth().Padding(2).VAlign(VAlign_Center)
                                            [
                                                SNew(SCheckBox)
                                                    .IsChecked_Lambda([this, Index]()
                                                        {
                                                            if (SessionManager == nullptr || Index == INDEX_NONE)
                                                                return ECheckBoxState::Unchecked;

                                                            const TArray<FMocapClassCaptureRule>& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index))
                                                                return ECheckBoxState::Unchecked;

                                                            return Rules[Index].bTransformOnly
                                                                ? ECheckBoxState::Checked
                                                                : ECheckBoxState::Unchecked;
                                                        })

                                                    .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetClassRuleEnabled(Index, State == ECheckBoxState::Checked);
                                                            }
                                                        })
                                            ]

                                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(2)
                                            [
                                                SNew(SClassPropertyEntryBox)
                                                    .MetaClass(AActor::StaticClass())
                                                    .AllowNone(true)
                                                    .SelectedClass_Lambda([this, Index]() -> UClass*
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return nullptr;
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return nullptr;
                                                            return Rules[Index].ActorClass.Get();
                                                        })
                                                    .OnSetClass_Lambda([this, Index](const UClass* InClass)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetClassRuleClass(Index, const_cast<UClass*>(InClass));
                                                            }
                                                        })
                                            ]

                                        + SHorizontalBox::Slot().AutoWidth().Padding(6, 2).VAlign(VAlign_Center)
                                            [
                                                SNew(STextBlock).Text(FText::FromString(TEXT("Tag")))
                                            ]

                                            + SHorizontalBox::Slot().AutoWidth().Padding(2)
                                            [
                                                SNew(SEditableTextBox)
                                                    .MinDesiredWidth(140.f)
                                                    .Text_Lambda([this, Index]()
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return FText::GetEmpty();
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return FText::GetEmpty();

                                                            const FName Tag = Rules[Index].RequiredTag;
                                                            return Tag.IsNone() ? FText::GetEmpty() : FText::FromName(Tag);
                                                        })
                                                    .OnTextCommitted_Lambda([this, Index](const FText& NewText, ETextCommit::Type)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                const FString S = NewText.ToString().TrimStartAndEnd();
                                                                SessionManager->SetClassRuleRequiredTag(Index, S.IsEmpty() ? NAME_None : FName(*S));
                                                            }
                                                        })
                                            ]                                       


                                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 2)
                                            [
                                                SNew(SButton)
                                                    .Text(FText::FromString(TEXT("Remove")))
                                                    .OnClicked_Lambda([this, Index]()
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->RemoveClassRule(Index);
                                                                RefreshClassRuleList();
                                                            }
                                                            return FReply::Handled();
                                                        })
                                            ]
                                    ]

                                // Row 2: Auto-stop policies
                                + SVerticalBox::Slot().AutoHeight().Padding(2)
                                    [
                                        SNew(SHorizontalBox)

                                            + SHorizontalBox::Slot().AutoWidth().Padding(2).VAlign(VAlign_Center)
                                            [
                                                SNew(SCheckBox)
                                                    .IsChecked_Lambda([this, Index]()
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return ECheckBoxState::Unchecked;
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return ECheckBoxState::Unchecked;
                                                            return Rules[Index].AutoStop.bStopWhenNearlyStationary ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                                                        })
                                                    .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetRule_StopWhenNearlyStationary(Index, State == ECheckBoxState::Checked);
                                                            }
                                                        })
                                                    [
                                                        SNew(STextBlock).Text(FText::FromString(TEXT("Stop if stationary")))
                                                    ]
                                            ]

                                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 2).VAlign(VAlign_Center)
                                            [
                                                SNew(STextBlock).Text(FText::FromString(TEXT("Speed")))
                                            ]

                                            + SHorizontalBox::Slot().AutoWidth().Padding(2)
                                            [
                                                SNew(SNumericEntryBox<float>)
                                                    .MinValue(0.f).MaxValue(100000.f)
                                                    .Value_Lambda([this, Index]() -> TOptional<float>
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return TOptional<float>();
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return TOptional<float>();
                                                            return TOptional<float>(Rules[Index].AutoStop.LinearSpeedThreshold);
                                                        })

                                                    .OnValueChanged_Lambda([this, Index](float V)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetRule_LinearSpeedThreshold(Index, V);
                                                            }
                                                        })
                                            ]

                                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 2).VAlign(VAlign_Center)
                                            [
                                                SNew(STextBlock).Text(FText::FromString(TEXT("Hold(s)")))
                                            ]

                                            + SHorizontalBox::Slot().AutoWidth().Padding(2)
                                            [
                                                SNew(SNumericEntryBox<float>)
                                                    .MinValue(0.f).MaxValue(10.f)
                                                    .Value_Lambda([this, Index]() -> TOptional<float>
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return TOptional<float>();
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return TOptional<float>();
                                                            return TOptional<float>(Rules[Index].AutoStop.StationaryHoldSeconds);
                                                        })

                                                    .OnValueChanged_Lambda([this, Index](float V)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetRule_StationaryHoldSeconds(Index, V);
                                                            }
                                                        })
                                            ]

                                        + SHorizontalBox::Slot().AutoWidth().Padding(16, 2).VAlign(VAlign_Center)
                                            [
                                                SNew(SCheckBox)
                                                    .IsChecked_Lambda([this, Index]()
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return ECheckBoxState::Unchecked;
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return ECheckBoxState::Unchecked;
                                                            return Rules[Index].AutoStop.bStopWhenOutOfPlayerRadius ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                                                        })
                                                    .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetRule_StopWhenOutOfPlayerRadius(Index, State == ECheckBoxState::Checked);
                                                            }
                                                        })
                                                    [
                                                        SNew(STextBlock).Text(FText::FromString(TEXT("Stop outside player radius")))
                                                    ]
                                            ]

                                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 2).VAlign(VAlign_Center)
                                            [
                                                SNew(STextBlock).Text(FText::FromString(TEXT("Radius")))
                                            ]

                                            + SHorizontalBox::Slot().AutoWidth().Padding(2)
                                            [
                                                SNew(SNumericEntryBox<float>)
                                                    .MinValue(0.f).MaxValue(1000000.f)
                                                    .Value_Lambda([this, Index]() -> TOptional<float>
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return TOptional<float>();
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return TOptional<float>();
                                                            return TOptional<float>(Rules[Index].AutoStop.PlayerRadius);
                                                        })

                                                    .OnValueChanged_Lambda([this, Index](float V)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetRule_PlayerRadius(Index, V);
                                                            }
                                                        })
                                            ]
                                    ]

                                // Row 3: Stop events + autobake
                                + SVerticalBox::Slot().AutoHeight().Padding(2)
                                    [
                                        SNew(SHorizontalBox)

                                            + SHorizontalBox::Slot().AutoWidth().Padding(2).VAlign(VAlign_Center)
                                            [
                                                SNew(SCheckBox)
                                                    .IsChecked_Lambda([this, Index]()
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return ECheckBoxState::Unchecked;
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return ECheckBoxState::Unchecked;
                                                            return Rules[Index].AutoStop.bStopOnHitEvent ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                                                        })
                                                    .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetRule_StopOnHitEvent(Index, State == ECheckBoxState::Checked);
                                                            }
                                                        })
                                                    [
                                                        SNew(STextBlock).Text(FText::FromString(TEXT("Stop on Hit")))
                                                    ]
                                            ]

                                        + SHorizontalBox::Slot().AutoWidth().Padding(12, 2).VAlign(VAlign_Center)
                                            [
                                                SNew(SCheckBox)
                                                    .IsChecked_Lambda([this, Index]()
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return ECheckBoxState::Unchecked;
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return ECheckBoxState::Unchecked;
                                                            return Rules[Index].AutoStop.bStopOnDestroyed ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                                                        })
                                                    .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetRule_StopOnDestroyed(Index, State == ECheckBoxState::Checked);
                                                            }
                                                        })
                                                    [
                                                        SNew(STextBlock).Text(FText::FromString(TEXT("Stop on Destroyed")))
                                                    ]
                                            ]

                                        + SHorizontalBox::Slot().AutoWidth().Padding(20, 2).VAlign(VAlign_Center)
                                            [
                                                SNew(SCheckBox)
                                                    .IsChecked_Lambda([this, Index]()
                                                        {
                                                            if (!SessionManager || Index == INDEX_NONE) return ECheckBoxState::Unchecked;
                                                            const auto& Rules = SessionManager->GetClassRules();
                                                            if (!Rules.IsValidIndex(Index)) return ECheckBoxState::Unchecked;
                                                            return Rules[Index].AutoStop.bAutoBakeOnAutoStop ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                                                        })
                                                    .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
                                                        {
                                                            if (SessionManager && Index != INDEX_NONE)
                                                            {
                                                                SessionManager->SetRule_AutoBakeOnAutoStop(Index, State == ECheckBoxState::Checked);
                                                            }
                                                        })
                                                    [
                                                        SNew(STextBlock).Text(FText::FromString(TEXT("Auto-bake on stop")))
                                                    ]
                                            ]
                                    ]
                            ];
                    })
        ];
}

TSharedRef<SWidget> SMocapRecorderPanel::BuildTargetList()
{
    return
        SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(STextBlock)
                .Text(FText::FromString(TEXT("Capture Targets")))
        ]

        + SVerticalBox::Slot().FillHeight(1.f)
        [
            SAssignNew(TargetListView, SListView<TSharedPtr<int32>>)
                .ListItemsSource(&TargetIndexItems)
                .OnGenerateRow_Lambda([this](
                    TSharedPtr<int32> Item,
                    const TSharedRef<STableViewBase>& Owner)
                    {
                        const int32 Index = Item.IsValid() ? *Item : INDEX_NONE;

                        return SNew(STableRow<TSharedPtr<int32>>, Owner)
                            [
                                SNew(SHorizontalBox)

                                    + SHorizontalBox::Slot().AutoWidth().Padding(2)
                                    [
                                        SNew(SCheckBox)
                                            .IsChecked_Lambda([this, Index]()
                                                {
                                                    if (!SessionManager || Index == INDEX_NONE)
                                                        return ECheckBoxState::Unchecked;

                                                    const TArray<FMocapEditorSessionTarget>& Targets = SessionManager->GetTargets();
                                                    if (!Targets.IsValidIndex(Index))
                                                        return ECheckBoxState::Unchecked;

                                                    return Targets[Index].bEnabled
                                                        ? ECheckBoxState::Checked
                                                        : ECheckBoxState::Unchecked;
                                                })
                                            .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
                                                {
                                                    if (SessionManager && Index != INDEX_NONE)
                                                    {
                                                        SessionManager->SetTargetEnabled(Index, State == ECheckBoxState::Checked);
                                                    }
                                                })
                                    ]

                                + SHorizontalBox::Slot().FillWidth(1.f).Padding(2)
                                    [
                                        SNew(STextBlock)
                                            .Text_Lambda([this, Index]()
                                                {
                                                    if (!SessionManager || Index == INDEX_NONE)
                                                        return FText::FromString(TEXT("<Invalid>"));

                                                    const TArray<FMocapEditorSessionTarget>& Targets = SessionManager->GetTargets();
                                                    if (!Targets.IsValidIndex(Index))
                                                        return FText::FromString(TEXT("<Invalid>"));

                                                    const FMocapEditorSessionTarget& T = Targets[Index];

                                                    if (T.Actor.IsValid())
                                                    {
                                                        return FText::FromString(T.Actor->GetName());
                                                    }

                                                    return FText::FromString(
                                                        T.LastKnownLabel.IsEmpty() ? TEXT("<Unresolved Actor>") : T.LastKnownLabel
                                                    );

                                                })
                                    ]
                            ];
                    })

        ];
}

FReply SMocapRecorderPanel::OnAddSelectedActors()
{
    SessionManager->AddSelectedActorsFromOutliner();
    RefreshTargetList();
    return FReply::Handled();
}

FReply SMocapRecorderPanel::OnClearTargets()
{
    SessionManager->ClearTargets();
    RefreshTargetList();
    return FReply::Handled();
}

FReply SMocapRecorderPanel::OnClearBakeQueue()
{
    if (SessionManager)
    {
        SessionManager->ClearBakeQueue();
    }
    return FReply::Handled();
}

FReply SMocapRecorderPanel::OnStartSession()
{
    UMocapCaptureEditorSessionManager* Mgr = SessionManager.Get();
    if (!IsValid(Mgr))
    {
        UE_LOG(LogTemp, Error, TEXT("MocapPanel: SessionManager invalid; cannot start session."));
        return FReply::Handled();
    }

    const bool bStarted = Mgr->StartSession();
    UE_LOG(LogTemp, Warning, TEXT("MocapPanel: StartSession -> %s"), bStarted ? TEXT("true") : TEXT("false"));

    return FReply::Handled();
}

FReply SMocapRecorderPanel::OnStopSession()
{
    SessionManager->StopSession();
    return FReply::Handled();
}

bool SMocapRecorderPanel::IsRecording() const
{
    UMocapCaptureEditorSessionManager* Manager = SessionManager.Get();
    return IsValid(Manager) && Manager->IsRecording();
}

ECheckBoxState SMocapRecorderPanel::GetRuleTransformOnlyChecked(int32 RuleIndex) const
{
    if (SessionManager == nullptr)
        return ECheckBoxState::Unchecked;

    const TArray<FMocapClassCaptureRule>& Rules = SessionManager->GetClassRules();
    if (!Rules.IsValidIndex(RuleIndex))
        return ECheckBoxState::Unchecked;

    return Rules[RuleIndex].bTransformOnly
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SMocapRecorderPanel::OnRuleTransformOnlyChanged(
    ECheckBoxState NewState,
    int32 RuleIndex)
{
    if (SessionManager == nullptr)
        return;

    if (RuleIndex == INDEX_NONE)
        return;

    const bool bEnableTransformOnly = (NewState == ECheckBoxState::Checked);

    SessionManager->SetClassRuleTransformOnly(RuleIndex, bEnableTransformOnly);
}

// ------------------------------------------------------------
// Tick – drives blinking animation
// ------------------------------------------------------------

void SMocapRecorderPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    // No per-frame work needed for the SessionManager-based UI right now.
}

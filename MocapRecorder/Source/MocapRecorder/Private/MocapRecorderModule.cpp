#include "MocapRecorderModule.h"
#include "Modules/ModuleManager.h"

static const TCHAR* GMocapRecorder_VersionFingerprint = TEXT("2026-02-02 Step1-HygieneLock P");


IMPLEMENT_MODULE(FMocapRecorderModule, MocapRecorder)

// Keep ONLY the define here:
DEFINE_LOG_CATEGORY(LogMocapRecorder);

void FMocapRecorderModule::StartupModule()
{
	UE_LOG(LogMocapRecorder, Warning, TEXT("MocapRecorder: Startup (%s)"), GMocapRecorder_VersionFingerprint);

	UE_LOG(LogMocapRecorder, Warning, TEXT("MocapRecorder: Startup (VERSION_FINGERPRINT 2026-02-01 J)"));

}

void FMocapRecorderModule::ShutdownModule()
{
}

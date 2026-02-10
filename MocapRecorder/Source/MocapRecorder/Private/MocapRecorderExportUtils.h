#pragma once

#include "CoreMinimal.h"

class FArchive;

namespace MocapRecorderExportUtils
{
    // JSON-safe escaping for strings you write to JSON text
    FString EscapeJsonString(const FString& In);

    // Writes a single UTF-8 encoded line (adds '\n')
    void WriteUtf8Line(FArchive& Ar, const FString& Line);
}

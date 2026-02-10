#include "MocapRecorderExportUtils.h"

#include "Serialization/Archive.h"
#include "Containers/StringConv.h"

namespace MocapRecorderExportUtils
{
    FString EscapeJsonString(const FString& In)
    {
        FString Out;
        Out.Reserve(In.Len() + 8);

        for (TCHAR C : In)
        {
            switch (C)
            {
            case TEXT('\"'): Out += TEXT("\\\""); break;
            case TEXT('\\'): Out += TEXT("\\\\"); break;
            case TEXT('\n'): Out += TEXT("\\n");  break;
            case TEXT('\r'): Out += TEXT("\\r");  break;
            case TEXT('\t'): Out += TEXT("\\t");  break;
            default:         Out += C;            break;
            }
        }
        return Out;
    }

    void WriteUtf8Line(FArchive& Ar, const FString& Line)
    {
        FTCHARToUTF8 UTF8(*Line);
        Ar.Serialize((void*)UTF8.Get(), UTF8.Length());
        Ar.Serialize((void*)"\n", 1);
    }
}

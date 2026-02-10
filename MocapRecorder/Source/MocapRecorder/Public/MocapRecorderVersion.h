#pragma once
#include "Runtime/Launch/Resources/Version.h"

#define VMC_UE_AT_LEAST(Maj, Min) \
    ((ENGINE_MAJOR_VERSION > (Maj)) || (ENGINE_MAJOR_VERSION == (Maj) && ENGINE_MINOR_VERSION >= (Min)))

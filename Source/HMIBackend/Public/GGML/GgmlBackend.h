#pragma once

#include "HMIMinimal.h"
#include "GgmlBackend.generated.h"

UENUM(BlueprintType)
enum class EGgmlBackend : uint8
{
	Auto   = 0 UMETA(DisplayName = "Auto"),
	Cpu    = 1 UMETA(DisplayName = "CPU"),
	Vulkan = 2 UMETA(DisplayName = "Vulkan"),
	Cuda   = 3 UMETA(DisplayName = "CUDA"),
};

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** Editor-only tooling: data validation commandlets land here so shipping
 * builds never carry them. */
class FWorldSportsEditorModule : public IModuleInterface
{
};

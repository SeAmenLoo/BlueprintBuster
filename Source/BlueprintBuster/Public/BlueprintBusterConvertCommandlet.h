#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BlueprintBusterConvertCommandlet.generated.h"

UCLASS()
class BLUEPRINTBUSTER_API UBlueprintBusterConvertCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UBlueprintBusterConvertCommandlet();
	virtual int32 Main(const FString& Params) override;
};


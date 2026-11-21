#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "QingBuildLibrary.generated.h"
class USkeletalMesh;
class UAnimBlueprint;
UCLASS()
class QINGBUILDER_API UQingBuildLibrary : public UBlueprintFunctionLibrary {
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Qing") static FString InspectMesh(USkeletalMesh* Mesh);
 UFUNCTION(BlueprintCallable,Category="Qing") static void FinalizeMesh(USkeletalMesh* Mesh);
 UFUNCTION(BlueprintCallable,Category="Qing") static FString InspectRenderData(USkeletalMesh* Mesh,const FString& OutputFile);
 UFUNCTION(BlueprintCallable,Category="Qing") static UAnimBlueprint* BuildAnimation(USkeletalMesh* Mesh,const FString& PlanFile);
 UFUNCTION(BlueprintCallable,Category="Qing") static FString ValidateAnimation(USkeletalMesh* Mesh,UAnimBlueprint* Blueprint,const FString& FrameFile);
};

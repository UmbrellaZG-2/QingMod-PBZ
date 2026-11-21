#include "QingBuildLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/PoseSnapshot.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/KismetSystemLibrary.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_PoseSnapshot.h"
#include "AnimNodes/AnimNode_PoseSnapshot.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Event.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_MakeArray.h"
#include "EdGraphSchema_K2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

namespace {
TSharedPtr<FJsonObject> Read(const FString& File){FString Text;check(FFileHelper::LoadFileToString(Text,*File));TSharedPtr<FJsonObject> J;check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J));return J;}
FString Json(TSharedPtr<FJsonObject> J){FString S;FJsonSerializer::Serialize(J.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return S;}
TArray<TSharedPtr<FJsonValue>> Vec(FVector V){return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};}
void Save(UObject* O){auto* Pkg=O->GetOutermost();Pkg->MarkPackageDirty();FSavePackageArgs A;A.TopLevelFlags=RF_Public|RF_Standalone;check(UPackage::SavePackage(Pkg,O,*FPackageName::LongPackageNameToFilename(Pkg->GetName(),FPackageName::GetAssetPackageExtension()),A));}
template<class T> T* Node(UEdGraph* G){T* N=NewObject<T>(G);G->AddNode(N,false,false);N->CreateNewGuid();return N;}
UEdGraphPin* P(UEdGraphNode* N,const TCHAR* Name){auto* Pin=N->FindPin(Name);if(!Pin){FString S;for(auto* V:N->Pins)S+=V->PinName.ToString()+TEXT(",");UE_LOG(LogTemp,Fatal,TEXT("Missing pin %s: %s (%s)"),Name,*N->GetClass()->GetName(),*S);}return Pin;}
void Link(UEdGraphPin* A,UEdGraphPin* B){check(A&&B);const auto* Schema=GetDefault<UEdGraphSchema_K2>();checkf(Schema->TryCreateConnection(A,B),TEXT("Connection %s -> %s failed"),*A->PinName.ToString(),*B->PinName.ToString());}
UK2Node_CallFunction* Call(UEdGraph* G,UClass* C,const FName Name){auto* N=Node<UK2Node_CallFunction>(G);auto* F=C->FindFunctionByName(Name);check(F);N->SetFromFunction(F);N->AllocateDefaultPins();return N;}
UK2Node_VariableGet* Get(UEdGraph* G,FName Name){auto* N=Node<UK2Node_VariableGet>(G);N->VariableReference.SetSelfMember(Name);N->AllocateDefaultPins();return N;}
UK2Node_VariableSet* Set(UEdGraph* G,FName Name){auto* N=Node<UK2Node_VariableSet>(G);N->VariableReference.SetSelfMember(Name);N->AllocateDefaultPins();return N;}
FEdGraphPinType Type(FName Kind,UObject* Sub=nullptr,bool Array=false){FEdGraphPinType T;T.PinCategory=Kind;T.PinSubCategoryObject=Sub;T.ContainerType=Array?EPinContainerType::Array:EPinContainerType::None;return T;}
}

void UQingBuildLibrary::FinalizeMesh(USkeletalMesh* Mesh){check(Mesh);Mesh->Modify();for(int I=0;I<Mesh->GetLODNum();I++)Mesh->GetLODInfo(I)->BuildSettings.bUseHighPrecisionSkinWeights=true;Mesh->PostEditChange();Save(Mesh);}

FString UQingBuildLibrary::InspectRenderData(USkeletalMesh* Mesh,const FString& OutputFile){
 check(Mesh&&Mesh->GetResourceForRendering());const auto& L=Mesh->GetResourceForRendering()->LODRenderData[0];const auto& W=L.SkinWeightVertexBuffer;const auto& R=Mesh->GetRefSkeleton();
 auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("index16"),W.Use16BitBoneIndex());J->SetBoolField(TEXT("weight16"),W.Use16BitBoneWeight());J->SetNumberField(TEXT("max_influences"),W.GetMaxBoneInfluences());
 TArray<TSharedPtr<FJsonValue>> Vertices,Sections;int Invalid=0;double WeightError=0;
 for(const auto& S:L.RenderSections){auto SJ=MakeShared<FJsonObject>();SJ->SetNumberField(TEXT("material"),S.MaterialIndex);SJ->SetNumberField(TEXT("bones"),S.BoneMap.Num());SJ->SetNumberField(TEXT("vertices"),S.NumVertices);SJ->SetNumberField(TEXT("triangles"),S.NumTriangles);SJ->SetBoolField(TEXT("disabled"),S.bDisabled);Sections.Add(MakeShared<FJsonValueObject>(SJ));
  for(uint32 V=S.BaseVertexIndex;V<S.BaseVertexIndex+S.NumVertices;V++){auto VJ=MakeShared<FJsonObject>();VJ->SetArrayField(TEXT("position"),Vec(FVector(L.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V))));auto UV=L.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(V,0);VJ->SetArrayField(TEXT("uv"),{MakeShared<FJsonValueNumber>(UV.X),MakeShared<FJsonValueNumber>(UV.Y)});VJ->SetNumberField(TEXT("material"),S.MaterialIndex);
   TArray<TSharedPtr<FJsonValue>> Weights;double Total=0;for(uint32 I=0;I<W.GetMaxBoneInfluences();I++){const auto Weight=W.GetBoneWeight(V,I);if(!Weight)continue;const auto Local=W.GetBoneIndex(V,I);if(!S.BoneMap.IsValidIndex(Local)){Invalid++;continue;}const int Bone=S.BoneMap[Local];auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("bone"),R.GetBoneName(Bone).ToString());B->SetNumberField(TEXT("weight"),double(Weight)/65535.0);Weights.Add(MakeShared<FJsonValueObject>(B));Total+=double(Weight)/65535.0;}
   WeightError=FMath::Max(WeightError,FMath::Abs(Total-1));VJ->SetArrayField(TEXT("weights"),Weights);Vertices.Add(MakeShared<FJsonValueObject>(VJ));}}
 J->SetArrayField(TEXT("sections"),Sections);J->SetNumberField(TEXT("invalid_indices"),Invalid);J->SetNumberField(TEXT("max_weight_sum_error"),WeightError);J->SetArrayField(TEXT("vertices"),Vertices);check(FFileHelper::SaveStringToFile(Json(J),*OutputFile));J->RemoveField(TEXT("vertices"));return Json(J);
}

FString UQingBuildLibrary::InspectMesh(USkeletalMesh* Mesh){
 check(Mesh);const auto& Ref=Mesh->GetRefSkeleton();TArray<FTransform> Global;TArray<TSharedPtr<FJsonValue>> Bones;auto J=MakeShared<FJsonObject>();
 for(int I=0;I<Ref.GetNum();I++){int Parent=Ref.GetParentIndex(I);Global.Add(Parent>=0?Ref.GetRefBonePose()[I]*Global[Parent]:Ref.GetRefBonePose()[I]);auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("name"),Ref.GetBoneName(I).ToString());B->SetNumberField(TEXT("parent"),Parent);B->SetArrayField(TEXT("position"),Vec(Global[I].GetTranslation()));auto Q=Global[I].GetRotation();B->SetArrayField(TEXT("rotation"),{MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)});Bones.Add(MakeShared<FJsonValueObject>(B));}
 J->SetArrayField(TEXT("bones"),Bones);J->SetBoolField(TEXT("high_precision_skin_weights"),Mesh->GetLODInfo(0)->BuildSettings.bUseHighPrecisionSkinWeights);J->SetNumberField(TEXT("morphs"),Mesh->GetMorphTargets().Num());auto Bounds=Mesh->GetBounds();J->SetArrayField(TEXT("origin"),Vec(Bounds.Origin));J->SetArrayField(TEXT("extent"),Vec(Bounds.BoxExtent));
 if(auto* Model=Mesh->GetImportedModel()){if(Model->LODModels.Num()){const auto& L=Model->LODModels[0];J->SetNumberField(TEXT("vertices"),L.NumVertices);J->SetNumberField(TEXT("triangles"),L.IndexBuffer.Num()/3);int MaxBones=0;for(const auto& S:L.Sections)MaxBones=FMath::Max(MaxBones,S.BoneMap.Num());J->SetNumberField(TEXT("max_section_bones"),MaxBones);}}
 return Json(J);
}

UAnimBlueprint* UQingBuildLibrary::BuildAnimation(USkeletalMesh* Mesh,const FString& PlanFile){
 check(Mesh);auto Plan=Read(PlanFile);const auto& Inputs=Plan->GetArrayField(TEXT("input_names"));int Count=Plan->GetIntegerField(TEXT("bone_count"));int Morphs=Plan->GetIntegerField(TEXT("morph_count"));
 if(!Mesh->GetSkeleton()){auto* SP=CreatePackage(TEXT("/Game/QingMMD/SK_Qing_Skeleton"));auto* S=NewObject<USkeleton>(SP,TEXT("SK_Qing_Skeleton"),RF_Public|RF_Standalone);check(S->MergeAllBonesToBoneTree(Mesh));Mesh->SetSkeleton(S);FAssetRegistryModule::AssetCreated(S);Save(S);Save(Mesh);}
 const FString PackageName=TEXT("/Game/QingMMD/ABP_Qing_FullMMD_v3");auto* Package=CreatePackage(*PackageName);check(!FindObject<UObject>(Package,TEXT("ABP_Qing_FullMMD_v3")));
 auto* BP=CastChecked<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(UAnimInstance::StaticClass(),Package,TEXT("ABP_Qing_FullMMD_v3"),BPTYPE_Normal,UAnimBlueprint::StaticClass(),UAnimBlueprintGeneratedClass::StaticClass()));BP->TargetSkeleton=Mesh->GetSkeleton();
 auto Add=[&](FName Name,FEdGraphPinType T){check(FBlueprintEditorUtils::AddMemberVariable(BP,Name,T));};
 auto FloatType=Type(UEdGraphSchema_K2::PC_Real);FloatType.PinSubCategory=UEdGraphSchema_K2::PC_Float;
 Add(TEXT("MMDABI"),Type(UEdGraphSchema_K2::PC_Int));Add(TEXT("MMDReady"),FloatType);Add(TEXT("MMDDeltaSeconds"),FloatType);Add(TEXT("MMDSourceValid"),Type(UEdGraphSchema_K2::PC_Boolean));
 Add(TEXT("MMDComponentWorld"),Type(UEdGraphSchema_K2::PC_Struct,TBaseStructure<FTransform>::Get()));Add(TEXT("MMDInputPose"),Type(UEdGraphSchema_K2::PC_Struct,TBaseStructure<FTransform>::Get(),true));Add(TEXT("MMDLocalPose"),Type(UEdGraphSchema_K2::PC_Struct,TBaseStructure<FTransform>::Get(),true));Add(TEXT("MMDBoneNames"),Type(UEdGraphSchema_K2::PC_Name,nullptr,true));
 for(int I=0;I<Morphs;I++)Add(*FString::Printf(TEXT("MMDMorph%03d"),I),FloatType);
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
 UEdGraph* EventGraph=BP->UbergraphPages[0];UK2Node_Event* Event=nullptr;
 for(UEdGraphNode* N:EventGraph->Nodes){auto* E=Cast<UK2Node_Event>(N);if(E&&E->EventReference.GetMemberName()==TEXT("BlueprintUpdateAnimation"))Event=E;}
 if(!Event){Event=Node<UK2Node_Event>(EventGraph);Event->EventReference.SetExternalMember(TEXT("BlueprintUpdateAnimation"),UAnimInstance::StaticClass());Event->bOverrideFunction=true;Event->AllocateDefaultPins();}
 UEdGraphPin* Exec=P(Event,TEXT("then"));
 auto Chain=[&](UEdGraphNode* N){if(auto* In=N->FindPin(TEXT("execute"))){Link(Exec,In);Exec=P(N,TEXT("then"));}};
 auto* Dt=Set(EventGraph,TEXT("MMDDeltaSeconds"));Chain(Dt);Link(P(Event,TEXT("DeltaTimeX")),P(Dt,TEXT("MMDDeltaSeconds")));
 auto* Owning=Call(EventGraph,UAnimInstance::StaticClass(),TEXT("GetOwningComponent"));Chain(Owning);
 auto* Parent=Call(EventGraph,USceneComponent::StaticClass(),TEXT("GetAttachParent"));Chain(Parent);Link(P(Owning,TEXT("ReturnValue")),P(Parent,TEXT("self")));
 auto* Valid=Call(EventGraph,UKismetSystemLibrary::StaticClass(),TEXT("IsValid"));Link(P(Parent,TEXT("ReturnValue")),P(Valid,TEXT("Object")));auto* SV=Set(EventGraph,TEXT("MMDSourceValid"));Chain(SV);Link(P(Valid,TEXT("ReturnValue")),P(SV,TEXT("MMDSourceValid")));
 auto* World=Call(EventGraph,USceneComponent::StaticClass(),TEXT("K2_GetComponentToWorld"));Chain(World);Link(P(Owning,TEXT("ReturnValue")),P(World,TEXT("self")));auto* SW=Set(EventGraph,TEXT("MMDComponentWorld"));Chain(SW);Link(P(World,TEXT("ReturnValue")),P(SW,TEXT("MMDComponentWorld")));
 auto* Make=Node<UK2Node_MakeArray>(EventGraph);Make->AllocateDefaultPins();for(int I=1;I<Inputs.Num();I++)Make->AddInputPin();
 auto* SI=Set(EventGraph,TEXT("MMDInputPose"));Link(P(Make,TEXT("Array")),P(SI,TEXT("MMDInputPose")));
 for(int I=0;I<Inputs.Num();I++){auto* Socket=Call(EventGraph,USceneComponent::StaticClass(),TEXT("GetSocketTransform"));Chain(Socket);Link(P(Parent,TEXT("ReturnValue")),P(Socket,TEXT("self")));P(Socket,TEXT("InSocketName"))->DefaultValue=Inputs[I]->AsString();P(Socket,TEXT("TransformSpace"))->DefaultValue=TEXT("RTS_Component");Link(P(Socket,TEXT("ReturnValue")),P(Make,*FString::Printf(TEXT("[%d]"),I)));}Chain(SI);
 for(int I=0;I<Morphs;I++){auto* Morph=Call(EventGraph,UAnimInstance::StaticClass(),TEXT("SetMorphTarget"));Chain(Morph);P(Morph,TEXT("MorphTargetName"))->DefaultValue=FString::Printf(TEXT("mmd_m%03d"),I);auto Name=FString::Printf(TEXT("MMDMorph%03d"),I);auto* V=Get(EventGraph,*Name);Link(P(V,*Name),P(Morph,TEXT("Value")));}
 UEdGraph* Anim=nullptr;for(UEdGraph* G:BP->FunctionGraphs)if(G->GetFName()==TEXT("AnimGraph"))Anim=G;check(Anim);
 UAnimGraphNode_Root* Root=nullptr;for(UEdGraphNode* N:Anim->Nodes)if(auto* R=Cast<UAnimGraphNode_Root>(N))Root=R;check(Root);
 auto* Snapshot=Node<UAnimGraphNode_PoseSnapshot>(Anim);Snapshot->Node.Mode=ESnapshotSourceMode::SnapshotPin;Snapshot->PostPlacedNewNode();Snapshot->AllocateDefaultPins();for(auto& O:Snapshot->ShowPinForProperties)if(O.PropertyName==TEXT("Snapshot"))O.bShowPin=true;Snapshot->ReconstructNode();
 auto* Struct=Node<UK2Node_MakeStruct>(Anim);Struct->StructType=FPoseSnapshot::StaticStruct();Struct->bMadeAfterOverridePinRemoval=true;Struct->AllocateDefaultPins();
 auto* Local=Get(Anim,TEXT("MMDLocalPose"));auto* Names=Get(Anim,TEXT("MMDBoneNames"));Link(P(Local,TEXT("MMDLocalPose")),P(Struct,TEXT("LocalTransforms")));Link(P(Names,TEXT("MMDBoneNames")),P(Struct,TEXT("BoneNames")));P(Struct,TEXT("SkeletalMeshName"))->DefaultValue=TEXT("QingTransport885");P(Struct,TEXT("bIsValid"))->DefaultValue=TEXT("true");Link(P(Struct,TEXT("PoseSnapshot")),P(Snapshot,TEXT("Snapshot")));Link(P(Snapshot,TEXT("Pose")),P(Root,TEXT("Result")));
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);check(BP->Status!=BS_Error);
 auto* CDO=CastChecked<UAnimInstance>(BP->GeneratedClass->GetDefaultObject());CDO->bUseMultiThreadedAnimationUpdate=false;FindFProperty<FIntProperty>(BP->GeneratedClass,TEXT("MMDABI"))->SetPropertyValue_InContainer(CDO,4401);
 auto FillArray=[&](const FName Name,int Size){auto* Prop=FindFProperty<FArrayProperty>(BP->GeneratedClass,Name);FScriptArrayHelper H(Prop,Prop->ContainerPtrToValuePtr<void>(CDO));H.Resize(Size);return H;};
 auto In=FillArray(TEXT("MMDInputPose"),Inputs.Num());for(int I=0;I<In.Num();I++)*reinterpret_cast<FTransform*>(In.GetRawPtr(I))=FTransform::Identity;
 auto LocalArray=FillArray(TEXT("MMDLocalPose"),Count+1);auto NamesArray=FillArray(TEXT("MMDBoneNames"),Count+1);*reinterpret_cast<FTransform*>(LocalArray.GetRawPtr(0))=FTransform::Identity;*reinterpret_cast<FName*>(NamesArray.GetRawPtr(0))=TEXT("mmd_root");
 const auto& Ref=Mesh->GetRefSkeleton();for(int I=0;I<Count;I++){FName Name=*FString::Printf(TEXT("mmd_b%04d"),I);int Index=Ref.FindBoneIndex(Name);check(Index!=INDEX_NONE);*reinterpret_cast<FTransform*>(LocalArray.GetRawPtr(I+1))=Ref.GetRefBonePose()[Index];*reinterpret_cast<FName*>(NamesArray.GetRawPtr(I+1))=Name;}
 FAssetRegistryModule::AssetCreated(BP);Save(BP);Mesh->SetPostProcessAnimBlueprint(TSubclassOf<UAnimInstance>(BP->GeneratedClass.Get()));Mesh->SetPositiveBoundsExtension(FVector(70,70,50));Mesh->SetNegativeBoundsExtension(FVector(70,70,30));Save(Mesh);return BP;
}

FString UQingBuildLibrary::ValidateAnimation(USkeletalMesh* Mesh,UAnimBlueprint* Blueprint,const FString& FrameFile){
 check(Mesh&&Blueprint);auto Frame=Read(FrameFile);
 auto IVS=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("QingValidationWorld"),nullptr,true,ERHIFeatureLevel::SM5,&IVS);
 auto* Owner=World->SpawnActor<AActor>();auto* Comp=NewObject<USkeletalMeshComponent>(Owner);Owner->AddInstanceComponent(Comp);Owner->SetRootComponent(Comp);
 auto* Source=NewObject<USceneComponent>(Owner);Owner->AddInstanceComponent(Source);Owner->SetRootComponent(Source);Source->RegisterComponentWithWorld(World);Comp->SetupAttachment(Source);Comp->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;Comp->bEnableUpdateRateOptimizations=false;
 Mesh->SetPostProcessAnimBlueprint(TSubclassOf<UAnimInstance>(Blueprint->GeneratedClass.Get()));Comp->SetSkeletalMesh(Mesh);Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);Comp->SetOverridePostProcessAnimBP(TSubclassOf<UAnimInstance>(Blueprint->GeneratedClass.Get()),false);Comp->SetDisablePostProcessBlueprint(false);Comp->RegisterComponentWithWorld(World);Comp->InitAnim(true);
 auto* RefBP=CastChecked<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(UAnimInstance::StaticClass(),GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UAnimBlueprint::StaticClass(),TEXT("QingValidationRef")),BPTYPE_Normal,UAnimBlueprint::StaticClass(),UAnimBlueprintGeneratedClass::StaticClass()));RefBP->TargetSkeleton=Mesh->GetSkeleton();FKismetEditorUtilities::CompileBlueprint(RefBP);Comp->SetAnimInstanceClass(RefBP->GeneratedClass);
 auto* Instance=Comp->GetPostProcessInstance();if(!Instance){auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("passed"),false);J->SetStringField(TEXT("error"),TEXT("Registered test component has no postprocess instance"));World->DestroyWorld(false);return Json(J);}
 auto* Prop=FindFProperty<FArrayProperty>(Instance->GetClass(),TEXT("MMDLocalPose"));check(Prop);FScriptArrayHelper H(Prop,Prop->ContainerPtrToValuePtr<void>(Instance));const auto& Poses=Frame->GetArrayField(TEXT("local"));check(H.Num()==Poses.Num());
 for(int I=0;I<30;I++)FindFProperty<FFloatProperty>(Instance->GetClass(),*FString::Printf(TEXT("MMDMorph%03d"),I))->SetPropertyValue_InContainer(Instance,float(I+1)/40.f);
 for(int I=0;I<H.Num();I++){const auto& V=Poses[I]->AsArray();*reinterpret_cast<FTransform*>(H.GetRawPtr(I))=FTransform(FQuat(V[3]->AsNumber(),V[4]->AsNumber(),V[5]->AsNumber(),V[6]->AsNumber()),FVector(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber()),FVector::OneVector);}
 Comp->TickAnimation(1.f/60,false);Comp->RefreshBoneTransforms();const auto& CS=Comp->GetComponentSpaceTransforms();const auto& Ref=Mesh->GetRefSkeleton();const auto& Expected=Frame->GetArrayField(TEXT("global"));double Max=0,Angle=0;for(int I=0;I<Expected.Num();I++){int Index=Ref.FindBoneIndex(*FString::Printf(TEXT("mmd_b%04d"),I));const auto& V=Expected[I]->AsArray();const auto& T=CS[Index];Max=FMath::Max(Max,FVector::Distance(T.GetLocation(),FVector(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber())));Angle=FMath::Max(Angle,T.GetRotation().AngularDistance(FQuat(V[3]->AsNumber(),V[4]->AsNumber(),V[5]->AsNumber(),V[6]->AsNumber())));}
 auto J=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Bad;
 for(int I=0;I<Expected.Num();I++){int Index=Ref.FindBoneIndex(*FString::Printf(TEXT("mmd_b%04d"),I));const auto& V=Expected[I]->AsArray();const auto& T=CS[Index];double E=FVector::Distance(T.GetLocation(),FVector(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber()));if(E>0.001){auto B=MakeShared<FJsonObject>();B->SetNumberField(TEXT("bone"),I);B->SetNumberField(TEXT("error_cm"),E);B->SetArrayField(TEXT("actual"),Vec(T.GetLocation()));B->SetArrayField(TEXT("expected"),{V[0],V[1],V[2]});Bad.Add(MakeShared<FJsonValueObject>(B));}}
 J->SetArrayField(TEXT("mismatches"),Bad);
 J->SetBoolField(TEXT("should_update_post"),Comp->ShouldUpdatePostProcessInstance());J->SetBoolField(TEXT("should_evaluate_post"),Comp->ShouldEvaluatePostProcessInstance());
 for(TFieldIterator<FStructProperty> It(Instance->GetClass());It;++It){if(It->Struct==FAnimNode_PoseSnapshot::StaticStruct()){auto* N=It->ContainerPtrToValuePtr<FAnimNode_PoseSnapshot>(Instance);J->SetNumberField(TEXT("snapshot_mode"),int(N->Mode));J->SetNumberField(TEXT("snapshot_transforms"),N->Snapshot.LocalTransforms.Num());J->SetNumberField(TEXT("snapshot_names"),N->Snapshot.BoneNames.Num());J->SetBoolField(TEXT("snapshot_valid"),N->Snapshot.bIsValid);}}
 double MorphError=0;for(int I=0;I<30;I++)MorphError=FMath::Max(MorphError,double(FMath::Abs(Comp->GetMorphTarget(*FString::Printf(TEXT("mmd_m%03d"),I))-float(I+1)/40.f)));
 J->SetNumberField(TEXT("morphs_checked"),30);J->SetNumberField(TEXT("max_morph_error"),MorphError);J->SetNumberField(TEXT("post_local_count"),H.Num());J->SetNumberField(TEXT("max_position_error_cm"),Max);J->SetNumberField(TEXT("max_rotation_error_rad"),Angle);J->SetNumberField(TEXT("bones_checked"),Expected.Num());J->SetBoolField(TEXT("passed"),Max<0.001&&Angle<0.001&&MorphError<0.00001);World->DestroyWorld(false);return Json(J);
}

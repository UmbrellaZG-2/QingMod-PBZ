using UnrealBuildTool;
public class QingBuilder : ModuleRules {
 public QingBuilder(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine"});
  PrivateDependencyModuleNames.AddRange(new string[]{"UnrealEd","BlueprintGraph","Kismet","KismetCompiler","AnimGraph","AnimGraphRuntime","Json","JsonUtilities","AssetRegistry","SkeletalMeshDescription","MeshDescription","MeshUtilities","Slate","SlateCore"});
 }
}

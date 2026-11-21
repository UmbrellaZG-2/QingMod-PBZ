#include <Windows.h>
#include <Mod/CppUserModBase.hpp>
#include "ue4ss_reflection.h"
#include "ceris_core.h"
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {
using Clock=std::chrono::steady_clock;
using RC::Unreal::UObject;
// Engine-owned TArray header. We never resize it or allocate engine memory.
struct Array {CerisUETransform* data;int32_t num,max;};
static_assert(sizeof(Array)==16&&sizeof(CerisUETransform)==96);
std::filesystem::path module_dir() {
 HMODULE module{};
 // Legacy UE4SS callback registration has no removal token. Pin code for process
 // lifetime; uninstall deactivates shared state, so retained callbacks are inert.
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&module_dir),&module))throw std::runtime_error("Module path unavailable");
 std::array<wchar_t,32768> path{};if(!GetModuleFileNameW(module,path.data(),DWORD(path.size())))throw std::runtime_error("Module name unavailable");
 return std::filesystem::path(path.data()).parent_path().parent_path();
}
template<class T>T* value(UObject* o,const wchar_t* name){
 auto* p=o->GetPropertyByName(name);if(!p||p->GetElementSize()!=sizeof(T))return nullptr;
 return static_cast<T*>(o->GetValuePtrByPropertyName(name));
}
Array* array(UObject* o,const wchar_t* name,int n){
 auto* a=value<Array>(o,name);if(!a||!a->data||a->num!=n||a->max<a->num||a->max>10000)return nullptr;
 auto* p=static_cast<RC::Unreal::FArrayProperty*>(o->GetPropertyByName(name));
 if(!p->GetInner()||p->GetInner()->GetElementSize()!=96)return nullptr;
 return a;
}
bool valid_transform(const CerisUETransform& t){
 double length=0;for(double q:t.rotation){if(!std::isfinite(q))return false;length+=q*q;}
 if(std::abs(length-1)>0.02)return false;
 for(int i=0;i<3;++i)if(!std::isfinite(t.translation[i])||std::abs(t.scale[i]-1)>0.01)return false;
 return true;
}
struct Runtime {void* handle=nullptr;Clock::time_point last=Clock::now();uint64_t frames=0;float grip_peak_cm=0;int cloth_rescues=0;~Runtime(){if(handle)ceris_game_destroy(handle);}};
struct State {
 std::mutex mutex;bool active=true,enabled=true;std::filesystem::path root=module_dir();
 std::unordered_map<UObject*,std::unique_ptr<Runtime>> instances;
 std::vector<float> morphs;std::vector<int> parents;int bones=0,morph_count=0;Clock::time_point config_check{};
 int inputs=0;double bind_scale=100.0;uint64_t errors=0;
 State(){std::ifstream f(root/L"game_plan.json");nlohmann::json j;f>>j;inputs=int(j.at("input_names").size());bones=j.at("bone_count");morph_count=j.at("morph_count");parents=j.at("parents").get<std::vector<int>>();if(bones!=885||morph_count!=30||parents.size()!=size_t(bones))throw std::runtime_error("Qing model schema mismatch");morphs.resize(morph_count);if(inputs<1||inputs>128)throw std::runtime_error("Invalid game plan");log("bridge_loaded; runtime=Qing-1.0; bones=885; morphs=30; cloth_diagnostics=1; grip_constraints="+std::to_string(j.value("grips",nlohmann::json::array()).size())+"; waiting_for_MMDABI_4401");}
 void log(const std::string& line){std::ofstream f(root/L"runtime.log",std::ios::app);f<<GetTickCount64()<<" "<<line<<"\n";}
 void config(){
  auto now=Clock::now();if(now-config_check<std::chrono::seconds(1))return;config_check=now;
  try{std::ifstream f(root/L"expressions.json");nlohmann::json j;f>>j;auto m=j.at("weights");if(m.size()!=size_t(morph_count))throw std::runtime_error("Qing expression count mismatch");
   std::vector<float> next(morph_count);for(int i=0;i<morph_count;++i){next[i]=m[i].get<float>();if(!std::isfinite(next[i])||std::abs(next[i])>4)throw std::runtime_error("Invalid expression weight");}
   morphs=next;enabled=j.value("enabled",true);
  }catch(const std::exception& e){if(errors++<10)log(std::string("config_error: ")+e.what());}
 }
 void frame(UObject* object){
  auto* abi=value<int32_t>(object,L"MMDABI");if(!abi||*abi!=4401)return;
  std::lock_guard<std::mutex> lock(mutex);if(!active)return;
  auto* ready=value<float>(object,L"MMDReady");if(!ready)return;*ready=0;
  try{
   config();if(!enabled)return;
   auto* source=array(object,L"MMDInputPose",inputs);auto* output=array(object,L"MMDLocalPose",bones+1);
   auto* world=value<CerisUETransform>(object,L"MMDComponentWorld");auto* dt=value<float>(object,L"MMDDeltaSeconds");
   auto* validprop=object->GetPropertyByName(L"MMDSourceValid");auto* validptr=object->GetValuePtrByPropertyName(L"MMDSourceValid");
   if(!source||!output||!world||!dt||!validprop||!validptr)throw std::runtime_error("Blueprint ABI schema mismatch");
   if(!static_cast<RC::Unreal::FBoolProperty*>(validprop)->GetPropertyValue(validptr))return;
   if(!valid_transform(*world)||!std::isfinite(*dt)||*dt<0)throw std::runtime_error("Invalid component transform/time");
   for(int i=0;i<inputs;++i)if(!valid_transform(source->data[i]))throw std::runtime_error("Invalid source transform");
   std::vector<float*> morph_properties(morph_count);
   for(int i=0;i<morph_count;++i){wchar_t key[32];swprintf_s(key,L"MMDMorph%03d",i);morph_properties[i]=value<float>(object,key);if(!morph_properties[i])throw std::runtime_error("Missing expression property");}
   auto now=Clock::now();
   for(auto it=instances.begin();it!=instances.end();)if(now-it->second->last>std::chrono::seconds(10))it=instances.erase(it);else ++it;
   auto& r=instances[object];if(!r||now-r->last>std::chrono::seconds(1)){
    r=std::make_unique<Runtime>();auto pmx=(root/L"model.pmx").u8string(),plan=(root/L"game_plan.json").u8string();
    r->handle=ceris_game_create(pmx.c_str(),plan.c_str());if(!r->handle)throw std::runtime_error(ceris_error());log("instance_created; original_bones=885; rigids=750; joints=1117; morphs=30; pelvis_calibrations="+std::to_string(ceris_game_pelvis_calibrations(r->handle))+" hair_calibrations="+std::to_string(ceris_game_hair_calibrations(r->handle)));
   }
   // Compute into owned staging storage; a failed frame never partially writes UE memory.
   std::vector<CerisUETransform> staging(bones);
   auto start=Clock::now();
   if(!ceris_game_step(r->handle,source->data,inputs,world,morphs.data(),morph_count,*dt,staging.data(),bones))throw std::runtime_error(ceris_error());
   for(const auto& t:staging)if(!valid_transform(t))throw std::runtime_error("Invalid output transform");
   // The FBX reference skeleton carries a uniform scale on its adapter root.
   // Retain that scale and express child translations in the scaled space;
   // otherwise inverse bind matrices shrink each vertex onto its driving bone.
   std::vector<CerisUETransform> local(bones+1);
   local[0].rotation[3]=1;local[0].scale[0]=local[0].scale[1]=local[0].scale[2]=bind_scale;
   for(int i=0;i<bones;++i){const auto& child=staging[i];auto& dst=local[i+1];dst=child;
     if(parents[i]>=0){const auto& parent=staging[parents[i]];
       auto pq=glm::normalize(glm::dquat(parent.rotation[3],parent.rotation[0],parent.rotation[1],parent.rotation[2]));
       auto cq=glm::normalize(glm::dquat(child.rotation[3],child.rotation[0],child.rotation[1],child.rotation[2]));
       auto q=glm::normalize(glm::inverse(pq)*cq);auto p=glm::inverse(pq)*(glm::dvec3(child.translation[0],child.translation[1],child.translation[2])-glm::dvec3(parent.translation[0],parent.translation[1],parent.translation[2]));
       dst.rotation[0]=q.x;dst.rotation[1]=q.y;dst.rotation[2]=q.z;dst.rotation[3]=q.w;dst.translation[0]=p.x;dst.translation[1]=p.y;dst.translation[2]=p.z;
     }
     if(!valid_transform(dst))throw std::runtime_error("Invalid local transform");
     for(int axis=0;axis<3;++axis)dst.translation[axis]/=bind_scale;
   }
   std::memcpy(output->data,local.data(),local.size()*sizeof(CerisUETransform));for(int i=0;i<morph_count;++i)*morph_properties[i]=morphs[i];*ready=1;
   r->last=now;++r->frames;r->grip_peak_cm=std::max(r->grip_peak_cm,ceris_game_grip_error_cm(r->handle));
   float cloth[8]{};const bool cloth_ok=ceris_game_cloth_stats(r->handle,cloth,8)!=0;
   const int cloth_rescues=cloth_ok?int(cloth[6]):0;
   if(cloth_ok&&cloth_rescues>r->cloth_rescues){
     r->cloth_rescues=cloth_rescues;
     log("cloth_snap frames="+std::to_string(r->frames)+" dev_units="+std::to_string(cloth[0])+" cloud_radius="+std::to_string(cloth[4])+" worst_bone="+std::to_string(int(cloth[7]))+" count="+std::to_string(cloth_rescues));
   }
   if(r->frames==1||r->frames%600==0){
     std::string line="pose_applied frames="+std::to_string(r->frames)+" step_ms="+std::to_string(std::chrono::duration<double,std::milli>(Clock::now()-start).count())+" grip_max_error_cm="+std::to_string(r->grip_peak_cm)+" physics_rescues="+std::to_string(ceris_game_physics_rescues(r->handle));
     if(cloth_ok)line+=" cloth_dev="+std::to_string(cloth[0])+" cloth_radius="+std::to_string(cloth[4])+" cloth_rescues="+std::to_string(cloth_rescues);
     log(line);r->grip_peak_cm=0;
   }
  }catch(const std::exception& e){if(errors++<30)log(std::string("frame_error: ")+e.what());}
 }
};
class Mod final:public RC::CppUserModBase {
 std::shared_ptr<State> state;
public:
 Mod(){ModName=STR("Qing Full MMD");ModVersion=STR("1.0");ModAuthors=STR("Qing mod workspace");ModDescription=STR("Qing original PMX with fall protection and calibrated palm constraints");}
 void on_unreal_init() override {
  try{state=std::make_shared<State>();auto s=state;
   RC::Unreal::Hook::RegisterProcessEventPostCallback([s](UObject* o,RC::Unreal::UFunction* f,void*){
    if(o&&f&&reinterpret_cast<UObject*>(f)->GetName()==L"BlueprintUpdateAnimation")s->frame(o);
   });
  }catch(const std::exception& e){std::ofstream f(module_dir()/L"runtime.log",std::ios::app);f<<"startup_error: "<<e.what()<<"\n";}
 }
 ~Mod(){if(state){std::lock_guard<std::mutex> lock(state->mutex);state->active=false;state->instances.clear();state->log("bridge_deactivated");}}
};
}
extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod(){return new Mod();}
extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod){delete mod;}

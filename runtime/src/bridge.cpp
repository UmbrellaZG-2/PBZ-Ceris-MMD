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
 std::array<float,29> morphs{};Clock::time_point config_check{};
 int inputs=0;uint64_t errors=0;
 State(){std::ifstream f(root/L"game_plan.json");nlohmann::json j;f>>j;inputs=int(j.at("input_names").size());if(inputs<1||inputs>128)throw std::runtime_error("Invalid game plan");log("bridge_loaded; runtime=41.0; pelvis_calibration=1; cloth_diagnostics=1; grip_constraints="+std::to_string(j.value("grips",nlohmann::json::array()).size())+"; waiting_for_MMDABI_3701");}
 void log(const std::string& line){std::ofstream f(root/L"runtime.log",std::ios::app);f<<GetTickCount64()<<" "<<line<<"\n";}
 void config(){
  auto now=Clock::now();if(now-config_check<std::chrono::seconds(1))return;config_check=now;
  try{std::ifstream f(root/L"expressions.json");nlohmann::json j;f>>j;auto m=j.at("weights");if(m.size()!=29)throw std::runtime_error("29 expression weights required");
   std::array<float,29> next{};for(int i=0;i<29;++i){next[i]=m[i].get<float>();if(!std::isfinite(next[i])||std::abs(next[i])>4)throw std::runtime_error("Invalid expression weight");}
   morphs=next;enabled=j.value("enabled",true);
  }catch(const std::exception& e){if(errors++<10)log(std::string("config_error: ")+e.what());}
 }
 void frame(UObject* object){
  auto* abi=value<int32_t>(object,L"MMDABI");if(!abi||*abi!=3701)return;
  std::lock_guard<std::mutex> lock(mutex);if(!active)return;
  auto* ready=value<float>(object,L"MMDReady");if(!ready)return;*ready=0;
  try{
   config();if(!enabled)return;
   auto* source=array(object,L"MMDInputPose",inputs);auto* output=array(object,L"MMDPose",943);
   auto* world=value<CerisUETransform>(object,L"MMDComponentWorld");auto* dt=value<float>(object,L"MMDDeltaSeconds");
   auto* validprop=object->GetPropertyByName(L"MMDSourceValid");auto* validptr=object->GetValuePtrByPropertyName(L"MMDSourceValid");
   if(!source||!output||!world||!dt||!validprop||!validptr)throw std::runtime_error("Blueprint ABI schema mismatch");
   if(!static_cast<RC::Unreal::FBoolProperty*>(validprop)->GetPropertyValue(validptr))return;
   if(!valid_transform(*world)||!std::isfinite(*dt)||*dt<0)throw std::runtime_error("Invalid component transform/time");
   for(int i=0;i<inputs;++i)if(!valid_transform(source->data[i]))throw std::runtime_error("Invalid source transform");
   std::array<float*,29> morph_properties{};
   for(int i=0;i<29;++i){wchar_t key[32];swprintf_s(key,L"MMDMorph%03d",i);morph_properties[i]=value<float>(object,key);if(!morph_properties[i])throw std::runtime_error("Missing expression property");}
   auto now=Clock::now();
   for(auto it=instances.begin();it!=instances.end();)if(now-it->second->last>std::chrono::seconds(10))it=instances.erase(it);else ++it;
   auto& r=instances[object];if(!r||now-r->last>std::chrono::seconds(1)){
    r=std::make_unique<Runtime>();auto pmx=(root/L"model.pmx").u8string(),plan=(root/L"game_plan.json").u8string();
    r->handle=ceris_game_create(pmx.c_str(),plan.c_str());if(!r->handle)throw std::runtime_error(ceris_error());log("instance_created; original_bones=943; rigids=785; joints=1306; morphs=29; pelvis_calibrations="+std::to_string(ceris_game_pelvis_calibrations(r->handle)));
   }
   // Compute into owned staging storage; a failed frame never partially writes UE memory.
   std::array<CerisUETransform,943> staging{};
   auto start=Clock::now();
   if(!ceris_game_step(r->handle,source->data,inputs,world,morphs.data(),29,*dt,staging.data(),943))throw std::runtime_error(ceris_error());
   for(const auto& t:staging)if(!valid_transform(t))throw std::runtime_error("Invalid output transform");
   std::memcpy(output->data,staging.data(),sizeof(staging));for(int i=0;i<29;++i)*morph_properties[i]=morphs[i];*ready=1;
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
 Mod(){ModName=STR("Ceris Full MMD");ModVersion=STR("41.0");ModAuthors=STR("Ceris mod workspace");ModDescription=STR("Original PMX with skirt collision guard, fall protection and calibrated palm constraints");}
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

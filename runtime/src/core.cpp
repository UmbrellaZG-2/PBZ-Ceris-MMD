#include "ceris_core.h"
#include <Saba/Model/MMD/PMXModel.h>
#include <Saba/Model/MMD/MMDPhysics.h>
#include <Saba/Model/MMD/PMXFile.h>
#include <btBulletDynamicsCommon.h>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
thread_local std::string last_error;
struct FrameGrip {
 int clavicle,upper,elbow,hand;std::vector<int> upper_helpers,lower_helpers;
 glm::vec3 target,pole;float max_stretch=1.35f,max_protraction=0;
};
struct Runtime {
    saba::PMXModel model;
    saba::PMXFile source;
    std::string inventory;
    std::vector<saba::PMXNode*> append_nodes;
    std::vector<FrameGrip> frame_grips;
    explicit Runtime(const char* path) {
        if (!path || !saba::ReadPMXFile(&source,path) || !model.Load(path,""))
            throw std::runtime_error("PMX load failed");
        model.SetParallelUpdateHint(1);
        model.InitializeAnimation();
        model.GetMMDPhysics()->SetFPS(60.0f);
        model.GetMMDPhysics()->SetMaxSubStepCount(8);
        auto* nodes=model.GetNodeManager();
        for(size_t i=0;i<nodes->GetNodeCount();++i){auto* n=static_cast<saba::PMXNode*>(nodes->GetMMDNode(i));if(n->GetAppendNode())append_nodes.push_back(n);}
        std::stable_sort(append_nodes.begin(),append_nodes.end(),[](auto* a,auto* b){return a->GetDeformdepth()<b->GetDeformdepth();});
        auto* phys=model.GetPhysicsManager();
        if (nodes->GetNodeCount()!=source.m_bones.size() ||
            phys->GetRigidBodys()->size()!=source.m_rigidbodies.size() ||
            phys->GetJoints()->size()!=source.m_joints.size() ||
            model.GetMorphManager()->GetMorphCount()!=source.m_morphs.size())
            throw std::runtime_error("Runtime dropped PMX records");
        nlohmann::json j;
        j["bones"]=nodes->GetNodeCount(); j["vertices"]=model.GetVertexCount();
        j["morphs"]=model.GetMorphManager()->GetMorphCount();
        j["rigids"]=phys->GetRigidBodys()->size(); j["joints"]=phys->GetJoints()->size();
        j["ik_solvers"]=model.GetIKManager()->GetIKSolverCount();
        j["bullet_version"]=btGetVersion(); j["physics_hz"]=60;
        j["coordinates"]="saba_right_handed_x_y_minus_z";
        j["rigid_parameters"]=nlohmann::json::array();
        for (auto& owned:*phys->GetRigidBodys()) {
            auto* rb=owned->GetRigidBody(); auto* proxy=rb->getBroadphaseProxy();
            j["rigid_parameters"].push_back({
                {"inverse_mass",rb->getInvMass()}, {"linear_damping",rb->getLinearDamping()},
                {"angular_damping",rb->getAngularDamping()}, {"friction",rb->getFriction()},
                {"restitution",rb->getRestitution()}, {"group",int(uint16_t(proxy->m_collisionFilterGroup))},
                {"mask",int(uint16_t(proxy->m_collisionFilterMask))}, {"kinematic",rb->isKinematicObject()},
                {"shape_type",rb->getCollisionShape()->getShapeType()}});
        }
        inventory=j.dump(2);
    }
};
Runtime& get(void* h) { if(!h) throw std::runtime_error("Null runtime"); return *static_cast<Runtime*>(h); }
template<class F> int checked(F f) { try { f(); last_error.clear(); return 1; } catch(const std::exception& e) {last_error=e.what(); return 0;} }
bool finite(const glm::mat4& m) { const float* p=glm::value_ptr(m); for(int i=0;i<16;++i) if(!std::isfinite(p[i]))return false; return true; }
glm::quat rotation_between(glm::vec3 a,glm::vec3 b){
 if(glm::length(a)<1e-6f||glm::length(b)<1e-6f)return glm::quat(1,0,0,0);
 a=glm::normalize(a);b=glm::normalize(b);float dot=glm::clamp(glm::dot(a,b),-1.f,1.f);
 if(dot<-.99999f){glm::vec3 axis=glm::cross(a,std::abs(a.x)<.8f?glm::vec3(1,0,0):glm::vec3(0,1,0));return glm::angleAxis(3.14159265359f,glm::normalize(axis));}
 glm::vec3 axis=glm::cross(a,b);return glm::normalize(glm::quat(1+dot,axis.x,axis.y,axis.z));
}
void solve_grip(Runtime& r,const FrameGrip& g,std::vector<CerisGlobalInput>& inputs){
 auto* nodes=r.model.GetNodeManager();auto find=[&](int bone)->CerisGlobalInput&{for(auto& d:inputs)if(d.index==bone)return d;throw std::runtime_error("Grip driver missing");};
 auto& upper=find(g.upper);auto& elbow=find(g.elbow);auto& hand=find(g.hand);
 auto bind=[&](int i){return r.source.m_bones[i].m_position*glm::vec3(1,1,-1);};
 glm::vec3 shoulder(nodes->GetMMDNode(g.upper)->GetGlobalTransform()[3]);
 glm::vec3 upper_rest=bind(g.elbow)-bind(g.upper),lower_rest=bind(g.hand)-bind(g.elbow);
 float a=glm::length(upper_rest),b=glm::length(lower_rest);if(a<1e-5f||b<1e-5f)throw std::runtime_error("Degenerate grip chain");
 // Let the original clavicle swing toward an extended grip before stretching
 // the arm. This keeps the shoulder attached and bounds scapular assistance.
 auto* clavicle=nodes->GetMMDNode(g.clavicle);glm::vec3 pivot(clavicle->GetGlobalTransform()[3]);
 glm::vec3 shoulder_offset=shoulder-pivot,to_target=g.target-pivot;float radius=glm::length(shoulder_offset),target_distance=glm::length(to_target);
 float preferred_reach=(a+b)*1.15f;
 if(glm::length(g.target-shoulder)>preferred_reach&&radius>1e-4f&&target_distance>1e-4f){
  float current_angle=std::acos(glm::clamp(glm::dot(shoulder_offset,to_target)/(radius*target_distance),-1.f,1.f));
  float desired_angle=std::acos(glm::clamp((target_distance*target_distance+radius*radius-preferred_reach*preferred_reach)/(2*target_distance*radius),-1.f,1.f));
  float assist=glm::clamp(current_angle-desired_angle,0.f,glm::radians(30.f));glm::vec3 axis=glm::cross(shoulder_offset,to_target);
  if(assist>1e-6f&&glm::length(axis)>1e-5f){
   glm::quat q=glm::angleAxis(assist,glm::normalize(axis))*glm::quat_cast(clavicle->GetGlobalTransform());
   auto* parent=clavicle->GetParent();if(parent)q=glm::inverse(glm::quat_cast(parent->GetGlobalTransform()))*q;
   clavicle->SetAnimationRotate(glm::normalize(q));clavicle->UpdateLocalTransform();clavicle->UpdateGlobalTransform();
   shoulder=glm::vec3(nodes->GetMMDNode(g.upper)->GetGlobalTransform()[3]);
  }
 }
 glm::vec3 delta=g.target-shoulder;float distance=glm::length(delta);
 glm::vec3 direction=distance>1e-6f?delta/distance:glm::normalize(upper_rest);
 // Different torso proportions can place the source grip beyond the complete
 // clavicle+arm reach. Allow a small, bounded shoulder-girdle protraction, then
 // distribute the remaining reach across both segments instead of a wrist gap.
 float protraction=std::min(std::max(0.f,distance-(a+b)*1.30f),g.max_protraction);
 if(protraction>0){
  glm::vec3 shift=direction*protraction;auto* parent=clavicle->GetParent();glm::mat3 parent_r=parent?glm::mat3(parent->GetGlobalTransform()):glm::mat3(1);
  clavicle->SetAnimationTranslate(clavicle->GetAnimationTranslate()+glm::inverse(parent_r)*shift);
  clavicle->UpdateLocalTransform();clavicle->UpdateGlobalTransform();shoulder=glm::vec3(nodes->GetMMDNode(g.upper)->GetGlobalTransform()[3]);
  delta=g.target-shoulder;distance=glm::length(delta);direction=glm::normalize(delta);
 }
 float stretch=glm::clamp(distance/((a+b)*.995f),1.f,g.max_stretch);a*=stretch;b*=stretch;
 float reach=glm::clamp(distance,std::abs(a-b)+1e-4f,(a+b)*.99999f);
 glm::vec3 wrist=shoulder+direction*reach;
 glm::vec3 pole=g.pole-shoulder;pole-=direction*glm::dot(pole,direction);
 glm::quat upper_q(upper.rotation[3],upper.rotation[0],upper.rotation[1],upper.rotation[2]);
 glm::quat elbow_q(elbow.rotation[3],elbow.rotation[0],elbow.rotation[1],elbow.rotation[2]);
 if(glm::length(pole)<1e-4f){pole=upper_q*upper_rest;pole-=direction*glm::dot(pole,direction);}
 if(glm::length(pole)<1e-4f) pole=glm::cross(direction,std::abs(direction.y)<.8f?glm::vec3(0,1,0):glm::vec3(1,0,0));
 pole=glm::normalize(pole);float along=(a*a-b*b+reach*reach)/(2*reach);float height=std::sqrt(std::max(0.f,a*a-along*along));
 glm::vec3 joint=shoulder+direction*along+pole*height;
 upper_q=glm::normalize(rotation_between(upper_q*upper_rest,joint-shoulder)*upper_q);
 elbow_q=glm::normalize(rotation_between(elbow_q*lower_rest,wrist-joint)*elbow_q);
 std::memcpy(upper.rotation,glm::value_ptr(upper_q),16);std::memcpy(elbow.rotation,glm::value_ptr(elbow_q),16);
 std::memcpy(elbow.position,glm::value_ptr(joint),12);elbow.flags|=1;
 std::memcpy(hand.position,glm::value_ptr(wrist),12);hand.flags|=1;
 // Stretch the original intermediate/twist pivots along each limb, so weighted
 // helper bones stay between their endpoints. No bone scale or PMX edit.
 auto helpers=[&](const std::vector<int>& ids,glm::vec3 axis){axis=glm::normalize(axis);for(int id:ids){auto* n=nodes->GetMMDNode(id);glm::vec3 t=n->GetInitialTranslate();n->SetAnimationTranslate(axis*glm::dot(t,axis)*(stretch-1));n->UpdateLocalTransform();}};
 helpers(g.upper_helpers,upper_rest);helpers(g.lower_helpers,lower_rest);
}
}
void* ceris_create(const char* path) {
    try { auto r=std::make_unique<Runtime>(path); last_error.clear();return r.release(); }
    catch(const std::exception& e) { last_error=e.what(); return nullptr; }
}
void ceris_destroy(void* h) { delete static_cast<Runtime*>(h); }
const char* ceris_error() { return last_error.c_str(); }
const char* ceris_inventory(void* h) { try{return get(h).inventory.c_str();}catch(...){return "{}";} }
int ceris_reset(void* h) { return checked([&]{get(h).model.InitializeAnimation();}); }
int ceris_step(void* h,const CerisBoneInput* bones,int count,const float* morphs,int nm,float dt,int physics,int ik) {
    return checked([&]{
        auto& r=get(h);auto& m=r.model;auto* nodes=m.GetNodeManager();auto* mm=m.GetMorphManager();
        if(count<0 || count>int(nodes->GetNodeCount()) || (count&&!bones) || nm!=int(mm->GetMorphCount()) || !morphs || !std::isfinite(dt)||dt<0||dt>0.25f)
            throw std::runtime_error("Invalid frame inputs");
        // Reset authored animation and bone-morph contributions, never carry them between frames.
        for(size_t i=0;i<nodes->GetNodeCount();++i) {
            auto* n=nodes->GetMMDNode(i); n->LoadInitialTRS();
            n->SetAnimationTranslate(glm::vec3(0));n->SetAnimationRotate(glm::quat(1,0,0,0));
        }
        m.BeginAnimation();
        for(int i=0;i<count;++i) {
            const auto& b=bones[i];if(b.index<0||b.index>=int(nodes->GetNodeCount()))throw std::runtime_error("Bone index out of range");
            for(float v:b.translation) if(!std::isfinite(v))throw std::runtime_error("Nonfinite translation");
            for(float v:b.rotation) if(!std::isfinite(v))throw std::runtime_error("Nonfinite quaternion");
            glm::quat q(b.rotation[3],b.rotation[0],b.rotation[1],b.rotation[2]);
            if(glm::length(q)<0.0001f)throw std::runtime_error("Zero quaternion");
            auto* n=nodes->GetMMDNode(b.index);n->SetAnimationTranslate(glm::make_vec3(b.translation));n->SetAnimationRotate(glm::normalize(q));
        }
        for(int i=0;i<nm;++i) {if(!std::isfinite(morphs[i]))throw std::runtime_error("Nonfinite morph");mm->GetMorph(i)->SetWeight(morphs[i]);}
        for(size_t i=0;i<m.GetIKManager()->GetIKSolverCount();++i)m.GetIKManager()->GetMMDIKSolver(i)->Enable(ik!=0);
        m.UpdateMorphAnimation();m.UpdateNodeAnimation(false);
        if(physics)m.UpdatePhysicsAnimation(dt);
        m.UpdateNodeAnimation(true);m.EndAnimation();
        for(size_t i=0;i<nodes->GetNodeCount();++i)if(!finite(nodes->GetMMDNode(i)->GetGlobalTransform()))throw std::runtime_error("Nonfinite bone output");
    });
}
int ceris_bone_matrices(void* h,float* out,int n) {
    return checked([&]{auto* nodes=get(h).model.GetNodeManager();if(!out||n!=int(nodes->GetNodeCount()*16))throw std::runtime_error("Bone output size mismatch");
        for(size_t i=0;i<nodes->GetNodeCount();++i)std::memcpy(out+i*16,glm::value_ptr(nodes->GetMMDNode(i)->GetGlobalTransform()),64);});
}
int ceris_vertices(void* h,float* out,int n) {
    return checked([&]{auto& m=get(h).model;if(!out||n!=int(m.GetVertexCount()*3))throw std::runtime_error("Vertex output size mismatch");
        m.Update();std::memcpy(out,m.GetUpdatePositions(),sizeof(float)*n);});
}
int ceris_rigid_matrices(void* h,float* out,int n) {
    return checked([&]{auto* rb=get(h).model.GetPhysicsManager()->GetRigidBodys();if(!out||n!=int(rb->size()*16))throw std::runtime_error("Rigid output size mismatch");
        for(size_t i=0;i<rb->size();++i) {const auto matrix=(*rb)[i]->GetTransform();if(!finite(matrix))throw std::runtime_error("Nonfinite rigid output");std::memcpy(out+i*16,glm::value_ptr(matrix),64);}});
}

int ceris_step_global(void* h,const CerisGlobalInput* drivers,int count,const float* morphs,int nm,float dt,int reset) {
 return checked([&]{
  auto& r=get(h);auto& m=r.model;auto* nodes=m.GetNodeManager();auto* mm=m.GetMorphManager();
  if(!drivers||count<1||count>int(nodes->GetNodeCount())||!morphs||nm!=int(mm->GetMorphCount())||!std::isfinite(dt)||dt<0||dt>.25f)throw std::runtime_error("Invalid global frame inputs");
  for(size_t i=0;i<nodes->GetNodeCount();++i){auto* n=nodes->GetMMDNode(i);n->LoadInitialTRS();n->SetAnimationTranslate(glm::vec3(0));n->SetAnimationRotate(glm::quat(1,0,0,0));}
  m.BeginAnimation();
  for(int i=0;i<nm;++i){if(!std::isfinite(morphs[i]))throw std::runtime_error("Nonfinite expression");mm->GetMorph(i)->SetWeight(morphs[i]);}
  for(size_t i=0;i<m.GetIKManager()->GetIKSolverCount();++i)m.GetIKManager()->GetMMDIKSolver(i)->Enable(false);
  m.UpdateMorphAnimation();m.UpdateNodeAnimation(false);
  std::vector<CerisGlobalInput> resolved(drivers,drivers+count);
  for(int i=0;i<count;++i){
   auto& d=resolved[i];if(d.index<0||d.index>=int(nodes->GetNodeCount()))throw std::runtime_error("Global bone index out of range");
   // Resolve original append/cancellation bones before deriving the next local driver.
   for(auto* a:r.append_nodes){a->UpdateAppendTransform();a->UpdateGlobalTransform();}
   for(const auto& grip:r.frame_grips)if(grip.upper==d.index)solve_grip(r,grip,resolved);
   auto* n=nodes->GetMMDNode(d.index);auto* p=n->GetParent();glm::mat4 parent=p?p->GetGlobalTransform():glm::mat4(1);
   if(d.flags&1){
    glm::vec3 desired=glm::make_vec3(d.position);
    if(d.anchor>=0){if(d.anchor>=int(nodes->GetNodeCount()))throw std::runtime_error("Invalid IK anchor");desired=glm::vec3(nodes->GetMMDNode(d.anchor)->GetGlobalTransform()[3]);}
    glm::vec3 t=glm::vec3(glm::inverse(parent)*glm::vec4(desired,1));n->SetAnimationTranslate(t-n->GetInitialTranslate());
   }
   if(d.flags&2){
    glm::quat desired(d.rotation[3],d.rotation[0],d.rotation[1],d.rotation[2]);
    if(glm::length(desired)<.0001f)throw std::runtime_error("Zero global quaternion");
    glm::quat q=glm::normalize(glm::inverse(glm::quat_cast(parent))*glm::normalize(desired));
    const auto& b=r.source.m_bones[d.index];
    if((uint16_t(b.m_boneFlag)&0x400)!=0){
     glm::vec3 axis=b.m_fixedAxis*glm::vec3(1,1,-1);float l=glm::length(axis);
     if(l>1e-6f){axis/=l;glm::vec3 v=axis*glm::dot(glm::vec3(q.x,q.y,q.z),axis);q=glm::normalize(glm::quat(q.w,v.x,v.y,v.z));}
    }
    if(d.twist>=0){
     if(d.twist>=int(nodes->GetNodeCount()))throw std::runtime_error("Invalid twist bone");
     glm::vec3 axis=glm::normalize(r.source.m_bones[d.twist].m_fixedAxis*glm::vec3(1,1,-1));
     glm::vec3 v=axis*glm::dot(glm::vec3(q.x,q.y,q.z),axis);glm::quat twist(q.w,v.x,v.y,v.z);
     twist=glm::length(twist)>1e-6f?glm::normalize(twist):glm::quat(1,0,0,0);
     n->SetAnimationRotate(glm::normalize(q*glm::inverse(twist)));
     nodes->GetMMDNode(d.twist)->SetAnimationRotate(twist);
     nodes->GetMMDNode(d.twist)->UpdateLocalTransform();
    }else n->SetAnimationRotate(q);
   }
   n->UpdateLocalTransform();n->UpdateGlobalTransform();
  }
  for(size_t i=0;i<m.GetIKManager()->GetIKSolverCount();++i)m.GetIKManager()->GetMMDIKSolver(i)->Enable(true);
  m.UpdateNodeAnimation(false);if(reset)m.ResetPhysics();m.UpdatePhysicsAnimation(dt);m.UpdateNodeAnimation(true);m.EndAnimation();
  for(size_t i=0;i<nodes->GetNodeCount();++i)if(!finite(nodes->GetMMDNode(i)->GetGlobalTransform()))throw std::runtime_error("Nonfinite global bone output");
 });
}

namespace {
glm::quat uq(const CerisUETransform& t){return glm::normalize(glm::quat(float(t.rotation[3]),float(t.rotation[0]),float(t.rotation[1]),float(t.rotation[2])));}
glm::vec3 ut(const CerisUETransform& t){return glm::vec3(float(t.translation[0]),float(t.translation[1]),float(t.translation[2]));}
glm::quat jq(const nlohmann::json& a){return glm::normalize(glm::quat(a.at(3).get<float>(),a.at(0).get<float>(),a.at(1).get<float>(),a.at(2).get<float>()));}
glm::vec3 jv(const nlohmann::json& a){return glm::vec3(a.at(0).get<float>(),a.at(1).get<float>(),a.at(2).get<float>());}
struct Driver {int bone,input,anchor=-1,twist=-1;glm::quat correction;bool position=false;glm::vec3 bind,source_bind;};
struct Grip {FrameGrip frame;int hand_input,elbow_input;glm::vec3 source_contact,target_contact;glm::quat hand_correction;};
struct Game {
 std::unique_ptr<Runtime> runtime;
 std::vector<Driver> drivers;
 std::vector<glm::quat> bind_rotations;
 std::vector<Grip> grips;
 glm::mat3 convert{1};float units=8.8f;glm::vec3 bind_offset{0};
 glm::vec3 origin{0},last_location{0};bool initialized=false;int input_count=0,center_index=2,pelvis_input=0;
 glm::vec3 center_bind{0},pelvis_bind{0};float grip_error_cm=0;
 Game(const char* pmx,const char* plan):runtime(std::make_unique<Runtime>(pmx)){
  std::ifstream stream(std::filesystem::u8path(plan));nlohmann::json j;stream>>j;
  input_count=int(j.at("input_names").size());units=j.at("units").get<float>();
  auto a=j.at("saba_to_ue");for(int row=0;row<3;++row)for(int col=0;col<3;++col)convert[col][row]=a[row][col].get<float>();
  bind_offset=jv(j.at("bind_offset"));center_bind=jv(j.at("center_bind_saba"));pelvis_bind=jv(j.at("pelvis_bind_ue"));pelvis_input=j.at("pelvis_input").get<int>();
  for(auto& d:j.at("drivers")){
   Driver v;v.bone=d.at("bone").get<int>();v.input=d.at("input").get<int>();v.position=d.at("position").get<bool>();
   v.correction=jq(d.at("correction_ue"));v.bind=jv(d.at("bind_saba"));v.source_bind=jv(d.at("source_bind_ue"));v.anchor=d.value("anchor",-1);v.twist=d.value("twist",-1);drivers.push_back(v);
  }
  for(auto& q:j.at("target_bind_rotations"))bind_rotations.push_back(jq(q));
  for(const auto& v:j.value("grips",nlohmann::json::array())){
   Grip g;g.frame.clavicle=v.at("clavicle");g.frame.upper=v.at("upper");g.frame.elbow=v.at("elbow");g.frame.hand=v.at("hand");g.hand_input=v.at("hand_input");g.elbow_input=v.at("elbow_input");
   g.frame.upper_helpers=v.at("upper_helpers").get<std::vector<int>>();g.frame.lower_helpers=v.at("lower_helpers").get<std::vector<int>>();g.frame.max_stretch=v.at("max_stretch");
   g.frame.max_protraction=v.at("max_protraction_cm").get<float>()/units;
   g.source_contact=jv(v.at("source_contact_local"));g.target_contact=jv(v.at("target_contact_bind_ue"));g.hand_correction=jq(v.at("hand_correction_ue"));
   if(g.hand_input<0||g.hand_input>=input_count||g.elbow_input<0||g.elbow_input>=input_count||g.frame.max_stretch<1||g.frame.max_stretch>1.5f||g.frame.max_protraction<0||g.frame.max_protraction>1)throw std::runtime_error("Invalid grip plan");
   for(int b:{g.frame.clavicle,g.frame.upper,g.frame.elbow,g.frame.hand})if(b<0||b>=int(bind_rotations.size()))throw std::runtime_error("Invalid grip bone");
   for(const auto* helpers:{&g.frame.upper_helpers,&g.frame.lower_helpers})for(int b:*helpers)if(b<0||b>=int(bind_rotations.size()))throw std::runtime_error("Invalid grip helper");
   grips.push_back(g);
  }
  if(bind_rotations.size()!=runtime->source.m_bones.size())throw std::runtime_error("Plan bone count mismatch");
 }
};
void set_global(std::vector<CerisGlobalInput>& out,int index,glm::vec3 t,glm::quat q,unsigned flags){
 CerisGlobalInput v{};v.index=index;v.flags=flags;std::memcpy(v.position,glm::value_ptr(t),12);std::memcpy(v.rotation,glm::value_ptr(q),16);out.push_back(v);
}
}
void* ceris_game_create(const char* pmx,const char* plan){try{auto r=std::make_unique<Game>(pmx,plan);last_error.clear();return r.release();}catch(const std::exception& e){last_error=e.what();return nullptr;}}
void ceris_game_destroy(void* h){delete static_cast<Game*>(h);}
int ceris_game_rigid_matrices(void* h,float* out,int n){if(!h)return 0;return ceris_rigid_matrices(static_cast<Game*>(h)->runtime.get(),out,n);}
int ceris_game_saba_matrices(void* h,float* out,int n){if(!h)return 0;return ceris_bone_matrices(static_cast<Game*>(h)->runtime.get(),out,n);}
float ceris_game_grip_error_cm(void* h){return h?static_cast<Game*>(h)->grip_error_cm:0;}
int ceris_game_step(void* h,const CerisUETransform* source,int ns,const CerisUETransform* world,const float* morphs,int nm,float dt,CerisUETransform* out,int no){
 return checked([&]{
  if(!h||!source||!world||!out)throw std::runtime_error("Null game frame");auto& g=*static_cast<Game*>(h);
  if(ns!=g.input_count||no!=int(g.bind_rotations.size())||!std::isfinite(dt)||dt<0)throw std::runtime_error("Game frame schema mismatch");
  const glm::vec3 world_t=ut(*world);const glm::mat3 world_r=glm::mat3_cast(uq(*world));const glm::mat3 ci=glm::inverse(g.convert);
  bool reset=!g.initialized||glm::length(world_t-g.last_location)>500.f||dt>.25f;
  if(reset){g.origin=world_t;g.initialized=true;}g.last_location=world_t;
  const glm::vec3 root_t=ci*(world_t-g.origin)/g.units;
  const glm::mat3 root_r=ci*world_r*g.convert;
  std::vector<CerisGlobalInput> inputs;inputs.reserve(g.drivers.size()+2);
  const glm::vec3 root_bind=g.runtime->source.m_bones[1].m_position*glm::vec3(1,1,-1);
  set_global(inputs,1,root_t+root_r*root_bind,glm::quat_cast(root_r),3);
  const glm::vec3 pelvis_delta=ci*(ut(source[g.pelvis_input])-g.pelvis_bind)/g.units;
  set_global(inputs,g.center_index,root_t+root_r*(g.center_bind+pelvis_delta),glm::quat(1,0,0,0),1);
  for(const auto& d:g.drivers){
   const glm::mat3 desired_ue=glm::mat3_cast(uq(source[d.input])*d.correction);
   const glm::quat q=glm::quat_cast(root_r*ci*desired_ue*g.convert);
   glm::vec3 p(0);if(d.position)p=root_t+root_r*(d.bind+ci*(ut(source[d.input])-d.source_bind)/g.units);
   set_global(inputs,d.bone,p,q,d.position?3:2);
   inputs.back().anchor=d.anchor;inputs.back().twist=d.twist;
   if(d.anchor>=0)inputs.back().flags=1;
  }
  g.runtime->frame_grips.clear();
  for(const auto& grip:g.grips){
   FrameGrip frame=grip.frame;const auto& hand=source[grip.hand_input];glm::quat q=uq(hand)*grip.hand_correction;
   glm::vec3 contact=ut(hand)+uq(hand)*grip.source_contact;
   glm::vec3 wrist=contact-q*grip.target_contact;
   frame.target=root_t+root_r*(ci*(wrist-g.bind_offset)/g.units);
   frame.pole=root_t+root_r*(ci*(ut(source[grip.elbow_input])-g.bind_offset)/g.units);
   g.runtime->frame_grips.push_back(frame);
  }
  if(!ceris_step_global(g.runtime.get(),inputs.data(),int(inputs.size()),morphs,nm,std::min(dt,.05f),reset?1:0))throw std::runtime_error(last_error);
  auto* nodes=g.runtime->model.GetNodeManager();
  for(int i=0;i<no;++i){
   const auto& m=nodes->GetMMDNode(i)->GetGlobalTransform();
   const glm::mat3 local_r=glm::transpose(world_r)*g.convert*glm::mat3(m)*ci;
   const glm::quat q=glm::normalize(glm::quat_cast(local_r)*g.bind_rotations[i]);
   const glm::vec3 p=glm::transpose(world_r)*(g.convert*glm::vec3(m[3])*g.units+g.origin-world_t)+g.bind_offset;
   out[i]={};out[i].rotation[0]=q.x;out[i].rotation[1]=q.y;out[i].rotation[2]=q.z;out[i].rotation[3]=q.w;
   double qlength=0;for(double v:out[i].rotation)qlength+=v*v;qlength=std::sqrt(qlength);for(double& v:out[i].rotation)v/=qlength;
   out[i].translation[0]=p.x;out[i].translation[1]=p.y;out[i].translation[2]=p.z;out[i].scale[0]=out[i].scale[1]=out[i].scale[2]=1;
   if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||glm::length(p)>10000.f)throw std::runtime_error("Game pose out of bounds");
  }
  g.grip_error_cm=0;
  for(const auto& grip:g.grips){
   const auto& hand=out[grip.frame.hand];const auto& original=source[grip.hand_input];
   glm::vec3 actual=ut(hand)+(uq(hand)*glm::inverse(g.bind_rotations[grip.frame.hand]))*grip.target_contact;
   glm::vec3 expected=ut(original)+uq(original)*grip.source_contact;
   g.grip_error_cm=std::max(g.grip_error_cm,glm::length(actual-expected));
  }
 });
}

"""Compare old/new palm contacts using coherent UE-evaluated animation frames."""
from pathlib import Path
import ctypes as C,json,math,time,hashlib,numpy as np
O=Path(__file__).resolve().parent;R=O.parents[2];V=O.parent/'v37_full_mmd_2026-11-10'
p=json.loads((O/'game_plan.json').read_text(encoding='utf-8'));u=json.loads((V/'ue_import.json').read_text(encoding='utf-8'));audit=json.loads((V/'source_audit.json').read_text(encoding='utf-8'))
class T(C.Structure):_fields_=[('q',C.c_double*4),('t',C.c_double*4),('s',C.c_double*4)]
def rotate(q,v):q=np.array(q);v=np.array(v);q=q/np.linalg.norm(q);return v+2*np.cross(q[:3],np.cross(q[:3],v)+q[3]*v)
def mul(a,b):a=np.array(a);b=np.array(b);return np.r_[a[3]*b[:3]+b[3]*a[:3]+np.cross(a[:3],b[:3]),a[3]*b[3]-a[:3]@b[:3]]
def inverse(q):return np.array(q)*[-1,-1,-1,1]
def put(t,v):t.t[:3]=v[:3];t.q[:]=v[3:7];t.s[:3]=v[7:10]
names=list(u['game_reference_global']);indices=[names.index(n) for n in p['input_names']]
animation=json.loads((O.parent/'v36_2026-11-10/runtime_evaluation.json').read_text(encoding='utf-8'));all_frames=np.array(animation['source_frames'])
assert all_frames.shape==(161,264,10)
ref=np.array(list(u['game_reference_global'].values()));assert np.max(np.abs(all_frames[0,indices,:3]-ref[indices,:3]))<.01,'Bone order/reference mismatch (compressed sequence tolerance 0.1mm)'
frames=all_frames[:,indices].copy();source=(T*51)();out=(T*943)();world=T();world.q[3]=1;world.s[:3]=[1,1,1];morphs=(C.c_float*29)()
def run(path,plan,label):
 lib=C.CDLL(str(path));lib.ceris_error.restype=C.c_char_p;lib.ceris_game_create.argtypes=[C.c_char_p,C.c_char_p];lib.ceris_game_create.restype=C.c_void_p;lib.ceris_game_destroy.argtypes=[C.c_void_p]
 lib.ceris_game_step.argtypes=[C.c_void_p,C.POINTER(T),C.c_int,C.POINTER(T),C.POINTER(C.c_float),C.c_int,C.c_float,C.POINTER(T),C.c_int]
 h=lib.ceris_game_create(audit['source'].encode(),str(plan).encode());assert h,lib.ceris_error()
 errors=[];angles=[];times=[];samples=[];rows=[];max_stretch=1
 try:
  for f,frame in enumerate(frames):
   for t,v in zip(source,frame):put(t,v)
   angle=.8*math.sin(f*.05);world.q[:]=[0,0,math.sin(angle/2),math.cos(angle/2)];world.t[:3]=[80*math.sin(f*.06)+(100000 if f>=110 else 0),50*math.cos(f*.04),0]
   morphs[13]=.4*(1+math.sin(f*.13))
   start=time.perf_counter();ok=lib.ceris_game_step(h,source,51,C.byref(world),morphs,29,.05,out,943);times.append((time.perf_counter()-start)*1000);assert ok,lib.ceris_error()
   a=np.frombuffer(out,dtype='f8').reshape(943,12).copy();assert np.isfinite(a).all();samples.append(a)
   for g in p['grips']:
    v=frame[g['hand_input']];desired=v[:3]+rotate(v[3:7],g['source_contact_local']);actual=a[g['hand'],4:7]+rotate(mul(a[g['hand'],:4],inverse(p['target_bind_rotations'][g['hand']])),g['target_contact_bind_ue']);err=float(np.linalg.norm(actual-desired));errors.append(err)
    actual_q=mul(a[g['hand'],:4],inverse(p['target_bind_rotations'][g['hand']]));expected_q=mul(v[3:7],g['hand_correction_ue']);angle_error=2*math.acos(min(1.,abs(float(actual_q@expected_q)/(np.linalg.norm(actual_q)*np.linalg.norm(expected_q)))));angles.append(angle_error)
    upper,elbow,hand=g['upper'],g['elbow'],g['hand'];bind=[np.array(u['reference_global'][f'mmd_b{i:04d}'][:3]) for i in [upper,elbow,hand]]
    ratios=[np.linalg.norm(a[y,4:7]-a[x,4:7])/np.linalg.norm(bind[k+1]-bind[k]) for k,(x,y) in enumerate([(upper,elbow),(elbow,hand)])];max_stretch=max(max_stretch,*ratios)
    rows.append(dict(frame=f*3,side=g['side'],palm_error_cm=err,stretch=max(ratios)))
  np.savez_compressed(O/(label+'_motion.npz'),transforms=np.array(samples),source=frames)
  if label=='after':(O/'integration_frame.bin').write_bytes(bytes(source)+bytes(world)+bytes(out)+bytes(morphs))
 finally:lib.ceris_game_destroy(h)
 return dict(max_palm_error_cm=max(errors),p95_palm_error_cm=float(np.percentile(errors,95)),max_palm_angle_error_rad=max(angles),max_segment_stretch=max_stretch,mean_ms=float(np.mean(times)),worst=max(rows,key=lambda r:r['palm_error_cm']),frames=rows)
before=run(O/'CerisMMDCore.dll',V/'game_plan.json','before');after=run(R/'modwork/mmd_runtime/build/CerisMMDCore.dll',O/'game_plan.json','after')
files=[R/'modwork/mmd_runtime/build/CerisMMDCore.dll',R/'modwork/mmd_runtime/build/main.dll',O/'game_plan.json',R/'modwork/mmd_runtime/src/core.cpp',R/'modwork/mmd_runtime/src/bridge.cpp']
r=dict(ok=bool(after['max_palm_error_cm']<.02 and after['max_segment_stretch']<1.351 and after['max_palm_angle_error_rad']<.001),before=before,after=after,in_game_verified=False,coverage='161 coherent UE pose samples: gait, torso turn, shoulder swing, elbow bend, wrist rotation, world rotation and teleport',validated_files=[dict(path=str(f),sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in files])
(O/'grip_validation.json').write_text(json.dumps(r,indent=2));print(json.dumps({k:({a:b for a,b in v.items() if a!='frames'} if isinstance(v,dict) else v) for k,v in r.items()},indent=2));assert r['ok']

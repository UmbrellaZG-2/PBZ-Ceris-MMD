"""Calibrate a full palm frame and contact point while keeping v37's 51-input ABI."""
from pathlib import Path
import json,numpy as np
O=Path(__file__).resolve().parent;V=O.parent/'v37_full_mmd_2026-11-10'
p=json.loads((V/'game_plan.json').read_text(encoding='utf-8'));u=json.loads((V/'ue_import.json').read_text(encoding='utf-8'));d=json.loads((V/'source_full.json').read_text(encoding='utf-8'))
def norm(v):return v/np.linalg.norm(v)
def mat(q):
 x,y,z,w=norm(np.array(q));return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
def quat(m):
 # Stable symmetric eigenvalue construction (xyzw).
 K=np.array([[m[0,0]-m[1,1]-m[2,2],m[1,0]+m[0,1],m[2,0]+m[0,2],m[2,1]-m[1,2]],[m[1,0]+m[0,1],m[1,1]-m[0,0]-m[2,2],m[2,1]+m[1,2],m[0,2]-m[2,0]],[m[2,0]+m[0,2],m[2,1]+m[1,2],m[2,2]-m[0,0]-m[1,1],m[1,0]-m[0,1]],[m[2,1]-m[1,2],m[0,2]-m[2,0],m[1,0]-m[0,1],np.trace(m)]])/3
 _,v=np.linalg.eigh(K);q=v[:,-1];return q if q[3]>=0 else -q
def basis(forward,across):
 x=norm(forward);z=norm(np.cross(x,across));y=np.cross(z,x);return np.column_stack([x,y,z])
src={k:np.array(v) for k,v in u['game_reference_global'].items()};tp=np.array([u['reference_global'][f'mmd_b{i:04d}'][:3] for i in range(943)])
bybone={v['bone']:v for v in p['drivers']};p['grips']=[]
for side,upper,elbow,hand,finger in [('r',19,24,31,100),('l',37,42,49,85)]:
 name='hand_'+side;s=src[name];idx=finger+3;mid=finger+6;little=finger+12
 target_basis=basis(tp[mid]-tp[hand],tp[idx]-tp[little]);source_basis=basis(src['middle_01_'+side][:3]-s[:3],src['index_01_'+side][:3]-src['pinky_01_'+side][:3])
 align=source_basis@target_basis.T;correction=mat(s[3:7]).T@align
 bybone[hand]['correction_ue']=quat(correction).tolist()
 # Match centers of corresponding palm spans, not merely wrist bone origins.
 target_contact=.55*((tp[idx]+tp[mid]+tp[little])/3-tp[hand])
 source_contact=.55*((src['index_01_'+side][:3]+src['middle_01_'+side][:3]+src['pinky_01_'+side][:3])/3-s[:3])
 grip=dict(side=side,upper=upper,elbow=elbow,hand=hand,hand_input=p['input_names'].index(name),elbow_input=p['input_names'].index('lowerarm_'+side),source_contact_local=(mat(s[3:7]).T@source_contact).tolist(),target_contact_bind_ue=target_contact.tolist(),hand_correction_ue=quat(correction).tolist(),max_stretch=1.35,upper_helpers=list(range(upper+1,upper+5))+[elbow+5],lower_helpers=list(range(elbow+1,elbow+5)))
 grip['clavicle']=upper-2;grip['max_protraction_cm']=4.0;p['grips'].append(grip)
 # Transfer finger flexion in a palm-consistent frame, retaining the original
 # joints and bind offsets rather than twisting each finger independently.
 for f,part in enumerate(['thumb','index','middle','ring','pinky']):
  for joint in range(3):
   b=finger+f*3+joint;n=f'{part}_{joint+1:02d}_{side}'
   next_b=b+1 if joint<2 else b;prev_b=b if joint<2 else b-1
   next_name=f'{part}_{joint+2:02d}_{side}' if joint<2 else n;prev_name=n if joint<2 else f'{part}_{joint:02d}_{side}'
   tb=basis(tp[next_b]-tp[prev_b],target_basis[:,1]);sb=basis(src[next_name][:3]-src[prev_name][:3],source_basis[:,1])
   bybone[b]['correction_ue']=quat(mat(src[n][3:7]).T@sb@tb.T).tolist()
p['runtime_version']='38.0';p['grip_design']='two-bone IK to calibrated original-game palm contacts; original twist helpers follow segment reach'
assert p['input_names']==json.loads((V/'game_plan.json').read_text(encoding='utf-8'))['input_names']
(O/'game_plan.json').write_text(json.dumps(p,ensure_ascii=False,indent=2),encoding='utf-8')
print('Grip plan: two palms; unchanged 51-input ABI; max arm reach 1.35x')

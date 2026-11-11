import bpy,numpy as np,json
from mathutils import Matrix,Quaternion,Vector
from pathlib import Path
O=Path(__file__).resolve().parent;V=O.parent/'v37_full_mmd_2026-11-10'
bpy.ops.wm.open_mainfile(filepath=str(V/'CerisFullMMD.blend'))
plan=json.loads((O/'game_plan.json').read_text(encoding='utf-8'))
arm=bpy.data.objects['Armature'];obj=bpy.data.objects['CerisFullMMD'];s=bpy.context.scene
s.render.engine='BLENDER_WORKBENCH'
if not s.world:s.world=bpy.data.worlds.new('World')
s.display.shading.light='STUDIO';s.display.shading.color_type='MATERIAL';s.display.shading.show_shadows=True;s.display.shading.show_cavity=True;s.display.shading.background_type='WORLD';s.world.color=(.10,.10,.12)
s.render.resolution_x=900;s.render.resolution_y=900;s.render.resolution_percentage=100
for i,m in enumerate(obj.data.materials):m.diffuse_color=(.8,.65,.6,1) if i==10 else (.55,.02,.035,1) if i in (15,16,25) else (.16,.17,.2,1) if i in (8,9) else (.85,.85,.85,1)
cam=bpy.data.objects.new('GripCamera',bpy.data.cameras.new('GripCamera'));s.collection.objects.link(cam);s.camera=cam;cam.data.type='ORTHO'
reflect=Matrix.Diagonal((1,-1,1,1))
def camera(center,scale,offset=(50,-320,30)):
 center=Vector(center);cam.data.ortho_scale=scale;cam.location=center+Vector(offset);cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
def depth(b):return 0 if b.parent is None else 1+depth(b.parent)
ordered=sorted([b for b in arm.pose.bones if b.name.startswith('mmd_b')],key=depth)
def matrix(v):
 m=Quaternion((v[3],v[0],v[1],v[2])).to_matrix().to_4x4();m.translation=Vector(v[4:7]);return reflect@m@reflect
def pose(data,frame):
 for b in ordered:
  g=matrix(data[frame,int(b.name[5:])])
  if b.parent and b.parent.name.startswith('mmd_b'):
   pg=matrix(data[frame,int(b.parent.name[5:])]);rest=b.parent.bone.matrix_local.inverted()@b.bone.matrix_local;b.matrix_basis=rest.inverted()@pg.inverted()@g
  else:b.matrix_basis=b.bone.matrix_local.inverted()@g
 bpy.context.view_layer.update()
markers=[]
for g in plan['grips']:
 bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=8,radius=.7)
 marker=bpy.context.object;marker.name='SourcePalm_'+g['side'];m=bpy.data.materials.new(marker.name);m.diffuse_color=(.05,1,.15,1);marker.data.materials.append(m);markers.append(marker)
def contacts(source,frame):
 for g,marker in zip(plan['grips'],markers):
  v=source[frame,g['hand_input']];q=Quaternion((v[6],v[3],v[4],v[5]));c=Vector(v[:3])+q@Vector(g['source_contact_local']);marker.location=(c.x,-c.y,c.z)
def render(name):s.render.filepath=str(O/(name+'.png'));bpy.ops.render.render(write_still=True)
for label in ['before','after']:
 a=np.load(O/(label+'_motion.npz'));data=a['transforms'];source=a['source']
 for f in [0,120,143]:
  pose(data,f);contacts(source,f);camera((0,0,95),210);render(f'{label}_{f*3:03d}')
  if label=='after' and f==143:
   for g,marker in zip(plan['grips'],markers):camera(marker.location,38,offset=(50,-180,80));render('after_hand_'+g['side'])
print('GRIP_RENDER_DONE')

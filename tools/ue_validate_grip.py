import unreal as u,json
from pathlib import Path
O=Path(__file__).resolve().parent
bp=u.load_object(None,'/Game/CerisMMD_Full/ABP_Ceris_FullMMD')
source=u.load_object(None,'/HuaShira/Characters/Hero/Hero_Hun/SkinParts/Hun_Body_Simple')
mesh=u.load_object(None,'/HuaShira/Characters/Hero/Hero_Hun/SkinParts/Hun_Hair_Band')
r={'post_process':u.CerisMMDTools.validate_transfer(bp,source,mesh,str(O),True)}
(O/'ue_grip_result.json').write_text(json.dumps(r,indent=2))
assert r['post_process']=='OK',r

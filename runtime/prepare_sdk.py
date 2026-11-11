"""Make a narrow ABI adapter from this machine's exact UE4SS build.

The matching UEPseudo repository is not public. No guessed UObject layouts
are used. Only publicly exported reflection functions are called, and all
game data exchanged lives in our own versioned Blueprint properties.
CppUserModBase is the matching public header with its unnecessary GUI
include removed (all data members, virtual slots and signatures retained).
"""
from pathlib import Path
import hashlib,json,re,subprocess
R=Path(__file__).resolve().parent
S=R/'deps/RE-UE4SS-1ceefac8b8c286f0fb3eb62b4aae075ff81df0bd'
O=R/'generated/sdk'
(O/'Mod').mkdir(parents=True,exist_ok=True)
source=S/'UE4SS/include/Mod/CppUserModBase.hpp'
text=source.read_text(encoding='utf-8')
text=text.replace('#include <GUI/GUITab.hpp>','namespace RC::GUI { class GUITab; }')
text=text.replace('GUI::GUITab::RenderFunctionType','void (*)(CppUserModBase*)')
(O/'Mod/CppUserModBase.hpp').write_text(text,encoding='utf-8')
exports=[]
raw_exports=(R/'deps/ue4ss-exports.txt').read_bytes()
export_text=raw_exports.decode('utf-16' if raw_exports[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig',errors='replace')
for line in export_text.splitlines():
    match=re.match(r'\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)',line)
    if match:exports.append(match.group(1))
assert len(exports)>1000,len(exports)
(O/'UE4SS.def').write_text('LIBRARY UE4SS.dll\nEXPORTS\n'+'\n'.join(exports)+'\n',encoding='ascii')
subprocess.run([r'D:\VScode\Root\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\lib.exe','/nologo','/machine:x64',f'/def:{O / "UE4SS.def"}',f'/out:{O / "UE4SS.lib"}'],check=True)
binary=Path(r'E:\SteamLibrary\steamapps\common\PBZ\platform\steam\ue4ss\UE4SS.dll')
(R/'generated/sdk_provenance.json').write_text(json.dumps({
    'ue4ss_commit':'1ceefac8b8c286f0fb3eb62b4aae075ff81df0bd',
    'ue4ss_dll_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
    'public_header_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
    'adapter_header_sha256':hashlib.sha256((O/'Mod/CppUserModBase.hpp').read_bytes()).hexdigest(),
    'export_count':len(exports),'private_sdk_used':False,'guessed_game_object_offsets':False,
    'header_edits':['GUI include replaced by forward declaration','GUI callback alias expanded to identical function pointer type']
},indent=2),encoding='utf-8')
print('Prepared exact-build UE4SS import library',len(exports))

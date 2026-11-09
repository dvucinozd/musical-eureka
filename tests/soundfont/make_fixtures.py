"""Own CC0 synthetic SF2 banks; no vendor samples. Deterministic bytes."""
import struct as S, math, json, hashlib
from pathlib import Path
out=Path(__file__).parent/'fixtures';out.mkdir(exist_ok=True)
def chunk(tag,b):return tag.encode()+S.pack('<I',len(b))+b+b'\0'*(len(b)%2)
def list_(tag,b):return chunk('LIST',tag.encode()+b)
def name(s):return s.encode().ljust(20,b'\0')
def gen(op,v):return S.pack('<HH',op,v&65535)
def bank(layers=1,rate=48000,odd=False,mode=1,global_offset=None,local_offset=None,root=60,loop=(64,448),split=False,exclusive=0,keyscale=0,banknum=0,program=0,velocity_split=False,dynamic=False,stereo=False,hostile=False,compat=False,preset_scalar=None,filter_q=480,terminal_mod=False):
    pcm=S.pack('<'+'h'*558,*([int(12000*math.sin(2*math.pi*i/64)) for i in range(512)]+[0]*46))
    if stereo:pcm+=S.pack('<'+'h'*558,*([-int(12000*math.sin(2*math.pi*i/64)) for i in range(512)]+[0]*46))
    ph=name('Synthetic')+S.pack('<HHHIII',program,banknum,0,0,0,0)+name('EOP')+S.pack('<HHHIII',0,0,1,0,0,0)
    pg=(gen(*preset_scalar) if preset_scalar else b'')+gen(41,0)+gen(0,0);pb=S.pack('<HHHH',0,0,2 if preset_scalar else 1,0)
    if hostile:
        ph=ph[:24]+S.pack('<H',0)+ph[26:62]+S.pack('<H',1024)+ph[64:]
        pg=(gen(43,0)+gen(41,0))*1024+gen(0,0)
        pb=b''.join(S.pack('<HH',i*2,0) for i in range(1025))
    ig=b'';bags=[]
    if global_offset is not None:bags.append((len(ig)//4,0));ig+=gen(0,global_offset)
    for i in range(layers):
        bags.append((len(ig)//4,0))
        if split:ig+=gen(43,(i%128)*257)
        if hostile:ig+=gen(43,0x7f7f)
        if velocity_split:ig+=gen(44,0x3f00 if i%2==0 else 0x7f40)
        if exclusive:ig+=gen(57,exclusive)
        if keyscale:ig+=gen(39,keyscale)+gen(40,keyscale)+gen(31,keyscale)+gen(32,keyscale)
        if dynamic:
            for op,val in [(5,1200),(6,1200),(7,1200),(8,4000),(9,filter_q),(10,2400),(11,2400),(13,100),(22,0),(24,0),(26,-1200),(28,1200),(29,500),(34,-1200),(36,1200),(37,50)]:ig+=gen(op,val)
        if compat:
            ig+=gen(8,13500)+gen(22,-16000)+gen(24,4500)
            for op in [21,23,25,26,27,28,30,33,34,35,36,38]:ig+=gen(op,-32768)
        if local_offset is not None:ig+=gen(0,local_offset)
        ig+=gen(54,mode)+gen(53,i%2 if stereo else 0)
    bags.append((len(ig)//4,0));ig+=gen(0,0)
    ib=b''.join(S.pack('<HH',*b) for b in bags)
    inst=name('Sine')+S.pack('<H',0)+name('EOI')+S.pack('<H',len(bags)-1)
    sh=name('Wave')+S.pack('<IIIIIBbHH',0,511,*loop,rate,root,0,1 if stereo else 0,4 if stereo else 1)
    if stereo:sh+=name('Right')+S.pack('<IIIIIBbHH',558,1069,622,1006,rate,root,0,0,2)
    sh+=name('EOS')+bytes(26)
    pd=b''.join(chunk(k,v) for k,v in [('phdr',ph),('pbag',pb),('pmod',bytes(8)+(b'\x02\x00' if terminal_mod else bytes(2))),('pgen',pg),('inst',inst),('ibag',ib),('imod',bytes(8)+(b'\x16\x00' if terminal_mod else bytes(2))),('igen',ig),('shdr',sh)])
    info=chunk('ifil',S.pack('<HH',2,4))+(chunk('INAM',b'odd') if odd else b'')
    body=b'sfbk'+list_('INFO',info)+list_('sdta',chunk('smpl',pcm))+list_('pdta',pd)
    return b'RIFF'+S.pack('<I',len(body))+body
manifest={}
for label,args in [('mono',{}),('odd_info',{'odd':True}),('two_layers',{'layers':2}),('four_layers',{'layers':4}),('five_layers',{'layers':5}),('rate22050',{'rate':22050}),('rate44100',{'rate':44100}),('one_shot',{'mode':0}),('release_loop',{'mode':3}),('offset_add',{'global_offset':64,'local_offset':-32}),('short_loop',{'loop':(64,65),'root':0,'rate':192000}),('max_regions',{'layers':256,'split':True}),('exclusive',{'layers':2,'exclusive':1}),('keyscale',{'keyscale':40}),('bank128',{'banknum':128,'program':127}),('velocity_split',{'layers':2,'velocity_split':True}),('dynamic',{'dynamic':True}),('dynamic_moderate',{'dynamic':True,'filter_q':240}),('stereo_pair',{'layers':2,'stereo':True})]:
    b=bank(**args);(out/(label+'.sf2')).write_bytes(b);manifest[label]={'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b),'expected':'LIMIT' if label=='five_layers' else 'OK','parameters':args}
def bad(label,b,expected):
    b=bytes(b);(out/(label+'.sf2')).write_bytes(b);manifest[label]={'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b),'expected':expected}
base=bank()
compat=bank(compat=True);bad('compat_sentinels',compat,'OK')
for tag in ['phdr','pbag','pmod','pgen','inst','ibag','imod','igen','shdr','smpl']:
    b=bytearray(base);pos=b.index(tag.encode());b[pos+4:pos+8]=b'\xff'*4;bad('bad_count_'+tag,b,'INVALID')
b=bytearray(base);b[b.index(b'igen')+14:b.index(b'igen')+16]=b'\xff\xff';bad('bad_sample_id',b,'INVALID')
b=bytearray(base);b[b.index(b'shdr')+28:b.index(b'shdr')+32]=b'\xff'*4;bad('bad_sample_start',b,'INVALID')
b=bytearray(base);b[b.index(b'shdr')+53]=128;bad('rom_sample',b,'UNSUPPORTED')
b=bytearray(base);b[b.index(b'smpl')+8:b.index(b'smpl')+12]=b'OggS';bad('sf3_ogg',b,'UNSUPPORTED')
b=bytearray(base);b[b.index(b'ifil')+8]=3;bad('sf3_version',b,'UNSUPPORTED')
b=base+base[12:36];b=b[:4]+S.pack('<I',len(b)-8)+b[8:];bad('duplicate_info',b,'INVALID')
b=bytearray(bank(odd=True));p=b.index(b'odd');del b[p+3];b[4:8]=S.pack('<I',len(b)-8);bad('bad_odd_padding',b,'INVALID')
bad('bad_keyscale',bank(keyscale=32767),'INVALID')
bad('unsupported_keyscale',bank(keyscale=1200),'UNSUPPORTED')
bad('bad_signed_offset',bank(local_offset=-1),'INVALID')
bad('hostile_empty_intersections',bank(layers=512,hostile=True),'LIMIT')
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')

# Preset scalar values are offsets added to the instrument value; TSF clamps the
# merged sum (docs/SOUNDFONT_SF2_COMPAT_2026-10-30.md, C2). Tuning and key
# scaling keep their bounded windows at preset level.
for label,op,val in [('preset_negative_filter',8,-1200),('preset_negative_attenuation',48,-60)]:
    bad(label,bank(preset_scalar=(op,val)),'OK')
for op in [5,6,7,8,9,10,11,13,17,21,22,23,24,25,26,27,28,29,30,33,34,35,36,37,38,48]:
    for tag,val in [('min',-32767),('max',32767)]:
        bad(f'preset_offset_{op}_{tag}',bank(preset_scalar=(op,val)),'OK')
bad('preset_tuning_outside',bank(preset_scalar=(51,121)),'UNSUPPORTED')
bad('preset_keyscale_outside',bank(preset_scalar=(39,41)),'UNSUPPORTED')
# The terminal modulator record is always ignored (SF2 2.04); tools leave junk.
bad('terminal_modulator_nonzero',bank(terminal_mod=True),'OK')
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
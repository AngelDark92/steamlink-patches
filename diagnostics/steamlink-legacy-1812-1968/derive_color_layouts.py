import sys, io, json, hashlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / '../builds/steamlink-patches/build/oled-native-audit/python'))
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from capstone.arm64 import ARM64_OP_IMM, ARM64_OP_MEM

out = Path(__file__).resolve().parent
out.mkdir(parents=True, exist_ok=True)
results = []
for ver, code in [('2.0.20','5001812'),('2.0.21','5001968')]:
    p = Path(f'decoded-apk-android-steamlinkvr-release-base-{ver}-{code}/lib/arm64-v8a/libvrlink_scene.so')
    b = p.read_bytes(); e = ELFFile(io.BytesIO(b))
    syms = list(e.get_section_by_name('.dynsym').iter_symbols())
    sym = {s.name:s for s in syms}
    names = {s['st_value']:s.name for s in syms if s['st_value']}
    plt=e.get_section_by_name('.plt')
    for i,r in enumerate(e.get_section_by_name('.rela.plt').iter_relocations()):
        names[plt['sh_addr']+32+i*16] = syms[r['r_info_sym']].name
    def off(a):
        for s in e.iter_segments():
            if s['p_type']=='PT_LOAD' and s['p_vaddr']<=a<s['p_vaddr']+s['p_filesz']:
                return a-s['p_vaddr']+s['p_offset']
        raise ValueError(hex(a))
    def h(x): return hashlib.sha256(x).hexdigest()
    def guard(offset,size,**kw): return dict(offset=hex(offset),size=size,sha256=h(b[offset:offset+size]),**kw)
    def fg(name):
        s=sym[name]; return guard(off(s['st_value']),s['st_size'],symbol=name,vaddr=hex(s['st_value']))
    def unique(needle):
        hits=[]; start=0
        while (start:=b.find(needle,start))>=0: hits.append(start); start+=1
        assert len(hits)==1,(needle,hits)
        return hits[0]
    md=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
    funcs=[s for s in syms if s['st_size'] and any(x in s.name for x in ['SRGBCorrectionPass','QSVLRendererXR15SetupSwapchains','XRQCreateSwapchain','chewInit'])]
    lines=[]; draws=[]; formats=[]
    for s in sorted(funcs,key=lambda s:s['st_value']):
        a=s['st_value']; lines.append(f'\n{s.name} @ {a:#x} size {s["st_size"]}')
        for ins in md.disasm(b[off(a):off(a)+s['st_size']],a):
            target=names.get(int(ins.op_str[1:],16),'') if ins.mnemonic in ('b','bl') and ins.op_str.startswith('#0x') else ''
            lines.append(f'{ins.address:#x} {ins.bytes.hex():8} {ins.mnemonic:8} {ins.op_str} {target}')
            if 'RenderSpecific' in s.name and target=='glDrawArrays': draws.append(hex(ins.address+4))
            if 'SetupSwapchains' in s.name and ins.bytes==bytes.fromhex('69889152'): formats.append(hex(off(ins.address)))
    (out/f'color-disassembly-{code}.txt').write_text('\n'.join(lines))
    prefix=unique(b'#extension GL_OES_EGL_image_external_essl3 : enable')
    prefix=b.rfind(b'#version 300 es\n',0,prefix)
    suffix=unique(b'\n            vec2 placeInSection = fract(uvmask')
    opaque=unique(b'\n\t\t\tcolor.a = 1.0;\n        }\n')
    assert b[prefix+1087]==0 and b[suffix+296]==0 and b[opaque+29]==0
    before=bytes.fromhex('e1a30091e00314aae2031caae822099b'); after=bytes.fromhex('e91b00f9082140b9e83b00b9')
    for f in formats:
        n=int(f,16); assert b[n-16:n]==before and b[n+4:n+16]==after
    sites=[]
    for original,replacement in [('libGLESv3.so','libgxd.so'),('glShaderSource','gxShaderSource'),('glDrawArrays','gxDrawArrays'),('eglCreateContext','gxdCreateContext'),('eglDestroyContext','gxdDestroyContext'),('eglMakeCurrent','gxdMakeCurrent'),('eglTerminate','gxdTerminate'),('glCompileShader','gxCompileShader'),('glLinkProgram','gxLinkProgram'),('glDeleteShader','gxDeleteShader'),('glDeleteProgram','gxDeleteProgram')]:
        # Direct imports use dynstr offsets; loader names use exact rodata C strings.
        if original in ['glShaderSource','glDrawArrays','eglCreateContext','eglDestroyContext','eglMakeCurrent','eglTerminate']:
            s=sym[original]; assert s['st_shndx']=='SHN_UNDEF'
            n=e.get_section_by_name('.dynstr')['sh_offset']+s['st_name']
        else:
            sect=e.get_section_by_name('.dynstr') if original=='libGLESv3.so' else e.get_section_by_name('.rodata')
            needle=original.encode()+b'\0'; data=sect.data(); n=sect['sh_offset']+data.index(needle)
            assert data.count(needle)==1
        assert b[n:n+len(original)+1]==original.encode()+b'\0'
        sites.append(dict(offset=hex(n),stock=original,hooked=replacement))
    ctor=next(s.name for s in syms if 'SRGBCorrectionPassC2' in s.name)
    render='_ZN18SRGBCorrectionPass14RenderSpecificEibj'; uniforms='_ZN18SRGBCorrectionPass20UpdateShaderUniformsEv'
    guards=[fg('chewInit'),fg(render),fg(uniforms),guard(opaque,29),guard(suffix,296)]
    guards += [fg(n) for n in ['glCompileShader','glLinkProgram','glDeleteShader','glDeleteProgram']]
    guards += [guard(e.get_section_by_name(n)['sh_offset'],e.get_section_by_name(n)['sh_size'],section=n) for n in ['.dynsym','.dynamic','.rela.dyn','.rela.plt','.gnu.hash']]
    # Independently trace text address materialization and relocation references.
    targetnames={prefix:'prefix',suffix:'masked suffix',opaque:'opaque suffix'}
    for site in sites[-4:]: targetnames[int(site['offset'],16)]=site['stock']+' loader name'
    for r in e.get_section_by_name('.rela.dyn').iter_relocations():
        if r['r_addend'] in targetnames: targetnames[r['r_offset']]='pointer to '+targetnames[r['r_addend']]
    md.detail=True
    textsec=e.get_section_by_name('.text'); instructions=list(md.disasm(textsec.data(),textsec['sh_addr']))
    references=[]; ref_lines=[]
    for index,ins in enumerate(instructions):
        if ins.mnemonic!='adrp': continue
        reg=ins.operands[0].reg; page=ins.operands[1].imm
        for j in instructions[index+1:index+8]:
            address=None
            if j.mnemonic=='add' and len(j.operands)==3 and j.operands[1].reg==reg and j.operands[2].type==ARM64_OP_IMM: address=page+j.operands[2].imm
            if j.mnemonic=='ldr' and len(j.operands)>1 and j.operands[1].type==ARM64_OP_MEM and j.operands[1].mem.base==reg: address=page+j.operands[1].mem.disp
            if address in targetnames:
                references.append(dict(adrp=hex(ins.address),use=hex(j.address),target=hex(address),kind=targetnames[address]))
                ref_lines.append(f'\nREFERENCE {ins.address:#x}->{address:#x}: {targetnames[address]}')
                for k in instructions[max(0,index-12):index+24]:
                    target=names.get(int(k.op_str[1:],16),'') if k.mnemonic in ('b','bl') and k.op_str.startswith('#0x') else ''
                    ref_lines.append(f'{k.address:#x} {k.bytes.hex():8} {k.mnemonic:8} {k.op_str} {target}')
    (out/f'color-references-{code}.txt').write_text('\n'.join(ref_lines))
    results.append(dict(version=ver,code=code,path=str(p),size=len(b),sha256=h(b),prefix=guard(prefix,1087),masked=guard(suffix,296),opaque=guard(opaque,29),stockMaskedCompleteHash=h(b[prefix:prefix+1087]+b[suffix:suffix+296]),stockOpaqueCompleteHash=h(b[prefix:prefix+1087]+b[opaque:opaque+29]),swapchainOffsets=formats,drawReturns=draws,constructor=fg(ctor),render=fg(render),sites=sites,guards=guards,references=references))
(out/'color-layouts.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps([{k:r[k] for k in ['code','size','sha256','prefix','masked','opaque','swapchainOffsets','drawReturns']} for r in results],indent=2))

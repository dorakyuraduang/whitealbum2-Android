"""Prove optimizer transfer boundaries on small synthetic CFGs; no EXE input."""
import importlib.util
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('shared_aot',ROOT/'scripts/generate_shared_aot.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


def arithmetic(name='add',carry=False):
    operation=f'wr::{name}(f,a,b,32'+(',((s.flags&wr::CF)!=0))' if carry else ')')
    return '{auto a=wr::reg(s,0,32,0);auto b=wr::reg(s,1,32,0);auto& f=s;auto v='+operation+';wr::set_reg(s,0,v,32,0);s.flags=f.flags;s.defined_flags=f.defined_flags;}\ns.eip=0x2u;'


def run(parts,transfers=None):
    bodies={index:(body,b'',body) for index,body in enumerate(parts)}
    edges={index:[dict(kind='fallthrough',to=index+1)] for index in range(len(parts)-1)}
    edges[len(parts)-1]=[dict(kind='return',to=None)]
    if transfers:edges.update(transfers)
    return module.dead_flag_operations(dict(locations=list(bodies)),bodies,edges)


def main():
    returned='s.eip=wr::pop(s,m);'
    assert run([arithmetic(),arithmetic('sub'),returned])=={0:'add'}
    assert run([arithmetic(),arithmetic('add',True),returned])=={}
    assert run([arithmetic(),arithmetic('logic'),returned])=={} # raw AF survives AND/OR/XOR
    assert run([arithmetic(),'wr::set_reg(s,0,m.load(0x1000u,32),32,0);',arithmetic('sub'),returned])=={}
    assert run([arithmetic(),'throw wr::GuestFault(wr::FaultKind::unsupported,s.eip,"stop");',arithmetic('sub'),returned])=={}
    assert run([arithmetic(),arithmetic('sub'),returned],{0:[dict(kind='call_direct',to=2),dict(kind='call_continuation',to=1)]})=={}
    assert run([arithmetic(),'s.eip=(s.flags&wr::ZF)?0x2u:0x3u;',arithmetic('sub'),returned],
               {1:[dict(kind='branch_taken',to=2),dict(kind='branch_not_taken',to=3)]})=={}
    assert run([arithmetic(),arithmetic('sub'),returned],{1:[dict(kind='branch_taken',to=0),dict(kind='branch_not_taken',to=2)]})=={0:'add'}
    bad=arithmetic('sub').replace('wr::reg(s,0,32,0)','wr::reg(s,8,32,0)')
    assert run([arithmetic(),bad,returned])=={}
    lifted=module.register_integers('wr::set_reg(s,0,wr::reg(s,1,16,0),8,8);')
    assert lifted=='integer.template set<0,8,8>(integer.template get<1,16,0>());'
    assert 'integer.external' in module.register_integers('wr::multiply(s,wr::reg(s,1,32,0),32,true);')
    for source in ['wr::reg(s,8,32,0);','wr::reg(s,0,64,0);','wr::guest_cpuid(s);','s.r[x]=0;']:
        try:module.register_integers(source)
        except ValueError:pass
        else:raise AssertionError('unsafe integer source accepted: '+source)
    bad=arithmetic('sub').replace('wr::reg(s,0,32,0)','wr::reg(s,0,64,0)')
    assert run([arithmetic(),bad,returned])=={}
    leaf={1:dict(locations=[1,2],calls=[])}
    bodies={1:(arithmetic(),b'',arithmetic()),2:(returned,b'',returned)}
    edges={1:[dict(kind='fallthrough',to=2)],2:[dict(kind='return',to=None)]}
    assert module.floating_neutral_leaves(leaf,bodies,edges,set())=={1}
    assert module.floating_neutral_leaves(leaf,bodies,edges,{1})==set()
    for unsafe in ('s.fp.put16(0,0);','wr::x87_wait(s);','wr::sse_execute(s,0);','wr::guest_rdtsc(s);'):
        changed={**bodies,1:(unsafe,b'',unsafe)}
        assert module.floating_neutral_leaves(leaf,changed,edges,set())==set()
    for kind,target in (('call_direct',2),('call_indirect',None),('jump_import',None),('jump_direct',3)):
        changed={**edges,1:[dict(kind=kind,to=target)]}
        assert module.floating_neutral_leaves(leaf,bodies,changed,set())==set()
    direct='f_1<Metered>(s,m,scope,budget,0u);'
    assert module.register_floating(direct,{1})==direct
    assert 'floating.external' in module.register_floating(direct,set())
    print('PASS static flag liveness: carry, undefined bits, joins, loops, calls and precise fault boundaries.')


if __name__=='__main__':main()

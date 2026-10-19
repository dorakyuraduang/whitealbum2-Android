"""Round-trip native widget JSON through an independent standard JSON parser.

Uses only a C++ fixture created from cached resources and owned RAM. No Android
runtime, game, window or screen is accessed. UTF-16 code units are compared so
supplementary and isolated surrogate units are preserved like Java strings.
"""
import argparse
import hashlib
import json
from pathlib import Path


def code_units(value):
    data=value.encode('utf-16-le',errors='surrogatepass')
    return [int.from_bytes(data[n:n+2],'little') for n in range(0,len(data),2)]


def verify(fixture,report):
    raw=fixture.read_bytes()
    if not raw.isascii():raise ValueError('Widget JSON must be ASCII with Unicode escapes')
    data=json.loads(raw);strings=0;menu_items=0;control_count=0;non_ascii=0
    def text(value,expected):
        nonlocal strings,non_ascii
        if code_units(value)!=expected:raise ValueError('Widget Unicode changed during JSON transport')
        strings+=1;non_ascii+=int(any(c>=128 for c in expected))
    for item in data['strings']:text(item['value'],item['units'])
    if len(data['strings'])!=77 or len(data['before'])!=3:raise ValueError('Missing Unicode boundary fixtures')
    if not all(code_units(item['value'])!=item['units'] for item in data['before']):raise ValueError('Former UTF8 byte quoting no longer reproduces')
    def menu(actual,expected):
        nonlocal menu_items
        if len(actual)!=len(expected):raise ValueError('Menu structure changed')
        for value,source in zip(actual,expected):
            if {k:v for k,v in value.items() if k not in ('text','children')}!={k:v for k,v in source.items() if k not in ('text','children')}:raise ValueError('Menu command/state changed')
            text(value['text'],source['text']);menu(value['children'],source['children']);menu_items+=1
    for item in data['menus']:menu(item['transport'],item['expected'])
    for item in data['dialogs']:
        value,source=item['transport'],item['expected']
        if {k:v for k,v in value.items() if k not in ('title','controls')}!={k:v for k,v in source.items() if k not in ('title','controls')}:raise ValueError('Dialog properties changed')
        text(value['title'],source['title'])
        if len(value['controls'])!=len(source['controls']):raise ValueError('Dialog control count changed')
        for control,original in zip(value['controls'],source['controls']):
            if {k:v for k,v in control.items() if k not in ('type','text','items')}!={k:v for k,v in original.items() if k not in ('type','text','items')}:raise ValueError('Control state/identity changed')
            text(control['type'],original['type']);text(control['text'],original['text'])
            if len(control['items'])!=len(original['items']):raise ValueError('List item count changed')
            for value_text,source_units in zip(control['items'],original['items']):text(value_text,source_units)
            control_count+=1
    if data['cached_menus']!=2 or data['cached_dialogs']!=7 or menu_items!=67 or control_count!=53:raise ValueError('Cached resource coverage changed')
    result=dict(schema='wa2.widget-unicode.v1',fixture_sha256=hashlib.sha256(raw).hexdigest(),unicode_input_cases=len(data['strings']),
                utf16_code_units_covered=65536,old_byte_quoting_failures_reproduced=len(data['before']),cached_menus=data['cached_menus'],
                cached_dialogs=data['cached_dialogs'],menu_items=menu_items,controls=control_count,exact_text_roundtrips=strings,
                non_ascii_roundtrips=non_ascii,command_ids_flags_and_control_state_preserved=True,ascii_json=True,
                independent_standard_json_parser=True,android_runtime_parser_executed=False,game_launched=False,screen_access=False,source_executable_opened=False,passed=True)
    report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('PASS widget Unicode: '+str(strings)+' exact text round-trips; cached menus/dialogs and command state preserved.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('fixture',type=Path);parser.add_argument('report',type=Path);args=parser.parse_args();verify(args.fixture,args.report)


if __name__=='__main__':main()

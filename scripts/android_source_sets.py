"""Select Android presentation adapters without duplicating common game logic."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LEGACY_ONLY=frozenset({'PortUi.java','NativeWidgets.java','WidgetSession.java','WidgetControls.java','WidgetIcon.java','FileChooser.java'})

def java_sources(backend):
    main=sorted((ROOT/'android/app/src/main/java').rglob('*.java'))
    if backend=='shared-aot':return main
    if backend!='native':raise ValueError('Unknown Android source backend: '+backend)
    sources=[path for path in main if path.name not in LEGACY_ONLY]
    sources+=sorted((ROOT/'android/app/src/native/java').rglob('*.java'))
    if len([path for path in sources if path.name=='PortUi.java'])!=1:raise ValueError('Native Android needs exactly one native presentation adapter')
    for path in sources:
        value=path.read_text(encoding='utf-8')
        for token in ('NativeWidgets','WidgetSession','WidgetControls','WidgetIcon','FileChooser','"wa2_android"'):
            if token in value:raise ValueError('Legacy Android dependency in native source '+str(path)+': '+token)
    return sorted(sources)

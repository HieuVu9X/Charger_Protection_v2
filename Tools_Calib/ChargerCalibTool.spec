# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['C:/Users/Admin/OneDrive/Desktop/hieuvm/14. Charger_Protection_v2/Charger_Protection_v2/Tools_Calib/charger_tool.py'],
    pathex=['C:/Users/Admin/OneDrive/Desktop/hieuvm/14. Charger_Protection_v2/Charger_Protection_v2/Tools_Calib'],
    binaries=[],
    datas=[],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='ChargerCalibTool',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

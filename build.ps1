$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'bin'
New-Item -ItemType Directory -Force -Path $out | Out-Null

Push-Location $root
try {
    cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cl.exe /nologo /O2 /MT /std:c++20 /EHsc /I src /I third_party\imgui /I third_party\imgui\backends src\main.cpp src\core\config.cpp src\core\updater.cpp src\modules\audio_module.cpp src\modules\media_module.cpp src\modules\system_module.cpp src\input\global_hook.cpp src\ui\overlay_window.cpp src\ui\radial_renderer.cpp third_party\imgui\imgui.cpp third_party\imgui\imgui_draw.cpp third_party\imgui\imgui_tables.cpp third_party\imgui\imgui_widgets.cpp third_party\imgui\backends\imgui_impl_win32.cpp third_party\imgui\backends\imgui_impl_dx11.cpp /Fo:bin\ /Fe:bin\RadialMenuWHook.exe /link user32.lib gdi32.lib d3d11.lib dxgi.lib dwmapi.lib shell32.lib ole32.lib winhttp.lib /SUBSYSTEM:WINDOWS'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}

Get-ChildItem $out -Filter *.exe | Select-Object Name, @{N='KB';E={[math]::Round($_.Length/1KB,1)}} | Format-Table -AutoSize

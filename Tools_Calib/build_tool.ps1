param([string]$Python = (Join-Path $PSScriptRoot '.python/python.exe'))
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Python)) {
    throw 'Python not found. Pass -Python with the python.exe path and install requirements-build.txt.'
}
& $Python -m PyInstaller --noconfirm --clean --onefile --windowed --noupx `
    --name ChargerCalibTool --distpath (Join-Path $PSScriptRoot 'dist') `
    --workpath (Join-Path $PSScriptRoot 'build') --specpath $PSScriptRoot `
    --paths $PSScriptRoot (Join-Path $PSScriptRoot 'charger_tool.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$smoke = Start-Process -FilePath (Join-Path $PSScriptRoot 'dist/ChargerCalibTool.exe') `
    -ArgumentList '--smoke-test' -WindowStyle Hidden -Wait -PassThru
if ($smoke.ExitCode -ne 0) { exit $smoke.ExitCode }
Write-Output ('Built: ' + (Join-Path $PSScriptRoot 'dist/ChargerCalibTool.exe'))

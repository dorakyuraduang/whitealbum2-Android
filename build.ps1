param(
    [ValidateSet('Build','Test')][string]$Action = 'Build',
    [string]$Python
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
if (-not $Python) {
    $bundledPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
    if (Test-Path -LiteralPath $bundledPython) { $Python = $bundledPython }
    else { $Python = (Get-Command python -ErrorAction Stop).Source }
}
$cmake = Join-Path $projectRoot 'tools\python\cmake\data\bin\cmake.exe'
$ctest = Join-Path $projectRoot 'tools\python\cmake\data\bin\ctest.exe'
$upstream = Join-Path $projectRoot 'tools\WindowsRecompTool-main'
if (-not (Test-Path -LiteralPath $cmake)) { throw 'Portable dependencies are missing. See README.md.' }
$env:PYTHONPATH = Join-Path $projectRoot 'tools\python'
$env:PYTHONUTF8 = '1'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot 'build\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $projectRoot 'build\zig-local'
$env:TEMP = Join-Path $projectRoot 'build\temp'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Path $env:TEMP,(Join-Path $projectRoot 'logs') -Force | Out-Null
function Invoke-Logged([string]$Executable,[string[]]$Arguments,[string]$Log) {
    & $Executable @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $projectRoot "logs\$Log")
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE. See logs\$Log." }
}
Push-Location -LiteralPath $projectRoot
try {
    Invoke-Logged $cmake @('-S',$upstream,'-B','build\winrecomp','-G','Ninja',"-DCMAKE_TOOLCHAIN_FILE=$projectRoot/scripts/zig-toolchain.cmake",'-DCMAKE_BUILD_TYPE=Release',"-DPython3_EXECUTABLE=$Python",'-DBUILD_TESTING=ON') 'toolchain-configure.log'
    Invoke-Logged $cmake @('--build','build\winrecomp','--parallel','2') 'toolchain-build.log'
    if ($Action -eq 'Test') {
        Invoke-Logged $ctest @('--test-dir','build\winrecomp','--output-on-failure','-E','^(gui_boundary|directdraw_boundary)$') 'core-regression-tests.log'
        Invoke-Logged $Python @('scripts\verify_flags.py') 'flags-tests.log'
        Invoke-Logged $Python @('scripts\verify_memory.py') 'memory-tests.log'
        Invoke-Logged $Python @("$upstream/tests/conformance.py",'--tool','build\winrecomp\winrecomp.exe','--cxx',"$projectRoot/scripts/zig-cxx.cmd",'--out','build\wa2-integer-tests','--target','WA2_chs.exe','--entry','0x479140','--entry','0x479160','--vectors','1024') 'wa2-integer-tests.log'
        Invoke-Logged $Python @("$upstream/tests/floating.py",'--tool','build\winrecomp\winrecomp.exe','--cxx',"$projectRoot/scripts/zig-cxx.cmd",'--native-lib','build\winrecomp\libwinrecomp_native.a','--out','build\sse-tests','--simd','--gas-win64','--vectors','96') 'sse-tests.log'
        if (-not (Test-Path -LiteralPath 'build\wa2-win64\CMakeCache.txt')) { throw 'Run .\build.ps1 before testing target API bridges.' }
        Invoke-Logged $cmake @('--build','build\wa2-win64','--parallel','2') 'port-tests-build.log'
        Invoke-Logged $Python @('scripts\make_resource_samples.py') 'resource-sample-build.log'
        Invoke-Logged $Python @('scripts\verify_execution.py','--program','build\wa2-win64\candidate\WA2_SDL2_x64.exe') 'execution-suite-build.log'
        Invoke-Logged $Python @('scripts\stage_runtime.py','--candidate','build\wa2-win64\candidate\WA2_SDL2_x64.exe') 'runtime-staging.log'
        return
    }
    Invoke-Logged $Python @('scripts\analyze_target.py') 'target-analysis.log'
    if (-not (Test-Path -LiteralPath 'config\wa2.json')) {
        Invoke-Logged 'build\winrecomp\winrecomp.exe' @('cfg','WA2_chs.exe','logs\cfg-initial.json') 'initial-discovery.log'
        Invoke-Logged $Python @('scripts\recover_seeds.py') 'seed-recovery.log'
    }
    if (-not (Test-Path -LiteralPath 'generated\wa2-win64\CMakeLists.txt')) {
        Invoke-Logged $Python @('scripts\generate_project.py') 'project-generation.log'
    }
    Invoke-Logged $cmake @('-S','generated\wa2-win64','-B','build\wa2-win64','-G','Ninja',"-DCMAKE_TOOLCHAIN_FILE=$projectRoot/scripts/zig-toolchain.cmake",'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG',"-DWINRECOMP_SOURCE=$upstream","-DPython3_EXECUTABLE=$Python") 'wa2-configure.log'
    Invoke-Logged $cmake @('--build','build\wa2-win64','--parallel','2') 'wa2-build.log'
    Invoke-Logged $ctest @('--test-dir','build\winrecomp','--output-on-failure','-E','^(gui_boundary|directdraw_boundary)$') 'core-regression-tests.log'
    Invoke-Logged $Python @('scripts\make_resource_samples.py') 'resource-sample-build.log'
    Invoke-Logged $Python @('scripts\verify_execution.py','--program','build\wa2-win64\candidate\WA2_SDL2_x64.exe') 'execution-suite-build.log'
    Invoke-Logged $Python @('scripts\verify_fault_table.py') 'fault-table-tests.log'
    Invoke-Logged $Python @('scripts\stage_runtime.py','--candidate','build\wa2-win64\candidate\WA2_SDL2_x64.exe') 'runtime-staging.log'
    Invoke-Logged $Python @('scripts\verify_output.py') 'verify-output.log'
} finally { Pop-Location }

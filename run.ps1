param([string]$DataRoot,[UInt64]$Budget = [UInt64]::MaxValue,[switch]$ReadOnly)
$ErrorActionPreference = 'Stop'
try {
    if ($Budget -eq 0) { throw 'Budget must be positive.' }
    $program = Join-Path $PSScriptRoot 'out\WA2_SDL2_x64.exe'
    if (-not (Test-Path -LiteralPath $program -PathType Leaf)) { throw 'Build out\WA2_SDL2_x64.exe first with .\build.ps1.' }
    if (-not $DataRoot) {
        $resourceRoot = Join-Path $PSScriptRoot 'res'
        if (Test-Path -LiteralPath $resourceRoot -PathType Container) { $DataRoot = $resourceRoot }
        else { $DataRoot = $PSScriptRoot }
    }
    if (-not (Test-Path -LiteralPath $DataRoot -PathType Container)) { throw "Resource directory does not exist: $DataRoot" }
    $DataRoot = (Get-Item -LiteralPath $DataRoot).FullName
    $logRoot = Join-Path $PSScriptRoot 'logs'
    New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
    $report = Join-Path $logRoot 'runtime.json'
    $console = Join-Path $logRoot 'launch-console.log'
    if (Test-Path -LiteralPath $report -PathType Leaf) { Remove-Item -LiteralPath $report }
    Write-Host "Resource directory: $DataRoot"
    Write-Host "Runtime report: $report"
    $programArguments = @('--root',$DataRoot,'--budget',"$Budget",'--report',$report)
    if ($ReadOnly) { $programArguments += '--read-only' }
    # GUI-subsystem executables do not reliably block PowerShell's '&' operator.
    # Hold this exact process and drain both pipes before reading its exit code.
    $quotedArguments = foreach ($value in $programArguments) {
        '"' + ([regex]::Replace([string]$value,'(\\*)"','$1$1\"') -replace '(\\+)$','$1$1') + '"'
    }
    $launch = New-Object System.Diagnostics.ProcessStartInfo
    $launch.FileName = $program
    $launch.Arguments = $quotedArguments -join ' '
    $launch.WorkingDirectory = $PSScriptRoot
    $launch.UseShellExecute = $false
    $launch.CreateNoWindow = $true
    $launch.RedirectStandardOutput = $true
    $launch.RedirectStandardError = $true
    $game = New-Object System.Diagnostics.Process
    $game.StartInfo = $launch
    try {
        [void]$game.Start()
        $standardOutput = $game.StandardOutput.ReadToEndAsync()
        $standardError = $game.StandardError.ReadToEndAsync()
        $game.WaitForExit()
        $nativeExitCode = $game.ExitCode
        [System.IO.File]::WriteAllText($console,($standardOutput.Result + $standardError.Result),[System.Text.UTF8Encoding]::new($false))
    } finally { $game.Dispose() }
    if ($nativeExitCode -ne 0) {
        Write-Host "WA2 x64 stopped (exit code $nativeExitCode)."
        if (Test-Path -LiteralPath $report -PathType Leaf) {
            $state = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
            if ($state.error) { Write-Host $state.error }
        }
        Write-Host "Console log: $console"
    }
    exit $nativeExitCode
} catch {
    Write-Host "Launch failed: $($_.Exception.Message)"
    exit 1
}

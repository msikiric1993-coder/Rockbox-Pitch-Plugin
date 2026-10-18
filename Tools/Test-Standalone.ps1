param([switch]$Hardware, [string]$OutputPattern, [string]$BuildDir)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = "$taskRoot\out\build\release" }
$taskExe = "$BuildDir\TDSpeedApp_artefacts\Release\TDSpeedPreview.exe"
$taskQA = "$taskRoot\out\QA"
New-Item -ItemType Directory -Force $taskQA | Out-Null
$taskCtest = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $taskCtest --test-dir "$BuildDir" -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'DSP regressions failed' }
foreach ($taskMode in @('self-test','probe','route-check','preview','ui-check')) {
    $taskExtension = if ($taskMode -eq 'preview') { '.png' } else { '.json' }
    $taskOutput = "$taskQA\$taskMode$taskExtension"
    $taskProcess = Start-Process -FilePath $taskExe -ArgumentList "--$taskMode",('"'+$taskOutput+'"') -WindowStyle Hidden -PassThru
    $taskProcess.WaitForExit()
    if ($taskProcess.ExitCode -ne 0) { throw "$taskMode failed" }
}
if ($Hardware) {
    if (Get-Process vsthost -ErrorAction SilentlyContinue) { throw 'Close VSTHost before hardware checks' }
    $taskReport = Get-Content "$taskQA\probe.json" -Raw | ConvertFrom-Json
    $taskCable = $taskReport.cables | Where-Object usable | Select-Object -First 1
    $taskPhysicalOutputs = @($taskReport.devices | Where-Object { -not $_.capture -and $_.state -eq 1 -and $_.cableKind -eq 0 })
    if ($OutputPattern) {
        $taskPhysical = $taskPhysicalOutputs | Where-Object { $_.name -match $OutputPattern } | Select-Object -First 1
    } else {
        $taskPhysical = $taskPhysicalOutputs | Where-Object { $_.id -eq $taskReport.multimedia } | Select-Object -First 1
        if (-not $taskPhysical) { $taskPhysical = $taskPhysicalOutputs | Select-Object -First 1 }
    }
    if (-not $taskCable -or -not $taskPhysical) { throw 'A usable cable and physical output are required for hardware checks' }
    $taskProcess = Start-Process -FilePath $taskExe -ArgumentList '--signal-check',('"'+$taskCable.capture+'"'),('"'+$taskCable.playback+'"'),('"'+$taskPhysical.id+'"'),('"'+"$taskQA\signal-check.json"+'"') -WindowStyle Hidden -PassThru
    $taskProcess.WaitForExit()
    if ($taskProcess.ExitCode -ne 0) { throw 'End-to-end signal check failed' }
    $taskProcess = Start-Process -FilePath $taskExe -ArgumentList '--dry-signal-check',('"'+$taskCable.capture+'"'),('"'+$taskCable.playback+'"'),('"'+$taskPhysical.id+'"'),('"'+"$taskQA\dry-signal-check.json"+'"') -WindowStyle Hidden -PassThru
    $taskProcess.WaitForExit()
    if ($taskProcess.ExitCode -ne 0) { throw 'Dry pass-through signal check failed' }
    $taskProcess = Start-Process -FilePath $taskExe -ArgumentList '--bridge-check',('"'+"$taskQA\bridge-check.json"+'"') -WindowStyle Hidden -PassThru
    $taskProcess.WaitForExit()
    if ($taskProcess.ExitCode -ne 0) { throw 'Startup/bypass/fallback check failed' }
    $taskProcess = Start-Process -FilePath $taskExe -ArgumentList '--stream-check','invalid-capture','invalid-output',('"'+"$taskQA\stream-failure.json"+'"') -WindowStyle Hidden -PassThru
    $taskProcess.WaitForExit()
    $taskFailure = Get-Content "$taskQA\stream-failure.json" -Raw | ConvertFrom-Json
    if ($taskProcess.ExitCode -eq 0 -or $taskFailure.running -or -not $taskFailure.error) { throw 'Invalid streams did not fail safely' }
    $taskProcess = Start-Process -FilePath $taskExe -ArgumentList '--routing-test' ,('"'+"$taskQA\watchdog-check.json"+'"') -WindowStyle Hidden -PassThru
    $taskProcess.WaitForExit()
    if ($taskProcess.ExitCode -ne 0) { throw 'Watchdog/routing recovery failed' }
}
Write-Output "Checks passed; reports in $taskQA"

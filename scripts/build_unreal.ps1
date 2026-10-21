[CmdletBinding()]
param(
    [ValidateSet('Editor', 'Game')]
    [string]$Target = 'Editor',
    [ValidateSet('Development', 'DebugGame', 'Shipping')]
    [string]$Configuration = 'Development',
    [string]$EngineRoot = '',
    [int]$ModuleSuffix = 0,
    [switch]$DisableUnity,
    [ValidateRange(1, 86400)]
    [int]$BuildLockTimeoutSeconds = 1800
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
. (Join-Path $PSScriptRoot 'maintenance/unreal_build_support.ps1')
. (Join-Path $PSScriptRoot 'maintenance/environment.ps1')
$projectRoot = Get-BuildCanonicalPath (Split-Path -Parent $PSScriptRoot)
$uproject = Join-Path $projectRoot 'unreal/HeliosOutpost.uproject'
$logDirectory = Join-Path $projectRoot 'unreal/Saved/Logs/Build'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$runId = '{0}_{1}_{2}' -f (Get-Date -Format 'yyyyMMdd_HHmmss_fff'), $PID, ([guid]::NewGuid().ToString('N').Substring(0, 8))
$logPath = Join-Path $logDirectory "unreal_build_$runId.log"
$summaryPath = [IO.Path]::ChangeExtension($logPath, '.json')
$writer = [IO.StreamWriter]::new($logPath, $false, [Text.UTF8Encoding]::new($false))
$writer.AutoFlush = $true
$timer = [Diagnostics.Stopwatch]::StartNew()
$targetName = if ($Target -eq 'Editor') { 'HeliosOutpostEditor' } else { 'HeliosOutpost' }
$lockStream = $null
$process = $null
$projectQueueSeconds = 0.0
$engineQueueSeconds = 0.0
$engineQueueStart = $null
$queueStart = $null
$buildStart = $null
$buildEnd = $null
$failure = $null
$rawOutput = [Text.StringBuilder]::new()
$summary = [ordered]@{
    started_at = (Get-Date -Format o); process_id = $PID
    project = $uproject; physical_project = $uproject
    target = $targetName; configuration = $Configuration; module_suffix = $ModuleSuffix
    engine = $EngineRoot; status = 'failed'; exit_code = $null; error = $null
    project_queue_seconds = 0; engine_queue_seconds = 0; queue_seconds = 0
    build_seconds = 0; total_seconds = 0
    actions = $null; compile_steps = $null; up_to_date = $null; makefile_reasons = @()
    log = $logPath
}
function Write-BuildMessage([string]$Message) {
    $writer.WriteLine('[{0}] {1}', (Get-Date -Format o), $Message)
    Write-Host $Message
}
function Write-ToolLine([string]$Line) {
    $writer.WriteLine($Line)
    [void]$rawOutput.AppendLine($Line)
    Write-Host $Line
    if ($Line -match 'Build\.bat is already running') {
        if ($null -eq $script:engineQueueStart) { $script:engineQueueStart = $timer.Elapsed.TotalSeconds }
    }
    elseif ($null -ne $script:engineQueueStart -and $Line -match '^(Using bundled|Running UnrealBuildTool:|Building UnrealBuildTool)') {
        $script:engineQueueSeconds += $timer.Elapsed.TotalSeconds - $script:engineQueueStart
        $script:engineQueueStart = $null
    }
}

try {
    Write-BuildMessage "Building $targetName $Configuration. Log: $logPath"
    if ($ModuleSuffix -gt 0 -and $Target -ne 'Editor') { throw 'ModuleSuffix is supported only for Editor builds.' }
    $EngineRoot = Resolve-HeliosEngineRoot -EngineRoot $EngineRoot
    $buildBatch = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
    if (-not (Test-Path -LiteralPath $buildBatch)) { throw "Unreal build script not found: $buildBatch" }
    if (-not (Test-Path -LiteralPath $uproject)) { throw "Project file not found: $uproject" }
    # Keep the installed engine's ASCII alias, which may be a junction into a Unicode path.
    # Only the project lock needs physical canonicalization; compiler paths must stay stable.
    $EngineRoot = [IO.Path]::GetFullPath($EngineRoot).TrimEnd('\')
    $buildBatch = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
    $summary.engine = $EngineRoot

    # Canonical paths make subst aliases and physical paths share one OS-owned lock.
    $lockDirectory = Join-Path $projectRoot 'unreal/Saved/Locks'
    New-Item -ItemType Directory -Force -Path $lockDirectory | Out-Null
    $lockPath = Join-Path $lockDirectory 'unreal_build.lock'
    $queueStart = $timer.Elapsed.TotalSeconds
    $nextNotice = 0.0
    while ($null -eq $lockStream) {
        try { $lockStream = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
        catch [IO.IOException] {
            if (($_.Exception.HResult -band 0xffff) -notin @(32, 33)) { throw }
            $elapsed = $timer.Elapsed.TotalSeconds - $queueStart
            if ($elapsed -ge $BuildLockTimeoutSeconds) { throw "Project build queue timed out after $BuildLockTimeoutSeconds seconds. Existing builds were left running." }
            if ($elapsed -ge $nextNotice) {
                Write-BuildMessage ('Waiting for the current project build ({0:N0}s); this request has not started compiling.' -f $elapsed)
                $nextNotice = $elapsed + 30
            }
            Start-Sleep -Milliseconds 200
        }
    }
    $projectQueueSeconds = $timer.Elapsed.TotalSeconds - $queueStart
    $owner = [Text.Encoding]::UTF8.GetBytes("pid=$PID`nstarted=$($summary.started_at)`nlog=$logPath`n")
    $lockStream.SetLength(0)
    $lockStream.Write($owner, 0, $owner.Length)
    $lockStream.Flush()

    $buildProject = Resolve-BuildProjectDrive $projectRoot
    $summary.project = $buildProject
    $arguments = @($targetName, 'Win64', $Configuration, "-Project=$buildProject", '-WaitMutex', '-NoHotReloadFromIDE')
    if ($DisableUnity) { $arguments += '-DisableUnity' }
    if ($ModuleSuffix -gt 0) {
        $description = Get-Content -LiteralPath $uproject -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($module in $description.Modules) { $arguments += "-ModuleWithSuffix=$($module.Name),$ModuleSuffix" }
    }
    Write-BuildMessage "Engine: $EngineRoot; project: $buildProject; physical project: $uproject"
    # Reject cmd expansion syntax instead of interpreting a path as shell code.
    foreach ($arg in @($buildBatch) + $arguments) {
        if ($arg -match '["%\r\n]') { throw 'Build paths/arguments cannot contain quotes, percent signs or line breaks.' }
    }
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $env:ComSpec
    $startInfo.Arguments = '/d /v:off /s /c "' + ((@($buildBatch) + $arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ') + '"'
    $startInfo.WorkingDirectory = $EngineRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = Get-BuildOutputEncoding
    $startInfo.StandardErrorEncoding = $startInfo.StandardOutputEncoding
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $buildStart = $timer.Elapsed.TotalSeconds
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadLineAsync()
    $stderrTask = $process.StandardError.ReadLineAsync()
    $nextEngineNotice = 0.0
    while ($null -ne $stdoutTask -or $null -ne $stderrTask -or -not $process.HasExited) {
        if ($null -ne $stdoutTask -and $stdoutTask.IsCompleted) {
            $line = $stdoutTask.GetAwaiter().GetResult()
            if ($null -eq $line) { $stdoutTask = $null }
            else { Write-ToolLine $line; $stdoutTask = $process.StandardOutput.ReadLineAsync() }
        }
        if ($null -ne $stderrTask -and $stderrTask.IsCompleted) {
            $line = $stderrTask.GetAwaiter().GetResult()
            if ($null -eq $line) { $stderrTask = $null }
            else { Write-ToolLine $line; $stderrTask = $process.StandardError.ReadLineAsync() }
        }
        if ($null -ne $engineQueueStart) {
            $engineWait = $timer.Elapsed.TotalSeconds - $engineQueueStart
            if ($projectQueueSeconds + $engineQueueSeconds + $engineWait -ge $BuildLockTimeoutSeconds) {
                # Stop only our waiting wrapper, never the build holding the engine lock.
                if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
                throw "Engine build queue timed out after $BuildLockTimeoutSeconds seconds. Existing builds were left running."
            }
            if ($engineWait -ge $nextEngineNotice) {
                Write-BuildMessage ('Waiting for another engine build ({0:N0}s); this request has not started compiling.' -f $engineWait)
                $nextEngineNotice = $engineWait + 30
            }
        }
        if (($null -eq $stdoutTask -or -not $stdoutTask.IsCompleted) -and ($null -eq $stderrTask -or -not $stderrTask.IsCompleted)) { Start-Sleep -Milliseconds 50 }
    }
    $process.WaitForExit()
    $buildEnd = $timer.Elapsed.TotalSeconds
    $summary.exit_code = $process.ExitCode
    if ($process.ExitCode -ne 0) { throw "Unreal build failed with exit code $($process.ExitCode). See $logPath" }
    $summary.status = 'succeeded'
}
catch {
    $failure = $_
    $summary.error = $_.Exception.Message
    Write-BuildMessage "ERROR: $($summary.error)"
}
finally {
    if ($null -eq $buildStart -and $null -ne $queueStart) { $projectQueueSeconds = $timer.Elapsed.TotalSeconds - $queueStart }
    if ($null -ne $engineQueueStart) { $engineQueueSeconds += $timer.Elapsed.TotalSeconds - $engineQueueStart }
    if ($null -ne $buildStart -and $null -eq $buildEnd) { $buildEnd = $timer.Elapsed.TotalSeconds }
    $summary.project_queue_seconds = [math]::Round($projectQueueSeconds, 3)
    $summary.engine_queue_seconds = [math]::Round($engineQueueSeconds, 3)
    $summary.queue_seconds = [math]::Round($projectQueueSeconds + $engineQueueSeconds, 3)
    if ($null -ne $buildStart) { $summary.build_seconds = [math]::Round([math]::Max(0.0, [double]($buildEnd - $buildStart - $engineQueueSeconds)), 3) }
    $summary.total_seconds = [math]::Round($timer.Elapsed.TotalSeconds, 3)
    $observations = Get-UnrealBuildObservations $rawOutput.ToString()
    foreach ($key in $observations.Keys) { $summary[$key] = $observations[$key] }
    try {
        [IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        $actionText = if ($null -eq $summary.actions) { 'unknown' } else { [string]$summary.actions }
        Write-BuildMessage "Build $($summary.status): queue $($summary.queue_seconds)s, build $($summary.build_seconds)s, total $($summary.total_seconds)s, actions $actionText. Summary: $summaryPath"
    }
    finally {
        $writer.Dispose()
        if ($null -ne $process) { $process.Dispose() }
        if ($null -ne $lockStream) { $lockStream.Dispose() }
        # Keep the ASCII alias alive so cached dependency paths continue to resolve.
    }
}
if ($null -ne $failure) { throw $failure }

[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$ArchiveDirectory = '',
    [ValidateRange(2, 64)]
    [int]$CookerCoreLimit = 3,
    [ValidateRange(1, 86400)]
    [int]$BuildLockTimeoutSeconds = 1800
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
. (Join-Path $PSScriptRoot 'maintenance/unreal_build_support.ps1')
. (Join-Path $PSScriptRoot 'maintenance/environment.ps1')
$projectRoot = Get-BuildCanonicalPath (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($ArchiveDirectory)) {
    $ArchiveDirectory = Join-Path (Split-Path -Parent $projectRoot) ((Split-Path -Leaf $projectRoot) + '_packaged')
}
$ArchiveDirectory = [IO.Path]::GetFullPath($ArchiveDirectory)
$logDirectory = Join-Path $projectRoot 'unreal/Saved/Logs/Package'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$runId = '{0}_{1}_{2}' -f (Get-Date -Format 'yyyyMMdd_HHmmss_fff'), $PID, ([guid]::NewGuid().ToString('N').Substring(0, 8))
$logPath = Join-Path $logDirectory "unreal_package_$runId.log"
$summaryPath = [IO.Path]::ChangeExtension($logPath, '.json')
$writer = [IO.StreamWriter]::new($logPath, $false, [Text.UTF8Encoding]::new($false))
$writer.AutoFlush = $true
$timer = [Diagnostics.Stopwatch]::StartNew()
$lockStream = $null
$failure = $null
$summary = [ordered]@{
    started_at = (Get-Date -Format o); process_id = $PID
    project = (Join-Path $projectRoot 'unreal/HeliosOutpost.uproject')
    engine = $EngineRoot; configuration = 'Shipping'; platform = 'Win64'; cooker_core_limit = $CookerCoreLimit
    archive_directory = $ArchiveDirectory; status = 'failed'
    exit_code = $null; error = $null; total_seconds = 0; log = $logPath
}
function Write-PackageMessage([string]$Message) {
    $writer.WriteLine($Message)
    Write-Host $Message
}

try {
    # A source-only checkout must explain missing assets before invoking Unreal.
    & (Join-Path $PSScriptRoot 'check_resources.ps1') | Out-Host
    $EngineRoot = Resolve-HeliosEngineRoot -EngineRoot $EngineRoot
    $summary.engine = $EngineRoot
    $uatBatch = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
    if (-not (Test-Path -LiteralPath $uatBatch)) { throw "Unreal AutomationTool entrypoint not found: $uatBatch" }
    $lockDirectory = Join-Path $projectRoot 'unreal/Saved/Locks'
    New-Item -ItemType Directory -Force -Path $lockDirectory | Out-Null
    $lockPath = Join-Path $lockDirectory 'unreal_build.lock'
    $nextNotice = 0.0
    while ($null -eq $lockStream) {
        try { $lockStream = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
        catch [IO.IOException] {
            if (($_.Exception.HResult -band 0xffff) -notin @(32, 33)) { throw }
            if ($timer.Elapsed.TotalSeconds -ge $BuildLockTimeoutSeconds) { throw "Project build/package queue timed out after $BuildLockTimeoutSeconds seconds. Existing jobs were left running." }
            if ($timer.Elapsed.TotalSeconds -ge $nextNotice) {
                Write-PackageMessage ('Waiting for the current project build/package ({0:N0}s).' -f $timer.Elapsed.TotalSeconds)
                $nextNotice = $timer.Elapsed.TotalSeconds + 30
            }
            Start-Sleep -Milliseconds 200
        }
    }
    $owner = [Text.Encoding]::UTF8.GetBytes("pid=$PID`nstarted=$($summary.started_at)`nlog=$logPath`n")
    $lockStream.SetLength(0)
    $lockStream.Write($owner, 0, $owner.Length)
    $lockStream.Flush()

    $buildProject = Resolve-BuildProjectDrive $projectRoot
    $localCache = (Join-Path (Split-Path -Parent $buildProject) 'DerivedDataCache').Replace('\', '/')
    # A full loose cook replaces old Zen store metadata; DDC stays project-local.
    # Bound pending work on small-memory machines without changing cooked quality.
    $cookerOptions = @(
        '-FullCook', '-SkipZenStore', "-LocalDataCachePath=$localCache",
        '-ini:Editor:[EditorDomain]:CookAttachmentsEnabled=False',
        "-corelimit=$CookerCoreLimit", '-CookProcessCount=1',
        '-asyncassetcompilationmaxconcurrency=2', '-asynctexturecompilationmaxconcurrency=1',
        '-asyncstaticmeshcompilationmaxconcurrency=1',
        '-ini:Editor:[CookSettings]:MaxConcurrentShaderJobs=128',
        '-ini:Editor:[CookSettings]:PackagesPerGC=200',
        '-ini:Editor:[CookSettings]:SoftGCMinimumPeriodSeconds=5',
        '-ini:Editor:[CookPlatformDataCacheSettings]:Texture2D=2',
        '-ini:Editor:[CookPlatformDataCacheSettings]:StaticMesh=2',
        '-ini:Editor:[CookPlatformDataCacheSettings]:SkeletalMesh=2',
        '-ini:Editor:[CookPlatformDataCacheSettings]:SoundWave=4'
    ) -join ' '
    $arguments = @(
        'BuildCookRun', "-project=$buildProject", '-target=HeliosOutpost',
        '-platform=Win64', '-clientconfig=Shipping', '-noP4', '-unattended', '-utf8output',
        '-ddc=InstalledNoZenLocalFallback', "-AdditionalCookerOptions=$cookerOptions",
        '-build', '-cook', '-stage', '-pak', '-prereqs', '-archive', "-archivedirectory=$ArchiveDirectory"
    )
    # AutomationTool clears its own log folder on startup, so dedicate one per run.
    $uatLogDirectory = Join-Path $logDirectory "UAT_$runId"
    Write-PackageMessage "Packaging Win64 Shipping. Engine: $EngineRoot"
    Write-PackageMessage "Project: $buildProject; archive: $ArchiveDirectory; log: $logPath"
    $summary.exit_code = Invoke-UnrealBatchFile -BatchFile $uatBatch -Arguments $arguments -WorkingDirectory $EngineRoot -LogDirectory $uatLogDirectory -OnOutput { param($Line) Write-PackageMessage $Line }
    if ($summary.exit_code -ne 0) { throw "Unreal packaging failed with exit code $($summary.exit_code). See $logPath" }
    if (-not (Test-Path -LiteralPath (Join-Path $ArchiveDirectory 'Windows/HeliosOutpost.exe'))) {
        throw "AutomationTool completed but the expected archived game is missing: $ArchiveDirectory/Windows/HeliosOutpost.exe"
    }
    $summary.status = 'succeeded'
}
catch {
    $failure = $_
    $summary.error = $_.Exception.Message
    Write-PackageMessage "ERROR: $($summary.error)"
}
finally {
    $summary.total_seconds = [math]::Round($timer.Elapsed.TotalSeconds, 3)
    try {
        [IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        Write-PackageMessage "Package $($summary.status). Summary: $summaryPath"
    }
    finally {
        $writer.Dispose()
        if ($null -ne $lockStream) { $lockStream.Dispose() }
    }
}
if ($null -ne $failure) { throw $failure }

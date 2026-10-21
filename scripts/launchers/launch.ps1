[CmdletBinding()]
param(
    [ValidateSet('Game','Editor')][string]$Mode = 'Game',
    [ValidateSet('HeliosAdventure','IkunTown','IkunGhostVillage','UrbanChallenge','CFMBioPyramid','CFMBioPyramidFloating','HeliosOutpost','HeliosEnemyTestbed')][string]$MapId = 'HeliosAdventure',
    [ValidateSet('Default','Calamity','Ultimatum','YunSummerMegatron','Fighter','SwordDog')][string]$Preset = 'Default',
    [string]$EngineRoot = '',
    [switch]$Headless, [switch]$Offscreen, [switch]$Wait, [switch]$DryRun,
    [switch]$EnableMCP, [switch]$Diagnostics, [switch]$QA,
    [switch]$CombatSettingsQA, [switch]$CombatSettingsTravel,
    [string]$ExpectedCombat = '',
    [switch]$UrbanChallengeQA, [switch]$UrbanCombatQA, [switch]$UrbanFullRunQA,
    [ValidatePattern('^[a-z0-9_]+$')][string]$UrbanWeapon = 'cfm_chan_zeus',
    [ValidateSet('d3d11','d3d12')][string]$GraphicsApi = 'd3d12',
    [ValidateRange(640,7680)][int]$Width = 1920,
    [ValidateRange(480,4320)][int]$Height = 1080,
    [string[]]$AdditionalArguments = @()
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../maintenance/environment.ps1')
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project = Join-Path $projectRoot 'unreal/HeliosOutpost.uproject'
$mapFile = Join-Path $projectRoot ('unreal/Content/Helios/Maps/' + $MapId + '.umap')
$logDirectory = Join-Path $projectRoot 'unreal/Saved/Logs/Launchers'
$runId = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$logBase = $Mode.ToLowerInvariant() + '_' + $MapId + '_' + $runId
$launcherLog = Join-Path $logDirectory ($logBase + '.json')
$engineLog = Join-Path $logDirectory ($logBase + '.log')

function Format-HeliosArgument([string]$Value) {
    if ($Value -match '^(\-[^=\s]+=)(.*)$') {
        $prefix = $Matches[1]; $valuePart = $Matches[2]
        if ($valuePart -match '[\s"]' -and -not ($valuePart.StartsWith('"') -and $valuePart.EndsWith('"'))) { return $prefix + '"' + $valuePart.Replace('"','\"') + '"' }
        return $Value
    }
    if ($Value -match '\s' -and -not ($Value.StartsWith('"') -and $Value.EndsWith('"'))) { return '"' + $Value.Replace('"','\"') + '"' }
    return $Value
}

try {
    if ($Headless -and $Offscreen) { throw 'Choose -Headless or -Offscreen; headless runs cannot verify rendered graphics.' }
    if ($Mode -eq 'Editor' -and $Preset -ne 'Default') { throw 'Weapon presets apply only to -Mode Game.' }
    foreach ($required in @($project,(Join-Path $projectRoot 'scripts/check_resources.ps1'))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw ('Required file is missing: ' + $required) }
    }
    & (Join-Path $projectRoot 'scripts/check_resources.ps1') -ProjectRoot $projectRoot | Out-Null
    if (-not (Test-Path -LiteralPath $mapFile -PathType Leaf)) { throw ('The selected map is missing: ' + $MapId + '. Restore its legally obtained resources according to resources.manifest.json.') }
    $resolvedEngine = Resolve-HeliosEngineRoot -EngineRoot $EngineRoot
    $editor = Join-Path $resolvedEngine 'Engine/Binaries/Win64/UnrealEditor.exe'
    if (-not $PSBoundParameters.ContainsKey('GraphicsApi') -and ($Mode -eq 'Editor' -or $Preset -eq 'Calamity' -or $Preset -eq 'Fighter' -or $MapId -eq 'HeliosEnemyTestbed')) { $GraphicsApi = 'd3d11' }
    if ($Preset -in @('Ultimatum','SwordDog')) {
        if (-not $PSBoundParameters.ContainsKey('Width')) { $Width = 1600 }
        if (-not $PSBoundParameters.ContainsKey('Height')) { $Height = 900 }
    }
    $quality = 'sg.ViewDistanceQuality 2,sg.AntiAliasingQuality 3,sg.ShadowQuality 2,sg.GlobalIlluminationQuality 2,sg.ReflectionQuality 2,sg.PostProcessQuality 2,sg.TextureQuality 3,sg.EffectsQuality 2,sg.FoliageQuality 2,sg.ShadingQuality 2,r.ScreenPercentage 85,t.MaxFPS 60'
    if ($Preset -eq 'Calamity') { $quality = 'sg.ViewDistanceQuality 1,sg.AntiAliasingQuality 2,sg.ShadowQuality 1,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.PostProcessQuality 1,sg.TextureQuality 2,sg.EffectsQuality 2,sg.FoliageQuality 1,sg.ShadingQuality 2,r.ScreenPercentage 100,t.MaxFPS 60' }
    if ($Preset -eq 'Fighter') { $quality = 'r.ScreenPercentage 100,r.VSync 0,t.MaxFPS 60' }
    if ($Preset -in @('Ultimatum','SwordDog')) { $quality = 't.MaxFPS 60' }
    $engineArguments = @($project,('/Game/Helios/Maps/' + $MapId),('-' + $GraphicsApi),'-NoLiveCoding','-nosplash',('-abslog=' + $engineLog))
    # Keep startup independent of machine-wide Zen/cache service health.
    $engineArguments += @('-ddc=InstalledNoZenLocalFallback', ('-LocalDataCachePath=' + (Join-Path $projectRoot 'unreal/DerivedDataCache')))
    if ($Mode -eq 'Game') { $engineArguments += @('-game','-windowed',('-ResX=' + $Width),('-ResY=' + $Height),'-ForceRes','-NoAssetRegistryCache',('-ExecCmds=' + $quality)) }
    switch ($Preset) {
        'Calamity' { $engineArguments += '-CalamityDefault' }
        'Ultimatum' { $engineArguments += '-HeliosUltimatum' }
        'YunSummerMegatron' { $engineArguments += '-HeliosYunSummerMegatron' }
        'Fighter' { $engineArguments += @('-VXQuickStart','-VXVariant=1080p') }
    }
    if ($EnableMCP -or $Diagnostics -or $QA) { $engineArguments += '-HeliosCodexMCP' }
    if ($Diagnostics) { $engineArguments += '-HeliosMCPDiagnostics' }
    if ($QA) {
        $engineArguments += @('-HeliosMCPQA','-HeliosAdventureSeed=9182026')
        if ($MapId -eq 'IkunTown') { $engineArguments += @('-HeliosTownQA','-HeliosTownSkipSmoke') }
        if ($MapId -eq 'IkunGhostVillage') { $engineArguments += @('-HeliosVillageQA','-HeliosTownQA','-HeliosTownSkipSmoke') }
    }
    if ($CombatSettingsQA) {
        if ($projectRoot.Replace('\','/') -notlike '*/qa/combat_settings_repair/*/host' -or -not (Test-Path -LiteralPath $ExpectedCombat -PathType Leaf)) { throw 'Combat settings QA requires an isolated host and independent expected values.' }
        $engineArguments += @('-HeliosQingxiaoQA','-QXSettings','-HeliosNoPet','-unattended','-nosound',('-HeliosExpectedCombat=' + $ExpectedCombat))
        if ($CombatSettingsTravel) { $engineArguments += '-QXSettingsTravel' }
    } elseif ($CombatSettingsTravel) { throw '-CombatSettingsTravel requires -CombatSettingsQA.' }
    if ($UrbanChallengeQA -or $UrbanCombatQA -or $UrbanFullRunQA) {
        if ($MapId -ne 'UrbanChallenge') { throw 'Urban QA requires -MapId UrbanChallenge.' }
        $engineArguments += @('-HeliosUrbanQA','-RenderOffscreen','-unattended','-nosound','-trace=none')
        if ($UrbanCombatQA) { $engineArguments += '-UrbanCombatQA' }
        if ($UrbanFullRunQA) { $engineArguments += @('-UrbanFullRunQA',('-UrbanWeapon=' + $UrbanWeapon)) }
    }
    if ($Headless) { $engineArguments += @('-nullrhi','-nosound','-unattended') }
    if ($Offscreen) { $engineArguments += @('-RenderOffscreen','-unattended') }
    $engineArguments += $AdditionalArguments
    $formattedArguments = @($engineArguments | ForEach-Object { Format-HeliosArgument $_ })
    $launch = [ordered]@{
        Mode = $Mode; MapId = $MapId; Preset = $Preset; EngineRoot = $resolvedEngine
        Executable = $editor; Arguments = $engineArguments; CommandLine = ($formattedArguments -join ' ')
        WorkingDirectory = (Split-Path -Parent $project); EngineLog = $engineLog; StartedUTC = (Get-Date).ToUniversalTime().ToString('o')
        ProcessId = $null; DryRun = [bool]$DryRun
    }
    if ($DryRun) { [pscustomobject]$launch; return }
    New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
    $launch | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $launcherLog -Encoding UTF8
    $windowStyle = if ($Headless -or $Offscreen -or $UrbanChallengeQA -or $UrbanCombatQA -or $UrbanFullRunQA -or $engineArguments -contains '-RenderOffscreen' -or $engineArguments -contains '-nullrhi') { 'Hidden' } else { 'Normal' }
    $process = Start-Process -FilePath $editor -ArgumentList $formattedArguments -WorkingDirectory $launch.WorkingDirectory -WindowStyle $windowStyle -PassThru
    $null = $process.Handle # Keep ExitCode available after WaitForExit on Windows PowerShell 5.1.
    $launch.ProcessId = $process.Id
    $launch | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $launcherLog -Encoding UTF8
    Write-Host ('Started {0}: PID {1}; log: {2}' -f $MapId,$process.Id,$engineLog)
    if ($Wait) {
        $process.WaitForExit()
        $launch['ExitCode'] = $process.ExitCode
        $launch | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $launcherLog -Encoding UTF8
        if ($process.ExitCode -ne 0) { throw ('Unreal exited with code ' + $process.ExitCode + '; see ' + $engineLog) }
    } elseif ($process.WaitForExit(10000) -and $process.ExitCode -ne 0) {
        throw ('Unreal exited during startup with code ' + $process.ExitCode + '; see ' + $engineLog)
    }
    [pscustomobject]$launch
} catch {
    if (-not $DryRun) {
        New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
        $_ | Out-String | Set-Content -LiteralPath (Join-Path $logDirectory ($logBase + '_error.log')) -Encoding UTF8
    }
    throw
}

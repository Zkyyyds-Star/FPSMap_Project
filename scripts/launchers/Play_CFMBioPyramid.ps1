# Compatibility preset; the shared launcher owns resource checks and engine discovery.
[CmdletBinding()]
param(
    [Alias('Map')][ValidateSet('HeliosAdventure','IkunTown','IkunGhostVillage','UrbanChallenge','CFMBioPyramid','CFMBioPyramidFloating','HeliosOutpost','HeliosEnemyTestbed')][string]$MapId = 'CFMBioPyramidFloating',
    [string]$EngineRoot = '',
    [switch]$Headless, [switch]$Offscreen, [switch]$Wait, [switch]$DryRun,
    [switch]$EnableMCP, [switch]$Codex, [switch]$Diagnostics, [switch]$QA,
    [switch]$CombatSettingsQA, [switch]$CombatSettingsTravel, [string]$ExpectedCombat = '',
    [switch]$UrbanChallengeQA, [switch]$UrbanCombatQA, [switch]$UrbanFullRunQA,
    [ValidatePattern('^[a-z0-9_]+$')][string]$UrbanWeapon = 'cfm_chan_zeus',
    [ValidateSet('d3d11','d3d12')][string]$GraphicsApi = 'd3d12',
    [ValidateRange(640,7680)][int]$Width = 1920,
    [ValidateRange(480,4320)][int]$Height = 1080,
    [string[]]$AdditionalArguments = @()
)
$ErrorActionPreference = 'Stop'
$launchParameters = @{}
foreach ($key in $PSBoundParameters.Keys) { if ($key -ne 'Codex') { $launchParameters[$key] = $PSBoundParameters[$key] } }
if (-not $launchParameters.ContainsKey('MapId')) { $launchParameters.MapId = $MapId }
$launchParameters.Mode = 'Game'
$launchParameters.Preset = 'Default'
if ($Codex) { $launchParameters.EnableMCP = $true }
if (-not $QA) {
    $userDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../unreal/Saved/BioPyramidFloatingUser')).Replace('\','/') + '/'
    $launchParameters.AdditionalArguments = @($AdditionalArguments) + @('-SaveToUserDir',('-UserDir=' + $userDirectory))
}
if ($QA) {
    $launchParameters.Remove('QA')
    $qaOutput = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('../../unreal/Saved/Automation/BioHunterChoice_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))))
    $launchParameters.AdditionalArguments = @($AdditionalArguments) + @('-SaveToUserDir',('-UserDir=' + $qaOutput.Replace('\','/') + '/User/'),'-HeliosBioHotelQA','-HeliosBioHunterChoiceQA','-HeliosNoPet',('-BioHotelQAOut=' + $qaOutput),('-BioExpectedMap=/Game/Helios/Maps/' + $MapId))
}
& (Join-Path $PSScriptRoot 'launch.ps1') @launchParameters

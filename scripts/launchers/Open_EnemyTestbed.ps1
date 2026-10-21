# Compatibility preset; the shared launcher owns resource checks and engine discovery.
[CmdletBinding()]
param(
    [Alias('Map')][ValidateSet('HeliosAdventure','IkunTown','IkunGhostVillage','UrbanChallenge','CFMBioPyramid','CFMBioPyramidFloating','HeliosOutpost','HeliosEnemyTestbed')][string]$MapId = 'HeliosEnemyTestbed',
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
$launchParameters.Mode = 'Editor'
$launchParameters.Preset = 'Default'
if ($Codex) { $launchParameters.EnableMCP = $true }

& (Join-Path $PSScriptRoot 'launch.ps1') @launchParameters

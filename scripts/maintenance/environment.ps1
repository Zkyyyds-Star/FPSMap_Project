# Shared, read-only Unreal Engine discovery. No registry, drive or environment changes.
function Get-HeliosCanonicalDirectory([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path).TrimEnd('\','/')
    for ($depth = 0; $depth -lt 8; $depth++) {
        $item = Get-Item -LiteralPath $resolved -ErrorAction SilentlyContinue
        if (-not $item -or -not $item.LinkType -or -not $item.Target) { break }
        $target = [string]@($item.Target)[0]
        if (-not [IO.Path]::IsPathRooted($target)) { $target = Join-Path $item.Parent.FullName $target }
        $resolved = [IO.Path]::GetFullPath($target).TrimEnd('\','/')
    }
    return $resolved
}

function Test-HeliosEngineRoot([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    $versionPath = Join-Path $Path 'Engine/Build/Build.version'
    $editorPath = Join-Path $Path 'Engine/Binaries/Win64/UnrealEditor.exe'
    if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf) -or -not (Test-Path -LiteralPath $editorPath -PathType Leaf)) { return $false }
    try {
        $version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
        return ([int]$version.MajorVersion -eq 5 -and [int]$version.MinorVersion -eq 8)
    } catch { return $false }
}

function Resolve-HeliosEngineRoot([string]$EngineRoot = '') {
    $registered = [Collections.Generic.List[string]]::new()
    $preferred = ''
    $projectFile = Join-Path $PSScriptRoot '../../unreal/HeliosOutpost.uproject'
    $association = ''
    if (Test-Path -LiteralPath $projectFile -PathType Leaf) {
        $association = [string](Get-Content -LiteralPath $projectFile -Raw | ConvertFrom-Json).EngineAssociation
    }
    $builds = Get-ItemProperty -LiteralPath 'HKCU:\Software\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
    if ($builds) {
        foreach ($property in $builds.PSObject.Properties) {
            if ($property.Name -like 'PS*' -or $property.Value -isnot [string]) { continue }
            $registered.Add([string]$property.Value)
            if ($property.Name -eq $association) { $preferred = [string]$property.Value }
        }
    }
    foreach ($registryBase in @('HKLM:\SOFTWARE\EpicGames\Unreal Engine','HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine')) {
        foreach ($key in @(Get-ChildItem -LiteralPath $registryBase -ErrorAction SilentlyContinue)) {
            $installation = Get-ItemProperty -LiteralPath $key.PSPath -ErrorAction SilentlyContinue
            if ($installation.InstalledDirectory) { $registered.Add([string]$installation.InstalledDirectory) }
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($EngineRoot)) {
        $candidates = @($EngineRoot)
    } elseif (-not [string]::IsNullOrWhiteSpace($env:UE_ENGINE_ROOT)) {
        $candidates = @($env:UE_ENGINE_ROOT)
    } else {
        $candidates = @($preferred) + @($registered)
    }
    foreach ($candidate in $candidates) {
        if (-not (Test-HeliosEngineRoot $candidate)) { continue }
        $selected = [IO.Path]::GetFullPath($candidate).TrimEnd('\','/')
        $canonical = Get-HeliosCanonicalDirectory $selected
        # Existing registered ASCII aliases avoid toolchain issues with Unicode paths.
        foreach ($alias in @($registered | Sort-Object Length)) {
            if ($alias -match '^[\x00-\x7F]+$' -and (Test-HeliosEngineRoot $alias) -and (Get-HeliosCanonicalDirectory $alias) -eq $canonical) {
                return [IO.Path]::GetFullPath($alias).TrimEnd('\','/')
            }
        }
        return $selected
    }
    if ($EngineRoot) { throw 'The explicit -EngineRoot must contain Unreal Engine 5.8 and UnrealEditor.exe.' }
    if ($env:UE_ENGINE_ROOT) { throw 'UE_ENGINE_ROOT must contain Unreal Engine 5.8 and UnrealEditor.exe.' }
    throw 'Unreal Engine 5.8 was not found in registered installations. Set UE_ENGINE_ROOT or pass -EngineRoot.'
}


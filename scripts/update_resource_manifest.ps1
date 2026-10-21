[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$ManifestPath = ''
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw 'ProjectRoot must be an existing project directory.' }
if ([string]::IsNullOrWhiteSpace($ManifestPath)) { $ManifestPath = Join-Path $root 'resources.manifest.json' }
$content = Join-Path $root 'unreal/Content'
if (-not (Test-Path -LiteralPath $content -PathType Container)) { throw 'Unreal Content is missing. Restore resources before regenerating the manifest; do not replace a missing-resource manifest with an empty one.' }
$requiredInputs = @(
    'exports/layout.json',
    'exports/cfm_biopyramid_floating/water/water_visual_fixture.json',
    'qa/cfm_bio_box_weapons/stun_controller_source_audit.json'
)
$paths = [Collections.Generic.List[string]]::new()
Get-ChildItem -LiteralPath $content -Recurse -File -Force | Where-Object {
    $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and $_.Extension -notin @('.pyc', '.pyo')
} | ForEach-Object { $paths.Add($_.FullName.Substring($root.Length + 1).Replace('\', '/')) }
if ($paths.Count -eq 0) { throw 'Unreal Content is empty. Restore resources before regenerating the manifest.' }
foreach ($relative in $requiredInputs) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) { throw "Required local input is missing: $relative. Restore it before updating the manifest." }
    $paths.Add($relative)
}
$sortedPaths = $paths.ToArray()
[Array]::Sort($sortedPaths, [StringComparer]::Ordinal)
$files = [Collections.Generic.List[object]]::new()
$totalBytes = [long]0
foreach ($relative in $sortedPaths) {
    $path = Join-Path $root $relative
    $item = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $files.Add([ordered]@{
        path = $relative
        bytes = [long]$item.Length
        sha256 = $hash
        license_status = 'unconfirmed'
        distribution = 'local_only'
    })
    $totalBytes += $item.Length
}
$manifest = [ordered]@{
    schema_version = 1
    policy = 'Local resources are excluded from the public source repository. Obtain legal copies and restore their relative paths; a manifest does not grant redistribution rights.'
    hash_algorithm = 'SHA256'
    files = $files.ToArray()
}
$json = ($manifest | ConvertTo-Json -Depth 6) -replace "`r`n", "`n"
$fullManifest = [IO.Path]::GetFullPath($ManifestPath)
$temporary = $fullManifest + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
try {
    [IO.File]::WriteAllText($temporary, $json + "`n", [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $fullManifest -Force
}
finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
}
[pscustomobject]@{ status = 'updated'; files = $files.Count; bytes = $totalBytes; sha256 = (Get-FileHash -LiteralPath $fullManifest -Algorithm SHA256).Hash.ToLowerInvariant() }

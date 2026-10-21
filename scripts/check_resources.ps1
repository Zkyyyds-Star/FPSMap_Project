[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$ManifestPath = '',
    [switch]$VerifyHash
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw 'ProjectRoot must be an existing project directory.' }
if ([string]::IsNullOrWhiteSpace($ManifestPath)) { $ManifestPath = Join-Path $root 'resources.manifest.json' }
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw 'Resource manifest is missing. Restore resources.manifest.json from the source repository. A complete game also requires legally obtained local resources.'
}
$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schema_version -ne 1 -or $manifest.hash_algorithm -ne 'SHA256' -or @($manifest.files).Count -eq 0) { throw 'Unsupported or empty resource manifest.' }
$requiredInputs = @(
    'exports/layout.json',
    'exports/cfm_biopyramid_floating/water/water_visual_fixture.json',
    'qa/cfm_bio_box_weapons/stun_controller_source_audit.json'
)
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$failures = [Collections.Generic.List[object]]::new()
$totalBytes = [long]0
$contentCount = 0
foreach ($entry in $manifest.files) {
    $relative = [string]$entry.path
    if ([string]::IsNullOrWhiteSpace($relative) -or [IO.Path]::IsPathRooted($relative) -or
        $relative.Contains('\') -or $relative.Contains(':') -or $relative -match '(^|/)\.{1,2}(/|$)' -or
        $relative -match '[\x00-\x1f]' -or -not $seen.Add($relative)) {
        throw "Invalid or duplicate manifest path: $relative"
    }
    if (-not ($relative.StartsWith('unreal/Content/', [StringComparison]::Ordinal) -or $relative -in $requiredInputs)) {
        throw "Unexpected resource scope in manifest: $relative"
    }
    $size = [long]0
    if (-not [long]::TryParse([string]$entry.bytes, [ref]$size) -or $size -lt 0 -or [string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw "Invalid size or SHA256 in manifest: $relative"
    }
    if ($relative.StartsWith('unreal/Content/', [StringComparison]::Ordinal)) { $contentCount++ }
    $totalBytes += $size
    $path = Join-Path $root ($relative.Replace('/', [IO.Path]::DirectorySeparatorChar))
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $failures.Add([pscustomobject]@{ path = $relative; reason = 'missing' })
        continue
    }
    $item = Get-Item -LiteralPath $path
    if ($item.Length -ne $size) {
        $failures.Add([pscustomobject]@{ path = $relative; reason = "size mismatch: expected $size, actual $($item.Length)" })
        continue
    }
    if ($VerifyHash -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
        $failures.Add([pscustomobject]@{ path = $relative; reason = 'SHA256 mismatch' })
    }
}
if ($contentCount -eq 0) { throw 'Resource manifest contains no Unreal Content files.' }
foreach ($relative in $requiredInputs) {
    if (-not $seen.Contains($relative)) { throw "Resource manifest omits required local input: $relative" }
}
if ($failures.Count -gt 0) {
    $shown = @($failures | Select-Object -First 30 | ForEach-Object { '  {0}: {1}' -f $_.path, $_.reason })
    $message = "Resource check failed: $($failures.Count) missing or changed files.`n" + ($shown -join "`n")
    if ($failures.Count -gt 30) { $message += "`n  ... $($failures.Count - 30) more. Full entries are available in the error TargetObject.failures." }
    $message += "`nRestore legally obtained resources to the paths in resources.manifest.json. The public source repository alone cannot run the complete game; this tool does not download or extract resources."
    $detail = [pscustomobject]@{ status = 'failed'; failures = $failures.ToArray(); checked_files = $seen.Count }
    $record = [Management.Automation.ErrorRecord]::new([IO.InvalidDataException]::new($message), 'LocalResourcesInvalid', [Management.Automation.ErrorCategory]::InvalidData, $detail)
    $PSCmdlet.ThrowTerminatingError($record)
}
[pscustomobject]@{ status = 'passed'; files = $seen.Count; bytes = $totalBytes; verified_hash = [bool]$VerifyHash }

# Shared helpers for the existing build entrypoint. Reuse a matching ASCII drive alias.
if (-not ('HeliosBuildDrive' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class HeliosBuildDrive {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern uint QueryDosDevice(string name, StringBuilder target, int length);
}
'@
}
function Get-BuildCanonicalPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $drive = [IO.Path]::GetPathRoot($full).TrimEnd('\')
    $buffer = [Text.StringBuilder]::new(4096)
    if ([HeliosBuildDrive]::QueryDosDevice($drive, $buffer, $buffer.Capacity) -gt 0) {
        $target = $buffer.ToString()
        if ($target.StartsWith('\??\')) {
            $suffix = if ($full.Length -gt 3) { $full.Substring(3) } else { '' }
            $resolved = if ($suffix) { Join-Path $target.Substring(4) $suffix } else { $target.Substring(4) }
            $full = [IO.Path]::GetFullPath($resolved).TrimEnd('\')
        }
    }
    return $full
}
function Resolve-BuildProjectDrive([string]$ProjectRoot) {
    $canonical = Get-BuildCanonicalPath $ProjectRoot
    foreach ($letter in @('R','Q','P','Z','Y')) {
        $candidate = $letter + ':\'
        if (Test-Path -LiteralPath $candidate) {
            if ((Get-BuildCanonicalPath $candidate) -eq $canonical) {
                return Join-Path $candidate 'unreal\HeliosOutpost.uproject'
            }
            continue
        }
        & subst ($letter + ':') $canonical
        if ($LASTEXITCODE -ne 0) { continue }
        return Join-Path $candidate 'unreal\HeliosOutpost.uproject'
    }
    throw 'No free ASCII drive alias is available for this Unreal project.'
}
function Get-BuildOutputEncoding { return [Text.UTF8Encoding]::new($false) }
function Invoke-UnrealBatchFile {
    param(
        [Parameter(Mandatory)][string]$BatchFile,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [Parameter(Mandatory)][string]$LogDirectory,
        [Parameter(Mandatory)][scriptblock]$OnOutput
    )
    # cmd.exe expands percent signs even inside quoted arguments.
    foreach ($argument in @($BatchFile) + $Arguments) {
        if ($argument -match '["%\r\n]') { throw 'Unreal paths/arguments cannot contain quotes, percent signs or line breaks.' }
    }
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $env:ComSpec
    $startInfo.Arguments = '/d /v:off /s /c "' + ((@($BatchFile) + $Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ') + '"'
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = Get-BuildOutputEncoding
    $startInfo.StandardErrorEncoding = $startInfo.StandardOutputEncoding
    $startInfo.EnvironmentVariables['uebp_LogFolder'] = $LogDirectory
    $startInfo.EnvironmentVariables['uebp_FinalLogFolder'] = $LogDirectory
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        [void]$process.Start()
        $stdoutTask = $process.StandardOutput.ReadLineAsync()
        $stderrTask = $process.StandardError.ReadLineAsync()
        while ($null -ne $stdoutTask -or $null -ne $stderrTask -or -not $process.HasExited) {
            if ($null -ne $stdoutTask -and $stdoutTask.IsCompleted) {
                $line = $stdoutTask.GetAwaiter().GetResult()
                if ($null -eq $line) { $stdoutTask = $null }
                else { & $OnOutput $line; $stdoutTask = $process.StandardOutput.ReadLineAsync() }
            }
            if ($null -ne $stderrTask -and $stderrTask.IsCompleted) {
                $line = $stderrTask.GetAwaiter().GetResult()
                if ($null -eq $line) { $stderrTask = $null }
                else { & $OnOutput $line; $stderrTask = $process.StandardError.ReadLineAsync() }
            }
            if (($null -eq $stdoutTask -or -not $stdoutTask.IsCompleted) -and ($null -eq $stderrTask -or -not $stderrTask.IsCompleted)) { Start-Sleep -Milliseconds 50 }
        }
        $process.WaitForExit()
        return $process.ExitCode
    }
    finally { $process.Dispose() }
}
function Get-UnrealBuildObservations([string]$Output) {
    $actions = $null
    if ($Output -match 'run (\d+) action') { $actions = [int]$Matches[1] }
    return @{
        actions = $actions
        compile_steps = ([regex]::Matches($Output, '\[\d+/\d+\] Compile')).Count
        up_to_date = $Output -match 'Target is up to date'
        makefile_reasons = @([regex]::Matches($Output, '(?m)^.*(?:Invalidating makefile|Creating makefile).*$') | ForEach-Object { $_.Value.Trim() })
    }
}

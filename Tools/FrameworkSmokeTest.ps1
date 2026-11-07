# Copyright (c) 2026 DIVIDE-Studio
#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $OutputRoot,
    [ValidateSet('Vulkan', 'OpenGL')]
    [string] $Backend = 'Vulkan',
    [string] $ExpectedGPU = '',
    [ValidatePattern('^$|^[A-Za-z0-9_-][A-Za-z0-9 _-]{0,62}[A-Za-z0-9_-]?$')]
    [string] $Project = '',
    [ValidatePattern('^$|^[A-Za-z0-9_-][A-Za-z0-9 _-]{0,62}[A-Za-z0-9_-]?$')]
    [string] $Scene = '',
    [ValidateRange(0, 32)]
    [int] $BuildJobs = 0,
    [string] $AmdCaptureExe = '',
    [string] $RenderDocInjectExe = '',
    [switch] $ApproveStalledDeviceLoss,
    [switch] $ApproveStalledNonDeviceLoss,
    [switch] $Revalidate,
    [switch] $SkipUnitTests,
    [switch] $PreflightOnly,
    [ValidateRange(120, 7200)]
    [int] $RenderTimeoutSeconds = 120,
    [ValidateRange(0, 1000000)]
    [int] $DiagnosticPresents = 0,
    [ValidateRange(300, 1000000)]
    [int] $MinimumPresents = 300,
    [ValidatePattern('^$|^[A-Z_]{1,32}$')]
    [string] $MaterialDebugView = '',
    [switch] $EditorPaused,
    [switch] $TracePipelines,
    [switch] $PerfMode,
    [switch] $GpuScopeTimings,
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',
    [ValidateRange(0, 2147483647)]
    [int] $MaxAttempts = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT' -or -not [IO.Path]::IsPathRooted($OutputRoot))
{
    throw 'The render smoke runner requires Windows and an absolute output directory.'
}
if (($Backend -eq 'OpenGL' -and $ExpectedGPU -notmatch '^[0-9a-fA-F]{32}$') -or
    ($Backend -eq 'Vulkan' -and $ExpectedGPU))
{
    throw 'OpenGL requires the expected Vulkan GPU UUID; Vulkan selects and records its own GPU.'
}
$ExpectedGPU = $ExpectedGPU.ToLowerInvariant()
if ($Scene -and -not $Project)
{
    throw '-Scene requires -Project.'
}
if ($MaterialDebugView -and $DiagnosticPresents -eq 0)
{
    throw '-MaterialDebugView is restricted to diagnostic runs; full acceptance requires normal rendering.'
}
if ($EditorPaused -and $DiagnosticPresents -eq 0)
{
    throw '-EditorPaused is restricted to diagnostic runs; full acceptance requires Play mode.'
}
if ($TracePipelines -and ($Backend -ne 'Vulkan' -or $DiagnosticPresents -eq 0))
{
    throw '-TracePipelines requires a Vulkan diagnostic run.'
}
if ($PerfMode -and ($DiagnosticPresents -eq 0 -or $TracePipelines -or $RenderDocInjectExe -or $AmdCaptureExe))
{
    throw '-PerfMode requires a diagnostic run without pipeline tracing, RenderDoc or Radeon capture; it disables API validation.'
}
if ($GpuScopeTimings -and -not $PerfMode)
{
    throw '-GpuScopeTimings requires -PerfMode.'
}
if ($Configuration -ne 'Debug' -and -not $PerfMode)
{
    throw '-Configuration Release is restricted to -PerfMode runs; acceptance uses the Debug instrumented build.'
}
if ($DiagnosticPresents -gt 0 -and $PSBoundParameters.ContainsKey('MinimumPresents'))
{
    throw '-MinimumPresents cannot be combined with -DiagnosticPresents.'
}

$script:Repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$script:Root = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if ($script:Root -eq $script:Repo -or $script:Root.StartsWith($script:Repo + '\', [StringComparison]::OrdinalIgnoreCase))
{
    throw 'Keep smoke artifacts outside the source checkout.'
}
if ($script:Root.Contains('"') -or $script:Root.Contains('%') -or $script:Repo.Contains('"') -or $script:Repo.Contains('%'))
{
    throw 'The checkout and output paths cannot contain quotes or percent signs.'
}
[IO.Directory]::CreateDirectory($script:Root) | Out-Null
$script:StatePath = Join-Path $script:Root 'state.json'
$script:Timeline = Join-Path $script:Root 'timeline.log'
$script:ConfigurePreset = 'windows-msvc-editor-instrumentation'
$script:BuildPreset = if ($Configuration -eq 'Release') { 'windows-msvc-release-editor-instrumentation' } else { 'windows-msvc-debug-editor-instrumentation' }
$script:TargetPresents = if ($DiagnosticPresents -gt 0) { $DiagnosticPresents } else { $MinimumPresents }

$lock = [IO.File]::Open((Join-Path $script:Root 'supervisor.lock'), [IO.FileMode]::OpenOrCreate,
                        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
$hashAlgorithm = [Security.Cryptography.SHA256]::Create()
try
{
    $repoHash = [BitConverter]::ToString($hashAlgorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($script:Repo.ToLowerInvariant()))).Replace('-', '').Substring(0, 20)
}
finally { $hashAlgorithm.Dispose() }
try
{
    $globalLock = [IO.File]::Open((Join-Path ([IO.Path]::GetTempPath()) ("divide-vulkan-smoke-$repoHash.lock")),
                                  [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
}
catch
{
    $lock.Dispose()
    throw
}

try
{
    if (-not ('DivideSmoke.Job' -as [type]))
    {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace DivideSmoke
{
    public static class Job
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct BasicLimits
        {
            public long ProcessTime, JobTime;
            public uint Flags;
            public UIntPtr MinimumWorkingSet, MaximumWorkingSet;
            public uint ActiveProcessLimit;
            public UIntPtr Affinity;
            public uint PriorityClass, SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct IoCounters
        {
            public ulong ReadOperations, WriteOperations, OtherOperations;
            public ulong ReadBytes, WriteBytes, OtherBytes;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ExtendedLimits
        {
            public BasicLimits Basic;
            public IoCounters Io;
            public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemory, PeakJobMemory;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateJobObject(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetInformationJobObject(IntPtr job, int type, ref ExtendedLimits limits, uint length);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateJobObject(IntPtr job, uint code);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint access, bool inheritHandle, int processId);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool IsProcessInJob(IntPtr process, IntPtr job, out bool result);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool DebugBreakProcess(IntPtr process);

        public static IntPtr Create()
        {
            IntPtr job = CreateJobObject(IntPtr.Zero, null);
            if (job == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            ExtendedLimits limits = new ExtendedLimits();
            limits.Basic.Flags = 0x00002000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            if (!SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf(typeof(ExtendedLimits))))
            {
                int error = Marshal.GetLastWin32Error();
                CloseHandle(job);
                throw new Win32Exception(error);
            }
            return job;
        }

        public static void Assign(IntPtr job, IntPtr process)
        {
            if (!AssignProcessToJobObject(job, process)) throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        public static void Terminate(IntPtr job)
        {
            if (!TerminateJobObject(job, 1)) throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        public static void Close(IntPtr job)
        {
            if (job != IntPtr.Zero && !CloseHandle(job)) throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        public static bool Contains(IntPtr job, int processId)
        {
            IntPtr process = OpenProcess(0x1000, false, processId);
            if (process == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            try
            {
                bool result;
                if (!IsProcessInJob(process, job, out result)) throw new Win32Exception(Marshal.GetLastWin32Error());
                return result;
            }
            finally { CloseHandle(process); }
        }

        public static void BreakOwned(IntPtr job, int processId)
        {
            IntPtr process = OpenProcess(0x043A, false, processId);
            if (process == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            try
            {
                bool result;
                if (!IsProcessInJob(process, job, out result)) throw new Win32Exception(Marshal.GetLastWin32Error());
                if (!result) throw new InvalidOperationException("The debuggee is not in the owned job.");
                if (!DebugBreakProcess(process)) throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            finally { CloseHandle(process); }
        }
    }
}
'@
    }

    function Write-Timeline([string] $Message)
    {
        $line = '{0:o} {1}' -f [DateTime]::UtcNow, $Message
        Add-Content -LiteralPath $script:Timeline -Value $line -Encoding UTF8
        Write-Host $line
    }

    function Save-State
    {
        $temp = $script:StatePath + '.' + $PID + '.tmp'
        $json = $script:State | ConvertTo-Json -Depth 12
        [IO.File]::WriteAllText($temp, $json, [Text.UTF8Encoding]::new($false))
        if ([IO.File]::Exists($script:StatePath))
        {
            [IO.File]::Replace($temp, $script:StatePath, [System.Management.Automation.Language.NullString]::Value)
        }
        else
        {
            [IO.File]::Move($temp, $script:StatePath)
        }
    }

    function Quote-Argument([string] $Value)
    {
        if ($Value.Contains('"') -or $Value.EndsWith('\'))
        {
            throw 'Unexpected quote or trailing separator in a launcher argument.'
        }
        return '"' + $Value + '"'
    }

    function Start-OwnedProcess([string] $Directory, [string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory)
    {
        [IO.Directory]::CreateDirectory($Directory) | Out-Null
        $configPath = Join-Path $Directory 'launch.json'
        $gatePath = Join-Path $Directory 'launch.gate'
        $bootstrapPath = Join-Path $Directory 'bootstrap.ps1'
        $stdoutPath = Join-Path $Directory 'stdout.log'
        $stderrPath = Join-Path $Directory 'stderr.log'
        if ([IO.File]::Exists($configPath) -or [IO.File]::Exists($gatePath))
        {
            throw "Refusing to overwrite an existing owned-process record: $Directory"
        }
        $configuration = @{
            executable = $Executable
            arguments = @($Arguments)
            workingDirectory = $WorkingDirectory
        } | ConvertTo-Json -Depth 5
        [IO.File]::WriteAllText($configPath, $configuration, [Text.UTF8Encoding]::new($false))
        $bootstrap = @'
param([string] $Configuration, [string] $Gate)
$ErrorActionPreference = 'Stop'
$deadline = [DateTime]::UtcNow.AddSeconds(30)
while (-not [IO.File]::Exists($Gate))
{
    if ([DateTime]::UtcNow -gt $deadline) { exit 125 }
    Start-Sleep -Milliseconds 50
}
$launch = Get-Content -LiteralPath $Configuration -Raw | ConvertFrom-Json
Set-Location -LiteralPath $launch.workingDirectory
$arguments = @($launch.arguments)
# Piping forces PowerShell to wait for GUI-subsystem executables (the renderer without a debugger).
& $launch.executable @arguments | ForEach-Object { [Console]::Out.WriteLine($_) }
if ($null -eq $LASTEXITCODE) { exit 126 }
exit $LASTEXITCODE
'@
        [IO.File]::WriteAllText($bootstrapPath, $bootstrap, [Text.UTF8Encoding]::new($false))
        $powershell = (Get-Command powershell.exe -ErrorAction Stop).Source
        $args = '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
                (Quote-Argument $bootstrapPath) + ' -Configuration ' + (Quote-Argument $configPath) +
                ' -Gate ' + (Quote-Argument $gatePath)

        $job = [DivideSmoke.Job]::Create()
        $process = $null
        try
        {
            $process = Start-Process -FilePath $powershell -ArgumentList $args -WorkingDirectory $WorkingDirectory `
                -NoNewWindow -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
            [DivideSmoke.Job]::Assign($job, $process.Handle)
            [IO.File]::WriteAllText($gatePath, 'owned')
            return [PSCustomObject]@{
                Process = $process
                Job = $job
                StdoutPath = $stdoutPath
                StderrPath = $stderrPath
                StdoutReader = $null
                StderrReader = $null
                Started = [DateTime]::UtcNow
                TrackedProcesses = [Collections.Generic.List[object]]::new()
            }
        }
        catch
        {
            if ($null -ne $process -and -not $process.HasExited)
            {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            }
            [DivideSmoke.Job]::Close($job)
            throw
        }
    }

    function Show-OwnedOutput($Owned)
    {
        foreach ($kind in @('Stdout', 'Stderr'))
        {
            $readerName = $kind + 'Reader'
            $pathName = $kind + 'Path'
            if ($null -eq $Owned.$readerName -and [IO.File]::Exists($Owned.$pathName))
            {
                $stream = [IO.File]::Open($Owned.$pathName, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                                          [IO.FileShare]::ReadWrite)
                $Owned.$readerName = [IO.StreamReader]::new($stream)
            }
            $reader = $Owned.$readerName
            if ($null -ne $reader)
            {
                while ($null -ne ($line = $reader.ReadLine()))
                {
                    Write-Host $line
                }
            }
        }
    }

    function Stop-OwnedProcess($Owned, [bool] $Terminate)
    {
        $errors = [Collections.Generic.List[string]]::new()
        try
        {
            if ($Terminate)
            {
                [DivideSmoke.Job]::Terminate($Owned.Job)
            }
        }
        catch { $errors.Add("Could not terminate the owned job: $($_.Exception.Message)") }
        finally
        {
            try { [DivideSmoke.Job]::Close($Owned.Job) }
            catch { $errors.Add("Could not close the owned job: $($_.Exception.Message)") }
            if ($null -ne $Owned.StdoutReader) { $Owned.StdoutReader.Dispose() }
            if ($null -ne $Owned.StderrReader) { $Owned.StderrReader.Dispose() }
        }
        if (-not $Owned.Process.WaitForExit(5000))
        {
            $errors.Add("Owned launcher $($Owned.Process.Id) did not terminate.")
        }
        for ($i = $Owned.TrackedProcesses.Count - 1; $i -ge 0; $i--)
        {
            $tracked = $Owned.TrackedProcesses[$i]
            try
            {
                if (-not $tracked.Process.WaitForExit(2000))
                {
                    $current = Get-Process -Id $tracked.Process.Id -ErrorAction Stop
                    if ($current.StartTime.ToUniversalTime() -ne $tracked.Started -or
                        -not [string]::Equals($current.Path, $tracked.Path, [StringComparison]::OrdinalIgnoreCase))
                    {
                        throw "Process identity changed for PID $($tracked.Process.Id); refusing to stop it."
                    }
                    Stop-Process -Id $tracked.Process.Id -Force -ErrorAction Stop
                    if (-not $tracked.Process.WaitForExit(5000))
                    {
                        throw "Owned process $($tracked.Process.Id) did not terminate."
                    }
                }
            }
            catch { $errors.Add($_.Exception.Message) }
            finally { $tracked.Process.Dispose() }
        }
        if ($errors.Count -gt 0)
        {
            throw (($errors -join ' ') + ' Refusing further GPU work.')
        }
    }

    function Wait-OwnedProcess($Owned, [int] $TimeoutSeconds)
    {
        $timedOut = $false
        try
        {
            while (-not $Owned.Process.HasExited)
            {
                Show-OwnedOutput $Owned
                if (([DateTime]::UtcNow - $Owned.Started).TotalSeconds -ge $TimeoutSeconds)
                {
                    $timedOut = $true
                    break
                }
                Start-Sleep -Milliseconds 500
            }
            if (-not $timedOut)
            {
                $Owned.Process.WaitForExit()
                Show-OwnedOutput $Owned
                $exitCode = $Owned.Process.ExitCode
            }
            else
            {
                $exitCode = 124
            }
        }
        finally
        {
            Stop-OwnedProcess $Owned $timedOut
        }
        return [PSCustomObject]@{ ExitCode = $exitCode; TimedOut = $timedOut }
    }

    function Start-RenderDocInjection([string] $Attempt, [int] $RendererPid)
    {
        $directory = Join-Path $Attempt 'renderdoc'
        $inject = Start-OwnedProcess (Join-Path $directory 'inject') $RenderDocInjectExe `
            @('inject', "--PID=$RendererPid", '-c', (Join-Path $directory 'capture')) $Attempt
        $result = Wait-OwnedProcess $inject 10
        $output = if ([IO.File]::Exists($inject.StderrPath)) { [IO.File]::ReadAllText($inject.StderrPath) } else { '' }
        $match = [regex]::Match($output, '(?im)^\s*Launched as ID (\d+)\s*$')
        if ($result.TimedOut -or -not $match.Success -or $result.ExitCode -ne [int]$match.Groups[1].Value)
        {
            throw 'RenderDoc injection did not report a matching target identifier.'
        }

        $target = @{ pid = $RendererPid; ident = $result.ExitCode } | ConvertTo-Json
        $targetPath = Join-Path $directory 'target.json'
        $temporaryPath = $targetPath + '.tmp'
        [IO.File]::WriteAllText($temporaryPath, $target, [Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temporaryPath, $targetPath)
        Write-Timeline "RenderDoc was injected into verified renderer PID $RendererPid as target $($result.ExitCode)."
    }

    function Start-AmdCrashCapture([string] $Attempt, [string] $RendererName)
    {
        $directory = Join-Path $Attempt 'amd-capture'
        $dump = Join-Path $Attempt 'amd-crash.rgd'
        $rgd = Join-Path (Split-Path -Parent $AmdCaptureExe) 'rgd.exe'
        $arguments = @('--mode', 'crash', '--process', $RendererName, '--output', $dump,
                       '--rgd-enhanced', '--rgd-collect-sgprs', '--rgd-marker-source', '--rgd-expand-markers',
                       '--rgd-text-summary', '--rgd-json-summary', '--rgd-cli-path', $rgd, '--verbose')
        $capture = Start-OwnedProcess $directory $AmdCaptureExe $arguments $Attempt
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        try
        {
            while (-not $capture.Process.HasExited -and [DateTime]::UtcNow -lt $deadline)
            {
                Show-OwnedOutput $capture
                if ([IO.File]::Exists($capture.StdoutPath))
                {
                    $stream = [IO.File]::Open($capture.StdoutPath, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                                              [IO.FileShare]::ReadWrite)
                    try
                    {
                        $reader = [IO.StreamReader]::new($stream)
                        try { $output = $reader.ReadToEnd() }
                        finally { $reader.Dispose() }
                    }
                    finally { $stream.Dispose() }
                    if ($output -match 'Capture API initialized successfully')
                    {
                        Write-Timeline 'Radeon crash capture is ready for this attempt.'
                        return $capture
                    }
                }
                Start-Sleep -Milliseconds 250
            }
            throw 'Radeon crash capture did not initialize; no renderer was started.'
        }
        catch
        {
            Stop-OwnedProcess $capture (-not $capture.Process.HasExited)
            throw
        }
    }

    function Stop-AmdCrashCapture($Capture, [string] $Attempt)
    {
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        try
        {
            while (-not $Capture.Process.HasExited -and [DateTime]::UtcNow -lt $deadline)
            {
                Show-OwnedOutput $Capture
                Start-Sleep -Milliseconds 250
            }
            $timedOut = -not $Capture.Process.HasExited
            if ($timedOut)
            {
                $exitCode = 124
                Write-Timeline 'Radeon capture did not exit within the report window; stopping its owned process tree.'
            }
            else
            {
                $Capture.Process.WaitForExit()
                Show-OwnedOutput $Capture
                $exitCode = $Capture.Process.ExitCode
            }
        }
        finally
        {
            Stop-OwnedProcess $Capture (-not $Capture.Process.HasExited)
        }
        $dump = Join-Path $Attempt 'amd-crash.rgd'
        $hasDump = [IO.File]::Exists($dump) -and (Get-Item -LiteralPath $dump).Length -gt 0
        if ($hasDump) { Write-Timeline 'Radeon crash evidence was preserved for this attempt.' }
        elseif ($timedOut -or $exitCode -ne 0) { Write-Timeline 'Radeon crash capture ended without a confirmed report.' }
        return [PSCustomObject]@{ ExitCode = $exitCode; TimedOut = $timedOut; HasDump = $hasDump }
    }

    function Get-BinaryDirectory
    {
        $presets = Get-Content -LiteralPath (Join-Path $script:Repo 'CMakePresets.json') -Raw | ConvertFrom-Json
        $byName = @{}
        foreach ($preset in $presets.configurePresets) { $byName[$preset.name] = $preset }
        function Find-BinaryDirectory([string] $Name, [int] $Depth)
        {
            if ($Depth -gt 20 -or -not $byName.ContainsKey($Name)) { throw "Invalid preset inheritance: $Name" }
            $preset = $byName[$Name]
            $property = $preset.PSObject.Properties['binaryDir']
            if ($null -ne $property) { return [string]$property.Value }
            $parents = $preset.PSObject.Properties['inherits']
            if ($null -ne $parents)
            {
                foreach ($parent in @($parents.Value))
                {
                    $result = Find-BinaryDirectory $parent ($Depth + 1)
                    if ($result) { return $result }
                }
            }
            return $null
        }
        $path = Find-BinaryDirectory $script:ConfigurePreset 0
        if (-not $path) { throw 'The configured preset has no binary directory.' }
        $path = $path.Replace('${sourceDir}', $script:Repo).Replace('${presetName}', $script:ConfigurePreset).Replace('/', '\')
        return [IO.Path]::GetFullPath($path)
    }

    function Test-InstrumentedConfiguration([string] $BuildDirectory)
    {
        $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
        $ninjaPath = Join-Path $BuildDirectory 'build.ninja'
        if (-not [IO.File]::Exists($cachePath) -or -not [IO.File]::Exists($ninjaPath)) { return $false }

        $cache = @{}
        foreach ($line in [IO.File]::ReadLines($cachePath))
        {
            if ($line -match '^([^#/:]+):[^=]*=(.*)$') { $cache[$matches[1]] = $matches[2] }
        }
        foreach ($name in @('BUILD_TESTING_INTERNAL', 'ENABLE_OPTICK_PROFILER', 'ENABLE_FUNCTION_PROFILING', 'WINDOWS_OS_BUILD'))
        {
            if (-not $cache.ContainsKey($name) -or $cache[$name] -notin @('ON', 'TRUE', '1')) { return $false }
        }
        if ($cache['RUN_TESTING_INTERNAL'] -notin @('OFF', 'FALSE', '0') -or
            $cache['DIVIDE_STARTUP_TYPE'] -ne 'IN_EDITOR' -or
            $cache['CMAKE_GENERATOR'] -ne 'Ninja Multi-Config' -or
            $cache['CMAKE_CXX_COMPILER'] -notmatch '(^|[\\/])cl(\.exe)?$' -or
            -not $cache['CMAKE_HOME_DIRECTORY']) { return $false }
        if ([IO.Path]::GetFullPath($cache['CMAKE_HOME_DIRECTORY'].Replace('/', '\')).TrimEnd('\') -ne $script:Repo)
        {
            return $false
        }

        $generated = [IO.File]::GetLastWriteTimeUtc($ninjaPath)
        foreach ($relative in @('CMakePresets.json', 'CMakeUserPresets.json', 'vcpkg.json', 'vcpkg-configuration.json'))
        {
            $path = Join-Path $script:Repo $relative
            if ([IO.File]::Exists($path) -and [IO.File]::GetLastWriteTimeUtc($path) -gt $generated) { return $false }
        }
        $overlays = Join-Path $script:Repo 'Source\CMakeHelpers\OverlayPorts'
        if ([IO.Directory]::Exists($overlays))
        {
            foreach ($file in Get-ChildItem -LiteralPath $overlays -Force -Recurse)
            {
                if ($file.LastWriteTimeUtc -gt $generated) { return $false }
            }
        }
        return $true
    }

    function Get-TargetMetadata([string] $BuildDirectory, [string] $Name)
    {
        $reply = Join-Path $BuildDirectory '.cmake\api\v1\reply'
        $index = Get-ChildItem -LiteralPath $reply -Filter 'index-*.json' -File |
                 Sort-Object LastWriteTimeUtc | Select-Object -Last 1
        if ($null -eq $index) { throw 'CMake did not produce File API metadata.' }
        $indexData = Get-Content -LiteralPath $index.FullName -Raw | ConvertFrom-Json
        $codemodel = @($indexData.objects | Where-Object { $_.kind -eq 'codemodel' } | Select-Object -First 1)
        if ($codemodel.Count -ne 1) { throw 'CMake codemodel is missing.' }
        $model = Get-Content -LiteralPath (Join-Path $reply $codemodel[0].jsonFile) -Raw | ConvertFrom-Json
        $config = @($model.configurations | Where-Object { $_.name -eq $Configuration })
        if ($config.Count -ne 1) { throw "CMake $Configuration configuration is missing or ambiguous." }
        $target = @($config[0].targets | Where-Object { $_.name -eq $Name })
        if ($target.Count -ne 1) { throw "CMake target is missing or ambiguous: $Name" }
        $data = Get-Content -LiteralPath (Join-Path $reply $target[0].jsonFile) -Raw | ConvertFrom-Json
        $artifacts = @($data.artifacts | Where-Object { $_.path -match '\.exe$' })
        if ($artifacts.Count -ne 1) { throw "CMake did not identify one executable for $Name." }
        $exe = [IO.Path]::GetFullPath((Join-Path $BuildDirectory $artifacts[0].path.Replace('/', '\')))
        $source = [IO.Path]::GetFullPath($model.paths.source.Replace('/', '\')).TrimEnd('\')
        if ($source -ne $script:Repo -or -not [IO.File]::Exists($exe))
        {
            throw "CMake target or source directory is invalid: $Name"
        }
        return [PSCustomObject]@{ Executable = $exe; WorkingDirectory = $source }
    }

    function Get-VisualStudioEnvironment
    {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not [IO.File]::Exists($vswhere)) { throw 'Visual Studio discovery tool is unavailable.' }
        $installation = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'An x64 MSVC installation was not found.' }
        $vcvars = Join-Path ($installation | Select-Object -First 1) 'VC\Auxiliary\Build\vcvars64.bat'
        if (-not [IO.File]::Exists($vcvars)) { throw 'The x64 MSVC developer environment is unavailable.' }
        return $vcvars
    }

    function Get-Debugger
    {
        $package = Get-AppxPackage -Name 'Microsoft.WinDbg' -ErrorAction SilentlyContinue |
                   Sort-Object Version -Descending | Select-Object -First 1
        if ($null -eq $package) { return $null }
        $debugger = Join-Path $package.InstallLocation 'amd64\cdb.exe'
        if (-not [IO.File]::Exists($debugger)) { return $null }
        return $debugger
    }

    function Copy-DebuggerRuntime([string] $Debugger, [string] $Attempt)
    {
        if (-not $Debugger) { return $null }

        $source = Split-Path -Parent $Debugger
        $destination = Join-Path $Attempt 'debugger-runtime'
        if ([IO.Directory]::Exists($destination) -or [IO.File]::Exists($destination))
        {
            throw 'Refusing to overwrite an existing debugger runtime.'
        }
        [IO.Directory]::CreateDirectory($destination) | Out-Null
        $files = @((Get-Item -LiteralPath $Debugger))
        $files += @(Get-ChildItem -LiteralPath $source -Filter '*.dll' -File)
        $notice = Join-Path $source 'NOTICE.txt'
        if ([IO.File]::Exists($notice)) { $files += Get-Item -LiteralPath $notice }
        foreach ($file in $files)
        {
            $copy = Join-Path $destination $file.Name
            Copy-Item -LiteralPath $file.FullName -Destination $copy
            if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash)
            {
                throw "Debugger runtime copy verification failed: $($file.Name)"
            }
        }
        Write-Timeline 'Copied the installed CDB runtime locally so its process tree can inherit the owned job.'
        return (Join-Path $destination 'cdb.exe')
    }

    function Copy-Logs([string] $Destination)
    {
        [IO.Directory]::CreateDirectory($Destination) | Out-Null
        foreach ($name in @('console.log', 'errors.log', 'CEGUI.log'))
        {
            $source = Join-Path (Join-Path $script:Repo 'Logs') $name
            if ([IO.File]::Exists($source))
            {
                Copy-Item -LiteralPath $source -Destination (Join-Path $Destination $name)
            }
        }
    }

    function Save-SourceState([string] $Attempt)
    {
        & git -C $script:Repo --no-pager diff --binary "--output=$(Join-Path $Attempt 'unstaged.patch')" --
        if ($LASTEXITCODE -ne 0) { throw 'Could not archive the unstaged diff.' }
        & git -C $script:Repo --no-pager diff --cached --binary "--output=$(Join-Path $Attempt 'staged.patch')" --
        if ($LASTEXITCODE -ne 0) { throw 'Could not archive the staged diff.' }
        & git -C $script:Repo status --short --untracked-files=all |
            Out-File -LiteralPath (Join-Path $Attempt 'git-status.txt') -Encoding UTF8
        if ($LASTEXITCODE -ne 0) { throw 'Could not archive the Git status.' }
        & git -C $script:Repo rev-parse HEAD | Out-File -LiteralPath (Join-Path $Attempt 'head.txt') -Encoding ASCII
        if ($LASTEXITCODE -ne 0) { throw 'Could not archive HEAD.' }
        foreach ($relative in @('Source\Core\Headers\RenderSmokeTest.h', 'Source\Core\Headers\SmokeOutput.h',
                                'Source\Core\Headers\VulkanRecoveryPolicy.h',
                                'Source\Core\RenderSmokeTest.cpp',
                                'Source\UnitTests\Test-Engine\RenderSmokeTests.cpp',
                                'Tools\VulkanRecoveryProbe.cpp', 'Tools\FrameworkSmokeTest.ps1', 'XML\config.xml'))
        {
            $source = Join-Path $script:Repo $relative
            if ([IO.File]::Exists($source))
            {
                $dest = Join-Path (Join-Path $Attempt 'source-files') $relative
                [IO.Directory]::CreateDirectory((Split-Path -Parent $dest)) | Out-Null
                Copy-Item -LiteralPath $source -Destination $dest
            }
        }
    }

    function Assert-NoConflictingRenderer([string] $Executable)
    {
        $expected = [IO.Path]::GetFileName($Executable)
        $matches = @(Get-CimInstance Win32_Process | Where-Object {
            $_.ExecutablePath -and [IO.Path]::GetFileName($_.ExecutablePath) -eq $expected
        })
        if ($matches.Count -gt 0)
        {
            throw "A renderer using the same executable is already running (PID $($matches[0].ProcessId)); it was not modified."
        }
    }

    function Register-OwnedCandidate($Owned, $Info)
    {
        $process = [Diagnostics.Process]::GetProcessById([int]$Info.ProcessId)
        try
        {
            $started = $process.StartTime.ToUniversalTime()
            $null = $process.Handle
            if ($started -lt $Owned.Started.AddSeconds(-5) -or
                -not [string]::Equals($process.Path, $Info.ExecutablePath, [StringComparison]::OrdinalIgnoreCase) -or
                [Math]::Abs(($started - $Info.CreationDate.ToUniversalTime()).TotalSeconds) -gt 1)
            {
                throw "Process identity could not be verified for PID $($Info.ProcessId)."
            }
            foreach ($tracked in $Owned.TrackedProcesses)
            {
                if ($tracked.Process.Id -eq $process.Id)
                {
                    if ($tracked.Started -ne $started) { throw "Process identity changed for PID $($process.Id)." }
                    $process.Dispose()
                    return
                }
            }
            $Owned.TrackedProcesses.Add([PSCustomObject]@{
                Process = $process
                Started = $started
                Path = $Info.ExecutablePath
            })
        }
        catch
        {
            $process.Dispose()
            throw
        }
    }

    function Register-OwnedDebugger($Owned, [string] $Debugger)
    {
        $matches = @(Get-CimInstance Win32_Process -Filter "Name='cdb.exe'" | Where-Object {
            $_.ExecutablePath -and
            [string]::Equals($_.ExecutablePath, $Debugger, [StringComparison]::OrdinalIgnoreCase)
        })
        if ($matches.Count -eq 0) { return $false }
        if ($matches.Count -ne 1) { throw 'The attempt-specific debugger process is ambiguous.' }
        $debuggerProcess = $matches[0]
        Register-OwnedCandidate $Owned $debuggerProcess
        if ([int]$debuggerProcess.ParentProcessId -ne $Owned.Process.Id -or
            -not [DivideSmoke.Job]::Contains($Owned.Job, [int]$debuggerProcess.ProcessId))
        {
            throw 'The debugger is not inside the supervisor''s owned job.'
        }
        return $true
    }

    function Confirm-OwnedDebuggee($Owned, [int] $DebuggeePid, [string] $Executable, [string] $Debugger)
    {
        $child = Get-CimInstance Win32_Process -Filter "ProcessId=$DebuggeePid"
        if ($null -eq $child -or -not $child.ExecutablePath -or
            -not [string]::Equals($child.ExecutablePath, $Executable, [StringComparison]::OrdinalIgnoreCase))
        {
            throw 'The gated process does not match the expected executable.'
        }
        if ($Debugger)
        {
            $parentId = [int]$child.ParentProcessId
            $parent = Get-CimInstance Win32_Process -Filter "ProcessId=$parentId"
            if ($null -eq $parent -or -not $parent.ExecutablePath -or
                -not [string]::Equals($parent.ExecutablePath, $Debugger, [StringComparison]::OrdinalIgnoreCase))
            {
                throw 'The gated process was not launched by this attempt''s debugger.'
            }
            Register-OwnedCandidate $Owned $parent
            Register-OwnedCandidate $Owned $child
            if ([int]$parent.ParentProcessId -ne $Owned.Process.Id -or
                -not [DivideSmoke.Job]::Contains($Owned.Job, $parentId))
            {
                throw 'The debugger is not inside the supervisor''s owned job.'
            }
        }
        else
        {
            if ([int]$child.ParentProcessId -ne $Owned.Process.Id)
            {
                throw 'The gated process was not launched by this attempt''s supervisor.'
            }
            Register-OwnedCandidate $Owned $child
        }
        if (-not [DivideSmoke.Job]::Contains($Owned.Job, $DebuggeePid))
        {
            throw 'The gated process is not inside the supervisor''s owned job.'
        }
    }

    function Read-KeyValues([string] $Path)
    {
        $values = @{}
        if ([IO.File]::Exists($Path))
        {
            foreach ($line in [IO.File]::ReadAllLines($Path))
            {
                if ($line -match '^([a-z_]+)=(.*)$') { $values[$matches[1]] = $matches[2] }
            }
        }
        return $values
    }

    function Get-Progress([string] $Attempt)
    {
        $events = Join-Path $Attempt 'events.log'
        $progress = [PSCustomObject]@{ Ready = $false; Presents = 0; Capture = $false; WindowCapture = $false; GPU = ''; SupervisorPid = 0 }
        if ([IO.File]::Exists($events))
        {
            foreach ($line in (Get-Content -LiteralPath $events))
            {
                if ($line -eq 'SCENE_READY') { $progress.Ready = $true }
                elseif ($line -match '^PRESENT count=(\d+)') { $progress.Presents = [int]$matches[1] }
                elseif ($line -eq 'CAPTURE_COMPLETE') { $progress.Capture = $true }
                elseif ($line -match '^WINDOW_CAPTURE_COMPLETE ') { $progress.WindowCapture = $true }
                elseif ($line -match '^GPU uuid=([0-9a-fA-F]{32}) ') { $progress.GPU = $matches[1].ToLowerInvariant() }
                elseif ($line -match '^SUPERVISOR_WAIT pid=(\d+)$') { $progress.SupervisorPid = [int]$matches[1] }
            }
        }
        return $progress
    }

    function Get-FailureSignature([string] $Attempt, [string] $Kind)
    {
        $path = Join-Path $Attempt 'failure.txt'
        $raw = if ([IO.File]::Exists($path)) { [IO.File]::ReadAllText($path) } else { $Kind }
        $fields = Read-KeyValues $path
        $debuggerSignature = $false
        if ($Kind -eq 'unexpected_exit' -and -not [IO.File]::Exists($path))
        {
            $debugLog = Join-Path $Attempt 'debugger.log'
            if ([IO.File]::Exists($debugLog))
            {
                $debugText = [IO.File]::ReadAllText($debugLog)
                $marker = [regex]::Match($debugText, 'SMOKE_DEBUGGER_FAILURE\r?\nLast event:')
                if ($marker.Success)
                {
                    $failureText = $debugText.Substring($marker.Index)
                    $event = [regex]::Match($failureText, '(?m)^Last event: [^\r\n]*?(?:code ([0-9a-fA-F]+)|Hit breakpoint)')
                    $location = [regex]::Match($failureText, '(?m)^(\S+![^\r\n]+):\r?\n[0-9a-fA-F`]+\s')
                    if ($event.Success -and $location.Success)
                    {
                        $eventName = if ($event.Groups[1].Success) { $event.Groups[1].Value.ToLowerInvariant() } else { 'breakpoint' }
                        $symbol = $location.Groups[1].Value
                        if ($symbol -like 'Divide_Framework!*') { $symbol = $symbol -replace '\+0x[0-9a-fA-F]+$', '' }
                        $raw = "debugger:${eventName}:$symbol"
                        $debuggerSignature = $true
                        if ($eventName -in @('breakpoint', '80000003'))
                        {
                            $detail = ''
                            $errorLog = Join-Path $Attempt 'logs-after\errors.log'
                            if ([IO.File]::Exists($errorLog))
                            {
                                $assertions = [regex]::Matches([IO.File]::ReadAllText($errorLog),
                                    '(?m)ASSERT \[([^\r\n]+?)\s*:\s*\d+\]:\s*([^\r\n]+)')
                                if ($assertions.Count -gt 0)
                                {
                                    $assertion = $assertions[$assertions.Count - 1]
                                    $detail = "$($assertion.Groups[1].Value):$($assertion.Groups[2].Value)"
                                }
                            }
                            $stderr = Join-Path $Attempt 'renderer\stderr.log'
                            if (-not $detail -and [IO.File]::Exists($stderr))
                            {
                                $assertions = [regex]::Matches([IO.File]::ReadAllText($stderr),
                                    '(?m)^Assertion failed:\s*(.+?), file (.+?), line \d+\s*$')
                                if ($assertions.Count -gt 0)
                                {
                                    $assertion = $assertions[$assertions.Count - 1]
                                    $detail = "$($assertion.Groups[2].Value):$($assertion.Groups[1].Value)"
                                }
                            }
                            if ($detail)
                            {
                                $detail = $detail -replace '0x[0-9a-fA-F]+', '<handle>' -replace '\b[0-9]{6,}\b', '<number>'
                                $raw += ":$detail"
                            }
                        }
                    }
                }
            }
        }
        if ($Kind -eq 'device_lost')
        {
            if ($raw -match '\b(vk[A-Za-z0-9_]+)\(\): Returned error VK_ERROR_DEVICE_LOST\b')
            {
                return "device_lost:$($matches[1])"
            }
            if ($fields.ContainsKey('detail') -and $fields['detail'] -match '^\s*(vk[A-Za-z0-9_]+)\b')
            {
                return "device_lost:$($matches[1])"
            }
        }
        if ($Kind -eq 'gpu_crash')
        {
            $report = Join-Path $Attempt 'amd-crash.txt'
            if ([IO.File]::Exists($report) -and
                [IO.File]::ReadAllText($report) -match '(?im)^API shader hash\s*:\s*(0x[0-9a-f]+)')
            {
                return "gpu_crash:shader:$($matches[1].ToLowerInvariant())"
            }
        }
        $messageID = if ($debuggerSignature) { 'debugger' }
                     elseif ($fields.ContainsKey('message_id')) { $fields['message_id'] }
                     else { $Kind }
        $stable = ($raw -split "`r?`n" | Where-Object { $_ -notmatch '^(run_id|presents|thread|object type)=' }) -join "`n"
        if (-not $debuggerSignature)
        {
            $stable = $stable -replace '0x[0-9a-fA-F]+', '<handle>' -replace '\b[0-9]{6,}\b', '<number>'
        }
        $sha = [Security.Cryptography.SHA256]::Create()
        try
        {
            $hash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($stable))).Replace('-', '')
        }
        finally { $sha.Dispose() }
        return '{0}:{1}:{2}' -f $Kind, $messageID, $hash.Substring(0, 16)
    }

    function Get-FailureKind([string] $Attempt, [string] $Fallback)
    {
        $crashDump = Join-Path $Attempt 'amd-crash.rgd'
        if ([IO.File]::Exists($crashDump) -and (Get-Item -LiteralPath $crashDump).Length -gt 0)
        {
            return 'gpu_crash'
        }
        $path = Join-Path $Attempt 'failure.txt'
        if ([IO.File]::Exists($path) -and [IO.File]::ReadAllText($path) -match '\bVK_ERROR_DEVICE_LOST\b')
        {
            return 'device_lost'
        }
        return $Fallback
    }

    function Wait-GPURecovery([string] $GPU, [string] $Probe, [string] $Attempt)
    {
        $script:State.phase = 'waiting_for_recovery'
        $script:State.gpu = $GPU
        Save-State
        if ($GPU -notmatch '^[0-9a-f]{32}$' -or -not [IO.File]::Exists($Probe))
        {
            throw 'Cannot verify GPU recovery without the selected device UUID and the recovery probe.'
        }
        $delay = 3
        $probeNumber = 0
        while ($true)
        {
            Write-Timeline "Waiting $delay seconds before probing GPU $GPU."
            Start-Sleep -Seconds $delay
            do
            {
                $probeNumber++
                $probeDir = Join-Path (Join-Path $Attempt 'recovery') ('probe-{0:D4}' -f $probeNumber)
            } while ([IO.Directory]::Exists($probeDir))
            $probeProcess = Start-OwnedProcess $probeDir $Probe @("--uuid=$GPU", '--timeout-ms=3000') $script:Repo
            $result = Wait-OwnedProcess $probeProcess 15
            $stdout = if ([IO.File]::Exists($probeProcess.StdoutPath)) { [IO.File]::ReadAllText($probeProcess.StdoutPath) } else { '' }
            if (-not $result.TimedOut -and $result.ExitCode -eq 0 -and $stdout -match '(?m)^PROBE_RECOVERED\s*$')
            {
                $script:State.recoveryVerifiedForAttempt = $Attempt
                Save-State
                Write-Timeline "GPU $GPU completed a fresh-device fence probe."
                return
            }
            Write-Timeline "Recovery probe $probeNumber failed or timed out; retaining its evidence."
            $delay = [Math]::Min(60, $delay * 2)
        }
    }

    function New-DebuggerScript([string] $Path, [string] $DumpPath)
    {
        $quotedDump = '\"' + $DumpPath.Replace('\', '\\') + '\"'
        $capture = ".echo SMOKE_DEBUGGER_FAILURE; .lastevent; r; ~* kb; lmv; .dump /mdi $quotedDump; q"
        $lines = @(
            "bu ucrtbased!abort `"$capture`"",
            "bu ucrtbase!abort `"$capture`"",
            "sxe -c `"$capture`" bpe",
            "sxe -c `"$capture`" av",
            "sxe -c `"$capture`" sov",
            "sxe -c `"$capture`" c000001d",
            "sxe -c `"$capture`" c0000409",
            "sxe -c `"$capture`" c0000374",
            'sxe -c ".echo SMOKE_DEBUGGEE_EXIT; .lastevent; q" epr',
            'g'
        )
        [IO.File]::WriteAllLines($Path, $lines, [Text.Encoding]::ASCII)
    }

    function Test-NonInteractiveAbort([string] $Fixture, [string] $Directory)
    {
        $check = Start-OwnedProcess (Join-Path $Directory 'crt-abort-check') $Fixture @('abort') $Directory
        $result = Wait-OwnedProcess $check 15
        $stderr = if ([IO.File]::Exists($check.StderrPath)) { [IO.File]::ReadAllText($check.StderrPath) } else { '' }
        if ($result.TimedOut -or $result.ExitCode -ne 3 -or $stderr -notmatch 'abort\(\)')
        {
            throw 'The noninteractive CRT abort fixture did not report and terminate as expected.'
        }
        Write-Timeline 'The noninteractive CRT abort fixture reported and terminated without a dialog.'
    }

    function Test-Debugger([string] $Debugger, [string] $Fixture, [string] $Directory)
    {
        if (-not $Debugger)
        {
            Write-Timeline 'CDB is unavailable; validation failures will terminate without debugger capture.'
            return
        }
        $Directory = Join-Path $Directory 'debugger check'
        [IO.Directory]::CreateDirectory($Directory) | Out-Null
        $marker = Join-Path $Directory 'fixture-marker.txt'
        $dump = Join-Path $Directory 'fixture.dmp'
        $commands = Join-Path $Directory 'debugger-commands.txt'
        $debugLog = Join-Path $Directory 'debugger.log'
        New-DebuggerScript $commands $dump
        $ownershipMarker = Join-Path $Directory 'ownership-pid.txt'
        $ownershipDump = Join-Path $Directory 'ownership.dmp'
        $ownershipLog = Join-Path $Directory 'ownership-debugger.log'
        $ownershipCommands = Join-Path $Directory 'ownership-debugger-commands.txt'
        New-DebuggerScript $ownershipCommands $ownershipDump
        $ownership = Start-OwnedProcess (Join-Path $Directory 'ownership-process') $Debugger `
            @('-y', (Split-Path -Parent $Fixture), '-G', '-logo', $ownershipLog, '-cf', $ownershipCommands,
              $Fixture, 'owned', $ownershipMarker) $Directory
        try
        {
            $deadline = [DateTime]::UtcNow.AddSeconds(15)
            $confirmed = $false
            $markerReadError = ''
            while (-not $ownership.Process.HasExited -and [DateTime]::UtcNow -lt $deadline)
            {
                $null = Register-OwnedDebugger $ownership $Debugger
                if ([IO.File]::Exists($ownershipMarker))
                {
                    try
                    {
                        $pidText = [IO.File]::ReadAllText($ownershipMarker)
                    }
                    catch
                    {
                        $exception = $_.Exception
                        if ($exception -is [Management.Automation.MethodInvocationException])
                        {
                            $exception = $exception.InnerException
                        }
                        if ($exception -isnot [IO.IOException] -or ($exception.HResult -band 0xffff) -notin @(32, 33))
                        {
                            throw
                        }
                        $markerReadError = $exception.Message
                        Start-Sleep -Milliseconds 100
                        continue
                    }
                    if ($pidText -notmatch '^\d+$') { throw 'The debugger fixture returned an invalid PID.' }
                    Confirm-OwnedDebuggee $ownership ([int]$pidText) $Fixture $Debugger
                    $confirmed = $true
                    break
                }
                Start-Sleep -Milliseconds 100
            }
            if (-not $confirmed)
            {
                if ($markerReadError) { throw "The debugger ownership fixture marker remained unavailable: $markerReadError" }
                throw 'The debugger ownership fixture did not become ready.'
            }
            [DivideSmoke.Job]::BreakOwned($ownership.Job, [int]$pidText)
            $deadline = [DateTime]::UtcNow.AddSeconds(15)
            while (-not $ownership.Process.HasExited -and [DateTime]::UtcNow -lt $deadline)
            {
                Show-OwnedOutput $ownership
                Start-Sleep -Milliseconds 100
            }
            $transcript = if ([IO.File]::Exists($ownershipLog)) { [IO.File]::ReadAllText($ownershipLog) } else { '' }
            if (-not $ownership.Process.HasExited -or $transcript -notmatch 'SMOKE_DEBUGGER_FAILURE' -or
                -not [IO.File]::Exists($ownershipDump))
            {
                throw 'The debugger could not capture an owned, unresponsive child.'
            }
        }
        finally { Stop-OwnedProcess $ownership $true }
        Write-Timeline 'The debugger captured and terminated an owned, unresponsive child.'

        $debugged = Start-OwnedProcess (Join-Path $Directory 'debugger-process') $Debugger `
            @('-y', (Split-Path -Parent $Fixture), '-G', '-logo', $debugLog, '-cf', $commands, $Fixture, 'break', $marker) $Directory
        $result = Wait-OwnedProcess $debugged 15
        $markerText = if ([IO.File]::Exists($marker)) { [IO.File]::ReadAllText($marker) } else { '' }
        $transcript = if ([IO.File]::Exists($debugLog)) { [IO.File]::ReadAllText($debugLog) } else { '' }
        if ($result.TimedOut -or $markerText -notmatch 'before' -or $markerText -match 'resumed' -or
            $transcript -notmatch 'SMOKE_DEBUGGER_FAILURE' -or -not [IO.File]::Exists($dump))
        {
            throw 'The scripted debugger did not capture and terminate a controlled breakpoint safely.'
        }
        Write-Timeline 'The scripted debugger captured a controlled breakpoint without resuming the child.'

        $abortDump = Join-Path $Directory 'abort.dmp'
        $abortCommands = Join-Path $Directory 'abort-debugger-commands.txt'
        $abortLog = Join-Path $Directory 'abort-debugger.log'
        New-DebuggerScript $abortCommands $abortDump
        $abort = Start-OwnedProcess (Join-Path $Directory 'abort-process') $Debugger `
            @('-y', (Split-Path -Parent $Fixture), '-G', '-logo', $abortLog, '-cf', $abortCommands, $Fixture, 'abort') $Directory
        $abortResult = Wait-OwnedProcess $abort 15
        $abortTranscript = if ([IO.File]::Exists($abortLog)) { [IO.File]::ReadAllText($abortLog) } else { '' }
        if ($abortResult.TimedOut -or $abortTranscript -notmatch '(?m)^SMOKE_DEBUGGER_FAILURE\r?$' -or
            $abortTranscript -notmatch 'ucrtbased!abort' -or -not [IO.File]::Exists($abortDump))
        {
            throw 'The scripted debugger did not capture a controlled CRT abort safely.'
        }
        Write-Timeline 'The scripted debugger captured a controlled CRT abort and its stack.'

        $successLog = Join-Path $Directory 'success-debugger.log'
        $normal = Start-OwnedProcess (Join-Path $Directory 'normal-process') $Debugger `
            @('-y', (Split-Path -Parent $Fixture), '-G', '-logo', $successLog, '-cf', $commands, $Fixture, 'success') $Directory
        $normalResult = Wait-OwnedProcess $normal 15
        $normalTranscript = if ([IO.File]::Exists($successLog)) { [IO.File]::ReadAllText($successLog) } else { '' }
        if ($normalResult.TimedOut -or $normalResult.ExitCode -ne 0 -or
            $normalTranscript -notmatch '(?im)^Last event: [^\r\n]*Exit process [^\r\n]*, code (?:0|0x0+)\s*$')
        {
            throw 'The debugger did not report a controlled clean process exit.'
        }
    }

    function Get-DisplayReset([DateTime] $Since)
    {
        try
        {
            $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; StartTime = $Since; Id = 4101 } -ErrorAction Stop |
                        Where-Object { $_.ProviderName -eq 'Display' })
            if ($events.Count -gt 0) { return $events[0] }
        }
        catch
        {
            if ($_.FullyQualifiedErrorId -notmatch 'NoMatchingEventsFound')
            {
                if (-not $script:EventLogWarning)
                {
                    Write-Timeline ("Windows display-reset events unavailable: " + $_.Exception.Message)
                    $script:EventLogWarning = $true
                }
            }
        }
        return $null
    }

    function Record-DisplayReset([string] $Attempt, [DateTime] $Since)
    {
        $path = Join-Path $Attempt 'display-reset.json'
        if ([IO.File]::Exists($path)) { return $true }
        $event = Get-DisplayReset $Since
        if ($null -eq $event) { return $false }

        $record = @{
            timeUtc = $event.TimeCreated.ToUniversalTime().ToString('o')
            recordId = $event.RecordId
            provider = $event.ProviderName
            eventId = $event.Id
            message = $event.Message
        } | ConvertTo-Json -Depth 3
        [IO.File]::WriteAllText($path, $record, [Text.UTF8Encoding]::new($false))
        return $true
    }

    function Wait-Renderer($Owned, [string] $Attempt, [string] $Debugger, $AmdCapture, [string] $Renderer)
    {
        $startupDeadline = $Owned.Started.AddSeconds(180)
        $readyTime = $null
        $lastProgress = $Owned.Started
        $lastCount = 0
        $captureTime = $null
        $failureTime = $null
        $nextResetCheck = $Owned.Started
        $reason = ''
        $terminate = $false
        $debuggerTracked = $false
        $ownershipVerified = $false
        try
        {
            while (-not $Owned.Process.HasExited)
            {
                Show-OwnedOutput $Owned
                if ($null -ne $AmdCapture) { Show-OwnedOutput $AmdCapture }
                $now = [DateTime]::UtcNow
                $progress = Get-Progress $Attempt
                $gateFailure = 'supervisor_ownership'
                try
                {
                    if ($Debugger -and -not $debuggerTracked)
                    {
                        $debuggerTracked = Register-OwnedDebugger $Owned $Debugger
                    }
                    if ($progress.SupervisorPid -gt 0 -and -not $ownershipVerified)
                    {
                        Confirm-OwnedDebuggee $Owned $progress.SupervisorPid $Renderer $Debugger
                        if ($RenderDocInjectExe)
                        {
                            $gateFailure = 'renderdoc_preflight'
                            Start-RenderDocInjection $Attempt $progress.SupervisorPid
                            $gateFailure = 'supervisor_ownership'
                        }
                        $gate = [IO.File]::Open((Join-Path $Attempt 'supervisor.ready'), [IO.FileMode]::CreateNew,
                                                [IO.FileAccess]::Write, [IO.FileShare]::Read)
                        $gate.Dispose()
                        $ownershipVerified = $true
                        Write-Timeline "Verified renderer PID $($progress.SupervisorPid) and released its pre-GPU gate."
                    }
                }
                catch
                {
                    Write-Timeline ("Renderer pre-GPU gate failed ($gateFailure): " + $_.Exception.Message)
                    $reason = $gateFailure
                    $terminate = $true
                    break
                }
                if ($progress.GPU -and $script:State.gpu -ne $progress.GPU)
                {
                    $script:State.gpu = $progress.GPU
                    Save-State
                }
                if ($progress.Ready -and $null -eq $readyTime)
                {
                    $readyTime = $now
                    $lastProgress = $now
                    Write-Timeline 'Scene readiness reached.'
                }
                if ($progress.Presents -gt $lastCount)
                {
                    $lastCount = $progress.Presents
                    $lastProgress = $now
                    Write-Timeline "Scene presents: $lastCount."
                }
                if ($progress.Capture -and $progress.WindowCapture -and $null -eq $captureTime)
                {
                    $captureTime = [DateTime]::UtcNow
                    Write-Timeline 'Scene and editor images captured; waiting for normal shutdown.'
                }
                if ([IO.File]::Exists((Join-Path $Attempt 'failure.txt')) -and $null -eq $failureTime)
                {
                    $failureTime = $now
                    Write-Timeline 'Failure evidence detected; waiting for bounded debugger capture.'
                }
                if ($null -ne $failureTime -and ($now - $failureTime).TotalSeconds -ge 10)
                {
                    $reason = 'debugger_capture_timeout'
                    $terminate = $true
                    break
                }
                if ($null -eq $failureTime -and $null -ne $AmdCapture -and $AmdCapture.Process.HasExited)
                {
                    $reason = 'crash_capture_stopped'
                    $terminate = $true
                    break
                }
                if ($null -eq $failureTime -and $now -ge $nextResetCheck)
                {
                    if (Record-DisplayReset $Attempt $Owned.Started)
                    {
                        $reason = 'driver_reset'
                        $terminate = $true
                        break
                    }
                    $nextResetCheck = $now.AddSeconds(5)
                }
                if ($null -eq $failureTime -and $null -eq $readyTime -and $now -ge $startupDeadline)
                {
                    $reason = 'startup_timeout'
                    $terminate = $true
                    break
                }
                if ($null -eq $failureTime -and $null -ne $readyTime -and $null -eq $captureTime)
                {
                    if (($now - $lastProgress).TotalSeconds -ge 30)
                    {
                        $reason = 'gpu_hang'
                        $terminate = $true
                        break
                    }
                    if (($now - $readyTime).TotalSeconds -ge $RenderTimeoutSeconds)
                    {
                        $reason = 'render_timeout'
                        $terminate = $true
                        break
                    }
                }
                if ($null -eq $failureTime -and $null -ne $captureTime -and ($now - $captureTime).TotalSeconds -ge 60)
                {
                    $reason = 'shutdown_timeout'
                    $terminate = $true
                    break
                }
                Start-Sleep -Milliseconds 500
            }
            if (-not $terminate)
            {
                $Owned.Process.WaitForExit()
                Show-OwnedOutput $Owned
                $exitCode = $Owned.Process.ExitCode
            }
            else
            {
                $exitCode = 124
                if ($Debugger -and $ownershipVerified -and
                    $reason -in @('startup_timeout', 'gpu_hang', 'render_timeout', 'shutdown_timeout'))
                {
                    try
                    {
                        Confirm-OwnedDebuggee $Owned $progress.SupervisorPid $Renderer $Debugger
                        [DivideSmoke.Job]::BreakOwned($Owned.Job, $progress.SupervisorPid)
                        Write-Timeline "Requested debugger capture for $reason."
                        $captureDeadline = [DateTime]::UtcNow.AddSeconds(10)
                        while (-not $Owned.Process.HasExited -and [DateTime]::UtcNow -lt $captureDeadline)
                        {
                            Show-OwnedOutput $Owned
                            Start-Sleep -Milliseconds 250
                        }
                        if (-not [IO.File]::Exists((Join-Path $Attempt 'failure.dmp')))
                        {
                            Write-Timeline "No debugger dump was produced for $reason before termination."
                        }
                    }
                    catch
                    {
                        Write-Timeline ("Could not capture $reason`: " + $_.Exception.Message)
                    }
                }
            }
        }
        finally
        {
            Stop-OwnedProcess $Owned $terminate
        }
        return [PSCustomObject]@{ ExitCode = $exitCode; Reason = $reason; Debugging = [bool]$Debugger; OwnershipVerified = $ownershipVerified }
    }

    function Confirm-Result([string] $Attempt, [string] $RunID, $ProcessResult)
    {
        $result = Read-KeyValues (Join-Path $Attempt 'result.txt')
        $progress = Get-Progress $Attempt
        if ([IO.File]::Exists((Join-Path $Attempt 'failure.txt')) -or
            (Get-FailureKind $Attempt '') -eq 'gpu_crash' -or $ProcessResult.Reason -or
            $ProcessResult.ExitCode -ne 0 -or -not $ProcessResult.OwnershipVerified -or -not $result.ContainsKey('run_id') -or
            $result['run_id'] -ne $RunID -or $result['status'] -ne 'success' -or
            $result['backend'] -ne $Backend -or -not $progress.GPU -or
            ($Backend -eq 'OpenGL' -and $progress.GPU -ne $ExpectedGPU) -or
            $result['target'] -ne [string]$script:TargetPresents -or
            [int]$result['presents'] -lt $script:TargetPresents -or -not $progress.Ready -or
            $progress.Presents -lt $script:TargetPresents -or -not $progress.Capture -or -not $progress.WindowCapture)
        {
            return $false
        }
        if ($ProcessResult.Debugging)
        {
            $debugLog = Join-Path $Attempt 'debugger.log'
            if (-not [IO.File]::Exists($debugLog) -or
                [IO.File]::ReadAllText($debugLog) -notmatch '(?im)^Last event: [^\r\n]*Exit process [^\r\n]*, code (?:0|0x0+)\s*$')
            {
                return $false
            }
        }
        if ($result['capture'] -ne 'scene.png' -or $result['window_capture'] -ne 'editor.png')
        {
            return $false
        }
        Add-Type -AssemblyName System.Drawing
        foreach ($name in @('scene.png', 'editor.pending.png'))
        {
            $image = Join-Path $Attempt $name
            if (-not [IO.File]::Exists($image) -or (Get-Item -LiteralPath $image).Length -eq 0)
            {
                return $false
            }
            try
            {
                $bitmap = [Drawing.Image]::FromFile($image)
                try
                {
                    if ($bitmap.Width -le 0 -or $bitmap.Height -le 0) { return $false }
                }
                finally { $bitmap.Dispose() }
            }
            catch [System.ArgumentException]
            {
                Write-Timeline "The $name capture is not a readable image."
                return $false
            }
        }
        return $true
    }

    function Update-FailurePolicy([string] $Signature, [int] $Presents, [bool] $DeviceLoss, [string] $FailureAttempt = '')
    {
        if ($DeviceLoss)
        {
            if ($script:State.lastDeviceSignature -eq $Signature -and $Presents -le $script:State.maximumDevicePresents)
            {
                $script:State.deviceRepeatCount++
            }
            else
            {
                $script:State.lastDeviceSignature = $Signature
                $script:State.maximumDevicePresents = $Presents
                $script:State.deviceRepeatCount = 1
            }
            $repairsWithoutProgress = $script:State.deviceRepeatCount - 1
            if ($repairsWithoutProgress -ge 3 -and $repairsWithoutProgress % 3 -eq 0)
            {
                $script:State.pendingApproval = $true
            }
            $script:State.deviceFailureAccountedAttempt = $FailureAttempt
        }
        else
        {
            if ($script:State.lastSignature -eq $Signature -and $Presents -le $script:State.maximumPresents)
            {
                $script:State.repeatCount++
            }
            else
            {
                $script:State.lastSignature = $Signature
                $script:State.maximumPresents = $Presents
                $script:State.repeatCount = 1
            }
            if ($script:State.repeatCount - 1 -ge 3) { $script:State.phase = 'stopped_no_progress' }
        }
        Save-State
    }

    function Complete-InterruptedAttempt()
    {
        $attempt = $script:State.attempt
        $stalledContinuation = $script:State.stalledNonDeviceAttempt -and $script:State.stalledNonDeviceAttempt -eq $attempt
        $progress = Get-Progress $attempt
        $failure = Read-KeyValues (Join-Path $attempt 'failure.txt')
        $launch = Join-Path $attempt 'renderer\launch.json'
        $reset = [IO.File]::Exists((Join-Path $attempt 'display-reset.json'))
        if (-not $reset -and [IO.File]::Exists($launch))
        {
            $reset = Record-DisplayReset $attempt ([IO.File]::GetCreationTimeUtc($launch))
        }
        if (-not $failure.ContainsKey('kind') -and -not $reset -and (Get-FailureKind $attempt '') -ne 'gpu_crash')
        {
            $script:State.phase = 'interrupted'
            Save-State
            throw 'The interrupted renderer left no complete failure record; inspect its artifacts before continuing.'
        }

        $fallback = if ($failure.ContainsKey('kind')) { $failure['kind'] } else { 'driver_reset' }
        $kind = Get-FailureKind $attempt $fallback
        if ($reset -and $kind -notin @('device_lost', 'gpu_hang', 'gpu_crash', 'driver_reset'))
        {
            $kind = 'driver_reset'
        }
        $deviceLoss = $kind -in @('device_lost', 'gpu_hang', 'gpu_crash', 'driver_reset')
        $script:State.phase = if ($stalledContinuation) { 'stopped_no_progress' } else { 'needs_fix' }
        if ($stalledContinuation) { $script:State.stalledNonDeviceAttempt = '' }
        $signature = Get-FailureSignature $attempt $kind
        if (-not $deviceLoss -or $script:State.deviceFailureAccountedAttempt -ne $attempt)
        {
            Update-FailurePolicy $signature $progress.Presents $deviceLoss $attempt
        }
        Save-State
        Write-Timeline "Recovered interrupted attempt: kind=$kind signature=$signature presents=$($progress.Presents)."
    }

    $script:EventLogWarning = $false
    if ([IO.File]::Exists($script:StatePath))
    {
        $script:State = Get-Content -LiteralPath $script:StatePath -Raw | ConvertFrom-Json
        if ($script:State.version -ne 1) { throw 'Unsupported or corrupt supervisor state.' }
    }
    else
    {
        $script:State = [PSCustomObject]@{
            version = 1
            backend = $Backend
            expectedGPU = $ExpectedGPU
            project = $Project
            scene = $Scene
            attempts = 0
            phase = 'ready'
            gpu = ''
            probe = ''
            renderer = ''
            attempt = ''
            lastSignature = ''
            maximumPresents = 0
            repeatCount = 0
            lastDeviceSignature = ''
            maximumDevicePresents = 0
            deviceRepeatCount = 0
            deviceFailureAccountedAttempt = ''
            recoveryVerifiedForAttempt = ''
            recoveryRequiredForAttempt = ''
            buildPending = $false
            recoveryContinuation = 'needs_fix'
            pendingApproval = $false
            stalledNonDeviceAttempt = ''
        }
        Save-State
    }
    foreach ($entry in @(
        @{ Name = 'backend'; Value = 'Vulkan' },
        @{ Name = 'expectedGPU'; Value = '' },
        @{ Name = 'project'; Value = '' },
        @{ Name = 'scene'; Value = '' },
        @{ Name = 'lastDeviceSignature'; Value = '' },
        @{ Name = 'maximumDevicePresents'; Value = 0 },
        @{ Name = 'deviceRepeatCount'; Value = 0 },
        @{ Name = 'deviceFailureAccountedAttempt'; Value = '' },
        @{ Name = 'recoveryVerifiedForAttempt'; Value = '' },
        @{ Name = 'recoveryRequiredForAttempt'; Value = '' },
        @{ Name = 'buildPending'; Value = $false },
        @{ Name = 'recoveryContinuation'; Value = 'needs_fix' },
        @{ Name = 'stalledNonDeviceAttempt'; Value = '' }
    ))
    {
        if ($null -eq $script:State.PSObject.Properties[$entry.Name])
        {
            $script:State | Add-Member -NotePropertyName $entry.Name -NotePropertyValue $entry.Value
        }
    }
    if ($script:State.backend -ne $Backend -or
        ($Backend -eq 'OpenGL' -and $script:State.expectedGPU -ne $ExpectedGPU) -or
        $script:State.project -ne $Project -or $script:State.scene -ne $Scene)
    {
        throw 'The output directory belongs to a different backend, GPU, project or scene; use its original settings or a new directory.'
    }
    if ($script:State.phase -eq 'needs_fix' -and $script:State.attempt -and
        $script:State.lastSignature -like 'unexpected_exit:unexpected_exit:*' -and
        -not [IO.File]::Exists((Join-Path $script:State.attempt 'failure.txt')) -and
        [IO.File]::Exists((Join-Path $script:State.attempt 'debugger.log')))
    {
        $normalized = Get-FailureSignature $script:State.attempt 'unexpected_exit'
        if ($normalized -like 'unexpected_exit:debugger:*')
        {
            $oldCount = [int]$script:State.repeatCount
            $matchingCount = 1
            for ($number = [int]$script:State.attempts - 1; $number -gt 0 -and $matchingCount -lt $oldCount; $number--)
            {
                $previous = Join-Path $script:Root ('attempt-{0:D2}' -f $number)
                if ([IO.File]::Exists((Join-Path $previous 'failure.txt')) -or
                    -not [IO.File]::Exists((Join-Path $previous 'debugger.log')))
                {
                    $matchingCount = $oldCount
                    break
                }
                $previousSignature = Get-FailureSignature $previous 'unexpected_exit'
                if ($previousSignature -notlike 'unexpected_exit:debugger:*')
                {
                    $matchingCount = $oldCount
                    break
                }
                if ($previousSignature -ne $normalized) { break }
                $matchingCount++
            }
            $script:State.lastSignature = $normalized
            $script:State.repeatCount = $matchingCount
            Save-State
            Write-Timeline "Normalized the preserved debugger failure signature: $normalized."
        }
    }
    if ($script:State.lastDeviceSignature -like 'device_lost:*' -and
        $script:State.deviceFailureAccountedAttempt -and
        [IO.File]::Exists((Join-Path $script:State.deviceFailureAccountedAttempt 'failure.txt')))
    {
        $accountedAttempt = $script:State.deviceFailureAccountedAttempt
        if ((Get-FailureKind $accountedAttempt '') -eq 'device_lost')
        {
            $normalized = Get-FailureSignature $accountedAttempt 'device_lost'
            if ($script:State.lastDeviceSignature -ne $normalized)
            {
                $script:State.lastDeviceSignature = $normalized
                Save-State
                Write-Timeline "Normalized the preserved device-loss signature: $normalized."
            }
        }
    }

    $vcvars = Get-VisualStudioEnvironment
    $reuseVisualStudioEnvironment = $false
    if ($env:VSCMD_VER)
    {
        $expectedInstallation = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $vcvars)))
        $compiler = Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue
        $validInstallation = $env:VSINSTALLDIR -and
                             [IO.Path]::GetFullPath($env:VSINSTALLDIR).TrimEnd('\').Equals($expectedInstallation, [StringComparison]::OrdinalIgnoreCase)
        $validCompiler = $compiler -and
                         $compiler.Source.StartsWith((Join-Path $expectedInstallation 'VC\Tools\MSVC') + '\', [StringComparison]::OrdinalIgnoreCase) -and
                         $compiler.Source -match '\\bin\\Hostx64\\x64\\cl\.exe$'
        if (-not $validInstallation -or -not $validCompiler -or
            $env:VSCMD_ARG_HOST_ARCH -ne 'x64' -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64')
        {
            throw 'The active Visual Studio environment does not match the selected x64 toolchain; start from a clean shell or its x64 developer shell.'
        }
        $reuseVisualStudioEnvironment = $true
        Write-Timeline 'Reusing the active matching x64 Visual Studio developer environment.'
    }
    if (-not $env:VULKAN_SDK -or -not [IO.File]::Exists((Join-Path $env:VULKAN_SDK 'Lib\vulkan-1.lib')) -or
        -not [IO.File]::Exists((Join-Path $env:VULKAN_SDK 'Include\vulkan\vulkan.h')))
    {
        throw 'A Vulkan SDK with headers and the loader import library is required.'
    }
    # Perf mode runs without a debugger: an attached debugger enables the Windows debug heap and skews timings.
    $debuggerSource = if ($PerfMode) { $null } else { Get-Debugger }
    $buildDirectory = Get-BinaryDirectory
    if ($AmdCaptureExe)
    {
        if (-not [IO.Path]::IsPathRooted($AmdCaptureExe) -or -not [IO.File]::Exists($AmdCaptureExe) -or
            -not [IO.File]::Exists((Join-Path (Split-Path -Parent $AmdCaptureExe) 'rgd.exe')))
        {
            throw 'Radeon capture requires absolute paths to RadeonDeveloperPanelCLI.exe and its adjacent rgd.exe.'
        }
        $AmdCaptureExe = [IO.Path]::GetFullPath($AmdCaptureExe)
    }
    if ($RenderDocInjectExe)
    {
        if ($Backend -ne 'Vulkan' -or -not [IO.Path]::IsPathRooted($RenderDocInjectExe) -or
            -not [IO.File]::Exists($RenderDocInjectExe) -or
            -not [string]::Equals([IO.Path]::GetFileName($RenderDocInjectExe), 'renderdoccmd.exe', [StringComparison]::OrdinalIgnoreCase))
        {
            throw 'RenderDoc injection requires Vulkan and an absolute path to renderdoccmd.exe.'
        }
        $RenderDocInjectExe = [IO.Path]::GetFullPath($RenderDocInjectExe)
    }
    if ($BuildJobs -eq 0)
    {
        $processors = @(Get-CimInstance Win32_Processor)
        $cores = ($processors | Measure-Object -Property NumberOfCores -Sum).Sum
        $logical = ($processors | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
        $freeGiB = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
        $memoryLimit = [Math]::Max(1, [Math]::Floor($freeGiB - 4))
        $cpuLimit = [Math]::Min($logical, [Math]::Ceiling($cores * 1.25))
        $BuildJobs = [int][Math]::Max(1, [Math]::Min(32, [Math]::Min($cpuLimit, $memoryLimit)))
        Write-Timeline "Selected $BuildJobs build jobs from $cores cores, $logical threads and $([Math]::Round($freeGiB, 1)) GiB available memory."
    }
    if (-not $PreflightOnly)
    {
        Assert-NoConflictingRenderer (Join-Path $script:Repo 'Divide-Framework.exe')
    }

    if ($script:State.phase -eq 'waiting_for_recovery')
    {
        if ($script:State.renderer) { Assert-NoConflictingRenderer $script:State.renderer }
        Wait-GPURecovery $script:State.gpu $script:State.probe $script:State.attempt
        if ($script:State.recoveryContinuation -eq 'interrupted')
        {
            Complete-InterruptedAttempt
            exit 86
        }
        $script:State.phase = $script:State.recoveryContinuation
        Save-State
        Write-Timeline 'Recovery was verified; diagnose and repair the preserved failure before the next attempt.'
        exit 87
    }
    if ($script:State.phase -eq 'needs_fix' -and $script:State.attempt -and
        $script:State.recoveryVerifiedForAttempt -ne $script:State.attempt)
    {
        $previousFailure = Read-KeyValues (Join-Path $script:State.attempt 'failure.txt')
        $previousKind = if ($previousFailure.ContainsKey('kind'))
                        {
                            Get-FailureKind $script:State.attempt $previousFailure['kind']
                        }
                        else { '' }
        if ([IO.File]::Exists((Join-Path $script:State.attempt 'display-reset.json')) -and
            $previousKind -notin @('device_lost', 'gpu_hang', 'gpu_crash', 'driver_reset'))
        {
            $previousKind = 'driver_reset'
        }
        $previousDeviceLoss = $previousKind -in @('device_lost', 'gpu_hang', 'gpu_crash', 'driver_reset', 'render_timeout', 'shutdown_timeout')
        if ($previousDeviceLoss -or
            $script:State.recoveryRequiredForAttempt -eq $script:State.attempt -or
            $script:State.deviceFailureAccountedAttempt -eq $script:State.attempt)
        {
            if ($script:State.renderer) { Assert-NoConflictingRenderer $script:State.renderer }
            $progress = Get-Progress $script:State.attempt
            if ($progress.GPU) { $script:State.gpu = $progress.GPU }
            if ($previousDeviceLoss -and $script:State.deviceFailureAccountedAttempt -ne $script:State.attempt)
            {
                $legacyKind = if ($previousFailure.ContainsKey('kind')) { $previousFailure['kind'] } else { $previousKind }
                $legacySignature = Get-FailureSignature $script:State.attempt $legacyKind
                if ($script:State.lastSignature -eq $legacySignature)
                {
                    $script:State.lastSignature = ''
                    $script:State.repeatCount = 0
                }
                $signature = Get-FailureSignature $script:State.attempt $previousKind
                Update-FailurePolicy $signature $progress.Presents $true $script:State.attempt
                Write-Timeline "Reclassified the preserved failure as a GPU recovery event: $signature."
            }
            $script:State.recoveryContinuation = 'needs_fix'
            Save-State
            Wait-GPURecovery $script:State.gpu $script:State.probe $script:State.attempt
            $script:State.phase = 'needs_fix'
            Save-State
            Write-Timeline 'Recovery was verified; diagnose and repair the preserved failure before the next attempt.'
            exit 87
        }
    }
    if ($script:State.phase -eq 'preflight')
    {
        if ($script:State.renderer) { Assert-NoConflictingRenderer $script:State.renderer }
        if (-not $script:State.attempt -or
            [IO.File]::Exists((Join-Path $script:State.attempt 'renderer\launch.json')))
        {
            throw 'The interrupted preflight has an unexpected renderer launch record; inspect its artifacts before continuing.'
        }
        $previousAttempt = [IO.Path]::GetFullPath($script:State.attempt).TrimEnd('\')
        if (-not $previousAttempt.StartsWith($script:Root + '\', [StringComparison]::OrdinalIgnoreCase))
        {
            throw 'The interrupted preflight path is outside the smoke artifact directory.'
        }
        $remaining = @(Get-CimInstance Win32_Process | Where-Object {
            $_.ExecutablePath -and $_.ExecutablePath.StartsWith($previousAttempt + '\', [StringComparison]::OrdinalIgnoreCase)
        })
        if ($remaining.Count -gt 0)
        {
            throw 'An interrupted preflight helper is still running; inspect it before continuing.'
        }
        $stalledContinuation = $script:State.stalledNonDeviceAttempt -and $script:State.stalledNonDeviceAttempt -eq $script:State.attempt
        $script:State.phase = if ($stalledContinuation) { 'stopped_no_progress' } else { 'needs_fix' }
        $script:State.stalledNonDeviceAttempt = ''
        Save-State
        Write-Timeline 'The interrupted preflight was preserved; no renderer was launched and its iteration remains counted.'
    }
    if ($script:State.phase -in @('running', 'building', 'testing'))
    {
        if ($script:State.renderer) { Assert-NoConflictingRenderer $script:State.renderer }
        if ($script:State.phase -eq 'running')
        {
            $progress = Get-Progress $script:State.attempt
            if ($progress.GPU) { $script:State.gpu = $progress.GPU }
            if (-not [IO.Directory]::Exists((Join-Path $script:State.attempt 'logs-after')))
            {
                Copy-Logs (Join-Path $script:State.attempt 'logs-after')
            }
            $failure = Read-KeyValues (Join-Path $script:State.attempt 'failure.txt')
            if ($script:State.gpu -or ($failure.ContainsKey('kind') -and $failure['kind'] -in @('device_lost', 'gpu_hang')))
            {
                $script:State.recoveryContinuation = 'interrupted'
                Save-State
                Wait-GPURecovery $script:State.gpu $script:State.probe $script:State.attempt
            }
            Complete-InterruptedAttempt
            exit 86
        }
        if ($script:State.phase -eq 'building')
        {
            # Older state files counted an attempt before starting the build.
            if (-not $script:State.buildPending -and $script:State.attempts -gt 0)
            {
                $script:State.attempts--
            }
            $script:State.buildPending = $false
            $stalledContinuation = $script:State.stalledNonDeviceAttempt -and $script:State.stalledNonDeviceAttempt -eq $script:State.attempt
            $script:State.phase = if ($stalledContinuation) { 'stopped_no_progress' } else { 'needs_fix' }
            $script:State.stalledNonDeviceAttempt = ''
            Save-State
            Write-Timeline 'The previous build was interrupted; its artifacts were preserved and no iteration was charged.'
            exit 86
        }
        $script:State.phase = 'interrupted'
        Save-State
        throw 'An earlier attempt was interrupted. Its owned processes have been stopped; inspect its artifacts before continuing.'
    }
    if ($script:State.phase -eq 'interrupted')
    {
        throw 'The previous attempt was interrupted without a complete failure record; inspect its artifacts before continuing.'
    }
    $approvedStalledNonDevice = $false
    if ($script:State.phase -eq 'stopped_no_progress')
    {
        if (-not $ApproveStalledNonDeviceLoss)
        {
            throw 'The loop stopped after three repairs without progress on the same non-device-loss failure.'
        }
        if ($PreflightOnly -or $DiagnosticPresents -eq 0)
        {
            throw 'The approved non-device-loss continuation permits one diagnostic renderer attempt only.'
        }
        if ($script:State.recoveryRequiredForAttempt -and
            $script:State.recoveryRequiredForAttempt -ne $script:State.recoveryVerifiedForAttempt)
        {
            throw 'The previous attempt still requires verified GPU recovery before continuing.'
        }
        $approvedStalledNonDevice = $true
    }
    elseif ($ApproveStalledNonDeviceLoss)
    {
        throw 'There is no stopped non-device-loss failure to approve.'
    }
    if ($script:State.phase -eq 'success' -and -not $PreflightOnly -and -not $Revalidate)
    {
        throw 'This repair loop already completed successfully; pass -Revalidate after further changes.'
    }
    if ($script:State.pendingApproval)
    {
        if (-not $ApproveStalledDeviceLoss)
        {
            throw 'User approval is required after three repeated device-loss repairs without presentation progress.'
        }
        $script:State.pendingApproval = $false
        Save-State
        Write-Timeline 'The user-approved device-loss continuation was recorded.'
    }
    elseif ($ApproveStalledDeviceLoss)
    {
        throw 'There is no pending device-loss approval to apply.'
    }

    if ($PreflightOnly)
    {
        $attempt = Join-Path $script:Root ('preflight-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
    }
    else
    {
        if ($MaxAttempts -gt 0 -and $script:State.attempts -ge $MaxAttempts) { throw "The $MaxAttempts-iteration repair budget is exhausted." }
        $attemptNumber = $script:State.attempts + 1
        $attemptName = 'attempt-{0:D2}' -f $attemptNumber
        $attempt = Join-Path $script:Root $attemptName
        $retry = 0
        while ([IO.Directory]::Exists($attempt) -or [IO.File]::Exists($attempt))
        {
            $retry++
            $attempt = Join-Path $script:Root ('{0}-build-retry-{1:D2}' -f $attemptName, $retry)
        }
        if ($approvedStalledNonDevice)
        {
            $script:State.stalledNonDeviceAttempt = $attempt
            Write-Timeline "User approved one non-device-loss diagnostic: iteration=$attemptNumber signature=$($script:State.lastSignature) repeats=$($script:State.repeatCount)."
        }
        $script:State.attempt = $attempt
        $script:State.phase = 'building'
        $script:State.buildPending = $true
        $script:State.gpu = if ($Backend -eq 'OpenGL') { $ExpectedGPU } else { '' }
        Save-State
    }
    [IO.Directory]::CreateDirectory($attempt) | Out-Null
    Save-SourceState $attempt
    Copy-Logs (Join-Path $attempt 'logs-before')

    $query = Join-Path $buildDirectory '.cmake\api\v1\query\client-divide-smoke\codemodel-v2'
    [IO.Directory]::CreateDirectory((Split-Path -Parent $query)) | Out-Null
    $queryCreated = -not [IO.File]::Exists($query)
    if ($queryCreated) { [IO.File]::WriteAllText($query, '') }

    $probe = Join-Path $attempt 'VulkanRecoveryProbe.exe'
    $fixture = Join-Path $attempt 'DebuggerFixture.exe'
    $fixtureSource = Join-Path $attempt 'DebuggerFixture.cpp'
    $fixtureText = @'
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "Core/Headers/SmokeRuntimeDialogs.h"
int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    if (std::strcmp(argv[1], "hang") == 0) { Sleep(30000); return 0; }
    if (std::strcmp(argv[1], "success") == 0) return 0;
    if (std::strcmp(argv[1], "abort") == 0)
    {
        if (!Divide::RenderSmokeTest::ConfigureNonInteractiveCRT()) return 4;
        std::abort();
    }
    if (std::strcmp(argv[1], "owned") == 0 && argc == 3)
    {
        const std::string temporary = std::string(argv[2]) + ".tmp";
        FILE* file = nullptr;
        if (fopen_s(&file, temporary.c_str(), "wb") != 0) return 3;
        const int written = std::fprintf(file, "%lu", GetCurrentProcessId());
        if (std::fclose(file) != 0 || written <= 0 ||
            !MoveFileExA(temporary.c_str(), argv[2], MOVEFILE_WRITE_THROUGH)) return 3;
        Sleep(30000);
        return 0;
    }
    if (std::strcmp(argv[1], "break") != 0 || argc != 3) return 2;
    FILE* file = nullptr;
    if (fopen_s(&file, argv[2], "wb") != 0) return 3;
    std::fputs("before\n", file);
    std::fflush(file);
    DebugBreak();
    std::fputs("resumed\n", file);
    std::fclose(file);
    return 23;
}
'@
    [IO.File]::WriteAllText($fixtureSource, $fixtureText, [Text.Encoding]::ASCII)

    $buildScript = Join-Path $attempt 'build.cmd'
    $probeSource = Join-Path $script:Repo 'Tools\VulkanRecoveryProbe.cpp'
    $probeObject = Join-Path $attempt 'VulkanRecoveryProbe.obj'
    $fixtureObject = Join-Path $attempt 'DebuggerFixture.obj'
    $fixturePDB = Join-Path $attempt 'DebuggerFixture.pdb'
    $engineInclude = Join-Path $script:Repo 'Source'
    $sdkInclude = Join-Path $env:VULKAN_SDK 'Include'
    $sdkLibrary = Join-Path $env:VULKAN_SDK 'Lib'
    $buildLines = @('@echo off')
    if (-not $reuseVisualStudioEnvironment)
    {
        $buildLines += @(
            ('call "' + $vcvars + '"'),
            'if errorlevel 1 exit /b %errorlevel%'
        )
    }
    $buildLines += @(
        ('cl /nologo /std:c++20 /EHsc /O2 /W4 /I"' + $sdkInclude + '" /Fo"' + $probeObject + '" /Fe"' + $probe +
            '" "' + $probeSource + '" /link /LIBPATH:"' + $sdkLibrary + '" vulkan-1.lib'),
        'if errorlevel 1 exit /b %errorlevel%',
        ('cl /nologo /std:c++20 /EHsc /W4 /MDd /Zi /I"' + $engineInclude + '" /Fd"' + $fixturePDB +
            '" /Fo"' + $fixtureObject + '" /Fe"' + $fixture + '" "' + $fixtureSource + '"'),
        'if errorlevel 1 exit /b %errorlevel%'
    )
    if (-not $PreflightOnly)
    {
        if ($queryCreated -or -not (Test-InstrumentedConfiguration $buildDirectory))
        {
            $buildLines += @(
                ('cmake --preset ' + $script:ConfigurePreset + ' -DBUILD_TESTING_INTERNAL=ON -DRUN_TESTING_INTERNAL=OFF'),
                'if errorlevel 1 exit /b %errorlevel%'
            )
        }
        else
        {
            Write-Timeline 'Reusing the verified instrumented CMake configuration; Ninja will regenerate changed build files.'
        }
        $buildTargets = if ($SkipUnitTests) { 'Divide-Framework' } else { 'Divide-Framework Divide-Test-Engine' }
        $buildLines += @(
            ('cmake --build --preset ' + $script:BuildPreset + ' --target ' + $buildTargets + ' --parallel ' + $BuildJobs),
            'if errorlevel 1 exit /b %errorlevel%'
        )
    }
    $buildLines += 'exit /b 0'
    [IO.File]::WriteAllLines($buildScript, $buildLines, [Text.Encoding]::ASCII)
    if ($PreflightOnly)
    {
        Write-Timeline 'Starting preflight.'
    }
    else
    {
        $attemptLabel = if ($MaxAttempts -eq 0) { "$attemptNumber (no total limit)" } else { "$attemptNumber of $MaxAttempts" }
        Write-Timeline "Starting build for iteration $attemptLabel."
    }
    $build = Start-OwnedProcess (Join-Path $attempt 'build') $env:ComSpec @('/d', '/c', $buildScript) $script:Repo
    $buildResult = Wait-OwnedProcess $build 2700
    if ($buildResult.TimedOut -or $buildResult.ExitCode -ne 0)
    {
        if (-not $PreflightOnly)
        {
            $script:State.buildPending = $false
            $script:State.phase = if ($approvedStalledNonDevice) { 'stopped_no_progress' } else { 'needs_fix' }
            $script:State.stalledNonDeviceAttempt = ''
            Save-State
            Write-Timeline "Build failed; its artifacts were preserved and iteration $attemptNumber was not charged."
        }
        throw "The probe/editor build failed or timed out (exit $($buildResult.ExitCode)); no renderer was started."
    }
    if (-not $PreflightOnly)
    {
        $script:State.attempts = $attemptNumber
        $script:State.buildPending = $false
        $script:State.phase = 'preflight'
        Save-State
    }

    $invalidProbe = Start-OwnedProcess (Join-Path $attempt 'invalid-probe') $probe @('--uuid=invalid') $script:Repo
    $invalidResult = Wait-OwnedProcess $invalidProbe 15
    if ($invalidResult.TimedOut -or $invalidResult.ExitCode -ne 2)
    {
        throw 'The recovery probe did not reject an invalid device UUID.'
    }
    $debugger = Copy-DebuggerRuntime $debuggerSource $attempt
    Test-NonInteractiveAbort $fixture $attempt
    Test-Debugger $debugger $fixture $attempt
    $hung = Start-OwnedProcess (Join-Path $attempt 'timeout-check') $fixture @('hang') $script:Repo
    $hungResult = Wait-OwnedProcess $hung 2
    if (-not $hungResult.TimedOut) { throw 'The owned-process timeout check failed.' }
    Write-Timeline 'The owned-process timeout check terminated its isolated helper.'

    if ($PreflightOnly)
    {
        Write-Timeline 'Preflight completed without launching the renderer.'
        exit 0
    }

    $engine = Get-TargetMetadata $buildDirectory 'Divide-Framework'
    $tests = if ($SkipUnitTests) { $null } else { Get-TargetMetadata $buildDirectory 'Divide-Test-Engine' }
    Assert-NoConflictingRenderer $engine.Executable
    $script:State.probe = $probe
    $script:State.renderer = $engine.Executable
    $script:State.phase = 'testing'
    Save-State
    $metadata = @{
        backend = $Backend
        expectedGPU = if ($Backend -eq 'OpenGL') { $ExpectedGPU } else { $null }
        project = $Project
        scene = $Scene
        targetPresents = $script:TargetPresents
        diagnosticOnly = $DiagnosticPresents -gt 0
        materialDebugView = if ($MaterialDebugView) { $MaterialDebugView } else { $null }
        editorPaused = [bool]$EditorPaused
        approvedStalledNonDeviceLoss = $approvedStalledNonDevice
        renderTimeoutSeconds = $RenderTimeoutSeconds
        maxAttempts = $MaxAttempts
        renderer = $engine.Executable
        rendererSha256 = (Get-FileHash -LiteralPath $engine.Executable -Algorithm SHA256).Hash
        tests = if ($tests) { $tests.Executable } else { $null }
        testsSha256 = if ($tests) { (Get-FileHash -LiteralPath $tests.Executable -Algorithm SHA256).Hash } else { $null }
        unitTestsSkipped = [bool]$SkipUnitTests
        debugger = $debugger
        debuggerSource = $debuggerSource
        debuggerSha256 = if ($debugger) { (Get-FileHash -LiteralPath $debugger -Algorithm SHA256).Hash } else { $null }
        amdCapture = if ($AmdCaptureExe) { $AmdCaptureExe } else { $null }
        renderdocInject = if ($RenderDocInjectExe) { $RenderDocInjectExe } else { $null }
        vulkanSdk = $env:VULKAN_SDK
        gpus = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, PNPDeviceID)
    } | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText((Join-Path $attempt 'environment.json'), $metadata, [Text.UTF8Encoding]::new($false))
    if ($tests)
    {
        $test = Start-OwnedProcess (Join-Path $attempt 'tests') $tests.Executable @('[render_smoke]') $tests.WorkingDirectory
        $testResult = Wait-OwnedProcess $test 120
        if ($testResult.TimedOut -or $testResult.ExitCode -ne 0)
        {
            $script:State.phase = if ($approvedStalledNonDevice) { 'stopped_no_progress' } else { 'needs_fix' }
            $script:State.stalledNonDeviceAttempt = ''
            Update-FailurePolicy 'smoke_policy_test_failure' 0 $false
            throw 'The focused engine tests failed; no renderer was started.'
        }
    }
    else { Write-Timeline 'Unit tests are deferred until the framework smoke test passes.' }

    Assert-NoConflictingRenderer $engine.Executable
    $runID = 'attempt-{0:D2}' -f $script:State.attempts
    $smokeMode = if ($Backend -eq 'OpenGL') { '--frameworkSmokeTest=OpenGL' } else { '--frameworkSmokeTest=Vulkan' }
    $arguments = @($smokeMode, '--smokeWindowCapture', '--smokeSupervisorGate',
                   "--smokeOutput=$attempt", "--smokeRun=$runID", "--smokeFrames=$script:TargetPresents")
    if ($Backend -eq 'OpenGL') { $arguments += "--smokeExpectedGPU=$ExpectedGPU" }
    if ($RenderDocInjectExe) { $arguments += '--smokeRenderDocCapture' }
    if ($Project) { $arguments += "--smokeProject=$Project" }
    if ($Scene) { $arguments += "--smokeScene=$Scene" }
    if ($MaterialDebugView) { $arguments += "--smokeMaterialView=$MaterialDebugView" }
    if ($EditorPaused) { $arguments += '--smokeEditorPaused' }
    if ($TracePipelines) { $arguments += '--smokePipelineTrace' }
    if ($PerfMode) { $arguments += '--smokePerfMode' }
    if ($GpuScopeTimings) { $arguments += '--smokeGpuScopeTimings' }
    $program = $engine.Executable
    if ($debugger)
    {
        $commandFile = Join-Path $attempt 'debugger-commands.txt'
        New-DebuggerScript $commandFile (Join-Path $attempt 'failure.dmp')
        $arguments += '--smokeDebugger'
        $arguments = @('-y', (Split-Path -Parent $engine.Executable), '-G', '-logo', (Join-Path $attempt 'debugger.log'), '-cf', $commandFile,
                       $engine.Executable) + $arguments
        $program = $debugger
    }

    Copy-Logs (Join-Path $attempt 'logs-immediately-before-launch')
    try
    {
        $amdCapture = if ($AmdCaptureExe)
                      {
                          Start-AmdCrashCapture $attempt ([IO.Path]::GetFileNameWithoutExtension($engine.Executable))
                      }
                      else { $null }
    }
    catch
    {
        $script:State.phase = if ($approvedStalledNonDevice) { 'stopped_no_progress' } else { 'needs_fix' }
        $script:State.stalledNonDeviceAttempt = ''
        Save-State
        throw
    }
    $script:State.phase = 'running'
    Save-State
    Write-Timeline "Launching protected $Backend renderer $runID."
    try
    {
        $renderer = Start-OwnedProcess (Join-Path $attempt 'renderer') $program $arguments $engine.WorkingDirectory
        $processResult = Wait-Renderer $renderer $attempt $debugger $amdCapture $engine.Executable
    }
    finally
    {
        $amdResult = if ($null -ne $amdCapture) { Stop-AmdCrashCapture $amdCapture $attempt } else { $null }
    }
    Copy-Logs (Join-Path $attempt 'logs-after')
    $progress = Get-Progress $attempt
    $failure = Read-KeyValues (Join-Path $attempt 'failure.txt')
    if (Confirm-Result $attempt $runID $processResult)
    {
        [IO.File]::Move((Join-Path $attempt 'editor.pending.png'), (Join-Path $attempt 'editor.png'))
        $script:State.phase = if ($DiagnosticPresents -gt 0) { 'needs_fix' } else { 'success' }
        $script:State.stalledNonDeviceAttempt = ''
        if ($DiagnosticPresents -gt 0)
        {
            $script:State.maximumPresents = [Math]::Max($script:State.maximumPresents, $progress.Presents)
            $script:State.repeatCount = 0
            $script:State.maximumDevicePresents = [Math]::Max($script:State.maximumDevicePresents, $progress.Presents)
            $script:State.deviceRepeatCount = 0
        }
        Save-State
        if ($DiagnosticPresents -gt 0)
        {
            Write-Timeline "The $Backend diagnostic completed after $script:TargetPresents presents and clean shutdown; the 300-present contract remains outstanding."
        }
        else
        {
            Write-Timeline "The $Backend presentation and capture contract completed; inspect the scene and editor images visually."
        }
        exit 0
    }

    $fallbackKind = if ($failure.ContainsKey('kind')) { $failure['kind'] }
                    elseif ($processResult.Reason) { $processResult.Reason }
                    else { 'unexpected_exit' }
    $kind = Get-FailureKind $attempt $fallbackKind
    $deviceLoss = $kind -in @('device_lost', 'gpu_hang', 'gpu_crash', 'driver_reset', 'render_timeout', 'shutdown_timeout')
    $reset = Record-DisplayReset $attempt $renderer.Started
    if ($reset -and -not $deviceLoss)
    {
        $kind = 'driver_reset'
        $deviceLoss = $true
    }
    $captureUncertain = $null -ne $amdResult -and ($amdResult.TimedOut -or $amdResult.ExitCode -ne 0)
    $needsRecovery = $deviceLoss -or $reset -or
                     (($kind -in @('startup_timeout', 'crash_capture_stopped', 'unexpected_exit') -or $captureUncertain) -and
                      ($progress.GPU -or $script:State.gpu))
    $signature = Get-FailureSignature $attempt $kind
    $script:State.phase = if ($approvedStalledNonDevice) { 'stopped_no_progress' } else { 'needs_fix' }
    if ($approvedStalledNonDevice) { $script:State.stalledNonDeviceAttempt = '' }
    $script:State.recoveryRequiredForAttempt = if ($needsRecovery) { $attempt } else { '' }
    if ($needsRecovery -and $approvedStalledNonDevice) { $script:State.recoveryContinuation = 'stopped_no_progress' }
    Update-FailurePolicy $signature $progress.Presents $deviceLoss $attempt
    Write-Timeline "Attempt failed: kind=$kind signature=$signature presents=$($progress.Presents)."
    if ($needsRecovery)
    {
        $stopForNoProgress = $script:State.phase -eq 'stopped_no_progress'
        $script:State.recoveryContinuation = if ($stopForNoProgress) { 'stopped_no_progress' } else { 'needs_fix' }
        Save-State
        $recoveryGPU = if ($progress.GPU) { $progress.GPU } else { $script:State.gpu }
        Wait-GPURecovery $recoveryGPU $probe $attempt
        $script:State.phase = $script:State.recoveryContinuation
        Save-State
        Write-Timeline 'GPU recovery was verified; repair the recorded failure before the next attempt.'
        if ($script:State.pendingApproval)
        {
            Write-Timeline 'User approval is required before another device-loss repair attempt.'
        }
        exit 87
    }
    if ($script:State.phase -eq 'stopped_no_progress')
    {
        Write-Timeline 'Stopping after three repeated repairs without presentation progress.'
    }
    exit 1
}
finally
{
    $globalLock.Dispose()
    $lock.Dispose()
}

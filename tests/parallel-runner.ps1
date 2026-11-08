# Process isolation is required: mod globals, COM apartments, fixture hooks and
# frame/background shutdown cannot be shared between concurrently running tests.
function ConvertTo-ProcessArgument([string]$Value) {
    # Windows argv quoting, with no command shell or expression evaluation.
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}

function Assert-RegressionExecutionContext {
    if (!('RegressionExecutionToken' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class RegressionExecutionToken {
    [DllImport("advapi32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsTokenRestricted(IntPtr token);
    [DllImport("advapi32.dll", SetLastError=true)]
    static extern bool GetTokenInformation(IntPtr token, int kind, out int value, int size, out int needed);
    public static bool IsAppContainer(IntPtr token) {
        int value, needed;
        if (!GetTokenInformation(token, 29, out value, sizeof(int), out needed))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return value != 0;
    }
}
'@
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        if ([RegressionExecutionToken]::IsTokenRestricted($identity.Token) -or
            [RegressionExecutionToken]::IsAppContainer($identity.Token)) {
            throw 'PENDING: restricted Windows execution token detected. Run build.bat from an ordinary terminal, or request an unrestricted rerun from the execution host. The build cannot remove its own sandbox restrictions.'
        }
    } finally { $identity.Dispose() }
}

function New-RegressionResultDirectory([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        if (!(Test-Path -LiteralPath $Path -PathType Container) -or
            @(Get-ChildItem -LiteralPath $Path -Force | Select-Object -First 1).Count) {
            throw "Use a fresh, empty result directory to preserve earlier evidence: $Path"
        }
    }
    $null = New-Item -ItemType Directory -Path $Path -Force
}

# Runtime policy only. Compiler failures never enter this retry gate.
function Invoke-RegressionGate {
    param(
        [Parameter(Mandatory=$true)][object[]]$Tasks,
        [Parameter(Mandatory=$true)][string]$OutputDirectory,
        [ValidateRange(1,32)][int]$Jobs = 1,
        [ValidateRange(1,86400)][int]$TimeoutSeconds = 900,
        [switch]$ObserveInterference
    )
    $attempts = [Collections.Generic.List[object]]::new()
    $initial = @(Invoke-RegressionTasks -Tasks $Tasks -OutputDirectory $OutputDirectory -Jobs $Jobs `
        -Phase regression -TimeoutSeconds $TimeoutSeconds -CollectResults -ObserveInterference:$ObserveInterference)
    foreach ($result in $initial) {
        $result | Add-Member -NotePropertyName Attempt -NotePropertyValue 0
        $attempts.Add($result)
    }
    $unsafe = @($initial | Where-Object { $_.Unreaped -or $_.Reason -like 'Output*' }).Count -gt 0
    foreach ($task in $Tasks) {
        $first = $initial | Where-Object Name -eq $task.Name
        if ($first.Status -eq 'Pass' -or $unsafe) { continue }
        # All three isolated attempts run, including after recovery or a second
        # failure. Separate directories preserve every stdout/stderr and receipt.
        foreach ($attempt in 1..3) {
            $directory = Join-Path $OutputDirectory ("isolated-$attempt\"+$task.Name)
            $null = New-Item -ItemType Directory -Path $directory -Force
            $retry = @(Invoke-RegressionTasks -Tasks @($task) -OutputDirectory $directory -Jobs 1 `
                -Phase regression -TimeoutSeconds $TimeoutSeconds -CollectResults -ObserveInterference:$ObserveInterference)
            foreach ($result in $retry) {
                $result | Add-Member -NotePropertyName Attempt -NotePropertyValue $attempt
                $attempts.Add($result)
                if ($result.Unreaped -or $result.Reason -like 'Output*') { $unsafe = $true }
            }
            if ($unsafe) { break }
        }
    }
    $summary = foreach ($task in $Tasks) {
        $history = @($attempts | Where-Object Name -eq $task.Name)
        $retries = @($history | Where-Object Attempt -gt 0)
        $failed = @($retries | Where-Object Status -eq Fail).Count
        $pending = @($history | Where-Object Status -eq Pending).Count
        $status = if ($failed -ge 2) { 'Fail' }
            elseif ($pending -or ($history[0].Status -ne 'Pass' -and $retries.Count -lt 3)) { 'Pending' }
            else { 'Pass' }
        [pscustomobject]@{ Name=$task.Name; Status=$status; Attempts=$history.Count
            FailedRetries=$failed; PendingAttempts=$pending
            Recovered=($status -eq 'Pass' -and $history[0].Status -ne 'Pass') }
    }
    $attempts | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'regression-attempts.csv') -NoTypeInformation
    $summary | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'regression-summary.csv') -NoTypeInformation
    $failedTests = @($summary | Where-Object Status -eq Fail)
    $pendingTests = @($summary | Where-Object { $_.Status -eq 'Pending' -or $_.PendingAttempts })
    $status = if ($failedTests.Count) { 'Fail' } elseif ($pendingTests.Count) { 'Pending' } else { 'Pass' }
    $report = [pscustomobject]@{ Status=$status; Failed=$failedTests.Count; Pending=$pendingTests.Count
        Passed=@($summary | Where-Object Status -eq Pass).Count; Tests=@($summary); Attempts=$attempts.ToArray() }
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'regression-report.json') -Encoding UTF8
    $summary | Format-Table Name,Status,Attempts,FailedRetries,PendingAttempts,Recovered -AutoSize | Out-Host
    Write-Host "RESULT: $status ($($report.Passed) passed, $($report.Failed) failed, $($report.Pending) pending). Reports: $OutputDirectory"
    if ($status -ne 'Pass') {
        throw "$($status.ToUpperInvariant()): regression gate; failed: $($failedTests.Name -join ', '); pending: $($pendingTests.Name -join ', '). Reports: $OutputDirectory"
    }
}

function Get-ExitDescription([int]$Code) {
    $known = @{
        0xC0000005 = 'access violation'; 0xC0000409 = 'fail-fast/stack buffer overrun';
        0xC00000FD = 'stack overflow'; 0xC0000374 = 'heap corruption'; 0xC000001D = 'illegal instruction';
        0xC0000094 = 'integer divide by zero'; 0xC0000420 = 'assertion failure'; 0x80000003 = 'breakpoint'
    }
    $unsigned = [uint32]([int64]$Code -band 0xFFFFFFFFL) # Unsuffixed 0xFFFFFFFF is Int32 -1.
    if ($known.ContainsKey([int64]$unsigned)) { return ('0x{0:X8} {1}' -f $unsigned, $known[[int64]$unsigned]) }
    if ($Code -eq 3) { return 'abort' }
    "$Code"
}

# Positive evidence only: an installed/running Windhawk service does not mean
# that a private test executable was injected. Observation is best effort.
function Get-TestInterference([Diagnostics.Process]$Process) {
    try {
        $Process.Refresh()
        @($Process.Modules | Where-Object {
            $_.ModuleName -match '^(windhawk(?:[._-].*)?|local@.*)\.dll$'
        } | ForEach-Object { $_.FileName })
    } catch { } # A short-lived process can exit while its modules are enumerated.
}

function Stop-RegressionProcess([Diagnostics.Process]$Process, [int]$CleanupTimeoutSeconds) {
    if ($Process.HasExited) { return $true }
    # taskkill includes descendants. Bound the helper itself as well as the
    # child: restricted execution hosts can prevent either operation.
    $killer = [Diagnostics.Process]::new()
    try {
        $killer.StartInfo = [Diagnostics.ProcessStartInfo]::new()
        $killer.StartInfo.FileName = Join-Path $env:SystemRoot 'System32\taskkill.exe'
        $killer.StartInfo.Arguments = "/PID $($Process.Id) /T /F"
        $killer.StartInfo.UseShellExecute = $false
        $killer.StartInfo.CreateNoWindow = $true
        $killer.StartInfo.RedirectStandardOutput = $true
        $killer.StartInfo.RedirectStandardError = $true
        if ($killer.Start()) {
            $out = $killer.StandardOutput.ReadToEndAsync()
            $err = $killer.StandardError.ReadToEndAsync()
            if (!$killer.WaitForExit($CleanupTimeoutSeconds*1000)) {
                try { $killer.Kill() } catch { }
            }
        }
    } catch { } finally { $killer.Dispose() }
    if (!$Process.HasExited) { try { $Process.Kill() } catch { } }
    $Process.WaitForExit($CleanupTimeoutSeconds*1000)
}

# Runs every task whose Serial flag is false with up to -Jobs processes, then
# runs Serial tasks one at a time with nothing else running. All tasks run even
# after a failure; every failure is reported together at the end.
function Invoke-RegressionTasks {
    param(
        [Parameter(Mandatory=$true)][object[]]$Tasks,
        [Parameter(Mandatory=$true)][string]$OutputDirectory,
        [ValidateRange(1,32)][int]$Jobs = 1,
        [ValidatePattern('^[a-z0-9-]+$')][string]$Phase = 'tasks',
        [ValidateRange(1,86400)][int]$TimeoutSeconds = 1800,
        [ValidateRange(1,60)][int]$CleanupTimeoutSeconds = 10,
        [switch]$ObserveInterference,
        [switch]$CollectResults
    )
    $names = @{}
    foreach ($task in $Tasks) {
        if ($task.Name -notmatch '^[a-z0-9-]+$' -or $names.ContainsKey($task.Name)) {
            throw "Invalid or duplicate task name: $($task.Name)"
        }
        $names[$task.Name] = $true
    }
    $pending = [Collections.Generic.Queue[object]]::new()
    foreach ($task in @($Tasks | Where-Object { !$_.Serial }) + @($Tasks | Where-Object { $_.Serial })) {
        $pending.Enqueue($task)
    }
    $running = [Collections.Generic.List[object]]::new()
    $results = [Collections.Generic.List[object]]::new()
    $batchClock = [Diagnostics.Stopwatch]::StartNew()
    $startFailure = $null
    try {
        while ($pending.Count -or $running.Count) {
            while (!$startFailure -and $pending.Count -and $running.Count -lt $Jobs) {
                # A serial task waits for every parallel task and runs alone.
                if ($pending.Peek().Serial -and $running.Count) { break }
                if ($running.Count -and $running[0].Task.Serial) { break }
                $task = $pending.Dequeue()
                if ($task.Serial) { Write-Host "SERIAL: $($task.Name) (no other test running)" }
                $process = [Diagnostics.Process]::new()
                $process.StartInfo = [Diagnostics.ProcessStartInfo]::new()
                $process.StartInfo.FileName = $task.FilePath
                $process.StartInfo.Arguments = (@($task.Arguments | ForEach-Object {
                    ConvertTo-ProcessArgument ([string]$_)
                }) -join ' ')
                $process.StartInfo.WorkingDirectory = $OutputDirectory
                $process.StartInfo.UseShellExecute = $false
                $process.StartInfo.CreateNoWindow = $true
                $process.StartInfo.RedirectStandardOutput = $true
                $process.StartInfo.RedirectStandardError = $true
                $clock = [Diagnostics.Stopwatch]::StartNew()
                try {
                    if (!$process.Start()) { throw "Could not start $($task.FilePath)" }
                } catch {
                    # Drain already started children before reporting.
                    $process.Dispose()
                    $startFailure = $_
                    $results.Add([pscustomobject]@{
                        Name=$task.Name; Serial=[bool]$task.Serial; ExitCode=-1; Seconds=0; TimedOut=$false
                        Stdout=''; Stderr=''; Status='Pending'; Reason="Process launch blocked: $_"
                        Modules=''; ProcessId=0; Unreaped=$false
                    })
                    break
                }
                # Read both pipes immediately: a full stderr pipe must never
                # block a compiler while its stdout is being drained (or vice versa).
                $stdoutPath = Join-Path $OutputDirectory ($task.Name + '.stdout.log')
                $stderrPath = Join-Path $OutputDirectory ($task.Name + '.stderr.log')
                $outFile = [IO.File]::Open($stdoutPath,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
                $errFile = [IO.File]::Open($stderrPath,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
                $stdout = $process.StandardOutput.BaseStream.CopyToAsync($outFile)
                $stderr = $process.StandardError.BaseStream.CopyToAsync($errFile)
                $wait = [Threading.ManualResetEvent]::new($false)
                $wait.SafeWaitHandle = [Microsoft.Win32.SafeHandles.SafeWaitHandle]::new($process.Handle,$false)
                $running.Add([pscustomobject]@{
                    Task=$task; Process=$process; Wait=$wait; Stdout=$stdout; Stderr=$stderr; Clock=$clock
                    OutFile=$outFile; ErrFile=$errFile; OutPath=$stdoutPath; ErrPath=$stderrPath; TimedOut=$false
                    Modules=@(); Blocked=$false
                })
                Write-Host "START: $($task.Name)"
            }
            if (!$running.Count) { break }
            # Module snapshots are diagnostic sampling, independent of test
            # deadlines. Never infer interference merely from an assertion.
            if ($ObserveInterference) {
                foreach ($child in $running) {
                    $child.Modules = @($child.Modules + @(Get-TestInterference $child.Process) | Select-Object -Unique)
                }
                # An injected engine invalidates every result of this run, not
                # just one test: stop all tests now instead of finishing and
                # retrying them. The finally block reaps the running processes.
                $injected = @($running | Where-Object { $_.Modules.Count })
                if ($injected.Count) {
                    throw "PENDING: Windhawk (or another mod engine) injected into test process $($injected[0].Task.Name): " +
                        "$($injected[0].Modules -join '; '). Results would be invalid, so the run was cancelled. " +
                        "Exclude the regression executables from Windhawk (or pause it), then rerun. Logs: $OutputDirectory"
                }
            }
            $remaining = ($running | ForEach-Object { $TimeoutSeconds*1000 - $_.Clock.ElapsedMilliseconds } |
                Measure-Object -Minimum).Minimum
            $index = [Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]@($running | ForEach-Object { $_.Wait }),
                [int][Math]::Max(0,[Math]::Min($(if ($ObserveInterference) { 1000 } else { [int]::MaxValue }),$remaining)))
            if ($index -eq [Threading.WaitHandle]::WaitTimeout) {
                # A hung test is a failure. Stop only that test's process tree;
                # its logs remain for diagnosis.
                foreach ($child in @($running)) {
                    if ($child.Clock.ElapsedMilliseconds -ge $TimeoutSeconds*1000 -and !$child.TimedOut) {
                        $child.TimedOut = $true
                        Write-Host "TIMEOUT: $($child.Task.Name) exceeded $TimeoutSeconds s"
                        $child.Blocked = !(Stop-RegressionProcess $child.Process $CleanupTimeoutSeconds)
                        if ($child.Blocked) {
                            # Do not overlap isolated retries with an unreaped
                            # process. Drain other peers, leave the gate pending.
                            $startFailure = 'Process termination blocked; run from an unrestricted terminal or request execution-host approval.'
                            $index = $running.IndexOf($child)
                            break
                        }
                    }
                }
                if ($index -eq [Threading.WaitHandle]::WaitTimeout) { continue }
            }
            $child = $running[$index]
            $child.Clock.Stop()
            $pipeFailure = ''
            try {
                $drained = [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($child.Stdout,$child.Stderr),$CleanupTimeoutSeconds*1000)
                if (!$drained) { $pipeFailure = 'Output pipes remain open after cleanup (possibly an inherited child handle).' }
            } catch { $pipeFailure = "Output capture failed: $_" }
            if ($pipeFailure) {
                $child.Process.StandardOutput.Dispose(); $child.Process.StandardError.Dispose()
                $startFailure = $pipeFailure
            }
            $child.OutFile.Dispose(); $child.ErrFile.Dispose()
            $output = [IO.File]::ReadAllText($child.OutPath)
            $errors = [IO.File]::ReadAllText($child.ErrPath)
            $code = if ($child.Blocked) { -1 } else { $child.Process.ExitCode }
            if ($child.TimedOut -and !$code) { $code = -1 }
            $results.Add([pscustomobject]@{
                Name=$child.Task.Name; Serial=[bool]$child.Task.Serial; ExitCode=$code
                Seconds=$child.Clock.Elapsed.TotalSeconds; TimedOut=$child.TimedOut
                Stdout=$child.OutPath; Stderr=$child.ErrPath
                Status=$(if ($child.Blocked -or $pipeFailure -or $child.Modules.Count) { 'Pending' } elseif ($code) { 'Fail' } else { 'Pass' })
                Reason=$(if ($child.Blocked) { [string]$startFailure } elseif ($pipeFailure) { $pipeFailure }
                    elseif ($child.Modules.Count) { 'Injected modules observed; controlled test result is pending.' }
                    elseif ($child.TimedOut) { 'timeout' } elseif ($code) { 'exit '+(Get-ExitDescription $code) } else { '' })
                Modules=($child.Modules -join '; '); ProcessId=$child.Process.Id; Unreaped=$child.Blocked
            })
            # Emit one complete log at a time so concurrent tests remain readable.
            if ($output) { Write-Host $output.TrimEnd() }
            if ($errors) { Write-Host $errors.TrimEnd() }
            $status = if ($child.TimedOut) { 'timeout' } elseif ($code) { Get-ExitDescription $code } else { '0' }
            Write-Host ('DONE: {0} exit={1} elapsed={2:N2}s' -f $child.Task.Name,$status,$child.Clock.Elapsed.TotalSeconds)
            $child.Wait.Dispose()
            $child.Process.Dispose()
            $running.RemoveAt($index)
        }
    } finally {
        # On infrastructure errors or interruption, drain already started
        # children before binaries can be removed. Never kill provider threads.
        foreach ($child in $running) {
            try {
                $null = Stop-RegressionProcess $child.Process $CleanupTimeoutSeconds
                $null = [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($child.Stdout,$child.Stderr),$CleanupTimeoutSeconds*1000)
            } finally {
                $child.Process.StandardOutput.Dispose(); $child.Process.StandardError.Dispose()
                $child.OutFile.Dispose(); $child.ErrFile.Dispose()
                $child.Wait.Dispose(); $child.Process.Dispose()
            }
        }
    }
    foreach ($task in $pending) {
        $results.Add([pscustomobject]@{
            Name=$task.Name; Serial=[bool]$task.Serial; ExitCode=-1; Seconds=0; TimedOut=$false
            Stdout=''; Stderr=''; Status='Pending'; Reason="Not started after infrastructure problem: $startFailure"
            Modules=''; ProcessId=0; Unreaped=$false
        })
    }
    $results | Export-Csv -LiteralPath (Join-Path $OutputDirectory ($Phase+'-results.csv')) -NoTypeInformation
    $parallelCount = @($results | Where-Object { !$_.Serial }).Count
    Write-Host ('Completed {0} tasks ({1} parallel with up to {2} processes, {3} serial) in {4:N2}s' -f
        $results.Count,$parallelCount,$Jobs,($results.Count-$parallelCount),$batchClock.Elapsed.TotalSeconds)
    if ($CollectResults) { return $results.ToArray() }
    $blocked = @($results | Where-Object Status -eq Pending)
    if ($blocked.Count) { throw "PENDING: $($blocked.Name -join ', '). $($blocked.Reason -join '; ') Logs retained at $OutputDirectory" }
    $failures = @($results | Where-Object { $_.ExitCode -ne 0 })
    if ($failures.Count) {
        throw "Failed tasks: $(($failures | ForEach-Object {
            "$($_.Name) ($(if ($_.TimedOut) { 'timeout' } else { 'exit ' + (Get-ExitDescription $_.ExitCode) }))"
        }) -join ', '). Logs retained at $OutputDirectory"
    }
}

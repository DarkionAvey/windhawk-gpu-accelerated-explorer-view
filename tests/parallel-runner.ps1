# Process isolation is required: mod globals, COM apartments, fixture hooks and
# frame/background shutdown cannot be shared between concurrently running tests.
function ConvertTo-ProcessArgument([string]$Value) {
    # Windows argv quoting, with no command shell or expression evaluation.
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}

function Get-ExitDescription([int]$Code) {
    $known = @{
        0xC0000005 = 'access violation'; 0xC0000409 = 'fail-fast/stack buffer overrun';
        0xC00000FD = 'stack overflow'; 0xC0000374 = 'heap corruption'; 0xC000001D = 'illegal instruction';
        0xC0000094 = 'integer divide by zero'; 0xC0000420 = 'assertion failure'; 0x80000003 = 'breakpoint'
    }
    $unsigned = [uint32]([int64]$Code -band 0xFFFFFFFF)
    if ($known.ContainsKey([int64]$unsigned)) { return ('0x{0:X8} {1}' -f $unsigned, $known[[int64]$unsigned]) }
    if ($Code -eq 3) { return 'abort' }
    "$Code"
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
        [ValidateRange(1,86400)][int]$TimeoutSeconds = 1800
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
                })
                Write-Host "START: $($task.Name)"
            }
            if (!$running.Count) { break }
            # The process handle signals on exit. The wait is bounded only by
            # the oldest child's remaining timeout, never by a polling interval.
            $remaining = ($running | ForEach-Object { $TimeoutSeconds*1000 - $_.Clock.ElapsedMilliseconds } |
                Measure-Object -Minimum).Minimum
            $index = [Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]@($running | ForEach-Object { $_.Wait }),
                [int][Math]::Max(0,[Math]::Min([int]::MaxValue,$remaining)))
            if ($index -eq [Threading.WaitHandle]::WaitTimeout) {
                # A hung test is a failure. Stop only that test's process tree;
                # its logs remain for diagnosis.
                foreach ($child in $running) {
                    if ($child.Clock.ElapsedMilliseconds -ge $TimeoutSeconds*1000 -and !$child.TimedOut) {
                        $child.TimedOut = $true
                        Write-Host "TIMEOUT: $($child.Task.Name) exceeded $TimeoutSeconds s"
                        & taskkill.exe /PID $child.Process.Id /T /F *> $null
                    }
                }
                continue
            }
            $child = $running[$index]
            $child.Process.WaitForExit()
            $child.Clock.Stop()
            $null = $child.Stdout.GetAwaiter().GetResult()
            $null = $child.Stderr.GetAwaiter().GetResult()
            $child.OutFile.Dispose(); $child.ErrFile.Dispose()
            $output = [IO.File]::ReadAllText($child.OutPath)
            $errors = [IO.File]::ReadAllText($child.ErrPath)
            $code = $child.Process.ExitCode
            if ($child.TimedOut -and !$code) { $code = -1 }
            $results.Add([pscustomobject]@{
                Name=$child.Task.Name; Serial=[bool]$child.Task.Serial; ExitCode=$code
                Seconds=$child.Clock.Elapsed.TotalSeconds; TimedOut=$child.TimedOut
                Stdout=$child.OutPath; Stderr=$child.ErrPath
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
                $child.Process.WaitForExit()
                $null = $child.Stdout.GetAwaiter().GetResult()
                $null = $child.Stderr.GetAwaiter().GetResult()
            } finally {
                $child.OutFile.Dispose(); $child.ErrFile.Dispose()
                $child.Wait.Dispose(); $child.Process.Dispose()
            }
        }
    }
    $results | Export-Csv -LiteralPath (Join-Path $OutputDirectory ($Phase+'-results.csv')) -NoTypeInformation
    $parallelCount = @($results | Where-Object { !$_.Serial }).Count
    Write-Host ('Completed {0} tasks ({1} parallel with up to {2} processes, {3} serial) in {4:N2}s' -f
        $results.Count,$parallelCount,$Jobs,($results.Count-$parallelCount),$batchClock.Elapsed.TotalSeconds)
    if ($startFailure) { throw $startFailure }
    $failures = @($results | Where-Object { $_.ExitCode -ne 0 })
    if ($failures.Count) {
        throw "Failed tasks: $(($failures | ForEach-Object {
            "$($_.Name) ($(if ($_.TimedOut) { 'timeout' } else { 'exit ' + (Get-ExitDescription $_.ExitCode) }))"
        }) -join ', '). Logs retained at $OutputDirectory"
    }
}

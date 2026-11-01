# Process isolation is required: mod globals, COM apartments, fixture hooks and
# frame/background shutdown cannot be shared between concurrently running suites.
function ConvertTo-ProcessArgument([string]$Value) {
    # Windows argv quoting, with no command shell or expression evaluation.
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}

function Invoke-RegressionTasks {
    param(
        [Parameter(Mandatory=$true)][object[]]$Tasks,
        [Parameter(Mandatory=$true)][string]$OutputDirectory,
        [ValidateRange(1,32)][int]$Jobs = 1,
        [ValidatePattern('^[a-z0-9-]+$')][string]$Phase = 'tasks'
    )
    $pending = [Collections.Generic.Queue[object]]::new()
    $names = @{}
    foreach ($task in $Tasks) {
        if ($task.Name -notmatch '^[a-z0-9-]+$' -or $names.ContainsKey($task.Name)) {
            throw "Invalid or duplicate task name: $($task.Name)"
        }
        $names[$task.Name] = $true
        $pending.Enqueue($task)
    }
    $running = [Collections.Generic.List[object]]::new()
    $results = [Collections.Generic.List[object]]::new()
    $batchClock = [Diagnostics.Stopwatch]::StartNew()
    try {
        while ($pending.Count -or $running.Count) {
            while ($pending.Count -and $running.Count -lt $Jobs) {
                if ($running.Count -and ($pending.Peek().Exclusive -or $running[0].Task.Exclusive)) { break }
                $task = $pending.Dequeue()
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
                    $process.Dispose()
                    throw
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
                    OutFile=$outFile; ErrFile=$errFile; OutPath=$stdoutPath; ErrPath=$stderrPath
                })
                Write-Host "START: $($task.Name)"
            }
            # The process handle signals on exit. No polling interval or
            # head-of-line wait on a slower, earlier child is involved.
            $index = [Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]@($running | ForEach-Object { $_.Wait }))
            $child = $running[$index]
            $child.Process.WaitForExit()
            $child.Clock.Stop()
            $null = $child.Stdout.GetAwaiter().GetResult()
            $null = $child.Stderr.GetAwaiter().GetResult()
            $child.OutFile.Dispose(); $child.ErrFile.Dispose()
            $stdoutPath = $child.OutPath; $stderrPath = $child.ErrPath
            $output = [IO.File]::ReadAllText($stdoutPath)
            $errors = [IO.File]::ReadAllText($stderrPath)
            $code = $child.Process.ExitCode
            $results.Add([pscustomobject]@{
                Name=$child.Task.Name; ExitCode=$code; Seconds=$child.Clock.Elapsed.TotalSeconds
                Stdout=$stdoutPath; Stderr=$stderrPath
            })
            # Emit one complete log at a time so concurrent suites remain readable.
            if ($output) { Write-Host $output.TrimEnd() }
            if ($errors) { Write-Host $errors.TrimEnd() }
            Write-Host ('DONE: {0} exit={1} elapsed={2:N2}s' -f $child.Task.Name,$code,$child.Clock.Elapsed.TotalSeconds)
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
    Write-Host ('Completed {0} tasks with up to {1} processes in {2:N2}s' -f $results.Count,$Jobs,$batchClock.Elapsed.TotalSeconds)
    $failures = @($results | Where-Object ExitCode -ne 0)
    if ($failures.Count) {
        throw "Failed tasks: $(($failures | ForEach-Object { "$($_.Name) (exit $($_.ExitCode))" }) -join ', '). Logs retained at $OutputDirectory"
    }
}

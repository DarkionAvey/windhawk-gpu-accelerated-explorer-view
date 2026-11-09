$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'parallel-runner.ps1')
$temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$fixture = Join-Path $temporary ('gpu-parallel-runner-'+[guid]::NewGuid().ToString('N')+' space [fixture] '+[char]0x062A)
New-Item -ItemType Directory -Path $fixture | Out-Null
$passed=$false
try {
    $program = Join-Path $fixture 'child script.ps1'
    $text = @'
param([string]$Mode,[string]$Root,[string]$Identity,[string]$Value)
$ErrorActionPreference='Stop'
$start=[Diagnostics.Stopwatch]::GetTimestamp()
if($Mode -like 'peer-*') {
    $a=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$Identity+'-a')
    $b=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$Identity+'-b')
    try {
        if($Mode -eq 'peer-a') { $null=$a.Set(); $ready=$b.WaitOne(10000) }
        else { $null=$b.Set(); $ready=$a.WaitOne(10000) }
        if(!$ready) { throw 'independent children did not overlap' }
    } finally { $a.Dispose(); $b.Dispose() }
}
if($Mode -eq 'pipes') {
    [Console]::Out.Write([string]::new([char]'O',131072))
    [Console]::Error.Write([string]::new([char]'E',131072))
}
if($Mode -eq 'hang' -or $Mode -eq 'gate-hang') { $null=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$Identity+'-never').WaitOne() }
if($Mode -eq 'interfered') {
    Add-Type -Name PrivateModuleLoader -Namespace Regression -MemberDefinition '[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet=System.Runtime.InteropServices.CharSet.Unicode, SetLastError=true)] public static extern System.IntPtr LoadLibraryW(string path);'
    if([Regression.PrivateModuleLoader]::LoadLibraryW((Join-Path $Root 'windhawk-fixture.dll')) -eq [IntPtr]::Zero) {throw 'Could not load private observation fixture'}
    $null=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$Identity+'-interfered').WaitOne()
}
$record=@{Start=$start; End=[Diagnostics.Stopwatch]::GetTimestamp(); Value=$Value}
[IO.File]::WriteAllText((Join-Path $Root ($Mode+'.json')),($record | ConvertTo-Json))
if($Mode -eq 'failure') { exit 7 }
if($Mode -like 'retry-*') {
    $counter=Join-Path $Root ($Mode+'.count')
    $attempt=if(Test-Path -LiteralPath $counter) { [int][IO.File]::ReadAllText($counter) } else { 0 }
    [IO.File]::WriteAllText($counter,[string]($attempt+1))
    [IO.File]::WriteAllText((Join-Path $Root ($Mode+'.'+$attempt+'.json')),($record | ConvertTo-Json))
    Write-Output "$Mode attempt $attempt"
    $failures=@{'retry-clean'=0; 'retry-recover'=1; 'retry-one'=2; 'retry-two'=3; 'retry-persistent'=4}
    if($attempt -lt $failures[$Mode]) { exit 7 }
}
'@
    [IO.File]::WriteAllText($program,$text,[Text.UTF8Encoding]::new($true))
    $shell = Join-Path $PSHOME 'powershell.exe'
    if (!(Test-Path -LiteralPath $shell)) { $shell = Join-Path $PSHOME 'pwsh.exe' }
    $identity = 'gpu-parallel-'+[guid]::NewGuid().ToString('N')
    $value = 'spaces "quotes" slash-before-quote \" trailing-slash\ ; $() ` '+[char]0x062A
    function Task([string]$Mode,[bool]$Serial=$false) {
        [pscustomobject]@{Name=$Mode; FilePath=$shell; Serial=$Serial
            Arguments=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',$program,$Mode,$fixture,$identity,$value)}
    }
    function Record([string]$Mode) { [IO.File]::ReadAllText((Join-Path $fixture ($Mode+'.json'))) | ConvertFrom-Json }
    # Serial tasks are listed among parallel ones; the runner must still finish
    # every parallel task first and then run each serial task alone.
    $tasks = @((Task 'peer-a'),(Task 'serial-one' $true),(Task 'peer-b'),(Task 'serial-two' $true),(Task 'after'),(Task 'pipes'))
    # Suppress the intentionally large pipe output, while retaining the logs.
    Invoke-RegressionTasks -Tasks $tasks -OutputDirectory $fixture -Jobs 2 *> $null
    $parallelEnd = (@('peer-a','peer-b','after','pipes') | ForEach-Object { (Record $_).End } | Measure-Object -Maximum).Maximum
    $one = Record 'serial-one'; $two = Record 'serial-two'
    if ($one.Start -lt $parallelEnd -or $two.Start -lt $parallelEnd) { throw 'Serial task started before the parallel phase drained' }
    if ($two.Start -lt $one.End -and $one.Start -lt $two.End) { throw 'Serial tasks overlapped each other' }
    $a = Record 'peer-a'; $b = Record 'peer-b'
    if ($a.Value -cne $value -or $b.Value -cne $value) { throw 'Native argument quoting changed the payload' }
    foreach ($stream in @('stdout','stderr')) {
        if ((Get-Item -LiteralPath (Join-Path $fixture ('pipes.'+$stream+'.log'))).Length -lt 131072) {
            throw "Redirected $stream pipe was truncated"
        }
    }
    $rejected=$false
    try {
        Invoke-RegressionTasks -Tasks @((Task 'failure'),(Task 'survivor'),(Task 'late-serial' $true)) -OutputDirectory $fixture -Jobs 2 *> $null
    } catch { $rejected=$_.Exception.Message -like '*failure (exit 7)*' }
    if (!$rejected -or !(Test-Path -LiteralPath (Join-Path $fixture 'survivor.json')) -or
        !(Test-Path -LiteralPath (Join-Path $fixture 'late-serial.json'))) {
        throw 'Failed child did not fail the gate after running its peers and the serial phase'
    }
    $results = @(Import-Csv -LiteralPath (Join-Path $fixture 'tasks-results.csv'))
    if ($results.Count -ne 3 -or ($results | Where-Object Name -eq 'failure').ExitCode -ne 7 -or
        !(Test-Path -LiteralPath (Join-Path $fixture 'failure.stderr.log'))) {
        throw 'Failure diagnostics were not retained'
    }
    $rejected=$false
    try {
        Invoke-RegressionTasks -Tasks @((Task 'hang'),(Task 'beside-hang')) -OutputDirectory $fixture -Jobs 2 -TimeoutSeconds 5 *> $null
    } catch { $rejected=$_.Exception.Message -like '*hang (timeout)*' }
    if (!$rejected -or !(Test-Path -LiteralPath (Join-Path $fixture 'beside-hang.json'))) {
        throw 'A hung child did not time out as a failure while its peer completed'
    }
    $rejected=$false
    $missing=[pscustomobject]@{Name='missing'; FilePath=(Join-Path $fixture 'missing.exe'); Arguments=@(); Serial=$false}
    try {
        Invoke-RegressionTasks -Tasks @((Task 'drained'),$missing) -OutputDirectory $fixture -Jobs 2 *> $null
    } catch { $rejected=$true }
    if (!$rejected -or !(Test-Path -LiteralPath (Join-Path $fixture 'drained.json')) -or
        !(Test-Path -LiteralPath (Join-Path $fixture 'drained.stdout.log'))) {
        throw 'Process startup failure did not drain and log its running peer'
    }
    Invoke-RegressionTasks -Tasks @((Task 'serial-a'),(Task 'serial-b')) -OutputDirectory $fixture -Jobs 1 *> $null
    $a = Record 'serial-a'; $b = Record 'serial-b'
    if ($b.Start -lt $a.End) { throw '-Jobs 1 overlapped children' }
    # The gate runs each test once; the first failure cancels a running peer
    # (here one that would otherwise hang to its timeout) and starts nothing
    # else, including the serial phase.
    $gateDirectory = Join-Path $fixture 'gate'
    $null=New-Item -ItemType Directory -Path $gateDirectory
    $rejected=$false
    $clock=[Diagnostics.Stopwatch]::StartNew()
    try {
        Invoke-RegressionGate -Tasks @((Task 'retry-two'),(Task 'gate-hang'),(Task 'gate-serial' $true)) -OutputDirectory $gateDirectory -Jobs 2 -TimeoutSeconds 60 *> $null
    } catch { $rejected=$_.Exception.Message -like 'FAIL:*retry-two*' }
    $clock.Stop()
    $report=Get-Content -LiteralPath (Join-Path $gateDirectory 'regression-report.json') -Raw | ConvertFrom-Json
    if(!$rejected -or $report.Failed -ne 1 -or $report.Passed -ne 0 -or $clock.Elapsed.TotalSeconds -ge 60 -or
        [int][IO.File]::ReadAllText((Join-Path $fixture 'retry-two.count')) -ne 1 -or
        (Test-Path -LiteralPath (Join-Path $fixture 'gate-serial.json'))) {
        throw 'A failed test was retried, or did not cancel its running peer and the remaining tests'
    }
    $pendingDirectory=Join-Path $fixture 'pending-gate'
    $null=New-Item -ItemType Directory -Path $pendingDirectory
    $rejected=$false
    try { Invoke-RegressionGate -Tasks @($missing) -OutputDirectory $pendingDirectory *> $null }
    catch {$rejected=$_.Exception.Message -like 'PENDING:*'}
    $pendingReport=Get-Content -LiteralPath (Join-Path $pendingDirectory 'regression-report.json') -Raw | ConvertFrom-Json
    if(!$rejected -or $pendingReport.Failed -ne 0 -or $pendingReport.Pending -ne 1 -or @($pendingReport.Attempts).Count -ne 1) {
        throw 'Infrastructure problems were not retained as pending'
    }
    # Load a harmless copy of the OS version DLL under a Windhawk-shaped name
    # in a private child. Exercise actual module enumeration, not log matching
    # or the machine's installed Windhawk configuration.
    Copy-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('System')) 'version.dll') -Destination (Join-Path $fixture 'windhawk-fixture.dll')
    $injectedDirectory=Join-Path $fixture 'injected-gate'
    $null=New-Item -ItemType Directory -Path $injectedDirectory
    # Injection invalidates the whole run: the gate must cancel at once,
    # name the module, reap every running test and start no retries.
    $message=''
    $clock=[Diagnostics.Stopwatch]::StartNew()
    try {
        Invoke-RegressionGate -Tasks @((Task 'interfered'),(Task 'unaffected')) -OutputDirectory $injectedDirectory -Jobs 2 -TimeoutSeconds 60 -ObserveInterference *> $null
    } catch { $message=$_.Exception.Message }
    $clock.Stop()
    $orphans=@(Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -like "*$identity*" -and $_.CommandLine -like '*interfered*' })
    if($message -notlike 'PENDING:*injected into test process*' -or $message -notlike '*windhawk-fixture.dll*' -or
        $clock.Elapsed.TotalSeconds -ge 60 -or $orphans.Count -or
        (Test-Path -LiteralPath (Join-Path $injectedDirectory 'isolated-1'))) {
        throw "Observed module interference did not cancel the run immediately: $message"
    }
    Write-Output 'PASS: parallel rendezvous, parallel-before-serial phases, isolated serial tasks, -Jobs 1, timeouts, Windows argument quoting, stdout/stderr draining, failure propagation and peer cleanup'
    Write-Output 'PASS: one attempt per test; the first failure cancels running peers and unstarted tests; infrastructure pending'
    Write-Output 'PASS: real private module observation cancels the whole run at once, with no retries or orphaned tests'
    $passed=$true
} finally {
    $resolved=[IO.Path]::GetFullPath($fixture)
    if ((Split-Path -Parent $resolved) -ne $temporary -or (Split-Path -Leaf $resolved) -notlike 'gpu-parallel-runner-*') {
        throw 'Refusing cleanup outside the private runner fixture'
    }
    if($passed) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    else { Write-Warning "Runner-check evidence retained at $resolved" }
}

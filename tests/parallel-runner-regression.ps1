$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'parallel-runner.ps1')
$temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$fixture = Join-Path $temporary ('gpu-parallel-runner-'+[guid]::NewGuid().ToString('N')+' space [fixture] '+[char]0x062A)
New-Item -ItemType Directory -Path $fixture | Out-Null
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
$record=@{Start=$start; End=[Diagnostics.Stopwatch]::GetTimestamp(); Value=$Value}
[IO.File]::WriteAllText((Join-Path $Root ($Mode+'.json')),($record | ConvertTo-Json))
if($Mode -eq 'failure') { exit 7 }
'@
    [IO.File]::WriteAllText($program,$text,[Text.UTF8Encoding]::new($true))
    $shell = Join-Path $PSHOME 'powershell.exe'
    if (!(Test-Path -LiteralPath $shell)) { $shell = Join-Path $PSHOME 'pwsh.exe' }
    $identity = 'gpu-parallel-'+[guid]::NewGuid().ToString('N')
    $value = 'spaces "quotes" slash-before-quote \" trailing-slash\ ; $() ` '+[char]0x062A
    function Task([string]$Mode,[bool]$Exclusive=$false) {
        [pscustomobject]@{Name=$Mode; FilePath=$shell; Exclusive=$Exclusive
            Arguments=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',$program,$Mode,$fixture,$identity,$value)}
    }
    $tasks = @((Task 'peer-a'),(Task 'peer-b'),(Task 'exclusive' $true),(Task 'after'),(Task 'pipes'))
    # Suppress the intentionally large pipe output, while retaining the logs.
    Invoke-RegressionTasks -Tasks $tasks -OutputDirectory $fixture -Jobs 2 *> $null
    $a = [IO.File]::ReadAllText((Join-Path $fixture 'peer-a.json')) | ConvertFrom-Json
    $b = [IO.File]::ReadAllText((Join-Path $fixture 'peer-b.json')) | ConvertFrom-Json
    $exclusive = [IO.File]::ReadAllText((Join-Path $fixture 'exclusive.json')) | ConvertFrom-Json
    $after = [IO.File]::ReadAllText((Join-Path $fixture 'after.json')) | ConvertFrom-Json
    if ($exclusive.Start -lt [Math]::Max($a.End,$b.End) -or $after.Start -lt $exclusive.End) {
        throw 'Exclusive task overlapped another child'
    }
    if ($a.Value -cne $value -or $b.Value -cne $value) { throw 'Native argument quoting changed the payload' }
    foreach ($stream in @('stdout','stderr')) {
        if ((Get-Item -LiteralPath (Join-Path $fixture ('pipes.'+$stream+'.log'))).Length -lt 131072) {
            throw "Redirected $stream pipe was truncated"
        }
    }
    $rejected=$false
    try {
        Invoke-RegressionTasks -Tasks @((Task 'failure'),(Task 'survivor')) -OutputDirectory $fixture -Jobs 2 *> $null
    } catch { $rejected=$_.Exception.Message -like '*failure (exit 7)*' }
    if (!$rejected -or !(Test-Path -LiteralPath (Join-Path $fixture 'survivor.json'))) {
        throw 'Failed child did not fail the gate and drain its peer'
    }
    $results = @(Import-Csv -LiteralPath (Join-Path $fixture 'tasks-results.csv'))
    if ($results.Count -ne 2 -or ($results | Where-Object Name -eq 'failure').ExitCode -ne 7 -or
        !(Test-Path -LiteralPath (Join-Path $fixture 'failure.stderr.log'))) {
        throw 'Failure diagnostics were not retained'
    }
    $rejected=$false
    $missing=[pscustomobject]@{Name='missing'; FilePath=(Join-Path $fixture 'missing.exe'); Arguments=@(); Exclusive=$false}
    try {
        Invoke-RegressionTasks -Tasks @((Task 'drained'),$missing) -OutputDirectory $fixture -Jobs 2 *> $null
    } catch { $rejected=$true }
    if (!$rejected -or !(Test-Path -LiteralPath (Join-Path $fixture 'drained.json')) -or
        !(Test-Path -LiteralPath (Join-Path $fixture 'drained.stdout.log'))) {
        throw 'Process startup failure did not drain and log its running peer'
    }
    Invoke-RegressionTasks -Tasks @((Task 'serial-a'),(Task 'serial-b')) -OutputDirectory $fixture -Jobs 1 *> $null
    $a = [IO.File]::ReadAllText((Join-Path $fixture 'serial-a.json')) | ConvertFrom-Json
    $b = [IO.File]::ReadAllText((Join-Path $fixture 'serial-b.json')) | ConvertFrom-Json
    if ($b.Start -lt $a.End) { throw '-Jobs 1 overlapped children' }
    Write-Output 'PASS: parallel process rendezvous, exclusive barriers, serial limit, Windows argument quoting, stdout/stderr draining, failure propagation and peer cleanup'
} finally {
    $resolved=[IO.Path]::GetFullPath($fixture)
    if ((Split-Path -Parent $resolved) -ne $temporary -or (Split-Path -Leaf $resolved) -notlike 'gpu-parallel-runner-*') {
        throw 'Refusing cleanup outside the private runner fixture'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

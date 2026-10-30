param(
    [ValidateRange(1,30)][int]$Seconds = 10,
    [int]$WindowNumber = 0,
    [string]$OutputDirectory = $PSScriptRoot,
    [uint32[]]$AdditionalThreadId = @(),
    [string]$WindhawkLog,
    [switch]$UiOnly,
    [switch]$ListOnly
)
$ErrorActionPreference = 'Stop'
function Get-RendererThreadFromLog {
    param([string]$Path, $Target)
    $gpu = $Target.Overlay.ToInt64().ToString('X16')
    if ($Target.Overlay -eq [IntPtr]::Zero) { throw 'The selected window has no visible GPU overlay. Enable the mod and select its window.' }
    $pattern = '^\S+\s+'+$Target.ProcessId+'\s+explorer\.exe\s+.*\[GPU pipe\] gpu='+$gpu+
        ' n=\d+ qpc=(\d+) tid=(\d+) .* op=(\S+) phase=\w a=\S+ b=\S+ v=([^ ]+)'
    $latest = -1L; $renderer = [uint32]0; $ownerSeen = $false
    foreach ($line in [IO.File]::ReadLines($Path)) {
        if ($line -notmatch $pattern) { continue }
        $clock = [long]$Matches[1]; $owner = [uint32]$Matches[2]; $operation = $Matches[3]; $values = $Matches[4]
        $candidate = [uint32]0
        if ($owner -eq $Target.ThreadId -and $operation -eq 'presentation.submit[rows,images,active]') { $ownerSeen = $true }
        if ($operation -eq 'frame.begin[pos,target,hz,inputAge]' -and $owner -ne $Target.ThreadId) {
            $candidate = $owner
        } elseif ($operation -eq 'presentation.accept[thread,frames,scroll,rows]' -and $owner -eq $Target.ThreadId) {
            $candidate = [uint32][double]::Parse($values.Split(',')[0],[cultureinfo]::InvariantCulture)
        }
        if ($candidate -and $clock -gt $latest) { $latest = $clock; $renderer = $candidate }
    }
    if (-not $renderer -or -not $ownerSeen) {
        throw ('No renderer in this log matches the selected window (PID {0}, UI {1}, overlay 0x{2}). Save a fresh log after navigating in that window.' -f $Target.ProcessId,$Target.ThreadId,$gpu)
    }
    return $renderer
}
function Invoke-ThreadStackCapture {
    param([string]$Sampler, $Target, [uint32[]]$Threads, [int]$SampleCount, [string]$Directory)
    if ($Threads.Count -lt 1 -or $Threads.Count -gt 4 -or @($Threads | Where-Object { $_ -eq 0 }).Count) {
        throw 'Select one to four valid threads from the same Explorer process.'
    }
    $captures = @()
    $stopName = 'Local\ExplorerStackCaptureStop-'+[guid]::NewGuid().ToString('N')
    $stopEvent = [Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$stopName)
    try {
        foreach ($thread in $Threads) {
            $started = Get-Date
            $output = Join-Path $Directory ('Explorer-UI-stacks-{0}-pid{1}-tid{2}.txt' -f $started.ToString('yyyyMMdd-HHmmss'),$Target.ProcessId,$thread)
            $start = New-Object Diagnostics.ProcessStartInfo
            $start.FileName = $Sampler
            $arguments = @($Target.ProcessId,$thread,(Split-Path -Parent $Sampler),$SampleCount,$stopName)
            $start.Arguments = ($arguments | ForEach-Object {
                '"'+[regex]::Replace([regex]::Replace([string]$_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
            }) -join ' '
            $start.UseShellExecute = $false
            $start.CreateNoWindow = $true
            $start.RedirectStandardOutput = $start.RedirectStandardError = $true
            $process = New-Object Diagnostics.Process
            $process.StartInfo = $start
            if (-not $process.Start()) { throw "Could not start capture for thread $thread" }
            # Drain both streams immediately. Sequential waits must never leave
            # a concurrently unwinding helper blocked on its full output pipe.
            $captures += [pscustomobject]@{ Process=$process; Output=$output; Started=$started; Thread=$thread;
                Stdout=$process.StandardOutput.ReadToEndAsync(); Stderr=$process.StandardError.ReadToEndAsync() }
        }
        $failed = $false
        foreach ($capture in $captures) {
            $capture.Process.WaitForExit()
            $header = @('Explorer thread stack capture',('Started: '+$capture.Started.ToString('o')),
                ('OS: '+[Environment]::OSVersion.VersionString),('Window: '+$Target.Title),
                ('PID: '+$Target.ProcessId),('UI thread: '+$Target.ThreadId),('Sampled thread: '+$capture.Thread),'') -join "`r`n"
            [IO.File]::WriteAllText($capture.Output,$header+"`r`n"+$capture.Stdout.GetAwaiter().GetResult()+
                $capture.Stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($true))
            Write-Output $capture.Output
            $failed = $failed -or $capture.Process.ExitCode -ne 0
        }
        if ($failed) { throw 'A capture failed; inspect the saved output. Access denied may require running Capture.cmd as administrator.' }
    } finally {
        # Cancellation is cooperative: every helper resumes a copied thread
        # before observing this event. Never terminate a suspended-thread copy.
        [void]$stopEvent.Set()
        foreach ($capture in $captures) { $capture.Process.WaitForExit(); $capture.Process.Dispose() }
        $stopEvent.Dispose()
    }
}
if (-not ('ExplorerStackCapture.Windows' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
namespace ExplorerStackCapture {
    public class Target {
        public string Title;
        public uint ProcessId, ThreadId;
        public IntPtr Window, Overlay;
    }
    public static class Windows {
        delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);
        [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
        [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr parameter);
        [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder text, int size);
        [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int size);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
        static string ClassName(IntPtr hwnd) {
            var text = new StringBuilder(256); GetClassName(hwnd, text, text.Capacity); return text.ToString();
        }
        public static Target[] Find() {
            var targets = new List<Target>();
            EnumWindows((frame, unused) => {
                string name = ClassName(frame);
                if ((name != "CabinetWClass" && name != "ExploreWClass") || !IsWindowVisible(frame)) return true;
                IntPtr tab = IntPtr.Zero, overlay = IntPtr.Zero;
                EnumChildWindows(frame, (child, ignored) => {
                    if (!IsWindowVisible(child)) return true;
                    string cls = ClassName(child);
                    if (cls == "ShellTabWindowClass") tab = child;
                    if (cls.IndexOf("gpu-accelerated-explorer-view", StringComparison.OrdinalIgnoreCase) >= 0 &&
                        cls.EndsWith("-overlay", StringComparison.OrdinalIgnoreCase)) overlay = child;
                    return true;
                }, IntPtr.Zero);
                var title = new StringBuilder(1024); GetWindowText(frame, title, title.Capacity);
                uint pid; uint tid = GetWindowThreadProcessId(overlay != IntPtr.Zero ? overlay : tab != IntPtr.Zero ? tab : frame, out pid);
                targets.Add(new Target { Title=title.ToString(), ProcessId=pid, ThreadId=tid, Window=frame, Overlay=overlay });
                return true;
            }, IntPtr.Zero);
            return targets.ToArray();
        }
    }
}
'@
}
$targets = @([ExplorerStackCapture.Windows]::Find() | Where-Object {
    $process = Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue
    $process -and $process.ProcessName -eq 'explorer'
})
if (-not $targets.Count) { throw 'No visible File Explorer window was found. Open the window where the lag occurs.' }
for ($index=0; $index -lt $targets.Count; ++$index) {
    $target = $targets[$index]
    Write-Host ('{0}: {1} (PID {2}, UI thread {3})' -f ($index+1),$target.Title,$target.ProcessId,$target.ThreadId)
}
if ($ListOnly) { return }
if ($WindowNumber -eq 0) {
    if ($targets.Count -eq 1) { $WindowNumber=1 }
    else {
        $answer = Read-Host 'Enter the Explorer window number'
        if (-not [int]::TryParse($answer,[ref]$WindowNumber)) { throw 'Enter a numeric window number.' }
    }
}
if ($WindowNumber -lt 1 -or $WindowNumber -gt $targets.Count) { throw 'The selected Explorer window number is out of range.' }
$sampler = Join-Path $PSScriptRoot 'live-stall-sampler.exe'
if (-not (Test-Path -LiteralPath $sampler)) { throw 'Keep live-stall-sampler.exe beside this script.' }
if (-not (Test-Path -LiteralPath $OutputDirectory)) { New-Item -ItemType Directory -Path $OutputDirectory | Out-Null }
$target = $targets[$WindowNumber-1]
if ($UiOnly -and ($AdditionalThreadId.Count -or $WindhawkLog)) { throw '-UiOnly cannot be combined with renderer selection.' }
if (-not $UiOnly -and -not $AdditionalThreadId.Count -and -not $WindhawkLog) {
    $selection = (Read-Host 'Drag a saved Windhawk log here to select this window''s renderer, or enter its current renderer thread ID').Trim().Trim('"')
    $renderer = [uint32]0
    if ([uint32]::TryParse($selection,[ref]$renderer) -and $renderer) { $AdditionalThreadId = @($renderer) }
    elseif ($selection) { $WindhawkLog = $selection }
    else { throw 'A renderer is required for animation diagnosis. Use -UiOnly only for an intentional UI-only capture.' }
}
if ($WindhawkLog) {
    $path = (Resolve-Path -LiteralPath $WindhawkLog).ProviderPath
    $AdditionalThreadId = @($AdditionalThreadId)+@(Get-RendererThreadFromLog $path $target)
}
$threads = @(@($target.ThreadId)+$AdditionalThreadId | Select-Object -Unique)
Write-Host ('Selected PID {0}; UI {1}; sampled threads {2}.' -f $target.ProcessId,$target.ThreadId,($threads -join ', '))
Write-Host ('Capture lasts about {0} seconds. Switch to the selected Explorer window and repeat Back/Forward swipes during capture.' -f $Seconds)
[void](Read-Host 'Press Enter to begin')
$current = @([ExplorerStackCapture.Windows]::Find() | Where-Object {
    $_.Window -eq $target.Window -and $_.Overlay -eq $target.Overlay -and
    $_.ProcessId -eq $target.ProcessId -and $_.ThreadId -eq $target.ThreadId
})
if (-not $current.Count) { throw 'The selected Explorer window or active tab changed. Run the capture again for the current tab.' }
Write-Host 'Capturing. Repeat the swipes now...'
# A local symbol path prevents network downloads. Module build identities and
# offsets remain available for later analysis on a machine with matching PDBs.
$outputs = @(Invoke-ThreadStackCapture $sampler $target $threads ($Seconds*100) $OutputDirectory)
foreach ($output in $outputs) { Write-Host ('Saved: '+$output) }
$archive = [IO.Path]::ChangeExtension($outputs[0],'.zip')
Compress-Archive -LiteralPath $outputs -DestinationPath $archive -Force
Write-Host ('Send this capture ZIP: '+$archive)
Write-Host 'Now save and send the Windhawk log covering these same swipes. The earlier log used to select the renderer is not the new gesture log.'

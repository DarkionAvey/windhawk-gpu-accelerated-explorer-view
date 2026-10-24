param([switch]$Accessibility)
$ErrorActionPreference = 'Stop'
# Read-only live integration probe. No input, focus, window or file changes.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class StatusbarProbe {
    public delegate bool Enumerate(IntPtr window, IntPtr context);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(Enumerate callback,IntPtr context);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent,Enumerate callback,IntPtr context);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder text,int count);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    public static string Class(IntPtr window) { var text=new StringBuilder(256); GetClassName(window,text,text.Capacity); return text.ToString(); }
}
'@
$previousDpi = [StatusbarProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
    $frames = [Collections.Generic.List[IntPtr]]::new()
    [void][StatusbarProbe]::EnumWindows({ param($window,$context)
        if ([StatusbarProbe]::Class($window) -eq 'CabinetWClass' -and [StatusbarProbe]::IsWindowVisible($window)) { $frames.Add($window) }
        return $true
    },[IntPtr]::Zero)
    if ($Accessibility) { Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,WindowsBase }
    foreach ($frame in $frames) {
        $bounds = [StatusbarProbe+Rect]::new(); [void][StatusbarProbe]::GetWindowRect($frame,[ref]$bounds)
        "FRAME hwnd=0x{0:X} bounds={1},{2},{3},{4}" -f $frame.ToInt64(),$bounds.Left,$bounds.Top,$bounds.Right,$bounds.Bottom
        $statusRoots = [Collections.Generic.List[IntPtr]]::new()
        $windowLines = [Collections.Generic.List[string]]::new()
        [void][StatusbarProbe]::EnumChildWindows($frame,{ param($window,$context)
            $rect = [StatusbarProbe+Rect]::new(); [void][StatusbarProbe]::GetWindowRect($window,[ref]$rect)
            $class = [StatusbarProbe]::Class($window)
            if ([StatusbarProbe]::IsWindowVisible($window) -and $rect.Bottom -gt $bounds.Bottom-150) {
                $windowLines.Add(("  HWND=0x{0:X} class={1} bounds={2},{3},{4},{5}" -f $window.ToInt64(),$class,$rect.Left,$rect.Top,$rect.Right,$rect.Bottom))
                if ($class -eq 'DirectUIHWND' -or ($rect.Top -gt $bounds.Bottom-150 -and $class -ne 'Button' -and $class -notlike '*tooltip*')) { $statusRoots.Add($window) }
            }
            return $true
        },[IntPtr]::Zero)
        $windowLines
        if ($Accessibility) {
            foreach ($window in $statusRoots) {
                $root = [Windows.Automation.AutomationElement]::FromHandle($window)
                $bottom=$root.Current.BoundingRectangle.Bottom-1
                $queue=[Collections.Generic.Queue[object]]::new(); $queue.Enqueue(@($root,0))
                $visited=0
                while ($queue.Count -and $visited -lt 128) {
                    $node=$queue.Dequeue(); $element=$node[0]; $depth=$node[1]; ++$visited
                    $current=$element.Current; $rect=$current.BoundingRectangle
                    "  UIA host=0x{0:X} depth={1} type={2} class={3} id={4} name={5} offscreen={6} bounds={7}" -f $window.ToInt64(),$depth,$current.ControlType.ProgrammaticName,$current.ClassName,$current.AutomationId,$current.Name,$current.IsOffscreen,$rect
                    if ($depth -lt 8 -and (($rect.Top -le $bottom -and $rect.Bottom -gt $bottom) -or $current.ControlType -eq [Windows.Automation.ControlType]::StatusBar)) {
                        $children=$element.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.Condition]::TrueCondition)
                        foreach ($child in $children) { $queue.Enqueue(@($child,($depth+1))) }
                    }
                }
            }
        }
    }
} finally { [void][StatusbarProbe]::SetThreadDpiAwarenessContext($previousDpi) }

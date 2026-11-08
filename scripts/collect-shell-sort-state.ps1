# Read-only diagnostics for "Shell reports an empty sort key" on a drive view.
# Dumps Explorer's saved per-folder view state (ShellBags) for This PC and the
# given drive roots, the drive/volume facts, and any desktop.ini that can set a
# folder type. Nothing is written except the report file.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\collect-shell-sort-state.ps1 -Drives D,C
param(
    [string[]]$Drives = @('D'),
    [string]$OutputFile = (Join-Path ([IO.Path]::GetTempPath()) 'gpu-shell-sort-state.txt')
)
$ErrorActionPreference = 'Continue'
$Drives = @($Drives | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().TrimEnd(':','\').ToUpperInvariant() } | Where-Object { $_ })
$lines = [Collections.Generic.List[string]]::new()
function Out([string]$text) { $lines.Add($text); Write-Output $text }

$thisPcClsid = [guid]'20D04FE0-3AEA-1069-A2D8-08002B30309D'
$folderTypes = @{
    '5c4f28b5-f869-4e84-8e60-f11db97c5cc7' = 'Generic'
    '7d49d726-3c21-4f05-99aa-fdc2c9474656' = 'Documents'
    'b3690e58-e961-423b-b687-386ebfd83239' = 'Pictures'
    '94d6ddcc-4a68-4175-a374-bd584a510b78' = 'Music'
    '5fa96407-7e77-483c-ac93-691d05850de8' = 'Videos'
    '885a186e-a440-4ada-812b-db871b942259' = 'Downloads'
}
$knownProperties = @{
    'b725f130-47ef-101a-a5f1-02608c9eebac,10' = 'ItemNameDisplay'
    'b725f130-47ef-101a-a5f1-02608c9eebac,12' = 'Size'
    'b725f130-47ef-101a-a5f1-02608c9eebac,14' = 'DateModified'
    'b725f130-47ef-101a-a5f1-02608c9eebac,4'  = 'ItemTypeText'
    '9b174b35-40ff-11d2-a27e-00c04fc30871,3'  = 'FreeSpace'
    '9b174b35-40ff-11d2-a27e-00c04fc30871,2'  = 'Capacity'
    '28636aa6-953d-11d2-b5d6-00c04fd918d0,11' = 'ItemType'
    '00000000-0000-0000-0000-000000000000,0'  = 'EMPTY KEY'
}
function PropertyName([guid]$fmtid, [uint32]$pid) {
    $name = $knownProperties["$fmtid,$pid"]
    if ($name) { "$fmtid,$pid ($name)" } else { "$fmtid,$pid" }
}

# A ShellBag "Sort" value: 16 reserved bytes, a DWORD count, then count
# SORTCOLUMN records (PROPERTYKEY 20 bytes + direction INT 4 bytes).
function DecodeSort([byte[]]$data) {
    if (!$data) { return '(none)' }
    $hex = ($data | ForEach-Object { $_.ToString('x2') }) -join ''
    if ($data.Length -lt 20) { return "undecoded len=$($data.Length) hex=$hex" }
    $count = [BitConverter]::ToUInt32($data, 16)
    if ($data.Length -ne 20 + 24 * $count) { return "undecoded len=$($data.Length) count=$count hex=$hex" }
    $columns = for ($i = 0; $i -lt $count; ++$i) {
        $offset = 20 + 24 * $i
        $fmtid = [guid]::new([byte[]]$data[$offset..($offset + 15)])
        $pid = [BitConverter]::ToUInt32($data, $offset + 16)
        $direction = [BitConverter]::ToInt32($data, $offset + 20)
        "$(PropertyName $fmtid $pid) dir=$direction"
    }
    "count=$count [" + ($columns -join '; ') + "] hex=$hex"
}

# First shell item of a BagMRU PIDL: returns 'ThisPC', a drive root 'X:\' or $null.
function DescribeItem([byte[]]$pidl) {
    if (!$pidl -or $pidl.Length -lt 3) { return $null }
    $type = $pidl[2]
    # Navigation-pane delegate items (0x1F with an embedded "/X:\" root) name a
    # drive at the BagMRU root, separately from This PC > X:\.
    $head = [Text.Encoding]::ASCII.GetString($pidl, 0, [Math]::Min(64, $pidl.Length))
    if ($type -eq 0x1F -and $head -match '/([A-Za-z]):\\\x00') { return "$($Matches[1].ToUpperInvariant()):\ (delegate)" }
    if ($type -eq 0x1F -and $pidl.Length -ge 20) {
        $clsid = [guid]::new([byte[]]$pidl[4..19])
        if ($clsid -eq $thisPcClsid) { return 'ThisPC' }
        return "CLSID $clsid"
    }
    if (($type -band 0x70) -eq 0x20 -and $pidl.Length -ge 6) {
        $text = [Text.Encoding]::ASCII.GetString($pidl, 3, [Math]::Min(3, $pidl.Length - 3))
        if ($text -match '^[A-Za-z]:\\') { return $text.ToUpperInvariant() }
    }
    $null
}

function DumpBag([string]$shellRoot, [string]$slot, [string]$label) {
    $bag = "$shellRoot\Bags\$slot"
    Out "  [$label] NodeSlot=$slot  $bag"
    if (!(Test-Path "Registry::$bag")) { Out '    (no Bags entry: Explorer uses folder-type defaults)'; return }
    foreach ($key in Get-ChildItem "Registry::$bag" -Recurse -ErrorAction SilentlyContinue) {
        $leaf = Split-Path $key.Name -Leaf
        $typeName = $folderTypes[$leaf.Trim('{}').ToLowerInvariant()]
        Out "    key $($key.Name.Substring($bag.Length))$(if ($typeName) { " = folder type $typeName" })"
        $values = Get-ItemProperty "Registry::$($key.Name)" -ErrorAction SilentlyContinue
        foreach ($property in $key.Property | Where-Object { $_ -notmatch '^(Min|Max|Win)Pos' }) {
            $value = $values.$property
            $text = if ($property -eq 'Sort') { DecodeSort $value }
                elseif ($value -is [byte[]]) { 'bin ' + (($value | Select-Object -First 64 | ForEach-Object { $_.ToString('x2') }) -join '') }
                else { "$value" }
            Out "      $property = $text"
        }
    }
}

function WalkBagMru([string]$shellRoot, [string]$path, [string]$chain, [int]$depth) {
    if ($depth -gt 2) { return }
    $key = Get-Item "Registry::$path" -ErrorAction SilentlyContinue
    if (!$key) { return }
    foreach ($name in $key.Property | Where-Object { $_ -match '^\d+$' }) {
        $item = DescribeItem ([byte[]]$key.GetValue($name))
        if (!$item) { continue }
        $child = "$path\$name"
        $slot = (Get-ItemProperty "Registry::$child" -Name NodeSlot -ErrorAction SilentlyContinue).NodeSlot
        $label = if ($chain) { "$chain > $item" } else { $item }
        $wanted = $item -eq 'ThisPC' -or ($item -match '^[A-Z]:\\' -and $Drives -contains $item.Substring(0,1))
        if ($wanted -and $null -ne $slot) { DumpBag $shellRoot $slot $label }
        elseif ($wanted) { Out "  [$label] no NodeSlot (never given its own view state)" }
        if ($item -eq 'ThisPC' -or $depth -eq 0) { WalkBagMru $shellRoot $child $label ($depth + 1) }
    }
}

Out "Collected $(Get-Date -Format o) on $([Environment]::OSVersion.VersionString) build $((Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').DisplayVersion)"
Out "Drives: $($Drives -join ', ')"
Out ''
Out '== Drives'
foreach ($letter in $Drives) {
    $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='${letter}:'" -ErrorAction SilentlyContinue
    if ($disk) { Out "  ${letter}: type=$($disk.DriveType) fs=$($disk.FileSystem) label='$($disk.VolumeName)' size=$($disk.Size) free=$($disk.FreeSpace) media=$($disk.MediaType)" }
    else { Out "  ${letter}: not present" }
    $volume = Get-Volume -DriveLetter $letter -ErrorAction SilentlyContinue
    if ($volume) { Out "     volume drivetype=$($volume.DriveType) health=$($volume.HealthStatus) guid=$($volume.UniqueId)" }
    foreach ($file in 'desktop.ini','autorun.inf') {
        $target = "${letter}:\$file"
        if (Test-Path -LiteralPath $target) {
            Out "     $file present:"
            Get-Content -LiteralPath $target -TotalCount 40 -ErrorAction SilentlyContinue | ForEach-Object { Out "       $_" }
        }
    }
}
Out ''
foreach ($shellRoot in 'HKEY_CURRENT_USER\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell',
                       'HKEY_CURRENT_USER\Software\Microsoft\Windows\Shell') {
    Out "== ShellBags under $shellRoot"
    if (!(Test-Path "Registry::$shellRoot\BagMRU")) { Out '  (no BagMRU)'; continue }
    WalkBagMru $shellRoot "$shellRoot\BagMRU" '' 0
    $allFolders = "$shellRoot\Bags\AllFolders"
    if (Test-Path "Registry::$allFolders") { DumpBag $shellRoot 'AllFolders' 'AllFolders (template applied to every folder)' }
    Out ''
}
Out '== Explorer view defaults'
foreach ($path in 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Streams\Defaults',
                  'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Modules\GlobalSettings\Sizer') {
    if (Test-Path $path) { Out "  $path values: $((Get-Item $path).Property -join ', ')" } else { Out "  $path absent" }
}
$advanced = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' -ErrorAction SilentlyContinue
Out "  Advanced: SeparateProcess=$($advanced.SeparateProcess) PersistBrowsers=$($advanced.PersistBrowsers) NavPaneExpandToCurrentFolder=$($advanced.NavPaneExpandToCurrentFolder)"
$bagLimit = Get-ItemProperty 'HKCU:\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell' -Name BagMRU* -ErrorAction SilentlyContinue
Out "  BagMRU size setting: $($bagLimit.'BagMRU Size')"

$lines | Set-Content -LiteralPath $OutputFile -Encoding UTF8
Write-Output ''
Write-Output "Report written to $OutputFile"

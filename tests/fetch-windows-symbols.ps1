param([string[]]$ModuleNames = @('DUI70.dll','DUser.dll','explorerframe.dll'))
$ErrorActionPreference = 'Stop'
$destination = Join-Path $PSScriptRoot '.symbol-cache'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
foreach ($moduleName in $ModuleNames) {
    $path = if ([IO.Path]::IsPathRooted($moduleName)) { $moduleName }
            else { Join-Path ([Environment]::SystemDirectory) $moduleName }
    $bytes = [IO.File]::ReadAllBytes($path)
    $pe = [BitConverter]::ToInt32($bytes,0x3c)
    $sections = [BitConverter]::ToUInt16($bytes,$pe+6)
    $optionalSize = [BitConverter]::ToUInt16($bytes,$pe+20)
    $optional = $pe+24
    if ([BitConverter]::ToUInt16($bytes,$optional) -ne 0x20b) { throw 'Only x64 system modules are supported' }
    $debugRva = [BitConverter]::ToUInt32($bytes,$optional+112+6*8)
    $debugSize = [BitConverter]::ToUInt32($bytes,$optional+112+6*8+4)
    $debugOffset = $null
    for ($section=0; $section -lt $sections; $section++) {
        $header = $optional+$optionalSize+40*$section
        $rva = [BitConverter]::ToUInt32($bytes,$header+12)
        $size = [math]::Max([BitConverter]::ToUInt32($bytes,$header+8),[BitConverter]::ToUInt32($bytes,$header+16))
        if ($debugRva -ge $rva -and $debugRva -lt $rva+$size) {
            $debugOffset = [BitConverter]::ToUInt32($bytes,$header+20)+$debugRva-$rva
            break
        }
    }
    if ($null -eq $debugOffset) { throw "No debug directory in $moduleName" }
    $found = $false
    for ($entry=0; $entry -lt $debugSize; $entry+=28) {
        if ([BitConverter]::ToUInt32($bytes,$debugOffset+$entry+12) -ne 2) { continue }
        $record = [BitConverter]::ToUInt32($bytes,$debugOffset+$entry+24)
        if ([BitConverter]::ToUInt32($bytes,$record) -ne 0x53445352) { continue }
        $guid = [guid]::new([byte[]]$bytes[($record+4)..($record+19)])
        $age = [BitConverter]::ToUInt32($bytes,$record+20)
        $end = $record+24
        while ($bytes[$end] -ne 0) { $end++ }
        $pdb = [IO.Path]::GetFileName([Text.Encoding]::ASCII.GetString($bytes,$record+24,$end-$record-24))
        $identity = $guid.ToString('N').ToUpperInvariant()+$age.ToString('X')
        $output = Join-Path $destination $pdb
        $identityFile = $output+'.identity'
        if (!(Test-Path -LiteralPath $output) -or !(Test-Path -LiteralPath $identityFile) -or
            [IO.File]::ReadAllText($identityFile) -ne $identity) {
            $uri = "https://msdl.microsoft.com/download/symbols/$pdb/$identity/$pdb"
            Invoke-WebRequest -Uri $uri -OutFile $output -UseBasicParsing
            [IO.File]::WriteAllText($identityFile,$identity)
        }
        Write-Output "$moduleName -> $pdb $identity"
        $found = $true
        break
    }
    if (!$found) { throw "No RSDS identity in $moduleName" }
}

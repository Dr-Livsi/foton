# Shows what takes the space in the portable package. The results are annotations,
# so they are visible on the run summary page, even when the log can't be opened.
#
# Besides the largest folders and files, it lists the DLLs in bin that nothing in the package
# loads, and who loads the large DLLs, to see what can be left out of the package.
param(
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][string]$Setup
)

$dir = Join-Path $env:RUNNER_TEMP "package-contents"
Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
# 7-Zip is installed on the runners of GitHub.
& "$env:ProgramFiles\7-Zip\7z.exe" x -y "-o$dir" $Archive | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "::error::Could not unpack $Archive"
    exit 1
}
$files = @(Get-ChildItem $dir -Recurse -File)

function Get-RelativePath($file) {
    $file.FullName.Substring($dir.Length + 1).Replace('\', '/')
}

function Format-Size([long]$bytes) {
    "{0:N1} MB" -f ($bytes / 1MB)
}

function Write-Notice([string]$title, [string[]]$lines) {
    $text = ($lines -join "`n").Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
    Write-Host "::notice title=$title::$text"
}

$total = ($files | Measure-Object Length -Sum).Sum
Write-Notice "Package sizes" @(
    "Foton-Setup.exe: $(Format-Size (Get-Item $Setup).Length)",
    "Foton-portable.7z: $(Format-Size (Get-Item $Archive).Length)",
    "Unpacked: $(Format-Size $total) in $($files.Count) files"
)

# Folders up to four levels deep, e.g. bin/qml/QtQuick/Controls.
$folders = @{}
foreach ($file in $files) {
    $parts = (Get-RelativePath $file).Split('/')
    for ($depth = 1; $depth -le [Math]::Min(4, $parts.Count - 1); ++$depth) {
        $folders[($parts[0..($depth - 1)] -join '/') + '/'] += $file.Length
    }
}
Write-Notice "Largest folders" @($folders.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 70 | ForEach-Object {
        "$(Format-Size $_.Value)  $($_.Key)"
    })

Write-Notice "Largest files" @($files | Sort-Object Length -Descending | Select-Object -First 60 | ForEach-Object {
        "$(Format-Size $_.Length)  $(Get-RelativePath $_)"
    })

# The DLLs that each program and DLL imports, with dumpbin of Visual Studio.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$dumpbin = Get-ChildItem "$vs\VC\Tools\MSVC" -Recurse -Filter dumpbin.exe | Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1
if (-not $dumpbin) {
    Write-Host "::warning::dumpbin was not found"
    exit 0
}

$bin = Join-Path $dir "bin"
$binDlls = @{}
foreach ($file in Get-ChildItem $bin -Filter *.dll -File) {
    $binDlls[$file.Name.ToLowerInvariant()] = $file
}
$imports = @{}
$importers = @{}
foreach ($file in $files | Where-Object { $_.Extension -in '.dll', '.exe' }) {
    $dependents = @(& $dumpbin.FullName /nologo /dependents $file.FullName | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $Matches[1].ToLowerInvariant() })
    $imports[$file.FullName] = $dependents
    foreach ($dependent in $dependents) {
        if ($binDlls.ContainsKey($dependent)) {
            $importers[$dependent] += @(Get-RelativePath $file)
        }
    }
}

# Everything that can be loaded: the programs, the plugins and the QML modules (the DLLs outside of bin itself).
$reached = @{}
$queue = [System.Collections.Generic.Queue[string]]::new()
foreach ($file in $files | Where-Object { $_.Extension -eq '.exe' -or ($_.Extension -eq '.dll' -and $_.DirectoryName -ne $bin) }) {
    foreach ($dependent in $imports[$file.FullName]) {
        $queue.Enqueue($dependent)
    }
}
while ($queue.Count -gt 0) {
    $name = $queue.Dequeue()
    if ($reached.ContainsKey($name) -or -not $binDlls.ContainsKey($name)) {
        continue
    }
    $reached[$name] = $true
    foreach ($dependent in $imports[$binDlls[$name].FullName]) {
        $queue.Enqueue($dependent)
    }
}
$unused = @($binDlls.Values | Where-Object { -not $reached.ContainsKey($_.Name.ToLowerInvariant()) } | Sort-Object Length -Descending)
$unusedSize = ($unused | Measure-Object Length -Sum).Sum
Write-Notice "DLLs in bin that nothing imports ($(Format-Size $unusedSize), some may be loaded at runtime)" @($unused | ForEach-Object {
        "$(Format-Size $_.Length)  $($_.Name)"
    })

# Who needs the large DLLs.
Write-Notice "Who imports the large DLLs in bin" @($binDlls.Values | Where-Object { $_.Length -ge 1MB } | Sort-Object Length -Descending | ForEach-Object {
        $who = @($importers[$_.Name.ToLowerInvariant()] | Where-Object { $_ })
        $list = ($who | Select-Object -First 8) -join ', '
        if ($who.Count -gt 8) {
            $list += ", ... ($($who.Count) in total)"
        }
        "$(Format-Size $_.Length)  $($_.Name) <- $list"
    })

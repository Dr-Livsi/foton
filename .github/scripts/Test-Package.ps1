# Starts Foton from the portable package to check that nothing it needs is missing,
# e.g. after files were left out of the package to make it smaller.
#
# It takes a screenshot without the user interface, opens it in the editor and starts in the
# system tray, which loads the user interface for selecting a region. Warnings about missing
# QML modules, plugins or libraries are reported as annotations, and the step fails.
param(
    [Parameter(Mandatory)][string]$Archive
)

$dir = Join-Path $env:RUNNER_TEMP "package-test"
Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
# 7-Zip is installed on the runners of GitHub.
& "$env:ProgramFiles\7-Zip\7z.exe" x -y "-o$dir" $Archive | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "::error::Could not unpack $Archive"
    exit 1
}
$foton = Join-Path $dir "bin\foton.exe"
$logs = Join-Path $env:RUNNER_TEMP "package-test-logs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

# Qt writes its messages to stderr instead of the debugger, and with the QML modules that are imported.
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QML_IMPORT_TRACE = "1"

$problems = [System.Collections.Generic.List[string]]::new()

function Invoke-Foton([string]$name, [string[]]$arguments, [int]$seconds, [bool]$keepsRunning) {
    $out = Join-Path $logs "$name.out.log"
    $err = Join-Path $logs "$name.err.log"
    $process = Start-Process -FilePath $foton -ArgumentList $arguments -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
    $exited = $process.WaitForExit($seconds * 1000)
    $problem = $null
    if (-not $exited) {
        if (-not $keepsRunning) {
            $problem = "${name}: still running after $seconds s"
        }
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        $process.WaitForExit()
    } elseif ($keepsRunning) {
        $problem = "${name}: quit after less than $seconds s with exit code $($process.ExitCode)"
    } elseif ($process.ExitCode -ne 0) {
        $problem = "${name}: exit code $($process.ExitCode)"
    }
    $output = @(Get-Content $err -ErrorAction SilentlyContinue)
    if ($problem) {
        # The last messages, without the ones about QML imports.
        $last = @($output | Where-Object { $_ -notmatch 'qt\.qml\.import' } | Select-Object -Last 8) -join ' | '
        $problems.Add("$problem, last messages: $last")
    }
    $output
}

$screenshot = Join-Path $env:RUNNER_TEMP "package-test.png"
$messages = @()
$messages += Invoke-Foton "screenshot" @("--background", "--fullscreen", "--nonotify", "--output", $screenshot) 60 $false
if (-not (Test-Path $screenshot) -or (Get-Item $screenshot).Length -eq 0) {
    $problems.Add("screenshot: no image was saved")
}
$messages += Invoke-Foton "editor" @("--edit-existing", $screenshot) 40 $true
$messages += Invoke-Foton "tray" @("--tray") 40 $true

# Messages that mean that something is missing.
$pattern = '(?i)(is not installed|cannot load library|could not load|failed to load|not found|no such file|is not a type|unavailable|could not find|cannot find|unknown component|ReferenceError|TypeError)'
$missing = @($messages | Where-Object { $_ -match $pattern -and $_ -notmatch 'qt\.qml\.import' } | Select-Object -Unique)
foreach ($line in $missing) {
    $problems.Add($line)
}

$imported = @($messages | Where-Object { $_ -match 'locateLocalQmldir: (\S+) module' } | ForEach-Object { $Matches[1] } | Sort-Object -Unique)
$text = (@("QML modules: $($imported -join ', ')") + @($problems | Select-Object -First 40)) -join "`n"
$text = $text.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
if ($problems.Count -gt 0) {
    Write-Host "::error title=Package test ($($problems.Count) problems)::$text"
    exit 1
}
Write-Host "::notice title=Package test passed::$text"

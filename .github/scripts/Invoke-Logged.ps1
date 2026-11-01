# Runs a program with its output written to a log file. If the program fails or
# runs longer than the time limit, the end of the log is reported as an error
# annotation, so it is visible on the run summary page.
function Invoke-Logged {
    param(
        [Parameter(Mandatory)][string]$Title,
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        # Text passed to the standard input of the program.
        [string]$InputText = "",
        [int]$TimeoutMinutes = 300,
        [int]$TailLines = 60
    )
    $log = New-TemporaryFile
    $inputFile = New-TemporaryFile
    Set-Content -Path $inputFile.FullName -Value $InputText -NoNewline
    $env:PYTHONUNBUFFERED = "1"
    # The runner image has the Android SDK installed. With these variables set,
    # Craft assumes an Android build and picks MinSizeRel, which has no binary cache on Windows.
    Remove-Item Env:ANDROID_SDK_ROOT, Env:ANDROID_NDK, Env:ANDROID_NDK_ROOT, Env:ANDROID_NDK_HOME, Env:ANDROID_HOME -ErrorAction SilentlyContinue
    # Craft warns about MinGW and the Unix tools of Git in PATH, they can break its builds.
    $env:PATH = ($env:PATH -split ';' | Where-Object { $_ -and $_ -notmatch '(?i)mingw|\\Git\\(usr\\)?bin|strawberry' }) -join ';'

    $quoted = @($FilePath) + $Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }
    $commandLine = ($quoted -join ' ') + " < `"$($inputFile.FullName)`" > `"$($log.FullName)`" 2>&1"
    Write-Host "> $commandLine"
    $process = Start-Process -FilePath cmd.exe -ArgumentList "/d /s /c `"$commandLine`"" -NoNewWindow -PassThru
    # Keep the handle open, otherwise ExitCode is not available after the process ends.
    $null = $process.Handle

    # Show the log while the program runs.
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    $reader = [System.IO.StreamReader]::new([System.IO.FileStream]::new($log.FullName, 'Open', 'Read', 'ReadWrite'))
    $timedOut = $false
    while ($true) {
        $exited = $process.WaitForExit(2000)
        while ($null -ne ($line = $reader.ReadLine())) { Write-Host $line }
        if ($exited) { break }
        if ((Get-Date) -gt $deadline) {
            $timedOut = $true
            # Kill the whole process tree.
            & taskkill /T /F /PID $process.Id | Out-Null
            $process.WaitForExit()
            break
        }
    }
    while ($null -ne ($line = $reader.ReadLine())) { Write-Host $line }
    $reader.Close()

    $code = if ($timedOut) { -1 } else { $process.ExitCode }
    if ($timedOut -or $code -ne 0) {
        $reason = if ($timedOut) { "timed out after $TimeoutMinutes min" } else { "exit code $code" }
        $tail = (Get-Content $log.FullName -Tail $TailLines) -join "`n"
        $tail = $tail.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
        Write-Host "::error title=$Title ($reason)::$tail"
        exit 1
    }
}

param(
    [Parameter(Mandatory = $true)]
    [string] $UsbDriveLetter,

    [Parameter(Mandatory = $true)]
    [string] $ModelPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Normalize-DriveLetter([string]$d) {
    $d = $d.Trim()
    if ($d.Length -eq 1) { return ($d + ':') }
    if ($d.Length -eq 2 -and $d[1] -eq ':') { return $d }
    throw "UsbDriveLetter must be like 'E' or 'E:'"
}

$drive = Normalize-DriveLetter $UsbDriveLetter
$root = "$drive\\"
if (-not (Test-Path $root)) {
    throw "Drive $drive is not accessible. Make sure the USB is mounted."
}

if (-not (Test-Path $ModelPath)) {
    throw "ModelPath not found: $ModelPath"
}

$modelName = Split-Path -Leaf $ModelPath

$destDir = $root
$destPath = Join-Path $destDir $modelName

Write-Host "Copying model to USB root..."
Write-Host "  Source: $ModelPath"
Write-Host "  Dest:   $destPath"

Copy-Item -Force $ModelPath $destPath

Write-Host "Done."
Write-Host "Next: boot USB and run /models then /model_info $modelName"

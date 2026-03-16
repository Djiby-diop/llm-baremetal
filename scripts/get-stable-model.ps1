[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$File,

  [string]$Repo = 'djibydiop/llm-baremetal',
  [string]$Revision = 'main',
  [string]$Sha256 = '',
  [string]$DestDir = 'models',
  [switch]$Force
)

$ErrorActionPreference = 'Stop'

$weightsScriptPath = Join-Path $PSScriptRoot 'get-weights.ps1'
if (-not (Test-Path -LiteralPath $weightsScriptPath)) {
  throw "Missing helper: $weightsScriptPath"
}

$url = "https://huggingface.co/$Repo/resolve/$Revision/$File"
$outName = Split-Path -Leaf $File

Write-Host "[StableModel] Repo: $Repo" -ForegroundColor Cyan
Write-Host "[StableModel] File: $File" -ForegroundColor Gray

if ($Sha256 -and $Force) {
  & $weightsScriptPath -Url $url -OutName $outName -Sha256 $Sha256 -DestDir $DestDir -Force
} elseif ($Sha256) {
  & $weightsScriptPath -Url $url -OutName $outName -Sha256 $Sha256 -DestDir $DestDir
} elseif ($Force) {
  & $weightsScriptPath -Url $url -OutName $outName -DestDir $DestDir -Force
} else {
  & $weightsScriptPath -Url $url -OutName $outName -DestDir $DestDir
}
if ($LASTEXITCODE -ne 0) {
  throw "get-weights.ps1 failed ($LASTEXITCODE)"
}
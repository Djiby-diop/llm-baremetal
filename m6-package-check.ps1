param(
  [string]$ArtifactsDir = '',
  [switch]$Rebuild,
  [switch]$NoModel,
  [string]$ModelBin = 'stories110M.bin',
  [ValidateRange(1,9)]
  [int]$XzLevel = 9
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

if (-not $ArtifactsDir) {
  $ArtifactsDir = Join-Path $PSScriptRoot 'release-artifacts'
}

if ($Rebuild) {
  Write-Host '[M6.2] Rebuilding release artifacts...' -ForegroundColor Cyan
  if ($NoModel) {
    & (Join-Path $PSScriptRoot 'release-image.ps1') -OutDir $ArtifactsDir -NoModel -XzLevel $XzLevel
  } else {
    & (Join-Path $PSScriptRoot 'release-image.ps1') -OutDir $ArtifactsDir -NoModel:$false -ModelBin $ModelBin -XzLevel $XzLevel
  }
  if ($LASTEXITCODE -ne 0) {
    throw "release-image.ps1 failed with exit code $LASTEXITCODE"
  }
}

Write-Host '[M6.2] Checking release artifacts...' -ForegroundColor Cyan
Write-Host "  Dir: $ArtifactsDir" -ForegroundColor Gray

if (-not (Test-Path $ArtifactsDir)) {
  throw "Artifacts directory not found: $ArtifactsDir"
}

$sha = Join-Path $ArtifactsDir 'SHA256SUMS.txt'
if (-not (Test-Path $sha)) {
  throw "Missing required artifact: $sha"
}

$shaLines = Get-Content -LiteralPath $sha -ErrorAction Stop
if (-not $shaLines -or $shaLines.Count -lt 2) {
  throw "Invalid checksum file (expected at least 2 entries): $sha"
}

function Get-ShaEntry {
  param([string[]]$Lines, [string]$FileName)
  foreach ($line in $Lines) {
    if ($line -match '^(?<hash>[A-Fa-f0-9]{64})\s+\*?(?<name>.+)$') {
      $name = $Matches['name'].Trim()
      if ($name -eq $FileName) {
        return $Matches['hash'].ToLowerInvariant()
      }
    }
  }
  return $null
}

$baseCandidates = @()
if ($PSBoundParameters.ContainsKey('NoModel')) {
  $baseCandidates = @($(if ($NoModel) { 'llm-baremetal-boot-nomodel' } else { 'llm-baremetal-boot' }))
} else {
  $baseCandidates = @('llm-baremetal-boot-nomodel','llm-baremetal-boot')
}

$img = $null
$xz = $null
$imgName = $null
$xzName = $null
$imgExpected = $null
$xzExpected = $null

foreach ($base in $baseCandidates) {
  $candidateImg = Join-Path $ArtifactsDir ($base + '.img')
  $candidateXz = Join-Path $ArtifactsDir ($base + '.img.xz')
  if (-not (Test-Path $candidateImg) -or -not (Test-Path $candidateXz)) {
    continue
  }

  $candidateImgName = Split-Path $candidateImg -Leaf
  $candidateXzName = Split-Path $candidateXz -Leaf
  $candidateImgExpected = Get-ShaEntry -Lines $shaLines -FileName $candidateImgName
  $candidateXzExpected = Get-ShaEntry -Lines $shaLines -FileName $candidateXzName

  if ($candidateImgExpected -and $candidateXzExpected) {
    $img = $candidateImg
    $xz = $candidateXz
    $imgName = $candidateImgName
    $xzName = $candidateXzName
    $imgExpected = $candidateImgExpected
    $xzExpected = $candidateXzExpected
    break
  }
}

if (-not $img -or -not $xz) {
  throw 'No valid artifact pair found (.img + .img.xz with matching SHA256SUMS entries).'
}

$imgActual = (Get-FileHash -Algorithm SHA256 -LiteralPath $img).Hash.ToLowerInvariant()
$xzActual = (Get-FileHash -Algorithm SHA256 -LiteralPath $xz).Hash.ToLowerInvariant()

if ($imgActual -ne $imgExpected) {
  throw "Checksum mismatch for $imgName"
}
if ($xzActual -ne $xzExpected) {
  throw "Checksum mismatch for $xzName"
}

$imgSizeMB = [math]::Round(((Get-Item -LiteralPath $img).Length / 1MB), 2)
$xzSizeMB = [math]::Round(((Get-Item -LiteralPath $xz).Length / 1MB), 2)

Write-Host '[M6.2] Package check: OK ✅' -ForegroundColor Green
Write-Host "  - $imgName ($imgSizeMB MB)" -ForegroundColor Gray
Write-Host "  - $xzName ($xzSizeMB MB)" -ForegroundColor Gray
Write-Host "  - SHA256SUMS.txt entries verified" -ForegroundColor Gray

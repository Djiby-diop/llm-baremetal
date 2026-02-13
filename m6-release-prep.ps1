param(
  [string]$ArtifactsDir = '',
  [string]$Tag = '',
  [switch]$Rebuild,
  [switch]$NoModel,
  [string]$ModelBin = 'stories110M.bin',
  [switch]$IncludeRawImage,
  [switch]$OpenFolder
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

if (-not $ArtifactsDir) {
  $ArtifactsDir = Join-Path $PSScriptRoot 'release-artifacts'
}
if (-not $Tag) {
  $Tag = 'v' + (Get-Date -Format 'yyyy.MM.dd-HHmm')
}

Write-Host '[M6.3] Preparing GitHub Release upload bundle' -ForegroundColor Cyan
Write-Host "  Artifacts: $ArtifactsDir" -ForegroundColor Gray
Write-Host "  Tag:       $Tag" -ForegroundColor Gray

$checkArgs = @{
  ArtifactsDir = $ArtifactsDir
}
if ($Rebuild) { $checkArgs.Rebuild = $true }
if ($PSBoundParameters.ContainsKey('NoModel')) { $checkArgs.NoModel = [bool]$NoModel }
if ($PSBoundParameters.ContainsKey('ModelBin')) { $checkArgs.ModelBin = $ModelBin }

& (Join-Path $PSScriptRoot 'm6-package-check.ps1') @checkArgs

$shaPath = Join-Path $ArtifactsDir 'SHA256SUMS.txt'
$shaLines = Get-Content -LiteralPath $shaPath -ErrorAction Stop

function Resolve-ReleaseBase {
  param([string]$Dir, [string[]]$ShaLines)
  $candidates = @('llm-baremetal-boot-nomodel','llm-baremetal-boot')
  foreach ($base in $candidates) {
    $img = Join-Path $Dir ($base + '.img')
    $xz = Join-Path $Dir ($base + '.img.xz')
    if ((Test-Path $img) -and (Test-Path $xz)) {
      $imgName = Split-Path $img -Leaf
      $xzName = Split-Path $xz -Leaf
      $hasImg = $false
      $hasXz = $false
      foreach ($line in $ShaLines) {
        if ($line -match '^[A-Fa-f0-9]{64}\s+\*?(?<name>.+)$') {
          $n = $Matches['name'].Trim()
          if ($n -eq $imgName) { $hasImg = $true }
          if ($n -eq $xzName) { $hasXz = $true }
        }
      }
      if ($hasImg -and $hasXz) {
        return $base
      }
    }
  }
  throw 'No valid release artifact base found.'
}

$base = Resolve-ReleaseBase -Dir $ArtifactsDir -ShaLines $shaLines
$imgName = "$base.img"
$xzName = "$base.img.xz"

$bundleDir = Join-Path $ArtifactsDir ("upload-" + $Tag)
New-Item -ItemType Directory -Force -Path $bundleDir | Out-Null

$copyList = @($xzName, 'SHA256SUMS.txt')
if ($IncludeRawImage) {
  $copyList += $imgName
}

foreach ($name in $copyList) {
  $src = Join-Path $ArtifactsDir $name
  if (-not (Test-Path $src)) {
    throw "Missing artifact for bundle: $src"
  }
  Copy-Item -LiteralPath $src -Destination (Join-Path $bundleDir $name) -Force
}

$releaseNotesPath = Join-Path $bundleDir 'RELEASE_NOTES.md'
$notes = @(
  "# llm-baremetal $Tag"
  ""
  "## Assets"
  "- $xzName"
  "- SHA256SUMS.txt"
  $(if ($IncludeRawImage) { "- $imgName" } else { "" })
  ""
  "## Verify"
  '```bash'
  'sha256sum -c SHA256SUMS.txt'
  '```'
  ""
  "## Flash (Linux example)"
  '```bash'
  "xz -d $xzName"
  "sudo dd if=$imgName of=/dev/sdX bs=4M conv=fsync status=progress"
  '```'
) | Where-Object { $_ -ne '' }

Set-Content -LiteralPath $releaseNotesPath -Value ($notes -join [Environment]::NewLine) -Encoding UTF8

Write-Host '[M6.3] Upload bundle ready ✅' -ForegroundColor Green
Write-Host "  - $(Join-Path $bundleDir $xzName)" -ForegroundColor Gray
Write-Host "  - $(Join-Path $bundleDir 'SHA256SUMS.txt')" -ForegroundColor Gray
if ($IncludeRawImage) {
  Write-Host "  - $(Join-Path $bundleDir $imgName)" -ForegroundColor Gray
}
Write-Host "  - $releaseNotesPath" -ForegroundColor Gray

Write-Host ''
Write-Host 'Next:' -ForegroundColor Cyan
Write-Host "  1) Create GitHub Release tag: $Tag"
Write-Host "  2) Upload files from: $bundleDir"
Write-Host '  3) Paste RELEASE_NOTES.md content into release description'

if ($OpenFolder) {
  Start-Process explorer.exe $bundleDir | Out-Null
}

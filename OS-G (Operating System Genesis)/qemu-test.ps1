param(
  [ValidateSet('debug','release')][string]$Profile = 'release',
  [string]$QemuPath,
  [string]$OvmfCode,
  [string]$OvmfVars,
  [ValidateSet('pass','fail')][string]$ExpectedResult = 'pass',
  [string]$RequiredRegex,
  [string]$PolicySource,
  # If set, generate weights.bin (and copy as WEIGHTS.BIN) based on @@SOMA:IO dim/layers.
  [switch]$AutoWeights,
  # When auto-generating weights, include a 16-byte header (magic OSGW, ver=1, dim, layers).
  # Note: if the policy contains @@SOMA:IO weights_header=0/1, that value overrides this switch.
  [switch]$WeightsHeader,
  # When auto-generating headered weights, corrupt the header magic to exercise the "invalid header ignored" path.
  [switch]$CorruptWeightsHeaderMagic,
  [int]$TimeoutSec = 120
)

$ErrorActionPreference = 'Stop'

# Ensure all relative paths (cargo build output, qemu-fs, weights.bin, etc.) resolve
# from the OS-G repo root that contains Cargo.toml.
Push-Location -LiteralPath $PSScriptRoot

function Get-SomaIoFromPolicyText([string]$text) {
  if (-not $text) { return @{ dim = $null; layers = $null; weights_header = $null } }

  $lines = $text -split "`r?`n"
  $in = $false
  $dim = $null
  $layers = $null
  $weightsHeader = $null

  foreach ($raw in $lines) {
    $line = $raw

    # Strip simple line comments (heuristic; policy files here are small).
    $line = $line -replace '(?m)//.*$', ''
    $line = $line -replace '(?m)^\s*[#;].*$', ''

    if ($line -match '^\s*@@') {
      $tag = ($line -replace '^\s*@@\s*', '').Trim()
      $in = ($tag -match '^(?i)SOMA:IO$|^(?i)SOMA:INTERACTIVE$')
      continue
    }

    if (-not $in) { continue }

    if ($line -match '(?i)^\s*dim\s*=\s*(\d+)\s*$') { $dim = [int]$Matches[1]; continue }
    if ($line -match '(?i)^\s*layers\s*=\s*(\d+)\s*$') { $layers = [int]$Matches[1]; continue }
    if ($line -match '(?i)^\s*weights_header\s*=\s*(\S+)\s*$') {
      $v = $Matches[1]
      if ($v -match '^(?i)(1|true|on)$') { $weightsHeader = $true; continue }
      if ($v -match '^(?i)(0|false|off)$') { $weightsHeader = $false; continue }
      continue
    }
  }

  return @{ dim = $dim; layers = $layers; weights_header = $weightsHeader }
}

function Write-WeightsBin([string]$Path, [int]$Dim, [int]$Layers, [float]$Value, [switch]$Header) {
  if ($Dim -lt 16) { $Dim = 16 }
  if ($Dim -gt 256) { $Dim = 256 }
  if ($Layers -lt 1) { $Layers = 1 }
  if ($Layers -gt 64) { $Layers = 64 }

  $floats = [int64]$Layers * [int64]$Dim * [int64]$Dim
  $payloadBytes = $floats * 4
  $headerBytes = if ($Header) { 16 } else { 0 }
  $totalBytes = $payloadBytes + $headerBytes

  $fs = $null
  $bw = $null
  try {
    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    $bw = New-Object System.IO.BinaryWriter($fs)

    if ($Header) {
      $bw.Write([byte[]][char[]]'OSGW')
      $bw.Write([UInt16]1)  # version
      $bw.Write([UInt16]0)  # reserved
      $bw.Write([UInt32]$Dim)
      $bw.Write([UInt32]$Layers)
    }

    $f = [single]$Value
    for ($i = 0; $i -lt $floats; $i++) {
      $bw.Write($f)
    }
  }
  finally {
    if ($bw) { $bw.Dispose() }
    if ($fs) { $fs.Dispose() }
  }

  Write-Host "  Generated weights.bin ($totalBytes bytes, dim=$Dim layers=$Layers header=$Header)"
}

function Resolve-QemuPath([string]$override) {
  if ($override) {
    if (-not (Test-Path $override)) { throw "QEMU not found at -QemuPath: $override" }
    return $override
  }
  $cmd = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $candidates = @(
    "C:\\Program Files\\qemu\\qemu-system-x86_64.exe",
    "C:\\Program Files (x86)\\qemu\\qemu-system-x86_64.exe",
    "C:\\msys64\\mingw64\\bin\\qemu-system-x86_64.exe",
    "C:\\msys64\\usr\\bin\\qemu-system-x86_64.exe"
  )
  $found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not $found) { throw "QEMU not found. Provide -QemuPath or install QEMU." }
  return $found
}

function Resolve-OvmfCode([string]$override) {
  if ($override) {
    if (-not (Test-Path $override)) { throw "OVMF_CODE not found at -OvmfCode: $override" }
    return $override
  }
  $candidates = @(
    "C:\\Program Files\\qemu\\share\\edk2-x86_64-code.fd",
    "C:\\Program Files (x86)\\qemu\\share\\edk2-x86_64-code.fd",
    "C:\\msys64\\usr\\share\\edk2-ovmf\\x64\\OVMF_CODE.fd",
    "C:\\msys64\\usr\\share\\ovmf\\x64\\OVMF_CODE.fd"
  )
  $found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not $found) { throw "OVMF_CODE not found. Install OVMF/edk2 or pass -OvmfCode." }
  return $found
}

function Resolve-OvmfVarsTemplate([string]$override) {
  if ($override) {
    if (-not (Test-Path $override)) { throw "OVMF_VARS not found at -OvmfVars: $override" }
    return $override
  }

  # Prefer the repo-provided vars template if available (outside OS-G, read-only access).
  $repoVars = Join-Path $PSScriptRoot "..\\ovmf-vars-temp.fd"
  if (Test-Path $repoVars) { return $repoVars }

  $candidates = @(
    "C:\\Program Files\\qemu\\share\\edk2-i386-vars.fd",
    "C:\\Program Files\\qemu\\share\\edk2-x86_64-vars.fd",
    "C:\\Program Files (x86)\\qemu\\share\\edk2-i386-vars.fd",
    "C:\\Program Files (x86)\\qemu\\share\\edk2-x86_64-vars.fd"
  )
  $found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not $found) { throw "OVMF_VARS template not found. Pass -OvmfVars." }
  return $found
}

# Ensure target exists
$installed = & rustup target list --installed 2>$null
if ($LASTEXITCODE -ne 0) {
  throw "rustup not found. Install Rust toolchain (rustup)."
}
if (-not ($installed -match 'x86_64-unknown-uefi')) {
  Write-Host "Installing Rust target x86_64-unknown-uefi..."
  & rustup target add x86_64-unknown-uefi
}

# Build EFI
$target = "x86_64-unknown-uefi"
$profileArgs = @()
if ($Profile -eq 'release') { $profileArgs += '--release' }

Write-Host "Building UEFI app..."
& cargo build @profileArgs --target $target --features uefi
if ($LASTEXITCODE -ne 0) { throw "cargo build failed" }

$efiName = "osg_uefi.efi"
$outDir = Join-Path $PSScriptRoot "target\\$target\\$Profile"
$built = Join-Path $outDir "osg_uefi.efi"
if (-not (Test-Path $built)) {
  # Cargo uses the bin name, which is file stem of src/bin.
  $built = Join-Path $outDir "osg_uefi.efi"
}
if (-not (Test-Path $built)) {
  # fallback to the default naming pattern
  $built = Join-Path $outDir "osg_uefi.efi"
}

# In practice, cargo outputs `osg_uefi.efi`? If not, locate any .efi produced.
if (-not (Test-Path $built)) {
  $built = Get-ChildItem -Path $outDir -Filter "*.efi" | Select-Object -First 1 | ForEach-Object { $_.FullName }
}
if (-not $built) { throw "No .efi produced in $outDir" }

# Prepare FAT directory
$fatRoot = Join-Path $PSScriptRoot "qemu-fs"
$bootDir = Join-Path $fatRoot "EFI\\BOOT"
New-Item -ItemType Directory -Force -Path $bootDir | Out-Null
$bootEfi = Join-Path $bootDir "BOOTX64.EFI"
$bootBackup = $null
if (Test-Path -LiteralPath $bootEfi) {
  $bootBackup = Join-Path $env:TEMP ("osg-bootx64-{0:yyyyMMdd-HHmmss}.bak" -f (Get-Date))
  Copy-Item -Force $bootEfi $bootBackup
}
Copy-Item -Force $built $bootEfi

# Add WEIGHTS.BIN to FAT root
$weightsSrc = Join-Path $PSScriptRoot "weights.bin"
$weightsDst = Join-Path $fatRoot "WEIGHTS.BIN"

if ($AutoWeights) {
  $policyText = $null
  if ($PolicySource -and (Test-Path $PolicySource)) {
    $policyText = Get-Content -LiteralPath $PolicySource -Raw -ErrorAction SilentlyContinue
  } elseif (Test-Path (Join-Path $PSScriptRoot 'policy.dplus')) {
    $policyText = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'policy.dplus') -Raw -ErrorAction SilentlyContinue
  }

  $cfg = Get-SomaIoFromPolicyText $policyText
  $dim = if ($null -ne $cfg.dim) { [int]$cfg.dim } else { 128 }

  # layers=0 means “use default allocated layers” in the UEFI demo.
  $defaultLayersAlloc = 6
  if ($null -eq $cfg.layers) {
    $layers = 1
  } elseif ([int]$cfg.layers -eq 0) {
    $layers = $defaultLayersAlloc
  } else {
    $layers = [int]$cfg.layers
  }

  $desiredHeader = if ($null -ne $cfg.weights_header) { [bool]$cfg.weights_header } else { [bool]$WeightsHeader }
  if ($null -ne $cfg.weights_header -and ([bool]$WeightsHeader -ne $desiredHeader)) {
    $p = if ($desiredHeader) { 1 } else { 0 }
    Write-Host "AutoWeights: policy overrides -WeightsHeader (weights_header=$p)"
  }
  Write-Host "AutoWeights: generating weights.bin from policy (dim=$dim layers=$layers header=$desiredHeader)"
  Write-WeightsBin -Path $weightsSrc -Dim $dim -Layers $layers -Value 0.02 -Header:$desiredHeader

  if ($CorruptWeightsHeaderMagic) {
    if (-not $desiredHeader) {
      throw "CorruptWeightsHeaderMagic requires a headered weights.bin. Set @@SOMA:IO weights_header=1 or pass -WeightsHeader."
    }
    if (-not (Test-Path $weightsSrc)) {
      throw "weights.bin not found to corrupt: $weightsSrc"
    }
    $fs = $null
    try {
      $fs = [System.IO.File]::Open($weightsSrc, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::Read)
      if ($fs.Length -lt 16) {
        throw "weights.bin too small to contain a header ($($fs.Length) bytes)."
      }
      $fs.Seek(0, [System.IO.SeekOrigin]::Begin) | Out-Null
      $bad = [byte[]][char[]]'BAD!'
      $fs.Write($bad, 0, $bad.Length)
    }
    finally {
      if ($fs) { $fs.Dispose() }
    }
    Write-Host "  Corrupted WEIGHTS.BIN header magic (BAD!)"
  }
}

if (Test-Path $weightsSrc) {
    Copy-Item -Force $weightsSrc $weightsDst
    Write-Host "  Copying weights.bin -> $weightsDst"
} else {
    Write-Warning "weights.bin not found at $weightsSrc. Soma will simulate weights."
}

$policyPath = Join-Path $fatRoot "policy.dplus"
$hadPolicy = Test-Path $policyPath
$policyBackup = $null

function Restore-Policy {
  if (-not $PolicySource) { return }

  if ($hadPolicy -and $policyBackup -and (Test-Path $policyBackup)) {
    Copy-Item -Force $policyBackup $policyPath
    Remove-Item -Force $policyBackup -ErrorAction SilentlyContinue
    return
  }

  if (-not $hadPolicy) {
    Remove-Item -Force $policyPath -ErrorAction SilentlyContinue
  }
}

function Restore-BootEfi {
  if (-not $bootBackup) { return }
  if (Test-Path -LiteralPath $bootBackup) {
    Copy-Item -Force $bootBackup $bootEfi
    Remove-Item -Force $bootBackup -ErrorAction SilentlyContinue
  }
}

if ($PolicySource) {
  if (-not (Test-Path $PolicySource)) {
    throw "PolicySource not found: $PolicySource"
  }

  $src = (Resolve-Path $PolicySource).Path
  $dst = $policyPath

  if ($hadPolicy) {
    $policyBackup = Join-Path $env:TEMP ("osg-policy-{0:yyyyMMdd-HHmmss}.bak" -f (Get-Date))
    Copy-Item -Force $policyPath $policyBackup
  }

  $dstResolved = $null
  try { $dstResolved = (Resolve-Path $dst).Path } catch { $dstResolved = $null }
  if (-not $dstResolved -or ($dstResolved -ne $src)) {
    Copy-Item -Force $src $dst
  }
}

# Prepare OVMF vars (writable copy inside OS-G)
$qemu = Resolve-QemuPath $QemuPath
$ovmfCode = Resolve-OvmfCode $OvmfCode
$varsTemplate = Resolve-OvmfVarsTemplate $OvmfVars
$varsCopy = Join-Path $PSScriptRoot "ovmf-vars-osg.fd"
Copy-Item -Force $varsTemplate $varsCopy

Write-Host "Launching QEMU..."
Write-Host "  QEMU: $qemu"
Write-Host "  OVMF_CODE: $ovmfCode"
Write-Host "  OVMF_VARS: $varsCopy"

function Read-NewText([string]$Path, [ref]$Pos) {
  if (-not (Test-Path $Path)) { return "" }
  $fs = $null
  try {
    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    if ($Pos.Value -gt $fs.Length) { $Pos.Value = 0 }
    $fs.Seek([int64]$Pos.Value, [System.IO.SeekOrigin]::Begin) | Out-Null
    $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8, $true, 4096, $true)
    $text = $sr.ReadToEnd()
    $Pos.Value = $fs.Position
    return $text
  }
  finally {
    if ($fs) { $fs.Dispose() }
  }
}

$args = @(
  '-machine', 'q35',
  '-m', '256',
  '-drive', "if=pflash,format=raw,readonly=on,file=$ovmfCode",
  '-drive', "if=pflash,format=raw,file=$varsCopy",
  '-drive', "format=raw,file=fat:rw:$fatRoot",
  '-serial', 'stdio'
)

$serialLog = Join-Path $env:TEMP ("osg-qemu-serial-{0:yyyyMMdd-HHmmss}.txt" -f (Get-Date))
$stderrLog = Join-Path $env:TEMP ("osg-qemu-stderr-{0:yyyyMMdd-HHmmss}.txt" -f (Get-Date))
Write-Host "  Serial log: $serialLog"

function Quote-Arg([string]$s) {
  if ($null -eq $s) { return '""' }
  if ($s -match '[\s"]') {
    return '"' + ($s -replace '"', '\\"') + '"'
  }
  return $s
}

# Start-Process builds a single command line; we must quote args with spaces.
$argString = ($args | ForEach-Object { Quote-Arg $_ }) -join ' '

$proc = Start-Process -FilePath $qemu -ArgumentList $argString -NoNewWindow -PassThru -RedirectStandardOutput $serialLog -RedirectStandardError $stderrLog

$start = Get-Date
$pos = 0
$sawPass = $false
$sawFail = $false
$tail = ""

try {
  while ($true) {
    $chunk = Read-NewText -Path $serialLog -Pos ([ref]$pos)
    if ($chunk) {
      # Stream to console.
      Write-Host -NoNewline $chunk

      # Match across chunk boundaries: QEMU/firmware sometimes splits serial writes.
      $tail = $tail + $chunk
      if ($tail.Length -gt 4096) {
        $tail = $tail.Substring($tail.Length - 4096)
      }

      if ($tail -match 'RESULT:\s*PASS') { $sawPass = $true; break }
      if ($tail -match 'RESULT:\s*FAIL') { $sawFail = $true; break }
    }

    if ($proc.HasExited) { break }

    if (((Get-Date) - $start).TotalSeconds -ge $TimeoutSec) {
      Write-Host "\n[timeout] No PASS/FAIL within ${TimeoutSec}s; terminating QEMU..."
      break
    }

    Start-Sleep -Milliseconds 100
  }

  if (-not $proc.HasExited) {
    try {
      Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    } catch {}
    try { $proc.WaitForExit() } catch {}
  }

  # Flush remaining output.
  $chunk = Read-NewText -Path $serialLog -Pos ([ref]$pos)
  if ($chunk) {
    Write-Host -NoNewline $chunk
    $tail = $tail + $chunk
    if ($tail.Length -gt 4096) {
      $tail = $tail.Substring($tail.Length - 4096)
    }
  }

  $requiredOk = $true
  if ($RequiredRegex) {
    if (-not (Test-Path $serialLog)) {
      $requiredOk = $false
    } else {
      $requiredOk = Select-String -Path $serialLog -Pattern $RequiredRegex -Quiet
    }
    if (-not $requiredOk) {
      Write-Host "[fail] RequiredRegex not found in serial output: $RequiredRegex"
    }
  }

  if ($sawPass) {
    if ($ExpectedResult -eq 'pass' -and $requiredOk) {
      Write-Host "[ok] Detected RESULT: PASS; QEMU terminated."
      exit 0
    }
    Write-Host "[fail] Detected RESULT: PASS but ExpectedResult=$ExpectedResult."
    exit 1
  }

  if ($sawFail) {
    if ($ExpectedResult -eq 'fail' -and $requiredOk) {
      Write-Host "[ok] Detected RESULT: FAIL (expected); QEMU terminated."
      exit 0
    }
    Write-Host "[fail] Detected RESULT: FAIL but ExpectedResult=$ExpectedResult."
    if (Test-Path $stderrLog) {
      Write-Host "--- QEMU STDERR (tail) ---"
      Get-Content -Path $stderrLog -Tail 50
    }
    exit 1
  }

  Write-Host "[fail] QEMU exited or timed out without PASS/FAIL."
  if (Test-Path $stderrLog) {
    Write-Host "--- QEMU STDERR (tail) ---"
    Get-Content -Path $stderrLog -Tail 50
  }
  exit 2
}
finally {
  Restore-BootEfi
  Restore-Policy
  Pop-Location
}

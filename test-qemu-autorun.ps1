$ErrorActionPreference = 'Stop'

# Forwarder: keep historical path stable.
$script = [System.IO.Path]::Combine($PSScriptRoot, 'tests', 'test-qemu-autorun.ps1')
if (-not (Test-Path $script)) {
  throw "llm-baremetal/tests/test-qemu-autorun.ps1 not found at: $script"
}

& $script @args
exit $LASTEXITCODE

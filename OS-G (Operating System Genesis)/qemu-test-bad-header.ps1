$ErrorActionPreference = 'Stop'

# Runs a PASS smoke test while feeding an invalid WEIGHTS.BIN header.
# Expected behavior:
# - UEFI logs: "Soma: weights header ignored (invalid)"
# - Still reaches: RESULT: PASS

$policy = Join-Path $PSScriptRoot 'policy-soma-dim64-layers6-header.dplus'

$params = @{
  PolicySource = $policy
  AutoWeights = $true
  CorruptWeightsHeaderMagic = $true
  ExpectedResult = 'pass'
  RequiredRegex = 'Soma:\s*weights header ignored \(invalid\)'
  TimeoutSec = 120
}

& (Join-Path $PSScriptRoot 'qemu-test.ps1') @params

exit $LASTEXITCODE

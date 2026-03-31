$ErrorActionPreference = 'Stop'

# Forwarder: tests harness expects run.ps1 under tests/.
$script = [System.IO.Path]::Combine($PSScriptRoot, '..', 'scripts', 'run.ps1')
& $script @args
exit $LASTEXITCODE

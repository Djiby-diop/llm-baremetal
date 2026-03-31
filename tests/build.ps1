$ErrorActionPreference = 'Stop'

# Forwarder: tests harness expects build.ps1 under tests/.
$script = [System.IO.Path]::Combine($PSScriptRoot, '..', 'scripts', 'build.ps1')
& $script @args
exit $LASTEXITCODE

Set-StrictMode -Version Latest

function Get-Fat83DirAndLeafFromModelSpec {
    param([Parameter(Mandatory=$true)][string]$ModelSpec)

    $norm = $ModelSpec -replace '/','\\'
    $dir = '::/'
    $leaf = $norm
    $inModels = $false

    if ($norm -match '^(models\\)(.+)$') {
        $dir = '::/models'
        $leaf = $Matches[2]
        $inModels = $true
    }

    return [pscustomobject]@{ Dir = $dir; Leaf = $leaf; InModels = $inModels }
}

function Resolve-Fat83AliasFromMdirLines {
    param(
        [Parameter(Mandatory=$true)][string[]]$MdirLines,
        [Parameter(Mandatory=$true)][string]$Leaf
    )

    foreach ($ln in $MdirLines) {
        $t = ($ln -as [string]).Trim()
        if (-not $t) { continue }
        if ($t -match '^Volume\s+in\s+drive') { continue }
        if ($t -match '^Volume\s+Serial\s+Number') { continue }
        if ($t -match '^Directory\s+for') { continue }
        if ($t -match '^\.+\s+<DIR>') { continue }
        if ($t -match '^\s*\d+\s+files?\b') { continue }
        if ($t -match '^\s*\d+\s+bytes\s+free\b') { continue }

        # Expected mdir format:
        #   STORIE~1 BIN  438381596 2026-02-05  21:43  stories110M.bin
        $parts = $t -split '\s+'
        if ($parts.Count -lt 3) { continue }

        $long = $parts[-1]
        if ($long -and ($long -ieq $Leaf)) {
            $short = $parts[0]
            $ext = $parts[1]
            if ($short -and $ext -and $ext -ne '<DIR>') {
                return ("{0}.{1}" -f $short, $ext)
            }
        }
    }

    return $null
}

function Resolve-Fat83ModelSpecFromMdirLines {
    param(
        [Parameter(Mandatory=$true)][string]$ModelSpec,
        [Parameter(Mandatory=$true)][string[]]$MdirLines
    )

    if (-not $ModelSpec) { return $ModelSpec }

    $info = Get-Fat83DirAndLeafFromModelSpec -ModelSpec $ModelSpec
    $alias = Resolve-Fat83AliasFromMdirLines -MdirLines $MdirLines -Leaf $info.Leaf
    if (-not $alias) { return $ModelSpec }

    if ($info.InModels) {
        return ('models\\' + $alias)
    }
    return $alias
}

# End of file

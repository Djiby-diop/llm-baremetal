# Weak Modules Focused Smoke Checks

$OO_ROOT = "C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal"

function Test-Module($name, $path) {
    Write-Host "--- Testing Module: $name ---" -ForegroundColor Cyan
    if (Test-Path "$OO_ROOT\$path") {
        Write-Host "[OK] Source found: $path" -ForegroundColor Green
    } else {
        Write-Host "[FAIL] Source missing: $path" -ForegroundColor Red
        return $false
    }
    
    # Try to compile the module object
    cd "$OO_ROOT"
    $obj = "$path\core\$name.o"
    if (Test-Path $obj) { Remove-Item $obj }
    
    Write-Host "Attempting compilation of $name..."
    # We use a mock compile just to check for syntax errors in headers/C
    gcc -c "$path\core\$name.c" -I"$path\core" -I"$path" -I"$OO_ROOT\core" -I"$OO_ROOT\..\oo-system\shared\oo-proto\include" -o "$path\core\$name.o"
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[OK] $name compiled successfully." -ForegroundColor Green
        return $true
    } else {
        Write-Host "[FAIL] $name failed to compile." -ForegroundColor Red
        return $false
    }
}

$results = @()
$results += Test-Module "calibrion" "oo-modules\calibrion-engine"
$results += Test-Module "cellion" "oo-modules\cellion-engine"
$results += Test-Module "chronion" "chronion-engine"
$results += Test-Module "collectivion" "oo-modules\collectivion-engine"
$results += Test-Module "compatibilion" "oo-modules\compatibilion-engine"
$results += Test-Module "soma_mind" "engine\ssm"

Write-Host "`n--- Final Summary ---"
if ($results -contains $false) {
    Write-Host "Some weak modules failed the smoke test. Manual intervention required." -ForegroundColor Yellow
} else {
    Write-Host "All identified weak modules passed basic smoke assertions." -ForegroundColor Green
}

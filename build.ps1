param([string]$Cxx = "g++", [ValidateSet("all", "3p3", "1p7")][string]$Budget = "all")
$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    $targets = if ($Budget -eq "all") { @("3p3", "1p7") } else { @($Budget) }
    foreach ($target in $targets) {
        $flags = @("-std=c++17", "-O2", "-static", "-s", "-pthread", "-Ithird_party")
        if ($target -eq "1p7") { $flags += @("-DTINGFENG_SEARCH_SECONDS=1.70", "-DPARALLEL_LOW_BUDGET") }
        Write-Host "Building v70-parallel-$target"
        & $Cxx @flags src/bot.cpp -o "build/bot-$target.exe"
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $target (exit $LASTEXITCODE)" }
    }
} finally { Pop-Location }

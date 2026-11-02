param([string]$Cxx='g++', [ValidateSet('all','3p3','1p7','5p5','3p5')][string]$Budget='all')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    foreach ($profile in @('parallel-3p3','parallel-1p7','hp-5p5','hp-3p5')) {
        $family,$time = $profile.Split('-')
        if ($Budget -ne 'all' -and $Budget -ne $time) { continue }
        $flags = @('-std=c++17','-O2','-static','-s','-pthread','-Ithird_party')
        if ($time -eq '1p7') { $flags += @('-DPARALLEL_LOW_BUDGET','-DTINGFENG_SEARCH_SECONDS=1.70') }
        if ($time -eq '3p5') { $flags += @('-DPARALLEL_LOW_BUDGET','-DTINGFENG_SEARCH_SECONDS=3.50') }
        Write-Host "Building v70-$profile"
        & $Cxx @flags "v70-$family.cpp" -o "build/v70-$profile.exe"
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $profile (exit $LASTEXITCODE)" }
    }
} finally { Pop-Location }

$ErrorActionPreference = "Stop"

$Forward = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $args.Count; $i++) {
    $arg = [string]$args[$i]

    if ($arg -ieq "-Clean") {
        $Forward.Add("--clean")
    }
    elseif ($arg -ieq "-Target") {
        if ($i + 1 -ge $args.Count) {
            throw "-Target requires a value"
        }
        $Forward.Add("--target")
        $i++
        $Forward.Add([string]$args[$i])
    }
    elseif ($arg -ieq "-BuildDir") {
        if ($i + 1 -ge $args.Count) {
            throw "-BuildDir requires a value"
        }
        $Forward.Add("--build-dir")
        $i++
        $Forward.Add([string]$args[$i])
    }
    else {
        $Forward.Add($arg)
    }
}

$Driver = Join-Path $PSScriptRoot "build.py"

$Py = Get-Command py -ErrorAction SilentlyContinue
if ($Py) {
    & $Py.Source -3 $Driver @Forward
    exit $LASTEXITCODE
}

$Python = Get-Command python -ErrorAction SilentlyContinue
if ($Python) {
    & $Python.Source $Driver @Forward
    exit $LASTEXITCODE
}

$Python3 = Get-Command python3 -ErrorAction SilentlyContinue
if ($Python3) {
    & $Python3.Source $Driver @Forward
    exit $LASTEXITCODE
}

throw "Python 3 is required to run the RP86 build driver."

param(
    [Parameter(Position = 0)]
    [string]$Command = "build",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Arguments
)
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
& py -3 (Join-Path $ScriptDir "$Command.py") @Arguments
exit $LASTEXITCODE

$ErrorActionPreference = "Stop"
param(
  [string]$SimCommand = $env:SIM_CMD,
  [int]$DurationSeconds = 70,
  [int]$BootDelaySeconds = 5
)

if (-not $SimCommand) {
  Write-Host "Set SIM_CMD env var or pass -SimCommand 'path/to/sim --arg'." -ForegroundColor Yellow
  exit 1
}

$log = Join-Path $env:TEMP "sim_can_repro.log"
Remove-Item -ErrorAction SilentlyContinue $log

Write-Host "Starting simulator: $SimCommand" -ForegroundColor Cyan
$simProc = Start-Process -FilePath "powershell.exe" -ArgumentList "-NoLogo","-NoProfile","-Command",$SimCommand -RedirectStandardOutput $log -RedirectStandardError $log -PassThru

Write-Host "Waiting ${BootDelaySeconds}s before powering the gauge..." -ForegroundColor Cyan
Start-Sleep -Seconds $BootDelaySeconds
Write-Host ">> Power the gauge now. Script will watch for ${DurationSeconds}s." -ForegroundColor Yellow

$start = Get-Date
Start-Sleep -Seconds $DurationSeconds

$stillRunning = $simProc.HasExited -eq $false
$lines = 0
if (Test-Path $log) {
  $lines = (Get-Content $log).Length
}

Write-Host "Simulator running: $stillRunning" -ForegroundColor Cyan
Write-Host "Simulator log lines: $lines" -ForegroundColor Cyan
if (Test-Path $log) {
  Write-Host "Last 10 log lines:" -ForegroundColor DarkGray
  Get-Content $log -Tail 10
}

if (-not $stillRunning) {
  Write-Error "Simulator exited early; check wiring/bitrate."
  exit 2
}
Write-Host "Repro done; simulator stayed alive for $((Get-Date)-$start)." -ForegroundColor Green

# Exora Client: installs the cosmetics service on a server (Linux with systemd).
# Run in PowerShell:
#   powershell -ExecutionPolicy Bypass -File .\deploy.ps1 -Server 1.2.3.4
# It tries every key in the .ssh folder; if none fits, ssh asks for the server password.
# ASCII only on purpose: Windows PowerShell reads scripts without a BOM in the local code page.
param(
	[string]$Server = "",
	[string]$User = "root",
	[int]$Port = 8770
)
# not "Stop": Windows PowerShell turns any text ssh writes to stderr (a refused key) into a stopping error
$ErrorActionPreference = "Continue"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path

if(-not $Server) { $Server = Read-Host "Server IP or domain" }
if(-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
	Write-Host "No ssh command. Install OpenSSH Client: Settings -> Apps -> Optional features" -ForegroundColor Red
	exit 1
}

# 1. which key fits
$Target = "$User@$Server"
$Key = $null
$SshDir = Join-Path $env:USERPROFILE ".ssh"
if(Test-Path $SshDir) {
	foreach($File in Get-ChildItem $SshDir -File) {
		if($File.Extension -eq ".pub") { continue }
		$Head = Get-Content $File.FullName -TotalCount 1 -ErrorAction SilentlyContinue
		if($Head -notmatch "PRIVATE KEY") { continue }
		Write-Host "Trying key $($File.Name)..."
		$Null = cmd /c "ssh -i `"$($File.FullName)`" -o IdentitiesOnly=yes -o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new $Target echo ok 2>nul"
		if($LASTEXITCODE -eq 0) { $Key = $File.FullName; break }
		Write-Host "  does not fit"
	}
}
if($Key) { Write-Host "Key works: $Key" -ForegroundColor Green }
else {
	Write-Host "No key fits for $Target." -ForegroundColor Yellow
	$Other = Read-Host "Login name on the server (Enter = $User)"
	if($Other) { $User = $Other; $Target = "$User@$Server" }
	Write-Host "ssh will ask for the password of $Target now (once)." -ForegroundColor Yellow
}

# 2. what runs on the server: the files go inside as base64 (no trouble with Windows line ends)
$ServerPy = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $Here "server.py")))
$Unit = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $Here "exora-cosmetics.service")))
$Remote = @"
set -e
if ! command -v python3 >/dev/null 2>&1; then
  (apt-get update && apt-get install -y python3) || dnf install -y python3 || yum install -y python3
fi
mkdir -p /opt/exora-cosmetics
echo $ServerPy | base64 -d > /opt/exora-cosmetics/server.py
echo $Unit | base64 -d | sed 's/EXORA_COSMETICS_PORT=8770/EXORA_COSMETICS_PORT=$Port/' > /etc/systemd/system/exora-cosmetics.service
systemctl daemon-reload
systemctl enable exora-cosmetics >/dev/null 2>&1
systemctl restart exora-cosmetics
if command -v ufw >/dev/null 2>&1 && ufw status | grep -q active; then ufw allow $Port/tcp >/dev/null; fi
if command -v firewall-cmd >/dev/null 2>&1 && firewall-cmd --state >/dev/null 2>&1; then firewall-cmd --permanent --add-port=$Port/tcp >/dev/null && firewall-cmd --reload >/dev/null; fi
sleep 2
systemctl is-active exora-cosmetics
python3 -c "import urllib.request;print(urllib.request.urlopen('http://127.0.0.1:$Port/v1/ping').read().decode())"
"@
$Remote = $Remote -replace "`r", ""
$Packed = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Remote))
$Command = "echo $Packed | base64 -d | bash"

Write-Host "Installing the service on $Target..."
if($Key) { & ssh -i $Key -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new $Target $Command }
else { & ssh -o StrictHostKeyChecking=accept-new $Target $Command }
if($LASTEXITCODE -ne 0) { Write-Host "Something went wrong on the server, send this text." -ForegroundColor Red; exit 1 }

# 3. can it be reached from outside
try {
	$Answer = Invoke-WebRequest -UseBasicParsing -TimeoutSec 10 "http://${Server}:$Port/v1/ping"
	Write-Host "DONE: the service answers at http://${Server}:$Port ($($Answer.Content))" -ForegroundColor Green
} catch {
	Write-Host "The service runs, but port $Port is closed from outside. Open it in the firewall of the hosting panel and run the script again." -ForegroundColor Yellow
}
Read-Host "Press Enter to close"

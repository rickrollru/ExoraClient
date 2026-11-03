# Exora Client: устанавливает сервис косметики на сервер (Linux с systemd).
# Запуск: правой кнопкой по файлу -> "Выполнить с помощью PowerShell", или в PowerShell:
#   powershell -ExecutionPolicy Bypass -File .\deploy.ps1 -Server 1.2.3.4
# Скрипт сам перебирает ключи из папки .ssh; если ни один не подошёл, спросит пароль сервера.
param(
	[string]$Server = "",
	[string]$User = "root",
	[int]$Port = 8770
)
$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path

if(-not $Server) { $Server = Read-Host "IP или домен сервера" }
if(-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
	Write-Host "Нет команды ssh. Установи OpenSSH: Параметры -> Приложения -> Дополнительные компоненты -> Клиент OpenSSH" -ForegroundColor Red
	exit 1
}

# 1. какой ключ подходит
$Target = "$User@$Server"
$Key = $null
$SshDir = Join-Path $env:USERPROFILE ".ssh"
if(Test-Path $SshDir) {
	foreach($File in Get-ChildItem $SshDir -File) {
		if($File.Extension -eq ".pub") { continue }
		$Head = Get-Content $File.FullName -TotalCount 1 -ErrorAction SilentlyContinue
		if($Head -notmatch "PRIVATE KEY") { continue }
		Write-Host "Пробую ключ $($File.Name)..."
		& ssh -i $File.FullName -o IdentitiesOnly=yes -o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new $Target "echo ok" 2>$null | Out-Null
		if($LASTEXITCODE -eq 0) { $Key = $File.FullName; break }
	}
}
if($Key) { Write-Host "Подошёл ключ: $Key" -ForegroundColor Green }
else { Write-Host "Ни один ключ не подошёл, сейчас ssh спросит пароль от сервера (один раз)." -ForegroundColor Yellow }

# 2. что выполнить на сервере: файлы внутри скрипта в base64 (без проблем с переводами строк Windows)
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

Write-Host "Устанавливаю сервис на $Target..."
if($Key) { & ssh -i $Key -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new $Target $Command }
else { & ssh -o StrictHostKeyChecking=accept-new $Target $Command }
if($LASTEXITCODE -ne 0) { Write-Host "На сервере что-то пошло не так, пришли этот текст." -ForegroundColor Red; exit 1 }

# 3. виден ли сервис снаружи
try {
	$Answer = Invoke-WebRequest -UseBasicParsing -TimeoutSec 10 "http://${Server}:$Port/v1/ping"
	Write-Host "Готово: сервис отвечает по адресу http://${Server}:$Port ($($Answer.Content))" -ForegroundColor Green
} catch {
	Write-Host "Сервис запущен, но снаружи порт $Port закрыт. Открой его в панели хостинга (файрвол), затем запусти скрипт ещё раз." -ForegroundColor Yellow
}
Read-Host "Нажми Enter, чтобы закрыть"

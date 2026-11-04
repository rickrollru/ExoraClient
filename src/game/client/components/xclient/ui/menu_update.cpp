// The client updates itself from the releases on GitHub: the archive of the new version (the Windows zip, the Linux
// tar.xz) is downloaded into the settings folder, checked by its sha256, and a small script puts its files over the
// game folder after the game has closed, then starts it again.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/fs.h>
#include <base/hash.h>
#include <base/io.h>
#include <base/log.h>
#include <base/process.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/version.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>

using namespace XcUi;

static const char *const UPDATE_FOLDER = "xclient/update";

#if defined(CONF_FAMILY_WINDOWS)
static const char *const UPDATE_ASSET_END = "-win64.zip";
static const char *const UPDATE_BINARY = "DDNet.exe";
#elif defined(CONF_PLATFORM_LINUX)
static const char *const UPDATE_ASSET_END = "-linux_x86_64.tar.xz";
static const char *const UPDATE_BINARY = "DDNet";
#else
static const char *const UPDATE_ASSET_END = nullptr; // no build for this system yet
static const char *const UPDATE_BINARY = "DDNet";
#endif

// "v0.4.10" against "0.4.9": the numbers one by one
int CXcMenu::CompareVersions(const char *pA, const char *pB)
{
	while(*pA && !str_isnum(*pA))
		pA++;
	while(*pB && !str_isnum(*pB))
		pB++;
	for(int i = 0; i < 4; i++)
	{
		const int A = str_toint(pA), B = str_toint(pB);
		if(A != B)
			return A < B ? -1 : 1;
		while(str_isnum(*pA))
			pA++;
		while(str_isnum(*pB))
			pB++;
		if(*pA == '.')
			pA++;
		if(*pB == '.')
			pB++;
	}
	return 0;
}

void CXcMenu::TakeRelease(const json_value *pRelease)
{
	const json_value *pTag = json_object_get(pRelease, "tag_name");
	if(!pTag || pTag->type != json_string)
		return;
	const char *pTagName = json_string_get(pTag);
	if(CompareVersions(pTagName, XCLIENT_VERSION) <= 0)
	{
		if(m_UpdateState == UPDATE_NONE || m_UpdateState == UPDATE_CHECKING)
			m_UpdateState = UPDATE_LATEST;
		return;
	}
	if(m_UpdateState == UPDATE_DOWNLOADING || m_UpdateState == UPDATE_READY)
		return;
	str_copy(m_aUpdateVersion, pTagName[0] == 'v' ? pTagName + 1 : pTagName);
	const json_value *pPage = json_object_get(pRelease, "html_url");
	m_UpdatePage = pPage && pPage->type == json_string ? json_string_get(pPage) : "https://github.com/rickrollru/ExoraClient/releases/latest";
	m_UpdateAsset.clear();
	m_UpdateAssetName.clear();
	m_UpdateAssetSize = 0;
	m_UpdateAssetSha.clear();
	const json_value *pAssets = json_object_get(pRelease, "assets");
	if(UPDATE_ASSET_END && pAssets && pAssets->type == json_array)
	{
		for(int i = 0; i < json_array_length(pAssets); i++)
		{
			const json_value *pAsset = json_array_get(pAssets, i);
			const json_value *pName = json_object_get(pAsset, "name");
			const json_value *pUrl = json_object_get(pAsset, "browser_download_url");
			if(!pName || pName->type != json_string || !pUrl || pUrl->type != json_string || !str_endswith(json_string_get(pName), UPDATE_ASSET_END))
				continue;
			// only plain file names: the name goes into a path
			const char *pFile = json_string_get(pName);
			if(str_find(pFile, "/") || str_find(pFile, "\\") || str_find(pFile, "..") || str_find(pFile, "'"))
				continue;
			m_UpdateAssetName = pFile;
			m_UpdateAsset = json_string_get(pUrl);
			const json_value *pSize = json_object_get(pAsset, "size");
			if(pSize && pSize->type == json_integer)
				m_UpdateAssetSize = pSize->u.integer;
			const json_value *pDigest = json_object_get(pAsset, "digest");
			if(pDigest && pDigest->type == json_string && str_startswith(json_string_get(pDigest), "sha256:"))
				m_UpdateAssetSha = json_string_get(pDigest) + str_length("sha256:");
			break;
		}
	}
	m_UpdateState = UPDATE_AVAILABLE;
	if(!m_UpdateToastShown && g_Config.m_XcUpdateCheck)
	{
		m_UpdateToastShown = true;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), Tr("Exora Client %s is out"), m_aUpdateVersion);
		Toast(aBuf, TOAST_INFO);
	}
}

void CXcMenu::CheckForUpdate()
{
	if(m_UpdateState == UPDATE_DOWNLOADING || m_UpdateState == UPDATE_READY)
		return;
	m_UpdatesRequested = false; // UpdateClientUpdates asks GitHub again
	m_UpdateState = UPDATE_CHECKING;
}

void CXcMenu::StartUpdateDownload()
{
	if(m_UpdateAsset.empty())
		return;
	Storage()->CreateFolder("xclient", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(UPDATE_FOLDER, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", UPDATE_FOLDER, m_UpdateAssetName.c_str());
	m_pUpdateDownload = HttpGetFile(m_UpdateAsset.c_str(), Storage(), aPath, IStorage::TYPE_SAVE);
	m_pUpdateDownload->HeaderString("User-Agent", "ExoraClient");
	m_pUpdateDownload->Timeout(CTimeout{20000, 0, 1024, 30});
	m_pUpdateDownload->MaxResponseSize(512 * 1024 * 1024);
	m_pUpdateDownload->LogProgress(HTTPLOG::FAILURE);
	m_pUpdateDownload->FailOnErrorStatus(true);
	SHA256_DIGEST Sha;
	if(!m_UpdateAssetSha.empty() && sha256_from_str(&Sha, m_UpdateAssetSha.c_str()) == 0)
		m_pUpdateDownload->ExpectSha256(Sha);
	Http()->Run(m_pUpdateDownload);
	m_UpdateState = UPDATE_DOWNLOADING;
	m_aUpdateError[0] = '\0';
}

void CXcMenu::UpdateSelfUpdate()
{
	if(g_Config.m_XcUpdateCheck || m_UpdateState == UPDATE_CHECKING)
		UpdateClientUpdates();
	if(m_UpdateState == UPDATE_CHECKING && m_UpdatesRequested && !m_pUpdatesRequest)
		m_UpdateState = m_aUpdateVersion[0] ? UPDATE_AVAILABLE : UPDATE_LATEST; // the answer came (or failed)
	if(m_UpdateState != UPDATE_DOWNLOADING || !m_pUpdateDownload || !m_pUpdateDownload->Done())
		return;
	const EHttpState State = m_pUpdateDownload->State();
	m_pUpdateDownload = nullptr;
	if(State == EHttpState::DONE)
	{
		m_UpdateState = UPDATE_READY;
		Toast(Tr("The update is downloaded, restart to put it on"), TOAST_SUCCESS);
	}
	else
	{
		m_UpdateState = UPDATE_FAILED;
		str_copy(m_aUpdateError, Tr("Could not download the update, try again"));
	}
}

// a path for a script in single quotes: ' becomes '' (PowerShell) or '\'' (sh)
static std::string Quote(const char *pPath, bool PowerShell)
{
	std::string Result = "'";
	for(const char *p = pPath; *p; p++)
	{
		if(*p == '\'')
			Result += PowerShell ? "''" : "'\\''";
		else
			Result += *p;
	}
	return Result + "'";
}

bool CXcMenu::InstallUpdate()
{
	if(m_UpdateState != UPDATE_READY)
		return false;
	char aGameExe[IO_MAX_PATH_LENGTH];
	Storage()->GetBinaryPathAbsolute(UPDATE_BINARY, aGameExe, sizeof(aGameExe));
	char aGameDir[IO_MAX_PATH_LENGTH];
	str_copy(aGameDir, aGameExe);
	{
		const int Length = str_length(aGameDir);
		int Cut = Length;
		while(Cut > 0 && aGameDir[Cut - 1] != '/' && aGameDir[Cut - 1] != '\\')
			Cut--;
		aGameDir[Cut > 1 ? Cut - 1 : Cut] = '\0';
	}
	// the game folder must take new files (not under Program Files and the like)
	{
		char aTest[IO_MAX_PATH_LENGTH];
		str_format(aTest, sizeof(aTest), "%s/xc_update_test.tmp", aGameDir);
		IOHANDLE File = io_open(aTest, IOFLAG_WRITE);
		if(!File)
		{
			m_UpdateState = UPDATE_FAILED;
			str_copy(m_aUpdateError, Tr("The game folder cannot be written to: download the update from the release page"));
			return false;
		}
		io_close(File);
		if(fs_remove(aTest) != 0)
			log_warn("xclient", "could not remove %s", aTest);
	}
	char aRelative[IO_MAX_PATH_LENGTH];
	char aArchive[IO_MAX_PATH_LENGTH], aWork[IO_MAX_PATH_LENGTH], aFiles[IO_MAX_PATH_LENGTH], aLog[IO_MAX_PATH_LENGTH], aScript[IO_MAX_PATH_LENGTH];
	str_format(aRelative, sizeof(aRelative), "%s/%s", UPDATE_FOLDER, m_UpdateAssetName.c_str());
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, aRelative, aArchive, sizeof(aArchive));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, UPDATE_FOLDER, aWork, sizeof(aWork));
	str_format(aFiles, sizeof(aFiles), "%s/files", aWork);
	str_format(aLog, sizeof(aLog), "%s/install.log", aWork);
	const int Pid = process_id();
	std::string Text;
	char aLine[256];
#if defined(CONF_FAMILY_WINDOWS)
	str_format(aScript, sizeof(aScript), "%s/install.ps1", aWork);
	const bool Ps = true;
	Text += "\xEF\xBB\xBF"; // UTF-8 with BOM, or Windows PowerShell reads Cyrillic paths wrong
	Text += "$ErrorActionPreference = 'Continue'\r\n";
	Text += "$log = " + Quote(aLog, Ps) + "\r\n";
	Text += "$files = " + Quote(aFiles, Ps) + "\r\n";
	Text += "$game = " + Quote(aGameDir, Ps) + "\r\n";
	Text += "'Exora Client update' | Out-File -LiteralPath $log -Encoding utf8\r\n";
	str_format(aLine, sizeof(aLine), "Wait-Process -Id %d -ErrorAction SilentlyContinue\r\n", Pid);
	Text += aLine;
	Text += "Start-Sleep -Milliseconds 500\r\n";
	Text += "Remove-Item -LiteralPath $files -Recurse -Force -ErrorAction SilentlyContinue\r\n";
	Text += "try { Expand-Archive -LiteralPath " + Quote(aArchive, Ps) + " -DestinationPath $files -Force -ErrorAction Stop } catch { $_ | Out-File -LiteralPath $log -Append -Encoding utf8 }\r\n";
	Text += "$exe = Get-ChildItem -LiteralPath $files -Recurse -Filter 'DDNet.exe' -ErrorAction SilentlyContinue | Select-Object -First 1\r\n";
	Text += "if($exe) {\r\n";
	// robocopy waits and tries again while a file is still locked; below 8 is success
	Text += "\trobocopy $exe.DirectoryName $game /E /R:20 /W:1 /NFL /NDL /NJH /NJS /NP | Out-File -LiteralPath $log -Append -Encoding utf8\r\n";
	Text += "\t\"robocopy $LASTEXITCODE\" | Out-File -LiteralPath $log -Append -Encoding utf8\r\n";
	Text += "} else { 'no DDNet.exe in the archive' | Out-File -LiteralPath $log -Append -Encoding utf8 }\r\n";
	Text += "Remove-Item -LiteralPath $files -Recurse -Force -ErrorAction SilentlyContinue\r\n";
	Text += "Remove-Item -LiteralPath " + Quote(aArchive, Ps) + " -Force -ErrorAction SilentlyContinue\r\n";
	Text += "Start-Process -FilePath (Join-Path $game 'DDNet.exe') -WorkingDirectory $game\r\n";
#else
	str_format(aScript, sizeof(aScript), "%s/install.sh", aWork);
	const bool Ps = false;
	Text += "#!/bin/sh\n";
	Text += "log=" + Quote(aLog, Ps) + "\n";
	Text += "files=" + Quote(aFiles, Ps) + "\n";
	Text += "game=" + Quote(aGameDir, Ps) + "\n";
	Text += "echo 'Exora Client update' > \"$log\"\n";
	str_format(aLine, sizeof(aLine), "while kill -0 %d 2>/dev/null; do sleep 0.3; done\n", Pid);
	Text += aLine;
	Text += "rm -rf \"$files\"; mkdir -p \"$files\"\n";
	Text += "tar -xf " + Quote(aArchive, Ps) + " -C \"$files\" >> \"$log\" 2>&1\n";
	Text += "exe=$(find \"$files\" -type f -name DDNet | head -n 1)\n";
	Text += "if [ -n \"$exe\" ]; then cp -rf \"$(dirname \"$exe\")\"/. \"$game\"/ >> \"$log\" 2>&1 && echo copied >> \"$log\"; else echo 'no DDNet in the archive' >> \"$log\"; fi\n";
	Text += "rm -rf \"$files\"; rm -f " + Quote(aArchive, Ps) + "\n";
	Text += "cd \"$game\" && nohup ./DDNet > /dev/null 2>&1 &\n";
#endif
	IOHANDLE File = io_open(aScript, IOFLAG_WRITE);
	if(!File)
	{
		m_UpdateState = UPDATE_FAILED;
		str_copy(m_aUpdateError, Tr("Could not write the update script"));
		return false;
	}
	io_write(File, Text.data(), Text.size());
	io_close(File);

#if defined(CONF_FAMILY_WINDOWS)
	char aShell[IO_MAX_PATH_LENGTH];
	const char *pRoot = getenv("SystemRoot");
	str_format(aShell, sizeof(aShell), "%s\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", pRoot ? pRoot : "C:\\Windows");
	const char *apArgs[] = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", aScript};
	const PROCESS Process = process_execute(aShell, EShellExecuteWindowState::HIDDEN, apArgs, std::size(apArgs));
#else
	const char *apArgs[] = {aScript};
	const PROCESS Process = process_execute("/bin/sh", EShellExecuteWindowState::HIDDEN, apArgs, std::size(apArgs));
#endif
	if(Process == INVALID_PROCESS)
	{
		m_UpdateState = UPDATE_FAILED;
		str_copy(m_aUpdateError, Tr("Could not start the update"));
		return false;
	}
	// the script puts the files over the game after it has closed and starts it again
	Client()->Quit();
	return true;
}

// a line on the home page while there is something to do
void CXcMenu::RenderUpdateBanner(float Width)
{
	if(!g_Config.m_XcUpdateCheck || (m_UpdateState != UPDATE_AVAILABLE && m_UpdateState != UPDATE_DOWNLOADING && m_UpdateState != UPDATE_READY && m_UpdateState != UPDATE_FAILED))
		return;
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Height = S(52.0f);
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	pDraw->AddRectFilled(Min, Max, Col(t.m_Card), S(12.0f));
	pDraw->AddRect(Min, Max, Col(t.m_Accent, 0.55f), S(12.0f), 0, 1.0f);
	ImFont *pBold = Fonts().m_pTitle;
	ImFont *pSmall = Fonts().m_pSmall;
	char aTitle[128];
	str_format(aTitle, sizeof(aTitle), Tr("Exora Client %s is out"), m_aUpdateVersion);
	const char *pText = m_UpdateState == UPDATE_DOWNLOADING ? Tr("Downloading...") : (m_UpdateState == UPDATE_READY ? Tr("Downloaded, the game restarts to put it on") : (m_UpdateState == UPDATE_FAILED ? m_aUpdateError : Tr("See what's new below")));
	const float Pad = S(14.0f);
	DrawIcon(pDraw, Fonts().m_pIcons, S(18.0f), ImVec2(Min.x + Pad + S(9.0f), Min.y + Height * 0.5f), Col(t.m_Accent), XC_ICON_ROTATE);
	const float TextX = Min.x + Pad + S(28.0f);
	pDraw->AddText(pBold, pBold->FontSize, ImVec2(TextX, Min.y + S(9.0f)), Col(t.m_Text), aTitle);
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(TextX, Min.y + S(29.0f)), Col(t.m_TextMuted), pText);
	if(m_UpdateState == UPDATE_DOWNLOADING && m_pUpdateDownload)
	{
		const double Total = m_pUpdateDownload->Size() > 0.0 ? m_pUpdateDownload->Size() : (double)m_UpdateAssetSize;
		const float Progress = Total > 0.0 ? (float)std::clamp(m_pUpdateDownload->Current() / Total, 0.0, 1.0) : 0.0f;
		const ImVec2 BarMin(TextX, Max.y - S(7.0f)), BarMax(Max.x - Pad, Max.y - S(4.0f));
		pDraw->AddRectFilled(BarMin, BarMax, Col(t.m_Border), S(2.0f));
		pDraw->AddRectFilled(BarMin, ImVec2(BarMin.x + (BarMax.x - BarMin.x) * Progress, BarMax.y), Col(t.m_Accent), S(2.0f));
	}
	else
	{
		const char *pButton = nullptr;
		if(m_UpdateState == UPDATE_READY)
			pButton = Tr("Restart and update");
		else if(m_UpdateState == UPDATE_AVAILABLE || m_UpdateState == UPDATE_FAILED)
			pButton = m_UpdateAsset.empty() ? Tr("Open the release") : Tr("Update");
		if(pButton)
		{
			const float ButtonWidth = std::max(S(120.0f), ImGui::CalcTextSize(pButton).x + S(28.0f));
			ImGui::SetCursorScreenPos(ImVec2(Max.x - Pad - ButtonWidth, Min.y + (Height - S(34.0f)) * 0.5f));
			ImGui::PushID("##update_banner");
			if(Button(pButton, true, ButtonWidth))
			{
				if(m_UpdateState == UPDATE_READY)
					InstallUpdate();
				else if(m_UpdateAsset.empty())
					Client()->ViewLink(m_UpdatePage.c_str());
				else
					StartUpdateDownload();
			}
			ImGui::PopID();
		}
	}
	ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
	ImGui::Dummy(ImVec2(Width, 0.0f));
}

// Settings -> Interface: the version, the check and the update by hand
void CXcMenu::RenderUpdateCard()
{
	if(!BeginCard("##updates", Tr("Updates"), Tr("New versions of Exora Client from GitHub: downloaded and put on by the client itself, your settings stay")))
		return;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s %s", Tr("This version:"), XCLIENT_VERSION);
	ParamLabel(aBuf);
	ToggleRow(Tr("Look for updates at start"), "xc_update_check", &g_Config.m_XcUpdateCheck);
	const char *pStatus = "";
	switch(m_UpdateState)
	{
	case UPDATE_CHECKING: pStatus = Tr("Looking for an update..."); break;
	case UPDATE_LATEST: pStatus = Tr("This is the newest version"); break;
	case UPDATE_AVAILABLE: str_format(aBuf, sizeof(aBuf), Tr("Exora Client %s is out"), m_aUpdateVersion); pStatus = aBuf; break;
	case UPDATE_DOWNLOADING: pStatus = Tr("Downloading..."); break;
	case UPDATE_READY: pStatus = Tr("Downloaded, the game restarts to put it on"); break;
	case UPDATE_FAILED: pStatus = m_aUpdateError; break;
	default: break;
	}
	if(pStatus[0])
		Description(pStatus);
	const float Half = (AvailWidth() - S(8.0f)) * 0.5f;
	if(Button(Tr("Check now"), false, Half))
		CheckForUpdate();
	ImGui::SameLine(0.0f, S(8.0f));
	if(m_UpdateState == UPDATE_READY)
	{
		if(Button(Tr("Restart and update"), true, Half))
			InstallUpdate();
	}
	else if(m_UpdateState == UPDATE_AVAILABLE || m_UpdateState == UPDATE_FAILED)
	{
		if(Button(m_UpdateAsset.empty() ? Tr("Open the release") : Tr("Update"), true, Half))
		{
			if(m_UpdateAsset.empty())
				Client()->ViewLink(m_UpdatePage.c_str());
			else
				StartUpdateDownload();
		}
	}
	EndCard();
}

// Settings -> Textures: the game's asset sets (game, emoticons, particles, HUD, extras, entities)
// as a gallery with previews. Picking one writes the cl_asset_* setting, DDNet reloads it right away.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/stb/stb_image.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <set>

using namespace XcUi;

namespace
{
struct SAssetType
{
	const char *m_pFolder; // assets/<folder>, also the name of the default picture
	const char *m_pConfig;
	char *m_pValue;
	float m_Aspect; // width / height of the picture
};

const SAssetType &AssetType(int Type)
{
	static const SAssetType s_aTypes[] = {
		{"game", "cl_asset_game", g_Config.m_ClAssetGame, 2.0f},
		{"emoticons", "cl_asset_emoticons", g_Config.m_ClAssetEmoticons, 1.0f},
		{"particles", "cl_asset_particles", g_Config.m_ClAssetParticles, 1.0f},
		{"hud", "cl_asset_hud", g_Config.m_ClAssetHud, 1.0f},
		{"extras", "cl_asset_extras", g_Config.m_ClAssetExtras, 1.0f},
		{"entities", "cl_assets_entities", g_Config.m_ClAssetsEntities, 1.0f},
	};
	return s_aTypes[Type];
}

struct SScanUser
{
	std::set<std::string> *m_pNames;
};

int AssetScanCallback(const char *pName, int IsDir, int DirType, void *pUser)
{
	(void)DirType;
	SScanUser *pScan = static_cast<SScanUser *>(pUser);
	if(pName[0] == '.')
		return 0;
	std::string Name;
	if(IsDir)
		Name = pName;
	else if(str_endswith_nocase(pName, ".png"))
		Name = std::string(pName, str_length(pName) - 4);
	else
		return 0;
	// default is reserved for the built-in set
	if(Name != "default" && Name.size() < 50)
		pScan->m_pNames->insert(Name);
	return 0;
}
} // namespace

void CXcMenu::ScanAssets(int Type)
{
	for(SAssetEntry &Entry : m_avAssets[Type])
		if(Entry.m_Loaded)
			Graphics()->UnloadTexture(&Entry.m_Preview);
	m_avAssets[Type].clear();

	std::set<std::string> Names;
	SScanUser Scan = {&Names};
	char aFolder[64];
	str_format(aFolder, sizeof(aFolder), "assets/%s", AssetType(Type).m_pFolder);
	Storage()->ListDirectory(IStorage::TYPE_ALL, aFolder, AssetScanCallback, &Scan);

	SAssetEntry Default;
	Default.m_Name = "default";
	m_avAssets[Type].push_back(Default);
	for(const std::string &Name : Names)
	{
		SAssetEntry Entry;
		Entry.m_Name = Name;
		m_avAssets[Type].push_back(Entry);
	}
	m_aAssetsScanned[Type] = true;
}

void CXcMenu::UnloadAssetPreviews()
{
	for(int Type = 0; Type < NUM_ASSET_TYPES; Type++)
	{
		for(SAssetEntry &Entry : m_avAssets[Type])
			if(Entry.m_Loaded)
				Graphics()->UnloadTexture(&Entry.m_Preview);
		m_avAssets[Type].clear();
		m_aAssetsScanned[Type] = false;
	}
}

static IGraphics::CTextureHandle LoadAssetPreview(IGraphics *pGraphics, IStorage *pStorage, int Type, const char *pName)
{
	const char *pFolder = AssetType(Type).m_pFolder;
	const bool Entities = str_comp(pFolder, "entities") == 0;
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pName, "default") == 0)
	{
		if(Entities)
			str_copy(aPath, "editor/entities_clear/ddnet.png");
		else
			str_format(aPath, sizeof(aPath), "%s.png", pFolder);
		return pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
	// a set is either one picture or a folder (entities: one picture per game mod)
	str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pFolder, pName);
	if(!pStorage->FileExists(aPath, IStorage::TYPE_ALL))
		str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pFolder, pName, Entities ? "ddnet" : pFolder);
	return pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
}

void CXcMenu::HandleAssetFile(const char *pPath)
{
	if(!str_endswith_nocase(pPath, ".png"))
	{
		Toast(Tr("Textures have to be png files"), TOAST_ERROR);
		return;
	}
	void *pData;
	unsigned Size;
	if(!Storage()->ReadFile(pPath, IStorage::TYPE_ABSOLUTE, &pData, &Size))
	{
		Toast(Tr("Could not read the file"), TOAST_ERROR);
		return;
	}
	int Width, Height, Comp;
	if(!stbi_info_from_memory((const stbi_uc *)pData, (int)Size, &Width, &Height, &Comp))
	{
		free(pData);
		Toast(Tr("This is not a picture"), TOAST_ERROR);
		return;
	}

	char aName[64];
	char aExtension[16];
	fs_split_file_extension(fs_filename(pPath), aName, sizeof(aName), aExtension, sizeof(aExtension));
	for(char *p = aName; *p; p++)
	{
		if(*p == '"' || *p == ';' || *p == '\\' || *p == '/' || *p == ' ')
			*p = '_';
	}
	if(str_comp(aName, "default") == 0)
		str_copy(aName, "default_2");

	const SAssetType &Asset = AssetType(m_AssetType);
	char aFolder[64];
	str_format(aFolder, sizeof(aFolder), "assets/%s", Asset.m_pFolder);
	Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(aFolder, IStorage::TYPE_SAVE);
	char aTarget[IO_MAX_PATH_LENGTH];
	str_format(aTarget, sizeof(aTarget), "%s/%s.png", aFolder, aName);
	IOHANDLE File = Storage()->OpenFile(aTarget, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	bool Success = false;
	if(File)
	{
		Success = io_write(File, pData, Size) == Size;
		io_close(File);
	}
	free(pData);
	if(!Success)
	{
		Toast(Tr("Could not save the file"), TOAST_ERROR);
		return;
	}
	// rescan on the next frame: this frame still draws the old previews
	m_aAssetsScanned[m_AssetType] = false;
	SetConfigString(Asset.m_pConfig, aName);
	Toast(Tr("Textures added and turned on"));
}

void CXcMenu::RenderSettingsTextures()
{
	const STheme &t = Theme();
	BeginCardGrid("##textures", 1);
	if(BeginCard("##tex_sets", Tr("Game textures"), Tr("Weapons, emotes, particles, HUD icons and entities. Click a set to use it")))
	{
		const char *apTypes[NUM_ASSET_TYPES] = {Tr("Game"), Tr("Emotes"), Tr("Particles"), Tr("HUD"), Tr("Extras"), Tr("Entities")};
		Segmented("##tex_type", &m_AssetType, apTypes, NUM_ASSET_TYPES);
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

		const int Type = m_AssetType;
		if(!m_aAssetsScanned[Type])
			ScanAssets(Type);
		const SAssetType &Asset = AssetType(Type);

		// previews load a few per frame so a big collection does not stall the menu
		int Budget = 4;
		CTileGrid Grid(AvailWidth(), S(Asset.m_Aspect > 1.5f ? 300.0f : 170.0f), S(14.0f));
		for(SAssetEntry &Entry : m_avAssets[Type])
		{
			if(!Entry.m_Loaded && Budget > 0)
			{
				Entry.m_Preview = LoadAssetPreview(Graphics(), Storage(), Type, Entry.m_Name.c_str());
				Entry.m_Loaded = true;
				Budget--;
			}
			Grid.Next();
			const bool Selected = str_comp(Asset.m_pValue, Entry.m_Name.c_str()) == 0;
			const char *pName = Entry.m_Name == "default" ? Tr("Default") : Entry.m_Name.c_str();
			char aId[80];
			str_format(aId, sizeof(aId), "##tex_%s", Entry.m_Name.c_str());
			const IGraphics::CTextureHandle Texture = Entry.m_Loaded ? Entry.m_Preview : IGraphics::CTextureHandle();
			if(BackgroundTile(aId, Texture, Asset.m_Aspect, pName, Selected ? Tr("In use") : nullptr, Selected, false, nullptr, Grid.Width(), 0.0f, 1.0f, Asset.m_Aspect > 1.5f ? 2.0f : 1.0f, true) && !Selected)
			{
				SetConfigString(Asset.m_pConfig, Entry.m_Name.c_str());
				PlayUiSound(SOUND_TOGGLE_ON);
			}
		}

		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		// add by path, or open the folder in the file manager
		const float ButtonWidth = S(130.0f);
		const float FolderWidth = S(150.0f);
		const float Gap = S(8.0f);
		InputText("##tex_path", m_aAssetPath, sizeof(m_aAssetPath), Tr("Path to a png"), std::max(AvailWidth() - ButtonWidth - FolderWidth - Gap * 2.0f, S(100.0f)));
		const bool Enter = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
		ImGui::SameLine(0.0f, Gap);
		if((Button(Tr("Add"), true, ButtonWidth) || Enter) && m_aAssetPath[0])
		{
			// "Copy as path" in Windows puts quotes around the path
			char aPath[IO_MAX_PATH_LENGTH];
			const char *pStart = m_aAssetPath[0] == '"' ? m_aAssetPath + 1 : m_aAssetPath;
			str_copy(aPath, pStart);
			const int Length = str_length(aPath);
			if(Length > 0 && aPath[Length - 1] == '"')
				aPath[Length - 1] = '\0';
			HandleAssetFile(aPath);
			m_aAssetPath[0] = '\0';
		}
		ImGui::SameLine(0.0f, Gap);
		if(Button(Tr("Open folder"), false, FolderWidth))
		{
			char aFolder[IO_MAX_PATH_LENGTH];
			char aRelative[64];
			str_format(aRelative, sizeof(aRelative), "assets/%s", Asset.m_pFolder);
			Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
			Storage()->CreateFolder(aRelative, IStorage::TYPE_SAVE);
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, aRelative, aFolder, sizeof(aFolder));
			Client()->ViewFile(aFolder);
			// files copied there by hand show up when the page is opened again
			m_aAssetsScanned[Type] = false;
		}
		ImGui::PushFont(Fonts().m_pSmall);
		ImGui::TextColored(t.m_TextMuted, "%s", Tr("Drag a png onto the game window to add it to the open category. Sets from skins.tw and the DDNet forum work as they are"));
		ImGui::PopFont();
		EndCard();
	}
	EndCardGrid();
}

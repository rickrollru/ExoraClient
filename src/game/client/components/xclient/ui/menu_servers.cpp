#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/favorites.h>
#include <engine/friends.h>
#include <engine/input.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/client/components/community_icons.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cstdlib>
#include <optional>

using namespace XcUi;

// the server browser works on the DDNet state (ui_page, br_filter_*, the community filters),
// so the classic browser and this one always show the same list

enum
{
	PANEL_SERVER = 0,
	PANEL_FRIENDS,
	PANEL_FILTERS,
	NUM_PANELS,
};

static ImVec4 PingColor(int Ping)
{
	if(Ping < 80)
		return ImVec4(0.24f, 0.84f, 0.64f, 1.0f); // mint
	if(Ping < 150)
		return ImVec4(1.0f, 0.78f, 0.3f, 1.0f);
	return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
}

static ImVec4 ToImVec4(ColorRGBA Color)
{
	return ImVec4(Color.r, Color.g, Color.b, Color.a);
}

// one entry of a DDNet filter list (communities, countries, types) clicked, like the classic browser:
// all shown -> only this one, the last one shown -> all again, otherwise this one on or off
static void ToggleFilterItem(IFilterList &Filter, const std::vector<const char *> &vpNames, int Index, const char *pNoneName)
{
	const char *pName = vpNames[Index];
	if(Filter.Empty())
	{
		for(int i = 0; i < (int)vpNames.size(); i++)
			if(i != Index && !(pNoneName && str_comp(vpNames[i], pNoneName) == 0))
				Filter.Add(vpNames[i]);
		return;
	}
	const bool Active = !Filter.Filtered(pName);
	bool OthersHidden = true;
	for(int i = 0; i < (int)vpNames.size(); i++)
		if(i != Index && !Filter.Filtered(vpNames[i]) && !(pNoneName && str_comp(vpNames[i], pNoneName) == 0))
			OthersHidden = false;
	if(Active && OthersHidden)
	{
		for(const char *pOther : vpNames)
			Filter.Remove(pOther);
	}
	else if(Active)
		Filter.Add(pName);
	else
		Filter.Remove(pName);
}

static void ShowAllFilterItems(IFilterList &Filter, const std::vector<const char *> &vpNames)
{
	for(const char *pName : vpNames)
		Filter.Remove(pName);
}

// a pill that can be on or off, with an optional picture in front; returns true when clicked
static bool FilterChip(const char *pId, const char *pLabel, bool On, float PictureWidth = 0.0f, ImVec2 *pPictureMin = nullptr)
{
	ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
	const STheme &t = Theme();
	ImFont *pFont = Fonts().m_pSmall;
	const float Height = S(26.0f);
	const ImVec2 TextSize = pLabel && pLabel[0] ? pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, pLabel) : ImVec2(0.0f, 0.0f);
	const float Gap = PictureWidth > 0.0f && TextSize.x > 0.0f ? S(6.0f) : 0.0f;
	const ImVec2 Size(PictureWidth + Gap + TextSize.x + S(20.0f), Height);
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const ImRect Rect(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y));
	const ImGuiID Id = pWindow->GetID(pId);
	ImGui::ItemSize(Rect);
	if(!ImGui::ItemAdd(Rect, Id))
		return false;
	bool Hovered, Held;
	const bool Pressed = ImGui::ButtonBehavior(Rect, Id, &Hovered, &Held);
	if(Pressed)
		PlayUiSound(SOUND_CLICK);
	const float Shown = Animate(Id, On ? 1.0f : 0.0f, 16.0f);
	const float Hover = Animate(Id + 1, Hovered ? 1.0f : 0.0f, 18.0f);
	ImDrawList *pDraw = pWindow->DrawList;
	pDraw->AddRectFilled(Rect.Min, Rect.Max, Col(Mix(t.m_Track, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.22f), Shown), 1.0f + Hover * 0.3f), Height * 0.5f);
	pDraw->AddRect(Rect.Min, Rect.Max, Col(t.m_Accent, Shown * 0.8f), Height * 0.5f, 0, 1.0f);
	if(pPictureMin)
		*pPictureMin = ImVec2(Rect.Min.x + S(10.0f), Rect.Min.y);
	if(TextSize.x > 0.0f)
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Rect.Min.x + S(10.0f) + PictureWidth + Gap, Rect.GetCenter().y - TextSize.y * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, std::max(Shown, Hover))), pLabel);
	return Pressed;
}

// chips flow like words: the next one goes to a new line when it does not fit
static void FlowNext(float &x, float NextWidth, float Avail, float Gap)
{
	if(x > 0.0f && x + NextWidth <= Avail)
		ImGui::SameLine(0.0f, Gap);
	else
		x = 0.0f;
	x += NextWidth + Gap;
}

static bool ToggleLine(const char *pLabel, int *pValue)
{
	RowLabel(pLabel, S(38.0f));
	ImGui::PushID(pLabel);
	const bool Changed = ToggleInt("##toggle", pValue);
	ImGui::PopID();
	return Changed;
}

void CXcMenu::DrawFlag(ImDrawList *pDraw, int Country, ImVec2 Min, float Height, float Alpha)
{
	const CCountryFlags::CCountryFlag &Flag = GameClient()->m_CountryFlags.GetByCountryCode(Country);
	if(!Flag.m_Texture.IsValid())
		return;
	pDraw->AddImageRounded((ImTextureID)(uintptr_t)m_Backend.TextureId(Flag.m_Texture), Min, ImVec2(Min.x + Height * 2.0f, Min.y + Height), ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, Alpha)), S(2.0f));
}

void CXcMenu::DrawCommunityIcon(ImDrawList *pDraw, const char *pCommunityId, ImVec2 Min, float Height, bool Active)
{
	const CCommunityIcon *pIcon = GameClient()->m_Menus.m_CommunityIcons.Find(pCommunityId);
	if(!pIcon)
		return;
	const IGraphics::CTextureHandle Texture = Active ? pIcon->m_OrgTexture : pIcon->m_GreyTexture;
	if(Texture.IsValid())
		pDraw->AddImage((ImTextureID)(uintptr_t)m_Backend.TextureId(Texture), Min, ImVec2(Min.x + Height * 2.0f, Min.y + Height), ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, Active ? 1.0f : 0.5f)));
}

bool CXcMenu::DrawMapPreview(ImDrawList *pDraw, const CServerInfo *pInfo, ImVec2 Min, ImVec2 Max, float Rounding, int Corners, float Alpha)
{
	const CXcMapPreviews::SPreview *pPreview = m_MapPreviews.Find(pInfo->m_aMap, pInfo->m_MapCrc);
	if(!pPreview || pPreview->m_Width <= 0 || pPreview->m_Height <= 0)
		return false;
	// cover the rect: the middle of a long map rather than a thin line of all of it
	const float AreaAspect = (Max.x - Min.x) / std::max(Max.y - Min.y, 1.0f);
	const float ImageAspect = pPreview->m_Width / (float)pPreview->m_Height;
	ImVec2 Uv0(0.0f, 0.0f), Uv1(1.0f, 1.0f);
	if(ImageAspect > AreaAspect)
	{
		const float Part = AreaAspect / ImageAspect;
		Uv0.x = 0.5f - Part * 0.5f;
		Uv1.x = 0.5f + Part * 0.5f;
	}
	else
	{
		const float Part = ImageAspect / AreaAspect;
		Uv0.y = 0.5f - Part * 0.5f;
		Uv1.y = 0.5f + Part * 0.5f;
	}
	pDraw->AddImageRounded((ImTextureID)(uintptr_t)m_Backend.TextureId(pPreview->m_Texture), Min, Max, Uv0, Uv1, Col(ImVec4(1, 1, 1, 1), Alpha), Rounding, Corners);
	return true;
}

void CXcMenu::UpdateBrowser()
{
	m_MapPreviews.Update();
	CMenus &Menus = GameClient()->m_Menus;
	// the classic menu does this in its render, which does not run while the new menu is open
	Menus.m_CommunityIcons.Update();
	if(Menus.m_CreateDefaultFavoriteCommunities && ServerBrowser()->DDNetInfoAvailable())
	{
		// the first start: DDNet becomes the first community tab, as in the classic menu
		Menus.m_CreateDefaultFavoriteCommunities = false;
		if(ServerBrowser()->Community(IServerBrowser::COMMUNITY_DDNET) != nullptr)
		{
			ServerBrowser()->FavoriteCommunitiesFilter().Clear();
			ServerBrowser()->FavoriteCommunitiesFilter().Add(IServerBrowser::COMMUNITY_DDNET);
			g_Config.m_UiPage = CMenus::PAGE_FAVORITE_COMMUNITY_1;
		}
	}
	if(g_Config.m_UiPage < CMenus::PAGE_INTERNET || g_Config.m_UiPage > CMenus::PAGE_FAVORITE_COMMUNITY_5 ||
		(g_Config.m_UiPage >= CMenus::PAGE_FAVORITE_COMMUNITY_1 && (size_t)(g_Config.m_UiPage - CMenus::PAGE_FAVORITE_COMMUNITY_1) >= ServerBrowser()->FavoriteCommunities().size()))
		g_Config.m_UiPage = CMenus::PAGE_INTERNET;
	Menus.RefreshBrowserTab(!m_BrowserOpened);
	m_BrowserOpened = true;
	m_ServersRequested = true;
	Menus.UpdateCommunityCache(false);
}

void CXcMenu::RenderBrowserTabs(float Width)
{
	const STheme &t = Theme();
	struct STab
	{
		int m_Page;
		const char *m_pIcon;
		const char *m_pCommunity;
		const char *m_pLabel;
	};
	std::vector<STab> vTabs = {
		{CMenus::PAGE_INTERNET, XC_ICON_GLOBE, nullptr, Tr("Internet")},
		{CMenus::PAGE_FAVORITES, XC_ICON_STAR, nullptr, Tr("Favorites")},
		{CMenus::PAGE_LAN, XC_ICON_DESKTOP, nullptr, "LAN"},
	};
	const std::vector<const CCommunity *> vpFavoriteCommunities = ServerBrowser()->FavoriteCommunities();
	for(size_t i = 0; i < vpFavoriteCommunities.size() && i < 5; i++)
		vTabs.push_back({CMenus::PAGE_FAVORITE_COMMUNITY_1 + (int)i, nullptr, vpFavoriteCommunities[i]->Id(), vpFavoriteCommunities[i]->Name()});

	ImGuiWindow *pWindow = ImGui::GetCurrentWindow();
	ImDrawList *pDraw = pWindow->DrawList;
	ImFont *pFont = Fonts().m_pLabel;
	const float Height = S(36.0f);
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const ImRect Rect(Pos, ImVec2(Pos.x + Width, Pos.y + Height));
	const ImGuiID Id = pWindow->GetID("##browser_tabs");
	ImGui::ItemSize(Rect);
	if(!ImGui::ItemAdd(Rect, Id))
		return;
	pDraw->AddRectFilled(Rect.Min, Rect.Max, Col(t.m_Track), S(10.0f));
	const float Inset = S(3.0f);
	const float TabWidth = (Width - Inset * 2.0f) / vTabs.size();
	int Current = 0;
	for(size_t i = 0; i < vTabs.size(); i++)
		if(vTabs[i].m_Page == g_Config.m_UiPage)
			Current = i;
	const float Shown = Animate(Id, (float)Current, 16.0f);
	const ImVec2 HighlightMin(Rect.Min.x + Inset + TabWidth * Shown, Rect.Min.y + Inset);
	pDraw->AddRectFilled(HighlightMin, ImVec2(HighlightMin.x + TabWidth, Rect.Max.y - Inset), Col(t.m_Accent), S(8.0f));

	for(size_t i = 0; i < vTabs.size(); i++)
	{
		const STab &Tab = vTabs[i];
		const ImRect TabRect(ImVec2(Rect.Min.x + Inset + TabWidth * i, Rect.Min.y), ImVec2(Rect.Min.x + Inset + TabWidth * (i + 1), Rect.Max.y));
		const ImGuiID TabId = Id + 10 + i;
		bool Hovered, Held;
		ImGui::ItemAdd(TabRect, TabId);
		if(ImGui::ButtonBehavior(TabRect, TabId, &Hovered, &Held) && g_Config.m_UiPage != Tab.m_Page)
		{
			g_Config.m_UiPage = Tab.m_Page;
			GameClient()->m_Menus.RefreshBrowserTab(false);
			PlayUiSound(SOUND_TAB);
		}
		if(Hovered && Tab.m_pCommunity)
			ImGui::SetTooltip("%s", Tab.m_pLabel);
		const float Under = std::clamp(1.0f - std::abs(Shown - i), 0.0f, 1.0f);
		const float Hover = Animate(TabId, Hovered ? 1.0f : 0.0f, 18.0f);
		const ImVec4 TextColor = Mix(Mix(t.m_TextSecondary, t.m_Text, Hover), t.m_OnAccent, Under);
		// icon + label, the label goes when the tab is too narrow
		const float IconHeight = S(14.0f);
		const float IconWidth = Tab.m_pCommunity ? IconHeight * 2.0f : pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, Tab.m_pIcon).x;
		const ImVec2 LabelSize = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, Tab.m_pLabel);
		const bool WithLabel = IconWidth + S(8.0f) + LabelSize.x + S(16.0f) <= TabWidth;
		const float ContentWidth = IconWidth + (WithLabel ? S(8.0f) + LabelSize.x : 0.0f);
		float x = TabRect.GetCenter().x - ContentWidth * 0.5f;
		const float CenterY = TabRect.GetCenter().y;
		if(Tab.m_pCommunity)
			DrawCommunityIcon(pDraw, Tab.m_pCommunity, ImVec2(x, CenterY - IconHeight * 0.5f), IconHeight);
		else
			DrawIcon(pDraw, pFont, pFont->FontSize, ImVec2(x + IconWidth * 0.5f, CenterY), Col(TextColor), Tab.m_pIcon);
		x += IconWidth + S(8.0f);
		if(WithLabel)
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(x, CenterY - LabelSize.y * 0.5f), Col(TextColor), Tab.m_pLabel);
	}
}

void CXcMenu::RenderBrowserCommunities()
{
	// the communities shown in the internet and favorites lists (DDNet, KoG, ...), like the classic filter:
	// a click shows or hides one, a right click gives it a tab at the top
	if((g_Config.m_UiPage != CMenus::PAGE_INTERNET && g_Config.m_UiPage != CMenus::PAGE_FAVORITES) || ServerBrowser()->Communities().empty())
		return;
	std::vector<const char *> vpNames;
	for(const CCommunity &Community : ServerBrowser()->Communities())
		vpNames.push_back(Community.Id());
	IFilterList &Filter = ServerBrowser()->CommunitiesFilter();
	bool AnyHidden = false;
	for(const char *pName : vpNames)
		AnyHidden |= Filter.Filtered(pName);

	const float Avail = ImGui::GetContentRegionAvail().x;
	const float Gap = S(6.0f);
	const float IconHeight = S(13.0f);
	ImFont *pFont = Fonts().m_pSmall;
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	float x = 0.0f;
	bool Changed = false;
	ImGui::PushID("##communities");
	FlowNext(x, pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, Tr("All communities")).x + S(20.0f), Avail, Gap);
	if(FilterChip("##all", Tr("All communities"), !AnyHidden))
	{
		ShowAllFilterItems(Filter, vpNames);
		Changed = true;
	}
	for(int i = 0; i < (int)vpNames.size(); i++)
	{
		const CCommunity &Community = ServerBrowser()->Communities()[i];
		const bool Favorite = ServerBrowser()->FavoriteCommunitiesFilter().Filtered(Community.Id());
		char aLabel[128];
		str_format(aLabel, sizeof(aLabel), "%s  %d%s%s", Community.Name(), Community.NumPlayers(), Favorite ? "  " : "", Favorite ? XC_ICON_STAR : "");
		const float PictureWidth = IconHeight * 2.0f;
		FlowNext(x, PictureWidth + S(6.0f) + pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, aLabel).x + S(20.0f), Avail, Gap);
		ImGui::PushID(i);
		ImVec2 PictureMin;
		const bool On = !Filter.Filtered(Community.Id());
		if(FilterChip("##community", aLabel, AnyHidden && On, PictureWidth, &PictureMin))
		{
			ToggleFilterItem(Filter, vpNames, i, nullptr);
			Changed = true;
		}
		if(ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", Favorite ? Tr("Right click: remove the tab at the top") : Tr("Right click: add a tab at the top"));
			if(ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				if(Favorite)
					ServerBrowser()->FavoriteCommunitiesFilter().Remove(Community.Id());
				else if(ServerBrowser()->FavoriteCommunities().size() < 5)
					ServerBrowser()->FavoriteCommunitiesFilter().Add(Community.Id());
				else
					Toast(Tr("Five community tabs at most"));
				PlayUiSound(SOUND_CLICK);
			}
		}
		ImGui::PopID();
		DrawCommunityIcon(pDraw, Community.Id(), ImVec2(PictureMin.x, PictureMin.y + (S(26.0f) - IconHeight) * 0.5f), IconHeight, !AnyHidden || On);
	}
	ImGui::PopID();
	if(Changed)
	{
		GameClient()->m_Menus.UpdateCommunityCache(true);
		Client()->ServerBrowserUpdate();
	}
}

void CXcMenu::RenderBrowserTypes()
{
	// the map types of the shown communities (Novice, Moderate, ... Solo, Race), taken from ddnet.org
	const ICommunityCache &Cache = ServerBrowser()->CommunityCache();
	if(!Cache.CountriesTypesFilterAvailable() || Cache.SelectableTypes().empty())
		return;
	std::vector<const char *> vpNames;
	for(const CCommunityType *pType : Cache.SelectableTypes())
		vpNames.push_back(pType->Name());
	IFilterList &Filter = ServerBrowser()->TypesFilter();

	const float Avail = ImGui::GetContentRegionAvail().x;
	const float Gap = S(6.0f);
	ImFont *pFont = Fonts().m_pSmall;
	float x = 0.0f;
	bool AnyHidden = false;
	for(const char *pName : vpNames)
		AnyHidden |= Filter.Filtered(pName);
	ImGui::PushID("##types");
	FlowNext(x, pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, Tr("All types")).x + S(20.0f), Avail, Gap);
	if(FilterChip("##all", Tr("All types"), !AnyHidden))
	{
		ShowAllFilterItems(Filter, vpNames);
		Client()->ServerBrowserUpdate();
	}
	for(int i = 0; i < (int)vpNames.size(); i++)
	{
		FlowNext(x, pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, vpNames[i]).x + S(20.0f), Avail, Gap);
		ImGui::PushID(i);
		if(FilterChip("##type", vpNames[i], AnyHidden && !Filter.Filtered(vpNames[i])))
		{
			ToggleFilterItem(Filter, vpNames, i, IServerBrowser::COMMUNITY_TYPE_NONE);
			Client()->ServerBrowserUpdate();
		}
		ImGui::PopID();
	}
	ImGui::PopID();
}

bool CXcMenu::RenderServerCard(const CServerInfo *pInfo, float Width, float Height)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	ImGui::PushID(pInfo->m_aAddress);
	const bool Clicked = ImGui::InvisibleButton("##card", ImVec2(Width, Height));
	const bool Hovered = ImGui::IsItemHovered();
	const bool DoubleClicked = Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	const ImGuiID Id = ImGui::GetItemID();
	ImGui::PopID();

	// the play button of the home page: the last server is picked and scrolled into sight (a server may list
	// several addresses, the last one is one of them)
	if(m_ScrollToSelected && !m_SelectedServer.empty() && str_find(pInfo->m_aAddress, m_SelectedServer.c_str()))
	{
		m_SelectedServer = pInfo->m_aAddress;
		ImGui::SetScrollHereY(0.3f);
		m_ScrollToSelected = false;
	}
	const bool Selected = m_SelectedServer == pInfo->m_aAddress;
	const float Hover = Animate(Id, Hovered ? 1.0f : 0.0f, 16.0f);
	const float Select = Animate(Id + 1, Selected ? 1.0f : 0.0f, 16.0f);
	const ImVec4 CardColor = Mix(t.m_Card, t.m_CardHover, Hover);
	pDraw->AddRectFilled(Min, Max, Col(CardColor), S(12.0f));
	// the scheme of the map on the right, when the map is on the disk; it fades into the card on the left
	const ImVec2 PreviewMin(Min.x + Width * 0.42f, Min.y + 1.0f);
	const ImVec2 PreviewMax(Max.x - 1.0f, Max.y - 1.0f);
	if(DrawMapPreview(pDraw, pInfo, PreviewMin, PreviewMax, S(12.0f), ImDrawFlags_RoundCornersRight, 0.55f + 0.3f * Hover))
	{
		const ImU32 Solid = Col(CardColor);
		const ImU32 Clear = Col(CardColor, 0.0f);
		const float FadeEnd = PreviewMin.x + (PreviewMax.x - PreviewMin.x) * 0.45f;
		pDraw->AddRectFilledMultiColor(PreviewMin, ImVec2(FadeEnd, PreviewMax.y), Solid, Clear, Clear, Solid);
	}
	pDraw->AddRect(Min, Max, Col(Mix(t.m_Border, t.m_Accent, Select)), S(12.0f), 0, 1.0f + Select);

	const float Pad = S(14.0f);
	ImFont *pLabel = Fonts().m_pLabel;
	ImFont *pSmall = Fonts().m_pSmall;

	// the marks in the top right corner: favorite, password, friends, finished map
	float MarksX = Max.x - Pad;
	const auto &&Mark = [&](const char *pText, const ImVec4 &Color) {
		const float MarkWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pText).x;
		MarksX -= MarkWidth;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(MarksX, Min.y + Pad + S(2.0f)), Col(Color), pText);
		MarksX -= S(8.0f);
	};
	if(pInfo->m_Favorite != TRISTATE::NONE)
		Mark(XC_ICON_STAR, t.m_Accent);
	if(pInfo->m_Flags & SERVER_FLAG_PASSWORD)
		Mark(XC_ICON_LOCK, t.m_TextMuted);
	if(g_Config.m_BrIndicateFinished && pInfo->m_HasRank == CServerInfo::RANK_RANKED)
		Mark(XC_ICON_FLAG_CHECK, ImVec4(0.24f, 0.84f, 0.64f, 1.0f));
	if(pInfo->m_FriendState != IFriends::FRIEND_NO)
	{
		char aFriends[32];
		str_format(aFriends, sizeof(aFriends), "%s %d", XC_ICON_HEART, pInfo->m_FriendNum);
		Mark(aFriends, ImVec4(1.0f, 0.36f, 0.54f, 1.0f));
	}

	// name, then the map below it
	const ImVec4 NameClip(Min.x + Pad, Min.y, MarksX, Max.y);
	pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + Pad, Min.y + Pad), Col(t.m_Text), pInfo->m_aName, nullptr, 0.0f, &NameClip);
	const ImVec4 ClipRect(Min.x + Pad, Min.y, Max.x - Pad, Max.y);
	char aMap[128];
	str_format(aMap, sizeof(aMap), "%s  %s", XC_ICON_MAP, pInfo->m_aMap);
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + Pad, Min.y + Pad + pLabel->FontSize + S(4.0f)), Col(t.m_TextMuted), aMap, nullptr, 0.0f, &ClipRect);

	// how full the server is
	const int NumPlayers = ServerBrowser()->Players(*pInfo);
	const int MaxPlayers = std::max(ServerBrowser()->Max(*pInfo), 1);
	const float BarY = Max.y - Pad - pSmall->FontSize - S(10.0f);
	const float Fill = std::clamp((float)NumPlayers / MaxPlayers, 0.0f, 1.0f);
	pDraw->AddRectFilled(ImVec2(Min.x + Pad, BarY), ImVec2(Max.x - Pad, BarY + S(3.0f)), Col(t.m_Track), S(2.0f));
	if(Fill > 0.0f)
		pDraw->AddRectFilled(ImVec2(Min.x + Pad, BarY), ImVec2(Min.x + Pad + (Width - Pad * 2.0f) * Fill, BarY + S(3.0f)), Col(t.m_Accent, 0.4f + 0.6f * Fill), S(2.0f));

	// bottom row: players, ping, game type
	const float RowY = Max.y - Pad - pSmall->FontSize;
	char aPlayers[64];
	str_format(aPlayers, sizeof(aPlayers), "%s  %d/%d", XC_ICON_USERS, NumPlayers, ServerBrowser()->Max(*pInfo));
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + Pad, RowY), Col(NumPlayers > 0 ? t.m_TextSecondary : t.m_TextMuted), aPlayers);
	const float PlayersWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aPlayers).x;
	const float PingX = Min.x + Pad + PlayersWidth + S(16.0f);
	pDraw->AddCircleFilled(ImVec2(PingX + S(4.0f), RowY + pSmall->FontSize * 0.5f), S(3.5f), Col(PingColor(pInfo->m_Latency)), 12);
	char aPing[32];
	str_format(aPing, sizeof(aPing), "%s%d ms", pInfo->m_LatencyIsEstimated ? "~" : "", pInfo->m_Latency);
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(PingX + S(12.0f), RowY), Col(t.m_TextMuted), aPing);
	if(pInfo->m_aGameType[0])
	{
		const ImVec2 TypeSize = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pInfo->m_aGameType);
		const ImVec2 ChipMin(Max.x - Pad - TypeSize.x - S(12.0f), RowY - S(3.0f));
		pDraw->AddRectFilled(ChipMin, ImVec2(Max.x - Pad, RowY + pSmall->FontSize + S(3.0f)), Col(t.m_Track), S(6.0f));
		const ImVec4 TypeColor = g_Config.m_UiColorizeGametype ? Mix(t.m_TextSecondary, ToImVec4(pInfo->m_GametypeColor), 0.75f) : t.m_TextSecondary;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(ChipMin.x + S(6.0f), RowY), Col(TypeColor), pInfo->m_aGameType);
	}

	if(Clicked)
	{
		m_SelectedServer = pInfo->m_aAddress;
		m_ServerPanel = PANEL_SERVER;
	}
	if(DoubleClicked)
		Client()->Connect(pInfo->m_aAddress);
	return Clicked;
}

void CXcMenu::RenderServerPlayers(const CServerInfo *pInfo)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	ImFont *pLabel = Fonts().m_pLabel;
	ImFont *pSmall = Fonts().m_pSmall;
	const float Width = ImGui::GetContentRegionAvail().x;
	const float RowHeight = S(40.0f);
	for(size_t i = 0; i < pInfo->m_vClients.size(); i++)
	{
		const CServerInfo::CClient &Player = pInfo->m_vClients[i];
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Width, Min.y + RowHeight);
		ImGui::PushID((int)i);
		ImGui::InvisibleButton("##player", ImVec2(Width, RowHeight));
		const bool Hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		if(!ImGui::IsItemVisible())
			continue;
		const bool Friend = Player.m_FriendState == IFriends::FRIEND_PLAYER;
		const bool Clan = Player.m_FriendState == IFriends::FRIEND_CLAN;
		if(Friend || Clan || Hovered)
			pDraw->AddRectFilled(Min, Max, Col(Friend ? ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.14f) : t.m_Track, Hovered ? 1.0f : 0.7f), S(8.0f));

		float x = Min.x + S(6.0f);
		// servers without the skins in their info show the default tee
		AddTeeSkin(pDraw, ImVec2(x, Min.y + S(2.0f)), ImVec2(x + RowHeight - S(4.0f), Max.y - S(2.0f)), Player.m_aSkin[0] ? Player.m_aSkin : "default", Player.m_aSkin[0] && Player.m_CustomSkinColors, Player.m_CustomSkinColorBody, Player.m_CustomSkinColorFeet);
		x += RowHeight;

		// the score: points or the best time, depends on the server
		char aScore[32] = "";
		if(!Player.m_Player)
			str_copy(aScore, Tr("spec"));
		else if(pInfo->m_ClientScoreKind == CServerInfo::CLIENT_SCORE_KIND_POINTS)
			str_format(aScore, sizeof(aScore), "%d", Player.m_Score);
		else
		{
			std::optional<int> Time;
			if(pInfo->m_ClientScoreKind == CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT)
			{
				const int TempTime = absolute(Player.m_Score);
				if(TempTime != 0 && TempTime != 9999)
					Time = TempTime;
			}
			else if(Player.m_Score >= 0)
				Time = Player.m_Score;
			if(Time.has_value())
				str_time((int64_t)Time.value() * 100, ETimeFormat::HOURS, aScore, sizeof(aScore));
		}
		const float FlagHeight = S(12.0f);
		const float RightX = Max.x - S(8.0f) - FlagHeight * 2.0f;
		DrawFlag(pDraw, Player.m_Country, ImVec2(RightX, Min.y + (RowHeight - FlagHeight) * 0.5f), FlagHeight, Player.m_Afk ? 0.5f : 1.0f);
		const float ScoreWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aScore).x;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(RightX - S(8.0f) - ScoreWidth, Min.y + (RowHeight - pSmall->FontSize) * 0.5f), Col(t.m_TextSecondary), aScore);

		const ImVec4 Clip(x, Min.y, RightX - S(16.0f) - ScoreWidth, Max.y);
		const float NameY = Player.m_aClan[0] ? Min.y + S(4.0f) : Min.y + (RowHeight - pLabel->FontSize) * 0.5f;
		const ImVec4 NameColor = Friend ? t.m_Accent : (Player.m_Afk ? t.m_TextMuted : t.m_Text);
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x, NameY), Col(NameColor), Player.m_aName, nullptr, 0.0f, &Clip);
		if(Player.m_aClan[0])
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(x, NameY + pLabel->FontSize + S(2.0f)), Col(Clan ? t.m_Accent : t.m_TextMuted), Player.m_aClan, nullptr, 0.0f, &Clip);

		if(Hovered)
		{
			// a friend button over the row
			const bool IsFriend = GameClient()->Friends()->IsFriend(Player.m_aName, Player.m_aClan, true);
			ImGui::SetTooltip("%s", IsFriend ? Tr("Right click: remove from friends") : Tr("Right click: add to friends"));
			if(ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				if(IsFriend)
					GameClient()->Friends()->RemoveFriend(Player.m_aName, Player.m_aClan);
				else
					GameClient()->Friends()->AddFriend(Player.m_aName, Player.m_aClan);
				Client()->ServerBrowserUpdate();
				PlayUiSound(SOUND_CLICK);
			}
		}
	}
}

void CXcMenu::RenderServerDetails(const CServerInfo *pInfo)
{
	const STheme &t = Theme();
	if(!pInfo)
	{
		Title(Tr("Server browser"));
		Description(Tr("Pick a server to see who is playing. Double click joins it"));
		return;
	}

	// the scheme of the map on top, when the map is on the disk
	{
		const ImVec2 PreviewMin = ImGui::GetCursorScreenPos();
		const ImVec2 PreviewMax(PreviewMin.x + ImGui::GetContentRegionAvail().x, PreviewMin.y + S(130.0f));
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		if(m_MapPreviews.Find(pInfo->m_aMap, pInfo->m_MapCrc))
		{
			pDraw->AddRectFilled(PreviewMin, PreviewMax, Col(t.m_Track), S(10.0f));
			DrawMapPreview(pDraw, pInfo, PreviewMin, PreviewMax, S(10.0f), ImDrawFlags_RoundCornersAll, 1.0f);
			ImGui::Dummy(ImVec2(0.0f, S(130.0f) + S(8.0f)));
		}
	}
	// map as the big title, the server name below
	ImGui::PushFont(Fonts().m_pHeader);
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextColored(t.m_Text, "%s", pInfo->m_aMap);
	ImGui::PopTextWrapPos();
	ImGui::PopFont();
	Description(pInfo->m_aName);
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

	const auto InfoRow = [&](const char *pLabel, const char *pValue, const ImVec4 &Color) {
		RowLabel(pLabel, ImGui::CalcTextSize(pValue).x);
		ImGui::TextColored(Color, "%s", pValue);
	};
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%d/%d", ServerBrowser()->Players(*pInfo), ServerBrowser()->Max(*pInfo));
	InfoRow(Tr("Players"), aBuf, t.m_Text);
	str_format(aBuf, sizeof(aBuf), "%s%d ms", pInfo->m_LatencyIsEstimated ? "~" : "", pInfo->m_Latency);
	InfoRow(Tr("Ping"), aBuf, PingColor(pInfo->m_Latency));
	InfoRow(Tr("Game type"), pInfo->m_aGameType, g_Config.m_UiColorizeGametype ? Mix(t.m_Text, ToImVec4(pInfo->m_GametypeColor), 0.75f) : t.m_Text);
	InfoRow(Tr("Version"), pInfo->m_aVersion, t.m_TextSecondary);
	if(pInfo->m_aCommunityId[0] && str_comp(pInfo->m_aCommunityId, IServerBrowser::COMMUNITY_NONE) != 0)
	{
		const CCommunity *pCommunity = ServerBrowser()->Community(pInfo->m_aCommunityId);
		InfoRow(Tr("Community"), pCommunity ? pCommunity->Name() : pInfo->m_aCommunityId, t.m_TextSecondary);
	}
	if(g_Config.m_BrIndicateFinished && pInfo->m_HasRank != CServerInfo::RANK_UNAVAILABLE)
		InfoRow(Tr("Map"), pInfo->m_HasRank == CServerInfo::RANK_RANKED ? Tr("finished") : Tr("not finished yet"), pInfo->m_HasRank == CServerInfo::RANK_RANKED ? ImVec4(0.24f, 0.84f, 0.64f, 1.0f) : t.m_TextSecondary);
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

	if(Button(Tr("Join"), true, -1.0f))
		Client()->Connect(pInfo->m_aAddress);
	ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
	const float Half = (ImGui::GetContentRegionAvail().x - S(8.0f)) * 0.5f;
	const bool Favorite = pInfo->m_Favorite != TRISTATE::NONE;
	if(Button(Favorite ? Tr("Unfavorite") : Tr("Favorite"), false, Half))
	{
		if(Favorite)
			Favorites()->Remove(pInfo->m_aAddresses, pInfo->m_NumAddresses);
		else
		{
			Favorites()->Add(pInfo->m_aAddresses, pInfo->m_NumAddresses);
			if(g_Config.m_UiPage == CMenus::PAGE_LAN)
				Favorites()->AllowPing(pInfo->m_aAddresses, pInfo->m_NumAddresses, true);
		}
		Client()->ServerBrowserUpdate();
	}
	ImGui::SameLine(0.0f, S(8.0f));
	if(Button(Tr("Copy address"), false, Half))
	{
		char aInfo[512];
		str_format(aInfo, sizeof(aInfo), "%s\nddnet://%s", pInfo->m_aName, pInfo->m_aAddress);
		Input()->SetClipboardText(aInfo);
		Toast(Tr("Address copied"));
	}
	if(Favorite)
	{
		// a favorite is not asked for its ping unless allowed: the server would see your address
		int AllowPing = pInfo->m_FavoriteAllowPing != TRISTATE::NONE;
		if(ToggleLine(Tr("Ask for ping (the server sees your IP)"), &AllowPing))
		{
			Favorites()->AllowPing(pInfo->m_aAddresses, pInfo->m_NumAddresses, AllowPing != 0);
			Client()->ServerBrowserUpdate();
		}
	}

	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
	str_format(aBuf, sizeof(aBuf), "%s (%d)", Tr("Players"), (int)pInfo->m_vClients.size());
	Title(aBuf);
	if(pInfo->m_vClients.empty())
		Description(Tr("Nobody is playing"));
	else
		RenderServerPlayers(pInfo);
}

void CXcMenu::RenderBrowserFriends()
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	ImFont *pLabel = Fonts().m_pLabel;
	ImFont *pSmall = Fonts().m_pSmall;
	IFriends *pFriends = GameClient()->Friends();

	// who is online: the friends found in the server list
	struct SOnline
	{
		const CServerInfo::CClient *m_pClient;
		const CServerInfo *m_pServer;
	};
	std::vector<SOnline> avOnline[2]; // players, clanmates
	std::vector<const CFriendInfo *> vpOffline;
	for(int i = 0; i < pFriends->NumFriends(); i++)
		vpOffline.push_back(pFriends->GetFriend(i));
	for(int ServerIndex = 0; ServerIndex < ServerBrowser()->NumServers(); ServerIndex++)
	{
		const CServerInfo *pServer = ServerBrowser()->Get(ServerIndex);
		if(pServer->m_FriendState == IFriends::FRIEND_NO)
			continue;
		for(const CServerInfo::CClient &Player : pServer->m_vClients)
		{
			if(Player.m_FriendState == IFriends::FRIEND_NO)
				continue;
			avOnline[Player.m_FriendState == IFriends::FRIEND_PLAYER ? 0 : 1].push_back({&Player, pServer});
			vpOffline.erase(std::remove_if(vpOffline.begin(), vpOffline.end(), [&](const CFriendInfo *pFriend) {
				return (pFriend->m_aName[0] == '\0' || str_comp(pFriend->m_aName, Player.m_aName) == 0) &&
				       ((pFriend->m_aName[0] != '\0' && g_Config.m_ClFriendsIgnoreClan) || str_comp(pFriend->m_aClan, Player.m_aClan) == 0);
			}),
				vpOffline.end());
		}
	}
	for(auto &vOnline : avOnline)
		std::sort(vOnline.begin(), vOnline.end(), [](const SOnline &A, const SOnline &B) { return str_comp_nocase(A.m_pClient->m_aName, B.m_pClient->m_aName) < 0; });
	std::sort(vpOffline.begin(), vpOffline.end(), [](const CFriendInfo *pA, const CFriendInfo *pB) { return str_comp_nocase(pA->m_aName[0] ? pA->m_aName : pA->m_aClan, pB->m_aName[0] ? pB->m_aName : pB->m_aClan) < 0; });

	const float Width = ImGui::GetContentRegionAvail().x;
	const float RowHeight = S(42.0f);
	const char *pRemoveName = nullptr;
	const char *pRemoveClan = nullptr;
	const auto &&FriendRow = [&](const char *pId, const char *pName, const char *pClan, const char *pWhere, const CServerInfo::CClient *pClient, const CServerInfo *pServer) {
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Width, Min.y + RowHeight);
		ImGui::PushID(pId);
		const bool Clicked = ImGui::InvisibleButton("##friend", ImVec2(Width, RowHeight));
		const bool Hovered = ImGui::IsItemHovered();
		const bool DoubleClicked = Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		ImGui::PopID();
		if(Hovered)
			pDraw->AddRectFilled(Min, Max, Col(t.m_Track), S(8.0f));
		float x = Min.x + S(6.0f);
		if(pClient && pClient->m_aSkin[0])
			AddTeeSkin(pDraw, ImVec2(x, Min.y + S(2.0f)), ImVec2(x + RowHeight - S(4.0f), Max.y - S(2.0f)), pClient->m_aSkin, pClient->m_CustomSkinColors, pClient->m_CustomSkinColorBody, pClient->m_CustomSkinColorFeet);
		else
		{
			DrawIcon(pDraw, pLabel, pLabel->FontSize, ImVec2(x + (RowHeight - S(4.0f)) * 0.5f, Min.y + RowHeight * 0.5f), Col(t.m_TextMuted), XC_ICON_USER);
		}
		x += RowHeight;
		const ImVec4 Clip(x, Min.y, Max.x - S(30.0f), Max.y);
		const bool Online = pServer != nullptr;
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x, Min.y + S(5.0f)), Col(Online ? t.m_Text : t.m_TextSecondary), pName[0] ? pName : pClan, nullptr, 0.0f, &Clip);
		char aSub[256];
		if(pName[0] && pClan[0])
			str_format(aSub, sizeof(aSub), "%s · %s", pClan, pWhere);
		else
			str_copy(aSub, pWhere);
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(x, Min.y + S(7.0f) + pLabel->FontSize), Col(Online ? t.m_Accent : t.m_TextMuted), aSub, nullptr, 0.0f, &Clip);
		if(Hovered)
		{
			// the cross on the right removes the friend
			const ImVec2 CrossMin(Max.x - S(26.0f), Min.y + (RowHeight - S(20.0f)) * 0.5f);
			const bool OverCross = ImGui::IsMouseHoveringRect(CrossMin, ImVec2(CrossMin.x + S(20.0f), CrossMin.y + S(20.0f)));
			DrawIcon(pDraw, pSmall, pSmall->FontSize, ImVec2(CrossMin.x + S(10.0f), CrossMin.y + S(10.0f)), Col(OverCross ? ImVec4(1.0f, 0.36f, 0.36f, 1.0f) : t.m_TextMuted), XC_ICON_USER_XMARK);
			if(OverCross)
				ImGui::SetTooltip("%s", Tr("Remove from friends"));
			if(Clicked && OverCross)
			{
				pRemoveName = pName;
				pRemoveClan = pClan;
				return;
			}
		}
		if(Clicked && pServer)
		{
			m_SelectedServer = pServer->m_aAddress;
			m_ServerPanel = PANEL_SERVER;
		}
		if(DoubleClicked && pServer)
			Client()->Connect(pServer->m_aAddress);
	};

	const char *apGroups[] = {Tr("Online friends"), Tr("Online clanmates")};
	char aBuf[128];
	for(int Group = 0; Group < 2; Group++)
	{
		if(Group == 1 && avOnline[1].empty())
			continue;
		str_format(aBuf, sizeof(aBuf), "%s (%d)", apGroups[Group], (int)avOnline[Group].size());
		Title(aBuf);
		if(avOnline[Group].empty())
			Description(Tr("Nobody is online right now"));
		for(size_t i = 0; i < avOnline[Group].size(); i++)
		{
			char aId[16];
			str_format(aId, sizeof(aId), "on%d_%d", Group, (int)i);
			FriendRow(aId, avOnline[Group][i].m_pClient->m_aName, avOnline[Group][i].m_pClient->m_aClan, avOnline[Group][i].m_pServer->m_aName, avOnline[Group][i].m_pClient, avOnline[Group][i].m_pServer);
		}
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	}
	str_format(aBuf, sizeof(aBuf), "%s (%d)", Tr("Offline"), (int)vpOffline.size());
	Title(aBuf);
	for(size_t i = 0; i < vpOffline.size(); i++)
	{
		char aId[16];
		str_format(aId, sizeof(aId), "off%d", (int)i);
		FriendRow(aId, vpOffline[i]->m_aName, vpOffline[i]->m_aClan, vpOffline[i]->m_aName[0] ? Tr("offline") : Tr("the whole clan"), nullptr, nullptr);
	}
	if(pRemoveName)
	{
		char aName[MAX_NAME_LENGTH], aClan[MAX_CLAN_LENGTH];
		str_copy(aName, pRemoveName);
		str_copy(aClan, pRemoveClan);
		pFriends->RemoveFriend(aName, aClan);
		Client()->ServerBrowserUpdate();
	}

	// add a friend by name, or a whole clan with an empty name
	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
	Title(Tr("Add a friend"));
	Description(Tr("Leave the name empty to add the whole clan"));
	InputText("##friend_name", m_aFriendName, sizeof(m_aFriendName), Tr("Name"));
	ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
	InputText("##friend_clan", m_aFriendClan, sizeof(m_aFriendClan), Tr("Clan"));
	ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
	if(Button(Tr("Add friend"), true, -1.0f) && (m_aFriendName[0] || m_aFriendClan[0]))
	{
		pFriends->AddFriend(m_aFriendName, m_aFriendClan);
		m_aFriendName[0] = '\0';
		m_aFriendClan[0] = '\0';
		Client()->ServerBrowserUpdate();
	}
}

void CXcMenu::RenderBrowserFilters()
{
	const STheme &t = Theme();
	CMenus &Menus = GameClient()->m_Menus;
	bool Changed = false;

	// sorting
	Title(Tr("Sort"));
	{
		static const int s_aSorts[] = {IServerBrowser::SORT_NUMPLAYERS, IServerBrowser::SORT_PING, IServerBrowser::SORT_NAME, IServerBrowser::SORT_MAP, IServerBrowser::SORT_GAMETYPE};
		const char *apSorts[] = {Tr("Players"), Tr("Ping"), Tr("Name"), Tr("Map"), Tr("Type")};
		int Current = 0;
		for(int i = 0; i < (int)std::size(s_aSorts); i++)
			if(g_Config.m_BrSort == s_aSorts[i])
				Current = i;
		// the natural order: most players first, the lowest ping first, names from A
		const auto &&NormalOrder = [](int Sort) { return Sort == IServerBrowser::SORT_NUMPLAYERS ? 2 : 0; };
		const auto &&ReverseOrder = [](int Sort) { return Sort == IServerBrowser::SORT_NUMPLAYERS ? 0 : 1; };
		int Reverse = g_Config.m_BrSortOrder == ReverseOrder(g_Config.m_BrSort) && g_Config.m_BrSortOrder != NormalOrder(g_Config.m_BrSort);
		if(ChoiceChips("##sort", &Current, apSorts, (int)std::size(apSorts)))
		{
			g_Config.m_BrSort = s_aSorts[Current];
			g_Config.m_BrSortOrder = Reverse ? ReverseOrder(g_Config.m_BrSort) : NormalOrder(g_Config.m_BrSort);
			Changed = true;
		}
		if(ToggleLine(Tr("Reverse order"), &Reverse))
		{
			g_Config.m_BrSortOrder = Reverse ? ReverseOrder(g_Config.m_BrSort) : NormalOrder(g_Config.m_BrSort);
			Changed = true;
		}
	}

	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
	Title(Tr("Show"));
	Changed |= ToggleLine(Tr("Has people playing"), &g_Config.m_BrFilterEmpty);
	Changed |= ToggleLine(Tr("Server not full"), &g_Config.m_BrFilterFull);
	Changed |= ToggleLine(Tr("Friends only"), &g_Config.m_BrFilterFriends);
	Changed |= ToggleLine(Tr("No password"), &g_Config.m_BrFilterPw);
	Changed |= ToggleLine(Tr("No login required"), &g_Config.m_BrFilterLogin);
	Changed |= ToggleLine(Tr("Count players only (no spectators)"), &g_Config.m_BrFilterSpectators);
	Changed |= ToggleLine(Tr("Hide connecting players"), &g_Config.m_BrFilterConnectingPlayers);
	if(ServerBrowser()->CommunityCache().AnyRanksAvailable())
	{
		if(ToggleLine(Tr("Mark finished maps"), &g_Config.m_BrIndicateFinished))
		{
			Changed = true;
			if(g_Config.m_BrIndicateFinished)
				ServerBrowser()->Refresh(ServerBrowser()->GetCurrentType());
			else
				g_Config.m_BrFilterUnfinishedMap = 0;
		}
		if(g_Config.m_BrIndicateFinished)
			Changed |= ToggleLine(Tr("Only maps not finished yet"), &g_Config.m_BrFilterUnfinishedMap);
	}

	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
	Title(Tr("Refresh on its own"));
	{
		static const int s_aSeconds[] = {0, 30, 60, 120};
		const char *apItems[] = {Tr("Off"), Tr("30 s"), Tr("1 min"), Tr("2 min")};
		int Current = 0;
		for(int i = 0; i < 4; i++)
			if(std::abs(g_Config.m_XcBrAutoRefresh - s_aSeconds[i]) < std::abs(g_Config.m_XcBrAutoRefresh - s_aSeconds[Current]))
				Current = i;
		if(Segmented("##auto_refresh", &Current, apItems, 4))
			g_Config.m_XcBrAutoRefresh = s_aSeconds[Current];
	}

	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
	Title(Tr("Text filters"));
	ParamLabel(Tr("Hide servers with these words (separate with ;)"));
	Changed |= InputText("##exclude", g_Config.m_BrExcludeString, sizeof(g_Config.m_BrExcludeString), "fng; vanilla");
	ParamLabel(Tr("Game types (separate with ;)"));
	Changed |= InputText("##gametype", g_Config.m_BrFilterGametype, sizeof(g_Config.m_BrFilterGametype), "DDraceNetwork; Gores");
	Changed |= ToggleLine(Tr("Exact game type"), &g_Config.m_BrFilterGametypeStrict);
	ParamLabel(Tr("Server address"));
	Changed |= InputText("##address_filter", g_Config.m_BrFilterServerAddress, sizeof(g_Config.m_BrFilterServerAddress), "127.0.0.1:8303");

	// communities: which are shown in the internet list, and which get their own tab (the star)
	if((g_Config.m_UiPage == CMenus::PAGE_INTERNET || g_Config.m_UiPage == CMenus::PAGE_FAVORITES) && !ServerBrowser()->Communities().empty())
	{
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		Title(Tr("Communities"));
		Description(Tr("Click to show or hide, the star adds a tab at the top"));
		std::vector<const char *> vpNames;
		for(const CCommunity &Community : ServerBrowser()->Communities())
			vpNames.push_back(Community.Id());
		IFilterList &Filter = ServerBrowser()->CommunitiesFilter();
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		ImFont *pLabel = Fonts().m_pLabel;
		ImFont *pSmall = Fonts().m_pSmall;
		const float Width = ImGui::GetContentRegionAvail().x;
		const float RowHeight = S(32.0f);
		for(int i = 0; i < (int)ServerBrowser()->Communities().size(); i++)
		{
			const CCommunity &Community = ServerBrowser()->Communities()[i];
			const bool Active = !Filter.Filtered(Community.Id());
			const bool Favorite = ServerBrowser()->FavoriteCommunitiesFilter().Filtered(Community.Id());
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			const ImVec2 Max(Min.x + Width, Min.y + RowHeight);
			ImGui::PushID(i);
			const bool Clicked = ImGui::InvisibleButton("##community", ImVec2(Width, RowHeight));
			const bool Hovered = ImGui::IsItemHovered();
			ImGui::PopID();
			const ImVec2 StarMin(Max.x - S(28.0f), Min.y);
			const bool OverStar = Hovered && ImGui::IsMouseHoveringRect(StarMin, Max);
			if(Hovered)
				pDraw->AddRectFilled(Min, Max, Col(t.m_Track), S(8.0f));
			DrawCommunityIcon(pDraw, Community.Id(), ImVec2(Min.x + S(6.0f), Min.y + (RowHeight - S(14.0f)) * 0.5f), S(14.0f), Active);
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + S(44.0f), Min.y + (RowHeight - pLabel->FontSize) * 0.5f), Col(Active ? t.m_Text : t.m_TextMuted), Community.Name());
			char aPlayers[32];
			str_format(aPlayers, sizeof(aPlayers), "%d %s", Community.NumPlayers(), XC_ICON_USER);
			const float PlayersWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aPlayers).x;
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(StarMin.x - S(8.0f) - PlayersWidth, Min.y + (RowHeight - pSmall->FontSize) * 0.5f), Col(t.m_TextMuted), aPlayers);
			DrawIcon(pDraw, pSmall, pSmall->FontSize, ImVec2(StarMin.x + S(12.0f), Min.y + RowHeight * 0.5f), Col(Favorite ? t.m_Accent : (OverStar ? t.m_Text : t.m_TextMuted), Favorite || OverStar ? 1.0f : 0.5f), XC_ICON_STAR);
			if(OverStar)
				ImGui::SetTooltip("%s", Favorite ? Tr("Remove the tab of this community") : Tr("Add a tab for this community"));
			if(Clicked && OverStar)
			{
				if(Favorite)
					ServerBrowser()->FavoriteCommunitiesFilter().Remove(Community.Id());
				else if(ServerBrowser()->FavoriteCommunities().size() < 5)
					ServerBrowser()->FavoriteCommunitiesFilter().Add(Community.Id());
				else
					Toast(Tr("Five community tabs at most"));
				PlayUiSound(SOUND_CLICK);
			}
			else if(Clicked)
			{
				ToggleFilterItem(Filter, vpNames, i, nullptr);
				Menus.UpdateCommunityCache(true);
				Changed = true;
				PlayUiSound(SOUND_CLICK);
			}
		}
	}

	// countries of the shown communities
	const ICommunityCache &Cache = ServerBrowser()->CommunityCache();
	if(Cache.CountriesTypesFilterAvailable() && !Cache.SelectableCountries().empty())
	{
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		Title(Tr("Countries"));
		std::vector<const char *> vpNames;
		for(const CCommunityCountry *pCountry : Cache.SelectableCountries())
			vpNames.push_back(pCountry->Name());
		IFilterList &Filter = ServerBrowser()->CountriesFilter();
		bool AnyHidden = false;
		for(const char *pName : vpNames)
			AnyHidden |= Filter.Filtered(pName);
		const float Avail = ImGui::GetContentRegionAvail().x;
		const float FlagHeight = S(12.0f);
		const float ChipWidth = FlagHeight * 2.0f + S(20.0f);
		float x = 0.0f;
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		for(int i = 0; i < (int)vpNames.size(); i++)
		{
			FlowNext(x, ChipWidth, Avail, S(6.0f));
			ImGui::PushID(i);
			ImVec2 FlagMin;
			const bool On = !Filter.Filtered(vpNames[i]);
			if(FilterChip("##country", "", AnyHidden && On, FlagHeight * 2.0f, &FlagMin))
			{
				ToggleFilterItem(Filter, vpNames, i, IServerBrowser::COMMUNITY_COUNTRY_NONE);
				Changed = true;
			}
			if(ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", vpNames[i]);
			ImGui::PopID();
			DrawFlag(pDraw, Cache.SelectableCountries()[i]->FlagId(), ImVec2(FlagMin.x, FlagMin.y + (S(26.0f) - FlagHeight) * 0.5f), FlagHeight, On ? 1.0f : 0.35f);
		}
	}

	ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
	if(Button(Tr("Reset filters"), false, -1.0f))
	{
		Menus.ResetServerbrowserFilters();
		Changed = true;
	}
	if(Changed)
		Client()->ServerBrowserUpdate();
}

void CXcMenu::RenderBrowserStatus()
{
	const STheme &t = Theme();
	ImFont *pSmall = Fonts().m_pSmall;
	char aStatus[128];
	if(ServerBrowser()->IsGettingServerlist())
		str_copy(aStatus, Tr("Getting the server list..."));
	else if(ServerBrowser()->IsRefreshing())
		str_format(aStatus, sizeof(aStatus), "%s %d%%", Tr("Refreshing"), ServerBrowser()->LoadingProgression());
	else
		str_format(aStatus, sizeof(aStatus), "%d %s · %d %s", ServerBrowser()->NumSortedServers(), Tr("servers"), ServerBrowser()->NumSortedPlayers(), Tr("players"));
	const float Height = S(36.0f);
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddText(pSmall, pSmall->FontSize, ImVec2(Pos.x + S(4.0f), Pos.y + (Height - pSmall->FontSize) * 0.5f), Col(t.m_TextMuted), aStatus);

	// join by address on the right
	const float ButtonWidth = S(120.0f);
	const float FieldWidth = std::min(S(320.0f), ImGui::GetContentRegionAvail().x * 0.5f);
	ImGui::SetCursorScreenPos(ImVec2(Pos.x + ImGui::GetContentRegionAvail().x - ButtonWidth - FieldWidth - S(8.0f), Pos.y));
	ImGui::BeginChild("##address", ImVec2(FieldWidth, Height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	InputText("##server_address", g_Config.m_UiServerAddress, sizeof(g_Config.m_UiServerAddress), Tr("Server address"), FieldWidth);
	const bool EnterPressed = ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
	ImGui::EndChild();
	ImGui::SameLine(0.0f, S(8.0f));
	if((Button(Tr("Connect"), false, ButtonWidth) || EnterPressed) && g_Config.m_UiServerAddress[0])
		Client()->Connect(g_Config.m_UiServerAddress);
}

// one server as a quiet row: the name and the map under it, how full it is, the ping, a join button; a click opens
// the row in place (the map, who plays), a double click joins
float CXcMenu::RenderServerRow(const CServerInfo *pInfo, float Width, bool Draw)
{
	const STheme &t = Theme();
	const float RowH = S(60.0f);
	const bool Selected = m_SelectedServer == pInfo->m_aAddress;
	ImGui::PushID(pInfo->m_aAddress);
	const ImGuiID OpenId = ImGui::GetID("##open");
	const float Opened = Animate(OpenId, Selected && m_ServerExpanded ? 1.0f : 0.0f, 12.0f);
	const float ExtraH = S(196.0f) * Opened;
	const float TotalH = RowH + ExtraH;
	if(!Draw)
	{
		ImGui::PopID();
		ImGui::Dummy(ImVec2(Width, TotalH + S(4.0f)));
		return TotalH;
	}
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + TotalH);
	ImFont *pLabel = Fonts().m_pLabel;
	ImFont *pSmall = Fonts().m_pSmall;
	const float Pad = S(16.0f);

	// the join button on the right of the row: quiet, lights up under the mouse
	const float JoinW = S(84.0f), JoinH = S(32.0f);
	const ImVec2 JoinMin(Min.x + Width - Pad - JoinW, Min.y + (RowH - JoinH) * 0.5f);
	ImGui::SetCursorScreenPos(JoinMin);
	const bool Join = ImGui::InvisibleButton("##join", ImVec2(JoinW, JoinH));
	const bool JoinHovered = ImGui::IsItemHovered();
	ImGui::SetCursorScreenPos(Min);
	ImGui::SetNextItemAllowOverlap();
	const bool Clicked = ImGui::InvisibleButton("##row", ImVec2(Width - Pad - JoinW - S(8.0f), RowH));
	const bool Hovered = ImGui::IsItemHovered() || JoinHovered;
	const bool DoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	const float Hover = Animate(ImGui::GetID("##hover"), Hovered ? 1.0f : 0.0f, 14.0f);
	const float JoinHover = Animate(ImGui::GetID("##join_hover"), JoinHovered ? 1.0f : 0.0f, 16.0f);

	// the row itself: nothing until the mouse or the selection, then a soft plate
	const float Plate = std::max(Hover * 0.6f, Selected ? 1.0f : 0.0f);
	if(Plate > 0.01f)
		pDraw->AddRectFilled(Min, Max, Col(ImVec4(1, 1, 1, 0.035f + 0.025f * Selected), Plate), S(14.0f));

	// name, marks, then the map and the mode under it
	const int NumPlayers = ServerBrowser()->Players(*pInfo);
	const int MaxPlayers = std::max(ServerBrowser()->Max(*pInfo), 1);
	const float RightW = S(150.0f) + S(56.0f) + JoinW + Pad + S(24.0f);
	const float NameMaxX = Max.x - RightW;
	float x = Min.x + Pad;
	const float NameY = Min.y + RowH * 0.5f - pLabel->FontSize - S(1.0f);
	const ImVec4 NameClip(x, Min.y, NameMaxX, Min.y + RowH);
	pDraw->AddText(pLabel, pLabel->FontSize * 1.05f, ImVec2(x, NameY), Col(t.m_Text, NumPlayers > 0 ? 1.0f : 0.7f), pInfo->m_aName, nullptr, 0.0f, &NameClip);
	const float NameW = std::min(pLabel->CalcTextSizeA(pLabel->FontSize * 1.05f, FLT_MAX, 0.0f, pInfo->m_aName).x, NameMaxX - x);
	float MarkX = x + NameW + S(8.0f);
	const auto Mark = [&](const char *pText, const ImVec4 &Color) {
		if(MarkX > NameMaxX)
			return;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(MarkX, NameY + S(1.0f)), Col(Color), pText);
		MarkX += pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pText).x + S(8.0f);
	};
	if(pInfo->m_Favorite != TRISTATE::NONE)
		Mark(XC_ICON_STAR, t.m_Accent);
	if(pInfo->m_Flags & SERVER_FLAG_PASSWORD)
		Mark(XC_ICON_LOCK, t.m_TextMuted);
	if(g_Config.m_BrIndicateFinished && pInfo->m_HasRank == CServerInfo::RANK_RANKED)
		Mark(XC_ICON_FLAG_CHECK, ImVec4(0.24f, 0.84f, 0.64f, 1.0f));
	if(pInfo->m_FriendState != IFriends::FRIEND_NO)
	{
		char aFriends[32];
		str_format(aFriends, sizeof(aFriends), "%s %d", XC_ICON_HEART, pInfo->m_FriendNum);
		Mark(aFriends, ImVec4(1.0f, 0.36f, 0.54f, 1.0f));
	}
	// players of this client here: the logo and how many, their names under the mouse
	if(const std::vector<std::string> *pExora = GameClient()->m_XcCosmetics.ExoraOn(pInfo->m_aName); pExora && MarkX < NameMaxX)
	{
		const float LogoSize = pSmall->FontSize * 1.05f;
		const ImVec2 MarkMin(MarkX, NameY);
		DrawLogo(pDraw, ImVec2(MarkX + LogoSize * 0.5f, NameY + S(1.0f) + pSmall->FontSize * 0.5f), LogoSize, t.m_Accent, 1.0f, true);
		char aCount[16];
		str_format(aCount, sizeof(aCount), "%d", (int)pExora->size());
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(MarkX + LogoSize + S(4.0f), NameY + S(1.0f)), Col(t.m_Accent), aCount);
		MarkX += LogoSize + S(4.0f) + pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aCount).x + S(8.0f);
		if(ImGui::IsMouseHoveringRect(MarkMin, ImVec2(MarkX, NameY + pLabel->FontSize + S(4.0f))))
		{
			std::string Names;
			for(const std::string &Name : *pExora)
				Names += (Names.empty() ? "" : ", ") + Name;
			ImGui::SetTooltip("%s: %s", Tr("Exora"), Names.c_str());
		}
	}
	char aSub[160];
	str_format(aSub, sizeof(aSub), "%s  ·  %s", pInfo->m_aMap, pInfo->m_aGameType);
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(x, Min.y + RowH * 0.5f + S(3.0f)), Col(t.m_TextMuted), aSub, nullptr, 0.0f, &NameClip);

	// how full: the count over a thin line
	const float FillW = S(150.0f);
	const float FillX = Max.x - Pad - JoinW - S(24.0f) - S(56.0f) - FillW;
	char aPlayers[32];
	str_format(aPlayers, sizeof(aPlayers), "%d / %d", NumPlayers, ServerBrowser()->Max(*pInfo));
	const float PlayersW = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aPlayers).x;
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(FillX + FillW - PlayersW, Min.y + RowH * 0.5f - pSmall->FontSize - S(2.0f)), Col(NumPlayers > 0 ? t.m_TextSecondary : t.m_TextMuted), aPlayers);
	const float LineY = Min.y + RowH * 0.5f + S(5.0f);
	const float Fill = std::clamp((float)NumPlayers / MaxPlayers, 0.0f, 1.0f);
	pDraw->AddRectFilled(ImVec2(FillX, LineY), ImVec2(FillX + FillW, LineY + S(3.0f)), Col(ImVec4(1, 1, 1, 0.08f)), S(2.0f));
	if(Fill > 0.0f)
		pDraw->AddRectFilled(ImVec2(FillX, LineY), ImVec2(FillX + FillW * Fill, LineY + S(3.0f)), Col(Mix(t.m_TextSecondary, t.m_Accent, Fill * Fill)), S(2.0f));

	// the ping: a colored dot and the number
	char aPing[16];
	str_format(aPing, sizeof(aPing), "%s%d", pInfo->m_LatencyIsEstimated ? "~" : "", pInfo->m_Latency);
	const float PingW = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aPing).x;
	const float PingRight = Max.x - Pad - JoinW - S(24.0f);
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(PingRight - PingW, Min.y + (RowH - pSmall->FontSize) * 0.5f), Col(t.m_TextMuted), aPing);
	pDraw->AddCircleFilled(ImVec2(PingRight - PingW - S(9.0f), Min.y + RowH * 0.5f), S(3.5f), Col(PingColor(pInfo->m_Latency)), 12);

	// the join button
	{
		const float Lift = JoinHover * S(1.5f);
		const ImVec2 BMin(JoinMin.x, JoinMin.y - Lift), BMax(JoinMin.x + JoinW, JoinMin.y + JoinH - Lift);
		const float Strong = std::max(JoinHover, Selected ? 1.0f : 0.0f);
		pDraw->AddRectFilled(BMin, BMax, Col(Mix(ImVec4(1, 1, 1, 0.06f + 0.04f * Hover), t.m_Text, Strong)), S(10.0f));
		const char *pJoin = Tr("Join");
		const ImVec2 Dim = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pJoin);
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2((BMin.x + BMax.x - Dim.x) * 0.5f, (BMin.y + BMax.y - Dim.y) * 0.5f), Col(Mix(t.m_TextSecondary, ImVec4(0.06f, 0.07f, 0.1f, 1.0f), Strong)), pJoin);
	}

	// opened in place: the scheme of the map, the players, a big join
	if(Opened > 0.01f)
	{
		const float InnerY = Min.y + RowH;
		const float H = ExtraH - S(14.0f);
		pDraw->PushClipRect(ImVec2(Min.x, InnerY), ImVec2(Max.x, Max.y), true);
		const ImVec2 MapMin(Min.x + Pad, InnerY), MapMax(Min.x + Pad + S(220.0f), InnerY + std::max(H, 1.0f));
		if(!DrawMapPreview(pDraw, pInfo, MapMin, MapMax, S(12.0f), ImDrawFlags_RoundCornersAll, Opened))
		{
			pDraw->AddRectFilled(MapMin, MapMax, Col(ImVec4(1, 1, 1, 0.04f), Opened), S(12.0f));
			const ImVec2 Dim = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pInfo->m_aMap);
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2((MapMin.x + MapMax.x - Dim.x) * 0.5f, (MapMin.y + MapMax.y - Dim.y) * 0.5f), Col(t.m_TextMuted, Opened), pInfo->m_aMap);
		}
		pDraw->PopClipRect();
		if(H > S(20.0f))
		{
			ImGui::SetCursorScreenPos(ImVec2(MapMax.x + S(16.0f), InnerY));
			ImGui::BeginChild("##players", ImVec2(Width - S(220.0f) - Pad * 3.0f, H), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
			ImGui::GetStyle().Alpha *= Opened;
			if(pInfo->m_vClients.empty())
				Description(Tr("Nobody plays here now"));
			else
				RenderServerPlayers(pInfo);
			ImGui::GetStyle().Alpha /= std::max(Opened, 0.01f);
			ImGui::EndChild();
		}
	}

	if(Clicked)
	{
		if(Selected)
			m_ServerExpanded = !m_ServerExpanded;
		else
		{
			m_SelectedServer = pInfo->m_aAddress;
			m_ServerExpanded = true;
		}
		PlayUiSound(SOUND_CLICK);
	}
	if(DoubleClicked || Join)
	{
		PlayUiSound(SOUND_CLICK);
		Client()->Connect(pInfo->m_aAddress);
	}
	ImGui::PopID();
	ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y + S(4.0f)));
	ImGui::Dummy(ImVec2(Width, 0.0f));
	return TotalH;
}

void CXcMenu::RenderServers()
{
	UpdateBrowser();
	const STheme &t = Theme();
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const ImVec2 Area = ImGui::GetContentRegionAvail();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	// the content keeps a readable width in the middle
	const float Width = std::min(Area.x, S(1060.0f));
	const float Left = Origin.x + (Area.x - Width) * 0.5f;

	// the players and the maps change: the list asks again on its own while it is open (in place, nothing jumps
	// away; not on LAN, where a refresh empties the list first)
	if(g_Config.m_XcBrAutoRefresh > 0 && ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_LAN)
	{
		if(ServerBrowser()->IsGettingServerlist() || ServerBrowser()->IsRefreshing())
			m_BrowserAutoTime = 0.0f;
		else
			m_BrowserAutoTime += std::min(Client()->RenderFrameTime(), 0.5f);
		if(m_BrowserAutoTime >= g_Config.m_XcBrAutoRefresh && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			ServerBrowser()->Refresh(ServerBrowser()->GetCurrentType());
			m_BrowserAutoTime = 0.0f;
		}
	}

	// the top: back (on the full screen page), one big search, friends, filters, refresh
	const float BarH = S(48.0f);
	const float Gap = S(10.0f);
	float x = Left;
	const auto Square = [&](const char *pId, const char *pIcon, const char *pTip, bool On) {
		ImGui::SetCursorScreenPos(ImVec2(x, Origin.y));
		const bool Pressed = ImGui::InvisibleButton(pId, ImVec2(BarH, BarH));
		const float Hover = Animate(ImGui::GetID(pId), ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f);
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", pTip);
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		const float Lift = Hover * S(2.0f);
		pDraw->AddRectFilled(ImVec2(x, Origin.y - Lift), ImVec2(x + BarH, Origin.y + BarH - Lift), Col(ImVec4(1, 1, 1, 0.045f + 0.045f * Hover)), S(14.0f));
		pDraw->AddRect(ImVec2(x, Origin.y - Lift), ImVec2(x + BarH, Origin.y + BarH - Lift), Col(On ? ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.55f) : ImVec4(1, 1, 1, 0.07f)), S(14.0f), 0, 1.0f);
		DrawIcon(pDraw, Fonts().m_pIcons, Fonts().m_pIcons->FontSize, ImVec2(x + BarH * 0.5f, Origin.y + BarH * 0.5f - Lift), Col(On ? t.m_Accent : Mix(t.m_TextSecondary, t.m_Text, Hover)), pIcon);
		x += BarH + Gap;
		if(Pressed)
			PlayUiSound(SOUND_CLICK);
		return Pressed;
	};
	if(m_ServersFullScreen && Square("##sv_back", XC_ICON_ARROW_LEFT, Tr("Back"), false))
		m_Page = PAGE_HOME;
	const float SearchW = Left + Width - x - (BarH + Gap) * 3.0f;
	ImGui::SetCursorScreenPos(ImVec2(x, Origin.y));
	{
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		pDraw->AddRectFilled(ImVec2(x, Origin.y), ImVec2(x + SearchW, Origin.y + BarH), Col(ImVec4(1, 1, 1, 0.045f)), S(16.0f));
		pDraw->AddRect(ImVec2(x, Origin.y), ImVec2(x + SearchW, Origin.y + BarH), Col(ImVec4(1, 1, 1, 0.07f)), S(16.0f), 0, 1.0f);
		DrawIcon(pDraw, Fonts().m_pLabel, Fonts().m_pLabel->FontSize, ImVec2(x + S(22.0f), Origin.y + BarH * 0.5f), Col(t.m_TextMuted), XC_ICON_SEARCH);
		ImGui::SetCursorScreenPos(ImVec2(x + S(40.0f), Origin.y + (BarH - S(34.0f)) * 0.5f));
		ImGui::BeginChild("##sv_search", ImVec2(SearchW - S(52.0f), S(34.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
		// typing anywhere on the page goes to the search
		if(m_SearchFocusNext)
		{
			ImGui::SetKeyboardFocusHere();
			m_SearchFocusNext = false;
		}
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
		if(InputText("##server_search", g_Config.m_BrFilterString, sizeof(g_Config.m_BrFilterString), Tr("Server, map or player"), SearchW - S(52.0f)))
			Client()->ServerBrowserUpdate();
		ImGui::PopStyleColor(3);
		ImGui::EndChild();
		x += SearchW + Gap;
	}
	if(Square("##sv_friends", XC_ICON_USERS, Tr("Friends"), m_BrowserDrawerOpen && m_BrowserDrawerTab == 1))
	{
		m_BrowserDrawerOpen = !(m_BrowserDrawerOpen && m_BrowserDrawerTab == 1);
		m_BrowserDrawerTab = 1;
	}
	if(Square("##sv_filters", XC_ICON_FILTER, Tr("Filters and sorting"), m_BrowserDrawerOpen && m_BrowserDrawerTab == 0))
	{
		m_BrowserDrawerOpen = !(m_BrowserDrawerOpen && m_BrowserDrawerTab == 0);
		m_BrowserDrawerTab = 0;
	}
	if(Square("##sv_refresh", XC_ICON_ROTATE, Tr("Refresh"), false))
	{
		GameClient()->m_Menus.RefreshBrowserTab(true);
		m_BrowserAutoTime = 0.0f;
	}

	// the lists as quiet pills: internet, favorites, LAN and the community tabs
	float y = Origin.y + BarH + S(14.0f);
	{
		struct SPill
		{
			int m_Page;
			const char *m_pLabel;
		};
		// "Exora": the internet list, only the servers where players of this client are now
		GameClient()->m_XcCosmetics.WantServers();
		char aExora[48];
		if(GameClient()->m_XcCosmetics.ExoraPlayersTotal() > 0)
			str_format(aExora, sizeof(aExora), "Exora  %d", GameClient()->m_XcCosmetics.ExoraPlayersTotal());
		else
			str_copy(aExora, "Exora");
		std::vector<SPill> vPills = {{CMenus::PAGE_INTERNET, Tr("All")}, {PILL_EXORA, aExora}, {CMenus::PAGE_FAVORITES, Tr("Favorites")}, {CMenus::PAGE_LAN, "LAN"}};
		const std::vector<const CCommunity *> vpFavoriteCommunities = ServerBrowser()->FavoriteCommunities();
		for(size_t i = 0; i < vpFavoriteCommunities.size() && i < 5; i++)
			vPills.push_back({CMenus::PAGE_FAVORITE_COMMUNITY_1 + (int)i, vpFavoriteCommunities[i]->Name()});
		ImFont *pFont = Fonts().m_pLabel;
		float px = Left;
		const float PillH = S(32.0f);
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		for(size_t i = 0; i < vPills.size(); i++)
		{
			const SPill &Pill = vPills[i];
			const float W = pFont->CalcTextSizeA(pFont->FontSize, FLT_MAX, 0.0f, Pill.m_pLabel).x + S(28.0f);
			ImGui::SetCursorScreenPos(ImVec2(px, y));
			ImGui::PushID((int)i);
			const bool Pressed = ImGui::InvisibleButton("##pill", ImVec2(W, PillH));
			const float Hover = Animate(ImGui::GetID("##pill_h"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f);
			const bool On = Pill.m_Page == PILL_EXORA ? m_BrowserExoraOnly : (!m_BrowserExoraOnly && g_Config.m_UiPage == Pill.m_Page);
			const float Sel = Animate(ImGui::GetID("##pill_s"), On ? 1.0f : 0.0f, 14.0f);
			ImGui::PopID();
			pDraw->AddRectFilled(ImVec2(px, y), ImVec2(px + W, y + PillH), Col(Mix(ImVec4(1, 1, 1, 0.05f * Hover), t.m_Text, Sel)), PillH * 0.5f);
			const ImVec4 TextCol = Mix(Mix(t.m_TextMuted, t.m_Text, Hover), ImVec4(0.06f, 0.07f, 0.1f, 1.0f), Sel);
			pDraw->AddText(pFont, pFont->FontSize, ImVec2(px + S(14.0f), y + (PillH - pFont->FontSize) * 0.5f), Col(TextCol), Pill.m_pLabel);
			if(Pressed && !On)
			{
				m_BrowserExoraOnly = Pill.m_Page == PILL_EXORA;
				const int Page = m_BrowserExoraOnly ? (int)CMenus::PAGE_INTERNET : Pill.m_Page;
				if(g_Config.m_UiPage != Page)
				{
					g_Config.m_UiPage = Page;
					GameClient()->m_Menus.RefreshBrowserTab(false);
				}
				PlayUiSound(SOUND_TAB);
			}
			px += W + S(6.0f);
		}
		y += PillH + S(10.0f);
	}

	// the list: rows, drawn only where they are seen; a row may open in place
	const float FootH = S(40.0f);
	const float ListH = Origin.y + Area.y - FootH - y;
	ImGui::SetCursorScreenPos(ImVec2(Left - S(6.0f), y));
	BeginInnerScroll("##server_rows", ImVec2(Width + S(12.0f), ListH));
	const int NumServers = ServerBrowser()->NumSortedServers();
	if(m_BrowserExoraOnly && g_Config.m_UiPage != CMenus::PAGE_INTERNET)
		m_BrowserExoraOnly = false;
	int NumShown = NumServers;
	if(m_BrowserExoraOnly)
	{
		NumShown = 0;
		for(int i = 0; i < NumServers; i++)
			NumShown += GameClient()->m_XcCosmetics.ExoraOn(ServerBrowser()->SortedGet(i)->m_aName) != nullptr;
	}
	if(m_BrowserExoraOnly && NumShown == 0 && NumServers > 0)
	{
		ImGui::Dummy(ImVec2(0.0f, S(30.0f)));
		Title(GameClient()->m_XcCosmetics.ServersKnown() ? Tr("Nobody plays Exora on a server now") : Tr("Asking who plays Exora..."));
		Description(Tr("Here are the servers where players of Exora Client are. Only those who share their cosmetics are seen (Settings, Online)."));
	}
	else if(NumServers == 0)
	{
		const bool Loading = ServerBrowser()->IsGettingServerlist() || ServerBrowser()->IsRefreshing();
		ImGui::Dummy(ImVec2(0.0f, S(30.0f)));
		Title(Loading ? Tr("Loading servers...") : Tr("No servers found"));
		Description(Loading ? Tr("Asking the master servers for the list") : (ServerBrowser()->IsServerlistError() ? Tr("Could not get the server list, try to refresh") : Tr("Try another filter or refresh the list")));
		if(!Loading && Button(Tr("Reset filters")))
		{
			GameClient()->m_Menus.ResetServerbrowserFilters();
			Client()->ServerBrowserUpdate();
		}
	}
	else
	{
		const float ViewTop = ImGui::GetScrollY();
		const float ViewBottom = ViewTop + ImGui::GetWindowHeight();
		const float RowW = ImGui::GetContentRegionAvail().x - S(6.0f);
		for(int i = 0; i < NumServers; i++)
		{
			const CServerInfo *pInfo = ServerBrowser()->SortedGet(i);
			if(m_BrowserExoraOnly && !GameClient()->m_XcCosmetics.ExoraOn(pInfo->m_aName))
				continue;
			// the play button of the home page: the last server is picked, opened and scrolled into sight
			if(m_ScrollToSelected && !m_SelectedServer.empty() && str_find(pInfo->m_aAddress, m_SelectedServer.c_str()))
			{
				m_SelectedServer = pInfo->m_aAddress;
				m_ServerExpanded = true;
				ImGui::SetScrollHereY(0.25f);
				m_ScrollToSelected = false;
			}
			const float RowTop = ImGui::GetCursorPosY();
			const bool Visible = RowTop + S(260.0f) >= ViewTop && RowTop <= ViewBottom;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(6.0f));
			RenderServerRow(pInfo, RowW, Visible);
		}
	}
	EndInnerScroll();

	// the foot: how many, the address field
	ImGui::SetCursorScreenPos(ImVec2(Left, Origin.y + Area.y - FootH + S(6.0f)));
	ImGui::BeginChild("##browser_status", ImVec2(Width, FootH - S(6.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	RenderBrowserStatus();
	ImGui::EndChild();

	// the drawer on the right: filters (with the communities and the map types) or friends
	m_BrowserDrawer = std::clamp(m_BrowserDrawer + (m_BrowserDrawerOpen ? Dt : -Dt) / 0.35f, 0.0f, 1.0f);
	if(m_BrowserDrawer > 0.0f)
	{
		const float k = 1.0f - std::pow(1.0f - m_BrowserDrawer, 3.0f);
		const float DrawerW = std::min(S(380.0f), Area.x * 0.9f);
		const ImVec2 DMin(Origin.x + Area.x - DrawerW + (1.0f - k) * S(30.0f), Origin.y + BarH + S(14.0f));
		const ImVec2 DMax(DMin.x + DrawerW, Origin.y + Area.y);
		const float OldAlpha = ImGui::GetStyle().Alpha;
		ImGui::GetStyle().Alpha *= k;
		ImGui::SetCursorScreenPos(DMin);
		ImGui::BeginChild("##browser_drawer", ImVec2(DrawerW, DMax.y - DMin.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		pDraw->AddRectFilled(DMin, DMax, Col(t.m_Panel), S(18.0f));
		pDraw->AddRect(DMin, DMax, Col(t.m_Border), S(18.0f), 0, 1.0f);
		const float Pad = S(18.0f);
		ImGui::SetCursorScreenPos(ImVec2(DMin.x + Pad, DMin.y + Pad));
		ImGui::BeginChild("##drawer_tabs", ImVec2(DrawerW - Pad * 2.0f, S(34.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
		const char *apTabs[] = {Tr("Filters"), Tr("Friends")};
		Segmented("##drawer_tab", &m_BrowserDrawerTab, apTabs, 2);
		ImGui::EndChild();
		ImGui::SetCursorScreenPos(ImVec2(DMin.x + Pad, DMin.y + Pad + S(46.0f)));
		BeginInnerScroll("##drawer_body", ImVec2(DrawerW - Pad * 2.0f, DMax.y - DMin.y - Pad * 2.0f - S(46.0f)));
		if(m_BrowserDrawerTab == 1)
			RenderBrowserFriends();
		else
		{
			RenderBrowserCommunities();
			RenderBrowserTypes();
			ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
			RenderBrowserFilters();
		}
		EndInnerScroll();
		ImGui::EndChild();
		ImGui::GetStyle().Alpha = OldAlpha;
		// a click outside closes it
		if(m_BrowserDrawerOpen && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseHoveringRect(DMin, DMax, false) && ImGui::GetIO().MousePos.y > Origin.y + BarH)
			m_BrowserDrawerOpen = false;
	}

	// typing on the page goes to the search; Esc on the full screen page goes home
	if(!ImGui::GetIO().WantTextInput && ImGui::GetIO().InputQueueCharacters.Size > 0)
		m_SearchFocusNext = true;
	if(m_ServersFullScreen && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput)
		m_Page = PAGE_HOME;
}

// Votes page: the running vote with yes/no, the vote options of the server and votes to kick a player
// or move them to the spectators. Only calls the normal DDNet votes, like the classic menu does.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>

#include <game/client/gameclient.h>

#include <algorithm>

using namespace XcUi;

namespace
{
// a row of a list: rounded plate, accent when selected
bool ListRow(const char *pId, const char *pText, const char *pSubText, bool Selected, bool *pDoubleClicked)
{
	const STheme &t = Theme();
	const float Height = S(34.0f);
	const float Width = AvailWidth();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(Width, Height));
	const bool Hovered = ImGui::IsItemHovered();
	*pDoubleClicked = Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Hover = Animate(ImGui::GetID(pId), Hovered ? 1.0f : 0.0f);
	if(Selected)
	{
		pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.2f)), S(8.0f));
		pDraw->AddRect(Min, Max, Col(t.m_Accent, 0.8f), S(8.0f), 0, S(1.2f));
	}
	else
		pDraw->AddRectFilled(Min, Max, Col(t.m_Track, 0.55f + Hover * 0.45f), S(8.0f));
	ImFont *pFont = Fonts().m_pLabel;
	const float TextY = Min.y + (Height - pFont->FontSize) * 0.5f;
	pDraw->PushClipRect(Min, Max, true);
	pDraw->AddText(pFont, pFont->FontSize, ImVec2(Min.x + S(12.0f), TextY), Col(Selected ? t.m_Text : t.m_TextSecondary), pText);
	if(pSubText && pSubText[0])
	{
		ImFont *pSmall = Fonts().m_pSmall;
		const float SubWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pSubText).x;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Max.x - SubWidth - S(12.0f), Min.y + (Height - pSmall->FontSize) * 0.5f), Col(t.m_TextMuted), pSubText);
	}
	pDraw->PopClipRect();
	return Clicked;
}
} // namespace

void CXcMenu::RenderVotes()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		RenderPlaceholder(XC_ICON_CHECK, Tr("Votes"), Tr("Join a server to vote"), false);
		return;
	}

	const STheme &t = Theme();
	CVoting &Voting = GameClient()->m_Voting;
	const bool Admin = Client()->RconAuthed();
	BeginCardGrid("##votes", 2);

	if(BeginCard("##vote_now", Tr("Current vote"), Voting.IsVoting() ? Tr("Vote yes or no, the bar shows how the others voted") : Tr("Nobody is voting right now")))
	{
		if(Voting.IsVoting())
		{
			Title(Voting.VoteDescription());
			char aInfo[128];
			if(Voting.VoteReason()[0])
				str_format(aInfo, sizeof(aInfo), "%s: %s · %s %d", Tr("Reason"), Voting.VoteReason(), Tr("seconds left"), Voting.SecondsLeft());
			else
				str_format(aInfo, sizeof(aInfo), "%s %d", Tr("seconds left"), Voting.SecondsLeft());
			Description(aInfo);

			// one bar: yes from the left in mint, no from the right in red
			const ImVec4 Yes(0.24f, 0.84f, 0.64f, 1.0f);
			const ImVec4 No(1.0f, 0.3f, 0.3f, 1.0f);
			const float Width = AvailWidth();
			const float Height = S(10.0f);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			ImDrawList *pDraw = ImGui::GetWindowDrawList();
			pDraw->AddRectFilled(Min, ImVec2(Min.x + Width, Min.y + Height), Col(t.m_Track), Height * 0.5f);
			const float Total = (float)std::max(Voting.XcTotal(), 1);
			const float YesPart = Animate(ImGui::GetID("##yes_bar"), Voting.XcYes() / Total);
			const float NoPart = Animate(ImGui::GetID("##no_bar"), Voting.XcNo() / Total);
			if(YesPart > 0.001f)
				pDraw->AddRectFilled(Min, ImVec2(Min.x + Width * YesPart, Min.y + Height), Col(Yes), Height * 0.5f);
			if(NoPart > 0.001f)
				pDraw->AddRectFilled(ImVec2(Min.x + Width * (1.0f - NoPart), Min.y), ImVec2(Min.x + Width, Min.y + Height), Col(No), Height * 0.5f);
			ImGui::Dummy(ImVec2(Width, Height + S(4.0f)));
			char aCount[128];
			str_format(aCount, sizeof(aCount), "%s %d · %s %d · %s %d", Tr("Yes"), Voting.XcYes(), Tr("No"), Voting.XcNo(), Tr("Did not vote"), std::max(Voting.XcTotal() - Voting.XcYes() - Voting.XcNo(), 0));
			Description(aCount);

			const float ButtonWidth = (AvailWidth() - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
			const int Taken = Voting.TakenChoice();
			if(Button(Taken == 1 ? Tr("You voted yes") : Tr("Yes (F3)"), Taken != -1, ButtonWidth))
				Voting.Vote(1);
			ImGui::SameLine();
			if(Button(Taken == -1 ? Tr("You voted no") : Tr("No (F4)"), false, ButtonWidth))
				Voting.Vote(-1);
		}
		EndCard();
	}

	if(BeginCard("##vote_reason", Tr("Reason"), Tr("Optional, other players see it next to your vote")))
	{
		InputText("##reason", m_aVoteReason, sizeof(m_aVoteReason), Tr("Why you start the vote"));
		if(Admin)
			Description(Tr("You are an admin: hold Ctrl when you start a vote to force it"));
		EndCard();
	}

	if(BeginCard("##vote_options", Tr("Server votes"), Tr("Click a vote and start it, a double click starts it at once")))
	{
		InputText("##vote_filter", m_aVoteFilter, sizeof(m_aVoteFilter), Tr("Search votes"));
		const int NumOptions = Voting.NumOptions();
		const float ListHeight = std::clamp(NumOptions * S(38.0f), S(38.0f), S(380.0f));
		ImGui::BeginChild("##options", ImVec2(AvailWidth(), ListHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, S(4.0f)));
		int Index = 0;
		int Shown = 0;
		bool Start = false;
		for(const CVoteOptionClient *pOption = Voting.FirstOption(); pOption; pOption = pOption->m_pNext, Index++)
		{
			if(m_aVoteFilter[0] && !str_utf8_find_nocase(pOption->m_aDescription, m_aVoteFilter))
				continue;
			Shown++;
			ImGui::PushID(Index);
			bool Double = false;
			if(ListRow("##option", pOption->m_aDescription, nullptr, m_VoteOption == Index, &Double))
			{
				m_VoteOption = Index;
				PlayUiSound(SOUND_CLICK);
			}
			Start |= Double;
			ImGui::PopID();
		}
		ImGui::PopStyleVar();
		ImGui::EndChild();
		if(Voting.IsReceivingOptions())
			Description(Tr("Loading the votes of the server..."));
		else if(Shown == 0)
			Description(NumOptions == 0 ? Tr("This server has no votes") : Tr("No votes found"));
		ImGui::BeginDisabled(m_VoteOption < 0 || m_VoteOption >= NumOptions || Voting.IsVoting());
		if(Button(Tr("Start the vote"), true, AvailWidth()) || (Start && !Voting.IsVoting()))
		{
			Voting.CallvoteOption(m_VoteOption, m_aVoteReason, Admin && Input()->ModifierIsPressed());
			m_aVoteReason[0] = '\0';
			Toast(Tr("Vote started"), TOAST_SUCCESS);
		}
		ImGui::EndDisabled();
		EndCard();
	}

	if(BeginCard("##vote_players", Tr("Players"), Tr("Start a vote to kick a player or move them to the spectators")))
	{
		const int LocalId = GameClient()->m_Snap.m_LocalClientId;
		int NumOthers = 0;
		for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
			NumOthers += pInfo && pInfo->m_ClientId != LocalId;
		const float ListHeight = std::clamp(NumOthers * S(38.0f), S(4.0f), S(300.0f));
		ImGui::BeginChild("##players", ImVec2(AvailWidth(), ListHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, S(4.0f)));
		bool Found = false;
		for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
		{
			if(!pInfo || pInfo->m_ClientId == LocalId || pInfo->m_ClientId < 0 || pInfo->m_ClientId >= MAX_CLIENTS)
				continue;
			const int Id = pInfo->m_ClientId;
			if(Id == m_VotePlayer)
				Found = true;
			const auto &ClientData = GameClient()->m_aClients[Id];
			ImGui::PushID(Id);
			bool Double = false;
			if(ListRow("##player", ClientData.m_aName, ClientData.m_aClan, m_VotePlayer == Id, &Double))
			{
				m_VotePlayer = Id;
				PlayUiSound(SOUND_CLICK);
			}
			ImGui::PopID();
		}
		ImGui::PopStyleVar();
		ImGui::EndChild();
		if(!Found)
			m_VotePlayer = -1;
		if(NumOthers == 0)
			Description(Tr("Nobody else is on the server"));
		ImGui::BeginDisabled(m_VotePlayer < 0 || Voting.IsVoting());
		const float ButtonWidth = (AvailWidth() - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		if(Button(Tr("Vote to kick"), false, ButtonWidth))
		{
			Voting.CallvoteKick(m_VotePlayer, m_aVoteReason, Admin && Input()->ModifierIsPressed());
			m_aVoteReason[0] = '\0';
			Toast(Tr("Vote started"), TOAST_SUCCESS);
		}
		ImGui::SameLine();
		if(Button(Tr("Vote to spectators"), false, ButtonWidth))
		{
			Voting.CallvoteSpectate(m_VotePlayer, m_aVoteReason, Admin && Input()->ModifierIsPressed());
			m_aVoteReason[0] = '\0';
			Toast(Tr("Vote started"), TOAST_SUCCESS);
		}
		ImGui::EndDisabled();
		EndCard();
	}
	EndCardGrid();
}

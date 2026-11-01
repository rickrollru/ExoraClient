#include "lyrics.h"

#include "music.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <game/client/components/xclient/xc_helpers.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/gameclient.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;
using namespace XcUi;

// the words on the map: how long one stays, its size in world units
static constexpr float WORD_LIFE = 2.6f;
static constexpr float WORD_SIZE = 26.0f;

// LRCLIB asks the programs that use it to say who they are
static const char *const LRCLIB_AGENT = "ExoraClient (https://github.com/rickrollru/ExoraClient)";

std::vector<CXcLyrics::SLine> CXcLyrics::ParseLrc(const char *pLrc)
{
	std::vector<SLine> vLines;
	const char *p = pLrc;
	while(p && *p)
	{
		const char *pEnd = str_find(p, "\n");
		std::string Line(p, pEnd ? (size_t)(pEnd - p) : str_length(p));
		p = pEnd ? pEnd + 1 : nullptr;
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		// all the times in front, then the text
		std::vector<double> vTimes;
		size_t Pos = 0;
		while(Pos < Line.size() && Line[Pos] == '[')
		{
			const size_t Close = Line.find(']', Pos);
			if(Close == std::string::npos)
				break;
			const std::string Tag = Line.substr(Pos + 1, Close - Pos - 1);
			int Minutes = 0;
			float Seconds = 0.0f;
			// tags like [ar:...] or [length: 3:20] have letters: not a time
			if(!Tag.empty() && Tag[0] >= '0' && Tag[0] <= '9' && sscanf(Tag.c_str(), "%d:%f", &Minutes, &Seconds) == 2)
				vTimes.push_back(Minutes * 60.0 + Seconds);
			Pos = Close + 1;
		}
		std::string Text = Line.substr(Pos);
		while(!Text.empty() && Text[0] == ' ')
			Text.erase(0, 1);
		for(double Time : vTimes)
			vLines.push_back({Time, Text});
	}
	std::stable_sort(vLines.begin(), vLines.end(), [](const SLine &a, const SLine &b) { return a.m_Time < b.m_Time; });
	return vLines;
}

void CXcLyrics::CleanTitle(const std::string &Artist, const std::string &Title, std::string &OutArtist, std::string &OutTitle)
{
	std::string Clean;
	// whatever is in brackets: (Official Video), [4K], (Lyrics)...
	int Depth = 0;
	for(char c : Title)
	{
		if(c == '(' || c == '[')
			Depth++;
		else if((c == ')' || c == ']') && Depth > 0)
			Depth--;
		else if(Depth == 0)
			Clean += c;
	}
	OutArtist = Artist;
	// the channels of YouTube: "Artist - Topic", "ArtistVEVO"
	bool Channel = false;
	for(const char *pSuffix : {" - Topic", "VEVO", "Vevo"})
		if(OutArtist.size() > (size_t)str_length(pSuffix) && OutArtist.compare(OutArtist.size() - str_length(pSuffix), std::string::npos, pSuffix) == 0)
		{
			OutArtist.erase(OutArtist.size() - str_length(pSuffix));
			Channel = true;
		}
	// "Artist - Title" in one line, as the browser tabs of YouTube have it
	const size_t Dash = Clean.find(" - ");
	if(Dash != std::string::npos)
	{
		const std::string Before = Clean.substr(0, Dash);
		// the channel of YouTube is often "Artist - Topic" or "ArtistVEVO": the part of the title is better
		std::string BeforeBare, ArtistBare;
		for(char c : Before)
			if(c != ' ')
				BeforeBare += c;
		for(char c : OutArtist)
			if(c != ' ')
				ArtistBare += c;
		if(OutArtist.empty() || Channel || str_find_nocase(BeforeBare.c_str(), ArtistBare.c_str()) || str_find_nocase(ArtistBare.c_str(), BeforeBare.c_str()))
			OutArtist = Before;
		Clean = Clean.substr(Dash + 3);
	}
	// "feat. Someone" is not part of the title in the database
	for(const char *pFeat : {" feat.", " ft.", " Feat.", " Ft."})
	{
		const size_t At = Clean.find(pFeat);
		if(At != std::string::npos)
			Clean = Clean.substr(0, At);
	}
	const auto Trim = [](std::string &s) {
		while(!s.empty() && (s.back() == ' ' || s.back() == '-' || s.back() == '|'))
			s.pop_back();
		while(!s.empty() && s.front() == ' ')
			s.erase(0, 1);
	};
	Trim(Clean);
	Trim(OutArtist);
	OutTitle = Clean;
}

void CXcLyrics::OnShutdown()
{
	if(m_pRequest)
		m_pRequest->Abort();
	m_pRequest = nullptr;
}

void CXcLyrics::StartSearch()
{
	m_Step = STEP_LRCLIB_GET;
	m_NeteaseId = 0;
	m_PlainLyrics.clear();
	m_Approx = false;
	m_State = STATE_LOADING;
	if(m_Duration <= 0.0)
		m_Step = STEP_LRCLIB_GET_ANY;
	RunStep();
}

void CXcLyrics::RunStep()
{
	char aArtist[256], aTitle[256], aWords[512], aUrl[1024];
	EscapeUrl(aArtist, m_Artist.c_str());
	EscapeUrl(aTitle, m_Title.c_str());
	char aBoth[512];
	str_format(aBoth, sizeof(aBoth), "%s %s", m_Artist.c_str(), m_Title.c_str());
	EscapeUrl(aWords, aBoth);
	bool Netease = false;
	switch(m_Step)
	{
	case STEP_LRCLIB_GET: str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/get?track_name=%s&artist_name=%s&duration=%d", aTitle, aArtist, (int)std::round(m_Duration)); break;
	case STEP_LRCLIB_GET_ANY: str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/get?track_name=%s&artist_name=%s", aTitle, aArtist); break;
	case STEP_LRCLIB_FIELDS: str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?track_name=%s&artist_name=%s", aTitle, aArtist); break;
	case STEP_LRCLIB_WORDS: str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?q=%s", aWords); break;
	case STEP_LRCLIB_TITLE: str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?q=%s", aTitle); break;
	case STEP_NETEASE_SEARCH:
		str_format(aUrl, sizeof(aUrl), "https://music.163.com/api/search/get/web?s=%s&type=1&limit=8", aWords);
		Netease = true;
		break;
	case STEP_NETEASE_LYRIC:
		str_format(aUrl, sizeof(aUrl), "https://music.163.com/api/song/lyric?id=%lld&lv=1", m_NeteaseId);
		Netease = true;
		break;
	default:
		Finish(false);
		return;
	}
	m_pRequest = HttpGet(aUrl);
	if(Netease)
	{
		m_pRequest->HeaderString("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
		m_pRequest->HeaderString("Referer", "https://music.163.com/");
	}
	else
		m_pRequest->HeaderString("User-Agent", LRCLIB_AGENT);
	m_pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	m_pRequest->MaxResponseSize(4 * 1024 * 1024);
	m_pRequest->LogProgress(HTTPLOG::NONE);
	Http()->Run(m_pRequest);
}

std::vector<CXcLyrics::SLine> CXcLyrics::SpreadPlain(const std::string &Text) const
{
	std::vector<std::string> vTexts;
	size_t Start = 0;
	while(Start <= Text.size())
	{
		size_t End = Text.find('\n', Start);
		if(End == std::string::npos)
			End = Text.size();
		std::string Line = Text.substr(Start, End - Start);
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		if(!Line.empty())
			vTexts.push_back(Line);
		Start = End + 1;
	}
	std::vector<SLine> vLines;
	if(vTexts.empty())
		return vLines;
	// from a little after the start to a little before the end; a guess, the plate says so
	const double Length = m_Duration > 30.0 ? m_Duration : 180.0;
	const double From = Length * 0.07, To = Length * 0.95;
	for(size_t i = 0; i < vTexts.size(); i++)
		vLines.push_back({From + (To - From) * i / vTexts.size(), vTexts[i]});
	return vLines;
}

void CXcLyrics::Finish(bool Found)
{
	if(!Found && !m_PlainLyrics.empty() && g_Config.m_XcLyricsExtra)
	{
		m_vLines = SpreadPlain(m_PlainLyrics);
		m_Approx = true;
		Found = !m_vLines.empty();
	}
	m_State = Found ? STATE_FOUND : STATE_NOT_FOUND;
	if(!Found)
		m_vLines.clear();
	m_Cache[m_Key] = {m_vLines, m_Approx, Client()->GlobalTime()};
}

void CXcLyrics::TakeAnswer()
{
	std::shared_ptr<IHttpRequest> pRequest = m_pRequest;
	m_pRequest = nullptr;
	json_value *pJson = pRequest->State() == EHttpState::DONE ? pRequest->ResultJson() : nullptr;
	std::string Lrc;
	// the closest length wins when the track tells its length
	const auto Closer = [&](double Length, double Best) { return m_Duration <= 0.0 || std::abs(Length - m_Duration) < std::abs(Best - m_Duration); };
	if(pJson && m_Step <= STEP_LRCLIB_TITLE)
	{
		const auto Take = [&](const json_value *pItem, double &BestLength) {
			const json_value *pSynced = json_object_get(pItem, "syncedLyrics");
			const json_value *pPlain = json_object_get(pItem, "plainLyrics");
			const json_value *pLength = json_object_get(pItem, "duration");
			const double Length = pLength && pLength->type == json_integer ? (double)pLength->u.integer : (pLength && pLength->type == json_double ? pLength->u.dbl : -1.0);
			// a search by words can find another song: its length must fit
			if(m_Step >= STEP_LRCLIB_WORDS && m_Duration > 0.0 && Length > 0.0 && std::abs(Length - m_Duration) > 8.0)
				return;
			if(pSynced && pSynced->type == json_string && json_string_get(pSynced)[0] && (Lrc.empty() || Closer(Length, BestLength)))
			{
				Lrc = json_string_get(pSynced);
				BestLength = Length;
			}
			if(m_PlainLyrics.empty() && pPlain && pPlain->type == json_string)
				m_PlainLyrics = json_string_get(pPlain);
		};
		double BestLength = 1e9;
		if(pJson->type == json_object)
			Take(pJson, BestLength);
		else if(pJson->type == json_array)
			for(int i = 0; i < json_array_length(pJson); i++)
				Take(json_array_get(pJson, i), BestLength);
	}
	else if(pJson && m_Step == STEP_NETEASE_SEARCH)
	{
		// the song with the closest length
		const json_value *pSongs = json_object_get(json_object_get(pJson, "result"), "songs");
		double BestLength = 1e9;
		for(int i = 0; pSongs && pSongs->type == json_array && i < json_array_length(pSongs); i++)
		{
			const json_value *pSong = json_array_get(pSongs, i);
			const json_value *pId = json_object_get(pSong, "id");
			const json_value *pLength = json_object_get(pSong, "duration");
			const double Length = pLength && pLength->type == json_integer ? pLength->u.integer / 1000.0 : -1.0;
			if(!pId || pId->type != json_integer)
				continue;
			if(m_Duration > 0.0 && Length > 0.0 && std::abs(Length - m_Duration) > 8.0)
				continue;
			if(m_NeteaseId == 0 || Closer(Length, BestLength))
			{
				m_NeteaseId = pId->u.integer;
				BestLength = Length;
			}
		}
	}
	else if(pJson && m_Step == STEP_NETEASE_LYRIC)
	{
		const json_value *pLyric = json_object_get(json_object_get(pJson, "lrc"), "lyric");
		if(pLyric && pLyric->type == json_string)
			Lrc = json_string_get(pLyric);
	}
	if(pJson)
		json_value_free(pJson);

	if(!Lrc.empty())
	{
		std::vector<SLine> vLines = ParseLrc(Lrc.c_str());
		// NetEase puts the credits ("作词 : ...") in timed lines at the start
		vLines.erase(std::remove_if(vLines.begin(), vLines.end(), [](const SLine &Line) { return Line.m_Text.find(" : ") != std::string::npos && Line.m_Time < 30.0 && (Line.m_Text.find("\xe4\xbd\x9c") != std::string::npos || Line.m_Text.find("\xe7\xbc\x96") != std::string::npos || Line.m_Text.find("\xe5\x88\xb6") != std::string::npos); }), vLines.end());
		int Words = 0;
		for(const SLine &Line : vLines)
			Words += !Line.m_Text.empty();
		if(Words >= 3)
		{
			m_vLines = vLines;
			m_Approx = false;
			Finish(true);
			return;
		}
	}
	// the next try
	if(m_Step == STEP_NETEASE_SEARCH && m_NeteaseId == 0)
		m_Step = STEP_NETEASE_LYRIC; // nothing to ask the lyrics of
	m_Step++;
	if(m_Step >= STEP_NETEASE_SEARCH && !g_Config.m_XcLyricsExtra)
		m_Step = NUM_STEPS;
	if(m_Step >= NUM_STEPS)
		Finish(false);
	else
		RunStep();
}

void CXcLyrics::OnRender()
{
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const CXcMusic &Music = GameClient()->m_XcMusic;
	const bool InGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(!g_Config.m_XcLyrics || !Music.HasTrack())
	{
		m_Fade = std::max(m_Fade - Dt * 3.0f, 0.0f);
		m_vWords.clear();
		return;
	}

	// a new track: the lyrics from the memory, or ask for them; only when the player tells the same track for a
	// second (switching tracks or pausing tells empty or half names for a moment, each cut the search before)
	const SXcMediaState &State = Music.State();
	const std::string Key = State.m_Artist + "\x1f" + State.m_Title;
	const float Now = Client()->GlobalTime();
	if(Key != m_SeenKey)
	{
		m_SeenKey = Key;
		m_SeenTime = Now;
	}
	const bool Settled = Now - m_SeenTime >= 1.0f;
	// a search that failed is tried again after two minutes (the network may have been down)
	auto Cached = m_Cache.find(Key);
	const bool Retry = Key == m_Key && m_State == STATE_NOT_FOUND && !m_pRequest && Cached != m_Cache.end() && Now - Cached->second.m_Time > 120.0f;
	if(Settled && (Key != m_Key || Retry))
	{
		if(m_pRequest)
			m_pRequest->Abort();
		m_pRequest = nullptr;
		m_Key = Key;
		m_vLines.clear();
		m_vWords.clear();
		m_LastLine = -1;
		CleanTitle(State.m_Artist, State.m_Title, m_Artist, m_Title);
		m_Duration = State.m_Duration;
		if(Cached != m_Cache.end() && !Retry && (!Cached->second.m_vLines.empty() || Now - Cached->second.m_Time <= 120.0f))
		{
			m_vLines = Cached->second.m_vLines;
			m_Approx = Cached->second.m_Approx;
			m_State = m_vLines.empty() ? STATE_NOT_FOUND : STATE_FOUND;
		}
		else if(!m_Title.empty())
			StartSearch();
		else
			m_State = STATE_NOT_FOUND;
	}
	// another track than the lyrics are for: nothing shown meanwhile
	if(Key != m_Key)
	{
		m_Fade = std::max(m_Fade - Dt * 3.0f, 0.0f);
		m_vWords.clear();
		return;
	}
	if(m_pRequest && m_pRequest->Done())
		TakeAnswer();

	const double Position = Music.Position() + g_Config.m_XcLyricsOffset / 1000.0;
	const bool Playing = Music.PausedFor() <= 0.0f;
	m_Fade = std::clamp(m_Fade + (m_State == STATE_FOUND && Playing ? Dt : -Dt) * 3.0f, 0.0f, 1.0f);
	if(m_State != STATE_FOUND || Position < 0.0 || !InGame)
	{
		m_vWords.clear();
		m_PlateLine = -2;
		return;
	}

	// the line sung now: the last one that has started
	int Line = -1;
	for(int i = 0; i < (int)m_vLines.size() && m_vLines[i].m_Time <= Position; i++)
		Line = i;
	if(Line != m_LastLine)
	{
		m_LastLine = Line;
		// the words of the new line pop up one by one over the time of the line
		if(Line >= 0 && Playing && (g_Config.m_XcLyricsShow == SHOW_MAP || g_Config.m_XcLyricsShow == SHOW_BOTH))
		{
			const double Next = Line + 1 < (int)m_vLines.size() ? m_vLines[Line + 1].m_Time : m_vLines[Line].m_Time + 4.0;
			const float Length = (float)std::clamp(Next - m_vLines[Line].m_Time, 0.5, 8.0);
			std::vector<std::string> vWords;
			const std::string &Text = m_vLines[Line].m_Text;
			size_t Start = 0;
			while(Start < Text.size())
			{
				size_t End = Text.find(' ', Start);
				if(End == std::string::npos)
					End = Text.size();
				if(End > Start)
					vWords.push_back(Text.substr(Start, End - Start));
				Start = End + 1;
			}
			// the words of the line before go away quickly
			for(SWord &Word : m_vWords)
				Word.m_Age = std::max(Word.m_Age, Word.m_Delay + WORD_LIFE - 0.3f);
			// each word comes in its turn, as the line is sung; its place is found when it appears
			const float Size = WORD_SIZE * g_Config.m_XcLyricsSize / 100.0f;
			for(size_t i = 0; i < vWords.size(); i++)
			{
				const float Width = TextRender()->TextWidth(Size, vWords[i].c_str());
				m_vWords.push_back({vWords[i], vec2(0.0f, 0.0f), false, Width, 0.0f, Length * 0.8f * i / std::max<size_t>(vWords.size(), 1), random_float(-0.04f, 0.04f)});
			}
		}
	}
	// the words age, appear on their places, go away
	for(SWord &Word : m_vWords)
	{
		Word.m_Age += Dt;
		if(!Word.m_Placed && Word.m_Age >= Word.m_Delay)
			PlaceWord(Word);
	}
	m_vWords.erase(std::remove_if(m_vWords.begin(), m_vWords.end(), [](const SWord &Word) { return Word.m_Age - Word.m_Delay > WORD_LIFE; }), m_vWords.end());
	if(g_Config.m_XcLyricsShow == SHOW_PLATE)
		m_vWords.clear();

	m_PlateLine = g_Config.m_XcLyricsShow == SHOW_PLATE || g_Config.m_XcLyricsShow == SHOW_BOTH ? Line : -2;
	m_PlatePosition = Position;
	// a line the HUD fonts cannot write: the game font, drawn here
	if(!m_vWords.empty())
		RenderWordsClassic();
	if(m_PlateLine >= 0 && !OwnFontCovers(m_vLines[m_PlateLine].m_Text.c_str()))
		RenderPlateClassic(m_PlateLine, Position);
}

bool CXcLyrics::OwnFontCovers(const char *pText)
{
	const char *pStr = pText;
	while(int Code = str_utf8_decode(&pStr))
	{
		if(Code < 0)
			return false;
		const bool Covered = (Code >= 0x20 && Code <= 0x24f) || (Code >= 0x370 && Code <= 0x3ff) || (Code >= 0x400 && Code <= 0x52f) || (Code >= 0x2000 && Code <= 0x206f) || Code == 0x2248;
		if(!Covered)
			return false;
	}
	return true;
}

// a place around the tee where the word stays: above and to the sides, as far as it can be from the words
// still shown, so they do not cover each other
void CXcLyrics::PlaceWord(SWord &Word)
{
	Word.m_Placed = true;
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	const bool HasTee = Local >= 0 && GameClient()->m_Snap.m_aCharacters[Local].m_Active;
	const vec2 Tee = HasTee ? GameClient()->m_aClients[Local].m_RenderPos : GameClient()->m_Camera.m_Center;
	const float Scale = g_Config.m_XcLyricsSize / 100.0f;
	float BestScore = -1.0f;
	for(int Try = 0; Try < 14; Try++)
	{
		// mostly over the tee, sometimes beside it, never under the feet
		const float Angle = random_float(-pi * 0.92f, -pi * 0.08f);
		const float Radius = random_float(90.0f, 165.0f) * Scale;
		const vec2 Pos = Tee + vec2(std::cos(Angle) * Radius * 1.5f, std::sin(Angle) * Radius);
		float Score = 1e9f;
		for(const SWord &Other : m_vWords)
		{
			if(!Other.m_Placed || &Other == &Word)
				continue;
			// the words are wide: the room across counts by their widths
			const float dx = std::max(std::abs(Pos.x - Other.m_Pos.x) - (Word.m_Width + Other.m_Width) * 0.5f, 0.0f);
			const float dy = std::max(std::abs(Pos.y - Other.m_Pos.y) - WORD_SIZE * Scale, 0.0f);
			Score = std::min(Score, dx + dy * 2.0f);
		}
		if(Score > BestScore)
		{
			BestScore = Score;
			Word.m_Pos = Pos;
		}
	}
}

// the plate and the words with the HUD fonts: the words glow in the accent color and stay where they appeared
void CXcLyrics::RenderHud(ImDrawList *pDraw, ImFont *pBold, ImFont *pSmall, float Alpha)
{
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const ColorRGBA Accent = EffectColor(GameClient(), 0, 0, -1);
	const auto Rgba = [](ColorRGBA c, float a) { return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, c.a * a)); };

	// the words: the world turned into the screen
	if(!m_vWords.empty() && Alpha > 0.0f)
	{
		const vec2 Camera = GameClient()->m_Camera.m_Center;
		const CScreenRect World = Graphics()->MapScreenToWorld(Camera.x, Camera.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
		const float PerUnit = Display.y / std::max(World.m_BottomRight.y - World.m_TopLeft.y, 1.0f);
		const auto ToScreen = [&](vec2 Pos) { return ImVec2((Pos.x - World.m_TopLeft.x) / (World.m_BottomRight.x - World.m_TopLeft.x) * Display.x, (Pos.y - World.m_TopLeft.y) / (World.m_BottomRight.y - World.m_TopLeft.y) * Display.y); };
		const float Size = WORD_SIZE * g_Config.m_XcLyricsSize / 100.0f * PerUnit;
		for(const SWord &Word : m_vWords)
		{
			const float t = Word.m_Age - Word.m_Delay;
			if(!Word.m_Placed || t < 0.0f || !OwnFontCovers(Word.m_Text.c_str()))
				continue;
			const float Life = t / WORD_LIFE;
			// pops in from small with a little overshoot, floats up a bit, fades at the end
			const float In = std::min(t / 0.22f, 1.0f);
			const float Pop = In < 1.0f ? 0.6f + 0.55f * std::sin(In * pi * 0.5f) - 0.15f * In : 1.0f;
			const float WordAlpha = Alpha * std::min(t / 0.12f, 1.0f) * (1.0f - std::max(Life - 0.65f, 0.0f) / 0.35f);
			const float TextSize = Size * Pop;
			const ImVec2 Dim = pBold->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, Word.m_Text.c_str());
			const ImVec2 Center = ToScreen(Word.m_Pos + vec2(0.0f, -14.0f * Life));
			const ImVec2 Pos(Center.x - Dim.x * 0.5f, Center.y - Dim.y * 0.5f);
			ColorHSLA Hsl = color_cast<ColorHSLA>(Accent);
			Hsl.h = std::fmod(Hsl.h + Word.m_Hue + 1.0f, 1.0f);
			const ColorRGBA Color = color_cast<ColorRGBA>(Hsl);
			// a soft shadow, a glow around the letters in the accent color, the letters light in the middle
			pDraw->AddText(pBold, TextSize, ImVec2(Pos.x, Pos.y + TextSize * 0.06f), Rgba(ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f), 0.45f * WordAlpha), Word.m_Text.c_str());
			for(int i = 0; i < 8; i++)
			{
				const float Angle = i * pi / 4.0f;
				const float r = TextSize * 0.07f;
				pDraw->AddText(pBold, TextSize, ImVec2(Pos.x + std::cos(Angle) * r, Pos.y + std::sin(Angle) * r), Rgba(Color, 0.22f * WordAlpha), Word.m_Text.c_str());
			}
			const ColorRGBA Light(mix(Color.r, 1.0f, 0.7f), mix(Color.g, 1.0f, 0.7f), mix(Color.b, 1.0f, 0.7f), 1.0f);
			pDraw->AddText(pBold, TextSize, Pos, Rgba(Light, WordAlpha), Word.m_Text.c_str());
		}
	}

	// the plate: the line before, the line sung now (filled with the accent as it goes), the next line
	const int Line = m_PlateLine;
	const float Fade = m_Fade * Alpha;
	if(Line < 0 || Line >= (int)m_vLines.size() || Fade <= 0.0f || !OwnFontCovers(m_vLines[Line].m_Text.c_str()))
		return;
	const STheme &t = Theme();
	const float k = Display.y / 1080.0f * g_Config.m_XcLyricsSize / 100.0f;
	const float Size = 30.0f * k, SmallSize = 20.0f * k;
	const float y = Display.y * g_Config.m_XcLyricsY / 100.0f;
	const char *pNow = m_vLines[Line].m_Text.c_str();
	const char *pBefore = Line >= 1 && OwnFontCovers(m_vLines[Line - 1].m_Text.c_str()) ? m_vLines[Line - 1].m_Text.c_str() : "";
	const char *pAfter = Line + 1 < (int)m_vLines.size() && OwnFontCovers(m_vLines[Line + 1].m_Text.c_str()) ? m_vLines[Line + 1].m_Text.c_str() : "";
	const auto Width = [&](ImFont *pFont, float TextSize, const char *pText) { return pFont->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pText).x; };
	const float NowWidth = Width(pBold, Size, pNow);
	const float PlateWidth = std::max({NowWidth, Width(pSmall, SmallSize, pBefore), Width(pSmall, SmallSize, pAfter), 220.0f * k}) + 56.0f * k;
	const float PlateHeight = Size + SmallSize * 2.0f + 40.0f * k;
	const ImVec2 Min((Display.x - PlateWidth) * 0.5f, y - PlateHeight * 0.5f);
	const ImVec2 Max(Min.x + PlateWidth, Min.y + PlateHeight);
	const float Rounding = 16.0f * k;
	for(int i = 3; i >= 1; i--)
	{
		const float Grow = i * 2.5f * k;
		pDraw->AddRectFilled(ImVec2(Min.x - Grow, Min.y - Grow + 3.0f * k), ImVec2(Max.x + Grow, Max.y + Grow + 3.0f * k), Col(ImVec4(0, 0, 0, 1), 0.07f * Fade), Rounding + Grow);
	}
	pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.8f), Fade), Rounding);
	pDraw->AddRect(Min, Max, Col(t.m_Border, Fade), Rounding, 0, 1.0f);
	if(m_Approx)
		pDraw->AddText(pSmall, SmallSize, ImVec2(Max.x - 24.0f * k, Min.y + 6.0f * k), Col(t.m_TextMuted, Fade), "\xe2\x89\x88");

	const float Top = Min.y + 14.0f * k;
	const auto Centered = [&](ImFont *pFont, float TextSize, float Ty, ImU32 Color, const char *pText) {
		if(pText[0])
			pDraw->AddText(pFont, TextSize, ImVec2((Display.x - Width(pFont, TextSize, pText)) * 0.5f, Ty), Color, pText);
	};
	Centered(pSmall, SmallSize, Top, Col(t.m_TextMuted, Fade), pBefore);
	Centered(pSmall, SmallSize, Top + SmallSize + Size + 14.0f * k, Col(t.m_TextMuted, Fade), pAfter);
	const float NowY = Top + SmallSize + 7.0f * k;
	Centered(pBold, Size, NowY, Col(t.m_Text, Fade), pNow);
	// the part already sung once more in the accent color, cut at the progress
	const double Start = m_vLines[Line].m_Time;
	const double End = Line + 1 < (int)m_vLines.size() ? m_vLines[Line + 1].m_Time : Start + 4.0;
	const float Progress = (float)std::clamp((m_PlatePosition - Start) / std::max(End - Start, 0.1), 0.0, 1.0);
	const float NowX = (Display.x - NowWidth) * 0.5f;
	pDraw->PushClipRect(ImVec2(NowX, NowY - Size * 0.2f), ImVec2(NowX + NowWidth * Progress, NowY + Size * 1.4f), true);
	pDraw->AddText(pBold, Size, ImVec2(NowX, NowY), Rgba(Accent, Fade), pNow);
	pDraw->PopClipRect();
}

// three lines at the bottom: the one before (dim), the one sung now (bright, filled as it goes), the next one (dim)
void CXcLyrics::RenderPlateClassic(int Line, double Position)
{
	if(m_Fade <= 0.0f)
		return;
	const float Width = (float)Graphics()->ScreenWidth(), Height = (float)Graphics()->ScreenHeight();
	Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
	const float k = Height / 1080.0f * g_Config.m_XcLyricsSize / 100.0f;
	const float Size = 30.0f * k, SmallSize = 21.0f * k;
	const float y = Height * g_Config.m_XcLyricsY / 100.0f;
	const ColorRGBA Accent = EffectColor(GameClient(), 0, 0, -1);
	const char *pNow = Line >= 0 ? m_vLines[Line].m_Text.c_str() : "";
	const char *pBefore = Line >= 1 ? m_vLines[Line - 1].m_Text.c_str() : "";
	const char *pAfter = Line + 1 < (int)m_vLines.size() ? m_vLines[Line + 1].m_Text.c_str() : "";
	const float NowWidth = TextRender()->TextWidth(Size, pNow);
	const float PlateWidth = std::max({NowWidth, TextRender()->TextWidth(SmallSize, pBefore), TextRender()->TextWidth(SmallSize, pAfter), 200.0f * k}) + 48.0f * k;
	const float PlateHeight = Size + SmallSize * 2.0f + 36.0f * k;
	const float x0 = (Width - PlateWidth) * 0.5f;
	Graphics()->DrawRect(x0, y - PlateHeight * 0.5f, PlateWidth, PlateHeight, ColorRGBA(0.05f, 0.06f, 0.08f, 0.55f * m_Fade), IGraphics::CORNER_ALL, 16.0f * k);
	// lyrics without times, spread over the track: a mark that the timing is a guess
	if(m_Approx)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f * m_Fade));
		TextRender()->Text(x0 + PlateWidth - 26.0f * k, y - PlateHeight * 0.5f + 6.0f * k, SmallSize, "\xe2\x89\x88");
	}

	const auto Centered = [&](const char *pText, float TextSize, float Ty, ColorRGBA Color) {
		if(!pText[0])
			return;
		TextRender()->TextColor(Color);
		TextRender()->Text((Width - TextRender()->TextWidth(TextSize, pText)) * 0.5f, Ty, TextSize, pText);
	};
	const float Top = y - PlateHeight * 0.5f + 12.0f * k;
	Centered(pBefore, SmallSize, Top, ColorRGBA(1.0f, 1.0f, 1.0f, 0.4f * m_Fade));
	Centered(pAfter, SmallSize, Top + SmallSize + Size + 12.0f * k, ColorRGBA(1.0f, 1.0f, 1.0f, 0.4f * m_Fade));
	// the line sung now: white, then the part already sung once more in the accent color, cut at the progress
	if(pNow[0])
	{
		const float NowY = Top + SmallSize + 6.0f * k;
		Centered(pNow, Size, NowY, ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f * m_Fade));
		const double Start = m_vLines[Line].m_Time;
		const double End = Line + 1 < (int)m_vLines.size() ? m_vLines[Line + 1].m_Time : Start + 4.0;
		const float Progress = (float)std::clamp((Position - Start) / std::max(End - Start, 0.1), 0.0, 1.0);
		const float NowX = (Width - NowWidth) * 0.5f;
		Graphics()->ClipEnable((int)NowX, (int)NowY, (int)(NowWidth * Progress) + 1, (int)(Size * 1.4f));
		TextRender()->TextColor(Accent.WithAlpha(m_Fade));
		TextRender()->Text(NowX, NowY, Size, pNow);
		Graphics()->ClipDisable();
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

// the words of the line fly up from next to your tee and fade
void CXcLyrics::RenderWordsClassic()
{
	if(m_vWords.empty())
		return;

	const vec2 Camera = GameClient()->m_Camera.m_Center;
	const CScreenRect Screen = Graphics()->MapScreenToWorld(Camera.x, Camera.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	Graphics()->MapScreen(Screen);
	const float Size = WORD_SIZE * g_Config.m_XcLyricsSize / 100.0f;
	for(const SWord &Word : m_vWords)
	{
		const float t = Word.m_Age - Word.m_Delay;
		if(t < 0.0f || !Word.m_Placed || OwnFontCovers(Word.m_Text.c_str()))
			continue;
		const float Life = t / WORD_LIFE;
		// pops in a little bigger, rises, fades at the end
		const float Pop = t < 0.15f ? 1.0f + (0.15f - t) * 2.0f : 1.0f;
		const float Alpha = std::min(t / 0.12f, 1.0f) * (1.0f - std::max(Life - 0.6f, 0.0f) / 0.4f);
		const float TextSize = Size * Pop;
		const vec2 Pos = Word.m_Pos + vec2(0.0f, -14.0f * Life);
		const float TextWidth = TextRender()->TextWidth(TextSize, Word.m_Text.c_str());
		const ColorRGBA Color = EffectColor(GameClient(), 0, 0, -1, Word.m_Hue);
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * Alpha));
		TextRender()->TextColor(ColorRGBA(mix(Color.r, 1.0f, 0.5f), mix(Color.g, 1.0f, 0.5f), mix(Color.b, 1.0f, 0.5f), Alpha));
		TextRender()->Text(Pos.x - TextWidth * 0.5f, Pos.y - TextSize * 0.5f, TextSize, Word.m_Text.c_str());
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
}

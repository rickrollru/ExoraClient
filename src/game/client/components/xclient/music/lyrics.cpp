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
#include <game/client/components/xclient/fx_renderer.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/gameclient.h>

#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/xc_render.h>

#include <game/client/components/xclient/ui/imgui_backend.h>

#include <algorithm>
#include <functional>
#include <cmath>

using namespace XcHelpers;
using namespace XcUi;

static float WordLife() { return g_Config.m_XcLyricsTime / 10.0f; }

// the words on the map: their size in world units; how long one stays comes from xc_lyrics_time
static constexpr float WORD_SIZE = 26.0f;
// a word of the line before goes away this fast when the next line starts
static constexpr float WORD_OUT_TIME = 0.35f;

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

const std::vector<CXcLyrics::SLine> *CXcLyrics::FoundLines(const std::string &Key, bool &Approx) const
{
	if(Key != m_Key || m_State != STATE_FOUND || m_vLines.empty())
		return nullptr;
	Approx = m_Approx;
	return &m_vLines;
}

void CXcLyrics::AddRecorded(const std::string &Key, std::vector<SLine> vLines, bool Approx)
{
	if(vLines.empty())
		return;
	auto Cached = m_Cache.find(Key);
	if(Cached != m_Cache.end() && !Cached->second.m_vLines.empty())
		return;
	m_Cache[Key] = SCached{vLines, Approx, Client()->GlobalTime()};
	// the track is already on: the recorded words instead of the search
	if(Key == m_Key && m_State != STATE_FOUND)
	{
		if(m_pRequest)
			m_pRequest->Abort();
		m_pRequest = nullptr;
		m_vLines = std::move(vLines);
		m_Approx = Approx;
		m_State = STATE_FOUND;
		m_LastLine = -1;
	}
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
	const std::string Key = TrackKey(State.m_Artist, State.m_Title);
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
				if(Word.m_Out < 0.0f)
					Word.m_Out = 0.0f;
			// each word comes in its turn, as the line is sung; its place is found when it appears
			const float Size = WORD_SIZE * g_Config.m_XcLyricsSize / 100.0f;
			for(size_t i = 0; i < vWords.size(); i++)
			{
				const float Width = TextRender()->TextWidth(Size, vWords[i].c_str());
				m_vWords.push_back({vWords[i], vec2(0.0f, 0.0f), false, Width, 0.0f, Length * 0.8f * i / std::max<size_t>(vWords.size(), 1), random_float(-0.04f, 0.04f), -1.0f});
			}
		}
	}
	// the words age, appear on their places, go away
	for(SWord &Word : m_vWords)
	{
		Word.m_Age += Dt;
		if(Word.m_Out >= 0.0f)
			Word.m_Out += Dt;
		if(!Word.m_Placed && Word.m_Age >= Word.m_Delay)
			PlaceWord(Word);
	}
	m_vWords.erase(std::remove_if(m_vWords.begin(), m_vWords.end(), [](const SWord &Word) { return Word.m_Age - Word.m_Delay > WordLife() || Word.m_Out > WORD_OUT_TIME || (Word.m_Out >= 0.0f && Word.m_Age < Word.m_Delay); }), m_vWords.end());
	if(g_Config.m_XcLyricsShow == SHOW_PLATE)
		m_vWords.clear();

	m_PlateLine = g_Config.m_XcLyricsShow == SHOW_PLATE || g_Config.m_XcLyricsShow == SHOW_BOTH ? Line : -2;
	m_PlatePosition = Position;
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
		const float Radius = random_float(90.0f, 165.0f) * Scale * g_Config.m_XcLyricsSpread / 100.0f;
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

bool CXcLyrics::WordLook(const SWord &Word, float &Alpha, float &Pop, vec2 &Pos) const
{
	const float t = Word.m_Age - Word.m_Delay;
	if(!Word.m_Placed || t < 0.0f)
		return false;
	const auto Smooth = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
	// grows in with a soft overshoot (ease out back), all smooth in time, no jumps
	const float In = std::clamp(t / 0.35f, 0.0f, 1.0f);
	const float x = In - 1.0f;
	const float c1 = 1.1f, c3 = c1 + 1.0f;
	Pop = 0.75f + 0.25f * (1.0f + c3 * x * x * x + c1 * x * x);
	// fades in, fades out at the end of its life or quickly when the next line sends it away
	const float Life = WordLife();
	Alpha = Smooth(t / 0.25f) * (1.0f - Smooth((t - Life * 0.7f) / (Life * 0.3f)));
	if(Word.m_Out >= 0.0f)
		Alpha *= 1.0f - Smooth(Word.m_Out / WORD_OUT_TIME);
	// floats up a little, slower and slower
	Pos = Word.m_Pos + vec2(0.0f, -16.0f * (1.0f - std::exp(-t * 0.9f)));
	return Alpha > 0.001f;
}

ColorRGBA CXcLyrics::WordColor(const SWord &Word) const
{
	ColorHSLA Hsl = color_cast<ColorHSLA>(EffectColor(GameClient(), 0, 0, -1));
	// rainbow: each word its own hue, a calm one (not full saturation)
	if(g_Config.m_XcLyricsColor == 2)
		Hsl = ColorHSLA((Word.m_Hue + 0.04f) / 0.08f, 0.75f, 0.62f, 1.0f);
	else
		Hsl.h = std::fmod(Hsl.h + Word.m_Hue + 1.0f, 1.0f);
	return color_cast<ColorRGBA>(Hsl);
}

// the plate and the words with the HUD fonts: clean letters over the soft light, staying where they appeared
// text at a position between pixels: ImGui puts text on whole pixels, so a word gliding slowly would step from
// pixel to pixel; the letters are drawn on the pixel and then moved by the rest
static void AddTextSmooth(ImDrawList &List, ImFont *pFont, float Size, ImVec2 Pos, ImU32 Color, const char *pText)
{
	const ImVec2 Base(std::floor(Pos.x), std::floor(Pos.y));
	const int First = List.VtxBuffer.Size;
	List.AddText(pFont, Size, Base, Color, pText);
	const float dx = Pos.x - Base.x, dy = Pos.y - Base.y;
	for(int i = First; i < List.VtxBuffer.Size; i++)
	{
		List.VtxBuffer[i].pos.x += dx;
		List.VtxBuffer[i].pos.y += dy;
	}
}

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
			float WordAlpha, Pop;
			vec2 WorldPos;
			// drawn as neon by the render module (RenderNeon)
			if(NeonActive() || !OwnFontCovers(Word.m_Text.c_str()) || !WordLook(Word, WordAlpha, Pop, WorldPos))
				continue;
			WordAlpha *= Alpha;
			const float TextSize = Size * Pop;
			const char *pText = Word.m_Text.c_str();
			const ImVec2 Dim = pBold->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pText);
			// not snapped to pixels: the words move smoothly with the camera and while they float up
			const ImVec2 Center = ToScreen(WorldPos);
			const ImVec2 Pos(Center.x - Dim.x * 0.5f, Center.y - Dim.y * 0.5f);
			const ColorRGBA Color = WordColor(Word);
			// the glow: the letters again in rings around them, many faint copies that add up to a soft light
			// close to the letters, fading out further away
			const float Glow = g_Config.m_XcLyricsGlow / 100.0f;
			if(Glow > 0.0f)
			{
				const float Reach = TextSize * 0.28f * g_Config.m_XcLyricsGlowSize / 100.0f;
				// the glow is the color itself, a bit deeper: around white letters it also keeps them readable on
				// bright maps
				const ColorRGBA GlowColor(Color.r * 0.85f, Color.g * 0.85f, Color.b * 0.85f, 1.0f);
				static constexpr int RINGS = 5, STEPS = 12;
				for(int Ring = RINGS; Ring >= 1; Ring--)
				{
					const float k = (Ring - 0.5f) / RINGS;
					const float r = Reach * k;
					// dense close to the letters, falling off softly outwards
					const float a = Glow * 0.22f * std::exp(-3.2f * k * k) * WordAlpha;
					const ImU32 Col32 = Rgba(GlowColor, a);
					for(int i = 0; i < STEPS; i++)
					{
						const float Angle = (i + Ring * 0.5f) * 2.0f * pi / STEPS;
						AddTextSmooth(*pDraw, pBold, TextSize, ImVec2(Pos.x + std::cos(Angle) * r, Pos.y + std::sin(Angle) * r), Col32, pText);
					}
				}
			}
			// the letters: white with a touch of the color, or the color itself
			const float White = g_Config.m_XcLyricsColor == 0 ? 0.85f : 0.25f;
			const ColorRGBA Letters(mix(Color.r, 1.0f, White), mix(Color.g, 1.0f, White), mix(Color.b, 1.0f, White), 1.0f);
			AddTextSmooth(*pDraw, pBold, TextSize, Pos, Rgba(Letters, WordAlpha), pText);
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
	// no dark shadow under the plate (the in-game interface has none)
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
	m_NowRect[0] = (Display.x - Width(pBold, Size, pNow)) * 0.5f;
	m_NowRect[1] = NowY;
	m_NowRect[2] = Size;
	m_NowFade = Fade;
	if(NeonActive())
		return; // the line sung now: neon, drawn by RenderNeon
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
		float Alpha, Pop;
		vec2 Pos;
		if(OwnFontCovers(Word.m_Text.c_str()) || !WordLook(Word, Alpha, Pop, Pos))
			continue;
		const float TextSize = Size * Pop;
		const float TextWidth = TextRender()->TextWidth(TextSize, Word.m_Text.c_str());
		const ColorRGBA Color = WordColor(Word);
		// with the render module these get the same neon as the words in the HUD fonts
		const bool Neon = NeonActive();
		float Reach = 0.0f;
		if(Neon)
		{
			const float PerUnit = Graphics()->ScreenHeight() / std::max(Screen.m_BottomRight.y - Screen.m_TopLeft.y, 1.0f);
			Reach = TextSize * PerUnit * 0.45f * g_Config.m_XcLyricsGlowSize / 100.0f + 4.0f; // pixels
			const float Room = Reach * 3.0f / PerUnit;
			const float aRect[4] = {Pos.x - TextWidth * 0.5f - Room, Pos.y - TextSize * 0.5f - Room, Pos.x + TextWidth * 0.5f + Room, Pos.y + TextSize * 0.5f + Room};
			Graphics()->XcCaptureBegin(aRect);
		}
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, Neon ? 0.0f : 0.5f * Alpha));
		TextRender()->TextColor(ColorRGBA(mix(Color.r, 1.0f, 0.5f), mix(Color.g, 1.0f, 0.5f), mix(Color.b, 1.0f, 0.5f), Alpha));
		TextRender()->Text(Pos.x - TextWidth * 0.5f, Pos.y - TextSize * 0.5f, TextSize, Word.m_Text.c_str());
		if(Neon)
		{
			XcRender::SGlowSettings Settings;
			Settings.m_Color = Color.WithAlpha(Alpha);
			Settings.m_RimColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f);
			Settings.m_Radius = Reach;
			Settings.m_Strength = 2.6f * g_Config.m_XcLyricsGlow / 100.0f;
			Settings.m_RimWidth = 0.0f;
			Settings.m_DoGlow = true;
			Graphics()->XcCaptureEnd(Settings);
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
}

bool CXcLyrics::NeonActive() const
{
	return GameClient()->m_FxRenderer.ModuleActive() && g_Config.m_XcLyricsGlow > 0;
}

// one piece of neon: the letters go into a capture, the render module blurs their shape into the glow around them,
// draws a light rim at their edge and the letters on top (the same as the neon outline of the tees)
void CXcLyrics::RenderNeon(CImGuiBackend &Backend, ImFont *pBold, float Alpha)
{
	if(!NeonActive() || Alpha <= 0.0f || !pBold)
		return;
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const ImTextureID FontTexture = ImGui::GetIO().Fonts->TexID;
	const auto Rgba = [](ColorRGBA c, float a) { return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, c.a * a)); };
	const float GlowAmount = g_Config.m_XcLyricsGlow / 100.0f;
	const float GlowSize = g_Config.m_XcLyricsGlowSize / 100.0f;

	// a piece of text at a pixel rectangle: captured, glowing in Color
	const auto Neon = [&](float x0, float y0, float x1, float y1, float TextSize, ColorRGBA Color, float PieceAlpha, const std::function<void(ImDrawList &)> &fnDraw) {
		const float Reach = TextSize * 0.45f * GlowSize + 4.0f;
		Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, Display.x, Display.y));
		// room for the whole glow, it fades out softly before the edge
		const float aRect[4] = {x0 - Reach * 3.0f, y0 - Reach * 3.0f, x1 + Reach * 3.0f, y1 + Reach * 3.0f};
		Graphics()->XcCaptureBegin(aRect);
		ImDrawList List(ImGui::GetDrawListSharedData());
		List._ResetForNewFrame();
		List.PushClipRectFullScreen();
		List.PushTextureID(FontTexture);
		fnDraw(List);
		Backend.RenderDrawList(&List, Display.x, Display.y);
		XcRender::SGlowSettings Settings;
		Settings.m_Color = Color.WithAlpha(PieceAlpha);
		Settings.m_RimColor = ColorRGBA(mix(Color.r, 1.0f, 0.35f), mix(Color.g, 1.0f, 0.35f), mix(Color.b, 1.0f, 0.35f), 0.9f * PieceAlpha);
		Settings.m_Radius = Reach;
		Settings.m_Strength = 2.6f * GlowAmount;
		Settings.m_RimWidth = 0.0f; // no rim: it made a hollow ring around each letter
		Settings.m_DoGlow = true;
		Graphics()->XcCaptureEnd(Settings);
	};

	// the words on the map
	if(!m_vWords.empty())
	{
		const vec2 Camera = GameClient()->m_Camera.m_Center;
		const CScreenRect World = Graphics()->MapScreenToWorld(Camera.x, Camera.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
		const float PerUnit = Display.y / std::max(World.m_BottomRight.y - World.m_TopLeft.y, 1.0f);
		const auto ToScreen = [&](vec2 Pos) { return ImVec2((Pos.x - World.m_TopLeft.x) / (World.m_BottomRight.x - World.m_TopLeft.x) * Display.x, (Pos.y - World.m_TopLeft.y) / (World.m_BottomRight.y - World.m_TopLeft.y) * Display.y); };
		const float Size = WORD_SIZE * g_Config.m_XcLyricsSize / 100.0f * PerUnit;
		for(const SWord &Word : m_vWords)
		{
			float WordAlpha, Pop;
			vec2 WorldPos;
			if(!OwnFontCovers(Word.m_Text.c_str()) || !WordLook(Word, WordAlpha, Pop, WorldPos))
				continue;
			WordAlpha *= Alpha;
			const float TextSize = Size * Pop;
			const char *pText = Word.m_Text.c_str();
			const ImVec2 Dim = pBold->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pText);
			const ImVec2 Center = ToScreen(WorldPos);
			const ImVec2 Pos(Center.x - Dim.x * 0.5f, Center.y - Dim.y * 0.5f);
			if(Pos.x > Display.x || Pos.y > Display.y || Pos.x + Dim.x < 0.0f || Pos.y + Dim.y < 0.0f)
				continue;
			const ColorRGBA Color = WordColor(Word);
			const float White = g_Config.m_XcLyricsColor == 0 ? 0.85f : 0.25f;
			const ColorRGBA Letters(mix(Color.r, 1.0f, White), mix(Color.g, 1.0f, White), mix(Color.b, 1.0f, White), 1.0f);
			Neon(Pos.x, Pos.y, Pos.x + Dim.x, Pos.y + Dim.y, TextSize, Color, WordAlpha, [&](ImDrawList &List) {
				AddTextSmooth(List, pBold, TextSize, Pos, Rgba(Letters, WordAlpha), pText);
			});
		}
	}

	// the line sung now on the plate: white, the sung part in the accent, all of it glowing in the accent
	const int Line = m_PlateLine;
	if(Line >= 0 && Line < (int)m_vLines.size() && m_NowFade > 0.0f && OwnFontCovers(m_vLines[Line].m_Text.c_str()))
	{
		const char *pNow = m_vLines[Line].m_Text.c_str();
		const float Size = m_NowRect[2];
		const ImVec2 Dim = pBold->CalcTextSizeA(Size, FLT_MAX, 0.0f, pNow);
		const ImVec2 Pos(m_NowRect[0], m_NowRect[1]);
		const ColorRGBA Accent = EffectColor(GameClient(), 0, 0, -1);
		const double Start = m_vLines[Line].m_Time;
		const double End = Line + 1 < (int)m_vLines.size() ? m_vLines[Line + 1].m_Time : Start + 4.0;
		const float Progress = (float)std::clamp((m_PlatePosition - Start) / std::max(End - Start, 0.1), 0.0, 1.0);
		const float Fade = m_NowFade;
		Neon(Pos.x, Pos.y, Pos.x + Dim.x, Pos.y + Dim.y, Size, Accent, Fade * 0.8f, [&](ImDrawList &List) {
			AddTextSmooth(List, pBold, Size, Pos, Rgba(ColorRGBA(0.95f, 0.95f, 0.97f, 1.0f), Fade), pNow);
			List.PushClipRect(ImVec2(Pos.x, Pos.y - Size * 0.2f), ImVec2(Pos.x + Dim.x * Progress, Pos.y + Size * 1.4f), true);
			AddTextSmooth(List, pBold, Size, Pos, Rgba(Accent, Fade), pNow);
			List.PopClipRect();
		});
	}
}

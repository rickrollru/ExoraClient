#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_MUSIC_LYRICS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_MUSIC_LYRICS_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

class IHttpRequest;
struct ImDrawList;
struct ImFont;

// The words of the song that plays in the music player, in time with it. The timed lyrics (LRC) come from the
// open LRCLIB database by the title and the artist the player tells; the line that is sung now is shown in a plate
// at the bottom of the screen, and its words pop up one by one around your tee and stay on their places on the map.
// Both are drawn with the HUD fonts (RenderHud); lines in other scripts take the game font.
class CXcLyrics : public CComponent
{
public:
	enum
	{
		SHOW_PLATE = 0,
		SHOW_MAP,
		SHOW_BOTH,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnShutdown() override;
	// the plate and the words in the new HUD style; called by the HUD inside its ImGui frame
	bool WantsHud() const { return m_Fade > 0.0f || !m_vWords.empty(); }
	void RenderHud(ImDrawList *pDraw, ImFont *pBold, ImFont *pSmall, float Alpha);
	// with the render module: the words and the line sung now as neon (captured, blurred by a shader into a glow,
	// a light rim around the letters), drawn after the HUD frame
	void RenderNeon(class CImGuiBackend &Backend, ImFont *pBold, float Alpha);
	bool NeonActive() const;
	// the HUD fonts have Latin and Cyrillic only; other letters (Japanese, Korean...) take the game font
	static bool OwnFontCovers(const char *pText);

	struct SLine
	{
		double m_Time; // seconds into the track
		std::string m_Text;
	};
	// "[mm:ss.xx]text" lines, several times in front of one text are allowed; sorted by time
	static std::vector<SLine> ParseLrc(const char *pLrc);
	// "Artist - Title (Official Video) [4K]" from a browser tab: the artist and the bare title
	static void CleanTitle(const std::string &Artist, const std::string &Title, std::string &OutArtist, std::string &OutTitle);
	// the lyrics found for the track with this key (artist and title), nullptr while there are none; for demos
	const std::vector<SLine> *FoundLines(const std::string &Key, bool &Approx) const;
	// lyrics recorded in a demo: taken instead of a search
	void AddRecorded(const std::string &Key, std::vector<SLine> vLines, bool Approx);
	// the lines of the track that plays now, nullptr while there are none; Approx: spread over the track, not timed
	const std::vector<SLine> *CurrentLines(bool *pApprox = nullptr) const
	{
		// the lines of the track before stay until the new one is searched: they are not its lines
		if(m_State != STATE_FOUND || m_vLines.empty() || m_Key != m_SeenKey)
			return nullptr;
		if(pApprox)
			*pApprox = m_Approx;
		return &m_vLines;
	}
	static std::string TrackKey(const std::string &Artist, const std::string &Title) { return Artist + "\x1f" + Title; }

private:
	enum
	{
		STATE_NONE = 0,
		STATE_LOADING,
		STATE_FOUND,
		STATE_NOT_FOUND,
	};
	// the tries one after another, until one gives timed lyrics
	enum
	{
		STEP_LRCLIB_GET = 0, // the exact match with the length of the track
		STEP_LRCLIB_GET_ANY, // the exact match, any length
		STEP_LRCLIB_FIELDS, // a search by the title and the artist
		STEP_LRCLIB_WORDS, // a search by the words "artist title"
		STEP_LRCLIB_TITLE, // a search by the title only
		STEP_NETEASE_SEARCH, // NetEase Music: find the song
		STEP_NETEASE_LYRIC, // NetEase Music: its lyrics
		NUM_STEPS,
	};
	std::string m_Key; // the track the lyrics are for
	std::string m_SeenKey; // the track the player tells now; taken when it stays a second (switching tells others)
	float m_SeenTime = 0.0f;
	int m_State = STATE_NONE;
	int m_Step = 0;
	long long m_NeteaseId = 0;
	std::string m_PlainLyrics; // lyrics without times, when nothing better is found
	bool m_Approx = false; // the lines are spread over the track, not timed
	std::string m_Artist, m_Title;
	double m_Duration = -1.0;
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::vector<SLine> m_vLines;
	struct SCached
	{
		std::vector<SLine> m_vLines;
		bool m_Approx;
		float m_Time; // when it was looked for: a failed search is tried again after a while
	};
	std::map<std::string, SCached> m_Cache; // tracks played in this session
	int m_LastLine = -1;
	float m_Fade = 0.0f;

	// the words of the current line: each pops up around the tee in its turn and stays on its place in the world
	struct SWord
	{
		std::string m_Text;
		vec2 m_Pos; // in the world, taken when the word appears
		bool m_Placed;
		float m_Width; // in world units, to keep the words apart
		float m_Age;
		float m_Delay;
		float m_Hue; // a little shift of the accent color
		float m_Out; // time since the next line sent it away, -1 while it stays
	};
	std::vector<SWord> m_vWords;

	void StartSearch();
	void RunStep();
	void TakeAnswer();
	void Finish(bool Found);
	// the lines spread evenly over the track (lyrics without times)
	std::vector<SLine> SpreadPlain(const std::string &Text) const;
	int m_PlateLine = -1;
	float m_NowRect[3] = {0.0f, 0.0f, 0.0f}; // the line sung now on the plate: x, y, size in pixels
	float m_NowFade = 0.0f;
	double m_PlatePosition = 0.0;
	void PlaceWord(SWord &Word);
	// how a word looks now: false while it is hidden
	bool WordLook(const SWord &Word, float &Alpha, float &Pop, vec2 &Pos) const;
	ColorRGBA WordColor(const SWord &Word) const;
	// the game font, for lines the HUD fonts cannot write
	void RenderPlateClassic(int Line, double Position);
	void RenderWordsClassic();
};

#endif

#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_MUSIC_LYRICS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_MUSIC_LYRICS_H

#include <base/vmath.h>

#include <game/client/component.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

class IHttpRequest;

// The words of the song that plays in the music player, in time with it. The timed lyrics (LRC) come from the
// open LRCLIB database by the title and the artist the player tells; the line that is sung now is shown in a plate
// at the bottom of the screen, and its words pop up one by one next to your tee on the map.
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

	struct SLine
	{
		double m_Time; // seconds into the track
		std::string m_Text;
	};
	// "[mm:ss.xx]text" lines, several times in front of one text are allowed; sorted by time
	static std::vector<SLine> ParseLrc(const char *pLrc);
	// "Artist - Title (Official Video) [4K]" from a browser tab: the artist and the bare title
	static void CleanTitle(const std::string &Artist, const std::string &Title, std::string &OutArtist, std::string &OutTitle);

private:
	enum
	{
		STATE_NONE = 0,
		STATE_LOADING,
		STATE_FOUND,
		STATE_NOT_FOUND,
	};
	std::string m_Key; // the track the lyrics are for
	int m_State = STATE_NONE;
	bool m_Searching = false; // the second try: a search instead of the exact match
	std::string m_Artist, m_Title;
	double m_Duration = -1.0;
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::vector<SLine> m_vLines;
	std::map<std::string, std::vector<SLine>> m_Cache; // tracks played in this session
	int m_LastLine = -1;
	float m_Fade = 0.0f;

	// the words of the current line that fly up next to the tee
	struct SWord
	{
		std::string m_Text;
		vec2 m_Offset; // from the tee
		float m_Age;
		float m_Delay;
	};
	std::vector<SWord> m_vWords;

	void Request(bool Search);
	void TakeAnswer();
	void RenderPlate(int Line, double Position);
	void RenderWords(float Dt);
};

#endif

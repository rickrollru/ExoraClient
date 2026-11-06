#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_COSMETICS_SYNC_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_COSMETICS_SYNC_H

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

class IHttpRequest;

// Shared cosmetics: other Exora players see your cape, scarf, accessory, tee shader and neon outline, and you see theirs.
// Your set goes to the cosmetics service under a random id of this client (no nick is stored); every few seconds the
// client says on which DDNet server and under which player number it plays, and asks who wears what there.
// While a player is drawn, CScope puts his set into the settings the effects read, and puts yours back right after.
class CXcCosmeticsSync : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnShutdown() override;

	// someone on this server shows a set (the effects then run even when your own are off)
	bool AnyRemote() const { return m_NumRemote > 0; }
	bool HasRemote(int ClientId) const;
	// the player plays Exora Client (the service saw him on this server), for the icon next to his name
	bool IsExora(int ClientId) const;
	// what the service says, for the menu
	const char *Status() const;
	int NumSeen() const { return m_NumRemote; }
	// how many Exora clients are running now (in the game or the menu), -1 while unknown
	int Online() const { return m_Online; }

	// while it lives, the settings of the effects are the ones of that player (when he has a set)
	class CScope
	{
	public:
		CScope(CXcCosmeticsSync &Sync, int ClientId);
		~CScope();
		CScope(const CScope &) = delete;
		CScope &operator=(const CScope &) = delete;

	private:
		std::vector<long long> m_vSaved;
		bool m_Active = false;
	};

private:
	struct SRemote
	{
		std::string m_Name;
		std::string m_Hash;
	};
	using TBundle = std::map<std::string, long long>;

	std::string OwnBundle(std::string &Hash) const;
	void EnsureIdentity();
	void SendBeat(bool WithBundle);
	void AskServer();
	void FetchBundles();
	void Finish();
	bool ApplyTo(int ClientId, std::vector<long long> &vSaved);
	void Restore(const std::vector<long long> &vSaved);

	std::shared_ptr<IHttpRequest> m_pBeat;
	std::shared_ptr<IHttpRequest> m_pServer;
	std::shared_ptr<IHttpRequest> m_pStats;
	float m_NextStats = 0.0f;
	int m_Online = -1;
	void UpdateOnline();
	std::vector<std::pair<std::string, std::shared_ptr<IHttpRequest>>> m_vpBundles;
	bool m_BeatHadBundle = false;
	float m_NextBeat = 0.0f;
	float m_NextAsk = 0.0f;
	std::string m_SentHash;
	bool m_NeedBundle = true;
	std::string m_Address;
	SRemote m_aRemote[MAX_CLIENTS];
	int m_NumRemote = 0;
	std::map<std::string, TBundle> m_Bundles;
	std::map<std::string, float> m_FailedHashes;
	int m_Status = 0;
	int m_ScopeDepth = 0;
};

#endif

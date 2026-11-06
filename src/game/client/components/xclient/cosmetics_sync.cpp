#include "cosmetics_sync.h"

#include "xc_helpers.h"

#include <base/hash.h>
#include <base/secure.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/components/xclient/ui/theme.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
// a setting the effects read: shared ones go into the set, the others only change while a player with a set is drawn
struct SField
{
	const char *m_pName;
	void *m_pValue;
	bool m_Color;
	bool m_Shared;
	int m_Min = std::numeric_limits<int>::min(); // what a set may say: kept in the range of the setting
	int m_Max = std::numeric_limits<int>::max();
};

#define XC_INT(Name, Var) {Name, &g_Config.Var, false, true}
#define XC_INT_R(Name, Var, Min, Max) {Name, &g_Config.Var, false, true, Min, Max}
#define XC_COL(Name, Var) {Name, &g_Config.Var, true, true}
#define XC_LOCAL(Name, Var) {Name, &g_Config.Var, false, false}

const SField s_aFields[] = {
	// cape
	XC_INT("xc_cape", m_XcCape), XC_INT("xc_cape_look", m_XcCapeLook), XC_INT("xc_cape_sparks", m_XcCapeSparks), XC_INT("xc_cape_wave", m_XcCapeWave),
	XC_INT("xc_cape_shape", m_XcCapeShape), XC_INT("xc_cape_inner_mode", m_XcCapeInnerMode), XC_COL("xc_cape_inner_color", m_XcCapeInnerColor),
	XC_INT("xc_cape_trim", m_XcCapeTrim), XC_COL("xc_cape_trim_color", m_XcCapeTrimColor), XC_INT("xc_cape_pattern", m_XcCapePattern),
	XC_INT("xc_cape_react", m_XcCapeReact), XC_INT("xc_cape_color_mode", m_XcCapeColorMode), XC_COL("xc_cape_color", m_XcCapeColor),
	XC_INT("xc_cape_length", m_XcCapeLength), XC_INT("xc_cape_width", m_XcCapeWidth), XC_INT("xc_cape_wind", m_XcCapeWind), XC_INT("xc_cape_alpha", m_XcCapeAlpha),
	// scarf
	XC_INT("xc_scarf", m_XcScarf), XC_INT("xc_scarf_look", m_XcScarfLook), XC_INT("xc_scarf_color_mode", m_XcScarfColorMode), XC_COL("xc_scarf_color", m_XcScarfColor),
	XC_INT("xc_scarf_stripes", m_XcScarfStripes), XC_COL("xc_scarf_stripe_color", m_XcScarfStripeColor), XC_INT("xc_scarf_tails", m_XcScarfTails),
	XC_INT("xc_scarf_fringe", m_XcScarfFringe), XC_INT("xc_scarf_length", m_XcScarfLength), XC_INT("xc_scarf_wind", m_XcScarfWind),
	// accessory
	XC_INT("xc_accessory", m_XcAccessory), XC_INT("xc_accessory_sign", m_XcAccessorySign), XC_INT("xc_accessory_size", m_XcAccessorySize),
	XC_INT("xc_accessory_height", m_XcAccessoryHeight), XC_INT("xc_accessory_alpha", m_XcAccessoryAlpha), XC_COL("xc_accessory_color", m_XcAccessoryColor),
	XC_INT("xc_accessory_color_mode", m_XcAccessoryColorMode), XC_INT("xc_accessory_physics", m_XcAccessoryPhysics),
	// neon outline
	XC_INT("xc_glow", m_XcGlow), XC_COL("xc_glow_color", m_XcGlowColor), XC_INT("xc_glow_color_mode", m_XcGlowColorMode), XC_INT("xc_glow_size", m_XcGlowSize),
	XC_INT("xc_glow_strength", m_XcGlowStrength), XC_INT("xc_glow_outline", m_XcGlowOutline), XC_INT("xc_glow_pulse", m_XcGlowPulse),
	// tee shader
	XC_INT("xc_tee_shader", m_XcTeeShader), XC_INT("xc_tee_shader_color_mode", m_XcTeeShaderColorMode), XC_COL("xc_tee_shader_color", m_XcTeeShaderColor),
	XC_COL("xc_tee_shader_color2", m_XcTeeShaderColor2), XC_INT("xc_tee_shader_alpha", m_XcTeeShaderAlpha), XC_INT("xc_tee_shader_scale", m_XcTeeShaderScale),
	XC_INT("xc_tee_shader_speed", m_XcTeeShaderSpeed), XC_INT("xc_tee_shader_shade", m_XcTeeShaderShade), XC_INT("xc_tee_shader_parts", m_XcTeeShaderParts),
	// trail
	XC_INT_R("xc_trail", m_XcTrail, 0, 6), XC_INT_R("xc_trail_dash", m_XcTrailDash, 0, 4), XC_INT_R("xc_trail_density", m_XcTrailDensity, 25, 300),
	XC_INT_R("xc_trail_length", m_XcTrailLength, 100, 2000), XC_INT_R("xc_trail_width", m_XcTrailWidth, 25, 300), XC_INT_R("xc_trail_glow", m_XcTrailGlow, 0, 200),
	XC_INT_R("xc_trail_color_mode", m_XcTrailColorMode, 0, 3), XC_COL("xc_trail_color", m_XcTrailColor), XC_INT_R("xc_trail_segments", m_XcTrailSegments, 0, 1),
	// hook
	XC_INT_R("xc_hook_style", m_XcHookStyle, 0, 3), XC_COL("xc_hook_color", m_XcHookColor), XC_INT_R("xc_hook_color_mode", m_XcHookColorMode, 0, 3),
	XC_INT_R("xc_hook_width", m_XcHookWidth, 25, 300), XC_INT_R("xc_hook_hit_fx", m_XcHookHitFx, 0, 3), XC_INT_R("xc_hook_hit_player_fx", m_XcHookHitPlayerFx, 0, 3),
	XC_INT_R("xc_hook_nohook_fx", m_XcHookNohookFx, 0, 3),
	// jump rings
	XC_INT_R("xc_jump_fx", m_XcJumpFx, 0, 3), XC_INT_R("xc_jump_fx_air", m_XcJumpFxAir, 0, 1), XC_INT_R("xc_jump_fx_size", m_XcJumpFxSize, 25, 300),
	XC_INT_R("xc_jump_fx_duration", m_XcJumpFxDuration, 200, 3000), XC_COL("xc_jump_fx_color", m_XcJumpFxColor), XC_INT_R("xc_jump_fx_color_mode", m_XcJumpFxColorMode, 0, 3),
	// death, spawn, finish
	XC_INT_R("xc_death_fx", m_XcDeathFx, 0, 7), XC_INT_R("xc_death_color_mode", m_XcDeathColorMode, 0, 3), XC_COL("xc_death_color", m_XcDeathColor),
	XC_INT_R("xc_spawn_fx", m_XcSpawnFx, 0, 1), XC_INT_R("xc_spawn_color_mode", m_XcSpawnColorMode, 0, 3), XC_COL("xc_spawn_color", m_XcSpawnColor),
	XC_INT_R("xc_finish_fx", m_XcFinishFx, 0, 2), XC_INT_R("xc_finish_amount", m_XcFinishAmount, 25, 300), XC_INT_R("xc_finish_color_mode", m_XcFinishColorMode, 0, 3),
	XC_COL("xc_finish_color", m_XcFinishColor),
	// gradient name, jelly tee
	XC_INT_R("xc_name_gradient", m_XcNameGradient, 0, 4), XC_COL("xc_name_gradient_color1", m_XcNameGradientColor1), XC_COL("xc_name_gradient_color2", m_XcNameGradientColor2),
	XC_INT_R("xc_name_gradient_flow", m_XcNameGradientFlow, 0, 1),
	XC_INT_R("xc_jelly", m_XcJelly, 0, 1), XC_INT_R("xc_jelly_strength", m_XcJellyStrength, 10, 300),
	// who sees what: changed while a player with a set is drawn, never sent
	XC_LOCAL("xc_cape_others", m_XcCapeOthers), XC_LOCAL("xc_scarf_others", m_XcScarfOthers), XC_LOCAL("xc_accessory_others", m_XcAccessoryOthers),
	XC_LOCAL("xc_accessory_friends", m_XcAccessoryFriends), XC_LOCAL("xc_glow_others", m_XcGlowOthers), XC_LOCAL("xc_tee_shader_others", m_XcTeeShaderOthers),
	XC_LOCAL("xc_trail_others", m_XcTrailOthers), XC_LOCAL("xc_hook_others", m_XcHookOthers), XC_LOCAL("xc_jump_fx_others", m_XcJumpFxOthers),
	XC_LOCAL("xc_death_others", m_XcDeathOthers), XC_LOCAL("xc_spawn_others", m_XcSpawnOthers), XC_LOCAL("xc_finish_others", m_XcFinishOthers),
	XC_LOCAL("xc_name_gradient_others", m_XcNameGradientOthers), XC_LOCAL("xc_jelly_others", m_XcJellyOthers),
};

#undef XC_INT
#undef XC_INT_R
#undef XC_COL
#undef XC_LOCAL

// a part of the set: it is on only when it is on for yourself; on another player it shows when his set has it on
struct SPart
{
	int *m_pOn;
	int *m_pSelf;
	int *m_pOthers;
};
const SPart s_aParts[] = {
	{&g_Config.m_XcCape, &g_Config.m_XcCapeSelf, &g_Config.m_XcCapeOthers},
	{&g_Config.m_XcScarf, &g_Config.m_XcScarfSelf, &g_Config.m_XcScarfOthers},
	{&g_Config.m_XcAccessory, &g_Config.m_XcAccessorySelf, &g_Config.m_XcAccessoryOthers},
	{&g_Config.m_XcGlow, &g_Config.m_XcGlowSelf, &g_Config.m_XcGlowOthers},
	{&g_Config.m_XcTeeShader, &g_Config.m_XcTeeShaderSelf, &g_Config.m_XcTeeShaderOthers},
	{&g_Config.m_XcTrail, &g_Config.m_XcTrailSelf, &g_Config.m_XcTrailOthers},
	{&g_Config.m_XcHookStyle, &g_Config.m_XcHookSelf, &g_Config.m_XcHookOthers},
	{&g_Config.m_XcJumpFx, &g_Config.m_XcJumpFxSelf, &g_Config.m_XcJumpFxOthers},
	{&g_Config.m_XcDeathFx, &g_Config.m_XcDeathSelf, &g_Config.m_XcDeathOthers},
	{&g_Config.m_XcSpawnFx, &g_Config.m_XcSpawnSelf, &g_Config.m_XcSpawnOthers},
	{&g_Config.m_XcFinishFx, &g_Config.m_XcFinishSelf, &g_Config.m_XcFinishOthers},
	{&g_Config.m_XcNameGradient, &g_Config.m_XcNameGradientSelf, &g_Config.m_XcNameGradientOthers},
	{&g_Config.m_XcJelly, &g_Config.m_XcJellySelf, &g_Config.m_XcJellyOthers},
};

// the menu accent is the viewer's own color: a set carries the color itself
const std::pair<int *, unsigned *> s_aColorModes[] = {
	{&g_Config.m_XcCapeColorMode, &g_Config.m_XcCapeColor},
	{&g_Config.m_XcScarfColorMode, &g_Config.m_XcScarfColor},
	{&g_Config.m_XcAccessoryColorMode, &g_Config.m_XcAccessoryColor},
	{&g_Config.m_XcGlowColorMode, &g_Config.m_XcGlowColor},
	{&g_Config.m_XcTeeShaderColorMode, &g_Config.m_XcTeeShaderColor},
	{&g_Config.m_XcTrailColorMode, &g_Config.m_XcTrailColor},
	{&g_Config.m_XcHookColorMode, &g_Config.m_XcHookColor},
	{&g_Config.m_XcJumpFxColorMode, &g_Config.m_XcJumpFxColor},
	{&g_Config.m_XcDeathColorMode, &g_Config.m_XcDeathColor},
	{&g_Config.m_XcSpawnColorMode, &g_Config.m_XcSpawnColor},
	{&g_Config.m_XcFinishColorMode, &g_Config.m_XcFinishColor},
};

long long ReadField(const SField &Field)
{
	return Field.m_Color ? (long long)*(unsigned *)Field.m_pValue : (long long)*(int *)Field.m_pValue;
}

void WriteField(const SField &Field, long long Value)
{
	if(Field.m_Color)
		*(unsigned *)Field.m_pValue = (unsigned)Value;
	else
		*(int *)Field.m_pValue = (int)Value;
}

void JsonEscape(const char *pIn, char *pOut, int OutSize)
{
	int o = 0;
	for(const char *p = pIn; *p && o < OutSize - 7; p++)
	{
		const unsigned char c = *p;
		if(c == '"' || c == '\\')
		{
			pOut[o++] = '\\';
			pOut[o++] = c;
		}
		else if(c < 0x20)
			o += str_format(pOut + o, OutSize - o, "\\u%04x", c);
		else
			pOut[o++] = c;
	}
	pOut[o] = '\0';
}

void RandomHex(char *pOut, int Bytes)
{
	unsigned char aBytes[32];
	secure_random_fill(aBytes, Bytes);
	for(int i = 0; i < Bytes; i++)
		str_format(pOut + i * 2, 3, "%02x", aBytes[i]);
}

bool IsHex(const char *p, int Length)
{
	if(str_length(p) != Length)
		return false;
	for(; *p; p++)
		if(!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
			return false;
	return true;
}

enum
{
	STATUS_OFF = 0,
	STATUS_WAITING,
	STATUS_OK,
	STATUS_FAILED,
};
} // namespace

void CXcCosmeticsSync::OnReset()
{
	for(SRemote &Remote : m_aRemote)
		Remote = SRemote();
	m_NumRemote = 0;
	m_SentHash.clear();
	m_NeedBundle = true;
	m_NextBeat = 0.0f;
	m_NextAsk = 0.0f;
}

void CXcCosmeticsSync::OnShutdown()
{
	for(auto *ppRequest : {&m_pBeat, &m_pServer, &m_pStats})
		if(*ppRequest)
			(*ppRequest)->Abort();
	for(auto &Pending : m_vpBundles)
		Pending.second->Abort();
	m_vpBundles.clear();
}

void CXcCosmeticsSync::EnsureIdentity()
{
	// a random id of this client and the secret that proves it, made once and kept in the settings
	if(!IsHex(g_Config.m_XcCosmeticsId, 16) || !IsHex(g_Config.m_XcCosmeticsSecret, 32))
	{
		RandomHex(g_Config.m_XcCosmeticsId, 8);
		RandomHex(g_Config.m_XcCosmeticsSecret, 16);
	}
}

std::string CXcCosmeticsSync::OwnBundle(std::string &Hash) const
{
	std::vector<long long> vValues;
	for(const SField &Field : s_aFields)
		vValues.push_back(ReadField(Field));
	const auto Index = [](const void *pValue) {
		for(size_t i = 0; i < std::size(s_aFields); i++)
			if(s_aFields[i].m_pValue == pValue)
				return (int)i;
		return -1;
	};
	// a part off for yourself is off in the set
	for(const SPart &Part : s_aParts)
		if(!*Part.m_pSelf)
			vValues[Index(Part.m_pOn)] = 0;
	// the accent becomes the color itself
	const ImVec4 Accent = XcUi::AccentColor(g_Config.m_XcUiAccent, g_Config.m_XcUiTheme == 0);
	const unsigned AccentPacked = color_cast<ColorHSLA>(ColorRGBA(Accent.x, Accent.y, Accent.z, 1.0f)).Pack(false);
	for(const auto &[pMode, pColor] : s_aColorModes)
		if(*pMode == XcHelpers::COLOR_ACCENT)
		{
			vValues[Index(pMode)] = XcHelpers::COLOR_CUSTOM;
			vValues[Index(pColor)] = AccentPacked;
		}
	// a name in the accent colors: the two colors themselves
	if(g_Config.m_XcNameGradient == 1)
	{
		vValues[Index(&g_Config.m_XcNameGradient)] = 4;
		vValues[Index(&g_Config.m_XcNameGradientColor1)] = AccentPacked;
		ColorHSLA Shifted = color_cast<ColorHSLA>(ColorRGBA(Accent.x, Accent.y, Accent.z, 1.0f));
		Shifted.h = std::fmod(Shifted.h + 0.12f, 1.0f);
		vValues[Index(&g_Config.m_XcNameGradientColor2)] = Shifted.Pack(false);
	}
	// an own picture of the cape is not shared (yet): the plain cloth instead
	if(g_Config.m_XcCapePattern == 3)
		vValues[Index(&g_Config.m_XcCapePattern)] = 0;

	std::string Bundle = "v1";
	for(size_t i = 0; i < std::size(s_aFields); i++)
	{
		if(!s_aFields[i].m_Shared)
			continue;
		char aPart[96];
		str_format(aPart, sizeof(aPart), ";%s=%lld", s_aFields[i].m_pName, vValues[i]);
		Bundle += aPart;
	}
	const SHA256_DIGEST Digest = sha256(Bundle.c_str(), Bundle.size());
	char aHex[SHA256_MAXSTRSIZE];
	sha256_str(Digest, aHex, sizeof(aHex));
	Hash.assign(aHex, 16);
	return Bundle;
}

void CXcCosmeticsSync::SendBeat(bool WithBundle)
{
	EnsureIdentity();
	std::string Hash;
	const std::string Bundle = OwnBundle(Hash);
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	char aName[64];
	JsonEscape(Client()->PlayerName(), aName, sizeof(aName));
	std::string Json = "{\"id\":\"" + std::string(g_Config.m_XcCosmeticsId) + "\",\"secret\":\"" + g_Config.m_XcCosmeticsSecret + "\"";
	if(g_Config.m_XcCosmeticsShare && LocalId >= 0)
		Json += ",\"server\":\"" + m_Address + "\",\"cid\":" + std::to_string(LocalId) + ",\"name\":\"" + aName + "\",\"hash\":\"" + Hash + "\"";
	if(WithBundle && g_Config.m_XcCosmeticsShare)
		Json += ",\"bundle\":\"" + Bundle + "\"";
	Json += "}";
	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "%s/v1/beat", g_Config.m_XcCosmeticsUrl);
	m_pBeat = HttpPostJson(aUrl, Json.c_str());
	m_pBeat->Timeout(CTimeout{4000, 8000, 100, 5});
	m_pBeat->LogProgress(HTTPLOG::NONE);
	m_pBeat->AllowInsecure(true);
	Http()->Run(m_pBeat);
	m_BeatHadBundle = WithBundle;
	m_SentHash = Hash;
}

void CXcCosmeticsSync::AskServer()
{
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/v1/server?addr=%s", g_Config.m_XcCosmeticsUrl, m_Address.c_str());
	m_pServer = HttpGet(aUrl);
	m_pServer->Timeout(CTimeout{4000, 8000, 100, 5});
	m_pServer->LogProgress(HTTPLOG::NONE);
	m_pServer->AllowInsecure(true);
	Http()->Run(m_pServer);
}

void CXcCosmeticsSync::FetchBundles()
{
	// the sets this client does not know yet, a few at a time; a set that failed waits a minute
	const float Now = Client()->GlobalTime();
	for(const SRemote &Remote : m_aRemote)
	{
		if(m_vpBundles.size() >= 4)
			break;
		if(Remote.m_Hash.empty() || m_Bundles.count(Remote.m_Hash))
			continue;
		auto Failed = m_FailedHashes.find(Remote.m_Hash);
		if(Failed != m_FailedHashes.end() && Now < Failed->second)
			continue;
		if(std::any_of(m_vpBundles.begin(), m_vpBundles.end(), [&](const auto &Pending) { return Pending.first == Remote.m_Hash; }))
			continue;
		char aUrl[256];
		str_format(aUrl, sizeof(aUrl), "%s/v1/bundle?hash=%s", g_Config.m_XcCosmeticsUrl, Remote.m_Hash.c_str());
		std::shared_ptr<IHttpRequest> pRequest = HttpGet(aUrl);
		pRequest->Timeout(CTimeout{4000, 8000, 100, 5});
		pRequest->LogProgress(HTTPLOG::NONE);
		pRequest->AllowInsecure(true);
		Http()->Run(pRequest);
		m_vpBundles.emplace_back(Remote.m_Hash, pRequest);
	}
}

void CXcCosmeticsSync::Finish()
{
	const float Now = Client()->GlobalTime();
	if(m_pBeat && m_pBeat->Done())
	{
		json_value *pJson = m_pBeat->State() == EHttpState::DONE ? m_pBeat->ResultJson() : nullptr;
		if(pJson && pJson->type == json_object)
		{
			const json_value *pNeed = json_object_get(pJson, "need_bundle");
			m_NeedBundle = pNeed && pNeed->type == json_boolean && pNeed->u.boolean;
			if(m_NeedBundle && !m_BeatHadBundle)
				m_NextBeat = 0.0f; // the service does not know the set: send it now
			m_Status = STATUS_OK;
			if(m_Online <= 0)
				m_NextStats = std::min(m_NextStats, Now + 1.0f);
		}
		else
		{
			m_Status = STATUS_FAILED;
			m_NextBeat = Now + 30.0f;
		}
		if(pJson)
			json_value_free(pJson);
		m_pBeat = nullptr;
	}
	if(m_pServer && m_pServer->Done())
	{
		json_value *pJson = m_pServer->State() == EHttpState::DONE ? m_pServer->ResultJson() : nullptr;
		if(pJson && pJson->type == json_object)
		{
			for(SRemote &Remote : m_aRemote)
				Remote = SRemote();
			const json_value *pPlayers = json_object_get(pJson, "players");
			const int LocalId = GameClient()->m_Snap.m_LocalClientId;
			for(int i = 0; pPlayers && pPlayers->type == json_array && i < json_array_length(pPlayers); i++)
			{
				const json_value *pPlayer = json_array_get(pPlayers, i);
				const json_value *pCid = json_object_get(pPlayer, "cid");
				const json_value *pName = json_object_get(pPlayer, "name");
				const json_value *pHash = json_object_get(pPlayer, "hash");
				if(!pCid || pCid->type != json_integer || !pName || pName->type != json_string)
					continue;
				const int Cid = (int)json_int_get(pCid);
				if(Cid < 0 || Cid >= MAX_CLIENTS || Cid == LocalId || Cid == GameClient()->m_aLocalIds[1])
					continue;
				m_aRemote[Cid].m_Name = json_string_get(pName);
				// no set: still an Exora player (the icon), nothing to draw
				m_aRemote[Cid].m_Hash = pHash && pHash->type == json_string ? json_string_get(pHash) : "";
			}
		}
		if(pJson)
			json_value_free(pJson);
		m_pServer = nullptr;
		FetchBundles();
	}
	for(auto It = m_vpBundles.begin(); It != m_vpBundles.end();)
	{
		if(!It->second->Done())
		{
			++It;
			continue;
		}
		json_value *pJson = It->second->State() == EHttpState::DONE ? It->second->ResultJson() : nullptr;
		const json_value *pData = pJson && pJson->type == json_object ? json_object_get(pJson, "data") : nullptr;
		if(pData && pData->type == json_string)
		{
			// "v1;name=value;...": only the names this client knows, the rest is ignored
			TBundle Bundle;
			char aToken[96];
			const char *pStr = json_string_get(pData);
			while((pStr = str_next_token(pStr, ";", aToken, sizeof(aToken))))
			{
				char *pEq = (char *)str_find(aToken, "=");
				if(!pEq)
					continue;
				*pEq = '\0';
				Bundle[aToken] = str_toint(pEq + 1);
			}
			if(m_Bundles.size() > 256)
				m_Bundles.clear();
			m_Bundles[It->first] = Bundle;
		}
		else
			m_FailedHashes[It->first] = Now + 60.0f;
		if(pJson)
			json_value_free(pJson);
		It = m_vpBundles.erase(It);
	}
	m_NumRemote = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
		m_NumRemote += HasRemote(i);
}

// how many clients run now: a short beat every half a minute also from the menu, and the count from the service
void CXcCosmeticsSync::UpdateOnline()
{
	const float Now = Client()->GlobalTime();
	if(m_pStats && m_pStats->Done())
	{
		json_value *pJson = m_pStats->State() == EHttpState::DONE ? m_pStats->ResultJson() : nullptr;
		const json_value *pOnline = pJson && pJson->type == json_object ? json_object_get(pJson, "online") : nullptr;
		if(pOnline && pOnline->type == json_integer)
			m_Online = (int)json_int_get(pOnline);
		if(pJson)
			json_value_free(pJson);
		m_pStats = nullptr;
	}
	if(!m_pStats && Now >= m_NextStats)
	{
		char aUrl[256];
		str_format(aUrl, sizeof(aUrl), "%s/v1/stats", g_Config.m_XcCosmeticsUrl);
		m_pStats = HttpGet(aUrl);
		m_pStats->Timeout(CTimeout{4000, 8000, 100, 5});
		m_pStats->LogProgress(HTTPLOG::NONE);
		m_pStats->AllowInsecure(true);
		Http()->Run(m_pStats);
		m_NextStats = Now + 30.0f;
	}
}

void CXcCosmeticsSync::OnRender()
{
	if(!g_Config.m_XcCosmeticsUrl[0])
		return;
	UpdateOnline();
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	if(!Online || (!g_Config.m_XcCosmeticsShare && !g_Config.m_XcCosmeticsOthers && !g_Config.m_XcExoraIcon))
	{
		if(m_NumRemote || !m_Address.empty())
		{
			OnReset();
			m_Address.clear();
		}
		m_Status = g_Config.m_XcCosmeticsShare || g_Config.m_XcCosmeticsOthers ? STATUS_WAITING : STATUS_OFF;
		// in the menu: only "this client runs", to be counted online (not when sharing is off altogether)
		const float Now = Client()->GlobalTime();
		if(m_pBeat && m_pBeat->Done())
		{
			// the first beat went through: ask the count again soon, this client is in it now
			if(m_Online <= 0)
				m_NextStats = std::min(m_NextStats, Now + 1.0f);
			m_pBeat = nullptr;
		}
		if(!m_pBeat && Now >= m_NextBeat && (g_Config.m_XcCosmeticsShare || g_Config.m_XcCosmeticsOthers))
		{
			EnsureIdentity();
			char aUrl[256];
			str_format(aUrl, sizeof(aUrl), "%s/v1/beat", g_Config.m_XcCosmeticsUrl);
			char aJson[160];
			str_format(aJson, sizeof(aJson), "{\"id\":\"%s\",\"secret\":\"%s\"}", g_Config.m_XcCosmeticsId, g_Config.m_XcCosmeticsSecret);
			m_pBeat = HttpPostJson(aUrl, aJson);
			m_pBeat->Timeout(CTimeout{4000, 8000, 100, 5});
			m_pBeat->LogProgress(HTTPLOG::NONE);
			m_pBeat->AllowInsecure(true);
			Http()->Run(m_pBeat);
			m_BeatHadBundle = false;
			m_NextBeat = Now + 30.0f;
		}
		return;
	}
	char aAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aAddress, sizeof(aAddress), true);
	if(m_Address != aAddress)
	{
		OnReset();
		m_Address = aAddress;
	}
	if(m_Status == STATUS_OFF)
		m_Status = STATUS_WAITING;
	Finish();

	const float Now = Client()->GlobalTime();
	// a set that changed goes out at once, otherwise a short "still here" every ten seconds
	std::string Hash;
	OwnBundle(Hash);
	const bool Changed = g_Config.m_XcCosmeticsShare && Hash != m_SentHash;
	if((g_Config.m_XcCosmeticsShare || g_Config.m_XcCosmeticsOthers) && !m_pBeat && GameClient()->m_Snap.m_LocalClientId >= 0 && (Now >= m_NextBeat || Changed))
	{
		SendBeat(m_NeedBundle || Changed);
		m_NextBeat = Now + 10.0f;
	}
	// who wears what, and who plays Exora at all (the icon)
	if((g_Config.m_XcCosmeticsOthers || g_Config.m_XcExoraIcon) && !m_pServer && Now >= m_NextAsk)
	{
		AskServer();
		m_NextAsk = Now + 10.0f;
	}
}

bool CXcCosmeticsSync::HasRemote(int ClientId) const
{
	if(!g_Config.m_XcCosmeticsOthers || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const SRemote &Remote = m_aRemote[ClientId];
	if(Remote.m_Hash.empty() || !m_Bundles.count(Remote.m_Hash))
		return false;
	// the number on the server may belong to someone else by now: the name must still match
	const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
	return Data.m_Active && str_comp(Data.m_aName, Remote.m_Name.c_str()) == 0;
}

bool CXcCosmeticsSync::IsExora(int ClientId) const
{
	if(!g_Config.m_XcExoraIcon || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	// yourself and your dummy
	if(ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1])
		return Client()->State() == IClient::STATE_ONLINE;
	const SRemote &Remote = m_aRemote[ClientId];
	if(Remote.m_Name.empty())
		return false;
	const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
	return Data.m_Active && str_comp(Data.m_aName, Remote.m_Name.c_str()) == 0;
}

const char *CXcCosmeticsSync::Status() const
{
	switch(m_Status)
	{
	case STATUS_OK: return "ok";
	case STATUS_FAILED: return "failed";
	case STATUS_WAITING: return "waiting";
	default: return "off";
	}
}

bool CXcCosmeticsSync::ApplyTo(int ClientId, std::vector<long long> &vSaved)
{
	if(m_ScopeDepth > 0 || !HasRemote(ClientId))
		return false;
	const TBundle &Bundle = m_Bundles.at(m_aRemote[ClientId].m_Hash);
	vSaved.clear();
	for(const SField &Field : s_aFields)
		vSaved.push_back(ReadField(Field));
	for(const SField &Field : s_aFields)
	{
		if(!Field.m_Shared)
			continue;
		auto It = Bundle.find(Field.m_pName);
		// a part the set does not name is off
		long long Value = It != Bundle.end() ? It->second : 0;
		if(!Field.m_Color)
			Value = std::clamp<long long>(Value, Field.m_Min, Field.m_Max);
		WriteField(Field, Value);
	}
	for(const SPart &Part : s_aParts)
		*Part.m_pOthers = *Part.m_pOn != 0;
	g_Config.m_XcAccessoryFriends = 0;
	// the values stay in their ranges whatever the set says
	g_Config.m_XcCapeLength = std::clamp(g_Config.m_XcCapeLength, 50, 200);
	g_Config.m_XcCapeWidth = std::clamp(g_Config.m_XcCapeWidth, 50, 200);
	g_Config.m_XcCapePattern = std::clamp(g_Config.m_XcCapePattern, 0, 2);
	g_Config.m_XcScarfTails = std::clamp(g_Config.m_XcScarfTails, 1, 2);
	g_Config.m_XcAccessory = std::clamp(g_Config.m_XcAccessory, 0, 10);
	g_Config.m_XcAccessorySize = std::clamp(g_Config.m_XcAccessorySize, 25, 300);
	g_Config.m_XcTeeShader = std::clamp(g_Config.m_XcTeeShader, 0, 5);
	g_Config.m_XcGlowSize = std::clamp(g_Config.m_XcGlowSize, 25, 300);
	m_ScopeDepth++;
	return true;
}

void CXcCosmeticsSync::Restore(const std::vector<long long> &vSaved)
{
	for(size_t i = 0; i < std::size(s_aFields) && i < vSaved.size(); i++)
		WriteField(s_aFields[i], vSaved[i]);
	m_ScopeDepth--;
}

static CXcCosmeticsSync *s_pScopeSync = nullptr;

CXcCosmeticsSync::CScope::CScope(CXcCosmeticsSync &Sync, int ClientId)
{
	m_Active = Sync.ApplyTo(ClientId, m_vSaved);
	if(m_Active)
		s_pScopeSync = &Sync;
}

CXcCosmeticsSync::CScope::~CScope()
{
	if(m_Active && s_pScopeSync)
		s_pScopeSync->Restore(m_vSaved);
}

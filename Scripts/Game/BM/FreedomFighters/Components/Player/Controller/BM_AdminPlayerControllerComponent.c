modded class JWK_FactionManager
{
	bool BM_LoadFaction(string factionKey)
	{
		if (GetJWKFaction(factionKey)) return true;

		JWK_FactionConfig config = GetFactionConfig(factionKey);
		if (!config) return false;

		JWK_Faction faction = JWK_Faction.CreateFromConfig(factionKey, config);
		if (!faction) return false;
		
		m_aFactions.Insert(faction);	
		InitializeFaction(faction);
		return true;
	}

	override JWK_EFactionRole GetRoleByFactionKey(string key)
	{
		// Fallback to original FF logic first
		JWK_EFactionRole role = super.GetRoleByFactionKey(key);
		if (role != JWK_EFactionRole.UNDEFINED) return role;

		// BM_FIX: The original mod hardcodes factions. Our dynamic Invader isn't recognized.
		// We MUST tell the rest of the game (UI, Quests, AI limits) that Invaders are enemies.
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (gm && gm.BM_GetInvaderFactionKey() == key) {
			return JWK_EFactionRole.ENEMY;
		}

		return JWK_EFactionRole.UNDEFINED;
	}
}

modded class JWK_AdminPlayerControllerComponent {

	void BM_ApplyInvasionConfig(string invaderFactionKey)
	{
		if (!IsOwnerPlayerAdmin()) return;
		Rpc(BM_RpcAsk_ApplyInvasionConfig, invaderFactionKey);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void BM_RpcAsk_ApplyInvasionConfig(string invaderFactionKey)
	{
		if (!IsOwnerPlayerAdmin()) return;

		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (gm) {
			gm.BM_SetInvaderFactionKey(invaderFactionKey);
		}
	}
}

// ==========================================
// BM_JWK_AdminMod.c
// THE ADMIN RPC BRIDGE
// ==========================================

modded class JWK_AdminPlayerControllerComponent
{
	void BM_ApplyInvasionSettings(string factionKey, int tickets, int minBases, bool instant)
	{
		if (!IsOwnerPlayerAdmin()) return;
		Rpc(BM_RpcAsk_ApplyInvasionSettings, factionKey, tickets, minBases, instant);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void BM_RpcAsk_ApplyInvasionSettings(string factionKey, int tickets, int minBases, bool instant)
	{
		if (!IsOwnerPlayerAdmin()) return;
		BM_InvasionManager manager = BM_InvasionManager.GetInstance();
		if (manager)
		{
			manager.Setup(factionKey, tickets, minBases, instant);
			Print("BM_Invasion: AAA SIM: Admin strategic settings applied natively.", LogLevel.NORMAL);
		}
	}
}

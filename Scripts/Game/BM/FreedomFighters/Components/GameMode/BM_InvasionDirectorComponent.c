[ComponentEditorProps(category: "BakerMods/Game Mode", description: "Handles the dynamic invasion and AI director for BakerMods.")]
class BM_InvasionDirectorComponentClass : JWK_GameModeComponentClass
{
}

class BM_InvasionDirectorComponent : JWK_GameModeComponent
{
	[Attribute(defvalue: "0", desc: "How many zones players must capture before the invasion triggers.", category: "BakerMods: Invasion")]
	protected int m_iZonesForInvasion;
	
	[Attribute(defvalue: "3", desc: "Base number of vehicles spawned per invasion wave.", category: "BakerMods: Invasion")]
	protected int m_iBaseVehicles;
	
	[Attribute(defvalue: "2", desc: "How many extra vehicles to spawn per human player.", category: "BakerMods: Invasion")]
	protected int m_iVehiclesPerPlayer;

	[Attribute(defvalue: "50", desc: "Maximum number of active Invader AI agents allowed before stopping new spawns.", category: "BakerMods: Invasion")]
	protected int m_iMaxActiveAI;

	protected bool m_bInvasionTriggered;
	protected string m_sInvaderFactionKey = "None";
	protected float m_fLastExpansionTime;
	protected EntityID m_iCurrentTargetID;
	protected float m_fLastReinforcementTime;
	protected float m_fLastRoamingTime;
	protected bool m_bIntelNotified;
	protected float m_fCurrentCooldown;
	protected int m_iLastAliveCount;
	protected int m_iPlayerZoneCount;

	// --- Dynamic AI Director ---
	protected EntityID m_iStalemateTargetID = EntityID.INVALID;
	protected int m_iStalemateWaveCount = 0;
	static const int STALEMATE_WAVE_THRESHOLD = 3; 
	protected int m_iCasualtiesSinceLastWave = 0;
	static const int CASUALTY_COOLDOWN_BONUS_S = 5;

	// --- Cleanup System ---
	protected ref array<EntityID> m_aTrackedEntities = new array<EntityID>();
	
	// --- Public API ---
	string GetInvaderFactionKey() { return m_sInvaderFactionKey; }
	EntityID GetCurrentTargetID() { return m_iCurrentTargetID; }
	void SetInvaderFactionKey(string factionKey) { m_sInvaderFactionKey = factionKey; }
	void SetPlayerZoneCount(int count) { m_iPlayerZoneCount = count; }
	
	// --- Main Logic Loop ---
	override void PostGameModeStart_S()
	{
		GetGame().GetCallqueue().CallLater(UpdateInvasionDirector, 15000, true);
	}
	
	protected void UpdateInvasionDirector()
	{
		if (!JWK.GetGameMode().IsRunning()) return;
		
		PeriodicCleanup_S();
		
		if (m_sInvaderFactionKey.IsEmpty() || m_sInvaderFactionKey == "None") return;

		if (!m_bInvasionTriggered)
		{
			if (m_iPlayerZoneCount == 0) // On a fresh game, this will be 0
				m_iPlayerZoneCount = GetPlayerZoneCount();
			
			if (m_iPlayerZoneCount >= m_iZonesForInvasion) {
				TriggerInvasion();
			}
		}
		else
		{
			// ... (invasion logic will go here)
		}
	}
	
	protected void TriggerInvasion()
	{
		m_bInvasionTriggered = true;
		// Placeholder for invasion trigger logic
		Print("BM_Invasion: Invasion Triggered!", LogLevel.NORMAL);
	}
	
	protected void PeriodicCleanup_S()
	{
		// Placeholder for cleanup logic
	}
	
	// --- Utility Functions ---
	int GetPlayerZoneCount()
	{
		int count = 0;
		array<EntityID> outIds = {};
		outIds.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity));
		outIds.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity));
		foreach (EntityID id : outIds) {
			if (JWK_FactionControlComponent.IsPlayerFaction(id)) count++;
		}
		return count;
	}
	
	static BM_InvasionDirectorComponent GetInstance()
	{
		JWK_GameMode gameMode = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
			return BM_InvasionDirectorComponent.Cast(gameMode.FindComponent(BM_InvasionDirectorComponent));
		
		return null;
	}
}

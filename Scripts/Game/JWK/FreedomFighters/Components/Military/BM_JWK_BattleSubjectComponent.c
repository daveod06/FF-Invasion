/**********************************************************************************************************************
 ************************************************* FREEDOM FIGHTERS ***************************************************
 **********************************************************************************************************************
 * This file is part of the Freedom Fighters project, published under modified APL-ND license. You are free to adapt  *
 * (i.e. modify, rework or update) and share (i.e. copy, distribute or transmit) the material under the following     *
 * conditions:                                                                                                        *
 * - Attribution - You must attribute the material in the manner specified by the author or licensor (but not in any  *
 *   way that suggests that they endorse you or your use of the material).                                            *
 * - Noncommercial - You may not use this material for any commercial purposes.                                       *
 * - Arma Only - You may not convert or adapt this material to be used in other games than Arma.                      *
 * - No Derivatives - If you remix, transform, or build upon the material, you may not distribute the modified        *
 *   material.                                                                                                        *
 * The above list is only a highlight and is NOT an exhaustive list of restrictions. Please visit the following URL   *
 * to view the full text of the license:                                                                              *
 *    https://www.johnnykerner.dev/FreedomFighters/project-license/20250916/                                          *
 **********************************************************************************************************************
 * For more info or to contact the author, visit the project website:                                                 *
 *    https://www.johnnykerner.dev/FreedomFighters/                                                                   *
 ******************************************************************************************************************** */
// BakerMods overrides applied:
//   Fix 2:  DoBattleFinish_S captures location for invaders when they win.
//   Fix 3:  Garrison groups tracked in m_BMGarrisonForce; cleaned up on battle finish.
//   Fix 5:  BM_GetInvaderFactionIndex() iterates faction manager safely.
//   Fix 8:  5-minute cooldown after an invader battle to prevent phantom re-triggers.
//   Fix 10: IsBattleActive_S() guard in BM_CheckInvaderAutostartCondition_S.
//   Fix 11: Garrison spawn uses fixed-direction scatter instead of east-only line.
//   Fix 17: Player notification broadcast when garrison is deployed.

modded class JWK_BattleSubjectComponent
{
	// -------------------------------------------------------------------------------------

	// BM: tracks spawned garrison groups so they can be cleaned up on battle finish (Fix 3)
	protected ref JWK_AIForce m_BMGarrisonForce;

	// BM: world time of last invader battle finish, used for re-trigger cooldown (Fix 8)
	protected float m_fBMLastInvaderBattleFinishTime = -999999;

	// -------------------------------------------------------------------------------------
	override void DoBattleStart_S(JWK_BattleControllerEntity controller)
	{
		super.DoBattleStart_S(controller);
		
		// BM Fix 3+11+17: stream garrison defenders if invaders are present in the battle zones
		BM_MaybeStreamGarrisonForces_S();
	}

	// -------------------------------------------------------------------------------------
	override void DoBattleFinish_S(JWK_BattleControllerEntity controller)
	{
		super.DoBattleFinish_S(controller);
		
		// BM Fix 3: clean up tracked garrison force
		if (m_BMGarrisonForce)
		{
			m_BMGarrisonForce.ForceDeleteAllUnits();
			m_BMGarrisonForce = null;
		}

		// BM Fix 2+8: handle invader battle outcome
		if (controller.IsInvaderBattle_S())
		{
			// Fix 8: set cooldown to prevent phantom re-triggers at this location
			m_fBMLastInvaderBattleFinishTime = GetGame().GetWorld().GetWorldTime();

			// Fix 2: if invaders won, force this location's faction to the invader faction
			int invaderIdx = BM_GetInvaderFactionIndex();
			if (invaderIdx >= 0 && controller.GetWinningFactionID() == invaderIdx)
				BM_CaptureLocationForInvaders_S();
		}
	}

	// -------------------------------------------------------------------------------------
	override bool CheckAutostartCondition_S()
	{		
		// BM Fix 10+8: invader-triggered defensive battle
		if (BM_CheckInvaderAutostartCondition_S()) return true;

		if (m_bAutostartOnPlayerDomination)
		{
			if (m_FactionAffiliation.GetAffiliatedFaction() == JWK.GetFactions().GetPlayerFaction())
				return false;

			array<JWK_WorldZoneComponent> zones = {};
			if (m_WorldZones)
				m_WorldZones.FindZonesByTag_S(JWK_EWorldZoneTag.BATTLE_AREA, zones);

			float players = GetPlayersWeightForAutostart(zones);
			float enemies = GetEnemiesWeightForAutostart(zones);

			if (players > enemies) return true;
		}

		return false;
	}

	// -------------------------------------------------------------------------------------
	// BM Fix 10: guard against re-triggering while battle is active or in cooldown.
	// Only triggers the FF battle system when BOTH invaders AND players are in the zone.
	// Pure AI-vs-AI fights are handled by the siege system in BM_InvasionManager instead.
	protected bool BM_CheckInvaderAutostartCondition_S()
	{
		// Fix 10: never trigger if a battle is already running here
		if (IsBattleActive_S()) return false;

		// Fix 8: 5-minute cooldown after last invader battle at this location
		float now = GetGame().GetWorld().GetWorldTime();
		if (now - m_fBMLastInvaderBattleFinishTime < 300000) return false;

		// Trigger for any non-invader location (occupier or player bases can be attacked)
		if (!m_FactionAffiliation) return false;
		Faction affiliated = m_FactionAffiliation.GetAffiliatedFaction();
		if (!affiliated) return false;
		if (affiliated.GetFactionKey() == BM_InvasionManager.s_InvaderFactionKey) return false;

		BM_InvasionManager invasion = BM_InvasionManager.GetInstance();
		if (!invasion || !invasion.m_bActive) return false;

		if (!m_WorldZones) return false;
		array<JWK_WorldZoneComponent> zones = {};
		m_WorldZones.FindZonesByTag_S(JWK_EWorldZoneTag.BATTLE_AREA, zones);
		if (zones.IsEmpty()) return false;

		// Require at least one player in the battle zone — AI-vs-AI is handled by siege system
		bool playerPresent = false;
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		foreach (int playerID : players) {
			IEntity pEnt = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerID);
			if (!pEnt) continue;
			foreach (JWK_WorldZoneComponent zone : zones) {
				if (zone.Contains(pEnt.GetOrigin())) { playerPresent = true; break; }
			}
			if (playerPresent) break;
		}
		if (!playerPresent) return false;

		JWK_AIForce invaderForce = invasion.GetInvaderForce();
		array<EntityID> groupIDs = invaderForce.GetGroups();
		foreach (EntityID groupID : groupIDs) {
			SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupID));
			if (!group || group.GetAgentsCount() == 0) continue;
			foreach (JWK_WorldZoneComponent zone : zones) {
				if (zone.Contains(group.GetOrigin())) {
					JWK_Log.Log(this, "Invader force + player in battle area — triggering defensive battle.");
					return true;
				}
			}
		}

		return false;
	}

	// -------------------------------------------------------------------------------------
	// BM Fix 3+11+17: spawn garrison when invaders are in battle zones OR when base is invader-owned (player attack).
	protected void BM_MaybeStreamGarrisonForces_S()
	{
		BM_InvasionManager invasion = BM_InvasionManager.GetInstance();
		if (!invasion || !invasion.m_bActive) return;

		// If this base is invader-owned, players are attacking — always stream defenders
		if (m_FactionAffiliation && m_FactionAffiliation.GetAffiliatedFactionKey() == BM_InvasionManager.s_InvaderFactionKey) {
			BM_StreamGarrisonForces_S();
			return;
		}

		if (!m_WorldZones) return;
		array<JWK_WorldZoneComponent> zones = {};
		m_WorldZones.FindZonesByTag_S(JWK_EWorldZoneTag.BATTLE_AREA, zones);
		if (zones.IsEmpty()) return;

		JWK_AIForce invaderForce = invasion.GetInvaderForce();
		array<EntityID> groupIDs = invaderForce.GetGroups();
		foreach (EntityID groupID : groupIDs) {
			SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupID));
			if (!group || group.GetAgentsCount() == 0) continue;
			foreach (JWK_WorldZoneComponent zone : zones) {
				if (zone.Contains(group.GetOrigin())) {
					BM_StreamGarrisonForces_S();
					return;
				}
			}
		}
	}

	// -------------------------------------------------------------------------------------
	// BM Fix 3+11+17: spawn 2 player-faction infantry groups and track them for later cleanup.
	// Fix 11: uses directional scatter (+X and -X) instead of an east-only line.
	// Fix 17: broadcasts alert notification to players.
	protected void BM_StreamGarrisonForces_S()
	{
		// Spawn defenders from the base OWNER's faction (could be occupier or player)
		if (!m_FactionAffiliation) return;
		Faction ownerFaction = m_FactionAffiliation.GetAffiliatedFaction();
		SCR_Faction scrFaction = SCR_Faction.Cast(ownerFaction);
		if (!scrFaction) return;

		SCR_EntityCatalog catalog = scrFaction.GetFactionEntityCatalogOfType(EEntityCatalogType.GROUP);
		if (!catalog) return;

		array<SCR_EntityCatalogEntry> entries = {};
		catalog.GetEntityList(entries);
		if (entries.IsEmpty()) return;

		// Fix 11: filter to infantry-only groups (exclude vehicle keywords)
		array<ResourceName> infantryGroups = {};
		foreach (SCR_EntityCatalogEntry entry : entries) {
			string path = entry.GetPrefab();
			path.ToLower();
			if (!path.Contains("brdm") && !path.Contains("btr") && !path.Contains("bmp") &&
				!path.Contains("tank") && !path.Contains("t72") && !path.Contains("t80") &&
				!path.Contains("ifv") && !path.Contains("apc") && !path.Contains("crew") &&
				!path.Contains("armor") && !path.Contains("vehicle"))
				infantryGroups.Insert(entry.GetPrefab());
		}
		if (infantryGroups.IsEmpty()) {
			// fallback: use any group if no infantry found
			foreach (SCR_EntityCatalogEntry entry : entries) infantryGroups.Insert(entry.GetPrefab());
		}

		// Fix 3: create tracked force for cleanup on battle finish
		m_BMGarrisonForce = new JWK_AIForce();
		m_BMGarrisonForce.m_Log.m_Prefix = "BM_Garrison";

		vector ownerOrigin = GetOwner().GetOrigin();

		// Spawn more defenders if this is an invader-owned base (players attacking, wave gen suppressed)
		int defenderCount = 2;
		if (ownerFaction.GetFactionKey() == BM_InvasionManager.s_InvaderFactionKey)
			defenderCount = 4;

		for (int i = 0; i < defenderCount; i++) {
			float angle = i * (2.0 * Math.PI / defenderCount) + JWK.Random.RandFloatXY(-0.3, 0.3);
			float radius = 10 + JWK.Random.RandFloatXY(0, 8);
			vector spawnPos = ownerOrigin + Vector(Math.Sin(angle) * radius, 0, Math.Cos(angle) * radius);
			spawnPos[1] = GetGame().GetWorld().GetSurfaceY(spawnPos[0], spawnPos[2]);

			SCR_AIGroup group = SCR_AIGroup.Cast(
				JWK_SpawnUtils.SpawnEntityPrefab(infantryGroups.GetRandomElement(), spawnPos)
			);
			if (group) {
				group.AddWaypoint(
					JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.SEARCH_AND_DESTROY, ownerOrigin)
				);
				m_BMGarrisonForce.AttachGroup(group);
			}
		}

		// Fix 17: notify players
		JWK_NamedLocationComponent namedLoc =
			JWK_CompTU<JWK_NamedLocationComponent>.FindIn(GetOwner());
		string locName = "your position";
		if (namedLoc) locName = namedLoc.GetName();
		JWK.GetNotifications().BroadcastNotification_S(
			ENotification.JWK_FREE_TEXT,
			"ALERT: Invader assault on " + locName + "! Garrison deployed."
		);

		JWK_Log.Log(this, "Streamed " + defenderCount + " garrison groups to defend against invaders at " + locName + ".");
	}

	// -------------------------------------------------------------------------------------
	// Fix 2: force faction change on this location entity to the invader faction.
	protected void BM_CaptureLocationForInvaders_S()
	{
		Faction invFac = GetGame().GetFactionManager()
			.GetFactionByKey(BM_InvasionManager.s_InvaderFactionKey);
		if (!invFac) return;

		JWK_FactionControlComponent ctrl =
			JWK_CompTU<JWK_FactionControlComponent>.FindIn(GetOwner());
		if (ctrl) {
			ctrl.ChangeControlToFaction(invFac);
			JWK_Log.Log(this, "Location captured by invaders.");
		}
	}

	// -------------------------------------------------------------------------------------
	// Fix 5: safe faction index lookup — iterates faction manager rather than calling GetFactionIndex.
	protected int BM_GetInvaderFactionIndex()
	{
		FactionManager factionMgr = GetGame().GetFactionManager();
		string key = BM_InvasionManager.s_InvaderFactionKey;
		for (int i = 0; i < factionMgr.GetFactionsCount(); i++) {
			Faction f = factionMgr.GetFactionByIndex(i);
			if (f && f.GetFactionKey() == key) return i;
		}
		return -1;
	}

}

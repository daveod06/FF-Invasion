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
//   Fix 1: SetupEnemyGenerator_S suppresses FF wave generator for invader battles.
//          CheckUpdatePoints_Invader_S scores based on BM_InvasionManager force instead.
//   Fix 5: BM_GetInvaderFactionIndex() replaces GetFactionIndex() (may not exist in API).

modded class JWK_BattleControllerEntity
{
	// BM: true when the attacking faction is the invader faction
	protected bool m_bIsInvaderBattle;

	// ---------------------------------------------------------------------------------------------
	bool IsInvaderBattle_S()
	{
		return m_bIsInvaderBattle;
	}
	
	// ---------------------------------------------------------------------------------------------
	override protected void SetupEnemyGenerator_S()
	{
		m_bIsInvaderBattle = BM_DetectInvaderBattle_S();

		// Fix 1: for invader battles the enemy force is managed entirely by BM_InvasionManager.
		// Do not start the FF wave generator — scoring is handled by CheckUpdatePoints_Invader_S.
		if (m_bIsInvaderBattle)
		{
			JWK_Log.Log(this, "Invader battle detected — wave generator suppressed.");
			return;
		}
		
		// call super last because we need to return above if invader battle
		super.SetupEnemyGenerator_S();
	}

	// ---------------------------------------------------------------------------------------------
	// BM: checks if the INVADERS are the attacking force in this battle.
	// Returns true only when invaders attack a non-invader base — NOT when players attack an invader base.
	// Player-vs-invader-base uses the normal FF wave generator to spawn invader defenders.
	protected bool BM_DetectInvaderBattle_S()
	{
		if (m_AttackingFaction_S != null &&
			m_AttackingFaction_S.GetFactionKey() == BM_InvasionManager.s_InvaderFactionKey)
			return true;

		BM_InvasionManager invasion = BM_InvasionManager.GetInstance();
		if (!invasion || !invasion.m_bActive) return false;

		JWK_BattleSubjectComponent subject = GetSubject_S();
		if (!subject) return false;

		// If base is invader-owned, players are the attackers — use normal FF wave gen, NOT invader battle path
		SCR_FactionAffiliationComponent baseAffil =
			JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(subject.GetOwner());
		if (baseAffil && baseAffil.GetAffiliatedFactionKey() == BM_InvasionManager.s_InvaderFactionKey)
			return false;

		// Check if invader groups are in the battle zone (invaders attacking a non-invader base)
		JWK_WorldZoneControllerComponent worldZones =
			JWK_CompTU<JWK_WorldZoneControllerComponent>.FindIn(subject.GetOwner());
		if (!worldZones) return false;

		array<JWK_WorldZoneComponent> zones = {};
		worldZones.FindZonesByTag_S(JWK_EWorldZoneTag.BATTLE_AREA, zones);
		if (zones.IsEmpty()) return false;

		JWK_AIForce invaderForce = invasion.GetInvaderForce();
		array<EntityID> groupIDs = invaderForce.GetGroups();
		foreach (EntityID groupID : groupIDs) {
			SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupID));
			if (!group || group.GetAgentsCount() == 0) continue;
			foreach (JWK_WorldZoneComponent zone : zones) {
				if (zone.Contains(group.GetOrigin())) return true;
			}
		}

		return false;
	}

	// ---------------------------------------------------------------------------------------------
	// Fix 5: safe faction index lookup by iterating faction manager (GetFactionIndex may not exist).
	protected int BM_GetInvaderFactionIndex()
	{
		FactionManager factionMgr = GetGame().GetFactionManager();
		string key = BM_InvasionManager.s_InvaderFactionKey;
		for (int i = 0; i < factionMgr.GetFactionsCount(); i++) {
			Faction f = factionMgr.GetFactionByIndex(i);
			if (f && f.GetFactionKey() == key) return i;
		}
		return -1; // not found — caller should treat as no invader win
	}

	// ---------------------------------------------------------------------------------------------
	// Fix 1: generator was never started for invader battles, skip cleanup
	override protected void DoFinish_S()
	{
		JWK_Log.Log(this, "Finishing, winner faction: " + m_iWinningFaction + ".");
		m_bIsActive = false;

		StopTimers();

		m_AIForce.Update();
		m_AIForce.ReleaseAllUnits(autoGC: true);

		JWK_BattleSubjectComponent subject = GetSubject_S();
		subject.DoBattleFinish_S(this);

		// Fix 1: generator was never started for invader battles, skip cleanup
		if (m_EnemyGenerator_S && !m_bIsInvaderBattle) {
			m_EnemyGenerator_S.Reset();
			m_EnemyGenerator_S.SetController(null);
			m_EnemyGenerator_S.SetSubject(null);
			m_EnemyGenerator_S.SetAIForce(null);
			m_EnemyGenerator_S.SetPlacementGenerator(null);
		}

		if (m_OnBattleFinished_S)
			m_OnBattleFinished_S.Invoke(this, subject);
	}

	// ---------------------------------------------------------------------------------------------
	// Fix 1: scoring path for invader battles — counts BM_InvasionManager groups in the battle zones.
	// Session 4: handles AI-vs-AI without players — auto-resolves based on invader presence.
	protected void CheckUpdatePoints_Invader_S()
	{
		BM_InvasionManager invasion = BM_InvasionManager.GetInstance();

		int enemyNum = 0;
		if (invasion) {
			JWK_AIForce invaderForce = invasion.GetInvaderForce();
			array<EntityID> groupIDs = invaderForce.GetGroups();
			foreach (EntityID groupID : groupIDs) {
				SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupID));
				if (!group || group.GetAgentsCount() == 0) continue;
				if (CheckIsEntityInPresenceZone(group))
					enemyNum += group.GetAgentsCount();
			}
		}

		int totalPlayers;
		int playerNum = GetPlayerCountInZone_S(totalPlayers);
		int newPoints = m_iPoints;

		if (enemyNum == 0) {
			// All invaders cleared from zone — defensive victory (players or AI won)
			newPoints = GetPointsToWin();
		} else if (playerNum > 0) {
			// Players present, invaders still fighting — build domination
			m_bDominationActive = true;
			float dominationProgress = (m_iDominationPoints / m_iDominationPointsRequired);
			newPoints = 100 + dominationProgress * 100;
		} else {
			// No players but invaders still present — let siege system handle it,
			// slowly tick toward invader win so battle doesn't stall forever
			newPoints = newPoints - 1;
		}

		if (m_bDominationActive) {
			if (GetWeightedPlayerPresence(playerNum, totalPlayers) >= enemyNum) {
				m_iDominationPoints += POINTS_INTERVAL_MS / 1000;
			}
		}

		m_bPlayersWereInArea = (playerNum > 0);

		JWK_Log.Log(this, string.Format(
			"[Invader] Invaders in zone: %1, players: %2. Points: %3/%4.",
			enemyNum, playerNum, newPoints, GetPointsToWin()
		));

		SetPoints_S(newPoints);
	}

	// ---------------------------------------------------------------------------------------------
	override protected void CheckFinishCondition_S()
	{
		int toWin = GetPointsToWin();

		if (m_iPoints > toWin) m_iPoints = toWin;
		if (m_iPoints < -toWin) m_iPoints = -toWin;

		JWK.GetBattleManager().SetPoints(m_iPoints);

		if (m_iPoints > 0)
			m_iWinningFaction = JWK.GetFactions().GetPlayerFactionId();
		
		if (m_iPoints < 0)
		{
			// Fix 5: use safe index lookup instead of potentially missing GetFactionIndex()
			if (m_bIsInvaderBattle)
			{
				int invIdx = BM_GetInvaderFactionIndex();
				if (invIdx >= 0)
					m_iWinningFaction = invIdx;
				else
					m_iWinningFaction = JWK.GetFactions().GetEnemyFactionId();
			}
			else
			{
				m_iWinningFaction = JWK.GetFactions().GetEnemyFactionId();
			}
		}
		
		if (m_iPoints == 0)
			m_iWinningFaction = -1;

		if (m_iPoints >= toWin || m_iPoints <= -toWin)
			DoFinish_S();
	}
}

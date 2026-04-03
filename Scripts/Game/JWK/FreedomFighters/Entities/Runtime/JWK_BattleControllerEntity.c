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

[ComponentEditorProps(category: "JWK/FreedomFighters", description: "")]
class JWK_BattleControllerEntityClass: JWK_BaseEntityClass
{
}

class JWK_BattleControllerEntity: JWK_BaseEntity
{
	[Attribute("300")]
	protected int m_iDominationPointsRequired;

	[Attribute("0")]
	protected int m_iDefaultStartTimerSeconds;

	// ----------------------------------------------------------------------------------------------

	protected bool m_bIsActive;
	protected string m_sLocationName;
	protected int m_iWinningFaction = -1;

	// todo: add replication for battle phases and remove phase coding in points
	// right now points in range <0, 100) indicate deployment phase and <100, 200> indicate domination.
	// UI progress bar progress is mapped/colored accordingly to this assumption.
	protected int m_iPoints;

	// ----------------------------------------------------------------------------------------------

	protected static const int POINTS_INTERVAL_MS = 5000;
	protected static const int DOMINATION_INTERVAL_MS = 1000;

	static const int ENEMY_PRESENCE_RANGE = 100;
	static const int ENEMY_PRESENCE_RANGE_SQ = (ENEMY_PRESENCE_RANGE * ENEMY_PRESENCE_RANGE);

	// ---------------------------------------------------------------------------------------------

	protected int m_iStartDelayMs;

	protected ref JWK_AIForce m_AIForce;

	// -------------------

	protected int m_iEnemySpawnDistanceMin = 250;
	protected int m_iEnemySpawnDistanceMax = 800;

	// -------------------

	// (JWK_BattleControllerEntity controller, JWK_BattleSubjectComponent subject)
	protected ref ScriptInvoker m_OnBattleStarted_S;

	// (JWK_BattleControllerEntity controller, JWK_BattleSubjectComponent subject)
	protected ref ScriptInvoker m_OnBattleFinished_S;

	// (JWK_BattleControllerEntity controller
	protected ref ScriptInvoker m_OnWiped_S;

	protected ref JWK_AttackPlacementGenerator m_PlacementGenerator;

	protected ref array<vector> m_AttackSources = {};
	protected ref array<vector> m_AttackSourcesFillIn = {};

	protected bool m_bPlayersWereInArea;

	protected bool m_bDominationActive;
	protected int m_iDominationPoints;

	protected EntityID m_SubjectID;
	protected ref array<JWK_WorldZoneComponent> m_aPresenceZones;

	protected Faction m_AttackingFaction_S;

	protected ref JWK_BaseBattleAIGenerator m_EnemyGenerator_S;

	// BM: true when the attacking faction is the invader faction
	protected bool m_bIsInvaderBattle;

	// ---------------------------------------------------------------------------------------------

	bool IsActive_S()
	{
		return m_bIsActive;
	}

	bool IsDelayActive_S()
	{
		return (m_iStartDelayMs > 0);
	}

	string GetLocationName()
	{
		return m_sLocationName;
	}

	int GetWinningFactionID()
	{
		return m_iWinningFaction;
	}

	JWK_BaseBattleAIGenerator GetEnemyGenerator_S()
	{
		return m_EnemyGenerator_S;
	}

	bool IsInvaderBattle_S()
	{
		return m_bIsInvaderBattle;
	}

	// -------------------------------------------------------------------------------------------

	ScriptInvoker GetOnBattleStarted_S()
	{
		if (!m_OnBattleStarted_S) m_OnBattleStarted_S = new ScriptInvoker();
		return m_OnBattleStarted_S;
	}

	ScriptInvoker GetOnBattleFinished_S()
	{
		if (!m_OnBattleFinished_S) m_OnBattleFinished_S = new ScriptInvoker();
		return m_OnBattleFinished_S;
	}

	ScriptInvoker GetOnWiped_S()
	{
		if (!m_OnWiped_S) m_OnWiped_S = new ScriptInvoker();
		return m_OnWiped_S;
	}

	// -------------------------------------------------------------------------------------------

	void StopTimers()
	{
		ScriptCallQueue queue = JWK.GetGameplayTimeCallQueue();
		if (!queue) return;

		queue.Remove(PollStartTimer_S);
		queue.Remove(CheckUpdatePoints_S);
	}

	JWK_AIForce GetAIForce_S()
	{
		return m_AIForce;
	}

	JWK_BattleSubjectComponent GetSubject_S()
	{
		return JWK_CompTU<JWK_BattleSubjectComponent>.FindIn(m_SubjectID);
	}

	void SetSubject_S(JWK_BattleSubjectComponent subject)
	{
		m_SubjectID = subject.GetOwner().GetID();

		if (subject.m_iStartTimerSeconds < 0) m_iStartDelayMs = m_iDefaultStartTimerSeconds * 1000;
		else m_iStartDelayMs = subject.m_iStartTimerSeconds * 1000;

		JWK_NamedLocationComponent namedLocation = JWK_CompTU<JWK_NamedLocationComponent>
			.FindIn(subject.GetOwner());

		if (namedLocation) m_sLocationName = namedLocation.GetName();
		else m_sLocationName = "";

		m_EnemyGenerator_S = subject.GetEnemyGenerator();

		InitPresenceZones_S(subject.GetOwner());
	}

	void SetAttackingFaction_S(Faction faction)
	{
		m_AttackingFaction_S = faction;
	}

	Faction GetAttackingFaction_S()
	{
		return m_AttackingFaction_S;
	}

	void SetPoints_S(int points)
	{
		if (m_iPoints == points) return;

		m_iPoints = points;
		JWK.GetBattleManager().SetPoints(m_iPoints);
		CheckFinishCondition_S();
	}

	int GetPointsToWin()
	{
		return 200;
	}

	// ---------------------------------------------------------------------------------------------

	void Start_S()
	{
		if (!Replication.IsServer()) return;

		m_bIsActive = true;
		JWK_BattleSubjectComponent subject = GetSubject_S();

		m_AIForce = new JWK_AIForce();
		m_AIForce.m_Log.m_Prefix = "Battle";
		m_AIForce.m_bAutoUnstuck = true;
		m_AIForce.GetOnGroupAttached().Insert(OnAIForceGroupAttached_S);
		m_AIForce.GetOnCrewedVehicleAttached().Insert(OnAIForceCrewedVehicleAttached_S);

		m_PlacementGenerator = new JWK_AttackPlacementGenerator();
		m_PlacementGenerator.SetTarget(GetOrigin());
		m_PlacementGenerator.SetMinimumDistance(m_iEnemySpawnDistanceMin);
		m_PlacementGenerator.SetMaximumDistance(m_iEnemySpawnDistanceMax);
		m_PlacementGenerator.SetIncludeRoads(subject.m_bEnemyAllowVehicles);
		m_PlacementGenerator.RunAsync();

		JWK.GetGameplayTimeCallQueue().CallLater(PollStartTimer_S, 1000, true);
		JWK.GetGameplayTimeCallQueue().CallLater(CheckUpdatePoints_S, POINTS_INTERVAL_MS, true);

		if (m_OnBattleStarted_S)
			m_OnBattleStarted_S.Invoke(this, subject);

		subject.DoBattleStart_S(this);

		SetupEnemyGenerator_S();

		if (subject.m_iStartNotification != 0) {
			JWK.GetNotifications().BroadcastNotification_S(
				subject.m_iStartNotification, m_sLocationName
			);
		}

		JWK_Log.Log(this, string.Format(
			"Battle deployment delay: %1s.", m_iStartDelayMs / 1000
		));
	}

	void Wipe_S()
	{
		m_AIForce.ForceDeleteAllUnits();

		if (m_OnWiped_S) m_OnWiped_S.Invoke(this);
	}

	void ForceFinish_S(int winnerFactionID)
	{
		m_iWinningFaction = winnerFactionID;
		DoFinish_S();
	}

	// ---------------------------------------------------------------------------------------------

	void JWK_BattleControllerEntity(IEntitySource src, IEntity parent)
	{
		if (!GetGame().InPlayMode()) return;

		SetEventMask(EntityEvent.INIT);
	}

	void ~JWK_BattleControllerEntity()
	{
		if (Replication.IsServer())
			StopTimers();
	}

	protected void InitPresenceZones_S(IEntity holder)
	{
		JWK_WorldZoneControllerComponent zoneController =
			JWK_CompTU<JWK_WorldZoneControllerComponent>.FindIn(holder);
		m_aPresenceZones = {};

		foreach (JWK_WorldZoneComponent zone : zoneController.GetZones_S()) {
			if (!zone || !zone.HasTag(JWK_EWorldZoneTag.BATTLE_AREA)) continue;

			m_aPresenceZones.Insert(zone);
		}

		if (m_aPresenceZones.IsEmpty())
			JWK_Log.Log(this, "No presence zones found! Fallback method will be used.", LogLevel.WARNING);
		else
			JWK_Log.Log(this, "Found " + m_aPresenceZones.Count() + " presence zones.");
	}

	protected void SetupEnemyGenerator_S()
	{
		if (!m_EnemyGenerator_S) {
			JWK_Log.Log(this, "No enemy generator to use!", LogLevel.ERROR);
			return;
		}

		m_bIsInvaderBattle = BM_DetectInvaderBattle_S();

		// Fix 1: for invader battles the enemy force is managed entirely by BM_InvasionManager.
		// Do not start the FF wave generator — scoring is handled by CheckUpdatePoints_Invader_S.
		if (m_bIsInvaderBattle) {
			JWK_Log.Log(this, "Invader battle detected — wave generator suppressed.");
			return;
		}

		int budget = AllocateEnemyBudget_S();
		JWK_Log.Log(this, "Final enemy budget: " + budget.ToString() + ".");

		// Note: this may break if multiple battles are to be handled at the same time
		// It could be the same m_EnemyGenerator_S instance for multiple battle subjects.
		m_EnemyGenerator_S.SetController(this);
		m_EnemyGenerator_S.SetSubject(GetSubject_S());
		m_EnemyGenerator_S.SetAIForce(m_AIForce);
		m_EnemyGenerator_S.SetPlacementGenerator(m_PlacementGenerator);
		m_EnemyGenerator_S.InitBudget(budget);

		m_EnemyGenerator_S.StartBattle();
	}

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

	protected void DoFinish_S()
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

	protected void PollStartTimer_S()
	{
		m_iStartDelayMs = Math.Max(0, m_iStartDelayMs - 1000);
	}

	protected void CheckUpdatePoints_S()
	{
		if (m_iStartDelayMs > 0) return;

		// Fix 1: invader battles score from BM invader groups, not the FF wave system
		if (m_bIsInvaderBattle) {
			CheckUpdatePoints_Invader_S();
			return;
		}

		int enemyNum = JWK_AIUtils.GetAIForceAgentsCountInArea(
			m_AIForce, GetOrigin(), ENEMY_PRESENCE_RANGE
		);

		int enemyTotal = m_AIForce.CountAgents();
		bool allSpawned = (m_AIForce.m_iPendingSpawnRequests == 0);

		int totalPlayers;
		int playerNum = GetPlayerCountInZone_S(totalPlayers);
		int newPoints = m_iPoints;

		if (playerNum == 0) {
			if (m_bPlayersWereInArea) {
				newPoints = -GetPointsLoseRate(totalPlayers);
			} else {
				newPoints = newPoints - GetPointsLoseRate(totalPlayers);
			}

		} else {
			// win immediately if all enemies are dead and there cant be more
			if (enemyTotal == 0 && allSpawned && m_EnemyGenerator_S.GetBudgetLeft() <= 0) {
				newPoints = GetPointsToWin();

			// enemy is still deploying its forces
			} else if (m_EnemyGenerator_S.GetBudgetLeft() > 0) {
				float resourceProgress = m_EnemyGenerator_S.GetBudgetUsedFactor();
				newPoints = resourceProgress * 100;

				JWK_Log.Log(this, string.Format(
					"Budget progress: %1 (%2/%3).",
					resourceProgress, m_EnemyGenerator_S.GetBudgetUsed(), m_EnemyGenerator_S.GetBudget()
				));

			// units may be still spawning, but there wont be more deployed
			} else {
				m_bDominationActive = true;

				float dominationProgress = (m_iDominationPoints / m_iDominationPointsRequired);
				newPoints = 100 + dominationProgress * 100;

				JWK_Log.Log(this, string.Format(
					"Domination progress: %1/%2 (total: %3). Gaining: %4",
					m_iDominationPoints,
					m_iDominationPointsRequired,
					newPoints,
					(playerNum >= enemyNum)
				));
			}
		}

		if (m_bDominationActive) {
			if (GetWeightedPlayerPresence(playerNum, totalPlayers) >= enemyNum) {
				m_iDominationPoints += POINTS_INTERVAL_MS / 1000;
			}
		}

		m_bPlayersWereInArea = (playerNum > 0);

		JWK_Log.Log(this, string.Format(
			"Enemy: %1 (total %2, groups %3), players: %4. Points: %5/%6.",
			enemyNum, enemyTotal, m_AIForce.GetGroups().Count(), playerNum, newPoints, GetPointsToWin()
		));

		SetPoints_S(newPoints);
	}

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

	protected int GetPointsLoseRate(int totalPlayers)
	{
		return Math.Min(10, totalPlayers);
	}

	protected int GetWeightedPlayerPresence(int playersPresent, int totalPlayers)
	{
		if (playersPresent == 0) return playersPresent;

		// help the players if there are less than 4, so they progress domination even if 1-2 AIs are hidden somewhere
		if (totalPlayers < 4) return Math.Max(4, playersPresent);

		// otherwise if there are many, they should just all be present
		return playersPresent;
	}

	protected void CheckFinishCondition_S()
	{
		int toWin = GetPointsToWin();

		if (m_iPoints > toWin) m_iPoints = toWin;
		if (m_iPoints < -toWin) m_iPoints = -toWin;

		JWK.GetBattleManager().SetPoints(m_iPoints);

		if (m_iPoints > 0) m_iWinningFaction = JWK.GetFactions().GetPlayerFactionId();
		if (m_iPoints < 0) {
			// Fix 5: use safe index lookup instead of potentially missing GetFactionIndex()
			if (m_bIsInvaderBattle) {
				int invIdx = BM_GetInvaderFactionIndex();
				if (invIdx >= 0) m_iWinningFaction = invIdx;
				else m_iWinningFaction = JWK.GetFactions().GetEnemyFactionId();
			} else {
				m_iWinningFaction = JWK.GetFactions().GetEnemyFactionId();
			}
		}
		if (m_iPoints == 0) m_iWinningFaction = -1;

		if (m_iPoints >= toWin || m_iPoints <= -toWin)
			DoFinish_S();
	}

	protected int GetPlayerCountInZone_S(out int outTotalPlayers)
	{
		array<int> players = {};
		PlayerManager mgr = GetGame().GetPlayerManager();
		mgr.GetPlayers(players);
		int result = 0;

		foreach (int playerID : players) {
			IEntity entity = mgr.GetPlayerControlledEntity(playerID);
			if (!entity) continue;

			result += CheckIsEntityInPresenceZone(entity);
		}

		outTotalPlayers = players.Count();
		return result;
	}

	protected bool CheckIsEntityInPresenceZone(IEntity entity)
	{
		if (m_aPresenceZones.IsEmpty()) {
			float distanceSq = vector.DistanceSqXZ(entity.GetOrigin(), GetOrigin());
			return (distanceSq < ENEMY_PRESENCE_RANGE_SQ);
		}

		foreach (JWK_WorldZoneComponent zone : m_aPresenceZones) {
			if (zone.Contains(entity.GetOrigin())) return true;
		}

		return false;
	}

	protected int AllocateEnemyBudget_S()
	{
		JWK_GameSettingsCache gameSettings = JWK.GameSettingsCache();

		// todo: unhardcode, use curves and difficulty settings
		// todo: move to a separate handler class attached via Attribute() to battle manager/subject, so this can
		// be modded or customized by map makers
		//
		// Right now, this translates to 15 enemies + X per online player.
		// X is influenced by threat, and its range depends on number of players online. This is to give a significant
		// scaling/difficulty curve in single player, but not go through the roof in MP (esp. if played with bigger
		// group than gamemode is designed for).
		// This will probably need a redesign once friendly AI is in.

		const int playerCount = gameSettings.GetBalancePlayerCount();
		const float progressFactor = gameSettings.GetBalanceProgress();

		float minThreatBudget, maxThreatBudget;
		GetPerPlayerBudgetRange(playerCount, minThreatBudget, maxThreatBudget);

		JWK_Log.Log(this, string.Format(
			"Per player budget range <%1, %2> * %3, progress factor: %4.",
			minThreatBudget.ToString(-1, 2),
			maxThreatBudget.ToString(-1, 2),
			JWK_BaseBattleAIGenerator.CharacterBudgetCost.ToString(-1, 2),
			progressFactor.ToString(-1, 2)
		));

		minThreatBudget *= JWK_BaseBattleAIGenerator.CharacterBudgetCost;
		maxThreatBudget *= JWK_BaseBattleAIGenerator.CharacterBudgetCost;

		float budgetPerPlayer = minThreatBudget + (maxThreatBudget-minThreatBudget) * progressFactor;
		float budget = (JWK_BaseBattleAIGenerator.BaseBudget + budgetPerPlayer * playerCount);

		JWK_Log.Log(this, string.Format(
			"Budget: %1 base + %2 per player * %3 players.",
			JWK_BaseBattleAIGenerator.BaseBudget, budgetPerPlayer.ToString(-1, 2), playerCount
		));

		// hard limit to 200 enemies at most
		return Math.ClampInt(
			budget * gameSettings.m_fBattleEnemyBudgetMultiplier,
			JWK_BaseBattleAIGenerator.BaseBudget,
			JWK_BaseBattleAIGenerator.BattleBudgetCap
		);
	}

	// Note: this is scaled afterwards by JWK_BaseBattleAIGenerator.CharacterBudgetCost.
	// This is done only for readability with current implementation.
	protected void GetPerPlayerBudgetRange(int playerCount, out float min, out float max)
	{
		// 5 to 35 enemies total
		if (playerCount == 1) {
			min = 5;
			max = 35;

		// 10 to 40 enemies total
		} else if (playerCount == 2) {
			min = 5;
			max = 20;

		// 15 to 45 enemies total
		} else if (playerCount == 3) {
			min = 5;
			max = 15;

		// 20 to 56 enemies total
		} else if (playerCount == 4) {
			min = 5;
			max = 14;

		// 30 to 65 enemies total
		} else if (playerCount == 5) {
			min = 6;
			max = 13;

		// 42 to 72 enemies total
		} else if (playerCount == 6) {
			min = 7;
			max = 12;

		// 49 to 84 enemies total
		} else if (playerCount == 7) {
			min = 7;
			max = 12;

		// 64+ to 96+ enemies total
		} else if (playerCount >= 8) {
			min = 8;
			max = 12;
		}
	}

	// ----------------------------------------------------------------------------------------------------

	// todo: split to separate JWK_BattleAICommander component or handler classes, preferably override-able by
	// battle subject, so a specific capturable location may specify custom enemy commander logic
	protected void OnAIForceGroupAttached_S(JWK_AIForce force, SCR_AIGroup aiGroup)
	{
		JWK_AIGroupComponent aiGroupComp = JWK_CompTU<JWK_AIGroupComponent>.FindIn(aiGroup);
		if (aiGroupComp && aiGroupComp.HasAssignedVehicle_S()) return;

		JWK_AIUtils.AddAttackAndSweepWaypoints(aiGroup, GetOrigin());
	}

	protected void OnAIForceCrewedVehicleAttached_S(JWK_AIForce force, JWK_CrewedVehicle crewedVehicle)
	{
		JWK_AIGroupComponent crewGroupComp = JWK_CompTU<JWK_AIGroupComponent>.FindIn(crewedVehicle.m_Crew);
		crewGroupComp.CrewReleaseDismountsAt_S(GetOrigin(), 40);

		JWK_CrewedVehicleComponent vehicleComp =
			JWK_CompTU<JWK_CrewedVehicleComponent>.FindIn(crewedVehicle.m_Vehicle);

		vehicleComp.GetOnGroupDismounted_S().Insert(OnVehicleGroupDismounted_S);
	}

	protected void OnVehicleGroupDismounted_S(JWK_CrewedVehicleComponent vehicle, SCR_AIGroup group)
	{
		JWK_AIUtils.AddAttackAndSweepWaypoints(group, group.GetCenterOfMass(), 80);
	}
}

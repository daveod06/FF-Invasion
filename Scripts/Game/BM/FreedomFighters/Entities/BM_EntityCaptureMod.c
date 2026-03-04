modded class JWK_BattleSubjectComponent
{
	override protected float GetPlayersWeightForAutostart(array<JWK_WorldZoneComponent> zones)
	{
		float playerWeight = super.GetPlayersWeightForAutostart(zones);
		
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return playerWeight;
		
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return playerWeight;
		
		int invaderCount = 0;
		JWK_AIForce force = gm.BM_GetInvaderForce();
		if (force) {
			invaderCount = JWK_AIUtils.GetAIForceAgentsCountInArea(force, GetOwner().GetOrigin(), 150);
		}
		
		return playerWeight + GetScaledPlayersPresenceWeight(invaderCount);
	}
}

modded class JWK_BattleControllerEntity
{
	// Remove the buggy logic that added invaders to the player count!
	override protected int GetPlayerCountInZone_S(out int outTotalPlayers)
	{
		return super.GetPlayerCountInZone_S(outTotalPlayers);
	}

	// Override the point update logic so Invaders can actually win battles they start
	override protected void CheckUpdatePoints_S()
	{
		super.CheckUpdatePoints_S();

		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return;

		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return;

		JWK_BattleSubjectComponent subject = GetSubject_S();
		if (!subject || !subject.GetOwner()) return;

		// Only apply this logic if this is the active invasion target
		if (gm.BM_GetCurrentTargetID() != subject.GetOwner().GetID()) return;

		JWK_AIForce force = gm.BM_GetInvaderForce();
		if (!force) return;

		int invaderCount = JWK_AIUtils.GetAIForceAgentsCountInArea(force, GetOrigin(), ENEMY_PRESENCE_RANGE);
		if (invaderCount == 0) return;

		// Count Defenders (Players + Rebel AI)
		int actualDefenderCount = 0;
		array<int> players = {};
		PlayerManager mgr = GetGame().GetPlayerManager();
		mgr.GetPlayers(players);
		foreach (int playerID : players) {
			IEntity playerEnt = mgr.GetPlayerControlledEntity(playerID);
			if (playerEnt && CheckIsEntityInPresenceZone(playerEnt)) {
				actualDefenderCount++;
			}
		}
		
		JWK_AIForceComponent garrisonForceComp = JWK_CompTU<JWK_AIForceComponent>.FindIn(subject.GetOwner());
		if (garrisonForceComp && garrisonForceComp.GetForce_S()) {
			actualDefenderCount += JWK_AIUtils.GetAIForceAgentsCountInArea(garrisonForceComp.GetForce_S(), GetOrigin(), ENEMY_PRESENCE_RANGE);
		}

		// If Invaders have completely wiped out the defenders, force an immediate win!
		if (actualDefenderCount == 0) {
			Print("BM_Invasion: Invaders have wiped out all defenders at " + subject.GetOwner().GetPrefabData().GetPrefabName() + ". Forcing capture!", LogLevel.NORMAL);
			int invaderFactionId = GetGame().GetFactionManager().GetFactionIndex(GetGame().GetFactionManager().GetFactionByKey(invaderKey));
			ForceFinish_S(invaderFactionId);
		} else {
			// If fighting is still happening, manually tick points down towards Invader win
			int toWin = GetPointsToWin();
			m_iPoints -= (GetPointsLoseRate(players.Count()) + 1); // Extra weight for invaders
			if (m_iPoints < -toWin) m_iPoints = -toWin;
			SetPoints_S(m_iPoints);
			
			if (m_iPoints <= -toWin) {
				int invaderFactionId = GetGame().GetFactionManager().GetFactionIndex(GetGame().GetFactionManager().GetFactionByKey(invaderKey));
				ForceFinish_S(invaderFactionId);
			}
		}
	}

	override int GetWinningFactionID()
	{
		int winner = super.GetWinningFactionID();

		// CUSTOM BAKERMODS LOGIC: Did Invaders actually win this?
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return winner;
		
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return winner;

		// --- NEW LOGIC: Robust Invader Win Check ---
		// If the base FF logic awarded the win to the default Enemy, but this base 
		// is the active Invasion Target, force the winner to be the Invaders!
		JWK_BattleSubjectComponent subject = GetSubject_S();
		if (subject && subject.GetOwner() && gm.BM_GetCurrentTargetID() == subject.GetOwner().GetID()) {
			if (winner == JWK.GetFactions().GetEnemyFactionId()) {
				return GetGame().GetFactionManager().GetFactionIndex(GetGame().GetFactionManager().GetFactionByKey(invaderKey));
			}
		}

		JWK_AIForce force = gm.BM_GetInvaderForce();
		if (!force) return winner;

		int invaderCount = JWK_AIUtils.GetAIForceAgentsCountInArea(force, GetOrigin(), ENEMY_PRESENCE_RANGE);
		if (invaderCount == 0) return winner;

		// --- IMPROVED LOGIC: Count both Players AND Rebel AI defenders ---
		int actualDefenderCount = 0;
		
		// 1. Count Human Players
		array<int> players = {};
		PlayerManager mgr = GetGame().GetPlayerManager();
		mgr.GetPlayers(players);
		foreach (int playerID : players) {
			IEntity playerEnt = mgr.GetPlayerControlledEntity(playerID);
			if (playerEnt && CheckIsEntityInPresenceZone(playerEnt)) {
				actualDefenderCount++;
			}
		}
		
		// 2. Count local Garrison AI (Rebel Defenders)
		if (subject && subject.GetOwner()) {
			JWK_AIForceComponent garrisonForceComp = JWK_CompTU<JWK_AIForceComponent>.FindIn(subject.GetOwner());
			if (garrisonForceComp && garrisonForceComp.GetForce_S()) {
				actualDefenderCount += JWK_AIUtils.GetAIForceAgentsCountInArea(garrisonForceComp.GetForce_S(), GetOrigin(), ENEMY_PRESENCE_RANGE);
			}
		}

		if (invaderCount > actualDefenderCount) {
			return GetGame().GetFactionManager().GetFactionIndex(GetGame().GetFactionManager().GetFactionByKey(invaderKey));
		}

		return winner;
	}
}

modded class JWK_TownEntity
{
	override protected void OnBattleFinished(JWK_BattleSubjectComponent subject, JWK_BattleControllerEntity controller)
	{
		int currentFactionId = m_FactionControl.GetFactionId();
		int winningFactionId = controller.GetWinningFactionID();
		
		// CUSTOM BAKERMODS LOGIC: Handle Invader win
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return;
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty()) return;

		int invaderFactionId = GetGame().GetFactionManager().GetFactionIndex(GetGame().GetFactionManager().GetFactionByKey(invaderKey));

		// Standard FF logic
		if (winningFactionId != invaderFactionId && (winningFactionId == JWK.GetFactions().GetPlayerFactionId() || winningFactionId == JWK.GetFactions().GetEnemyFactionId()))
		{
			super.OnBattleFinished(subject, controller);
			return;
		}
		
		if (winningFactionId == invaderFactionId)
		{
			m_FactionControl.ChangeControlToFactionId(winningFactionId);
			m_HeartsAndMinds.ResetSupport();

			// Fallback to hints instead of notifications to avoid enum errors
			string msg = "The Invaders (" + invaderKey + ") have captured the town of " + m_NamedLocation.GetName() + "!";
			JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);
			
			Print("BM_Invasion: Town " + m_NamedLocation.GetName() + " captured by Invaders (" + invaderKey + ")!", LogLevel.NORMAL);
		}
		else
		{
			super.OnBattleFinished(subject, controller);
		}
	}
}

modded class JWK_MilitaryBaseEntity
{
	override protected void OnBattleFinished(JWK_BattleSubjectComponent subject, JWK_BattleControllerEntity controller)
	{
		int currentFactionId = m_FactionControl.GetFactionId();
		int winningFactionId = controller.GetWinningFactionID();
		
		// CUSTOM BAKERMODS LOGIC: Handle Invader win
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return;
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty()) return;

		int invaderFactionId = GetGame().GetFactionManager().GetFactionIndex(GetGame().GetFactionManager().GetFactionByKey(invaderKey));

		// Standard FF logic
		if (winningFactionId != invaderFactionId && (winningFactionId == JWK.GetFactions().GetPlayerFactionId() || winningFactionId == JWK.GetFactions().GetEnemyFactionId()))
		{
			super.OnBattleFinished(subject, controller);
			return;
		}
		
		if (winningFactionId == invaderFactionId)
		{
			m_FactionControl.ChangeControlToFactionId(winningFactionId);
			
			string msg = "The Invaders (" + invaderKey + ") have seized control of the Military Base at " + m_NamedLocation.GetName() + "!";
			JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);
			
			Print("BM_Invasion: Military Base " + m_NamedLocation.GetName() + " captured by Invaders (" + invaderKey + ")!", LogLevel.NORMAL);
		}
		else
		{
			super.OnBattleFinished(subject, controller);
		}
	}
}

modded class JWK_TerritoryControlMapModule
{
	override void Draw()
	{
		super.Draw();
		
		// CUSTOM BAKERMODS LOGIC: Add a third draw call for the Invader faction frontlines
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return;
		
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return;
		
		Faction invaderFaction = GetGame().GetFactionManager().GetFactionByKey(invaderKey);
		if (!invaderFaction) return;

		int frontlinesAlpha = m_iFrontlineAlpha;
		if (m_bFullMode) frontlinesAlpha = m_iFullModeAlpha;

		// We need to find the shared vertex list. Since it's local in Draw(), 
		// we'll have to basically re-run the logic or find a way to tap into it.
		// For now, let's just add a dedicated draw call for Invaders.
		
		TriMeshDrawCommand drawCallInv = new TriMeshDrawCommand();
		drawCallInv.m_iColor = (invaderFaction.GetFactionColor().PackToInt() & 0x00FFFFFF) | (frontlinesAlpha << 24);
		drawCallInv.m_Vertices = {};
		drawCallInv.m_Indices = {};
		drawCallInv.m_pTexture = m_Contested1Tex; // Re-use the contested texture
		
		float mapZoom = m_MapEntity.GetCurrentZoom();
		drawCallInv.m_fUVScale = m_fFrontlineTextureUvScale / mapZoom;
		
		array<EntityID> nodes = JWK_IndexSystem.Get(GetWorld()).GetAll(JWK_TerritoryControlNodeComponent);
		foreach (EntityID id : nodes) {
			JWK_TerritoryControlNodeComponent node = JWK_CompTU<JWK_TerritoryControlNodeComponent>.FindIn(id);
			if (!node) continue;
			
			// Re-draw nodes that belong to the invader into our special draw call
			Faction nodeFaction;
			FactionAffiliationComponent affil = JWK_CompTU<FactionAffiliationComponent>.FindIn(node.GetOwner());
			if (affil) nodeFaction = affil.GetAffiliatedFaction();
			
			if (nodeFaction == invaderFaction) {
				DrawNode(node, drawCallInv);
			}
		}
		
		if (drawCallInv.m_Indices.Count() > 2) {
			m_aCommands.Insert(drawCallInv);
		}
	}
}

modded class JWK_FactionControlComponent
{
	override void ChangeControlToFaction(Faction faction)
	{
		super.ChangeControlToFaction(faction);
		
		// CUSTOM BAKERMODS LOGIC: Update the linked territory node
		// Without this, the map UI territory lines won't update correctly.
		JWK_TerritoryControllableComponent controllable = JWK_CompTU<JWK_TerritoryControllableComponent>.FindIn(GetOwner());
		if (controllable) {
			EntityID nodeId = controllable.GetNodeID();
			if (nodeId != EntityID.INVALID) {
				SCR_FactionAffiliationComponent nodeAffil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(nodeId);
				if (nodeAffil) {
					nodeAffil.SetAffiliatedFaction(faction);
				}
			}
		}
	}

	override bool IsEnemyFaction()
	{
		// Let the native logic check first
		if (super.IsEnemyFaction()) return true;
		
		// CUSTOM BAKERMODS LOGIC: If this base belongs to the Invaders, it counts as an ENEMY base
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (gm && gm.BM_GetInvaderFactionKey() == GetFactionKey()) {
			return true;
		}
		
		return false;
	}
}

modded class JWK_AIGarrisonComponent
{
	override protected void InitForce_S(JWK_StreamableAIForce aiForce, int forceSize)
	{
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		string invaderKey = "";
		if (gm) invaderKey = gm.BM_GetInvaderFactionKey();
		
		// CUSTOM BAKERMODS LOGIC: If this is an Invader base, spawn Invaders using their specific key
		if (!invaderKey.IsEmpty() && invaderKey == m_FactionControl.GetFactionKey())
		{
			// FORCE 2 SQUADS: Ensure at least 16-20 units for a proper guard
			int minGuardSize = 16;
			if (forceSize < minGuardSize) forceSize = minGuardSize;

			Print("BM_Invasion: Initializing persistent garrison at " + GetOwner().GetName() + " for faction " + invaderKey + " (Size: " + forceSize + ")", LogLevel.NORMAL);
			
			JWK_FactionForceInfantryComposition forceComposition = 
				JWK_CombatFactionTrait.GetForceCompositionByKey(invaderKey)
					.GenerateInfantry(
						forceSize, forceSize,
						intent: m_iCompositionIntent,
						threat: JWK.GetWorldThreat().GetLevelAt(GetOwner().GetOrigin())
					);
			
			if (!forceComposition) {
				Print("BM_Invasion: ERROR: Failed to generate infantry composition for " + invaderKey, LogLevel.ERROR);
				return;
			}
			
			for(int i = 0, n = forceComposition.GetGroupsCount(); i < n; i++) {
				JWK_AISpawnRequest spawnRequest = new JWK_AISpawnRequest();
				forceComposition.ApplyToSpawnRequest(i, spawnRequest);
				
				spawnRequest.m_AIForce = aiForce;
				JWK.GetAIForceManager().AddSpawnQueueItem_S(spawnRequest);
			}
			return;
		}
		
		// Fallback to native logic for regular factions
		super.InitForce_S(aiForce, forceSize);
	}

	override protected void ConfigureSpawnedGroup_S(SCR_AIGroup aiGroup)
	{
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		string invaderKey = "";
		if (gm) invaderKey = gm.BM_GetInvaderFactionKey();

		// BM_FIX: If this is an Invader-held base, force a DEFEND waypoint at the center
		if (!invaderKey.IsEmpty() && invaderKey == m_FactionControl.GetFactionKey())
		{
			// Clear any existing waypoints
			array<AIWaypoint> currentWaypoints = {};
			aiGroup.GetWaypoints(currentWaypoints);
			foreach (AIWaypoint wp : currentWaypoints) aiGroup.RemoveWaypoint(wp);

			AIWaypoint defend = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.DEFEND, GetOwner().GetOrigin());
			aiGroup.AddWaypoint(defend);
			
			Print("BM_Invasion: Assigned hard-defend waypoint to Invader garrison squad at " + GetOwner().GetName(), LogLevel.NORMAL);
			return;
		}

		// Otherwise use normal patrol logic
		super.ConfigureSpawnedGroup_S(aiGroup);
	}
}

modded class JWK_ReinforcementsQrfControllerEntity
{
	override protected JWK_CombinedFactionForceComposition CreateForceComposition()
	{
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return super.CreateForceComposition();

		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return super.CreateForceComposition();

		// Check if the target location is actually an Invader-held base
		bool isInvaderTarget = false;
		array<EntityID> bases = {};
		bases.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity));
		bases.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity));
		
		foreach (EntityID id : bases) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			if (ent && vector.Distance(ent.GetOrigin(), GetOrigin()) < 150) {
				SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
				if (fac && fac.GetAffiliatedFactionKey() == invaderKey) {
					isInvaderTarget = true;
					break;
				}
			}
		}

		if (isInvaderTarget)
		{
			Print("BM_Invasion: Intercepted QRF request for Invader-held base. Hijacking faction to " + invaderKey, LogLevel.NORMAL);
			
			JWK_FactionForceCompositionGenerationRequest request = new JWK_FactionForceCompositionGenerationRequest();
			
			// Match original logic but with Invader faction
			if (m_iCompositionIntent == JWK_EFactionForceCompositionIntent.GENERIC) {
				if (m_fThreat > 0.8) request.intent = JWK_EFactionForceCompositionIntent.QRF_SOF;
				else request.intent = JWK_EFactionForceCompositionIntent.QRF_REGULAR;
			} else {
				request.intent = m_iCompositionIntent;
			}
			
			request.manpowerAbsMin = m_iManpowerMin;
			request.manpowerMax = m_iManpowerMax;
			request.threat = m_fThreat;
			request.useVehicles = true; // Always allow vehicles for Invader QRF
			request.mechanizedManpowerShare = 0.5; // High mechanized share
			request.allowLightArmor = true;
			request.allowHeavyArmor = true; // Enable Tanks/Heavy support
			
			JWK_FactionForceCompositionGenerator gen = JWK_CombatFactionTrait.GetForceCompositionByKey(invaderKey);
			if (gen) {
				Print("BM_Invasion: Spawning Heavy Invader QRF (Armor: Enabled)", LogLevel.NORMAL);
				return gen.Generate(request);
			}
		}

		return super.CreateForceComposition();
	}
}

modded class JWK_WaveBattleAIGenerator
{
	override protected int GenerateWave(int budget, float threat)
	{
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return super.GenerateWave(budget, threat);

		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return super.GenerateWave(budget, threat);

		// Check if this battle is for an Invader-owned base
		IEntity subjectEnt = GetGame().GetWorld().FindEntityByID(m_Subject.GetOwner().GetID());
		SCR_FactionAffiliationComponent subjectFac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(subjectEnt);
		
		if (subjectFac && subjectFac.GetAffiliatedFactionKey() == invaderKey)
		{
			if (budget < CharacterBudgetCost) return 0;
			
			const int manpower = budget / CharacterBudgetCost;
			const JWK_BattleAIWave wave = new JWK_BattleAIWave();
			
			const JWK_FactionForceCompositionGenerationRequest request = new JWK_FactionForceCompositionGenerationRequest();
			request.intent = JWK_EFactionForceCompositionIntent.BATTLE;
			request.manpowerAbsMin = manpower;
			request.manpowerMax = manpower;
			request.threat = threat;
			request.maxHeavyArmedVehicles = 3; // INCREASED: Heavy Armor support
			request.maxLightArmedVehicles = 4;
			request.maxVehicles = 6;
			request.ignoreCustomMultiplier = true;
			request.useVehicles = (!m_PlacementContext.m_CurrentBag.GetTargetRoadPoints().IsEmpty() && m_Subject.m_bEnemyAllowVehicles);
			
			JWK_FactionForceCompositionGenerator gen = JWK_CombatFactionTrait.GetForceCompositionByKey(invaderKey);
			if (gen) {
				Print("BM_Invasion: Generating Battle Wave for Invader-held base using faction " + invaderKey + " (Manpower: " + manpower + ")", LogLevel.NORMAL);
				wave.m_ForceComposition = gen.Generate(request);
				m_aWaves.Insert(wave);
				return Math.Min(budget, wave.m_ForceComposition.GetManpower()) * CharacterBudgetCost;
			}
		}

		return super.GenerateWave(budget, threat);
	}
}

modded class JWK_OutpostCompositionsControllerComponent
{
	override void DoSpawnComps()
	{
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		string invaderKey = "";
		if (gm) invaderKey = gm.BM_GetInvaderFactionKey();
		
		// CUSTOM BAKERMODS LOGIC: Handle Invader compositions specifically
		if (!invaderKey.IsEmpty() && invaderKey == m_FactionControl.GetFactionKey())
		{
			JWK_CombatFactionTrait combatFaction = JWK_FactionTraitTU<JWK_CombatFactionTrait>.GetByKey(invaderKey);
			if (!combatFaction) {
				// FACTION RECOVERY: Try to force load it if missing
				JWK_FactionManager.Cast(GetGame().GetFactionManager()).BM_LoadFaction(invaderKey);
				combatFaction = JWK_FactionTraitTU<JWK_CombatFactionTrait>.GetByKey(invaderKey);
			}

			if (!combatFaction) return;
			
			foreach (JWK_OutpostCompositionSlot compSlot : m_Comps) {
				IEntity slotEntity = GetGame().GetWorld().FindEntityByID(compSlot.slotID);
				if (!slotEntity) continue;
				
				ResourceName prefab = GetPrefabForSlot(combatFaction, compSlot.type);
				
				// FALLBACK: If the custom faction has NO defenses defined, FF framework fails.
				// We force-map them to basic fortification types if they are null.
				if (prefab == ResourceName.Empty) {
					// Pull from the ENEMY role (USSR) as a visual fallback so the base isn't empty
					JWK_CombatFactionTrait enemyTrait = JWK_FactionTraitTU<JWK_CombatFactionTrait>.GetByRole(JWK_EFactionRole.ENEMY);
					if (enemyTrait) prefab = GetPrefabForSlot(enemyTrait, compSlot.type);
				}

				if (prefab == ResourceName.Empty) continue;
				if (compSlot.compID != EntityID.INVALID) continue;
				
				compSlot.compID = SpawnComposition(slotEntity, prefab).GetID();
				m_WorldSlots.MarkSlotInUse(compSlot.slotID);
			}
			return;
		}
		
		super.DoSpawnComps();
	}
}

modded class JWK_WorldZoneControllerComponent
{
	override protected void RefreshZones_S(JWK_WorldZoneComponent onlyZone = null)
	{
		// Let the native logic run first
		super.RefreshZones_S(onlyZone);
		
		// CUSTOM BAKERMODS LOGIC: Ensure Invaders are treated as "Illegal/Enemy" zones
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return;
		
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return;
		
		Faction invaderFaction = GetGame().GetFactionManager().GetFactionByKey(invaderKey);
		if (!invaderFaction) return;

		if (!m_FactionAffiliation) return;

		Faction myFaction = m_FactionAffiliation.GetAffiliatedFaction();
		if (myFaction != invaderFaction) return;

		// If we are here, this base is Invader-held. We MUST force the zones to be illegal.
		if (onlyZone) {
			if (CanConfigureZone_S(onlyZone)) {
				onlyZone.m_bIsIllegal = true;
				onlyZone.Commit();
			}
			return;
		}

		if (!m_aZones_S) return;

		foreach (JWK_WorldZoneComponent zone : m_aZones_S) {
			if (zone && CanConfigureZone_S(zone)) {
				zone.m_bIsIllegal = true;
				zone.Commit();
			}
		}
	}
}

modded class JWK_EntityGarbageCollectorSystem
{
	// FALLBACK FLAG: Set this to true if the new targeted streaming logic fails to keep invaders alive
	// or fails to wake up defenders, and you need to revert to tricking the global player list.
	static bool m_bBM_UseGCHackFallback = false;

	override protected void InitPlayersData()
	{
		super.InitPlayersData();
		
		if (!m_bBM_UseGCHackFallback) return;

		// CUSTOM BAKERMODS LOGIC: Add Invaders to the 'Player' list for GC protection
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return;
		
		JWK_AIForce invaderForce = gm.BM_GetInvaderForce();
		if (!invaderForce) return;
		
		array<EntityID> groups = invaderForce.GetGroups();
		foreach (EntityID id : groups) {
			SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(id));
			if (!group) continue;
			
			// We only count alive groups as "players" for GC protection
			if (JWK_AIUtils.CountAliveCharactersInGroup(group) > 0) {
				m_WorldInfo.m_aPlayerEntities.Insert(group);
				m_WorldInfo.m_aPlayerPos.Insert(group.GetOrigin());
			}
		}
	}
}

modded class JWK_StreamableAIForce
{
	override void CheckStreamStateConditions(out bool outStreamIn, out bool outStreamOut)
	{
		super.CheckStreamStateConditions(outStreamIn, outStreamOut);
		
		// If players already triggered stream in, no need to check invaders
		if (outStreamIn) return;
		
		// Check for invaders nearby
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return;
		
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return;
		
		JWK_AIForce force = gm.BM_GetInvaderForce();
		if (!force) return;
		
		int streamInAtSq = (m_iStreamDistance - m_iStreamThresholdSpread);
		streamInAtSq *= streamInAtSq;
		int streamOutAtSq = (m_iStreamDistance + m_iStreamThresholdSpread);
		streamOutAtSq *= streamOutAtSq;

		array<EntityID> invaderGroups = force.GetGroups();
		foreach (EntityID groupId : invaderGroups) {
			SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupId));
			if (!group) continue;
			
			float distanceSq = vector.DistanceSqXZ(group.GetOrigin(), m_OriginEntity.GetOrigin());

			if (distanceSq < streamOutAtSq) outStreamOut = false;
			
			if (distanceSq < streamInAtSq) {
				outStreamIn = true;
				return;
			}
		}
	}
}

modded class JWK_FactoryEntity
{
	override protected void OnBattleFinished_S(JWK_BattleSubjectComponent subject, JWK_BattleControllerEntity controller)
	{
		int currentFactionId = m_FactionControl.GetFactionId();
		int winningFactionId = controller.GetWinningFactionID();
		
		// CUSTOM BAKERMODS LOGIC: Handle Invader win
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return;
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty()) return;

		int invaderFactionId = GetGame().GetFactionManager().GetFactionIndex(GetGame().GetFactionManager().GetFactionByKey(invaderKey));

		// Standard FF logic
		if (winningFactionId != invaderFactionId && (winningFactionId == JWK.GetFactions().GetPlayerFactionId() || winningFactionId == JWK.GetFactions().GetEnemyFactionId()))
		{
			super.OnBattleFinished_S(subject, controller);
			return;
		}
		
		if (winningFactionId == invaderFactionId)
		{
			m_FactionControl.ChangeControlToFactionId(winningFactionId);
			
			string msg = "The Invaders (" + invaderKey + ") have seized control of the Factory at " + m_NamedLocation.GetName() + "!";
			JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);
			
			Print("BM_Invasion: Factory " + m_NamedLocation.GetName() + " captured by Invaders (" + invaderKey + ")!", LogLevel.NORMAL);
		}
		else
		{
			super.OnBattleFinished_S(subject, controller);
		}
	}
}

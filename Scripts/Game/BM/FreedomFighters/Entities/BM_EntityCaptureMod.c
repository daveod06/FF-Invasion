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
	override protected int GetPlayerCountInZone_S(out int outTotalPlayers)
	{
		int players = super.GetPlayerCountInZone_S(outTotalPlayers);
		
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return players;
		
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return players;
		
		JWK_AIForce force = gm.BM_GetInvaderForce();
		if (force) {
			int invaderCount = JWK_AIUtils.GetAIForceAgentsCountInArea(force, GetOrigin(), ENEMY_PRESENCE_RANGE);
			players += invaderCount;
			outTotalPlayers += invaderCount;
		}
		
		return players;
	}

	override int GetWinningFactionID()
	{
		int winner = super.GetWinningFactionID();

		// CUSTOM BAKERMODS LOGIC: Did Invaders actually win this?
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm) return winner;
		
		string invaderKey = gm.BM_GetInvaderFactionKey();
		if (invaderKey.IsEmpty() || invaderKey == "None") return winner;

		JWK_AIForce force = gm.BM_GetInvaderForce();
		if (!force) return winner;

		int invaderCount = JWK_AIUtils.GetAIForceAgentsCountInArea(force, GetOrigin(), ENEMY_PRESENCE_RANGE);
		if (invaderCount == 0) return winner;

		int actualPlayerCount = 0;
		array<int> players = {};
		PlayerManager mgr = GetGame().GetPlayerManager();
		mgr.GetPlayers(players);
		foreach (int playerID : players) {
			IEntity playerEnt = mgr.GetPlayerControlledEntity(playerID);
			if (playerEnt && CheckIsEntityInPresenceZone(playerEnt)) {
				actualPlayerCount++;
			}
		}

		if (invaderCount > actualPlayerCount) {
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

modded class JWK_GameMode
{
	[Attribute(defvalue: "0", desc: "How many zones players must capture before the invasion triggers.", category: "BakerMods: Invasion")]
	protected int m_iBM_ZonesForInvasion;
	
	[Attribute(defvalue: "6", desc: "Base number of infantry groups spawned per invasion wave.", category: "BakerMods: Invasion")]
	protected int m_iBM_BaseInfantryGroups;
	
	[Attribute(defvalue: "3", desc: "Base number of vehicles spawned per invasion wave.", category: "BakerMods: Invasion")]
	protected int m_iBM_BaseVehicles;
	
	[Attribute(defvalue: "2", desc: "How many extra infantry groups to spawn per human player.", category: "BakerMods: Invasion")]
	protected int m_iBM_InfantryPerPlayer;
	
	[Attribute(defvalue: "2", desc: "How many extra vehicles to spawn per human player.", category: "BakerMods: Invasion")]
	protected int m_iBM_VehiclesPerPlayer;

	[Attribute(defvalue: "50", desc: "Maximum number of active Invader AI agents allowed before stopping new spawns.", category: "BakerMods: Invasion")]
	protected int m_iBM_MaxActiveAI;

	protected bool m_bBM_InvasionTriggered;
	protected string m_sBM_InvaderFactionKey = "None";
	
	protected float m_fBM_LastExpansionTime;
	protected static const float BM_EXPANSION_INTERVAL = 300000; 
	
	protected EntityID m_iBM_CurrentTargetID;
	EntityID BM_GetCurrentTargetID() { return m_iBM_CurrentTargetID; }
	protected float m_fBM_LastReinforcementTime;
	protected float m_fBM_LastRoamingTime;
	protected bool m_bBM_IntelNotified;
	protected float m_fBM_CurrentCooldown;
	protected float m_fBM_LastCasualtyCheck;
	protected int m_iBM_LastAliveCount;

	// BM_DYNAMIC_BLACKLIST: Block specific logistical/useless trucks via their exact GUIDs
	protected ref array<string> m_aBM_VehicleBlacklist = {
		"{3336BE330C4F355B}", // Ural Ammo
		"{1BABF6B33DA0AEB6}", // Ural Command
		"{A5647958579A4149}", // Ural Repair
		"{4C81D7ED8F8C0D87}", // Ural Tanker (Fuel)
		"{92264FF932676C13}", // M923A1 Ammo
		"{36BDCC88B17B3BFA}", // M923A1 Command
		"{A042ACE5C2B13206}", // M923A1 Repair
		"{2BE1F8B9299B67C1}", // M923A1 Tanker (Fuel)
		"{43C4AF1EEBD001CE}", // UAZ452 Ambulance
		"{6039A367E967676F}", // Ural Fuel (Fallback)
		"{57C4106692881D29}", // Ural Ammo (Fallback)
		"{77876A67B10D701E}", // Ural Command (Fallback)
		"{06A6F56F144D9C7D}", // M923A1 Fuel (Fallback)
		"{1A5515C817C64795}", // M923A1 Repair (Fallback)
		"{E3D739818817688B}", // M923A1 Ammo (Fallback)
		"{D661F01F41F985D3}", // M1025 Ambulance (Fallback)
		"{5BB51296570A5E0B}"  // UAZ452 Ambulance (Fallback)
	};

	protected bool BM_IsVehicleValid(ResourceName res)
	{
		string resLower = res;
		resLower.ToLower();
		
		// 1. Mandatory Exclusions (Keywords)
		// We break this into groups to avoid "Formula too complex" compilation errors
		bool isLogistical = resLower.Contains("fuel") || resLower.Contains("supply") || resLower.Contains("ammo") || resLower.Contains("repair");
		bool isSpecialized = resLower.Contains("command") || resLower.Contains("construction") || resLower.Contains("builder") || resLower.Contains("crane");
		bool isMedical = resLower.Contains("medic") || resLower.Contains("ambulance");
		bool isSupport = resLower.Contains("tanker") || resLower.Contains("logistics") || resLower.Contains("maintenance") || resLower.Contains("workshop");
		bool isAirOrBase = resLower.Contains("helicopter") || resLower.Contains("heli") || resLower.Contains("_base") || resLower.Contains("core");

		if (isLogistical || isSpecialized || isMedical || isSupport || isAirOrBase) 
		{
			// SPECIAL CASE: Always allow actual Transport trucks
			if (resLower.Contains("transport")) return true;
			
			return false;
		}

		// 2. GUID Blacklist check (Precise GUIDs from MCP/Configs)
		foreach (string blacklistedGuid : m_aBM_VehicleBlacklist)
		{
			if (res.Contains(blacklistedGuid)) return false;
		}

		return true;
	}

	// Replicated property so all clients know who the invader is
	void BM_SetInvaderFactionKey(string key)
	{
		m_sBM_InvaderFactionKey = key;
		
		// Load on server
		JWK_FactionManager factionMgr = JWK_FactionManager.Cast(GetGame().GetFactionManager());
		if (factionMgr) factionMgr.BM_LoadFaction(key);
		
		// Tell all clients to load it too
		Rpc(BM_RpcDo_SyncInvaderFaction, key);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void BM_RpcDo_SyncInvaderFaction(string key)
	{
		m_sBM_InvaderFactionKey = key;
		
		JWK_FactionManager factionMgr = JWK_FactionManager.Cast(GetGame().GetFactionManager());
		if (factionMgr) {
			factionMgr.BM_LoadFaction(key);
			Print("BM_Invasion: Client synchronized invader faction: " + key, LogLevel.NORMAL);
		}
	}

	string BM_GetInvaderFactionKey()
	{
		return m_sBM_InvaderFactionKey;
	}

	override void OnWorldPostProcess(World world)
	{
		super.OnWorldPostProcess(world);
		
		if (Replication.IsServer()) {
			GetGame().GetCallqueue().CallLater(BM_UpdateInvasionDirector, 15000, true);
		}
	}

	protected ref JWK_AIForce m_InvaderForce;
	protected ref array<IEntity> m_aBM_EntitiesToClean = {};
	protected ref array<IEntity> m_aBM_ProtectedFromGC = new array<IEntity>();

	protected void BM_ProtectEntityFromGC(IEntity ent)
	{
		if (!ent) return;
		SCR_GarbageSystem gcManager = SCR_GarbageSystem.Cast(GetGame().GetWorld().FindSystem(SCR_GarbageSystem));
		if (gcManager) {
			gcManager.Withdraw(ent);
			if (!m_aBM_ProtectedFromGC) m_aBM_ProtectedFromGC = new array<IEntity>();
			m_aBM_ProtectedFromGC.Insert(ent);
		}
	}

	protected void BM_ProtectGroupFromGC(SCR_AIGroup group)
	{
		if (!group) return;
		BM_ProtectEntityFromGC(group);
		
		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents) {
			IEntity charEnt = agent.GetControlledEntity();
			if (charEnt) BM_ProtectEntityFromGC(charEnt);
		}
	}

	protected void BM_UpdateProtectedGC()
	{
		if (!m_aBM_ProtectedFromGC) return;
		
		SCR_GarbageSystem gcManager = SCR_GarbageSystem.Cast(GetGame().GetWorld().FindSystem(SCR_GarbageSystem));
		if (!gcManager) return;

		for (int i = m_aBM_ProtectedFromGC.Count() - 1; i >= 0; i--) {
			IEntity ent = m_aBM_ProtectedFromGC[i];
			if (!ent) {
				m_aBM_ProtectedFromGC.Remove(i);
				continue;
			}
			
			bool shouldClean = false;
			
			SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(ent);
			if (character) {
				DamageManagerComponent dmg = character.GetDamageManager();
				if (dmg && dmg.IsDestroyed()) shouldClean = true;
			}
			else if (ent.IsInherited(Vehicle)) {
				DamageManagerComponent dmg = DamageManagerComponent.Cast(ent.FindComponent(DamageManagerComponent));
				if (dmg && dmg.IsDestroyed()) shouldClean = true;
			}
			else if (ent.IsInherited(SCR_AIGroup)) {
				SCR_AIGroup grp = SCR_AIGroup.Cast(ent);
				if (grp && grp.GetAgentsCount() == 0) shouldClean = true;
			}
			
			if (shouldClean) {
				gcManager.Insert(ent);
				m_aBM_ProtectedFromGC.Remove(i);
			}
		}
	}

	protected void BM_UpdateInvasionDirector()
	{
		if (!IsRunning()) return;

		BM_UpdateProtectedGC();

		if (m_sBM_InvaderFactionKey.IsEmpty() || m_sBM_InvaderFactionKey == "None") return;

		if (!m_bBM_InvasionTriggered)
		{
			int playerZones = 0;
			array<EntityID> outIds = {};
			
			outIds = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
			foreach (EntityID id : outIds) {
				if (JWK_FactionControlComponent.IsPlayerFaction(id)) playerZones++;
			}
			
			outIds = JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity);
			foreach (EntityID id : outIds) {
				if (JWK_FactionControlComponent.IsPlayerFaction(id)) playerZones++;
			}
			
			if (playerZones >= m_iBM_ZonesForInvasion) {
				BM_TriggerInvasion();
			}
		}
		else
		{
			BM_CheckOffscreenCaptures();

			float currentTime = GetGame().GetWorld().GetWorldTime();
			
			// --- Reinforcements Check Every 2 Mins ---
			if (currentTime - m_fBM_LastReinforcementTime > 120000)
			{
				m_fBM_LastReinforcementTime = currentTime;
				
				// Send reinforcements if we have an active target and are low on troops
				int currentAI = 0;
				if (m_InvaderForce) currentAI = m_InvaderForce.CountAgents();
				
				if (currentAI < (m_iBM_MaxActiveAI * 0.5) && m_iBM_CurrentTargetID != EntityID.INVALID)
				{
					IEntity currentTargetEnt = GetGame().GetWorld().FindEntityByID(m_iBM_CurrentTargetID);
					if (currentTargetEnt)
					{
						SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(currentTargetEnt);
						if (fac && fac.GetAffiliatedFactionKey() != m_sBM_InvaderFactionKey)
						{
							SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
							if (invaderFaction) {
								Print("BM_Invasion: Reinforcing attack on " + currentTargetEnt.GetPrefabData().GetPrefabName(), LogLevel.NORMAL);
								IEntity sourceBase = BM_FindInvaderHeldBase(invaderFaction);
								vector sourcePos = "0 0 0";
								if (sourceBase) sourcePos = sourceBase.GetOrigin();
								BM_SpawnInvasionForce(currentTargetEnt.GetOrigin(), invaderFaction, sourcePos);
							}
						}
						else
						{
							// Target was captured, clear it
							m_iBM_CurrentTargetID = EntityID.INVALID;
						}
					}
				}
			}
			// ------------------------------------------

			// --- Roaming Patrol Check Every 10 Mins ---
			if (currentTime - m_fBM_LastRoamingTime > 600000)
			{
				m_fBM_LastRoamingTime = currentTime;
				SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
				if (invaderFaction) BM_SpawnRoamingPatrol(invaderFaction);
			}
			// ------------------------------------------

			if (currentTime - m_fBM_LastExpansionTime > m_fBM_CurrentCooldown)
			{
				m_fBM_LastExpansionTime = currentTime;
				m_bBM_IntelNotified = false;
				
				// RE-CALCULATE COOLDOWN: Standard is 10-15 mins.
				m_fBM_CurrentCooldown = (600000 + JWK.Random.RandFloat01() * 300000);
				
				// CASUALTY PENALTY: If invaders are losing troops, slow down the war machine
				if (m_InvaderForce) {
					int alive = m_InvaderForce.CountAgents();
					if (alive < m_iBM_LastAliveCount) {
						float penalty = (m_iBM_LastAliveCount - alive) * 10000; // 10s extra cooldown per death
						m_fBM_CurrentCooldown += penalty;
						Print("BM_Invasion: High casualties detected. Delaying next wave by " + (penalty/1000).ToString() + "s", LogLevel.NORMAL);
					}
					m_iBM_LastAliveCount = alive;
				}

				// Halt Capture Logic: Limit maximum territory
				int maxBases = 3 + (GetGame().GetPlayerManager().GetPlayerCount() * 2);
				int currentBases = 0;
				SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
				
				if (invaderFaction) {
					array<EntityID> bases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
					foreach (EntityID id : bases) {
						IEntity ent = GetGame().GetWorld().FindEntityByID(id);
						SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
						if (fac && fac.GetAffiliatedFaction() == invaderFaction) currentBases++;
					}
					array<EntityID> factories = JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity);
					foreach (EntityID id : factories) {
						IEntity ent = GetGame().GetWorld().FindEntityByID(id);
						SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
						if (fac && fac.GetAffiliatedFaction() == invaderFaction) currentBases++;
					}
				}

				if (currentBases < maxBases) {
					BM_ExecuteExpansionWave();
				}
				
				// Utilize FF Patrol System for Invaders
				BM_SpawnInvaderPatrols(invaderFaction);
			}
			else if (!m_bBM_IntelNotified && (m_fBM_CurrentCooldown - (currentTime - m_fBM_LastExpansionTime) < 120000))
			{
				// INTEL INTERCEPT: Warn players 2 minutes before the next wave
				m_bBM_IntelNotified = true;
				SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
				if (invaderFaction) {
					IEntity sourceBase = BM_FindInvaderHeldBase(invaderFaction);
					IEntity target = null;
					if (sourceBase) target = BM_FindNextExpansionTarget(sourceBase.GetOrigin(), invaderFaction);
					
					if (target) {
						JWK_NamedLocationComponent loc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(target);
						string name = "Unknown";
						if (loc) name = loc.GetName();
						
						string msg = "REBEL INTEL: Radio chatter suggests an Invader column is preparing to move from their territory towards " + name + ".";
						JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);
					}
				}
			}
		}
	}

	protected void BM_SpawnInvaderPatrols(Faction faction)
	{
		if (!faction) return;
		
		array<EntityID> bases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
		array<IEntity> myBases = {};
		
		foreach (EntityID id : bases) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			if (!ent) continue;
			
			SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			if (fac && fac.GetAffiliatedFaction() == faction) {
				myBases.Insert(ent);
			}
		}

		// BM_DYNAMIC_PATROL: Only spawn ONE patrol vehicle from a random base per cycle to keep it light
		if (myBases.IsEmpty()) return;
		IEntity ent = myBases.GetRandomElement();
		
		vector pos = ent.GetOrigin();
		
		SCR_EntityCatalog catalog = SCR_Faction.Cast(faction).GetFactionEntityCatalogOfType(EEntityCatalogType.VEHICLE);
		if (!catalog) return;
		
		array<SCR_EntityCatalogEntry> entries = {};
		catalog.GetEntityList(entries);
		if (entries.IsEmpty()) return;

		array<ResourceName> validPrefabs = {};
		foreach (SCR_EntityCatalogEntry entry : entries) {
			ResourceName res = entry.GetPrefab();
			if (BM_IsVehicleValid(res)) {
				validPrefabs.Insert(res);
			}
		}

		if (validPrefabs.IsEmpty()) return;
		
		ResourceName prefab = validPrefabs.GetRandomElement();

		if (!prefab.IsEmpty()) {
			vector spawnPos = pos + Vector(JWK.Random.RandFloatXY(-250, 250), 0, JWK.Random.RandFloatXY(-250, 250));
			
			JWK_Road road;
			int roadPointIdx;
			if (JWK_RoadNetworkManagerComponent.GetInstance().GetClosestRoad(spawnPos, road, roadPointIdx, 350))
			{
				spawnPos = road.points[roadPointIdx];
			}

			spawnPos[1] = GetGame().GetWorld().GetSurfaceY(spawnPos[0], spawnPos[2]) + 0.5;
			
			IEntity spawned = JWK_SpawnUtils.SpawnEntityPrefab(prefab, spawnPos);
			if (spawned) {
				// BM_GC_FIX: Protect vehicle from GC
				BM_ProtectEntityFromGC(spawned);
				
				SCR_AIGroup group;
				JWK_AmbientVehicleEventSpawner spawner = new JWK_AmbientVehicleEventSpawner();
				spawner.Init(JWK_AmbientTrafficSystem.Get());
				spawner.SpawnVehicleCrew(spawned, faction.GetFactionKey(), group);
				
				if (group) {
					// BM_GC_FIX: Protect group from GC
					BM_ProtectGroupFromGC(group);
					
					// Give them a proper PATROL order so they drive around the area instead of returning to base
					AIWaypoint wp = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.PATROL, pos);
					group.AddWaypoint(wp);
					
					if (!m_InvaderForce) {
						m_InvaderForce = new JWK_AIForce();
						m_InvaderForce.m_bAutoUnstuck = true;
					}
					
					JWK_CrewedVehicle cv = new JWK_CrewedVehicle();
					cv.m_Vehicle = Vehicle.Cast(spawned);
					cv.m_Crew = group;
					
					m_InvaderForce.AttachCrewedVehicle(cv);
				}
			}
		}
	}

	protected void BM_SpawnRoamingPatrol(Faction faction)
	{
		IEntity sourceBase = BM_FindInvaderHeldBase(faction);
		if (!sourceBase) return;

		vector pos = sourceBase.GetOrigin();
		
		array<EntityID> potential = {};
		potential.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity));
		potential.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_TownEntity));
		potential.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity));
		
		if (potential.Count() < 4) return;

		vector spawnPos = pos + Vector(JWK.Random.RandFloatXY(-100, 100), 0, JWK.Random.RandFloatXY(-100, 100));
		
		JWK_Road road;
		int roadPointIdx;
		if (JWK_RoadNetworkManagerComponent.GetInstance().GetClosestRoad(spawnPos, road, roadPointIdx, 350))
		{
			spawnPos = road.points[roadPointIdx];
		}
		
		spawnPos[1] = GetGame().GetWorld().GetSurfaceY(spawnPos[0], spawnPos[2]) + 1.2;
		if (spawnPos[1] <= 1.0) return;

		SCR_EntityCatalog catalog = SCR_Faction.Cast(faction).GetFactionEntityCatalogOfType(EEntityCatalogType.VEHICLE);
		if (!catalog) return;
		
		array<SCR_EntityCatalogEntry> entries = {};
		catalog.GetEntityList(entries);
		array<ResourceName> validPrefabs = {};
		foreach (SCR_EntityCatalogEntry entry : entries) {
			ResourceName res = entry.GetPrefab();
			if (BM_IsVehicleValid(res)) {
				validPrefabs.Insert(res);
			}
		}

		if (validPrefabs.IsEmpty()) return;
		ResourceName prefab = validPrefabs.GetRandomElement();

		IEntity spawned = JWK_SpawnUtils.SpawnEntityPrefab(prefab, spawnPos);
		if (!spawned) return;

		BM_ProtectEntityFromGC(spawned);
		
		SCR_AIGroup group;
		JWK_AmbientVehicleEventSpawner spawner = new JWK_AmbientVehicleEventSpawner();
		spawner.Init(JWK_AmbientTrafficSystem.Get());
		spawner.SpawnVehicleCrew(spawned, faction.GetFactionKey(), group);
		
		if (group) {
			BM_ProtectGroupFromGC(group);
			
			array<AIWaypoint> waypoints = {};
			for (int i = 0; i < 4; i++) {
				EntityID targetID = potential.GetRandomElement();
				IEntity targetEnt = GetGame().GetWorld().FindEntityByID(targetID);
				if (targetEnt) {
					AIWaypoint wp = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.MOVE, targetEnt.GetOrigin());
					group.AddWaypoint(wp);
					waypoints.Insert(wp);
				}
			}
			
			if (!waypoints.IsEmpty()) {
				AIWaypoint cycle = JWK.GetAIManager().SpawnCycleWaypoint(group.GetOrigin(), waypoints);
				group.AddWaypoint(cycle);
			}
			
			if (!m_InvaderForce) {
				m_InvaderForce = new JWK_AIForce();
				m_InvaderForce.m_bAutoUnstuck = true;
			}
			
			JWK_CrewedVehicle cv = new JWK_CrewedVehicle();
			cv.m_Vehicle = Vehicle.Cast(spawned);
			cv.m_Crew = group;
			
			m_InvaderForce.AttachCrewedVehicle(cv);
			Print("BM_Invasion: Dispatched long-range roaming patrol.", LogLevel.NORMAL);
		}
	}

	protected void BM_TriggerInvasion()
	{
		m_bBM_InvasionTriggered = true;
		m_fBM_LastExpansionTime = GetGame().GetWorld().GetWorldTime();
		m_fBM_CurrentCooldown = 300000; // Start first wave in 5 mins
		m_iBM_LastAliveCount = 0;
		m_bBM_IntelNotified = false;
		JWK_FactionManager factionMgr = JWK_FactionManager.Cast(GetGame().GetFactionManager());
		if (!factionMgr) return;

		if (!factionMgr.BM_LoadFaction(m_sBM_InvaderFactionKey)) return;

		SCR_Faction invaderFaction = SCR_Faction.Cast(factionMgr.GetFactionByKey(m_sBM_InvaderFactionKey));
		SCR_Faction occupierFaction = SCR_Faction.Cast(factionMgr.GetFactionByKey(JWK.GetGameConfig().GetEnemyFactionKey()));
		SCR_Faction playerFaction = SCR_Faction.Cast(factionMgr.GetFactionByKey(JWK.GetGameConfig().GetPlayerFactionKey()));

		if (!invaderFaction || !occupierFaction || !playerFaction) return;

		factionMgr.SetFactionsHostile(invaderFaction, occupierFaction);
		factionMgr.SetFactionsHostile(invaderFaction, playerFaction);
		
		Print("BM_Invasion: Three-way war activated!", LogLevel.NORMAL);

		IEntity beachhead = BM_FindInvasionTarget(occupierFaction);
		if (!beachhead) return;

		m_iBM_CurrentTargetID = beachhead.GetID();
		vector targetPos = beachhead.GetOrigin();
		
		string invaderName = invaderFaction.GetFactionName();
		string msg = "URGENT: Intelligence reports a large " + invaderName + " task force entering the sector! They are hostile to all local forces.";
		JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);

		BM_SpawnInvasionForce(targetPos, invaderFaction);
	}

	protected void BM_ExecuteExpansionWave()
	{
		SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
		if (!invaderFaction) return;

		// PERFORMANCE & PERSISTENCE: Check current force strength
		int currentAI = 0;
		if (m_InvaderForce) currentAI = m_InvaderForce.CountAgents();

		IEntity sourceBase = BM_FindInvaderHeldBase(invaderFaction);
		IEntity target;
		vector sourcePos = "0 0 0";

		// BM_FIX: Focus on the current target if we have one, instead of picking a new one every wave
		if (m_iBM_CurrentTargetID != EntityID.INVALID) {
			IEntity currentTarget = GetGame().GetWorld().FindEntityByID(m_iBM_CurrentTargetID);
			if (currentTarget) {
				SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(currentTarget);
				if (fac && fac.GetAffiliatedFactionKey() != m_sBM_InvaderFactionKey) {
					target = currentTarget;
					Print("BM_Invasion: Continuing attack on existing target: " + target.GetPrefabData().GetPrefabName(), LogLevel.NORMAL);
				}
			}
		}

		if (!target) {
			if (!sourceBase)
			{
				SCR_Faction occupierFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(JWK.GetGameConfig().GetEnemyFactionKey()));
				target = BM_FindInvasionTarget(occupierFaction);
			}
			else
			{
				sourcePos = sourceBase.GetOrigin();
				target = BM_FindNextExpansionTarget(sourcePos, invaderFaction);
			}
			
			if (target) {
				m_iBM_CurrentTargetID = target.GetID();
				Print("BM_Invasion: Selected new objective: " + target.GetPrefabData().GetPrefabName(), LogLevel.NORMAL);
			}
		}

		if (!target) return;

		// RE-TASKING LOGIC: If we have plenty of AI, just send them to the new target instead of spawning new ones
		if (currentAI >= m_iBM_MaxActiveAI)
		{
			Print("BM_Invasion: AI Limit reached (" + currentAI + "). Re-tasking existing units to " + target.GetPrefabData().GetPrefabName(), LogLevel.NORMAL);
			
			foreach (EntityID groupId : m_InvaderForce.GetGroups())
			{
				SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupId));
				if (!group) continue;
				
				// Clear old waypoints and use FF sweep logic to ensure they don't get stuck
				AIWaypoint current = group.GetCurrentWaypoint();
				if (current) group.RemoveWaypoint(current);
				
				// Prioritize getting to the new target objective quickly
				AIWaypoint move = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.FORCED_MOVE, target.GetOrigin());
				group.AddWaypoint(move);
				
				JWK_AIUtils.AddAttackAndSweepWaypoints(group, target.GetOrigin());
			}
			return;
		}

		BM_SpawnInvasionForce(target.GetOrigin(), invaderFaction, sourcePos);
	}

	protected IEntity BM_FindInvasionTarget(Faction occupierFaction)
	{
		SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));

		// TACTICAL TARGETING: Target the base closest to the center of the human players
		vector centerPos = "0 0 0";
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		int count = 0;
		foreach (int id : players)
		{
			IEntity p = GetGame().GetPlayerManager().GetPlayerControlledEntity(id);
			if (p) {
				centerPos += p.GetOrigin();
				count++;
			}
		}
		if (count > 0) centerPos = centerPos / count;
		else centerPos = "0 0 0";

		IEntity closestTarget = null;
		float minDist = 999999;

		array<EntityID> potential = {};
		potential.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_AirportEntity));
		potential.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity));

		foreach (EntityID id : potential) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			if (fac && fac.GetAffiliatedFaction() != invaderFaction) {
				float dist = vector.Distance(centerPos, ent.GetOrigin());
				if (dist < minDist) {
					minDist = dist;
					closestTarget = ent;
				}
			}
		}

		return closestTarget;
	}

	protected IEntity BM_FindInvaderHeldBase(Faction invaderFaction)
	{
		array<EntityID> bases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
		foreach (EntityID id : bases) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			if (fac && fac.GetAffiliatedFaction() == invaderFaction) return ent;
		}
		
		array<EntityID> factories = JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity);
		foreach (EntityID id : factories) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			if (fac && fac.GetAffiliatedFaction() == invaderFaction) return ent;
		}

		return null;
	}

	protected IEntity BM_FindNextExpansionTarget(vector sourcePos, Faction invaderFaction)
	{
		IEntity closest = null;
		float minDist = 999999;

		array<EntityID> potential = {};
		potential.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity));
		potential.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity));

		foreach (EntityID id : potential) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			if (fac && fac.GetAffiliatedFaction() != invaderFaction)
			{
				float dist = vector.Distance(sourcePos, ent.GetOrigin());
				if (dist < minDist && dist > 100) 
				{
					minDist = dist;
					closest = ent;
				}
			}
		}

		return closest;
	}

	protected void BM_SpawnInvasionForce(vector targetPos, Faction faction, vector sourcePos = "0 0 0")
	{
		int playerCount = GetGame().GetPlayerManager().GetPlayerCount();
		if (playerCount < 1) playerCount = 1;
		
		int numVehicles = m_iBM_BaseVehicles + (playerCount * m_iBM_VehiclesPerPlayer);

		// SURGICAL STRIKE: Find exactly ONE road assembly point near the target or owned base
		vector assemblyPos;
		JWK_Road road;
		int roadPointIdx;
		bool roadFound = false;
		
		bool isExpansion = (sourcePos != "0 0 0");
		vector origin;
		float radius;

		if (isExpansion) {
			origin = sourcePos;
			radius = 150;
		} else {
			origin = targetPos;
			radius = 400;
		}

		for (int attempt = 0; attempt < 30; attempt++)
		{
			vector spot = origin + Vector(JWK.Random.RandFloatXY(-radius, radius), 0, JWK.Random.RandFloatXY(-radius, radius));
			if (!isExpansion && vector.Distance(spot, targetPos) < 150) continue; 

			if (JWK_RoadNetworkManagerComponent.GetInstance().GetClosestRoad(spot, road, roadPointIdx, 500))
			{
				assemblyPos = road.points[roadPointIdx];
				if (GetGame().GetWorld().GetSurfaceY(assemblyPos[0], assemblyPos[2]) > 1.0)
				{
					roadFound = true;
					break;
				}
			}
		}

		if (!roadFound) 
		{
			Print("BM_Invasion: FAILED to find valid road near " + origin + ". Aborting wave.", LogLevel.ERROR);
			return;
		}

		for (int j = 0; j < numVehicles; j++) {
			GetGame().GetCallqueue().CallLater(BM_SpawnInvasionElement, j * 8000, false, targetPos, assemblyPos, faction, j);
		}
	}

	protected void BM_SpawnInvasionElement(vector targetPos, vector entryPos, Faction faction, int index)
	{
		vector spawnPos = entryPos;
		float spawnYaw = 0;
		
		// MANDATORY ROAD SNAP: Every vehicle must be on the asphalt
		JWK_Road road;
		int roadPointIdx;
		if (JWK_RoadNetworkManagerComponent.GetInstance().GetClosestRoad(spawnPos, road, roadPointIdx, 300))
		{
			spawnPos = road.points[roadPointIdx];
			spawnYaw = road.GetYawFromPoint(roadPointIdx, true);
			
			vector roadDir1 = Vector(Math.Sin(spawnYaw * Math.DEG2RAD), 0, Math.Cos(spawnYaw * Math.DEG2RAD));
			vector roadDir2 = -roadDir1;

			vector toTarget = targetPos - spawnPos;
			toTarget.Normalize();
			
			vector dir;
			// Ensure the convoy spawns facing TOWARDS the objective along the road
			if (vector.Dot(roadDir1, toTarget) > vector.Dot(roadDir2, toTarget)) {
				dir = roadDir1;
			} else {
				dir = roadDir2;
				spawnYaw += 180;
			}
			
			// Form a perfect convoy line (50m spacing) BACKWARDS from the assembly point
			spawnPos -= dir * (index * 50); 
		}
		else
		{
			Print("BM_Invasion: Element lost road anchor at index " + index, LogLevel.WARNING);
			return;
		}
		
		spawnPos[1] = GetGame().GetWorld().GetSurfaceY(spawnPos[0], spawnPos[2]) + 1.2;
		if (spawnPos[1] <= 1.0) return;

		// Custom catalog filter for combat vehicles
		SCR_EntityCatalog catalog = SCR_Faction.Cast(faction).GetFactionEntityCatalogOfType(EEntityCatalogType.VEHICLE);
		if (!catalog) return;
		
		array<SCR_EntityCatalogEntry> entries = {};
		catalog.GetEntityList(entries);
		array<ResourceName> validPrefabs = {};
		foreach (SCR_EntityCatalogEntry entry : entries) {
			ResourceName res = entry.GetPrefab();
			if (BM_IsVehicleValid(res)) {
				validPrefabs.Insert(res);
			}
		}

		if (validPrefabs.IsEmpty()) return;
		
		ResourceName prefab = validPrefabs.GetRandomElement();

		// BM_FIX: Spawn the vehicle perfectly aligned with the road and facing the objective!
		IEntity spawned = JWK_SpawnUtils.SpawnEntityPrefab(prefab, spawnPos, Vector(spawnYaw, 0, 0));
		if (!spawned) return;

		// BM_GC_FIX: Protect the vehicle from Garbage Collection
		BM_ProtectEntityFromGC(spawned);

		SCR_AIGroup group = null;
		JWK_AmbientVehicleEventSpawner spawner = new JWK_AmbientVehicleEventSpawner();
		spawner.Init(JWK_AmbientTrafficSystem.Get());
		spawner.SpawnVehicleCrew(spawned, faction.GetFactionKey(), group);

		if (group) {
			// BM_GC_FIX: Protect the AI Group from Garbage Collection
			BM_ProtectGroupFromGC(group);

			// Add a slight random offset so they don't all drive to the exact same 1x1 meter coordinate and ram each other
			vector offset = Vector(JWK.Random.RandFloatXY(-25, 25), 0, JWK.Random.RandFloatXY(-25, 25));
			vector finalPos = targetPos + offset;

			// FORCE ROAD DRIVING: Give an explicit FORCED_MOVE command near the target
			AIWaypoint move = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.FORCED_MOVE, finalPos);
			group.AddWaypoint(move);

			// DISMOUNT: Force the AI to exit the vehicles upon arrival so they can spread out
			AIWaypoint getOut = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.GET_OUT, finalPos);
			group.AddWaypoint(getOut);

			// Add the native FF Attack and Sweep cycle once they arrive and dismount
			JWK_AIUtils.AddAttackAndSweepWaypoints(group, targetPos);
			
			if (!m_InvaderForce) {
				m_InvaderForce = new JWK_AIForce();
				m_InvaderForce.m_bAutoUnstuck = true;
			}
			
			JWK_CrewedVehicle cv = new JWK_CrewedVehicle();
			cv.m_Vehicle = Vehicle.Cast(spawned);
			cv.m_Crew = group;
			
			m_InvaderForce.AttachCrewedVehicle(cv);
		}
	}
	
	protected int m_iTempInvaderCount;
	protected int m_iTempDefenderCount;

	bool BM_IsInvaderWinner(vector pos)
	{
		m_iTempInvaderCount = 0;
		m_iTempDefenderCount = 0;

		GetGame().GetWorld().QueryEntitiesBySphere(
			pos,
			150,
			this.BM_CountForcesAction,
			this.BM_FilterCharacters,
			EQueryEntitiesFlags.DYNAMIC | EQueryEntitiesFlags.WITH_OBJECT
		);

		// If invaders are present and no occupying defenders remain, they win!
		return (m_iTempInvaderCount > 0 && m_iTempDefenderCount == 0);
	}

	protected void BM_CheckOffscreenCaptures()
	{
		SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
		if (!invaderFaction) return;

		array<EntityID> potential = {};
		potential.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity));
		potential.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity));

		foreach (EntityID id : potential) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			if (!ent) continue;
			
			SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			if (!fac || fac.GetAffiliatedFaction() == invaderFaction) continue;

			m_iTempInvaderCount = 0;
			m_iTempDefenderCount = 0;

			GetGame().GetWorld().QueryEntitiesBySphere(
				ent.GetOrigin(),
				150,
				this.BM_CountForcesAction,
				this.BM_FilterCharacters,
				EQueryEntitiesFlags.DYNAMIC | EQueryEntitiesFlags.WITH_OBJECT
			);

			if (m_iTempInvaderCount > 0 && m_iTempDefenderCount == 0) {
				Faction occupierFaction = fac.GetAffiliatedFaction();
				int invaderFactionId = GetGame().GetFactionManager().GetFactionIndex(invaderFaction);
				
				// Wipe out any virtual defenders/garrison manpower so they don't magically respawn
				JWK_AIForceComponent forceComp = JWK_CompTU<JWK_AIForceComponent>.FindIn(ent);
				if (forceComp && forceComp.GetForce_S()) {
					forceComp.GetForce_S().ForceDeleteAllUnits();
				}
				
				// BM_GC_FIX: Use the native FF Battle System to execute the capture
				JWK_BattleSubjectComponent battleSubj = JWK_CompTU<JWK_BattleSubjectComponent>.FindIn(ent);
				if (battleSubj && JWK.GetBattleManager()) {
					JWK_BattleControllerEntity ctrl = JWK.GetBattleManager().GetController();
					
					// If a battle is already running on THIS base, force it to finish
					if (ctrl && ctrl.GetSubject_S() == battleSubj) {
						ctrl.ForceFinish_S(invaderFactionId);
						Print("BM_Invasion: Active FF battle finished in favor of invaders at " + ent.GetPrefabData().GetPrefabName(), LogLevel.NORMAL);
					} else if (!ctrl) {
						// No battle is running anywhere. Start a fake one and win it instantly!
						// This invokes the "Normal Loop" of Freedom Fighters.
						JWK.GetBattleManager().StartBattle_S(battleSubj);
						ctrl = JWK.GetBattleManager().GetController();
						if (ctrl) {
							ctrl.SetAttackingFaction_S(invaderFaction);
							ctrl.ForceFinish_S(invaderFactionId);
							Print("BM_Invasion: Fake FF battle triggered and won by invaders at " + ent.GetPrefabData().GetPrefabName(), LogLevel.NORMAL);
						}
					}
				}

				// BM_FIX: Cleanup the battlefield with a 2s delay to remove dead bodies and abandoned vehicles
				// We add a delay to ensure the native FF BattleManager has finished its own internal cleanup first.
				GetGame().GetCallqueue().CallLater(BM_CleanupBattlefield, 2000, false, ent.GetOrigin());
				
				// Clear current target if it was the one we just captured
				if (m_iBM_CurrentTargetID == ent.GetID()) {
					m_iBM_CurrentTargetID = EntityID.INVALID;
				}
				
				// Instantly initialize the new Invader Garrison to guard the base
				JWK_AIGarrisonComponent newGarrison = JWK_CompTU<JWK_AIGarrisonComponent>.FindIn(ent);
				JWK_AIForceComponent newForceComp = JWK_CompTU<JWK_AIForceComponent>.FindIn(ent);
				if (newGarrison && newForceComp && newForceComp.GetForce_S()) {
					// Initialize_S will read the new Invader faction control and spawn Invader AI
					newGarrison.Initialize_S();
					newForceComp.GetForce_S().DoStreamIn();
				}

				// BM_UPGRADE: Spawn extra mobile security patrols (Foot and Vehicle) around the base
				BM_SpawnLocalSecurity(ent.GetOrigin(), invaderFaction);
				
				// BM_FIX: Since Invaders can capture bases while players are not around or are dead,
				// we must manually trigger a GameOver state refresh in case the players just lost their final base.
				if (JWK.GetGameOver()) {
					GetGame().GetCallqueue().CallLater(JWK.GetGameOver().RefreshState, 5000, false);
				}
			}
		}
	}

	protected void BM_SpawnLocalSecurity(vector pos, Faction faction)
	{
		// 1. Spawn a Foot Patrol (3-man team)
		vector footSpawn = pos + Vector(JWK.Random.RandFloatXY(-50, 50), 0, JWK.Random.RandFloatXY(-50, 50));
		
		JWK_CombatFactionTrait combatTrait = JWK_FactionTraitTU<JWK_CombatFactionTrait>.GetByKey(faction.GetFactionKey());
		ResourceName groupPrefab = "";
		
		if (combatTrait) {
			JWK_FactionForceConfig force = combatTrait.GetForceByType(JWK_EFactionForceType.REGULAR);
			if (force) {
				groupPrefab = force.m_rDefaultGroupPrefab;
				if (groupPrefab.IsEmpty() && !force.m_aPatrolGroups.IsEmpty())
					groupPrefab = force.m_aPatrolGroups.GetRandomElement();
			}
		}

		if (!groupPrefab.IsEmpty()) {
			SCR_AIGroup footGroup = SCR_AIGroup.Cast(JWK_SpawnUtils.SpawnEntityPrefab(groupPrefab, footSpawn));
			if (footGroup) {
				AIWaypoint patrol = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.PATROL, pos);
				footGroup.AddWaypoint(patrol);
			}
		}

		// 2. Spawn an Armed Technical Patrol
		SCR_EntityCatalog catalog = SCR_Faction.Cast(faction).GetFactionEntityCatalogOfType(EEntityCatalogType.VEHICLE);
		if (catalog) {
			array<SCR_EntityCatalogEntry> entries = {};
			catalog.GetEntityList(entries);
			array<ResourceName> combatVics = {};
			foreach (SCR_EntityCatalogEntry entry : entries) {
				if (BM_IsVehicleValid(entry.GetPrefab())) combatVics.Insert(entry.GetPrefab());
			}
			
			if (!combatVics.IsEmpty()) {
				vector vicSpawn = pos + Vector(100, 0, 100);
				JWK_Road road; int roadIdx;
				if (JWK_RoadNetworkManagerComponent.GetInstance().GetClosestRoad(vicSpawn, road, roadIdx, 300)) vicSpawn = road.points[roadIdx];
				
				IEntity vic = JWK_SpawnUtils.SpawnEntityPrefab(combatVics.GetRandomElement(), vicSpawn);
				if (vic) {
					SCR_AIGroup vicGroup;
					JWK_AmbientVehicleEventSpawner spawner = new JWK_AmbientVehicleEventSpawner();
					spawner.Init(JWK_AmbientTrafficSystem.Get());
					spawner.SpawnVehicleCrew(vic, faction.GetFactionKey(), vicGroup);
					if (vicGroup) {
						AIWaypoint patrol = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.PATROL, pos);
						vicGroup.AddWaypoint(patrol);
					}
				}
			}
		}
	}

	protected void BM_CleanupBattlefield(vector centerPos)
	{
		m_aBM_EntitiesToClean.Clear();
		
		// Pass 1: Scan and collect
		GetGame().GetWorld().QueryEntitiesBySphere(
			centerPos,
			450, 
			this.BM_DoCleanupAction,
			null,
			EQueryEntitiesFlags.DYNAMIC | EQueryEntitiesFlags.WITH_OBJECT
		);
		
		int count = m_aBM_EntitiesToClean.Count();
		Print("BM_Invasion: Cleanup sweep initiated at " + centerPos.ToString() + ". Found " + count.ToString() + " objects to remove.", LogLevel.NORMAL);
		
		// Pass 2: Instant Deletion
		foreach (IEntity ent : m_aBM_EntitiesToClean)
		{
			if (ent) SCR_EntityHelper.DeleteEntityAndChildren(ent);
		}
		
		m_aBM_EntitiesToClean.Clear();
	}

	protected bool BM_DoCleanupAction(IEntity ent)
	{
		if (!ent) return true;
		
		// 1. Characters
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(ent);
		if (character) {
			DamageManagerComponent damageMgr = character.GetDamageManager();
			
			// Always cleanup dead bodies instantly
			if (damageMgr && damageMgr.IsDestroyed()) {
				m_aBM_EntitiesToClean.Insert(ent);
				return true;
			}
			
			return true;
		}
		
		// 2. Vehicles
		if (ent.IsInherited(Vehicle)) {
			DamageManagerComponent damageMgr = DamageManagerComponent.Cast(ent.FindComponent(DamageManagerComponent));
			
			// Always cleanup destroyed vehicles instantly
			if (damageMgr && damageMgr.IsDestroyed()) {
				m_aBM_EntitiesToClean.Insert(ent);
				return true;
			}

			// For SURVIVING vehicles: Check if it's an abandoned vehicle (empty) and non-player
			SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			SCR_BaseCompartmentManagerComponent compMgr = SCR_BaseCompartmentManagerComponent.Cast(ent.FindComponent(SCR_BaseCompartmentManagerComponent));
			
			bool isEmpty = (compMgr && compMgr.GetOccupantCount() == 0);
			
			if (isEmpty) {
				// Safety check: Never delete a vehicle owned by a player
				if (fac && fac.GetAffiliatedFaction() && fac.GetAffiliatedFaction().GetFactionKey() == JWK.GetGameConfig().GetPlayerFactionKey()) {
					return true;
				}
				
				m_aBM_EntitiesToClean.Insert(ent);
			}
			return true;
		}

		return true;
	}

	protected bool BM_FilterCharacters(IEntity ent)
	{
		return ent.IsInherited(ChimeraCharacter) || ent.IsInherited(Vehicle);
	}

	protected bool BM_CountForcesAction(IEntity ent)
	{
		// Handle Vehicles
		Vehicle vehicle = Vehicle.Cast(ent);
		if (vehicle) {
			DamageManagerComponent damageMgr = vehicle.GetDamageManager();
			if (damageMgr && damageMgr.IsDestroyed()) return true;
			
			SCR_BaseCompartmentManagerComponent compMgr = SCR_BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
			if (compMgr) {
				array<IEntity> occupants = {};
				compMgr.GetOccupants(occupants);
				foreach (IEntity occupant : occupants) {
					BM_CountForcesAction(occupant); // Recursively check characters inside
				}
			}
			return true;
		}

		// Handle Characters
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(ent);
		if (!character) return true;
		
		DamageManagerComponent damageMgr = character.GetDamageManager();
		if (damageMgr && damageMgr.IsDestroyed()) return true;

		SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(character);
		if (!fac) return true;
		
		Faction faction = fac.GetAffiliatedFaction();
		if (!faction) return true;
		
		string fKey = faction.GetFactionKey();

		if (fKey == m_sBM_InvaderFactionKey) {
			m_iTempInvaderCount++;
		} else {
			// Only count actual combatants as defenders (Players or USSR/Enemy faction)
			// Ignore ambient civilians, animals, etc.
			if (fKey == JWK.GetGameConfig().GetPlayerFactionKey() || fKey == JWK.GetGameConfig().GetEnemyFactionKey()) {
				m_iTempDefenderCount++;
			}
		}

		return true;
	}
	
	// Ensure the string replicates properly
	override bool RplSave(ScriptBitWriter writer)
	{
		if (!super.RplSave(writer)) return false;
		writer.WriteString(m_sBM_InvaderFactionKey);
		return true;
	}

	override bool RplLoad(ScriptBitReader reader)
	{
		if (!super.RplLoad(reader)) return false;
		if (!reader.ReadString(m_sBM_InvaderFactionKey)) return false;
		
		// Ensure joining client loads the faction
		if (!m_sBM_InvaderFactionKey.IsEmpty()) {
			JWK_FactionManager factionMgr = JWK_FactionManager.Cast(GetGame().GetFactionManager());
			if (factionMgr) factionMgr.BM_LoadFaction(m_sBM_InvaderFactionKey);
		}
		
		return true;
	}

	JWK_AIForce BM_GetInvaderForce()
	{
		return m_InvaderForce;
	}

	override void SaveState_S(JWK_GameModeSaveData saveData)
	{
		super.SaveState_S(saveData);

		if (saveData) {
			saveData.m_bBM_InvasionTriggered = m_bBM_InvasionTriggered;
			saveData.m_sBM_InvaderFactionKey = m_sBM_InvaderFactionKey;
			saveData.m_fBM_LastExpansionTime = m_fBM_LastExpansionTime;
			
			saveData.m_aInvaderGroupPrefabs.Clear();
			saveData.m_aInvaderGroupPositions.Clear();
			
			if (m_InvaderForce) {
				foreach (EntityID id : m_InvaderForce.GetGroups()) {
					SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(id));
					if (!group) continue;
					
					IEntity entityToSave = group;
					
					// BM_FIX: If the group is inside a vehicle, save the vehicle prefab instead so they don't spawn on foot
					array<AIAgent> agents = {};
					group.GetAgents(agents);
					foreach (AIAgent agent : agents) {
						IEntity charEnt = agent.GetControlledEntity();
						if (charEnt) {
							CompartmentAccessComponent compAccess = CompartmentAccessComponent.Cast(charEnt.FindComponent(CompartmentAccessComponent));
							if (compAccess) {
								BaseCompartmentSlot slot = compAccess.GetCompartment();
								if (slot) {
									Vehicle vic = Vehicle.Cast(slot.GetOwner());
									if (vic) {
										entityToSave = vic;
										break;
									}
								}
							}
						}
					}
					
					EntityPrefabData prefabData = entityToSave.GetPrefabData();
					if (!prefabData) continue;
					
					saveData.m_aInvaderGroupPrefabs.Insert(prefabData.GetPrefabName());
					saveData.m_aInvaderGroupPositions.Insert(entityToSave.GetOrigin());
				}
				Print("BM_Invasion: Saved " + saveData.m_aInvaderGroupPrefabs.Count() + " invader groups to database.", LogLevel.NORMAL);
			}
		}
	}

	override void LoadState_S(JWK_GameModeSaveData saveData)
	{
		super.LoadState_S(saveData);

		if (saveData) {
			m_bBM_InvasionTriggered = saveData.m_bBM_InvasionTriggered;
			m_sBM_InvaderFactionKey = saveData.m_sBM_InvaderFactionKey;
			
			// BM_FIX: Reset expansion time relative to current server time to prevent "Time Travel" stalling.
			m_fBM_LastExpansionTime = GetGame().GetWorld().GetWorldTime();
			
			if (!saveData.m_aInvaderGroupPrefabs.IsEmpty()) {
				// DELAYED RESTORATION: Wait for systems to wake up before spawning units
				GetGame().GetCallqueue().CallLater(BM_RestoreInvaderUnits, 10000, false, saveData);
			}
		}
	}

	protected void BM_RestoreInvaderUnits(JWK_GameModeSaveData saveData)
	{
		if (!saveData || saveData.m_aInvaderGroupPrefabs.IsEmpty()) return;

		int count = Math.Min(saveData.m_aInvaderGroupPrefabs.Count(), saveData.m_aInvaderGroupPositions.Count());
		Print("BM_Invasion: Restoration cycle started. Respawning " + count + " groups...", LogLevel.NORMAL);

		for (int i = 0; i < count; i++) {
			ResourceName prefab = saveData.m_aInvaderGroupPrefabs[i];
			vector pos = saveData.m_aInvaderGroupPositions[i];
			
			IEntity spawned = JWK_SpawnUtils.SpawnEntityPrefab(prefab, pos);
			if (spawned) {
				// BM_GC_FIX: Protect spawned units from getting immediately destroyed by ambient garbage collection
				BM_ProtectEntityFromGC(spawned);
				
				SCR_AIGroup group = SCR_AIGroup.Cast(spawned);
				if (!group) { // It might be a vehicle
					JWK_AmbientVehicleEventSpawner spawner = new JWK_AmbientVehicleEventSpawner();
					spawner.Init(JWK_AmbientTrafficSystem.Get());
					spawner.SpawnVehicleCrew(spawned, m_sBM_InvaderFactionKey, group);
				}
				
				if (group) {
					BM_ProtectGroupFromGC(group);
					
					if (!m_InvaderForce) {
						m_InvaderForce = new JWK_AIForce();
						m_InvaderForce.m_bAutoUnstuck = true;
					}
					
					JWK_CrewedVehicle cv = new JWK_CrewedVehicle();
					cv.m_Vehicle = Vehicle.Cast(spawned);
					cv.m_Crew = group;
					m_InvaderForce.AttachCrewedVehicle(cv);
					
					// Re-issue default orders to the current player position if invasion is active
					AIWaypoint snd = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.SEARCH_AND_DESTROY, group.GetOrigin());
					group.AddWaypoint(snd);
				}
			}
		}
	}
}

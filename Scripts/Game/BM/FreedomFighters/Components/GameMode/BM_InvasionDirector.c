modded class JWK_GameMode
{
	[Attribute(defvalue: "0", desc: "How many zones players must capture before the invasion triggers.", category: "BakerMods: Invasion")]
	protected int m_iBM_ZonesForInvasion;

	[Attribute(defvalue: "4", desc: "Maximum number of bases the Invaders will hold at once before stopping their expansion.", category: "BakerMods: Invasion")]
	protected int m_iBM_MaxInvaderBases;

	[Attribute(defvalue: "6", desc: "Base number of infantry groups spawned per invasion wave.", category: "BakerMods: Invasion")]
	protected int m_iBM_BaseInfantryGroups;

	[Attribute(defvalue: "3", desc: "Base number of vehicles spawned per invasion wave.", category: "BakerMods: Invasion")]
	protected int m_iBM_BaseVehicles;

	[Attribute(defvalue: "0", desc: "How many extra infantry groups to spawn per human player.", category: "BakerMods: Invasion")]
	protected int m_iBM_InfantryPerPlayer;

	[Attribute(defvalue: "0", desc: "How many extra vehicles to spawn per human player.", category: "BakerMods: Invasion")]
	protected int m_iBM_VehiclesPerPlayer;

	[Attribute(defvalue: "50", desc: "Maximum number of active Invader AI agents allowed before stopping new spawns.", category: "BakerMods: Invasion")]
	protected int m_iBM_MaxActiveAI;

	protected bool m_bBM_InvasionTriggered;
	protected bool m_bBM_InvaderHadBase;
	protected bool m_bBM_InvasionDefeated;
	protected string m_sBM_InvaderFactionKey = "None";	
	protected float m_fBM_LastExpansionTime;
	protected static const float BM_EXPANSION_INTERVAL = 300000; 
	
	protected EntityID m_iBM_CurrentTargetID;
	protected int m_iBM_CurrentTargetWaveCount;
	protected EntityID m_iBM_LastFailedTargetID = EntityID.INVALID;
	EntityID BM_GetCurrentTargetID() { return m_iBM_CurrentTargetID; }
	protected float m_fBM_LastReinforcementTime;
	protected float m_fBM_LastRoamingTime;
	protected float m_fBM_LastSynchronizedPatrolTime;
	protected float m_fBM_LastReassessmentTime;
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
		
		// STRICT RULE: No Helicopters or Aircraft
		bool isAirOrBase = resLower.Contains("helicopter") || resLower.Contains("heli") || resLower.Contains("plane") || resLower.Contains("air") || resLower.Contains("_base") || resLower.Contains("core");
		
		// STRICT RULE: No Trucks (Ural, M923, Zil, GAZ, etc.)
		bool isTruck = resLower.Contains("truck") || resLower.Contains("ural") || resLower.Contains("m923") || resLower.Contains("m35") || resLower.Contains("zil") || resLower.Contains("gaz") || resLower.Contains("van");

		if (isLogistical || isSpecialized || isMedical || isSupport || isAirOrBase || isTruck) 
		{
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

	protected bool BM_IsPlayerNearby(vector pos, float radius)
	{
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		float radiusSq = radius * radius;
		foreach (int playerId : players)
		{
			IEntity playerEnt = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
			if (playerEnt && vector.DistanceSqXZ(playerEnt.GetOrigin(), pos) < radiusSq) return true;
		}
		return false;
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
			bool isDestroyed = false;
			
			SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(ent);
			if (character) {
				DamageManagerComponent dmg = character.GetDamageManager();
				if (dmg && dmg.GetState() == EDamageState.DESTROYED) {
					shouldClean = true;
					isDestroyed = true;
				}
			}
			else if (ent.IsInherited(Vehicle)) {
				DamageManagerComponent dmg = DamageManagerComponent.Cast(ent.FindComponent(DamageManagerComponent));
				if (dmg && dmg.GetState() == EDamageState.DESTROYED) {
					shouldClean = true;
					isDestroyed = true;
				} else {
					SCR_BaseCompartmentManagerComponent compMgr = SCR_BaseCompartmentManagerComponent.Cast(ent.FindComponent(SCR_BaseCompartmentManagerComponent));
					bool hasAliveOccupants = false;
					if (compMgr) {
						array<IEntity> occupants = {};
						compMgr.GetOccupants(occupants);
						foreach (IEntity occ : occupants) {
							DamageManagerComponent occDmg = DamageManagerComponent.Cast(occ.FindComponent(DamageManagerComponent));
							if (occDmg && occDmg.GetState() != EDamageState.DESTROYED) {
								hasAliveOccupants = true;
								break;
							}
						}
					}
					
					if (!hasAliveOccupants) {
						SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
						// Never clean player-faction vehicles (safety)
						if (!fac || !fac.GetAffiliatedFaction() || fac.GetAffiliatedFaction().GetFactionKey() != JWK.GetGameConfig().GetPlayerFactionKey()) {
							shouldClean = true;
						}
					}
				}
			}
			else if (ent.IsInherited(SCR_AIGroup)) {
				SCR_AIGroup grp = SCR_AIGroup.Cast(ent);
				if (grp && grp.GetAgentsCount() == 0) shouldClean = true;
			}
			
			if (shouldClean) {
				// SMART CLEANUP: Distance check to avoid things "poofing" in front of players
				float safetyRadius = 150; // Standard for bodies/wrecks
				if (ent.IsInherited(Vehicle) && !isDestroyed) safetyRadius = 400; // Larger for functional abandoned cars

				if (BM_IsPlayerNearby(ent.GetOrigin(), safetyRadius)) continue;

				// If we are here, it's safe to remove. 
				// We use direct deletion if far enough, or re-insert into GC with aggressive settings if near-ish.
				if (!BM_IsPlayerNearby(ent.GetOrigin(), 600))
				{
					SCR_EntityHelper.DeleteEntityAndChildren(ent);
					Print("BM_Invasion: Smart GC: Deleted abandoned/destroyed entity " + ent.ToString(), LogLevel.NORMAL);
				}
				else
				{
					// Near players but outside safety radius, hand back to GC but with a very high priority
					gcManager.Insert(ent, 15, 50); 
				}
				
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
			if (m_bBM_InvasionDefeated) return; // PERMANENT DEFEAT: Do not trigger again.

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
			
			// --- Active Re-assessment Every 1 Min ---
			if (currentTime - m_fBM_LastReassessmentTime > 60000)
			{
				m_fBM_LastReassessmentTime = currentTime;
				BM_ReassessInvaderOrders();
				
				// --- BAKERMODS: DEFENDER WIN CONDITION ---
				if (m_iBM_CurrentTargetID != EntityID.INVALID) {
					IEntity currentTargetEnt = GetGame().GetWorld().FindEntityByID(m_iBM_CurrentTargetID);
					if (currentTargetEnt) {
						SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(currentTargetEnt);
						if (fac && fac.GetAffiliatedFactionKey() == JWK.GetGameConfig().GetPlayerFactionKey()) {
							if (m_iBM_CurrentTargetWaveCount >= 3) {
								int activeInvadersAtTarget = 0;
								if (m_InvaderForce) activeInvadersAtTarget = JWK_AIUtils.GetAIForceAgentsCountInArea(m_InvaderForce, currentTargetEnt.GetOrigin(), 400);
								
								if (activeInvadersAtTarget == 0) {
									// DEFENDERS WON!
									m_iBM_LastFailedTargetID = m_iBM_CurrentTargetID;
									m_iBM_CurrentTargetID = EntityID.INVALID;
									m_iBM_CurrentTargetWaveCount = 0;
									m_fBM_CurrentCooldown = 3600000; // 1 Hour cooldown
									
									// Force battle win for players
									JWK_BattleSubjectComponent battleSubj = JWK_CompTU<JWK_BattleSubjectComponent>.FindIn(currentTargetEnt);
									if (battleSubj && JWK.GetBattleManager()) {
										JWK_BattleControllerEntity ctrl = JWK.GetBattleManager().GetController();
										if (ctrl && ctrl.GetSubject_S() == battleSubj) {
											int playerFactionId = GetGame().GetFactionManager().GetFactionIndex(GetGame().GetFactionManager().GetFactionByKey(JWK.GetGameConfig().GetPlayerFactionKey()));
											ctrl.ForceFinish_S(playerFactionId);
										}
									}
									
									string tName = "the objective";
									JWK_NamedLocationComponent tLoc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(currentTargetEnt);
									if (tLoc) tName = tLoc.GetName();
									
									string msgWin = "VICTORY: The local resistance has successfully defended " + tName + " against all invader waves!";
									JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msgWin);
									Print("BM_Invasion: Defenders successfully held " + currentTargetEnt.GetPrefabData().GetPrefabName() + " after 3 waves. Forcing 1-hour delay.", LogLevel.NORMAL);
								}
							}
						}
					}
				}
				
				// --- WIN/LOSS CONDITION CHECK ---
				SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
				if (invaderFaction)
				{
					int currentBases = 0;
					array<EntityID> outIds = {};
					outIds.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity));
					outIds.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity));
					outIds.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_AirportEntity));
					
					foreach (EntityID id : outIds) {
						IEntity ent = GetGame().GetWorld().FindEntityByID(id);
						SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
						if (fac && fac.GetAffiliatedFaction() == invaderFaction) currentBases++;
					}
					
					// If they hold at least one base, they have successfully established a presence
					if (currentBases > 0) m_bBM_InvaderHadBase = true;
					
					// If they HAD a presence but now have 0 bases, they are defeated
					if (m_bBM_InvaderHadBase && currentBases == 0)
					{
						m_bBM_InvasionTriggered = false;
						m_bBM_InvasionDefeated = true; // NEW: Lock them out permanently!
						m_iBM_CurrentTargetID = EntityID.INVALID;
						
						string msg = "VICTORY: The " + invaderFaction.GetFactionName() + " invasion has been completely repelled! The sector is secure once again.";
						JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);
						
						Print("BM_Invasion: Invasion DEFEATED and permanently deactivated.", LogLevel.NORMAL);
						return; // Stop processing further to allow the defeat to stick
					}
				}
			}
			// ------------------------------------------
			
			// --- Reinforcements & Target Check Every 2 Mins ---
			if (currentTime - m_fBM_LastReinforcementTime > 120000)
			{
				m_fBM_LastReinforcementTime = currentTime;
				
				SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
				if (invaderFaction)
				{
					if (m_iBM_CurrentTargetID != EntityID.INVALID)
					{
						IEntity currentTargetEnt = GetGame().GetWorld().FindEntityByID(m_iBM_CurrentTargetID);
						if (currentTargetEnt)
						{
							SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(currentTargetEnt);
							if (fac && fac.GetAffiliatedFactionKey() != m_sBM_InvaderFactionKey)
							{
								int currentAI = 0;
								if (m_InvaderForce) currentAI = m_InvaderForce.CountAgents();
								
								if (currentAI < (m_iBM_MaxActiveAI * 0.5))
								{
									bool isPlayerBase = (fac.GetAffiliatedFactionKey() == JWK.GetGameConfig().GetPlayerFactionKey());
									if (isPlayerBase && m_iBM_CurrentTargetWaveCount >= 3) {
										Print("BM_Invasion: Max waves (3) reached. Halting reinforcements to player base.", LogLevel.NORMAL);
									} else {
										if (isPlayerBase) m_iBM_CurrentTargetWaveCount++;
										
										string name = "the objective";
										JWK_NamedLocationComponent loc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(currentTargetEnt);
										if (loc) name = loc.GetName();
										
										string msg = "REBEL INTEL: Additional " + invaderFaction.GetFactionName() + " units have been sighted moving to reinforce the attack on " + name + ".";
										JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);
										
										Print("BM_Invasion: Reinforcing attack on " + currentTargetEnt.GetPrefabData().GetPrefabName() + " (Wave " + m_iBM_CurrentTargetWaveCount + ")", LogLevel.NORMAL);
										IEntity sourceBase = BM_FindInvaderHeldBase(invaderFaction);
										vector sourcePos = "0 0 0";
										if (sourceBase) sourcePos = sourceBase.GetOrigin();
										BM_SpawnInvasionForce(currentTargetEnt.GetOrigin(), invaderFaction, sourcePos);
									}
								}
							}
							else
							{
								m_iBM_CurrentTargetID = EntityID.INVALID;
							}
						}
						else
						{
							m_iBM_CurrentTargetID = EntityID.INVALID;
						}
					}
					
					// If target was cleared, try to find a new one immediately so reassessment can pick it up
					if (m_iBM_CurrentTargetID == EntityID.INVALID)
					{
						int currentBases = 0;
						array<EntityID> outIds = {};
						outIds.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity));
						outIds.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity));
						
						foreach (EntityID id : outIds) {
							IEntity loopEnt = GetGame().GetWorld().FindEntityByID(id);
							SCR_FactionAffiliationComponent facCheck = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(loopEnt);
							if (facCheck && facCheck.GetAffiliatedFaction() == invaderFaction) currentBases++;
						}

						// NEW LOGIC: Only find a new target if we are under the max limit
						if (currentBases < m_iBM_MaxInvaderBases) 
						{
							IEntity sourceBase = BM_FindInvaderHeldBase(invaderFaction);
							IEntity target;
							if (!sourceBase)
							{
								SCR_Faction occupierFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(JWK.GetGameConfig().GetEnemyFactionKey()));
								target = BM_FindInvasionTarget(occupierFaction);
							}
							else
							{
								target = BM_FindNextExpansionTarget(sourceBase.GetOrigin(), invaderFaction);
							}
							
							if (target) {
								m_iBM_CurrentTargetID = target.GetID();
								
								string tName = "a new sector";
								JWK_NamedLocationComponent tLoc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(target);
								if (tLoc) tName = tLoc.GetName();
								
								string tMsg = "REBEL INTEL: The " + invaderFaction.GetFactionName() + " forces are re-grouping and pushing towards " + tName + ".";
								JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, tMsg);
								
								Print("BM_Invasion: Objective captured. Re-focusing existing forces on: " + target.GetPrefabData().GetPrefabName(), LogLevel.NORMAL);
							}
						} 
						else 
						{
							Print("BM_Invasion: MAX BASES (" + m_iBM_MaxInvaderBases + ") REACHED. Halting expansion, transitioning to defense.", LogLevel.NORMAL);
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

			// --- Synchronized Global Patrol Check Every 12 Mins ---
			if (currentTime - m_fBM_LastSynchronizedPatrolTime > 720000)
			{
				m_fBM_LastSynchronizedPatrolTime = currentTime;
				SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
				if (invaderFaction) BM_SpawnSynchronizedPatrols(invaderFaction);
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
						
						if (penalty > 60000) {
							m_fBM_CurrentCooldown = 3600000; // 1 HOUR DELAY
							SCR_Faction invaderFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
							if (invaderFaction) {
								string msg = "REBEL INTEL: Heavy losses have forced the " + invaderFaction.GetFactionName() + " command to delay their next offensive for an extended period.";
								JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);
							}
							Print("BM_Invasion: High casualties detected. Forcing next wave cooldown to 1 hour.", LogLevel.NORMAL);
						} else {
							m_fBM_CurrentCooldown += penalty;
							Print("BM_Invasion: Casualties detected. Delaying next wave by " + (penalty/1000).ToString() + "s", LogLevel.NORMAL);
						}
					}
					m_iBM_LastAliveCount = alive;
				}

				// Halt Capture Logic: Limit maximum territory
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

				if (currentBases < m_iBM_MaxInvaderBases) {
					BM_ExecuteExpansionWave();
				} else {
					Print("BM_Invasion: Skipping Expansion Wave (Max Bases Reached). Defending current territory.", LogLevel.NORMAL);
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
					int currentBasesCheck = 0;
					array<EntityID> basesCheck = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
					foreach (EntityID id : basesCheck) {
						IEntity ent = GetGame().GetWorld().FindEntityByID(id);
						SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
						if (fac && fac.GetAffiliatedFaction() == invaderFaction) currentBasesCheck++;
					}

					// Only notify if they are still expanding
					if (currentBasesCheck < m_iBM_MaxInvaderBases) {
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

	protected void BM_SpawnSynchronizedPatrols(Faction faction)
	{
		IEntity sourceBase = BM_FindInvaderHeldBase(faction);
		if (!sourceBase) return;

		vector pos = sourceBase.GetOrigin();
		
		array<EntityID> potentialTargets = {};
		potentialTargets.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity));
		potentialTargets.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_TownEntity));
		potentialTargets.InsertAll(JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity));
		
		if (potentialTargets.Count() < 6) return;

		// We will spawn 2 separate patrols in different directions
		int patrolCount = 2;
		string factionName = faction.GetFactionName();
		
		for (int p = 0; p < patrolCount; p++)
		{
			// Pick a random sector of the map far from the current objective
			IEntity targetSector = null;
			for (int attempt = 0; attempt < 10; attempt++)
			{
				EntityID id = potentialTargets.GetRandomElement();
				targetSector = GetGame().GetWorld().FindEntityByID(id);
				if (targetSector && targetSector.GetID() != m_iBM_CurrentTargetID)
				{
					// Ensure it's at least 1km away from the main fight if possible
					IEntity mainTarget = GetGame().GetWorld().FindEntityByID(m_iBM_CurrentTargetID);
					if (mainTarget && vector.Distance(targetSector.GetOrigin(), mainTarget.GetOrigin()) > 1000) break;
				}
			}
			
			if (!targetSector) continue;

			vector spawnPos = pos + Vector(JWK.Random.RandFloatXY(-150, 150), 0, JWK.Random.RandFloatXY(-150, 150));
			JWK_Road road; int roadIdx;
			if (JWK_RoadNetworkManagerComponent.GetInstance().GetClosestRoad(spawnPos, road, roadIdx, 400)) spawnPos = road.points[roadIdx];
			
			spawnPos[1] = GetGame().GetWorld().GetSurfaceY(spawnPos[0], spawnPos[2]) + 1.2;

			SCR_EntityCatalog catalog = SCR_Faction.Cast(faction).GetFactionEntityCatalogOfType(EEntityCatalogType.VEHICLE);
			if (!catalog) continue;
			
			array<SCR_EntityCatalogEntry> entries = {};
			catalog.GetEntityList(entries);
			array<ResourceName> validPrefabs = {};
			foreach (SCR_EntityCatalogEntry entry : entries) {
				if (BM_IsVehicleValid(entry.GetPrefab())) validPrefabs.Insert(entry.GetPrefab());
			}

			if (validPrefabs.IsEmpty()) continue;
			
			IEntity spawned = JWK_SpawnUtils.SpawnEntityPrefab(validPrefabs.GetRandomElement(), spawnPos);
			if (!spawned) continue;

			BM_ProtectEntityFromGC(spawned);
			
			SCR_AIGroup group;
			JWK_AmbientVehicleEventSpawner spawner = new JWK_AmbientVehicleEventSpawner();
			spawner.Init(JWK_AmbientTrafficSystem.Get());
			spawner.SpawnVehicleCrew(spawned, faction.GetFactionKey(), group);
			
			if (group) {
				BM_ProtectGroupFromGC(group);
				
				// Assign a long-range patrol route through that sector
				array<AIWaypoint> waypoints = {};
				vector sectorPos = targetSector.GetOrigin();
				
				for (int i = 0; i < 3; i++) {
					vector wpPos = sectorPos + Vector(JWK.Random.RandFloatXY(-400, 400), 0, JWK.Random.RandFloatXY(-400, 400));
					AIWaypoint wp = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.MOVE, wpPos);
					group.AddWaypoint(wp);
					waypoints.Insert(wp);
				}
				
				group.AddWaypoint(JWK.GetAIManager().SpawnCycleWaypoint(group.GetOrigin(), waypoints));
				
				if (!m_InvaderForce) m_InvaderForce = new JWK_AIForce();
				JWK_CrewedVehicle cv = new JWK_CrewedVehicle();
				cv.m_Vehicle = Vehicle.Cast(spawned);
				cv.m_Crew = group;
				m_InvaderForce.AttachCrewedVehicle(cv);

				// RADIO NOTIFICATION
				string locationName = "a remote sector";
				JWK_NamedLocationComponent loc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(targetSector);
				if (loc) locationName = loc.GetName();

				string msg = "RADIO CHATTER: High-frequency signal intercepted! " + factionName + " motorized elements are reported patrolling near " + locationName + ".";
				JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);
				
				Print("BM_Invasion: Dispatched synchronized map-wide patrol to " + locationName, LogLevel.NORMAL);
			}
		}
	}

	protected void BM_TriggerInvasion()
	{
		m_bBM_InvasionTriggered = true;
		m_fBM_LastExpansionTime = GetGame().GetWorld().GetWorldTime();
		m_fBM_LastReassessmentTime = m_fBM_LastExpansionTime;
		m_fBM_LastSynchronizedPatrolTime = m_fBM_LastExpansionTime;
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
				m_iBM_CurrentTargetWaveCount = 0; // Reset wave count
				Print("BM_Invasion: Selected new objective: " + target.GetPrefabData().GetPrefabName(), LogLevel.NORMAL);
			}
		}

		if (!target) return;

		bool isPlayerBase = false;
		SCR_FactionAffiliationComponent targetFac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(target);
		if (targetFac && targetFac.GetAffiliatedFactionKey() == JWK.GetGameConfig().GetPlayerFactionKey()) {
			isPlayerBase = true;
		}

		if (isPlayerBase && m_iBM_CurrentTargetWaveCount >= 3) {
			Print("BM_Invasion: Max waves (3) reached for player base. Halting expansion wave.", LogLevel.NORMAL);
			return;
		}
		
		if (isPlayerBase) {
			m_iBM_CurrentTargetWaveCount++;
		}

		// RE-TASKING LOGIC: If we have plenty of AI, just send them to the new target instead of spawning new ones
		if (currentAI >= m_iBM_MaxActiveAI)
		{
			Print("BM_Invasion: AI Limit reached (" + currentAI + "). Re-tasking existing units to " + target.GetPrefabData().GetPrefabName(), LogLevel.NORMAL);
			
			foreach (EntityID groupId : m_InvaderForce.GetGroups())
			{
				SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupId));
				if (!group || group.GetAgentsCount() == 0) continue;
				
				// BM_FIX: Do not re-task groups currently in combat/reacting to danger
				if (group.GetDangerEventsCount() > 0) continue;

				// Clear old waypoints and use FF sweep logic to ensure they don't get stuck
				array<AIWaypoint> currentWaypoints = {};
				group.GetWaypoints(currentWaypoints);
				foreach (AIWaypoint wp : currentWaypoints) group.RemoveWaypoint(wp);
				
				vector groupTarget = target.GetOrigin() + Vector(JWK.Random.RandFloatXY(-50, 50), 0, JWK.Random.RandFloatXY(-50, 50));
				
				// Prioritize getting to the new target objective quickly but safely
				AIWaypoint move = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.MOVE, groupTarget);
				group.AddWaypoint(move);
				
				JWK_AIUtils.AddAttackAndSweepWaypoints(group, groupTarget);
			}
			return;
		}

		string name = "the frontlines";
		JWK_NamedLocationComponent loc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(target);
		if (loc) name = loc.GetName();
		
		string msg = "URGENT: Intelligence reports a major " + invaderFaction.GetFactionName() + " offensive is underway towards " + name + "!";
		JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);

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
			if (id == m_iBM_LastFailedTargetID) continue;
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
			if (id == m_iBM_LastFailedTargetID) continue; // Skip last failed base
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
		// FACTION LOCK: Ensure we are actually spawning the Invader, even if the caller passed the wrong faction
		SCR_Faction actualFaction = SCR_Faction.Cast(faction);
		if (!actualFaction || actualFaction.GetFactionKey() != m_sBM_InvaderFactionKey)
		{
			actualFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
			if (!actualFaction) {
				// Final attempt to load
				JWK_FactionManager.Cast(GetGame().GetFactionManager()).BM_LoadFaction(m_sBM_InvaderFactionKey);
				actualFaction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sBM_InvaderFactionKey));
			}
		}
		
		if (!actualFaction) return;
		faction = actualFaction;

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

			// Add a slight random offset so they don't all drive to the exact same coordinate and ram each other
			vector offset = Vector(JWK.Random.RandFloatXY(-100, 100), 0, JWK.Random.RandFloatXY(-100, 100));
			vector finalPos = targetPos + offset;

			// COMBAT MOVE: Let them engage enemies on the way to the target
			AIWaypoint move = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.MOVE, finalPos);
			group.AddWaypoint(move);

			// DISMOUNT: Force the AI to exit the vehicles upon arrival so they can spread out
			AIWaypoint getOut = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.GET_OUT, finalPos);
			group.AddWaypoint(getOut);

			// Add the native FF Attack and Sweep cycle once they arrive and dismount
			JWK_AIUtils.AddAttackAndSweepWaypoints(group, finalPos);
			
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

	protected void BM_ReassessInvaderOrders()
	{
		if (!m_InvaderForce) return;
		
		IEntity currentTarget = null;
		if (m_iBM_CurrentTargetID != EntityID.INVALID)
			currentTarget = GetGame().GetWorld().FindEntityByID(m_iBM_CurrentTargetID);
		
		if (!currentTarget) return;
		
		vector targetPos = currentTarget.GetOrigin();
		
		array<EntityID> groupIds = m_InvaderForce.GetGroups();
		for (int i = 0; i < groupIds.Count(); i++)
		{
			EntityID groupId = groupIds[i];
			SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupId));
			if (!group || group.GetAgentsCount() == 0) continue;
			
			vector groupPos = group.GetOrigin();
			float distToTarget = vector.Distance(groupPos, targetPos);
			
			array<AIWaypoint> waypoints = {};
			group.GetWaypoints(waypoints);
			
			bool shouldReorder = false;
			bool isAssisting = false;
			vector assistPos;
			
			// --- MUTUAL SUPPORT LOGIC ---
			// If they have no orders, check if they should assist a nearby ally or hunt a threat
			if (waypoints.IsEmpty())
			{
				shouldReorder = true;
				
				// TACTICAL ASSIST: Check if any other invader group within 300m is taking fire
				foreach (EntityID otherId : groupIds)
				{
					if (otherId == groupId) continue;
					SCR_AIGroup otherGroup = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(otherId));
					if (otherGroup && otherGroup.GetDangerEventsCount() > 0)
					{
						vector otherPos = otherGroup.GetOrigin();
						if (vector.Distance(groupPos, otherPos) < 300)
						{
							isAssisting = true;
							assistPos = otherPos;
							break;
						}
					}
				}
			}
			else
			{
				// RELEVANCE CHECK: If they are far away, ensure their current destination matches the active objective
				if (distToTarget > 250)
				{
					AIWaypoint lastWp = waypoints[waypoints.Count() - 1];
					if (lastWp)
					{
						vector wpPos = lastWp.GetOrigin();
						if (vector.Distance(wpPos, targetPos) > 300)
						{
							shouldReorder = true;
						}
					}
				}
			}
			
			if (shouldReorder)
			{
				// Clear existing waypoints
				for (int j = waypoints.Count() - 1; j >= 0; j--)
				{
					group.RemoveWaypoint(waypoints[j]);
				}
				
				// --- ARMOR & CREW RETENTION LOGIC (Detection) ---
				bool inVehicle = false;
				bool vehicleHasGunner = false;
				IEntity vehicle = null;
				array<AIAgent> agents = {};
				group.GetAgents(agents);
				foreach (AIAgent agent : agents)
				{
					IEntity charEnt = agent.GetControlledEntity();
					if (charEnt && charEnt.GetParent() && charEnt.GetParent().IsInherited(Vehicle)) 
					{
						inVehicle = true;
						vehicle = charEnt.GetParent();
						break;
					}
				}
				
				if (inVehicle && vehicle)
				{
					SCR_BaseCompartmentManagerComponent compMgr = SCR_BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
					if (compMgr)
					{
						array<BaseCompartmentSlot> compartments = {};
						compMgr.GetCompartments(compartments);
						foreach (BaseCompartmentSlot slot : compartments)
						{
							if (slot.GetType() == ECompartmentType.TURRET && slot.GetOccupant())
							{
								vehicleHasGunner = true;
								break;
							}
						}
					}
				}

				// TARGET DETERMINATION: Objective vs Assist
				vector finalPos;
				float radius;
				float angle;

				if (isAssisting)
				{
					// Move to assist ally but with a slight flank offset (40m)
					radius = 40.0;
					angle = (i - (i / 12) * 12) * 30.0;
					finalPos = assistPos + Vector(Math.Sin(angle * Math.DEG2RAD) * radius, 0, Math.Cos(angle * Math.DEG2RAD) * radius);
					Print("BM_Invasion: Group " + groupId.ToString() + " moving to ASSIST nearby unit in combat.", LogLevel.NORMAL);
				}
				else
				{
					// Standard Objective Push logic
					int angleIdx = i - (i / 12) * 12;
					angle = angleIdx * 30.0;
					radius = JWK.Random.RandFloatXY(50, 140);
					
					// Infantry push directly into core
					if (!inVehicle && distToTarget < 250)
					{
						radius = JWK.Random.RandFloatXY(5, 25);
						angle = JWK.Random.RandFloat01() * 360;
					}
					
					finalPos = targetPos + Vector(Math.Sin(angle * Math.DEG2RAD) * radius, 0, Math.Cos(angle * Math.DEG2RAD) * radius);
				}
				
				AIWaypoint move = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.MOVE, finalPos);
				group.AddWaypoint(move);
				
				if (inVehicle)
				{
					if (vehicleHasGunner)
					{
						if (distToTarget < 200)
						{
							foreach (AIAgent agent : agents)
							{
								IEntity charEnt = agent.GetControlledEntity();
								if (!charEnt) continue;
								CompartmentAccessComponent compAccess = CompartmentAccessComponent.Cast(charEnt.FindComponent(CompartmentAccessComponent));
								if (compAccess && compAccess.IsInCompartment())
								{
									BaseCompartmentSlot slot = compAccess.GetCompartment();
									if (slot && slot.GetType() == ECompartmentType.CARGO)
									{
										compAccess.GetOutVehicle(0, -1, 0, false);
									}
								}
							}
						}
					}
					else
					{
						AIWaypoint getOut = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.GET_OUT, finalPos);
						group.AddWaypoint(getOut);
					}
				}
				
				// Use Search and Destroy to ensure they engage threats while moving/assisting
				JWK_AIUtils.AddAttackAndSweepWaypoints(group, finalPos, 60);
				
				if (!isAssisting) Print("BM_Invasion: Re-ordered group " + groupId.ToString() + " to Sector Angle " + angle.ToString() + " at " + finalPos.ToString(), LogLevel.NORMAL);
			}
		}
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
				
				// BM_NOTIFICATION: Tell the players they lost a base to the invaders
				string baseName = "an objective";
				JWK_NamedLocationComponent loc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(ent);
				if (loc) baseName = loc.GetName();
				
				string msg = "URGENT: Reports confirm that " + invaderFaction.GetFactionName() + " forces have successfully seized control of " + baseName + "!";
				JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, msg);

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
			
			// Always cleanup dead bodies instantly, but check for players first
			if (damageMgr && damageMgr.GetState() == EDamageState.DESTROYED) {
				if (!BM_IsPlayerNearby(ent.GetOrigin(), 100))
					m_aBM_EntitiesToClean.Insert(ent);
				return true;
			}
			
			return true;
		}
		
		// 2. Vehicles
		if (ent.IsInherited(Vehicle)) {
			DamageManagerComponent damageMgr = DamageManagerComponent.Cast(ent.FindComponent(DamageManagerComponent));
			
			// Cleanup destroyed vehicles instantly if no players nearby
			if (damageMgr && damageMgr.GetState() == EDamageState.DESTROYED) {
				if (!BM_IsPlayerNearby(ent.GetOrigin(), 150))
					m_aBM_EntitiesToClean.Insert(ent);
				return true;
			}

			// For SURVIVING vehicles: Check if it's an abandoned vehicle (empty) and non-player
			SCR_FactionAffiliationComponent fac = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			SCR_BaseCompartmentManagerComponent compMgr = SCR_BaseCompartmentManagerComponent.Cast(ent.FindComponent(SCR_BaseCompartmentManagerComponent));
			
			bool hasAliveOccupants = false;
			if (compMgr) {
				array<IEntity> occupants = {};
				compMgr.GetOccupants(occupants);
				foreach (IEntity occ : occupants) {
					DamageManagerComponent occDmg = DamageManagerComponent.Cast(occ.FindComponent(DamageManagerComponent));
					if (occDmg && occDmg.GetState() != EDamageState.DESTROYED) {
						hasAliveOccupants = true;
						break;
					}
				}
			}
			
			if (!hasAliveOccupants) {
				// Safety check: Never delete a vehicle owned by a player
				if (fac && fac.GetAffiliatedFaction() && fac.GetAffiliatedFaction().GetFactionKey() == JWK.GetGameConfig().GetPlayerFactionKey()) {
					return true;
				}
				
				// Abandoned vehicle cleanup: stricter player proximity check
				if (!BM_IsPlayerNearby(ent.GetOrigin(), 250))
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
			if (damageMgr && damageMgr.GetState() == EDamageState.DESTROYED) return true;
			
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
		if (damageMgr && damageMgr.GetState() == EDamageState.DESTROYED) return true;

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
			saveData.m_bBM_InvaderHadBase = m_bBM_InvaderHadBase;
			saveData.m_bBM_InvasionDefeated = m_bBM_InvasionDefeated;
			saveData.m_sBM_InvaderFactionKey = m_sBM_InvaderFactionKey;
			saveData.m_fBM_LastExpansionTime = m_fBM_LastExpansionTime;
			saveData.m_iBM_CurrentTargetWaveCount = m_iBM_CurrentTargetWaveCount;
			saveData.m_iBM_LastFailedTargetID = m_iBM_LastFailedTargetID;
			
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

	protected ref array<string> m_aTempRestorationPrefabs = new array<string>();
	protected ref array<vector> m_aTempRestorationPositions = new array<vector>();

	override void LoadState_S(JWK_GameModeSaveData saveData)
	{
		super.LoadState_S(saveData);

		if (saveData) {
			m_bBM_InvasionTriggered = saveData.m_bBM_InvasionTriggered;
			m_bBM_InvaderHadBase = saveData.m_bBM_InvaderHadBase;
			m_bBM_InvasionDefeated = saveData.m_bBM_InvasionDefeated;
			m_sBM_InvaderFactionKey = saveData.m_sBM_InvaderFactionKey;
			m_iBM_CurrentTargetWaveCount = saveData.m_iBM_CurrentTargetWaveCount;
			m_iBM_LastFailedTargetID = saveData.m_iBM_LastFailedTargetID;
			
			// FORCED PERSISTENCE FIX: Re-load the faction immediately on resume
			// This ensures the Invader is a "known entity" before bases stream in.
			if (!m_sBM_InvaderFactionKey.IsEmpty() && m_sBM_InvaderFactionKey != "None")
			{
				JWK_FactionManager factionMgr = JWK_FactionManager.Cast(GetGame().GetFactionManager());
				if (factionMgr) factionMgr.BM_LoadFaction(m_sBM_InvaderFactionKey);
			}
			
			// BM_FIX: Reset expansion time relative to current server time to prevent "Time Travel" stalling.
			m_fBM_LastExpansionTime = GetGame().GetWorld().GetWorldTime();
			
			if (!saveData.m_aInvaderGroupPrefabs.IsEmpty()) {
				m_aTempRestorationPrefabs.Copy(saveData.m_aInvaderGroupPrefabs);
				m_aTempRestorationPositions.Copy(saveData.m_aInvaderGroupPositions);
				
				// DELAYED RESTORATION: Wait for systems to wake up before spawning units
				GetGame().GetCallqueue().CallLater(BM_RestoreInvaderUnits, 10000, false);
			}
		}
	}

	protected void BM_RestoreInvaderUnits()
	{
		if (m_aTempRestorationPrefabs.IsEmpty()) return;

		int count = Math.Min(m_aTempRestorationPrefabs.Count(), m_aTempRestorationPositions.Count());
		Print("BM_Invasion: Restoration cycle started. Respawning " + count + " groups...", LogLevel.NORMAL);

		for (int i = 0; i < count; i++) {
			ResourceName prefab = m_aTempRestorationPrefabs[i];
			vector pos = m_aTempRestorationPositions[i];
			
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
					
					Vehicle vic = Vehicle.Cast(spawned);
					if (vic) {
						JWK_CrewedVehicle cv = new JWK_CrewedVehicle();
						cv.m_Vehicle = vic;
						cv.m_Crew = group;
						m_InvaderForce.AttachCrewedVehicle(cv);
					} else {
						m_InvaderForce.AttachGroup(group);
					}
					
					// Re-issue default orders to the current player position if invasion is active
					AIWaypoint snd = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.SEARCH_AND_DESTROY, group.GetOrigin());
					group.AddWaypoint(snd);
				}
			}
		}
		
		m_aTempRestorationPrefabs.Clear();
		m_aTempRestorationPositions.Clear();
	}
}

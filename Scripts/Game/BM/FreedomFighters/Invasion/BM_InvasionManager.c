// ==========================================
// BM_InvasionManager.c
// THE AAA STRATEGIC COMMAND - UNIFIED BRAIN
// ==========================================
// BakerMods override:
//   Session 4 fixes:
//     - Siege system: AI-vs-AI base capture without FF battle system
//     - Vehicle spawning: VEHICLE catalog + SpawnVehicleCrew (not GROUP catalog)
//     - Defender streaming: spawns base owner faction garrison on invader approach
//     - Base capture: JWK_FactionControlComponent.ChangeControlToFaction (auto-updates map)
//   Previous fixes retained: 4, 6, 7, 9, 11, 12, 13, 14, 15, 18, 19

// Tracks an active invader siege at a base
class BM_SiegeState
{
	EntityID m_BaseID;
	ref JWK_AIForce m_DefenderForce;
	float m_fStartTime;
}

[EntityEditorProps(category: "BakerMods/Invasion", description: "Standalone Strategic Brain Entity.")]
class BM_InvasionManagerClass : GenericEntityClass {}

class BM_InvasionManager : GenericEntity
{
	static string s_InvaderFactionKey = "USSR";

	string m_sInvaderFaction = "USSR";
	int m_iTickets = 5000;
	int m_iMinBases = 0;
	bool m_bInstant = false;

	bool m_bActive = false;
	int m_iPhase = 0;
	string m_sTargetBaseName = "";
	string m_sHQPersistentID = "";
	vector m_vHQPos = vector.Zero;

	protected bool m_bIsPersistenceLoaded = false;
	protected bool m_bReconstitutionNeeded = false;

	protected float m_fLastActionTime;
	protected float m_fLastSweepTime;
	protected IEntity m_HQ;
	protected ref JWK_AIForce m_InvaderForce;

	// Fix 6: convoy-in-flight guard — stays locked until troops die or target captured
	protected bool m_bConvoyEnRoute = false;

	// Fix 7: passive ticket replenishment counter (heartbeats = 2s each)
	protected int m_iTickReplenishCounter = 0;
	static const int TICK_REPLENISH_INTERVAL = 15; // every 30s

	// Vehicle tracking + abandoned cleanup
	protected ref array<EntityID> m_aInvaderVehicleIDs = {};
	protected ref map<EntityID, float> m_mAbandonedVehicleTimes = new map<EntityID, float>();
	static const float VEHICLE_ABANDON_GRACE_MS = 300000;      // 5 min empty before releasing to GC
	static const float VEHICLE_ABANDON_NEAR_TARGET_MS = 60000;  // 60s near target — clear clutter fast

	// Fix 15: bridgehead retry cooldown
	protected int m_iBridgeheadRetryCooldownMs = 0;

	// Siege system: AI-vs-AI base capture
	protected ref array<ref BM_SiegeState> m_aSieges = {};
	protected int m_iSiegeCheckCounter = 0;
	static const int SIEGE_CHECK_INTERVAL = 5;    // every 10s (5 heartbeats * 2s)
	static const float SIEGE_DETECT_RANGE = 200;
	static const float SIEGE_DETECT_RANGE_SQ = 40000;
	static const float SIEGE_GRACE_PERIOD_MS = 45000; // 45s before checking outcome

	// Ambient patrol system: 2 roaming infantry patrols for war ambiance (free, no tickets)
	protected ref array<SCR_AIGroup> m_aPatrolGroups = {};
	protected int m_iPatrolCheckCounter = 0;
	static const int PATROL_CHECK_INTERVAL = 15; // every 30s (15 heartbeats * 2s)
	static const int PATROL_MAX = 2;

	// Track bases the invaders have ever owned — if lost, prioritize retaking them
	protected ref set<string> m_sEverOwnedBaseNames = new set<string>();

	// Persistence: base names to re-capture after EPF restores (EPF overwrites faction with role-based lookup)
	ref array<string> m_aBM_PendingCapturedBaseNames;

	// --------------------------------------------------------------------------------------------------------

	static BM_InvasionManager GetInstance()
	{
		IEntity brain = GetGame().GetWorld().FindEntityByName("BM_Invasion_Brain");
		return BM_InvasionManager.Cast(brain);
	}

	void InitBrain()
	{
		if (Replication.IsServer())
		{
			SetName("BM_Invasion_Brain");

			JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
			if (gm && gm.IsNewGame_S()) m_bIsPersistenceLoaded = true;

			GetGame().GetCallqueue().CallLater(Heartbeat_S, 2000, true);
		}
	}

	void BM_OnPersistenceRestored()
	{
		m_bIsPersistenceLoaded = true;

		// Allow first action after a short delay (30s) rather than a full interval
		float now = GetGame().GetWorld().GetWorldTime();
		int actionIntervalMs = 300000;
		if (m_iPhase == 3) actionIntervalMs = 180000;
		if (m_iPhase == 4) actionIntervalMs = 120000;
		m_fLastActionTime = now - actionIntervalMs + 30000;

		// Re-spawn the HQ entity at the saved position so GetHQ() works after restart
		if (m_bActive && m_iPhase >= 2 && m_vHQPos != vector.Zero && !GetHQ())
		{
			m_HQ = JWK_SpawnUtils.SpawnEntityPrefab("{E33390F24DE49603}Prefabs/Compositions/BuildItems/CommandPost.et", m_vHQPos);
			if (m_HQ)
			{
				m_HQ.SetName("BM_Invader_HQ");
				BM_StripHQCharacters(m_HQ);
				Faction fac = GetGame().GetFactionManager().GetFactionByKey(m_sInvaderFaction);
				JWK_FactionControlComponent ctrl = JWK_FactionControlComponent.Cast(m_HQ.FindComponent(JWK_FactionControlComponent));
				if (ctrl) ctrl.ChangeControlToFaction(fac);
				EPF_PersistenceComponent pComp = EPF_PersistenceComponent.Cast(m_HQ.FindComponent(EPF_PersistenceComponent));
				if (pComp) { pComp.SetPersistentId("BM_INVADER_HQ"); m_sHQPersistentID = "BM_INVADER_HQ"; }
			}
		}

		// Re-apply invader faction to captured bases (EPF overwrites with role-based faction)
		if (m_aBM_PendingCapturedBaseNames && !m_aBM_PendingCapturedBaseNames.IsEmpty())
		{
			Faction invFac = GetGame().GetFactionManager().GetFactionByKey(m_sInvaderFaction);
			if (invFac) {
				foreach (string baseName : m_aBM_PendingCapturedBaseNames)
				{
					IEntity base = GetGame().GetWorld().FindEntityByName(baseName);
					if (!base) continue;
					JWK_FactionControlComponent ctrl = JWK_CompTU<JWK_FactionControlComponent>.FindIn(base);
					if (ctrl) {
						ctrl.ChangeControlToFaction(invFac);
						JWK_Log.Log(this, "Restored invader ownership of " + baseName);
					}
					// Restore ever-owned tracking for retake priority
					m_sEverOwnedBaseNames.Insert(baseName);
				}
			}
			m_aBM_PendingCapturedBaseNames = null;
		}

		if (m_bActive && m_iPhase >= 2 && m_sTargetBaseName != "")
			m_bReconstitutionNeeded = true;
	}

	void BM_ReconstituteAIFroces_S(array<ref BM_PersistentAIGroupData> groupData)
	{
		// Old infantry reconstitution disabled — fresh convoy with vehicles will be launched
		// by PerformReconstitution_S via the m_bReconstitutionNeeded flag
		JWK_Log.Log(this, "Skipping legacy infantry reconstitution — will launch fresh vehicle convoy instead.");
	}

	// --------------------------------------------------------------------------------------------------------

	protected void Heartbeat_S()
	{
		if (!m_bIsPersistenceLoaded) return;

		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (!gm || !gm.IsRunning()) return;

		if (!m_bActive) { CheckTrigger_S(); return; }

		// Fix 4: HQ liveness check — if HQ was set but is now destroyed, reset to bridgehead phase
		if (m_iPhase >= 2 && m_sHQPersistentID != string.Empty && !GetHQ()) {
			JWK_Log.Log(this, "HQ destroyed — resetting to bridgehead phase.");
			m_sHQPersistentID = string.Empty;
			m_vHQPos = vector.Zero;
			m_HQ = null;
			m_iPhase = 1;
			m_bConvoyEnRoute = false;
		}

		if (m_bReconstitutionNeeded)
		{
			m_bReconstitutionNeeded = false;
			PerformReconstitution_S();
		}

		float now = GetGame().GetWorld().GetWorldTime();

		// Fix 15: bridgehead phase with cooldown
		if (m_iPhase == 1) {
			m_iBridgeheadRetryCooldownMs = Math.Max(0, m_iBridgeheadRetryCooldownMs - 2000);
			if (m_iBridgeheadRetryCooldownMs == 0) EstablishBridgehead_S();
			return;
		}

		// Fix 7: passive ticket replenishment every 30s (15 heartbeats * 2s)
		m_iTickReplenishCounter++;
		if (m_iTickReplenishCounter >= TICK_REPLENISH_INTERVAL) {
			m_iTickReplenishCounter = 0;
			int ownedBases = CountInvaderOwnedBases_S();
			int replenish = 75 + (ownedBases * 35);
			AddTickets(replenish);
			if (m_iTickets > 5000) m_iTickets = 5000;
		}

		if (now - m_fLastSweepTime > 120000) {
			SanitySweep_S();
			m_fLastSweepTime = now;
		}

		// Siege system: check for invader presence at enemy bases
		m_iSiegeCheckCounter++;
		if (m_iSiegeCheckCounter >= SIEGE_CHECK_INTERVAL) {
			m_iSiegeCheckCounter = 0;
			CheckSieges_S();
		}

		// Ambient patrols: maintain 2 roaming groups once we have a base
		m_iPatrolCheckCounter++;
		if (m_iPatrolCheckCounter >= PATROL_CHECK_INTERVAL) {
			m_iPatrolCheckCounter = 0;
			ManagePatrols_S();
		}

		// Phase escalation and de-escalation based on owned bases
		int invaderBases = CountInvaderOwnedBases_S();

		if (m_iPhase == 2 && invaderBases >= 3) {
			m_iPhase = 3;
			JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT,
				"WARNING: Invader forces have established a frontline. Escalating assault.");
		}
		if (m_iPhase == 3 && invaderBases >= 6) {
			m_iPhase = 4;
			JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT,
				"CRITICAL: Invader forces are overwhelming the region.");
		}

		// Phase de-escalation: if players retake bases, drop phase back down
		if (m_iPhase == 4 && invaderBases < 4) {
			m_iPhase = 3;
			JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT,
				"UPDATE: Invader momentum stalled. Assault intensity reduced.");
		}
		if (m_iPhase == 3 && invaderBases < 2) {
			m_iPhase = 2;
			JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT,
				"UPDATE: Invader frontline collapsed. Regrouping.");
		}

		// Invasion defeat: tickets exhausted and no living invader units
		if (m_iTickets <= 0 && m_InvaderForce && m_InvaderForce.CountAgents() == 0) {
			EndInvasion_S();
			return;
		}

		// Player victory: invaders hold 0 bases and tickets are low
		if (invaderBases == 0 && m_iTickets < 1000 && m_iPhase >= 2) {
			EndInvasion_S();
			return;
		}

		// Phase-aware action interval
		int actionIntervalMs = 300000;
		if (m_iPhase == 3) actionIntervalMs = 180000;
		if (m_iPhase == 4) actionIntervalMs = 120000;

		if (now - m_fLastActionTime < actionIntervalMs) return;

		UpdateStrategicFrontline_S();
		m_fLastActionTime = now;
	}

	// --------------------------------------------------------------------------------------------------------

	protected void EndInvasion_S()
	{
		JWK_Log.Log(this, "Invasion defeated — tickets exhausted, no units remain.");
		JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT,
			"VICTORY: The invader offensive has been repelled! All hostile forces eliminated.");

		m_bActive = false;
		m_iPhase = 0;
		m_bConvoyEnRoute = false;
		m_sTargetBaseName = "";

		// Delete all invader force units (groups + vehicles)
		if (m_InvaderForce) {
			m_InvaderForce.ForceDeleteAllUnits();
		}

		// Re-register tracked vehicles into JWK GC so they get cleaned up naturally
		foreach (EntityID vicID : m_aInvaderVehicleIDs) {
			IEntity vic = GetGame().GetWorld().FindEntityByID(vicID);
			if (vic) JWK_EntityGarbageCollectorSystem.S_Register(vic);
		}
		m_aInvaderVehicleIDs.Clear();
		m_mAbandonedVehicleTimes.Clear();

		// Clean up all active sieges
		foreach (BM_SiegeState siege : m_aSieges) {
			if (siege.m_DefenderForce) siege.m_DefenderForce.ForceDeleteAllUnits();
		}
		m_aSieges.Clear();

		// Clean up ambient patrols
		foreach (SCR_AIGroup patrol : m_aPatrolGroups) {
			if (patrol) SCR_EntityHelper.DeleteEntityAndChildren(patrol);
		}
		m_aPatrolGroups.Clear();
		m_sEverOwnedBaseNames.Clear();

		// Clean up HQ if it somehow still exists
		if (GetHQ()) {
			SCR_EntityHelper.DeleteEntityAndChildren(m_HQ);
			m_HQ = null;
		}
		m_sHQPersistentID = string.Empty;
		m_vHQPos = vector.Zero;
	}

	// --------------------------------------------------------------------------------------------------------

	protected void PerformReconstitution_S()
	{
		if (m_sTargetBaseName == "") return;
		IEntity target = GetGame().GetWorld().FindEntityByName(m_sTargetBaseName);
		if (!target) return;
		SCR_FactionAffiliationComponent affil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(target);
		if (affil && affil.GetAffiliatedFactionKey() == GetFaction()) { m_sTargetBaseName = ""; return; }
		IEntity source = GetHQ();
		if (source) LaunchConvoy_S(source, target);

		// Prevent UpdateStrategicFrontline_S from also firing immediately
		m_fLastActionTime = GetGame().GetWorld().GetWorldTime();
	}

	protected void CheckTrigger_S()
	{
		if (m_bInstant) { StartInvasion_S(false); return; }
		int count = 0;
		array<EntityID> bases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
		foreach (EntityID id : bases) { if (JWK_FactionControlComponent.IsPlayerFaction(id)) count++; }
		if (count >= m_iMinBases) StartInvasion_S(false);
	}

	void StartInvasion_S(bool isSilentRestore = false)
	{
		if (m_bActive) return;
		m_bActive = true;
		if (m_iPhase == 0) m_iPhase = 1;
		m_fLastActionTime = 0;
		if (!isSilentRestore) {
			JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, "STRATEGIC ALERT: Hostile forces massing at the border!");
		}
	}

	// --------------------------------------------------------------------------------------------------------

	protected void EstablishBridgehead_S()
	{
		if (GetHQ() || m_sHQPersistentID != string.Empty) return;
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		JWK_RoadNetworkManagerComponent roadMgr = JWK_RoadNetworkManagerComponent.Cast(gm.FindComponent(JWK_RoadNetworkManagerComponent));
		if (!roadMgr) {
			m_iBridgeheadRetryCooldownMs = 60000;
			return;
		}

		array<GenericComponent> comps = JWK_IndexSystem.Get().GetAllGC(JWK_TerritoryControlNodeComponent);
		if (!comps || comps.IsEmpty()) {
			m_iBridgeheadRetryCooldownMs = 60000;
			return;
		}

		// Step 1: Find the "Capital" node (the one with the most neighbors within 3000m)
		// This mathematically guarantees we start our logic on the largest, densest landmass.
		IEntity capitalNode = null;
		int maxNeighbors = -1;
		
		foreach (GenericComponent comp : comps) {
			IEntity ent = JWK_TerritoryControlNodeComponent.Cast(comp).GetOwner();
			if (!ent) continue;
			int neighbors = 0;
			foreach (GenericComponent otherComp : comps) {
				IEntity otherEnt = JWK_TerritoryControlNodeComponent.Cast(otherComp).GetOwner();
				if (!otherEnt || ent == otherEnt) continue;
				if (vector.DistanceSqXZ(ent.GetOrigin(), otherEnt.GetOrigin()) < 9000000) { // 3000m radius
					neighbors++;
				}
			}
			if (neighbors > maxNeighbors) {
				maxNeighbors = neighbors;
				capitalNode = ent;
			}
		}

		if (!capitalNode) {
			m_iBridgeheadRetryCooldownMs = 15000;
			return;
		}

		vector capitalPos = capitalNode.GetOrigin();
		float oceanHeight = GetGame().GetWorld().GetOceanBaseHeight();

		// Step 2: Pick an "Edge" Node by projecting outwards from the Capital
		float angle = JWK.Random.RandFloatXY(0, Math.PI2);
		vector dir = Vector(Math.Sin(angle), 0, Math.Cos(angle));
		
		IEntity edgeNode = null;
		float maxProj = -99999999;
		
		foreach (GenericComponent comp : comps) {
			IEntity ent = JWK_TerritoryControlNodeComponent.Cast(comp).GetOwner();
			if (!ent) continue;
			
			vector nodePos = ent.GetOrigin();
			vector toNode = nodePos - capitalPos;
			float proj = (toNode[0] * dir[0]) + (toNode[2] * dir[2]);
			
			if (proj > maxProj) {
				// Step 3: Island Check. Does a straight line to the Capital cross the ocean?
				bool isConnected = true;
				vector flatNode = Vector(nodePos[0], 0, nodePos[2]);
				vector flatCap = Vector(capitalPos[0], 0, capitalPos[2]);
				float dist = vector.Distance(flatNode, flatCap);
				
				if (dist > 300) {
					vector lineDir = (flatCap - flatNode).Normalized();
					int steps = dist / 150; // Sample the terrain height every 150m
					for (int i = 1; i < steps; i++) {
						vector p = flatNode + (lineDir * (i * 150));
						float h = GetGame().GetWorld().GetSurfaceY(p[0], p[2]);
						if (h <= oceanHeight + 0.5) {
							isConnected = false; // Hit water! It's on a separated island or across a bay.
							break;
						}
					}
				}
				
				if (isConnected) {
					maxProj = proj;
					edgeNode = ent;
				}
			}
		}

		if (!edgeNode) {
			m_iBridgeheadRetryCooldownMs = 5000;
			return;
		}

		// Step 4: Find the closest road to this connected edge node
		JWK_Road road; int rIdx;
		if (roadMgr.GetClosestRoad(edgeNode.GetOrigin(), road, rIdx, 3000))
		{
			vector pos = road.points[rIdx];
			pos[1] = GetGame().GetWorld().GetSurfaceY(pos[0], pos[2]);
			
			if (pos[1] <= oceanHeight + 1.0) {
				m_iBridgeheadRetryCooldownMs = 5000; // Fast retry
				return;
			}

			// Step 5: Offset from the road safely
			vector roadDir = Vector(1,0,0);
			if (rIdx > 0) roadDir = (road.points[rIdx] - road.points[rIdx-1]).Normalized();
			vector perpDir = Vector(-roadDir[2], 0, roadDir[0]);
			
			bool foundSpot = false;
			vector bestPos = pos;
			array<float> offsets = {15, -15, 25, -25};
			
			foreach (float off : offsets) {
				vector testPos = pos + (perpDir * off);
				testPos[1] = GetGame().GetWorld().GetSurfaceY(testPos[0], testPos[2]);
				if (testPos[1] > oceanHeight + 1.5) { // Safe dry land
					bestPos = testPos;
					foundSpot = true;
					break;
				}
			}

			if (!foundSpot) {
				m_iBridgeheadRetryCooldownMs = 5000;
				return;
			}

			m_HQ = JWK_SpawnUtils.SpawnEntityPrefab("{4A1E3B17DB672E35}Prefabs/Structures/Military/Camps/TentFIA_01/TentFIA_01.et", bestPos);
			if (m_HQ) {
				m_HQ.SetName("BM_Invader_HQ");
				m_vHQPos = bestPos;
				BM_StripHQCharacters(m_HQ);
				Faction fac = GetGame().GetFactionManager().GetFactionByKey(m_sInvaderFaction);
				JWK_FactionControlComponent control = JWK_FactionControlComponent.Cast(m_HQ.FindComponent(JWK_FactionControlComponent));
				if (control) control.ChangeControlToFaction(fac);
				JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT, "URGENT: Enemy forward operating base established in the region!");
				m_iPhase = 2;
				m_fLastActionTime = GetGame().GetWorld().GetWorldTime() - 280000;
				EPF_PersistenceComponent pComp = EPF_PersistenceComponent.Cast(m_HQ.FindComponent(EPF_PersistenceComponent));
				if (pComp) { pComp.SetPersistentId("BM_INVADER_HQ"); m_sHQPersistentID = "BM_INVADER_HQ"; }
			}
		} else {
			m_iBridgeheadRetryCooldownMs = 5000;
		}
	}

	// --------------------------------------------------------------------------------------------------------

	protected void SanitySweep_S()
	{
		if (!m_InvaderForce) return;

		// Build list of positions where invader units should be kept alive
		ref array<vector> keepAlivePositions = {};

		// Invader-owned bases
		array<EntityID> allBases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
		foreach (EntityID bId : allBases) {
			IEntity base = GetGame().GetWorld().FindEntityByID(bId);
			if (!base) continue;
			SCR_FactionAffiliationComponent baseAffil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(base);
			if (baseAffil && baseAffil.GetAffiliatedFactionKey() == GetFaction())
				keepAlivePositions.Insert(base.GetOrigin());
		}

		// Current target base (where convoy troops are fighting)
		if (m_sTargetBaseName != "") {
			IEntity targetBase = GetGame().GetWorld().FindEntityByName(m_sTargetBaseName);
			if (targetBase) keepAlivePositions.Insert(targetBase.GetOrigin());
		}

		// Actively sieged bases
		foreach (BM_SiegeState siege : m_aSieges) {
			IEntity siegeBase = GetGame().GetWorld().FindEntityByID(siege.m_BaseID);
			if (siegeBase) keepAlivePositions.Insert(siegeBase.GetOrigin());
		}

		// HQ position
		if (m_vHQPos != vector.Zero)
			keepAlivePositions.Insert(m_vHQPos);

		array<EntityID> groupIDs = m_InvaderForce.GetGroups();
		for (int i = groupIDs.Count() - 1; i >= 0; i--)
		{
			SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupIDs[i]));
			if (!group) continue;
			if (group.GetAgentsCount() == 0) {
				m_InvaderForce.DetachGroup(group);
				SCR_EntityHelper.DeleteEntityAndChildren(group); continue;
			}

			// Only clean up groups with no waypoints AND far from all important positions
			array<AIWaypoint> waypoints = {};
			group.GetWaypoints(waypoints);
			if (!waypoints.IsEmpty()) continue;

			bool nearImportant = false;
			vector groupPos = group.GetOrigin();
			foreach (vector pos : keepAlivePositions) {
				if (vector.DistanceSqXZ(groupPos, pos) < 90000) { // 300m radius
					nearImportant = true; break;
				}
			}

			if (!nearImportant) {
				m_InvaderForce.DetachGroup(group);
				SCR_EntityHelper.DeleteEntityAndChildren(group);
				AddTickets(200);
			}
		}

		// Fix 6: convoy flag stays locked until target captured OR all invader troops are dead
		// No timeout — prevents piling 10 convoys at the same spot
		if (m_bConvoyEnRoute) {
			bool clearConvoy = false;
			string clearReason = "";

			// Target base captured by invaders → mission success, send next wave
			if (m_sTargetBaseName != "") {
				IEntity targetEnt = GetGame().GetWorld().FindEntityByName(m_sTargetBaseName);
				if (targetEnt) {
					SCR_FactionAffiliationComponent tgtAffil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(targetEnt);
					if (tgtAffil && tgtAffil.GetAffiliatedFactionKey() == GetFaction()) {
						clearConvoy = true;
						clearReason = "target captured";
					}
				} else {
					clearConvoy = true;
					clearReason = "target entity lost";
				}
			}

			// All invader agents dead → convoy wiped, allow sending reinforcements
			if (!clearConvoy && m_InvaderForce && m_InvaderForce.CountAgents() == 0) {
				clearConvoy = true;
				clearReason = "convoy wiped out";
			}

			if (clearConvoy) {
				m_bConvoyEnRoute = false;
				JWK_Log.Log(this, "Convoy flag cleared: " + clearReason);
			}
		}

		// Handle tracked vehicles: remove destroyed, release abandoned ones to JWK GC after grace period
		float now2 = GetGame().GetWorld().GetWorldTime();
		for (int v = m_aInvaderVehicleIDs.Count() - 1; v >= 0; v--)
		{
			EntityID vicID = m_aInvaderVehicleIDs[v];
			IEntity vic = GetGame().GetWorld().FindEntityByID(vicID);
			if (!vic) {
				m_aInvaderVehicleIDs.Remove(v);
				m_mAbandonedVehicleTimes.Remove(vicID);
				continue;
			}

			// Check if vehicle is empty
			SCR_BaseCompartmentManagerComponent compMgr = SCR_BaseCompartmentManagerComponent.Cast(vic.FindComponent(SCR_BaseCompartmentManagerComponent));
			bool isEmpty = true;
			if (compMgr) {
				array<IEntity> occupants = {};
				compMgr.GetOccupants(occupants);
				isEmpty = occupants.IsEmpty();
			}

			if (!isEmpty) {
				// Vehicle has crew/passengers — clear abandon timer
				m_mAbandonedVehicleTimes.Remove(vicID);
				continue;
			}

			// Vehicle is empty — start or check abandon timer
			if (!m_mAbandonedVehicleTimes.Contains(vicID)) {
				m_mAbandonedVehicleTimes.Set(vicID, now2);
				continue;
			}

			float abandonedAt = m_mAbandonedVehicleTimes.Get(vicID);

			// Use shorter grace if vehicle is near the current target (prevents clutter pileup)
			float graceMs = VEHICLE_ABANDON_GRACE_MS;
			if (m_sTargetBaseName != "") {
				IEntity targetEnt = GetGame().GetWorld().FindEntityByName(m_sTargetBaseName);
				if (targetEnt && vector.DistanceSqXZ(vic.GetOrigin(), targetEnt.GetOrigin()) < 40000) // 200m
					graceMs = VEHICLE_ABANDON_NEAR_TARGET_MS;
			}

			if (now2 - abandonedAt < graceMs) continue;

			// Don't delete if a player is nearby — they might want to use it
			if (BM_IsPlayerNearby(vic.GetOrigin(), 50)) {
				m_mAbandonedVehicleTimes.Set(vicID, now2); // reset timer, check again later
				continue;
			}

			// Grace period expired — delete vehicle outright to keep battlefield clean
			JWK_Log.Log(this, "Cleaning up abandoned vehicle: " + JWK_PrefabUtils.GetShortEntityPrefabName(vic));
			SCR_EntityHelper.DeleteEntityAndChildren(vic);
			m_aInvaderVehicleIDs.Remove(v);
			m_mAbandonedVehicleTimes.Remove(vicID);
		}
	}

	// ========================================================================================================
	// SIEGE SYSTEM — AI-vs-AI base capture
	// ========================================================================================================

	protected void CheckSieges_S()
	{
		string invFacKey = GetFaction();
		float now = GetGame().GetWorld().GetWorldTime();

		// Gather all capturable targets
		array<EntityID> allTargets = {};
		array<EntityID> milBases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
		foreach (EntityID mb : milBases) allTargets.Insert(mb);
		array<EntityID> factoryIDs = JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity);
		foreach (EntityID fct : factoryIDs) allTargets.Insert(fct);

		// Detect new sieges
		foreach (EntityID baseID : allTargets) {
			IEntity base = GetGame().GetWorld().FindEntityByID(baseID);
			if (!base) continue;

			SCR_FactionAffiliationComponent affil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(base);
			if (!affil) continue;

			// Skip invader-owned bases
			if (affil.GetAffiliatedFactionKey() == invFacKey) continue;

			// Skip if siege already active at this base
			if (FindSiegeForBase(baseID)) continue;

			// Check if invader groups are near this base
			if (CountInvaderAgentsNear(base.GetOrigin(), SIEGE_DETECT_RANGE) > 0) {
				StartSiege_S(base);

				// Clear convoy flag if this is the current target
				if (m_sTargetBaseName == base.GetName())
					m_bConvoyEnRoute = false;
			}
		}

		// Update active sieges
		for (int s = m_aSieges.Count() - 1; s >= 0; s--)
		{
			BM_SiegeState siege = m_aSieges[s];
			IEntity base = GetGame().GetWorld().FindEntityByID(siege.m_BaseID);

			if (!base) {
				if (siege.m_DefenderForce) siege.m_DefenderForce.ForceDeleteAllUnits();
				m_aSieges.Remove(s);
				continue;
			}

			// Check if base was already captured (e.g. by battle system or other means)
			SCR_FactionAffiliationComponent baseAffil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(base);
			if (baseAffil && baseAffil.GetAffiliatedFactionKey() == invFacKey) {
				if (siege.m_DefenderForce) siege.m_DefenderForce.ForceDeleteAllUnits();
				m_aSieges.Remove(s);
				continue;
			}

			// Grace period: let them fight before checking outcome
			if (now - siege.m_fStartTime < SIEGE_GRACE_PERIOD_MS) continue;

			int invadersNearby = CountInvaderAgentsNear(base.GetOrigin(), SIEGE_DETECT_RANGE);
			int defendersAlive = 0;
			if (siege.m_DefenderForce) defendersAlive = siege.m_DefenderForce.CountAgents();

			if (invadersNearby > 0 && defendersAlive == 0) {
				// Invaders won — capture the base, clean up defender force
				if (siege.m_DefenderForce) siege.m_DefenderForce.ForceDeleteAllUnits();
				CaptureBaseForInvaders_S(base);
				m_aSieges.Remove(s);

			} else if (invadersNearby == 0) {
				// Invaders defeated or retreated — defense succeeded
				if (siege.m_DefenderForce) siege.m_DefenderForce.ForceDeleteAllUnits();
				m_aSieges.Remove(s);

				JWK_NamedLocationComponent namedLoc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(base);
				string locName = "a location";
				if (namedLoc) locName = namedLoc.GetName();
				JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT,
					"UPDATE: Invader assault on " + locName + " repelled.");
			}
		}
	}

	protected void StartSiege_S(IEntity base)
	{
		// Get the base's current faction for defender spawning
		SCR_FactionAffiliationComponent affil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(base);
		if (!affil) return;
		string baseFactionKey = affil.GetAffiliatedFactionKey();

		Faction baseFaction = GetGame().GetFactionManager().GetFactionByKey(baseFactionKey);
		SCR_Faction scrFaction = SCR_Faction.Cast(baseFaction);
		if (!scrFaction) return;

		SCR_EntityCatalog catalog = scrFaction.GetFactionEntityCatalogOfType(EEntityCatalogType.GROUP);
		if (!catalog) return;

		array<SCR_EntityCatalogEntry> entries = {};
		catalog.GetEntityList(entries);
		if (entries.IsEmpty()) return;

		// Filter infantry-only groups
		array<ResourceName> infantryGroups = {};
		foreach (SCR_EntityCatalogEntry entry : entries) {
			string path = entry.GetPrefab();
			path.ToLower();
			if (!path.Contains("vehicle") && !path.Contains("armor") && !path.Contains("crew") &&
				!path.Contains("tank") && !path.Contains("brdm") && !path.Contains("btr") &&
				!path.Contains("bmp") && !path.Contains("ifv") && !path.Contains("apc"))
				infantryGroups.Insert(entry.GetPrefab());
		}
		if (infantryGroups.IsEmpty()) {
			foreach (SCR_EntityCatalogEntry entry : entries) infantryGroups.Insert(entry.GetPrefab());
		}

		// Create siege state
		BM_SiegeState siege = new BM_SiegeState();
		siege.m_BaseID = base.GetID();
		siege.m_fStartTime = GetGame().GetWorld().GetWorldTime();
		siege.m_DefenderForce = new JWK_AIForce();
		siege.m_DefenderForce.m_Log.m_Prefix = "BM_Siege_Def";

		vector baseOrigin = base.GetOrigin();

		// Spawn 3 defender groups around the base
		for (int i = 0; i < 3; i++) {
			float angle = i * (2.0 * Math.PI / 3.0) + JWK.Random.RandFloatXY(-0.3, 0.3);
			float radius = 10 + JWK.Random.RandFloatXY(0, 8);
			vector spawnPos = baseOrigin + Vector(Math.Sin(angle) * radius, 0, Math.Cos(angle) * radius);
			spawnPos[1] = GetGame().GetWorld().GetSurfaceY(spawnPos[0], spawnPos[2]);

			SCR_AIGroup group = SCR_AIGroup.Cast(
				JWK_SpawnUtils.SpawnEntityPrefab(infantryGroups.GetRandomElement(), spawnPos)
			);
			if (group) {
				group.AddWaypoint(
					JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.SEARCH_AND_DESTROY, baseOrigin)
				);
				siege.m_DefenderForce.AttachGroup(group);
			}
		}

		m_aSieges.Insert(siege);

		// Notify players
		JWK_NamedLocationComponent namedLoc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(base);
		string locName = "a location";
		if (namedLoc) locName = namedLoc.GetName();
		JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT,
			"ALERT: Invader forces assaulting " + locName + "! Garrison deployed.");
		JWK_Log.Log(this, "Siege started at " + locName + " — 3 defender groups spawned (" + baseFactionKey + ").");
	}

	protected void CaptureBaseForInvaders_S(IEntity base)
	{
		Faction invFac = GetGame().GetFactionManager().GetFactionByKey(GetFaction());
		if (!invFac) return;

		JWK_FactionControlComponent ctrl = JWK_CompTU<JWK_FactionControlComponent>.FindIn(base);
		if (!ctrl) return;

		ctrl.ChangeControlToFaction(invFac);

		// Track this base as ever-owned so we prioritize retaking it if lost
		string baseName = base.GetName();
		if (!baseName.IsEmpty()) m_sEverOwnedBaseNames.Insert(baseName);

		JWK_NamedLocationComponent namedLoc = JWK_CompTU<JWK_NamedLocationComponent>.FindIn(base);
		string locName = "a location";
		if (namedLoc) locName = namedLoc.GetName();
		JWK.GetNotifications().BroadcastNotification_S(ENotification.JWK_FREE_TEXT,
			"WARNING: " + locName + " has fallen to invader forces!");
		JWK_Log.Log(this, locName + " captured by invaders.");

		// Clear convoy target if it was this base
		if (m_sTargetBaseName == base.GetName()) {
			m_bConvoyEnRoute = false;
			m_sTargetBaseName = "";
		}
	}

	protected int CountInvaderAgentsNear(vector pos, float range)
	{
		if (!m_InvaderForce) return 0;
		float rangeSq = range * range;
		int count = 0;
		array<EntityID> groupIDs = m_InvaderForce.GetGroups();
		foreach (EntityID groupID : groupIDs) {
			SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupID));
			if (!group || group.GetAgentsCount() == 0) continue;
			if (vector.DistanceSqXZ(group.GetOrigin(), pos) < rangeSq)
				count += group.GetAgentsCount();
		}
		return count;
	}

	protected BM_SiegeState FindSiegeForBase(EntityID baseID)
	{
		foreach (BM_SiegeState siege : m_aSieges) {
			if (siege.m_BaseID == baseID) return siege;
		}
		return null;
	}

	// ========================================================================================================
	// AMBIENT PATROLS — roaming infantry for war ambiance (free, separate from tickets)
	// ========================================================================================================

	protected void ManagePatrols_S()
	{
		if (CountInvaderOwnedBases_S() < 1) return;

		// Prune dead/empty patrols
		for (int i = m_aPatrolGroups.Count() - 1; i >= 0; i--)
		{
			SCR_AIGroup grp = m_aPatrolGroups[i];
			if (!grp || grp.GetAgentsCount() == 0) {
				if (grp) SCR_EntityHelper.DeleteEntityAndChildren(grp);
				m_aPatrolGroups.Remove(i);
			}
		}

		// Spawn new patrols up to max
		while (m_aPatrolGroups.Count() < PATROL_MAX)
		{
			SCR_AIGroup patrol = SpawnPatrol_S();
			if (!patrol) break;
			m_aPatrolGroups.Insert(patrol);
		}
	}

	protected SCR_AIGroup SpawnPatrol_S()
	{
		// Pick a random invader-owned base as spawn origin
		array<IEntity> ownedBases = {};
		array<EntityID> allBases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
		foreach (EntityID bId : allBases) {
			IEntity base = GetGame().GetWorld().FindEntityByID(bId);
			if (!base) continue;
			SCR_FactionAffiliationComponent affil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(base);
			if (affil && affil.GetAffiliatedFactionKey() == GetFaction())
				ownedBases.Insert(base);
		}
		if (ownedBases.IsEmpty()) return null;

		IEntity spawnBase = ownedBases.GetRandomElement();
		vector origin = spawnBase.GetOrigin();

		// Get infantry group prefab from faction catalog
		Faction faction = GetGame().GetFactionManager().GetFactionByKey(GetFaction());
		SCR_Faction scrFaction = SCR_Faction.Cast(faction);
		if (!scrFaction) return null;
		SCR_EntityCatalog catalog = scrFaction.GetFactionEntityCatalogOfType(EEntityCatalogType.GROUP);
		if (!catalog) return null;

		array<SCR_EntityCatalogEntry> entries = {};
		catalog.GetEntityList(entries);
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
		if (infantryGroups.IsEmpty()) return null;

		// Spawn at base with slight offset
		float angle = JWK.Random.RandFloatXY(0, Math.PI2);
		vector spawnPos = origin + Vector(Math.Sin(angle) * 15, 0, Math.Cos(angle) * 15);
		spawnPos[1] = GetGame().GetWorld().GetSurfaceY(spawnPos[0], spawnPos[2]);

		SCR_AIGroup group = SCR_AIGroup.Cast(JWK_SpawnUtils.SpawnEntityPrefab(infantryGroups.GetRandomElement(), spawnPos));
		if (!group) return null;

		// Protect from all GC systems
		BM_ProtectGroupFromGC(group);

		// Give 3-4 random PATROL waypoints across the map for roaming
		int wpCount = 3 + JWK.Random.RandInt(0, 2);
		for (int w = 0; w < wpCount; w++)
		{
			// Pick a random base (any faction) as patrol destination for natural movement
			IEntity destBase = null;
			if (allBases.Count() > 0) {
				EntityID destID = allBases.GetRandomElement();
				destBase = GetGame().GetWorld().FindEntityByID(destID);
			}

			vector wpPos;
			if (destBase) {
				wpPos = destBase.GetOrigin();
				// Offset so they patrol around the base, not exactly to it
				wpPos = wpPos + Vector(JWK.Random.RandFloatXY(-150, 150), 0, JWK.Random.RandFloatXY(-150, 150));
			} else {
				wpPos = origin + Vector(JWK.Random.RandFloatXY(-500, 500), 0, JWK.Random.RandFloatXY(-500, 500));
			}
			wpPos[1] = GetGame().GetWorld().GetSurfaceY(wpPos[0], wpPos[2]);

			group.AddWaypoint(JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.PATROL, wpPos));
		}

		// Add a final CYCLE waypoint back to spawn so they loop
		group.AddWaypoint(JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.CYCLE, origin));

		JWK_Log.Log(this, "Spawned ambient patrol from " + spawnBase.GetName() + " with " + group.GetAgentsCount() + " agents.");
		return group;
	}

	// ========================================================================================================
	// CONVOY & STRATEGIC FRONTLINE
	// ========================================================================================================

	protected void UpdateStrategicFrontline_S()
	{
		if (m_bConvoyEnRoute) return;

		string facKey = GetFaction();
		array<EntityID> allTargets = {};
		array<EntityID> milBases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
		foreach (EntityID mb : milBases) allTargets.Insert(mb);
		array<EntityID> factories = JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity);
		foreach (EntityID fct : factories) allTargets.Insert(fct);
		array<IEntity> ownedBases = {};
		array<IEntity> enemyBases = {};
		foreach (EntityID id : allTargets) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			if (!ent) continue;
			SCR_FactionAffiliationComponent affil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			if (affil && affil.GetAffiliatedFactionKey() == facKey) ownedBases.Insert(ent);
			else enemyBases.Insert(ent);
		}
		if (enemyBases.IsEmpty()) return;
		IEntity source = GetHQ();
		if (!ownedBases.IsEmpty()) source = ownedBases.GetRandomElement();
		if (!source) return;

		// Prioritize retaking lost bases (bases we previously owned but no longer control)
		array<IEntity> lostBases = {};
		foreach (IEntity tgt : enemyBases) {
			string tgtName = tgt.GetName();
			if (!tgtName.IsEmpty() && m_sEverOwnedBaseNames.Contains(tgtName))
				lostBases.Insert(tgt);
		}

		// Pick target: lost bases first (closest), then any enemy base (closest)
		IEntity target = null;
		float minDist = 9999999;
		array<IEntity> priorityList = lostBases;
		if (priorityList.IsEmpty()) priorityList = enemyBases;

		foreach (IEntity tgt : priorityList) {
			float d = vector.DistanceSqXZ(source.GetOrigin(), tgt.GetOrigin());
			if (d < minDist && d > 2500) { minDist = d; target = tgt; }
		}
		if (target) {
			m_sTargetBaseName = target.GetName();

			int artilleryThreshold = 4;
			if (m_iPhase == 3) artilleryThreshold = 3;
			if (m_iPhase == 4) artilleryThreshold = 2;
			if (ownedBases.Count() >= artilleryThreshold)
				LaunchArtilleryStrike_S(target.GetOrigin());

			int groupsReused = 0;
			if (m_InvaderForce) {
				array<EntityID> groupIDs = m_InvaderForce.GetGroups();
				foreach (EntityID groupID : groupIDs) {
					SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(groupID));
					if (group && group.GetAgentsCount() > 0) {
						// Clear old waypoints
						array<AIWaypoint> wps = {};
						group.GetWaypoints(wps);
						foreach (AIWaypoint wp : wps) {
							group.RemoveWaypoint(wp);
						}
						// Add new waypoints
						group.AddWaypoint(JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.MOVE, target.GetOrigin()));
						group.AddWaypoint(JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.SEARCH_AND_DESTROY, target.GetOrigin()));
						groupsReused++;
					}
				}
				if (groupsReused > 0) {
					JWK_Log.Log(this, "Reusing " + groupsReused + " existing groups for assault on " + m_sTargetBaseName);
				}
			}

			int convoyCount = 1;
			if (m_iPhase == 3) convoyCount = 2;
			if (m_iPhase == 4) convoyCount = 3;

			// Scale down new spawns based on reused units (assume ~2 groups per convoy)
			int neededConvoys = convoyCount - (groupsReused / 2);
			if (neededConvoys < 0) neededConvoys = 0;
			
			// Always spawn at least 1 convoy if we have zero units on the field
			if (groupsReused == 0 && neededConvoys == 0) neededConvoys = 1;

			for (int c = 0; c < neededConvoys; c++)
				LaunchConvoy_S(source, target);

			m_bConvoyEnRoute = true;
		}
	}

	// --------------------------------------------------------------------------------------------------------

	protected void LaunchConvoy_S(IEntity source, IEntity target)
	{
		if (GetTickets() < 600) return;
		DrainTickets(600);

		int vehiclesSpawned = 0;

		// Use FF's proper composition system to generate and spawn crewed vehicles
		JWK_FactionForceCompositionGenerator compGen = JWK_CombatFactionTrait.GetForceCompositionByKey(GetFaction());
		if (compGen)
		{
			// Scale manpower and vehicle count with phase
			int minManpower = 6;
			int maxManpower = 12;
			int minVehicles = 2;
			int maxVehicles = 2;
			if (m_iPhase == 3) { minManpower = 9; maxManpower = 18; minVehicles = 2; maxVehicles = 3; }
			if (m_iPhase == 4) { minManpower = 12; maxManpower = 24; minVehicles = 3; maxVehicles = 4; }

			JWK_FactionForceCompositionGenerationRequest req = new JWK_FactionForceCompositionGenerationRequest();
			req.manpowerAbsMin = minManpower;
			req.manpowerMax = maxManpower;
			req.useVehicles = true;
			req.useInfantry = false;
			req.mechanizedManpowerShare = 1.0;
			req.minVehicles = minVehicles;
			req.maxVehicles = maxVehicles;
			req.allowLightArmor = true;
			req.allowHeavyArmor = (m_iPhase >= 3);
			req.threat = 0.7;

			JWK_FactionForceVehiclesComposition vehComp = compGen.GenerateVehicles(req);

			if (vehComp && vehComp.GetVehiclesCount() > 0)
			{
				JWK_Log.Log(this, "Composition generated: " + vehComp.GetVehiclesCount() + " vehicles, " + vehComp.GetManpower() + " manpower for " + GetFaction());

				// Find a road near the source for vehicle spawning
				JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
				JWK_RoadNetworkManagerComponent roadMgr;
				if (gm) roadMgr = JWK_RoadNetworkManagerComponent.Cast(gm.FindComponent(JWK_RoadNetworkManagerComponent));

				for (int i = 0; i < vehComp.GetVehiclesCount(); i++)
				{
					JWK_CrewedVehicleComposition compo = vehComp.GetVehicle(i);
					if (!compo) continue;

					// Find spawn position on road, spaced apart
					float angle = i * (2.0 * Math.PI / vehComp.GetVehiclesCount());
					float dist = 80 + (i * 30);
					vector spawnPos = source.GetOrigin() + Vector(Math.Sin(angle) * dist, 0, Math.Cos(angle) * dist);
					float yaw = 0;

					if (roadMgr) {
						JWK_Road road; int rIdx;
						if (roadMgr.GetClosestRoad(spawnPos, road, rIdx, 350)) {
							int offsetIdx = rIdx + (i * 5);
							if (offsetIdx >= road.points.Count()) offsetIdx = rIdx;
							spawnPos = road.points[offsetIdx];

							// Calculate yaw from road direction
							if (offsetIdx > 0)
							{
								vector roadDir = road.points[offsetIdx] - road.points[offsetIdx - 1];
								yaw = roadDir.VectorToAngles()[0];
							}
						}
					}
					spawnPos[1] = GetGame().GetWorld().GetSurfaceY(spawnPos[0], spawnPos[2]) + 0.5;

					// Use FF's proper vehicle spawning (handles CrewedVehicleComponent + AIGroupComponent setup)
					JWK_CrewedVehicle cv = JWK_VehicleUtils.SpawnCrewedVehicle_S(compo, spawnPos, yaw);
					if (!cv || !cv.m_Vehicle) {
						JWK_Log.Log(this, "Failed to spawn crewed vehicle index " + i, LogLevel.WARNING);
						continue;
					}

					JWK_Log.Log(this, "Spawned crewed vehicle: " + compo.m_rVehiclePrefab);

					// Protect from GC and vehicle streaming (prevents despawn when far from players)
					BM_ProtectEntityFromGC(cv.m_Vehicle);
					BM_DisableVehicleStreaming(cv.m_Vehicle);
					if (cv.m_Crew) BM_ProtectGroupFromGC(cv.m_Crew);
					foreach (SCR_AIGroup dismount : cv.m_aDismounts)
						if (dismount) BM_ProtectGroupFromGC(dismount);

					m_aInvaderVehicleIDs.Insert(cv.m_Vehicle.GetID());

					// Attach to invader force (fires OnCrewedVehicleAttached callbacks)
					GetInvaderForce().AttachCrewedVehicle(cv);

					// Give waypoints to the crew group
					if (cv.m_Crew) {
						cv.m_Crew.AddWaypoint(JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.MOVE, target.GetOrigin()));
						cv.m_Crew.AddWaypoint(JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.SEARCH_AND_DESTROY, target.GetOrigin()));
					}

					vehiclesSpawned++;
				}
			}
		} else {
			JWK_Log.Log(this, "No JWK_CombatFactionTrait found for " + GetFaction() + " — cannot generate vehicle composition.", LogLevel.WARNING);
		}

		// Fallback: if no vehicles spawned, send infantry instead
		if (vehiclesSpawned == 0) {
			JWK_Log.Log(this, "No vehicles spawned — falling back to infantry convoy.");
			LaunchInfantryOnly_S(source, target);
		}
	}

	protected void LaunchInfantryOnly_S(IEntity source, IEntity target)
	{
		Faction faction = GetGame().GetFactionManager().GetFactionByKey(GetFaction());
		SCR_Faction scrFaction = SCR_Faction.Cast(faction);
		if (!scrFaction) return;
		SCR_EntityCatalog catalog = scrFaction.GetFactionEntityCatalogOfType(EEntityCatalogType.GROUP);
		if (!catalog) return;
		array<SCR_EntityCatalogEntry> entries = {}; catalog.GetEntityList(entries);

		// Fix 12: filter out vehicle/armor groups from infantry selection
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
			foreach (SCR_EntityCatalogEntry entry : entries)
				infantryGroups.Insert(entry.GetPrefab());
		}

		for (int i = 0; i < 4; i++) {
			float angle = i * (Math.PI / 2) + JWK.Random.RandFloatXY(-0.5, 0.5);
			float radius = 10 + JWK.Random.RandFloatXY(0, 8);
			vector spawnPos = source.GetOrigin() + Vector(Math.Sin(angle) * radius, 0, Math.Cos(angle) * radius);
			spawnPos[1] = GetGame().GetWorld().GetSurfaceY(spawnPos[0], spawnPos[2]);

			SCR_AIGroup group = SCR_AIGroup.Cast(JWK_SpawnUtils.SpawnEntityPrefab(infantryGroups.GetRandomElement(), spawnPos));
			if (group) {
				group.AddWaypoint(JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.MOVE, target.GetOrigin()));
				group.AddWaypoint(JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.SEARCH_AND_DESTROY, target.GetOrigin()));
				GetInvaderForce().AttachGroup(group);
			}
		}
	}

	// --------------------------------------------------------------------------------------------------------

	protected void LaunchArtilleryStrike_S(vector center)
	{
		GetGame().GetCallqueue().CallLater(SimulateImpact_S, 5000, false, center, 12);
	}

	protected void SimulateImpact_S(vector center, int remaining)
	{
		if (remaining <= 0) return;
		vector pos = center + Vector(JWK.Random.RandFloatXY(-60, 60), 0, JWK.Random.RandFloatXY(-60, 60));
		pos[1] = 500;
		JWK_SpawnUtils.SpawnEntityPrefab("{E232B40CB053641C}Prefabs/Weapons/Warheads/Warhead_Artillery_152mm_HE.et", pos);
		GetGame().GetCallqueue().CallLater(SimulateImpact_S, JWK.Random.RandInt(1500, 4000), false, center, remaining - 1);
	}

	// --------------------------------------------------------------------------------------------------------

	// --------------------------------------------------------------------------------------------------------
	// GC PROTECTION & VEHICLE FILTERING (from old working code)
	// --------------------------------------------------------------------------------------------------------

	protected void BM_ProtectEntityFromGC(IEntity ent)
	{
		if (!ent) return;
		SCR_GarbageSystem gcManager = SCR_GarbageSystem.Cast(GetGame().GetWorld().FindSystem(SCR_GarbageSystem));
		if (gcManager) gcManager.Withdraw(ent);
	}

	// Strip any NPC characters baked into the CommandPost composition — keep the tent only
	protected void BM_StripHQCharacters(IEntity hq)
	{
		if (!hq) return;
		array<IEntity> toDelete = {};
		IEntity child = hq.GetChildren();
		while (child)
		{
			IEntity next = child.GetSibling();
			if (child.FindComponent(SCR_CharacterControllerComponent))
				toDelete.Insert(child);
			child = next;
		}
		foreach (IEntity ent : toDelete)
		{
			JWK_Log.Log(this, "Removing NPC from HQ composition: " + JWK_PrefabUtils.GetShortEntityPrefabName(ent));
			SCR_EntityHelper.DeleteEntityAndChildren(ent);
		}
	}

	protected bool BM_IsPlayerNearby(vector pos, float range)
	{
		float rangeSq = range * range;
		array<int> playerIds = {};
		GetGame().GetPlayerManager().GetPlayers(playerIds);
		foreach (int pid : playerIds) {
			IEntity pEnt = GetGame().GetPlayerManager().GetPlayerControlledEntity(pid);
			if (pEnt && vector.DistanceSqXZ(pEnt.GetOrigin(), pos) < rangeSq)
				return true;
		}
		return false;
	}

	protected void BM_DisableVehicleStreaming(IEntity vehicle)
	{
		if (!vehicle) return;
		JWK_StreamableVehicleComponent svc = JWK_StreamableVehicleComponent.Cast(vehicle.FindComponent(JWK_StreamableVehicleComponent));
		if (svc) svc.SetStreamingEnabled_S(false);
		JWK_EntityGarbageCollectorSystem.S_Unregister(vehicle);
	}

	protected void BM_ProtectGroupFromGC(SCR_AIGroup group)
	{
		if (!group) return;
		BM_ProtectEntityFromGC(group);
		JWK_EntityGarbageCollectorSystem.S_Unregister(group);
		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents) {
			IEntity charEnt = agent.GetControlledEntity();
			if (charEnt) BM_ProtectEntityFromGC(charEnt);
		}
	}

	protected bool BM_IsVehicleValid(ResourceName res)
	{
		string resLower = res;
		resLower.ToLower();

		// Hard exclusions first — logistics, support, air, trucks
		if (resLower.Contains("fuel") || resLower.Contains("supply") || resLower.Contains("ammo"))
			return false;
		if (resLower.Contains("repair") || resLower.Contains("tanker") || resLower.Contains("logistics"))
			return false;
		if (resLower.Contains("medic") || resLower.Contains("ambulance") || resLower.Contains("workshop"))
			return false;
		if (resLower.Contains("command") || resLower.Contains("construction") || resLower.Contains("crane"))
			return false;
		if (resLower.Contains("helicopter") || resLower.Contains("heli") || resLower.Contains("plane"))
			return false;
		if (resLower.Contains("_base") || resLower.Contains("core") || resLower.Contains("van"))
			return false;
		if (resLower.Contains("m923") || resLower.Contains("m35") || resLower.Contains("ural"))
			return false;
		if (resLower.Contains("zil") || resLower.Contains("gaz66") || resLower.Contains("truck"))
			return false;

		// Whitelist: known combat vehicle keywords
		if (resLower.Contains("btr") || resLower.Contains("brdm") || resLower.Contains("bmp"))
			return true;
		if (resLower.Contains("t72") || resLower.Contains("t80") || resLower.Contains("t55"))
			return true;
		if (resLower.Contains("m113") || resLower.Contains("m60") || resLower.Contains("m1a"))
			return true;
		if (resLower.Contains("lav") || resLower.Contains("stryker") || resLower.Contains("bradley"))
			return true;
		if (resLower.Contains("ifv") || resLower.Contains("apc") || resLower.Contains("tank"))
			return true;
		if (resLower.Contains("armor") || resLower.Contains("hmg") || resLower.Contains("armed"))
			return true;
		if (resLower.Contains("gun") || resLower.Contains("patrol"))
			return true;

		return false;
	}

	// --------------------------------------------------------------------------------------------------------


	// --------------------------------------------------------------------------------------------------------

	JWK_AIForce GetInvaderForce()
	{
		if (!m_InvaderForce) {
			m_InvaderForce = new JWK_AIForce();
			m_InvaderForce.m_bAutoUnstuck = true;
			m_InvaderForce.m_Log.m_Prefix = "BM_Invader";
			m_InvaderForce.SetRegisteredInManager(true);
		}
		return m_InvaderForce;
	}

	int CountInvaderOwnedBases_S()
	{
		string facKey = GetFaction();
		int count = 0;

		array<EntityID> milBases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
		foreach (EntityID id : milBases) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			if (!ent) continue;
			SCR_FactionAffiliationComponent affil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			if (affil && affil.GetAffiliatedFactionKey() == facKey) count++;
		}

		array<EntityID> factoryIDs = JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity);
		foreach (EntityID id : factoryIDs) {
			IEntity ent = GetGame().GetWorld().FindEntityByID(id);
			if (!ent) continue;
			SCR_FactionAffiliationComponent affil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(ent);
			if (affil && affil.GetAffiliatedFactionKey() == facKey) count++;
		}

		return count;
	}

	// --------------------------------------------------------------------------------------------------------

	void Setup(string f, int t, int b, bool i) { m_sInvaderFaction = f; s_InvaderFactionKey = f; m_iTickets = t; m_iMinBases = b; m_bInstant = i; }
	string GetFaction() { return m_sInvaderFaction; }
	int GetTickets() { return m_iTickets; }
	void AddTickets(int a) { m_iTickets += a; }
	void DrainTickets(int a) { m_iTickets -= a; if (m_iTickets < 0) m_iTickets = 0; }
	IEntity GetHQ() {
		if (!m_HQ) {
			m_HQ = GetGame().GetWorld().FindEntityByName("BM_Invader_HQ");
		}
		return m_HQ;
	}
}

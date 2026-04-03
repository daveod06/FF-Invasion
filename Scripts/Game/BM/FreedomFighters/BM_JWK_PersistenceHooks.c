// ==========================================
// BM_JWK_PersistenceHooks.c
// THE STRATEGIC MEMORY (MODDED CLASS)
// ==========================================
// Session 4: added m_aBM_CapturedBaseNames to persist invader base ownership.
// EPF only saves factionRole (PLAYER/ENEMY), which maps back to occupier on load.
// We re-apply invader faction to captured bases after EPF restores.

modded class JWK_GameModeSaveData
{
	string m_sBM_InvaderFaction;
	int m_iBM_Tickets;
	int m_iBM_MinBases;
	bool m_bBM_Instant;
	bool m_bBM_Active;
	int m_iBM_Phase;
	vector m_vBM_HQPos;
	string m_sBM_TargetBaseName;
	ref array<ref BM_PersistentAIGroupData> m_aBM_PersistedGroups;
	ref array<string> m_aBM_CapturedBaseNames;

	override bool SerializationSave(BaseSerializationSaveContext saveContext)
	{
		BM_InvasionManager mgr = BM_InvasionManager.GetInstance();
		if (mgr) {
			m_sBM_InvaderFaction = mgr.GetFaction();
			m_iBM_Tickets = mgr.GetTickets();
			m_iBM_MinBases = mgr.m_iMinBases;
			m_bBM_Instant = mgr.m_bInstant;
			m_bBM_Active = mgr.m_bActive;
			m_iBM_Phase = mgr.m_iPhase;
			m_vBM_HQPos = mgr.m_vHQPos;
			m_sBM_TargetBaseName = mgr.m_sTargetBaseName;

			// Persist invader AI groups
			m_aBM_PersistedGroups = {};
			JWK_AIForce force = mgr.GetInvaderForce();
			if (force)
			{
				array<EntityID> groups = force.GetGroups();
				foreach (EntityID id : groups)
				{
					SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().GetWorld().FindEntityByID(id));
					if (group)
					{
						ResourceName prefab = group.GetPrefabData().GetPrefabName();
						BM_PersistentAIGroupData entry = new BM_PersistentAIGroupData();
						entry.Init(prefab, group.GetOrigin());
						m_aBM_PersistedGroups.Insert(entry);
					}
				}
			}

			// Persist names of bases captured by invaders
			m_aBM_CapturedBaseNames = {};
			string invFacKey = mgr.GetFaction();

			array<EntityID> milBases = JWK_IndexSystem.Get().GetAll(JWK_MilitaryBaseEntity);
			foreach (EntityID bId : milBases) {
				IEntity base = GetGame().GetWorld().FindEntityByID(bId);
				if (!base) continue;
				SCR_FactionAffiliationComponent affil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(base);
				if (affil && affil.GetAffiliatedFactionKey() == invFacKey && !base.GetName().IsEmpty())
					m_aBM_CapturedBaseNames.Insert(base.GetName());
			}

			array<EntityID> factoryIDs = JWK_IndexSystem.Get().GetAll(JWK_FactoryEntity);
			foreach (EntityID fId : factoryIDs) {
				IEntity fac = GetGame().GetWorld().FindEntityByID(fId);
				if (!fac) continue;
				SCR_FactionAffiliationComponent affil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(fac);
				if (affil && affil.GetAffiliatedFactionKey() == invFacKey && !fac.GetName().IsEmpty())
					m_aBM_CapturedBaseNames.Insert(fac.GetName());
			}
		}

		if (!super.SerializationSave(saveContext)) return false;

		saveContext.WriteValue("m_sBM_InvaderFaction", m_sBM_InvaderFaction);
		saveContext.WriteValue("m_iBM_Tickets", m_iBM_Tickets);
		saveContext.WriteValue("m_iBM_MinBases", m_iBM_MinBases);
		saveContext.WriteValue("m_bBM_Instant", m_bBM_Instant);
		saveContext.WriteValue("m_bBM_Active", m_bBM_Active);
		saveContext.WriteValue("m_iBM_Phase", m_iBM_Phase);
		saveContext.WriteValue("m_vBM_HQPos", m_vBM_HQPos);
		saveContext.WriteValue("m_sBM_TargetBaseName", m_sBM_TargetBaseName);
		saveContext.WriteValue("m_aBM_PersistedGroups", m_aBM_PersistedGroups);
		saveContext.WriteValue("m_aBM_CapturedBaseNames", m_aBM_CapturedBaseNames);
		return true;
	}

	override bool SerializationLoad(BaseSerializationLoadContext loadContext)
	{
		if (!super.SerializationLoad(loadContext)) return false;
		loadContext.ReadValue("m_sBM_InvaderFaction", m_sBM_InvaderFaction);
		loadContext.ReadValue("m_iBM_Tickets", m_iBM_Tickets);
		loadContext.ReadValue("m_iBM_MinBases", m_iBM_MinBases);
		loadContext.ReadValue("m_bBM_Instant", m_bBM_Instant);
		loadContext.ReadValue("m_bBM_Active", m_bBM_Active);
		loadContext.ReadValue("m_iBM_Phase", m_iBM_Phase);
		loadContext.ReadValue("m_vBM_HQPos", m_vBM_HQPos);
		loadContext.ReadValue("m_sBM_TargetBaseName", m_sBM_TargetBaseName);
		loadContext.ReadValue("m_aBM_PersistedGroups", m_aBM_PersistedGroups);
		loadContext.ReadValue("m_aBM_CapturedBaseNames", m_aBM_CapturedBaseNames);
		return true;
	}
}

modded class JWK_GameMode
{
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		if (Replication.IsServer()) GetGame().GetCallqueue().CallLater(BM_Invasion_SpawnBrain_S, 100, false);
	}

	protected void BM_Invasion_SpawnBrain_S()
	{
		IEntity brain = GetGame().GetWorld().FindEntityByName("BM_Invasion_Brain");
		if (!brain)
		{
			EntitySpawnParams p = new EntitySpawnParams();
			p.Transform[3] = "0 0 0";
			IEntity spawned = GetGame().SpawnEntity(BM_InvasionManager, GetGame().GetWorld(), p);
			if (spawned) {
				BM_InvasionManager brainMgr = BM_InvasionManager.Cast(spawned);
				if (brainMgr) brainMgr.InitBrain();
			}
		}
	}

	override void LoadState_S(JWK_GameModeSaveData saveData)
	{
		super.LoadState_S(saveData);
		GetGame().GetCallqueue().CallLater(BM_Invasion_RestoreState_S, 1000, false, saveData);
	}

	protected void BM_Invasion_RestoreState_S(JWK_GameModeSaveData saveData)
	{
		BM_InvasionManager mgr = BM_InvasionManager.GetInstance();
		if (mgr) {
			mgr.m_sInvaderFaction = saveData.m_sBM_InvaderFaction;
			BM_InvasionManager.s_InvaderFactionKey = saveData.m_sBM_InvaderFaction;
			mgr.m_iTickets = saveData.m_iBM_Tickets;
			mgr.m_iMinBases = saveData.m_iBM_MinBases;
			mgr.m_bInstant = saveData.m_bBM_Instant;
			mgr.m_bActive = saveData.m_bBM_Active;
			mgr.m_iPhase = saveData.m_iBM_Phase;
			mgr.m_vHQPos = saveData.m_vBM_HQPos;
			mgr.m_sTargetBaseName = saveData.m_sBM_TargetBaseName;
			mgr.BM_ReconstituteAIFroces_S(saveData.m_aBM_PersistedGroups);

			EPF_PersistenceManager pMgr = EPF_PersistenceManager.GetInstance();
			if (pMgr) {
				// If EPF is already ACTIVE, call directly — the event won't fire again
				if (pMgr.GetState() == EPF_EPersistenceManagerState.ACTIVE) {
					mgr.BM_OnPersistenceRestored();
				} else {
					ScriptInvoker onActive = pMgr.GetOnActiveEvent();
					if (onActive) onActive.Insert(mgr.BM_OnPersistenceRestored);
				}
			} else {
				// No persistence manager — just enable the brain directly
				mgr.BM_OnPersistenceRestored();
			}

			// Store captured base names on manager so they can be re-applied after EPF finishes
			if (saveData.m_aBM_CapturedBaseNames && !saveData.m_aBM_CapturedBaseNames.IsEmpty())
				mgr.m_aBM_PendingCapturedBaseNames = saveData.m_aBM_CapturedBaseNames;
		}
	}
}

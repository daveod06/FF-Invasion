// BakerMods override:
//   - SpawnNpc_S: after spawning, overrides the NPC's faction affiliation to match the owner entity's
//     faction affiliation component (e.g. HQ commander inherits the HQ faction).

modded class JWK_BaseCharacterPlacementControllerComponent
{
	// BM: cached faction affiliation of the owner entity; used to override spawned NPC faction
	protected SCR_FactionAffiliationComponent m_OwnerFactionAffiliation;

	// --------------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		m_OwnerFactionAffiliation = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(owner);
	}

	// --------------------------------------------------------------------------------------------------------
	override IEntity SpawnNpc_S(ResourceName prefab, ResourceName groupPrefab = ResourceName.Empty)
	{
		IEntity npcEntity = super.SpawnNpc_S(prefab, groupPrefab);

		// BM: override NPC faction ONLY for invader-owned entities (e.g. HQ commander)
		// Normal occupier/player checkpoints use default AMBIENT role faction — don't touch them
		if (m_OwnerFactionAffiliation && npcEntity)
		{
			Faction ownerFaction = m_OwnerFactionAffiliation.GetAffiliatedFaction();
			if (ownerFaction && ownerFaction.GetFactionKey() == BM_InvasionManager.s_InvaderFactionKey)
			{
				SCR_FactionAffiliationComponent npcAffil = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(npcEntity);
				if (npcAffil)
					npcAffil.SetAffiliatedFaction(ownerFaction);
			}
		}

		return npcEntity;
	}
}

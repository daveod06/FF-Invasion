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
// BakerMods override:
//   - SpawnNpc_S: after spawning, overrides the NPC's faction affiliation to match the owner entity's
//     faction affiliation component (e.g. HQ commander inherits the HQ faction).

[ComponentEditorProps(category: "JWK/Framework", description: "")]
class JWK_BaseCharacterPlacementControllerComponentClass: JWK_EntityComponentClass
{
}

class JWK_BaseCharacterPlacementControllerComponent: JWK_EntityComponent
{
	// (JWK_BaseCharacterPlacementControllerComponent this, IEntity npc)
	protected ref ScriptInvoker m_OnNpcSpawned_S;
	protected JWK_ShopBackendComponent m_ShopBackend;

	// BM: cached faction affiliation of the owner entity; used to override spawned NPC faction
	protected SCR_FactionAffiliationComponent m_OwnerFactionAffiliation;

	// --------------------------------------------------------------------------------------------------------

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		m_ShopBackend = JWK_CompTU<JWK_ShopBackendComponent>.FindIn(owner);
		m_OwnerFactionAffiliation = JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(owner);
	}

	// --------------------------------------------------------------------------------------------------------

	void Initialize_S()
	{

	}

	ScriptInvoker GetOnNpcSpawned_S()
	{
		if (!m_OnNpcSpawned_S) m_OnNpcSpawned_S = new ScriptInvoker();
		return m_OnNpcSpawned_S;
	}

	vector FindRandomSpawnPosition_S()
	{
		return FindSpawnPosition();
	}

	IEntity SpawnNpc_S(
		ResourceName prefab,
		ResourceName groupPrefab = ResourceName.Empty
	) {
		vector spawnPosition = FindSpawnPosition();
		if (spawnPosition == vector.Zero) {
			JWK_Log.Log(this, "Failed to find spawn position!", LogLevel.ERROR);
			return null;
		}

		IEntity npcEntity = JWK_SpawnUtils.SpawnCharacter(
			spawnPosition, prefab, groupPrefab, JWK_EFactionRole.AMBIENT
		);

		JWK_BaseShopComponent shop = JWK_CompTU<JWK_BaseShopComponent>.FindIn(npcEntity);
		if (shop && m_ShopBackend)
			JWK.GetShopManager().LinkShopToBackend_S(shop, m_ShopBackend);

		// BM: override NPC faction ONLY for invader-owned entities (e.g. HQ commander)
		// Normal occupier/player checkpoints use default AMBIENT role faction — don't touch them
		if (m_OwnerFactionAffiliation && npcEntity) {
			Faction ownerFaction = m_OwnerFactionAffiliation.GetAffiliatedFaction();
			if (ownerFaction && ownerFaction.GetFactionKey() == BM_InvasionManager.s_InvaderFactionKey) {
				SCR_FactionAffiliationComponent npcAffil =
					JWK_CompTU<SCR_FactionAffiliationComponent>.FindIn(npcEntity);
				if (npcAffil) npcAffil.SetAffiliatedFaction(ownerFaction);
			}
		}

		JWK_Log.Log(this, "Spawned " + prefab + " at " + spawnPosition + ".");

		if (m_OnNpcSpawned_S) m_OnNpcSpawned_S.Invoke(this, npcEntity);

		return npcEntity;
	}

	protected vector FindSpawnPosition()
	{
		return JWK_SpawnUtils.FindSafeCharacterSpawnPosition(GetOwner().GetOrigin());
	}

	// --------------------------------------------------------------------------------------------------------
}

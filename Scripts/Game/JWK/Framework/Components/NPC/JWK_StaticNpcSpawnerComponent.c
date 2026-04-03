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

[ComponentEditorProps(category: "JWK/Framework", description: "")]
class JWK_StaticNpcSpawnerComponentClass: JWK_EntityComponentClass
{
}

class JWK_StaticNpcSpawnerComponent: JWK_EntityComponent
{
	[Attribute(desc: "SCR_AIGroup prefab in which spawned NPCs will be put.")]
	ResourceName m_rGroupPrefab;
	
	[Attribute(desc: "Character prefabs of random NPCs to spawn.")]
	ref array<ResourceName> m_aNpcPrefabs;
	
	[Attribute("0")]
	bool m_bAutoSpawn;
	
	// --------------------------------------------------------------------------------------------------------
	
	protected JWK_BaseCharacterPlacementControllerComponent m_Placement;
	
	// --------------------------------------------------------------------------------------------------------
	
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode()) return;
		
		m_Placement = JWK_CompTU<JWK_BaseCharacterPlacementControllerComponent>.FindIn(owner);
		if (!m_Placement) JWK_Log.Log(this, "SpawnControllerComponent not found!", LogLevel.ERROR);
		
		if (Replication.IsServer()) {
			if (m_bAutoSpawn) SetEventMask(owner, EntityEvent.INIT);
		}
	}
	
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		
		if (Replication.IsServer()) {
			if (m_bAutoSpawn) GetFramework().GetCallQueue().CallLater(SpawnAll_S);
		}
	}
	
	void SpawnAll_S()
	{		
		foreach (ResourceName prefab : m_aNpcPrefabs) {
			m_Placement.SpawnNpc_S(prefab, m_rGroupPrefab);
		}
	}
}
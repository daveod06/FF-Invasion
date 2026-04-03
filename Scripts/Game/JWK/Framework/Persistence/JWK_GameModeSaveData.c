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

[BaseContainerProps()]
class JWK_GameModeSaveDataClass : EPF_EntitySaveDataClass
{
}

[EDF_DbName.Automatic()]
class JWK_GameModeSaveData : EPF_EntitySaveData
{
	int m_iGameplayTime;
	int m_iRootSeed;
	string m_sSaveRevision;

	override EPF_EReadResult ReadFrom(IEntity entity, EPF_EntitySaveDataClass attributes)
	{
		EPF_EReadResult readResult = super.ReadFrom(entity, attributes);

		JWK_GameMode gameMode = JWK_GameMode.Cast(entity);
		gameMode.SaveState_S(this);

		return readResult;
	}

	override EPF_EApplyResult ApplyTo(IEntity entity, EPF_EntitySaveDataClass attributes)
	{
		EPF_EApplyResult applyResult = super.ApplyTo(entity, attributes);
		
		JWK_GameMode gameMode = JWK_GameMode.Cast(entity);
		gameMode.LoadState_S(this);
		
		return applyResult;
	}

	override protected bool SerializationSave(BaseSerializationSaveContext saveContext)
	{
		if (!super.SerializationSave(saveContext))
			return false;

		saveContext.WriteValue("m_iGameplayTime", m_iGameplayTime);
		saveContext.WriteValue("m_iRootSeed", m_iRootSeed);
		saveContext.WriteValue("m_sSaveRevision", m_sSaveRevision);

		return true;
	}

	override protected bool SerializationLoad(BaseSerializationLoadContext loadContext)
	{
		if (!super.SerializationLoad(loadContext))
			return false;

		loadContext.ReadValue("m_iGameplayTime", m_iGameplayTime);
		loadContext.ReadValue("m_iRootSeed", m_iRootSeed);
		loadContext.ReadValue("m_sSaveRevision", m_sSaveRevision);
		
		return true;
	}
}

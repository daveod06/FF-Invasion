[ComponentEditorProps(category: "BakerMods/Persistence", description: "Holds BakerMods specific game state for persistence.")]
class BM_GameModeStateComponentClass : ScriptComponentClass
{
}

class BM_GameModeStateComponent : ScriptComponent
{
	// Logic that was previously in EPF_OnLoad and EPF_OnSave is now handled by the SaveData class below.
	// The component itself can remain, in case it needs to hold runtime state or be accessed by other systems.
}

[EPF_ComponentSaveDataType(BM_GameModeStateComponent), BaseContainerProps()]
class BM_GameModeStateComponentSaveDataClass : EPF_ComponentSaveDataClass
{
}

[EDF_DbName()]
class BM_GameModeStateComponentSaveData : EPF_ComponentSaveData
{
	int m_iBM_PlayerZoneCount;

	override bool IsFor(IEntity owner, GenericComponent component, EPF_ComponentSaveDataClass attributes)
	{
		return component.IsInherited(BM_GameModeStateComponent);
	}

	override EPF_EReadResult ReadFrom(IEntity owner, GenericComponent component, EPF_ComponentSaveDataClass attributes)
	{
		BM_InvasionDirectorComponent director = BM_InvasionDirectorComponent.GetInstance();
		if (director)
		{
			m_iBM_PlayerZoneCount = director.GetPlayerZoneCount();
		}
		
		return EPF_EReadResult.OK;
	}
	
	override EPF_EApplyResult ApplyTo(IEntity owner, GenericComponent component, EPF_ComponentSaveDataClass attributes)
	{
		BM_InvasionDirectorComponent director = BM_InvasionDirectorComponent.GetInstance();
		if (director)
		{
			director.SetPlayerZoneCount(m_iBM_PlayerZoneCount);
			Print("BM_Invasion: Loaded " + m_iBM_PlayerZoneCount + " player-owned zones from save data component.", LogLevel.NORMAL);
		}
		
		return EPF_EApplyResult.OK;
	}
}

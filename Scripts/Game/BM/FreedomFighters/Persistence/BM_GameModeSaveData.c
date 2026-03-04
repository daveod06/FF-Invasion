modded class JWK_GameModeSaveData
{
	bool m_bBM_InvasionTriggered;
	bool m_bBM_InvaderHadBase;
	bool m_bBM_InvasionDefeated;
	string m_sBM_InvaderFactionKey;
	float m_fBM_LastExpansionTime;
	int m_iBM_CurrentTargetWaveCount;
	EntityID m_iBM_LastFailedTargetID = EntityID.INVALID;
	ref array<string> m_aInvaderGroupPrefabs = {};
	ref array<vector> m_aInvaderGroupPositions = {};

	override protected bool SerializationSave(BaseSerializationSaveContext saveContext)
	{
		if (!super.SerializationSave(saveContext))
			return false;

		saveContext.WriteValue("m_bBM_InvasionTriggered", m_bBM_InvasionTriggered);
		saveContext.WriteValue("m_bBM_InvaderHadBase", m_bBM_InvaderHadBase);
		saveContext.WriteValue("m_bBM_InvasionDefeated", m_bBM_InvasionDefeated);
		saveContext.WriteValue("m_sBM_InvaderFactionKey", m_sBM_InvaderFactionKey);
		saveContext.WriteValue("m_fBM_LastExpansionTime", m_fBM_LastExpansionTime);
		saveContext.WriteValue("m_iBM_CurrentTargetWaveCount", m_iBM_CurrentTargetWaveCount);
		saveContext.WriteValue("m_iBM_LastFailedTargetID", m_iBM_LastFailedTargetID);
		
		// Manual Array Serialization for Unit GPS and Prefabs
		int countPrefabs = m_aInvaderGroupPrefabs.Count();
		if (saveContext.StartArray("m_aInvaderGroupPrefabs", countPrefabs))
		{
			foreach (string prefab : m_aInvaderGroupPrefabs)
			{
				saveContext.WriteValue("", prefab);
			}
			saveContext.EndArray();
		}
		
		int countPositions = m_aInvaderGroupPositions.Count();
		if (saveContext.StartArray("m_aInvaderGroupPositions", countPositions))
		{
			foreach (vector pos : m_aInvaderGroupPositions)
			{
				saveContext.WriteValue("", pos);
			}
			saveContext.EndArray();
		}

		return true;
	}

	override protected bool SerializationLoad(BaseSerializationLoadContext loadContext)
	{
		if (!super.SerializationLoad(loadContext))
			return false;

		loadContext.ReadValue("m_bBM_InvasionTriggered", m_bBM_InvasionTriggered);
		loadContext.ReadValue("m_bBM_InvaderHadBase", m_bBM_InvaderHadBase);
		loadContext.ReadValue("m_bBM_InvasionDefeated", m_bBM_InvasionDefeated);
		loadContext.ReadValue("m_sBM_InvaderFactionKey", m_sBM_InvaderFactionKey);
		loadContext.ReadValue("m_fBM_LastExpansionTime", m_fBM_LastExpansionTime);
		loadContext.ReadValue("m_iBM_CurrentTargetWaveCount", m_iBM_CurrentTargetWaveCount);
		loadContext.ReadValue("m_iBM_LastFailedTargetID", m_iBM_LastFailedTargetID);
		
		if (!m_aInvaderGroupPrefabs)
			m_aInvaderGroupPrefabs = new array<string>();
		else
			m_aInvaderGroupPrefabs.Clear();
		
		int countPrefabs = 0;
		if (loadContext.StartArray("m_aInvaderGroupPrefabs", countPrefabs))
		{
			for (int i = 0; i < countPrefabs; i++)
			{
				string val;
				loadContext.ReadValue("", val);
				m_aInvaderGroupPrefabs.Insert(val);
			}
			loadContext.EndArray();
		}

		if (!m_aInvaderGroupPositions)
			m_aInvaderGroupPositions = new array<vector>();
		else
			m_aInvaderGroupPositions.Clear();
		
		int countPositions = 0;
		if (loadContext.StartArray("m_aInvaderGroupPositions", countPositions))
		{
			for (int i = 0; i < countPositions; i++)
			{
				vector val;
				loadContext.ReadValue("", val);
				m_aInvaderGroupPositions.Insert(val);
			}
			loadContext.EndArray();
		}
		
		return true;
	}
}

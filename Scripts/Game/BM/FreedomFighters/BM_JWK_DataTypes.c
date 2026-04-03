// ==========================================
// BM_JWK_DataTypes.c
// SHARED DATA STRUCTURES FOR INVASION
// ==========================================

[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_pPrefab")]
class BM_PersistentAIGroupData
{
	ResourceName m_pPrefab;
	vector m_vPosition;

	void BM_PersistentAIGroupData()
	{
	}

	void Init(ResourceName prefab, vector pos)
	{
		m_pPrefab = prefab;
		m_vPosition = pos;
	}

	bool Read(BaseSerializationLoadContext ctx)
	{
		ctx.ReadValue("m_pPrefab", m_pPrefab);
		ctx.ReadValue("m_vPosition", m_vPosition);
		return true;
	}

	bool Write(BaseSerializationSaveContext ctx)
	{
		ctx.WriteValue("m_pPrefab", m_pPrefab);
		ctx.WriteValue("m_vPosition", m_vPosition);
		return true;
	}
}

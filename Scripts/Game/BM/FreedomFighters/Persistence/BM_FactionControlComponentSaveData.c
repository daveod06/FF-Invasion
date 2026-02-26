modded class JWK_FactionControlComponentSaveData
{
	string m_sBM_FactionKey;

	override EPF_EReadResult ReadFrom(IEntity owner, GenericComponent component, EPF_ComponentSaveDataClass attributes)
	{
		EPF_EReadResult res = super.ReadFrom(owner, component, attributes);
		
		JWK_FactionControlComponent comp = JWK_FactionControlComponent.Cast(component);
		if (comp) {
			m_sBM_FactionKey = comp.GetFactionKey();
		}
		
		return res;
	}

	override EPF_EApplyResult ApplyTo(IEntity owner, GenericComponent component, EPF_ComponentSaveDataClass attributes)
	{
		// If we have a custom BakerMods faction key saved, use it!
		if (!m_sBM_FactionKey.IsEmpty()) {
			JWK_FactionControlComponent comp = JWK_FactionControlComponent.Cast(component);
			if (comp) {
				Faction targetFaction = GetGame().GetFactionManager().GetFactionByKey(m_sBM_FactionKey);
				if (targetFaction) {
					comp.ChangeControlToFaction(targetFaction);
					return EPF_EApplyResult.OK;
				}
			}
		}

		return super.ApplyTo(owner, component, attributes);
	}
}

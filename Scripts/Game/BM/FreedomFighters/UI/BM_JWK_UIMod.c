// ==========================================
// BM_JWK_UIMod.c
// THE LOBBY INTEGRATION
// ==========================================

modded class JWK_NewGameSetupUIComponent
{
	protected SCR_SpinBoxComponent m_InvaderFaction;
	protected SCR_SpinBoxComponent m_InvaderTickets;
	protected SCR_SpinBoxComponent m_InvasionMinBases;
	protected SCR_CheckboxComponent m_InvasionInstant;

	override void HandlerAttached(Widget w)
	{
		super.HandlerAttached(w);
		
		m_InvaderFaction = JWK_WidgetHandlerTU<SCR_SpinBoxComponent>.FindAny(w, "InvaderFaction");
		m_InvaderTickets = JWK_WidgetHandlerTU<SCR_SpinBoxComponent>.FindAny(w, "InvaderTickets");
		m_InvasionMinBases = JWK_WidgetHandlerTU<SCR_SpinBoxComponent>.FindAny(w, "InvasionMinBases");
		m_InvasionInstant = JWK_WidgetHandlerTU<SCR_CheckboxComponent>.FindAny(w, "InvasionInstant");
		
		if (m_InvaderFaction) GetGame().GetCallqueue().CallLater(BM_InitUI_S, 1000);
	}

	protected void BM_InitUI_S()
	{
		JWK_FactionManager factionMgr = JWK.GetFactions();
		if (!factionMgr) return;
		
		array<string> keys = {};
		factionMgr.GetRoleCompatibleFactionKeys(JWK_EFactionRole.ENEMY, keys);
		FillSpinBoxFactions(m_InvaderFaction, keys, "USSR");
		
		if (m_InvaderTickets) {
			m_InvaderTickets.AddItem("Low (2000)", false, JWK_StringWrapper.Create("2000"));
			m_InvaderTickets.AddItem("Standard (5000)", false, JWK_StringWrapper.Create("5000"));
			m_InvaderTickets.AddItem("Elite (10000)", false, JWK_StringWrapper.Create("10000"));
			m_InvaderTickets.SetCurrentItem(1);
		}
		
		if (m_InvasionMinBases) {
			for (int i = 0; i <= 6; i++) {
				m_InvasionMinBases.AddItem(i.ToString() + " Bases", false, JWK_StringWrapper.Create(i.ToString()));
			}
			m_InvasionMinBases.SetCurrentItem(0);
		}
	}

	override void ApplySettings()
	{
		super.ApplySettings();
		
		JWK_AdminPlayerControllerComponent ctrl = JWK_GetPlayerController<JWK_AdminPlayerControllerComponent>.Local();
		if (ctrl) {
			string fac = GetSpinBoxFactionKey(m_InvaderFaction);
			int tix = BM_GetUIValue(m_InvaderTickets, 5000);
			int min = BM_GetUIValue(m_InvasionMinBases, 0);
			bool ins = false; 
			if (m_InvasionInstant) ins = m_InvasionInstant.IsChecked();
			
			ctrl.BM_ApplyInvasionSettings(fac, tix, min, ins);
		}
	}

	protected int BM_GetUIValue(SCR_SpinBoxComponent sb, int df)
	{
		if (!sb) return df;
		JWK_StringWrapper sw = JWK_StringWrapper.Cast(sb.GetCurrentItemData());
		if (sw) return sw.value.ToInt();
		return df;
	}
}

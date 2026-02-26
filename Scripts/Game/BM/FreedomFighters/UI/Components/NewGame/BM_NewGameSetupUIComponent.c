modded class JWK_NewGameSetupUIComponent {
	protected SCR_SpinBoxComponent m_InvaderFaction;

	override void HandlerAttached(Widget w)
	{
		super.HandlerAttached(w);
		m_InvaderFaction = JWK_WidgetHandlerTU<SCR_SpinBoxComponent>.FindAny(m_wRoot, "InvaderFaction");
	}

	override void Init()
	{
		super.Init();
		
		JWK_FactionManager factionMgr = JWK.GetFactions();
		if (!factionMgr || !m_InvaderFaction) return;

		array<string> factionKeys = {};
		factionMgr.GetRoleCompatibleFactionKeys(JWK_EFactionRole.ENEMY, factionKeys);
		
		m_InvaderFaction.AddItem("None", false, JWK_StringWrapper.Create("None"));
		
		string currentInvader = "None";
		JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
		if (gm) currentInvader = gm.BM_GetInvaderFactionKey();

		FillSpinBoxFactions(m_InvaderFaction, factionKeys, currentInvader);
	}

	override void ApplySettings()
	{
		super.ApplySettings();

		if (m_InvaderFaction) {
			string invaderFaction = GetSpinBoxFactionKey(m_InvaderFaction);
			
			JWK_AdminPlayerControllerComponent ctrl = JWK_GetPlayerController<JWK_AdminPlayerControllerComponent>.Local();
			if (ctrl) {
				ctrl.BM_ApplyInvasionConfig(invaderFaction);
			}
		}
	}
}

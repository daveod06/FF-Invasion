// ==========================================
// BM_JWK_FactionManagerMod.c
// FORCE-LOAD FACTIONS NOT ASSIGNED TO AN FF ROLE
// ==========================================
//
// Bug:
//   BM_InvasionManager.LaunchConvoy_S calls
//     JWK_FactionForceCompositionGenerator compGen =
//         JWK_CombatFactionTrait.GetForceCompositionByKey(GetFaction());
//   This static lookup only finds traits whose owning JWK_Faction has had
//   trait.InitializePostSetup called (that call constructs
//   JWK_FactionForceCompositionGenerator and stores it on the trait).
//
//   Stock JWK_FactionManager.SetupFactions runs the JWK_Faction creation +
//   InitializePostSetup pipeline ONLY for factions picked by
//   GetFactionKeyByRole for each role — i.e., the active PLAYER, ENEMY,
//   SUPPORTING, AMBIENT factions. Any faction that has a registered
//   JWK_FactionConfig (e.g., a third-party faction mod adding to
//   JWK_FactionsConfig.m_aFactions via JWK_AddonIntegrationConfig) but is
//   not assigned to any role gets no JWK_Faction and no post-setup pass.
//
//   FF Conflict UI exposes only EnemyFaction; SupportingFaction is auto-
//   derived from the scenario. BM_Invasion's InvaderFaction widget
//   (BM_JWK_UIMod) writes only to BM_InvasionManager.m_sInvaderFaction
//   via BM_ApplyInvasionSettings → manager.Setup — it never updates FF
//   role assignment.
//
//   Effect: invader = US works by accident (US is the default
//   SupportingFaction in FF Conflict, so it gets a role and is post-set).
//   Any non-coincidence invader (UK from FF-BritishForces compat, MEI,
//   future faction mods) gets:
//     SCRIPT (W): [JWK][BM_InvasionManager][BM_Invasion_Brain]
//       No JWK_CombatFactionTrait found for <key> — cannot generate
//       vehicle composition.
//   and LaunchConvoy_S falls back to LaunchInfantryOnly_S.
//
// Fix:
//   Modded JWK_FactionManager.SetupFactions. After super completes the
//   standard role-based loading, walk m_aConfigs and force-load any
//   config whose faction key has not yet produced a JWK_Faction. Mirrors
//   the protected LoadFaction implementation exactly:
//     - JWK_Faction.CreateFromConfig(key, config)
//     - m_aFactions.Insert(faction)
//     - InitializeFaction(faction)         // voices + faction.Initialize
//     - faction.InitializePostSetup()       // per-trait post-setup;
//                                           // this is what builds the
//                                           // composition generator that
//                                           // GetForceCompositionByKey reads
//
//   Server-only, matching stock LoadFaction. Skips configs whose SCR_Faction
//   isn't registered (stale entries from removed addons) without raising.
//
//   Generalized — no per-faction hardcoding. Works for any faction that
//   registers a JWK_FactionConfig via FF's normal mechanisms.

modded class JWK_FactionManager
{
	override void SetupFactions()
	{
		super.SetupFactions();

		if (!Replication.IsServer()) return;

		foreach (JWK_FactionConfig config : m_aConfigs) {
			if (!config) continue;
			if (config.m_sFactionKey.IsEmpty()) continue;
			if (GetJWKFaction(config.m_sFactionKey)) continue;

			BM_ForceLoadFactionFromConfig(config);
		}
	}

	protected void BM_ForceLoadFactionFromConfig(JWK_FactionConfig config)
	{
		// Skip configs without a matching SCR_Faction — expected for stale
		// entries from removed addons; otherwise faction.Initialize would crash
		// when traits dereference the faction's vanilla SCR_Faction wrapper.
		if (!SCR_Faction.Cast(GetFactionByKey(config.m_sFactionKey))) return;

		JWK_Faction faction = JWK_Faction.CreateFromConfig(config.m_sFactionKey, config);
		if (!faction) {
			JWK_Log.Log(this, "CreateFromConfig failed for " + config.m_sFactionKey, LogLevel.WARNING);
			return;
		}

		m_aFactions.Insert(faction);
		InitializeFaction(faction);     // voices + faction.Initialize → trait.Initialize
		faction.InitializePostSetup();   // builds CombatTrait.m_ForceCompositionGenerator

		JWK_Log.Log(this, "Force-loaded faction " + config.m_sFactionKey + " (invader compatibility — was not assigned to any FF role).");
	}
}

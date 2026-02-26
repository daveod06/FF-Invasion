modded class JWK_WaveBattleAIGenerator
{
	override protected int GenerateWave(int budget, float threat)
	{
		// no point generating anything if we cant afford single character
		if (budget < CharacterBudgetCost) return 0;
		
		const int manpower = budget / CharacterBudgetCost;
		const JWK_BattleAIWave wave = new JWK_BattleAIWave();
		
		const JWK_FactionForceCompositionGenerationRequest request = new JWK_FactionForceCompositionGenerationRequest();
		request.intent = JWK_EFactionForceCompositionIntent.BATTLE;
		request.manpowerAbsMin = manpower;
		request.manpowerMax = manpower;
		request.threat = threat;
		request.maxHeavyArmedVehicles = 2;
		request.maxLightArmedVehicles = 3;
		request.maxVehicles = 5;
		request.ignoreCustomMultiplier = true;
		
		request.useVehicles = (
			!m_PlacementContext.m_CurrentBag.GetTargetRoadPoints().IsEmpty() && m_Subject.m_bEnemyAllowVehicles
		);
		
		JWK_FactionForceCompositionGenerator generator = null;
		
		// BM_GC_FIX: Dynamically determine the Faction to spawn based on the Base's current owner
		if (m_Subject && m_Subject.GetOwner()) {
			JWK_FactionControlComponent facControl = JWK_CompTU<JWK_FactionControlComponent>.FindIn(m_Subject.GetOwner());
			if (facControl && facControl.GetFactionKey()) {
				generator = JWK_CombatFactionTrait.GetForceCompositionByKey(facControl.GetFactionKey());
			}
		}
		
		if (!generator) {
			generator = JWK_CombatFactionTrait.GetForceCompositionByRole(JWK_EFactionRole.ENEMY);
		}
		
		wave.m_ForceComposition = generator.Generate(request);
		m_aWaves.Insert(wave);
		
		// Clamp because the actual manpower may be inflated by difficulty settings, which we dont want to count in.
		return Math.Min(budget, wave.m_ForceComposition.GetManpower()) * CharacterBudgetCost;
	}
}

modded class JWK_AIGarrisonComponent
{
	override void Initialize_S()
	{
		if (!m_FactionControl) return;
		
		// BM_GC_FIX: Skip player faction, but allow ANY non-player faction (including Invaders) to initialize Garrison
		if (m_FactionControl.GetFaction() == JWK.GetFactions().GetPlayerFaction()) return;
		
		if (m_iBaseForceSize <= 0) {
			JWK_Log.Log(this, "Initialization skipped, force size is not defined!", LogLevel.WARNING);
			return;
		}
		
		JWK_StreamableAIForce aiForce = m_AIForce.GetForce_S();
		if (aiForce.GetStreamableGroups().IsEmpty()) {
			InitForce_S(aiForce, m_iBaseForceSize);
			return;
		}
		
		if (aiForce.GetTotalManpower() > m_iBaseForceSize) {
			JWK_Log.Log(this, string.Format(
				"Existing garrison size exceeds capacity: %1/%2!",
				aiForce.GetTotalManpower(), m_iBaseForceSize
			), LogLevel.WARNING);
			
			ClampForceToCapacity_S(aiForce, m_iBaseForceSize);
			return;
		}
		
		JWK_Log.Log(this, string.Format(
			"Existing garrison size: %1/%2.",
			aiForce.GetTotalManpower(), m_iBaseForceSize
		));
	}
	
	override protected void InitForce_S(JWK_StreamableAIForce aiForce, int forceSize)
	{
		JWK_Log.Log(this, "Garrison empty, reinitializing.");
		
		JWK_FactionForceCompositionGenerator generator = null;
		if (m_FactionControl && m_FactionControl.GetFactionKey()) {
			generator = JWK_CombatFactionTrait.GetForceCompositionByKey(m_FactionControl.GetFactionKey());
		}
		
		if (!generator) {
			generator = JWK_CombatFactionTrait.GetForceCompositionByRole(JWK_EFactionRole.ENEMY);
		}
		
		JWK_FactionForceInfantryComposition forceComposition = 
			generator.GenerateInfantry(
				forceSize, forceSize,
				intent: m_iCompositionIntent,
				threat: JWK.GetWorldThreat().GetLevelAt(GetOwner().GetOrigin())
			);
		
		if (!forceComposition) return;
		
		for(int i = 0, n = forceComposition.GetGroupsCount(); i < n; i++) {
			JWK_AISpawnRequest spawnRequest = new JWK_AISpawnRequest();
			forceComposition.ApplyToSpawnRequest(i, spawnRequest);
			
			spawnRequest.m_AIForce = aiForce;
			JWK.GetAIForceManager().AddSpawnQueueItem_S(spawnRequest);
		}
	}
}

modded class JWK_ResistanceBattleControllerComponent
{
	override protected void Refresh_S()
	{
		// BM_GC_FIX: If the base is owned by Invaders, don't spawn "Friendly" Rebels on top of them.
		// These rebels are meant to assist players, but spawning them inside a freshly captured Invader base triggers a loop.
		if (m_Subject && m_Subject.GetOwner()) {
			JWK_FactionControlComponent facControl = JWK_CompTU<JWK_FactionControlComponent>.FindIn(m_Subject.GetOwner());
			if (facControl) {
				Faction currentFaction = facControl.GetFaction();
				string invaderKey = "";
				JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
				if (gm) invaderKey = gm.BM_GetInvaderFactionKey();
				
				if (!invaderKey.IsEmpty() && currentFaction && currentFaction.GetFactionKey() == invaderKey) {
					// It's Invader territory now. Stop spawning internal resistance helpers for this battle.
					return;
				}
			}
		}
		
		super.Refresh_S();
	}
}

modded class JWK_BattleSubjectComponent
{
	override bool CheckAutostartCondition_S()
	{
		// BM_GC_FIX: Prevent automatic battle re-triggering if the location is currently held by Invaders...
		// UNLESS a player is actually present to initiate the fight!
		if (GetOwner()) {
			JWK_FactionControlComponent facControl = JWK_CompTU<JWK_FactionControlComponent>.FindIn(GetOwner());
			if (facControl) {
				Faction currentFaction = facControl.GetFaction();
				string invaderKey = "";
				JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
				if (gm) invaderKey = gm.BM_GetInvaderFactionKey();
				
				if (!invaderKey.IsEmpty() && currentFaction && currentFaction.GetFactionKey() == invaderKey) {
					// Check for physical players in the area
					array<int> playerIds = {};
					GetGame().GetPlayerManager().GetPlayers(playerIds);
					bool playerFound = false;
					foreach (int id : playerIds)
					{
						IEntity playerEnt = GetGame().GetPlayerManager().GetPlayerControlledEntity(id);
						if (playerEnt && vector.Distance(playerEnt.GetOrigin(), GetOwner().GetOrigin()) < 300)
						{
							playerFound = true;
							break;
						}
					}

					// If a player is here, let the battle start so they can recapture it!
					if (playerFound) return super.CheckAutostartCondition_S();

					// Otherwise, keep it quiet to prevent AI vs AI loops
					return false;
				}
			}
		}
		
		return super.CheckAutostartCondition_S();
	}
}

modded class JWK_BattleManagerComponent
{
	override void StartBattle_S(JWK_BattleSubjectComponent subject)
	{
		if (subject && subject.GetOwner()) {
			JWK_GameMode gm = JWK_GameMode.Cast(GetGame().GetGameMode());
			if (gm) {
				string invaderKey = gm.BM_GetInvaderFactionKey();
				if (!invaderKey.IsEmpty()) {
					// 1. If it's an active INVADER ATTACK wave, we allow the director to handle it via ForceFinish
					if (gm.BM_GetCurrentTargetID() == subject.GetOwner().GetID()) {
						super.StartBattle_S(subject);
						return;
					}
					
					// 2. If it's an Invader-held base and a player is present, allow the battle normally
					JWK_FactionControlComponent facControl = JWK_CompTU<JWK_FactionControlComponent>.FindIn(subject.GetOwner());
					if (facControl && facControl.GetFactionKey() == invaderKey) {
						array<int> playerIds = {};
						GetGame().GetPlayerManager().GetPlayers(playerIds);
						bool playerFound = false;
						foreach (int id : playerIds)
						{
							IEntity playerEnt = GetGame().GetPlayerManager().GetPlayerControlledEntity(id);
							if (playerEnt && vector.Distance(playerEnt.GetOrigin(), subject.GetOwner().GetOrigin()) < 300)
							{
								playerFound = true;
								break;
							}
						}
						
						if (playerFound) {
							super.StartBattle_S(subject);
							return;
						}
						
						// If no player, suppress to prevent AI vs AI loops
						return;
					}
				}
			}
		}
		
		super.StartBattle_S(subject);
	}
}

modded class JWK_ReinforcementsQrfControllerEntity
{
	override protected JWK_CombinedFactionForceComposition CreateForceComposition()
	{
		JWK_FactionForceCompositionGenerationRequest request = new JWK_FactionForceCompositionGenerationRequest();
		
		if (m_iCompositionIntent == JWK_EFactionForceCompositionIntent.GENERIC) {
			if (m_fThreat > 0.8) request.intent = JWK_EFactionForceCompositionIntent.QRF_SOF;
			else request.intent = JWK_EFactionForceCompositionIntent.QRF_REGULAR;
		} else {
			request.intent = m_iCompositionIntent;
		}
		
		request.manpowerAbsMin = m_iManpowerMin;
		request.manpowerMax = m_iManpowerMax;
		request.threat = m_fThreat;
		request.useVehicles = m_bCanUseVehicles;
		request.mechanizedManpowerShare = m_fMechanizedShare;
		request.allowLightArmor = m_bAllowLightArmor;
		request.allowHeavyArmor = m_bAllowHeavyArmor;
		
		// BM_GC_FIX: Dynamically determine the Faction to spawn for QRF based on the target area's owner
		JWK_FactionForceCompositionGenerator generator = null;

		// Find the closest known location node to our origin
		EntityID targetID = JWK_IndexSystem.Get().FindNearestXZ(JWK_MilitaryBaseEntity, GetOrigin());
		if (targetID == EntityID.INVALID)
			targetID = JWK_IndexSystem.Get().FindNearestXZ(JWK_FactoryEntity, GetOrigin());
		
		IEntity targetLocation = GetGame().GetWorld().FindEntityByID(targetID);

		if (targetLocation) {
			JWK_FactionControlComponent facControl = JWK_CompTU<JWK_FactionControlComponent>.FindIn(targetLocation);
			if (facControl && facControl.GetFactionKey()) {
				// Use the faction that currently owns the base to generate the QRF
				string ownerKey = facControl.GetFactionKey();
				generator = JWK_CombatFactionTrait.GetForceCompositionByKey(ownerKey);
				Print("BM_Invasion: QRF reinforcing " + targetLocation.GetPrefabData().GetPrefabName() + " owned by " + ownerKey, LogLevel.NORMAL);
			}
		}
		
		if (!generator) {
			generator = JWK_CombatFactionTrait.GetForceCompositionByRole(m_iFactionRole);
			Print("BM_Invasion: QRF fallback to default role: " + SCR_Enum.GetEnumName(JWK_EFactionRole, m_iFactionRole), LogLevel.WARNING);
		}
		
		return generator.Generate(request);
	}
}

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
// BakerMods override: fixed syntax errors in Strategies array (periods → commas at rows 2 and 7).

class JWK_BattleAIWave
{
	ref JWK_CombinedFactionForceComposition m_ForceComposition;
}

class JWK_WaveBattleAIGenerator : JWK_BaseBattleAIGenerator
{
	[Attribute(defvalue: "180000", desc: "Defaults to 3 mins.")]
	protected int m_iNextWaveTimerMin;

	[Attribute(defvalue: "420000", desc: "Defaults to 7 mins.")]
	protected int m_iNextWaveTimerMax;

	[Attribute(defvalue: "50")]
	protected int m_iMinWaveBudget;

	[Attribute(defvalue: "150")]
	protected int m_iMaxWaveBudget;

	// --------------------------------------------------------------------------------------------------------

	static const int UPDATE_INTERVAL_MS = 5000;

	protected int m_iWaveTimer;
	protected int m_iCurrentWave = -1;

	protected ref array<ref JWK_BattleAIWave> m_aWaves = {};
	protected ref JWK_UnitsPlacementContext m_PlacementContext;
	protected ref JWK_BaseAIDeployer m_Deployer;

	// --------------------------------------------------------------------------------------------------------

	override void StartBattle()
	{
		super.StartBattle();

		m_AIForce.GetOnSpawnQueueEmptied().Insert(OnSpawnQueueEmptied);
		JWK.GetGameplayTimeCallQueue().CallLater(Update, UPDATE_INTERVAL_MS, true);
	}

	override void Reset()
	{
		super.Reset();

		m_AIForce.GetOnSpawnQueueEmptied().Remove(OnSpawnQueueEmptied);
		JWK.GetGameplayTimeCallQueue().Remove(Update);

		if (m_Deployer) {
			m_Deployer.GetOnFinished().Remove(OnDeployerFinished);
			m_Deployer = null;
		}

		m_aWaves = {};

		m_iWaveTimer = 0;
		m_iCurrentWave = -1;

		m_PlacementContext = null;
	}

	protected void Update()
	{
		if (!m_PlacementContext) {
			if (!m_PlacementGenerator.IsResultReady()) return;

			m_PlacementContext = m_PlacementGenerator.CreateResultContext();
			m_PlacementGenerator = null;

			InitializeWaves();
		}

		if (m_Controller.IsDelayActive_S()) return;
		if (m_iWaveTimer < 0) return;

		m_iWaveTimer = Math.Max(0, m_iWaveTimer - UPDATE_INTERVAL_MS);

		if (CanDeployNextWave())
			DeployNextWave();
	}

	protected void InitializeWaves()
	{
		JWK_Log.Log(this, "Initializing attack waves.");

		// to do: unhardcode
		static const array<ref array<float>> Strategies = {
			{0.2, 0.2, 0.6},
			{0.2, 0.3, 0.5},
			{0.2, 0.4, 0.4},
			{0.3, 0.3, 0.4},
			{0.4, 0.4, 0.2},
			{0.4, 0.3, 0.3},
			{0.5, 0.3, 0.2},
			{0.6, 0.2, 0.2}
		};
		const int waveCount = 3;
		const int wavePattern = JWK.Random.RandInt(0, Strategies.Count());
		int budgetLeft = GetBudget();

		JWK_Log.Log(this, "Selected wave pattern: " + wavePattern + ".");

		// Do not call ConsumeBudget(), but track it manually! We are only planning the waves at this point,
		// not deploying them.

		for (int i = 0; budgetLeft > 0; i++) {
			float waveSizeFactor = 1;
			float threat = 1;

			if (i < 3) {
				waveSizeFactor = Strategies[wavePattern][i];
				threat = Math.Min(1, (i+1) / waveCount);
			}

			int waveBudget = Math.Clamp(waveSizeFactor * GetBudget(), m_iMinWaveBudget, m_iMaxWaveBudget);
			waveBudget = Math.Min(waveBudget, budgetLeft);

			// If the budget left after this wave would be less than m_iMinWaveBudget, merge the leftover into the
			// current wave. This prevents situation in which last wave consists of only a few characters.
			if (budgetLeft - waveBudget < m_iMinWaveBudget)
				waveBudget = budgetLeft;

			JWK_Log.Log(this, string.Format(
				"Generating wave %1 with budget %2 out of %3 left.",
				(i+1), waveBudget, budgetLeft
			));

			int consumed = GenerateWave(waveBudget, threat);
			// leftover budget that cant be spent on anything, so just finish, and mark that leftover budget as consumed
			// so it doesnt block battle progress
			if (consumed == 0) {
				JWK_Log.Log(this, string.Format("Marking %1 leftover budget as spent.", budgetLeft));
				ConsumeBudget(budgetLeft);
				break;
			}

			budgetLeft -= consumed;
			JWK_Log.Log(this, string.Format("Wave %1 consumed %2 budget.", (i+1), consumed));
		}

		FinalizeWavesGeneration();
	}

	protected void FinalizeWavesGeneration()
	{
		SCR_StringArray manpowers = {};
		int totalManpower;

		foreach (JWK_BattleAIWave wave : m_aWaves) {
			const int manpower = wave.m_ForceComposition.GetManpower();
			manpowers.Insert(manpower.ToString());
			totalManpower += manpower;
		}

		JWK_Log.Log(this, string.Format(
			"Generated %1 waves in total with manpowers: %2 (total %3).",
			m_aWaves.Count(), manpowers.Join(", "), totalManpower
		));
	}

	protected int GenerateWave(int budget, float threat)
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

		wave.m_ForceComposition = JWK_CombatFactionTrait.GetForceCompositionByRole(JWK_EFactionRole.ENEMY)
			.Generate(request);
		m_aWaves.Insert(wave);

		// Clamp because the actual manpower may be inflated by difficulty settings, which we dont want to count in.
		return Math.Min(budget, wave.m_ForceComposition.GetManpower()) * CharacterBudgetCost;
	}

	protected bool CanDeployNextWave()
	{
		if (m_iCurrentWave == -1) return true;
		if (m_Deployer && !m_Deployer.IsFinished()) return false;

		if (m_iWaveTimer == 0) {
			JWK_Log.Log(this, "Triggering next wave, timer elapsed.");
			return true;
		}

		if (m_AIForce.m_iPendingSpawnRequests == 0) {
			int aliveAI = m_AIForce.CountAgents();
			int currentWaveManpower = m_aWaves[m_iCurrentWave].m_ForceComposition.GetManpower();

			if (aliveAI < currentWaveManpower / 2) {
				JWK_Log.Log(this, "Triggering next wave, "
					+ aliveAI + "/" + currentWaveManpower + " of current wave alive."
				);
				return true;
			}
		}

		return false;
	}

	bool DeployNextWave()
	{
		return DeployNextWaveStrategy(SelectDeploymentStrategy(m_iCurrentWave+1));
	}

	bool DeployNextWaveStrategy(JWK_EAIDeployStrategy strategy)
	{
		if (m_iCurrentWave+1 >= m_aWaves.Count()) return false;
		if (GetBudgetLeft() <= 0) return false;

		// --
		m_PlacementContext.Reinit();

		if (m_PlacementContext.m_CurrentBag.GetInfantryPositionsCount() == 0) {
			JWK_Log.Log(this, "Placement bag is empty!", LogLevel.ERROR);
			return false;
		}

		m_iCurrentWave += 1;

		JWK_Log.Log(this, string.Format(
			"Sending wave %1 with deployment strategy: %2.",
			m_iCurrentWave,
			SCR_Enum.GetEnumName(JWK_EAIDeployStrategy, strategy)
		));

		m_Deployer = JWK.GetAIManager().CreateAIDeployer(strategy);
		m_Deployer.SetForceComposition(m_aWaves[m_iCurrentWave].m_ForceComposition);
		m_Deployer.SetPlacementContext(m_PlacementContext);
		m_Deployer.SetAIForce(m_AIForce);
		m_Deployer.GetOnFinished().Insert(OnDeployerFinished);
		m_Deployer.Start();
		return true;
	}

	JWK_EAIDeployStrategy SelectDeploymentStrategy(int waveIndex)
	{
		float diceRoll = JWK.Random.RandFloat01();

		JWK_EAIDeployStrategy strategy = JWK_EAIDeployStrategy.SPREADED;
		if (diceRoll < 0.3) strategy = JWK_EAIDeployStrategy.CONCENTRATED;
		else if (diceRoll < 0.6) strategy = JWK_EAIDeployStrategy.CONVOY;

		return strategy;
	}

	protected void OnDeployerFinished()
	{
		const int spent = m_aWaves[m_iCurrentWave].m_ForceComposition.GetManpower()
			* JWK_BaseBattleAIGenerator.CharacterBudgetCost;

		if (spent == 0) {
			JWK_Log.Log(this, "Failed to spend any budget!", LogLevel.ERROR);
		}

		JWK_Log.Log(this, string.Format(
			"%1 budget spent on wave %2.",
			spent, m_iCurrentWave
		));

		if (m_iCurrentWave+1 < m_aWaves.Count()) {
			m_iWaveTimer = m_iNextWaveTimerMin
				+ JWK.Random.RandFloat01() * (m_iNextWaveTimerMax - m_iNextWaveTimerMin);

			JWK_Log.Log(this, "Next wave timer set to: " + m_iWaveTimer + "ms.");
		} else {
			m_iWaveTimer = -1;
		}

		ConsumeBudget(spent);
	}

	protected void OnSpawnQueueEmptied()
	{
		if (m_iCurrentWave == -1) return;

		JWK_Log.Log(this, string.Format(
			"Spawn queue emptied. Agents: %1, last wave: %2.",
			m_AIForce.m_iAgentsCount, m_aWaves[m_iCurrentWave].m_ForceComposition.GetManpower()
		));
	}
}

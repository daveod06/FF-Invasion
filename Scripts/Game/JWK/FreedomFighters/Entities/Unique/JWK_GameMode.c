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

class JWK_GameModeClass: SCR_BaseGameModeClass
{
}

class JWK_GameMode : SCR_BaseGameMode
{
	[Attribute("{461473B74D9BC6FF}Prefabs/World/JWK_World.et")]
	protected ResourceName m_rDefaultWorldPrefab;
	
	protected ref array<JWK_GameModeComponent> m_aComponents = {};
	// todo: move to framework system
	protected ref JWK_GameplayTimeScheduler m_GameplayTimeScheduler = new JWK_GameplayTimeScheduler();
	
	// ---------------------------------------------
	
	protected JWK_FrameworkSystem m_Framework;
	protected JWK_WorldEntity m_World;
	
	protected JWK_PreGameStateComponent m_PreGame;
	protected JWK_PersistenceManagerComponent m_Persistence;
	protected JWK_GameConfigComponent m_GameConfig;
	
	// ---------------------------------------------

	protected bool m_bGameModeInitialized;
	protected bool m_bIsNewGame_S;
	protected bool m_bWorldLoaded;
	protected bool m_bSaveLoaded_S;
	protected bool m_bIsGameEnd;
	
	// ---------------------------------------------
	
	protected int m_iRootSeed;
	
	// ---------------------------------------------
	
	// (int playerId, string persistentId)
	protected ref ScriptInvoker m_OnPlayerSetup_S;
	
	// (int playerId, IEntity from, IEntity to)
	// Invoked both on client and server!
	protected ref ScriptInvoker m_OnPlayerControlledEntityChanged;
	
	// (int playerId, IEntity entity, vector fromOrigin)
	// Invoked on server when player has been teleported, likely over a long distance. It may be also invoked
	// with fromOrigin == vector.Zero to indicate a spawn.
	protected ref ScriptInvoker m_OnPlayerTeleported_S;
	
	// (int rootSeed)
	protected ref ScriptInvokerInt m_OnRootSeedChanged;
	
	// ---------------------------------------------
	
	#ifdef JWK_DEBUG
	protected ref JWK_DiagHandler m_DiagHandler = new JWK_DiagHandler();
	protected ref JWK_DebugDrawManager m_DebugDrawManager;
	#endif

	// ---------------------------------------------
	
	void JWK_GameMode(IEntitySource src, IEntity parent)
	{		
		JWK_Log.Notice();
	}
	
	//------------------------------------------------------------------------------------------------
	
	// Has the game mode finished its EOnInit.
	bool IsInitialized()
	{
		return m_bGameModeInitialized;
	}
	
	// True if in current session a new game (will be/has been) started, i.e. there is no persisted state
	// that has been loaded. Can be used to perform one-off initializations after the startup procedure
	// finished.
	bool IsNewGame_S()
	{
		return m_bIsNewGame_S;
	}
	
	// Set to true at the beginning of OnWorldPostProcess callback. Means that static world is loaded, so
	// all entity/component searches and linking can begin.
	bool IsWorldLoaded()
	{
		return m_bWorldLoaded;
	}
	
	// Set to true when persistence is finished loading. NOTE: it will always remain false when new game
	// has been started (IsNewGame), as de facto there was no persistence state that was loaded.
	bool IsSaveLoaded_S()
	{
		return m_bSaveLoaded_S;
	}
	
	// OnGameEnd has been called.
	bool IsGameEnd()
	{
		return m_bIsGameEnd;
	}
	
	// This normally shouldn't be used except by the utility command from DevKit. Note that this most likely
	// will take no effect until the game is relaunched, as things depending on this may be already generated.
	// For that reason runtime root seed changes are also not synced over network.
	void SetRootSeed_S(int rootSeed)
	{
		m_iRootSeed = rootSeed;
	}
	
	// Persistent seed that is generated once on game start. Use this for any random sequences that should be
	// reproductible throughout the campaign.
	int GetRootSeed()
	{
		return m_iRootSeed;
	}
	
	JWK_PreGameStateComponent GetPreGame()
	{
		return m_PreGame;
	}
	
	// ----------------------------------------------------------------------------------------------
	
	ScriptInvoker GetOnPlayerSetup_S()
	{
		if (!m_OnPlayerSetup_S) m_OnPlayerSetup_S = new ScriptInvoker();
		return m_OnPlayerSetup_S;
	}
	
	ScriptInvoker GetOnPlayerControlledEntityChanged()
	{
		if (!m_OnPlayerControlledEntityChanged) m_OnPlayerControlledEntityChanged = new ScriptInvoker();
		return m_OnPlayerControlledEntityChanged;
	}
	
	void NotifyPlayerControlledEntityChanged(int playerId, IEntity from, IEntity to)
	{
		if (m_OnPlayerControlledEntityChanged)
			m_OnPlayerControlledEntityChanged.Invoke(playerId, from, to);
	}
	
	ScriptInvoker GetOnPlayerTeleported_S()
	{
		if (!m_OnPlayerTeleported_S) m_OnPlayerTeleported_S = new ScriptInvoker();
		return m_OnPlayerTeleported_S;
	}
	
	void NotifyPlayerTeleported_S(int playerId, IEntity entity, vector fromOrigin)
	{
		if (m_OnPlayerTeleported_S)
			m_OnPlayerTeleported_S.Invoke(playerId, entity, fromOrigin);
	}
	
	ScriptInvokerInt GetOnRootSeedChanged()
	{
		if (!m_OnRootSeedChanged) m_OnRootSeedChanged = new ScriptInvokerInt();
		return m_OnRootSeedChanged;
	}
	
	//------------------------------------------------------------------------------------------------
	
	override void OnGameModeStart()
	{
		super.OnGameModeStart();
		JWK_Log.Separator("OnGameModeStart");
		
		if (Replication.IsServer()) {
			OnGameModeStart_S();
			
		} else {
			JWK.GetFactions().SetupFactions();
		}
	}
	
	protected void OnFactionsConfigured_S()
	{
		JWK.GetFactions().SetupFactions();
	}
	
	protected void OnGameModeStart_S()
	{
		if (m_Persistence && m_Persistence.IsLoadFailure()) {
			JWK_FrameworkSystem.Get(GetWorld()).GetCallQueue().CallLater(
				SetFailureState_S, 0, false, JWK_EGameSetupFailureReason.BROKEN_SAVE
			);
			return;
		}
		
		if (m_bIsNewGame_S) {
			JWK.GetFactions().SetupFactions();
		}
		
		if (JWK.GetFactions().IsSetupFailed()) {
			JWK_FrameworkSystem.Get(GetWorld()).GetCallQueue().CallLater(
				SetFailureState_S, 0, false, JWK_EGameSetupFailureReason.FACTION_CONFIGS
			);
			return;
		}
		
		if (m_bIsNewGame_S) {
			m_iRootSeed = System.GetUnixTime();
			JWK_Log.Log(this, "Generated root seed = " + m_iRootSeed);
			
			RpcDo_SetRootSeed(m_iRootSeed);
			Rpc(RpcDo_SetRootSeed, m_iRootSeed);
			
			JWK.GetNotifications().BroadcastNotification_S(
				ENotification.JWK_GAME_STARTED,
				JWK.GetLegacyWorldSettings().m_sPlaceName
			);
			
			foreach (JWK_GameModeComponent component : m_aComponents)
				component.NewGameStart_S();
		}

		foreach (JWK_GameModeComponent component : m_aComponents) {
			component.PostGameModeStart_S();
		}
	
		JWK_Log.Log(this, "Started.");
		JWK_Log.Notice();
		
		if (EPF_PersistenceManager.GetInstance())
			EPF_PersistenceManager.GetInstance().JWK_SetPersistencePaused(false);
		
		m_Framework.GetCallQueue().CallLater(OnGameModeStartDelayed_S, 0);
	}

	protected void OnGameModeStartDelayed_S()
	{		
		JWK_Log.Separator("OnGameModeStartDelayed");
		foreach (JWK_GameModeComponent component : m_aComponents)
			component.PostGameModeStartDelayed_S();
		
		if (m_bIsNewGame_S) {
			array<int> playerIds = {};
			GetGame().GetPlayerManager().GetPlayers(playerIds);
			
			foreach (int playerId : playerIds)
				JWK.GetPlayerController(playerId).OpenSpawnSelectionMenu();
		}
		
		// force GM access in single player, which normally is blocked
		if (RplSession.Mode() == RplMode.None)
			EnableGameMasterForPlayer_S(SCR_PlayerController.GetLocalPlayerId());
	}
	
	override void OnGameEnd()
	{
		JWK_Log.Separator("OnGameEnd");
		m_bIsGameEnd = true;
		
		super.OnGameEnd();
	}
	
	protected void SetFailureState_S(int reason)
	{
		RpcDo_SetLoadFailureState(reason);
		Rpc(RpcDo_SetLoadFailureState, reason);
	}
	
	// ----------------------------------------------------------------------------------------------

	// Server method.
	protected override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{	
		IEntity entity = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (entity) {
			EPF_PersistenceComponent persistence = JWK_CompTU<EPF_PersistenceComponent>
				.FindIn(entity);

			if (persistence) {
				persistence.PauseTracking();
				persistence.Save();
			}
		}

		super.OnPlayerDisconnected(playerId, cause, timeout);
	}
	
	// ----------------------------------------------------------------------------------------------

	// Server method.
	void SetupPlayer(int playerId, string persistentId)
	{
		if (!Replication.IsServer()) return;
		
		JWK_Log.Log(this, "Setting up player (ID: " + playerId + ", persistent ID: " + persistentId + ").");

		if (m_OnPlayerSetup_S) m_OnPlayerSetup_S.Invoke(playerId, persistentId);
	}
	
	vector GetPlayerRandomSpawnPos()
	{
		JWK_TownManagerComponent townMgr = JWK.GetTowns();
		
		auto worldIndex = JWK_IndexSystem.Get(GetWorld());
		array<EntityID> comps = worldIndex.GetAll(JWK_PublicTransportStopComponent);
		if (comps.IsEmpty())
			comps = worldIndex.GetAll(JWK_TownEntity);
		
		if (comps.IsEmpty()) {
			JWK_Log.Log(this,
				"Failed to find fallback spawn position: there are no public transport stops nor towns in the world!",
				LogLevel.FATAL
			);
			return vector.Zero;
		}
		
		EntityID entityID = comps.GetRandomElement();
		return GetWorld().FindEntityByID(entityID).GetOrigin();
	}
	
	// ----------------------------------------------------------------------------------------------

	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		
		#ifdef JWK_DEBUG
		m_DiagHandler.InitDiag();
		m_DebugDrawManager = new JWK_DebugDrawManager();
		m_DebugDrawManager.Initialize();
		#endif
		
		if (SCR_Global.IsEditMode()) return;
		
		m_Framework = JWK_FrameworkSystem.Get(GetWorld());
		if (!m_Framework) {
			JWK_Log.Log(this, "Framework system not found!", LogLevel.FATAL);
		}
		
		m_Persistence = JWK_CompTU<JWK_PersistenceManagerComponent>.FindIn(owner);
		
		if (!CheckRequiredWorldEntities()) {
			JWK_Log.Log(this, "Incorrect world setup, game will not be started.", LogLevel.FATAL);
			return;
		}
		
		JWK_Log.Separator("EOnInit");
		
		if (EPF_PersistenceManager.IsPersistenceMaster()) {
			EPF_PersistenceComponent persistence = JWK_CompTU<EPF_PersistenceComponent>.FindIn(this);
			if (persistence)
				persistence.SetPersistentId("{JWK_GameMode-FreedomFighters}");
		}
		
		m_GameplayTimeScheduler.Init(IsMaster());
		m_PreGame = JWK_CompTU<JWK_PreGameStateComponent>.FindIn(owner);
		m_GameConfig = JWK_CompTU<JWK_GameConfigComponent>.FindIn(owner);
		
		DoComponentsInit();

		if (IsMaster()) EOnInit_S();
		
		m_bGameModeInitialized = true;
		JWK_Log.Log(this, "Entity initialized.");
	}
	
	protected void EOnInit_S()
	{
		JWK.GetProfileConfig().LoadConfig_S();
	}
	
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		super.EOnFrame(owner, timeSlice);
		
		if (IsRunning())
			m_GameplayTimeScheduler.SystemTick(timeSlice);
		
		#ifdef JWK_DEBUG
		m_DiagHandler.Update(timeSlice);
		m_DebugDrawManager.Update();
		#endif
	}
	
	// ----------------------------------------------------------------------------------------------
	
	protected void InitPersistence_S()
	{
		if (!m_Persistence || !m_Persistence.HasSaveGame()) {
			JWK_Log.Log(this, "No existing save found.");
			m_bIsNewGame_S = true;
			return;
		}
		
		JWK_Log.Log(this, "Found existing save data.");
		if (m_GameConfig) {
			m_GameConfig.GetOnFactionsConfigured().Insert(OnFactionsConfigured_S);
		}
	}

	// Server method.
	override protected void OnPlayerSpawned(int playerId, IEntity controlledEntity)
	{
		NotifyPlayerTeleported_S(playerId, controlledEntity, vector.Zero);
	}
	
	protected bool CheckRequiredWorldEntities()
	{
		if (!GetGame().GetAIWorld()) {
			Debug.Error2("FreedomFighters", "Missing required SCR_AIWorld entity.");
			return false;
		}
		
		if (!SCR_MapEntity.GetMapInstance()) {
			Debug.Error2("FreedomFighters", "Missing required SCR_MapEntity entity.");
			return false;
		}
		
		return true;
	}
	
	protected void DoComponentsInit()
	{
		array<Managed> comps = {};		
		FindComponents(JWK_GameModeComponent, comps);
		
		//JWK_Log.Log(this, "Initializing components:");
		foreach (Managed comp : comps) {
			JWK_GameModeComponent gmc = JWK_GameModeComponent.Cast(comp);
			InitComponent(gmc);
		}
	}
	
	protected JWK_GameModeComponent InitComponent(JWK_GameModeComponent comp)
	{	
		m_aComponents.Insert(comp);
		//JWK_Log.Log(this, "" + comp.Type().ToString() + ".");
		comp.GameModeInit(this);
		
		return comp;
	}
	
	protected void EnableGameMasterForPlayer_S(int playerId)
	{
		SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(
			SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore)
		);
		if (!core) return;

		SCR_EditorManagerEntity editorManager = core.GetEditorManager(playerId);
		if (!editorManager) return;
		
		editorManager.AddEditorModes(EEditorModeAccess.BASE, EEditorMode.EDIT, false);
	}
	
	override event void OnWorldPostProcess(World world)
	{
		JWK_Log.Separator("OnWorldPostProcess");
		m_bWorldLoaded = true;
		
		// ---------------------------------------
		
		JWK_WorldEntity worldEntity = JWK_WorldEntity.Get();
		if (worldEntity && worldEntity.GetWorld() == GetWorld()) {
			m_World = worldEntity;
		} else if (!SCR_Global.IsEditMode()) {
			JWK_Log.LogDeprecated(this, "JWK_WorldEntity not found!");
			m_World = JWK_WorldEntity.Cast(
				JWK_SpawnUtils.SpawnEntityPrefabLocal(m_rDefaultWorldPrefab, vector.Zero)
			);
		}
		
		// ---------------------------------------
		
		if (m_World)
			m_World.OnWorldPostProcess();
		
		if (!ValidateWorldSetup()) {
			Debug.Error2("JWK", "World setup validation failed. Check console log for details.");
			m_bGameModeInitialized = false;
			
			if (Replication.IsServer() && GetGame().InPlayMode()) {
				SetFailureState_S(JWK_EGameSetupFailureReason.WORLD_SETUP);
				m_PreGame.OnStateEntered();
			}
			return;
		}

		foreach (JWK_GameModeComponent component : m_aComponents)
			component.OnWorldPostProcess(world);
		
		JWK_Log.Log(this, "World post process finished.");
		
		// ---------------------------------------
		
		if (GetGame().InPlayMode()) {
			JWK.GetFactions().InitializeFactions();
		}
		
		// ---------------------------------------
		
		auto worldIndex = JWK_IndexSystem.Get(GetWorld());
		if (worldIndex) {
			#ifdef WORKBENCH
			worldIndex.LogState();
			#endif
		} else if (!world.IsEditMode()) {
			JWK_Log.Log(this, "Index not present in the world!", LogLevel.ERROR);
		}
		
		JWK_Log.Notice();
		super.OnWorldPostProcess(world);
		
		if (!Replication.IsServer() || !GetGame().InPlayMode()) return;
		
		InitPersistence_S();
		
		// Has to be delayed for IsDedicated() check and the possible call to StartGameMode()
		m_Framework.GetCallQueue().CallLater(OnWorldPostProcessDelayed_S);
	}
		
	protected void OnWorldPostProcessDelayed_S()
	{
		if (m_bIsNewGame_S) {
			JWK_ProfileConfigComponent profileConfig = JWK.GetProfileConfig();
			if (JWK_Rpl.IsDedicated() && profileConfig && profileConfig.IsAutoStart()) {
				profileConfig.ApplyToGameMode_S();
				
				if (!JWK.GetFactions().ValidateConfig()) {
					JWK_Log.Log(this, "Invalid faction selection, reverting to defaults!", LogLevel.WARNING);
					JWK.GetGameConfig().ResetFactionsToDefault();
				}
			}
			
			// On clients, this is called in PostRplLoad.
			m_PreGame.OnStateEntered();
		} else {
			// Server only. If there is an existing save, go straight to running state.
			StartGameMode();
		}
	}
	
	void OnPersistenceLoadFinished()
	{
		DoPostLoad();
	}
	
	void FinishGame(bool isWin)
	{
		JWK_Log.Separator("FinishGame");
		JWK_Log.Log(this, "Win condition: " + isWin);
		
		EGameOverTypes gameOverType = EGameOverTypes.FREEDOMFIGHTERS_GAME_OVER;
		if (isWin) gameOverType = EGameOverTypes.FREEDOMFIGHTERS_VICTORY;
		
		// todo: should be moved to a separate notification controller
		ENotification notification;
		if (isWin) {
			notification = ENotification.JWK_GAME_FINISHED_VICTORY;
		} else {
			notification = ENotification.JWK_GAME_FINISHED_DEFEAT;
		}
		
		JWK.GetNotifications().BroadcastNotification_S(
			notification,
			JWK.GetFactions().GetPlayerSCRFaction().GetUIInfo().GetName(),
			JWK_WorldEntity.Get().GetPlaceName(),
			JWK.GetFactions().GetEnemySCRFaction().GetUIInfo().GetName()
		);
		// todo end
		
		EndGameMode(SCR_GameModeEndData.CreateSimple(gameOverType));
	}
	
	// --------------------------------------------------------------------------------------------
	
	// Server method.
	protected void DoPostLoad()
	{		
		if (!m_bGameModeInitialized) return;
		
		m_bSaveLoaded_S = true;
		JWK_Log.Separator("DoPostLoad");
		
		foreach (JWK_GameModeComponent component : m_aComponents)
			component.PostGameLoad_S();
		
		JWK_FrameworkSystem.Get().GetCallQueue().CallLater(LogBanner);
	}

	protected void LogBanner()
	{
		JWK_PersistenceManagerComponent persistence = JWK_CompTU<JWK_PersistenceManagerComponent>.FindIn(this);
		JWK_ProfileConfigComponent profileConfig = JWK_CompTU<JWK_ProfileConfigComponent>.FindIn(this);
		
		JWK_Log.Separator(this, "Startup Complete");
		if (persistence && persistence.HasSaveGame()) {
			JWK_Log.Log(this, "Continuing from an existing save data.");
			JWK_Log.Log(this, string.Format("Gameplay time: %1s.", JWK_GameplayTimestamp.GetSystemAccumulator()));
			
			JWK_GameProgressManagerComponent gameProgress = JWK_CompTU<JWK_GameProgressManagerComponent>.FindIn(this);
			if (gameProgress) {
				JWK_Log.Log(this, string.Format(
					"Game progress: %1%%.", Math.Floor(gameProgress.GetTotalProgressPercentage())
				));
			}
		} else {
			JWK_Log.Log(this, "No existing save data found.");
			
			if (JWK_Rpl.IsDedicated()) {
				if (profileConfig && profileConfig.IsAutoStart()) {
					JWK_Log.Log(this, "The campaign will autostart once first player joins the game.");
					JWK_Log.Log(this, string.Format(
						"Selected factions are: PLAYER = %1, ENEMY = %2, SUPPORTING = %3, AMBIENT = %4.",
						JWK.GetGameConfig().GetPlayerFactionKey(),
						JWK.GetGameConfig().GetEnemyFactionKey(),
						JWK.GetGameConfig().GetSupportingFactionKey(),
						JWK.GetGameConfig().GetAmbientFactionKey(),
					));
					
					string presetName = JWK.GameSettings().GetPresetKey();
					if (presetName.IsEmpty()) presetName = "<custom>";
					JWK_Log.Log(this, string.Format("Selected settings preset: %1.", presetName));
					
				} else {
					JWK_Log.Log(this, "Join the game and log in as administrator to confirm settings and launch the campaign.");
				}
			}
		}
		
		JWK_Log.Log(this, "***************************************************************");
		JWK_Log.Log(this, "*** If you see this message, your server has fully launched ***");
		JWK_Log.Log(this, "***    and is now running the Freedom Fighters game mode.   ***");
		JWK_Log.Log(this, "***************************************************************");
	}

	protected bool ValidateWorldSetup()
	{	
		foreach (JWK_GameModeComponent component : m_aComponents) {
			if (!component.ValidateWorldSetup()) {
				JWK_Log.Log(this, string.Format(
					"World validation failed by: %1!", component.ClassName()
				), LogLevel.FATAL);
				
				return false;
			}
		}
		
		if (!JWK.GetFactions()) {
			JWK_Log.Log(this, "JWK_FactionManager not found!", LogLevel.FATAL);
			return false;
		}
		
		return true;
	}
	
	// --------------------------------------------------------------------------------------------
	
	override bool RplSave(ScriptBitWriter writer)
	{
		super.RplSave(writer);
		
		writer.WriteInt(m_iRootSeed);
		
		if (m_PreGame)
			writer.WriteInt(m_PreGame.GetFailureReason());

		return true;
	}
		
	override bool RplLoad(ScriptBitReader reader)
	{
		super.RplLoad(reader);
		
		if (!reader.ReadInt(m_iRootSeed)) return false;
		
		if (m_PreGame) {
			int reason;
			if (!reader.ReadInt(reason)) return false;
			
			if (reason != JWK_EGameSetupFailureReason.NONE) {
				RpcDo_SetLoadFailureState(reason);
				return true;
			}
		}
		
		m_Framework.GetCallQueue().CallLater(PostRplLoad);
		return true;
	}
	
	protected void PostRplLoad()
	{
		// Note: this is a hack to get around the problem that SCR_BaseGameMode initializes with state already set to
		// PREGAME, so on RplLoad value change is not triggered, and as a result OnStateEntered method of
		// SCR_BaseGameModeStateComponent attached to PREGAME is never invoked.
		
		if (GetState() == SCR_EGameModeState.PREGAME && m_PreGame)
			m_PreGame.OnStateEntered();
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_SetLoadFailureState(int reason)
	{
		if (reason == JWK_EGameSetupFailureReason.BROKEN_SAVE) {
			JWK_PersistenceManagerComponent persistence = JWK_CompTU<JWK_PersistenceManagerComponent>.FindIn(this);
			if (persistence) persistence.SetLoadFailed_C(true);
		}
		
		m_PreGame.SetFailureFlag(true, reason);
		m_PreGame.OnStateEntered();
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_SetRootSeed(int rootSeed)
	{
		m_iRootSeed = rootSeed;
		if (m_OnRootSeedChanged) m_OnRootSeedChanged.Invoke(m_iRootSeed);
	}
	
	// --------------------------------------------------------------------------------------------
	
	void SaveState_S(JWK_GameModeSaveData saveData)
	{
		saveData.m_iGameplayTime = JWK_GameplayTimestamp.GetSystemAccumulator();
		saveData.m_iRootSeed = m_iRootSeed;
		
		if (m_Persistence)
			saveData.m_sSaveRevision = m_Persistence.GetSaveRevision();
	}
	
	void LoadState_S(JWK_GameModeSaveData saveData)
	{
		JWK_GameplayTimestamp.SetSystemAccumulator(saveData.m_iGameplayTime);
		m_iRootSeed = saveData.m_iRootSeed;
		
		if (m_iRootSeed == 0) {
			m_iRootSeed = System.GetUnixTime();
			
			JWK_Log.Log(this, string.Format(
				"Root seed not set, regenerating: %1.", m_iRootSeed
			), LogLevel.WARNING);
		} else {
			JWK_Log.Log(this, string.Format("Root seed: %1.", m_iRootSeed));
		}
		
		if (m_Persistence) {
			if (!m_Persistence.IsSaveRevisionSupported(saveData.m_sSaveRevision)) {
				JWK_Log.Log(this, string.Format(
					"Detected save data revision mismatch, current: %1, got: %2!",
					m_Persistence.GetSaveRevision(),
					saveData.m_sSaveRevision
				), LogLevel.FATAL);
				
				EPF_PersistenceManager.GetInstance().JWK_SetLoadFailure(true);
			}
		}
	}
}

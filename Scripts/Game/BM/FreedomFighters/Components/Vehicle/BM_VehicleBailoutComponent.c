[ComponentEditorProps(category: "BakerMods/Components", description: "Monitors vehicle health and forces crew to bail out upon taking significant damage.")]
class BM_VehicleBailoutComponentClass : ScriptComponentClass
{
}

class BM_VehicleBailoutComponent : ScriptComponent
{
	protected IEntity m_Owner;
	protected SCR_VehicleDamageManagerComponent m_DamageManager;
	protected bool m_bHasBailed;

	override void OnPostInit(IEntity owner)
	{
		m_Owner = owner;
		m_bHasBailed = false;
		SetEventMask(owner, EntityEvent.INIT);
	}
	
	override void EOnInit(IEntity owner)
	{
		m_DamageManager = SCR_VehicleDamageManagerComponent.Cast(owner.FindComponent(SCR_VehicleDamageManagerComponent));
		if (m_DamageManager)
		{
			m_DamageManager.GetOnDamageStateChanged().Insert(OnVehicleStateChanged);
		}
	}

	protected void OnVehicleStateChanged(EDamageState state)
	{
		if (m_bHasBailed)
			return;

		// Bailout if the vehicle is ruined or heavily damaged
		if (state == EDamageState.DESTROYED)
		{
			m_bHasBailed = true;
			
			SCR_BaseCompartmentManagerComponent compartmentManager = SCR_BaseCompartmentManagerComponent.Cast(m_Owner.FindComponent(SCR_BaseCompartmentManagerComponent));
			if (!compartmentManager)
				return;
			
			IEntity pilot;
			array<BaseCompartmentSlot> compartments = {};
			compartmentManager.GetCompartments(compartments);
			foreach (BaseCompartmentSlot slot : compartments) {
				if (slot.GetType() == ECompartmentType.PILOT) {
					pilot = slot.GetOccupant();
					break;
				}
			}
			
			if (!pilot)
				return;

			AIAgent agent = AIAgent.Cast(pilot);
			if (!agent)
				return;
			
			SCR_AIGroup group = SCR_AIGroup.Cast(agent.GetParentGroup());
			if (!group)
				return;
				
			Print(string.Format("BM_Bailout: Vehicle %1 took heavy damage. Forcing crew to dismount and engage.", m_Owner), LogLevel.NORMAL);

			while (group.GetCurrentWaypoint())
			{
				group.RemoveWaypointAt(0);
			}
			
			vector bailoutPos = m_Owner.GetOrigin();
			AIWaypoint getOut = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.GET_OUT, bailoutPos);
			AIWaypoint fight = JWK.GetAIManager().SpawnWaypoint(JWK_EAIWaypoint.DEFEND, bailoutPos);
			
			group.AddWaypoint(getOut);
			group.AddWaypoint(fight);
			
			m_DamageManager.GetOnDamageStateChanged().Remove(OnVehicleStateChanged);
		}
	}
	
	void ~BM_VehicleBailoutComponent()
	{
		if (m_DamageManager)
		{
			m_DamageManager.GetOnDamageStateChanged().Remove(OnVehicleStateChanged);
		}
	}
}

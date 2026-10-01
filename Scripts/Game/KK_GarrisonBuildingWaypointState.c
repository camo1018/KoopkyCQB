class KK_GarrisonBuildingWaypointState : SCR_AIWaypointState
{
	protected ref KK_GarrisonBuildingActivity m_Activity;

	void KK_GarrisonBuildingWaypointState(
		notnull SCR_AIGroupUtilityComponent utility,
		SCR_AIWaypoint waypoint)
	{
	}

	override void OnSelected()
	{
		super.OnSelected();
		StartGarrisonActivity();
	}

	override void OnExecuteWaypointTree()
	{
		StartGarrisonActivity();
	}

	protected void StartGarrisonActivity()
	{
		// A joining soldier restarts the waypoint tree. The order already
		// running has to stay. A failed one is dropped without completing
		// the waypoint, then started again for the squad as it is now.
		if (m_Activity && m_Activity.IsLive())
			return;

		if (m_Activity)
			m_Activity.Supersede();

		KK_GarrisonBuildingWaypoint waypoint =
			KK_GarrisonBuildingWaypoint.Cast(m_Waypoint);

		if (!waypoint)
		{
			if (SCR_BaseGameMode.KK_LogEnabled())
				Print(
					"KK: Garrison waypoint has invalid type",
					LogLevel.ERROR
				);
			return;
		}

		m_Activity = new KK_GarrisonBuildingActivity(
			m_Utility,
			waypoint
		);

		m_Utility.AddAction(m_Activity);
	}

	override void OnDeselected()
	{
		if (m_Activity)
		{
			m_Activity.CancelGarrison();
			m_Activity = null;
		}

		super.OnDeselected();
	}
}

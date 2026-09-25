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
		if (m_Activity)
			return;

		KK_GarrisonBuildingWaypoint waypoint =
			KK_GarrisonBuildingWaypoint.Cast(m_Waypoint);

		if (!waypoint)
		{
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

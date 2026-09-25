class KK_ClearBuildingWaypointState : SCR_AIWaypointState
{
	protected ref KK_ClearBuildingActivity m_Activity;

	void KK_ClearBuildingWaypointState(
		notnull SCR_AIGroupUtilityComponent utility,
		SCR_AIWaypoint waypoint)
	{
	}

	override void OnSelected()
	{
		super.OnSelected();
		StartClearActivity();
	}

	override void OnExecuteWaypointTree()
	{
		StartClearActivity();
	}

	protected void StartClearActivity()
	{
		if (m_Activity)
			return;

		KK_ClearBuildingWaypoint waypoint =
			KK_ClearBuildingWaypoint.Cast(m_Waypoint);

		if (!waypoint)
		{
			Print(
				"KK: Clear Building waypoint has invalid type",
				LogLevel.ERROR
			);
			return;
		}

		m_Activity = new KK_ClearBuildingActivity(
			m_Utility,
			waypoint
		);

		m_Utility.AddAction(m_Activity);
	}

	override void OnDeselected()
	{
		if (m_Activity)
		{
			m_Activity.CancelClear();
			m_Activity = null;
		}

		super.OnDeselected();
	}
}
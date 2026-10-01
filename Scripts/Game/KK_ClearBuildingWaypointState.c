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
		KK_ClearBuildingWaypoint waypoint =
			KK_ClearBuildingWaypoint.Cast(m_Waypoint);

		// A new clear or garrison waypoint replaces this one. Do not
		// start it again. A soldier still spawning restarts this same
		// waypoint, and that order has to keep running.
		if (waypoint && waypoint.IsReplaced())
		{
			if (m_Activity)
				m_Activity.CancelClear();
			return;
		}

		// A joining soldier restarts the waypoint tree. The order already
		// running has to stay. A failed one is dropped without completing
		// the waypoint, then started again for the squad as it is now.
		if (m_Activity && m_Activity.IsLive())
			return;

		if (m_Activity)
			m_Activity.Supersede();

		if (!waypoint)
		{
			if (SCR_BaseGameMode.KK_LogEnabled())
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
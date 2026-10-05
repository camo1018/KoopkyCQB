class KK_AttackWaypointState : SCR_AIWaypointState
{
	protected ref KK_AttackActivity m_Activity;

	void KK_AttackWaypointState(
		notnull SCR_AIGroupUtilityComponent utility,
		SCR_AIWaypoint waypoint)
	{
	}

	override void OnSelected()
	{
		super.OnSelected();
		StartAttackActivity();
	}

	override void OnExecuteWaypointTree()
	{
		StartAttackActivity();
	}

	protected void StartAttackActivity()
	{
		KK_AttackWaypoint waypoint = KK_AttackWaypoint.Cast(m_Waypoint);

		// A newer order replaces this one. Do not start it again. A
		// soldier still spawning restarts this same waypoint, and that
		// order has to keep running.
		if (waypoint && waypoint.IsReplaced())
		{
			if (m_Activity)
				m_Activity.CancelAttack();
			return;
		}

		if (m_Activity && m_Activity.IsLive())
			return;

		if (m_Activity)
			m_Activity.Supersede();

		if (!waypoint)
		{
			if (SCR_BaseGameMode.KK_LogEnabled())
				Print(
					"KK: Attack waypoint has invalid type",
					LogLevel.ERROR
				);
			return;
		}

		m_Activity = new KK_AttackActivity(
			m_Utility,
			waypoint
		);

		m_Utility.AddAction(m_Activity);
	}

	override void OnDeselected()
	{
		if (m_Activity)
		{
			m_Activity.CancelAttack();
			m_Activity = null;
		}

		super.OnDeselected();
	}
}

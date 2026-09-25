class KK_ClearBuildingWaypointClass : SCR_AIWaypointClass
{
}

class KK_ClearBuildingWaypoint : SCR_AIWaypoint
{
	[Attribute("75", UIWidgets.EditBox, "Building search radius")]
	protected float m_fBuildingSearchRadius;

	[Attribute("2.5", UIWidgets.EditBox, "Horizontal sample spacing")]
	protected float m_fHorizontalSpacing;
	
	[Attribute("1.0", UIWidgets.EditBox, "Vertical sample spacing")]
	protected float m_fVerticalSpacing;

	[Attribute("1.25", UIWidgets.EditBox, "Sample deduplication distance")]
	protected float m_fDeduplicateDistance;

	[Attribute("4", UIWidgets.EditBox, "Room-like cluster radius")]
	protected float m_fClusterRadius;

	[Attribute("2.5", UIWidgets.EditBox, "Distance considered visited")]
	protected float m_fArrivalRadius;

	[Attribute("45", UIWidgets.EditBox, "Movement timeout in seconds")]
	protected float m_fMovementTimeout;

	[Attribute("3", UIWidgets.EditBox, "Fail if standing still with no progress this many seconds")]
	protected float m_fStuckTimeout;

	[Attribute("1", UIWidgets.EditBox, "Attempts before target is unreachable")]
	protected int m_iMaximumRetries;

	[Attribute("1", UIWidgets.EditBox, "Times to retry held nodes after the rest are done")]
	protected int m_iDeferRetries;

	[Attribute("1", UIWidgets.CheckBox, "If one node in a floor cluster is unreachable, fail the rest of that cluster")]
	protected bool m_bFailClusterOnUnreachable = true;

	[Attribute("0", UIWidgets.CheckBox, "Draw interior debug points in Workbench")]
	protected bool m_bDebugDraw;

	float GetBuildingSearchRadius()
	{
		return m_fBuildingSearchRadius;
	}

	float GetHorizontalSpacing()
	{
		return Math.Max(
			m_fHorizontalSpacing,
			1.5
		);
	}
	
	float GetVerticalSpacing()
	{
		return Math.Max(
			m_fVerticalSpacing,
			1.0
		);
	}

	float GetDeduplicateDistance()
	{
		return m_fDeduplicateDistance;
	}

	float GetClusterRadius()
	{
		return m_fClusterRadius;
	}

	float GetArrivalRadius()
	{
		return m_fArrivalRadius;
	}

	float GetMovementTimeout()
	{
		return m_fMovementTimeout;
	}

	float GetStuckTimeout()
	{
		return m_fStuckTimeout;
	}

	int GetMaximumRetries()
	{
		return m_iMaximumRetries;
	}

	int GetDeferRetries()
	{
		return Math.Max(m_iDeferRetries, 0);
	}

	bool GetFailClusterOnUnreachable()
	{
		return m_bFailClusterOnUnreachable;
	}

	bool GetDebugDraw()
	{
		if (m_bDebugDraw)
			return true;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return false;

		return mode.KK_GetDebugDraw();
	}

	void ApplyScenarioSettings()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return;

		m_fBuildingSearchRadius = mode.KK_GetClearSearchRadius();
		m_fHorizontalSpacing = mode.KK_GetHorizontalSpacing();
		m_fVerticalSpacing = mode.KK_GetVerticalSpacing();
		m_fDeduplicateDistance = mode.KK_GetDeduplicateDistance();
		m_fClusterRadius = mode.KK_GetClusterRadius();
		m_fArrivalRadius = mode.KK_GetClearArrivalRadius();
		m_fMovementTimeout = mode.KK_GetClearMovementTimeout();
		m_fStuckTimeout = mode.KK_GetClearStuckTimeout();
		m_iMaximumRetries = mode.KK_GetClearMaximumRetries();
		m_iDeferRetries = mode.KK_GetClearDeferRetries();
		m_bFailClusterOnUnreachable = mode.KK_GetClearFailCluster();
	}

	override SCR_AIWaypointState CreateWaypointState(
		SCR_AIGroupUtilityComponent groupUtilityComp)
	{
		return new KK_ClearBuildingWaypointState(
			groupUtilityComp,
			this
		);
	}
}
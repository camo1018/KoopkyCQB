class KK_GarrisonBuildingWaypointClass : SCR_AIWaypointClass
{
}

class KK_GarrisonBuildingWaypoint : SCR_AIWaypoint
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

	[Attribute("2.5", UIWidgets.EditBox, "Distance considered arrived")]
	protected float m_fArrivalRadius;

	[Attribute("5", UIWidgets.EditBox, "Allowed combat reposition radius")]
	protected float m_fHoldRadius;

	[Attribute("3", UIWidgets.EditBox, "Seconds between hold corrections")]
	protected float m_fReassignmentInterval;

	[Attribute("20", UIWidgets.EditBox, "Minimum seconds at a post before rotating")]
	protected float m_fRotateIntervalMin;

	[Attribute("60", UIWidgets.EditBox, "Maximum seconds at a post before rotating")]
	protected float m_fRotateIntervalMax;

	[Attribute("45", UIWidgets.EditBox, "Movement timeout in seconds")]
	protected float m_fMovementTimeout;

	[Attribute("3", UIWidgets.EditBox, "Fail if standing still with no progress this many seconds")]
	protected float m_fStuckTimeout;

	[Attribute("1", UIWidgets.EditBox, "Attempts before a hold is unreachable")]
	protected int m_iMaximumRetries;

	[Attribute("1", UIWidgets.CheckBox, "If one node in a floor cluster is unreachable, fail the rest of that cluster")]
	protected bool m_bFailClusterOnUnreachable = true;

	[Attribute("0", UIWidgets.CheckBox, "Mark doors and windows and prefer those posts")]
	protected bool m_bClassifyOpenings;

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

	float GetHoldRadius()
	{
		return Math.Max(
			m_fHoldRadius,
			GetArrivalRadius()
		);
	}

	float GetReassignmentInterval()
	{
		return Math.Max(
			m_fReassignmentInterval,
			1.0
		);
	}

	float GetRotateIntervalMin()
	{
		return Math.Max(m_fRotateIntervalMin, 1.0);
	}

	float GetRotateIntervalMax()
	{
		return Math.Max(m_fRotateIntervalMax, GetRotateIntervalMin());
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

	bool GetFailClusterOnUnreachable()
	{
		return m_bFailClusterOnUnreachable;
	}

	bool GetClassifyOpenings()
	{
		return m_bClassifyOpenings;
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

		m_fBuildingSearchRadius = mode.KK_GetGarrisonSearchRadius();
		m_fHorizontalSpacing = mode.KK_GetHorizontalSpacing();
		m_fVerticalSpacing = mode.KK_GetVerticalSpacing();
		m_fDeduplicateDistance = mode.KK_GetDeduplicateDistance();
		m_fClusterRadius = mode.KK_GetClusterRadius();
		m_fArrivalRadius = mode.KK_GetGarrisonArrivalRadius();
		m_fHoldRadius = mode.KK_GetHoldRadius();
		m_fReassignmentInterval = mode.KK_GetReassignmentInterval();
		m_fRotateIntervalMin = mode.KK_GetRotateIntervalMin();
		m_fRotateIntervalMax = mode.KK_GetRotateIntervalMax();
		m_fMovementTimeout = mode.KK_GetGarrisonMovementTimeout();
		m_fStuckTimeout = mode.KK_GetGarrisonStuckTimeout();
		m_iMaximumRetries = mode.KK_GetGarrisonMaximumRetries();
		m_bFailClusterOnUnreachable = mode.KK_GetGarrisonFailCluster();
		m_bClassifyOpenings = mode.KK_GetClassifyOpenings();
	}

	override SCR_AIWaypointState CreateWaypointState(
		SCR_AIGroupUtilityComponent groupUtilityComp)
	{
		return new KK_GarrisonBuildingWaypointState(
			groupUtilityComp,
			this
		);
	}
}

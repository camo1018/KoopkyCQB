enum KK_EAttackPace
{
	ADVANCE,
	BOUND,
	TAKE_COVER
}

class KK_AttackWaypointClass : SCR_AIWaypointClass
{
}

class KK_AttackWaypoint : SCR_AIWaypoint
{
	[Attribute("0", UIWidgets.ComboBox, "How the squad advances", "", ParamEnumArray.FromEnum(KK_EAttackPace))]
	protected KK_EAttackPace m_ePace;

	[Attribute("20", UIWidgets.EditBox, "Distance of one bound (m)")]
	protected float m_fStepLength;

	[Attribute("2", UIWidgets.EditBox, "Seconds to hold after a step. 0 goes straight on.")]
	protected float m_fPause;

	[Attribute("2", UIWidgets.EditBox, "Seconds to shoot on contact before the normal attack takes the fight. 0 hands it over immediately.")]
	protected float m_fReturnFire;

	[Attribute("12", UIWidgets.EditBox, "Distance between pair lanes (m)")]
	protected float m_fLaneOffset;

	[Attribute("8", UIWidgets.EditBox, "Distance to the point that finishes the order (m)")]
	protected float m_fArrivalRadius;

	protected bool m_bReplaced;

	KK_EAttackPace GetPace()
	{
		if (m_ePace == KK_EAttackPace.BOUND)
			return KK_EAttackPace.BOUND;

		if (m_ePace == KK_EAttackPace.TAKE_COVER)
			return KK_EAttackPace.TAKE_COVER;

		return KK_EAttackPace.ADVANCE;
	}

	void SetPace(KK_EAttackPace pace)
	{
		if (pace != KK_EAttackPace.BOUND && pace != KK_EAttackPace.TAKE_COVER)
			pace = KK_EAttackPace.ADVANCE;

		m_ePace = pace;
	}

	float GetStepLength()
	{
		return Math.Max(m_fStepLength, 8);
	}

	float GetPause()
	{
		return Math.Max(m_fPause, 0);
	}

	float GetReturnFire()
	{
		return Math.Max(m_fReturnFire, 0);
	}

	float GetLaneOffset()
	{
		return Math.Max(m_fLaneOffset, 4);
	}

	float GetArrivalRadius()
	{
		return Math.Max(m_fArrivalRadius, 3);
	}

	void ApplyScenarioSettings()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return;

		m_fStepLength = mode.KK_GetAttackStepLength();
		m_fPause = mode.KK_GetAttackPause();
		m_fReturnFire = mode.KK_GetAttackContactDuration();
		m_fLaneOffset = mode.KK_GetAttackLaneOffset();
		m_fArrivalRadius = mode.KK_GetAttackArrivalRadius();
	}

	void MarkReplaced()
	{
		m_bReplaced = true;
	}

	bool IsReplaced()
	{
		return m_bReplaced;
	}

	override SCR_AIWaypointState CreateWaypointState(
		SCR_AIGroupUtilityComponent groupUtilityComp)
	{
		return new KK_AttackWaypointState(
			groupUtilityComp,
			this
		);
	}
}

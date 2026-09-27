modded class SCR_AICombatComponent
{
	override void UpdatePerceptionFactor(
		PerceptionComponent perceptionComp,
		SCR_AIThreatSystem threatSystem)
	{
		if (
			!perceptionComp ||
			!threatSystem ||
			!KK_PerceptionBoost.IsActiveSoldier(GetOwner()) ||
			!KK_PerceptionBoost.UseSharpCombat()
		)
		{
			super.UpdatePerceptionFactor(perceptionComp, threatSystem);
			return;
		}

		EAIThreatState threatState = threatSystem.GetState();
		float perceptionFactor = PERCEPTION_FACTOR_SAFE;

		switch (threatState)
		{
			case EAIThreatState.VIGILANT:
				perceptionFactor = PERCEPTION_FACTOR_VIGILANT;
				break;
			case EAIThreatState.ALERTED:
				perceptionFactor = PERCEPTION_FACTOR_ALERTED;
				break;
			case EAIThreatState.THREATENED:
				perceptionFactor = PERCEPTION_FACTOR_ALERTED;
				break;
		}

		perceptionFactor *= m_fEquipmentPerceptionFactor;
		perceptionFactor *= m_fPerceptionFactor;
		perceptionComp.SetPerceptionFactor(perceptionFactor);
	}
}

class KK_GarrisonHold
{
	protected static ref set<IEntity> s_Pinned = new set<IEntity>();
	protected static ref set<IEntity> s_Traveling = new set<IEntity>();

	static void SetPinned(IEntity soldier, bool pinned)
	{
		if (!soldier)
			return;

		if (pinned)
		{
			s_Traveling.RemoveItem(soldier);
			s_Pinned.Insert(soldier);
		}
		else
		{
			s_Pinned.RemoveItem(soldier);
		}

		ApplyFootLock(soldier, pinned);
	}

	static bool IsPinned(IEntity soldier)
	{
		return soldier && s_Pinned.Contains(soldier);
	}

	// On the way to a post, combat must not steer them off the route.
	// Speed stays free so they can still sprint. A foot lock would stop the run.
	static void SetTraveling(IEntity soldier, bool traveling)
	{
		if (!soldier)
			return;

		if (!traveling)
		{
			s_Traveling.RemoveItem(soldier);
			return;
		}

		if (s_Pinned.Contains(soldier))
			return;

		s_Traveling.Insert(soldier);
		CancelCombatMove(soldier);
	}

	static bool IsTraveling(IEntity soldier)
	{
		return soldier && s_Traveling.Contains(soldier);
	}

	// Attack movement ignores the post order. Zero walk speed leaves aiming alone.
	protected static void ApplyFootLock(IEntity soldier, bool locked)
	{
		CharacterControllerComponent controller =
			CharacterControllerComponent.Cast(
				soldier.FindComponent(CharacterControllerComponent)
			);

		if (controller)
		{
			if (locked)
				controller.OverrideMaxSpeed(0);
			else
				controller.OverrideMaxSpeed(1);
		}

		AICharacterMovementComponent movement =
			AICharacterMovementComponent.Cast(
				soldier.FindComponent(AICharacterMovementComponent)
			);

		if (movement && locked)
			movement.SetMovementTypeWanted(EMovementType.IDLE);
	}

	protected static void CancelCombatMove(IEntity soldier)
	{
		SCR_AIUtilityComponent utility =
			SCR_AIUtilityComponent.Cast(
				soldier.FindComponent(SCR_AIUtilityComponent)
			);

		if (utility && utility.m_CombatMoveState)
			utility.m_CombatMoveState.CancelRequest();
	}
}

modded class SCR_AIAttackBehavior
{
	override float CustomEvaluate()
	{
		float score = super.CustomEvaluate();

		// Stay on a garrison post. The attack still aims and fires.
		IEntity body;
		if (m_Utility)
			body = m_Utility.m_OwnerEntity;

		if (!KK_GarrisonHold.IsPinned(body) && m_Utility)
			body = m_Utility.GetOwner();

		if (KK_GarrisonHold.IsPinned(body))
		{
			// Speed stays locked by the post. Combat move stays on so the shot can fire.
			m_bUseCombatMove = true;
			KK_GarrisonHold.SetPinned(body, true);
		}
		else if (KK_GarrisonHold.IsTraveling(body))
		{
			// Keep the garrison route. Do not foot-lock, or the sprint stops.
			m_bUseCombatMove = false;
			KK_GarrisonHold.SetTraveling(body, true);
		}

		return score;
	}

	override void InitWaitTime(SCR_AIUtilityComponent utility)
	{
		if (
			utility &&
			KK_PerceptionBoost.IsActiveSoldier(utility.m_OwnerEntity) &&
			KK_PerceptionBoost.UseSharpCombat()
		)
		{
			m_fWaitTime.m_Value = 0;
			return;
		}

		super.InitWaitTime(utility);
	}
}

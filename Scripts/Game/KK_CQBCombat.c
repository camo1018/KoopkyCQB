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

modded class SCR_AIAttackBehavior
{
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

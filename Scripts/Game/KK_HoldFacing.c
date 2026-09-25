class KK_HoldFacing
{
	protected static const float DEFEND_RANGE = 60.0;
	protected static const float LOOK_DISTANCE = 8.0;

	static bool HasFacing(KK_InteriorTarget target)
	{
		return target && target.m_vFacing.Length() > 0.01;
	}

	static void Apply(
		notnull SCR_AIActivityBase activity,
		notnull AIAgent agent,
		notnull KK_InteriorTarget target,
		float priorityLevel)
	{
		if (!HasFacing(target))
			return;

		IEntity controlled = agent.GetControlledEntity();
		if (!controlled)
			return;

		vector lookAt =
			target.m_vPosition + (target.m_vFacing * LOOK_DISTANCE);

		SCR_AIMessage_Defend message = SCR_AIMessage_Defend.Create(
			lookAt,
			DEFEND_RANGE,
			false,
			priorityLevel,
			null,
			activity
		);

		message.SetReceiver(agent);

		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (soldier && soldier.m_UtilityComponent)
		{
			soldier.m_UtilityComponent.m_Mailbox.RequestBroadcast(
				message,
				agent
			);
			return;
		}

		CharacterControllerComponent controller =
			CharacterControllerComponent.Cast(
				controlled.FindComponent(CharacterControllerComponent)
			);

		if (!controller)
			return;

		vector flat = target.m_vFacing;
		flat[1] = 0;
		if (flat.Length() < 0.01)
			return;

		flat.Normalize();
		float yaw = flat.ToYaw();
		controller.SetHeadingAngle(yaw * Math.DEG2RAD, true);
	}

	static void Release(
		notnull SCR_AIActivityBase activity,
		notnull AIAgent agent)
	{
		SCR_AIMessage_Cancel message =
			SCR_AIMessage_Cancel.Create(activity);

		message.SetReceiver(agent);

		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (soldier && soldier.m_UtilityComponent)
		{
			soldier.m_UtilityComponent.m_Mailbox.RequestBroadcast(
				message,
				agent
			);
		}
	}
}

class KK_AuthoredRouteHelper
{
	static void BuildGoals(
		KK_BuildingInteriorPlan plan,
		vector soldierPosition,
		notnull KK_InteriorTarget target,
		notnull array<vector> outGoals)
	{
		outGoals.Clear();

		if (!plan)
			return;

		int fromId = plan.FindNearestAuthoredId(soldierPosition);
		int toId = target.m_iAuthoredId;

		if (toId < 0)
			toId = plan.FindNearestAuthoredId(target.m_vPosition, true);

		if (fromId < 0 || toId < 0)
			return;

		array<int> path = {};
		if (!plan.FindAuthoredPath(fromId, toId, path) || path.Count() < 2)
			return;

		for (int i = 1; i < path.Count(); i++)
		{
			vector world;
			if (!plan.GetAuthoredWorldPosition(path[i], world))
			{
				outGoals.Clear();
				return;
			}

			outGoals.Insert(world);
		}
	}

	static vector CurrentMoveGoal(
		notnull KK_InteriorTarget target,
		array<vector> routeGoals,
		int routeIndex)
	{
		if (
			routeGoals &&
			routeIndex >= 0 &&
			routeIndex < routeGoals.Count()
		)
		{
			return routeGoals[routeIndex];
		}

		return target.m_vPosition;
	}

	static bool IsOnFinalGoal(array<vector> routeGoals, int routeIndex)
	{
		if (!routeGoals || routeGoals.IsEmpty())
			return true;

		return routeIndex >= routeGoals.Count();
	}

	static bool AdvanceIfArrived(
		array<vector> routeGoals,
		inout int routeIndex,
		vector unitPosition,
		vector finalPosition,
		float arrivalRadius)
	{
		if (!routeGoals || routeGoals.IsEmpty() || routeIndex >= routeGoals.Count())
		{
			return vector.Distance(unitPosition, finalPosition) <= arrivalRadius;
		}

		vector goal = routeGoals[routeIndex];
		if (vector.Distance(unitPosition, goal) > arrivalRadius)
			return false;

		routeIndex++;
		return routeIndex >= routeGoals.Count() &&
			vector.Distance(unitPosition, finalPosition) <= arrivalRadius;
	}
}

class KK_HoldFacing
{
	static bool HasFacing(KK_InteriorTarget target)
	{
		return target && target.m_vFacing.Length() > 0.01;
	}

	// Turn in place. A defend order walks toward a point along the facing
	// and fights the hold leash.
	static void Apply(
		notnull AIAgent agent,
		notnull KK_InteriorTarget target)
	{
		if (!HasFacing(target))
			return;

		IEntity controlled = agent.GetControlledEntity();
		if (!controlled)
			return;

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
		controller.SetHeadingAngle(flat.ToYaw() * Math.DEG2RAD, true);
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

}

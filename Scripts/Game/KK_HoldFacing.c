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

		if (KK_GarrisonHold.SnapAimClaimed(controlled))
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
	// Chest height, so the rifle points into the space and not at the floor.
	static const float LOOK_HEIGHT = 1.5;

	// A route goal is the authored node itself. Farther than this, it is
	// not that node.
	static const float LOOK_NODE_MATCH = 2.5;

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

	// First node still ahead on this route that has not been cleared.
	// A graph corner with no clear point counts: that is the next space
	// he is walking into. A point already visited is skipped.
	static vector UnclearedLook(
		KK_BuildingInteriorPlan plan,
		KK_InteriorTarget target,
		array<vector> routeGoals,
		int routeIndex)
	{
		if (routeGoals)
		{
			for (int i = routeIndex; i < routeGoals.Count(); i++)
			{
				if (NodeCleared(plan, routeGoals[i]))
					continue;

				return routeGoals[i] + Vector(0, LOOK_HEIGHT, 0);
			}
		}

		if (!target)
			return vector.Zero;

		return target.m_vPosition + Vector(0, LOOK_HEIGHT, 0);
	}

	// The hold direction, a few metres out at chest height.
	static vector FacingLook(vector feet, notnull KK_InteriorTarget target)
	{
		vector flat = target.m_vFacing;
		flat[1] = 0;
		if (flat.Length() < 0.01)
			return vector.Zero;

		flat.Normalize();
		vector look = feet + flat * 8;
		look[1] = feet[1] + LOOK_HEIGHT;
		return look;
	}

	protected static bool NodeCleared(
		KK_BuildingInteriorPlan plan,
		vector worldPosition)
	{
		if (!plan)
			return false;

		int authoredId = plan.FindNearestAuthoredId(worldPosition);
		if (authoredId < 0)
			return false;

		vector authored;
		if (!plan.GetAuthoredWorldPosition(authoredId, authored))
			return false;

		if (vector.Distance(authored, worldPosition) > LOOK_NODE_MATCH)
			return false;

		KK_InteriorTarget node = plan.GetTargetByAuthoredId(authoredId);
		if (!node)
			return false;

		return node.IsFinished();
	}

}

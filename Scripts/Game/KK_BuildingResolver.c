class KK_PendingBuildingOrder
{
	BaseBuilding m_Building;
	bool m_bLocked;
	vector m_vOrderAim;
	bool m_bHasOrderAim;
}

class KK_BuildingResolver
{
	protected static vector s_QueryOrigin;
	protected static ref array<BaseBuilding> s_aCandidates = {};
	protected static ref array<float> s_aDistances = {};
	protected static ref map<IEntity, ref KK_PendingBuildingOrder> s_mPendingOrders =
		new map<IEntity, ref KK_PendingBuildingOrder>();

	static array<BaseBuilding> FindOccupiableBuildings(
		vector origin,
		float radius = 75.0)
	{
		s_QueryOrigin = origin;
		s_aCandidates.Clear();
		s_aDistances.Clear();

		array<BaseBuilding> results = {};

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return results;

		world.QueryEntitiesBySphere(
			origin,
			radius,
			OnEntityFound,
			null,
			EQueryEntitiesFlags.STATIC |
			EQueryEntitiesFlags.DYNAMIC |
			EQueryEntitiesFlags.WITH_OBJECT
		);

		SortCandidatesByDistance();

		foreach (BaseBuilding candidate : s_aCandidates)
		{
			results.Insert(candidate);
		}

		if (results.IsEmpty())
		{
			if (SCR_BaseGameMode.KK_LogEnabled())
				PrintFormat(
					"KK: No building found within %1 metres of %2",
					radius,
					origin
				);
		}

		return results;
	}

	// A hit piece uses its building when the order point is on that building.
	// A point inside a building uses that one. Open ground uses the nearest.
	static BaseBuilding ResolveOrderBuilding(
		IEntity hitEntity,
		vector position,
		out bool locked,
		float radius = 75.0)
	{
		locked = false;

		BaseBuilding aimed = ResolveBuildingRoot(hitEntity);
		if (aimed && BoundsContain(aimed, position, 2.0))
		{
			locked = true;
			LogOrderBuilding("aimed piece", aimed);
			return aimed;
		}

		array<BaseBuilding> buildings =
			FindOccupiableBuildings(position, radius);

		BaseBuilding containing =
			SelectContainingBuilding(buildings, position, 0);

		if (!containing)
		{
			containing = SelectContainingBuilding(
				buildings,
				position,
				1.0
			);
		}

		if (containing)
		{
			locked = true;
			LogOrderBuilding("position inside", containing);
			return containing;
		}

		if (buildings.IsEmpty())
			return null;

		LogOrderBuilding("nearest", buildings[0]);
		return buildings[0];
	}

	static vector ResolveOrderPosition(
		IEntity hitEntity,
		vector position,
		out BaseBuilding building,
		out bool locked,
		float radius = 75.0)
	{
		building = ResolveOrderBuilding(
			hitEntity,
			position,
			locked,
			radius
		);

		if (!building)
			return position;

		return SCR_EntityHelper.GetEntityCenterWorld(building);
	}

	// The activity can start while the command is still spawning the waypoint.
	static void SetPendingOrder(
		IEntity group,
		BaseBuilding building,
		bool locked,
		vector orderAim = "0 0 0",
		bool hasOrderAim = false)
	{
		if (!group)
			return;

		KK_PendingBuildingOrder pending = new KK_PendingBuildingOrder();
		pending.m_Building = building;
		pending.m_bLocked = locked && building;
		pending.m_vOrderAim = orderAim;
		pending.m_bHasOrderAim = hasOrderAim;
		s_mPendingOrders.Set(group, pending);
	}

	static bool PeekOrderAim(IEntity group, out vector orderAim)
	{
		orderAim = "0 0 0";

		if (!group || !s_mPendingOrders.Contains(group))
			return false;

		KK_PendingBuildingOrder pending = s_mPendingOrders.Get(group);
		if (!pending || !pending.m_bHasOrderAim)
			return false;

		orderAim = pending.m_vOrderAim;
		return true;
	}

	static bool TakePendingOrder(
		IEntity group,
		out BaseBuilding building,
		out bool locked)
	{
		building = null;
		locked = false;

		if (!group || !s_mPendingOrders.Contains(group))
			return false;

		KK_PendingBuildingOrder pending = s_mPendingOrders.Get(group);
		s_mPendingOrders.Remove(group);

		if (!pending)
			return false;

		building = pending.m_Building;
		locked = pending.m_bLocked && building;
		return true;
	}

	static void ClearPendingOrder(IEntity group)
	{
		if (!group)
			return;

		if (s_mPendingOrders.Contains(group))
			s_mPendingOrders.Remove(group);
	}

	static void FillOrderCandidates(
		notnull array<BaseBuilding> candidates,
		BaseBuilding lockedBuilding,
		bool locked,
		vector origin,
		float radius)
	{
		if (locked && lockedBuilding)
		{
			candidates.Insert(lockedBuilding);
			return;
		}

		array<BaseBuilding> found =
			FindOccupiableBuildings(origin, radius);

		foreach (BaseBuilding candidate : found)
		{
			candidates.Insert(candidate);
		}
	}

	protected static BaseBuilding SelectContainingBuilding(
		array<BaseBuilding> buildings,
		vector position,
		float pad)
	{
		BaseBuilding best;
		float bestVolume = -1;

		foreach (BaseBuilding building : buildings)
		{
			if (!building)
				continue;

			if (!BoundsContain(building, position, pad))
				continue;

			vector size = SCR_EntityHelper.GetEntitySize(building);
			float volume = size[0] * size[1] * size[2];

			if (best && volume >= bestVolume)
				continue;

			best = building;
			bestVolume = volume;
		}

		return best;
	}

	protected static bool BoundsContain(
		notnull BaseBuilding building,
		vector worldPosition,
		float pad)
	{
		vector mins;
		vector maxs;
		building.GetBounds(mins, maxs);

		vector local = building.CoordToLocal(worldPosition);

		return local[0] >= mins[0] - pad &&
			local[0] <= maxs[0] + pad &&
			local[1] >= mins[1] - pad &&
			local[1] <= maxs[1] + pad &&
			local[2] >= mins[2] - pad &&
			local[2] <= maxs[2] + pad;
	}

	protected static void LogOrderBuilding(
		string reason,
		BaseBuilding building)
	{
		if (!SCR_BaseGameMode.KK_LogEnabled())
			return;

		PrintFormat(
			"KK: Order building (%1) is %2",
			reason,
			building
		);
	}

	protected static bool OnEntityFound(IEntity entity)
	{
		if (!entity)
			return true;

		BaseBuilding building = ResolveBuildingRoot(entity);
		if (!building)
			return true;

		if (ContainsCandidate(building))
			return true;

		vector center =
			SCR_EntityHelper.GetEntityCenterWorld(building);

		float distance = vector.Distance(
			s_QueryOrigin,
			center
		);

		s_aCandidates.Insert(building);
		s_aDistances.Insert(distance);

		return true;
	}

	protected static bool ContainsCandidate(notnull BaseBuilding building)
	{
		foreach (BaseBuilding existing : s_aCandidates)
		{
			if (existing == building)
				return true;
		}

		return false;
	}

	protected static void SortCandidatesByDistance()
	{
		int count = s_aCandidates.Count();
		int i;

		while (i < count)
		{
			int j = i + 1;

			while (j < count)
			{
				if (s_aDistances[j] < s_aDistances[i])
				{
					float swapDistance = s_aDistances[i];
					s_aDistances[i] = s_aDistances[j];
					s_aDistances[j] = swapDistance;

					BaseBuilding swapBuilding = s_aCandidates[i];
					s_aCandidates[i] = s_aCandidates[j];
					s_aCandidates[j] = swapBuilding;
				}

				j++;
			}

			i++;
		}
	}

	static BaseBuilding ResolveBuildingRoot(IEntity entity)
	{
		IEntity candidate = entity;
		BaseBuilding highestBuilding;

		while (candidate)
		{
			BaseBuilding building = BaseBuilding.Cast(candidate);

			if (building)
			{
				vector size = SCR_EntityHelper.GetEntitySize(building);

				// Climb past windows, doors, and other narrow parts.
				if (size[0] >= 1.5 && size[2] >= 1.5)
					highestBuilding = building;
			}

			candidate = candidate.GetParent();
		}

		return highestBuilding;
	}
}

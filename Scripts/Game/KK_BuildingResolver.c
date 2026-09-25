class KK_BuildingResolver
{
	protected static vector s_QueryOrigin;
	protected static ref array<BaseBuilding> s_aCandidates = {};
	protected static ref array<float> s_aDistances = {};

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
			PrintFormat(
				"KK: No building found within %1 metres of %2",
				radius,
				origin
			);
		}

		return results;
	}

	static BaseBuilding FindNearestBuilding(
		vector origin,
		float radius = 75.0)
	{
		array<BaseBuilding> buildings =
			FindOccupiableBuildings(origin, radius);

		if (buildings.IsEmpty())
			return null;

		return buildings[0];
	}

	static vector FindNearestBuildingPosition(
		vector origin,
		float radius = 75.0)
	{
		BaseBuilding building = FindNearestBuilding(origin, radius);
		if (!building)
			return origin;

		vector center = SCR_EntityHelper.GetEntityCenterWorld(building);

		PrintFormat(
			"KK: Nearest building to %1 is %2 at %3",
			origin,
			building,
			center
		);

		return center;
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

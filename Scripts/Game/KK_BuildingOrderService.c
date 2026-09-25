class KK_BuildingOrderService
{
	protected static const ResourceName DEFEND_WAYPOINT_PREFAB =
		"{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";

	// Call on server authority only.
	static bool AssignGarrisonPrototype(notnull SCR_AIGroup group, vector position)
	{
		EntitySpawnParams spawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		spawnParams.Transform[3] = position;

		IEntity waypointEntity = GetGame().SpawnEntityPrefabEx(
			DEFEND_WAYPOINT_PREFAB,
			false,
			null,
			spawnParams
		);

		SCR_DefendWaypoint waypoint = SCR_DefendWaypoint.Cast(waypointEntity);
		if (!waypoint)
		{
			Print("KK: Failed to spawn defend waypoint", LogLevel.ERROR);
			return false;
		}

		waypoint.SetCompletionRadius(12.0);
		waypoint.SetFastInit(false);

		group.AddWaypointAt(waypoint, 0);

		PrintFormat(
			"KK: Assigned garrison prototype to %1 at %2",
			group,
			position
		);

		return true;
	}

	static bool AssignGarrison(notnull SCR_AIGroup group, vector position)
	{
		const ResourceName garrisonPrefab =
			"{6A6969C011223344}Prefabs/AI/Waypoints/KK_AIWaypoint_GarrisonBuilding.et";

		EntitySpawnParams spawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		spawnParams.Transform[3] = position;

		IEntity waypointEntity = GetGame().SpawnEntityPrefabEx(
			garrisonPrefab,
			false,
			null,
			spawnParams
		);

		KK_GarrisonBuildingWaypoint waypoint =
			KK_GarrisonBuildingWaypoint.Cast(waypointEntity);

		if (!waypoint)
		{
			Print("KK: Failed to spawn garrison waypoint", LogLevel.ERROR);

			if (waypointEntity)
				SCR_EntityHelper.DeleteEntityAndChildren(waypointEntity);

			return false;
		}

		waypoint.ApplyScenarioSettings();
		group.AddWaypointAt(waypoint, 0);

		PrintFormat(
			"KK: Garrison assigned to %1 at %2 after clear",
			group,
			position
		);

		return true;
	}
}
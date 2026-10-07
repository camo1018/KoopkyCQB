class KK_BuildingOrderService
{
	static bool AssignGarrison(
		notnull SCR_AIGroup group,
		vector position,
		BaseBuilding building = null)
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
			if (SCR_BaseGameMode.KK_LogEnabled())
				Print("KK: Failed to spawn garrison waypoint", LogLevel.ERROR);

			if (waypointEntity)
				SCR_EntityHelper.DeleteEntityAndChildren(waypointEntity);

			return false;
		}

		waypoint.ApplyScenarioSettings();
		if (building)
			waypoint.SetOrderBuilding(building, true);

		group.AddWaypointAt(waypoint, 0);

		if (SCR_BaseGameMode.KK_LogEnabled())
			PrintFormat(
				"KK: Garrison assigned to %1 at %2 after clear",
				group,
				position
			);

		return true;
	}
}
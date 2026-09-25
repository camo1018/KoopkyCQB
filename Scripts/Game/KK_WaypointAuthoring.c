enum KK_EWaypointAuthorAction
{
	LOCK_BUILDING,
	PLACE_WINDOW,
	PLACE_DOOR,
	PLACE_POST,
	PLACE_ROUTE,
	TOGGLE_DEBUG,
	TOGGLE_FLOOR_0,
	TOGGLE_FLOOR_1,
	TOGGLE_FLOOR_2,
	TOGGLE_FLOOR_3,
	TOGGLE_FLOOR_4,
	TOGGLE_FLOOR_5,
	TOGGLE_FLOOR_6,
	TOGGLE_FLOOR_7,
	ENSURE_CACHE
}

class KK_WaypointAuthoring
{
	protected static BaseBuilding s_LockedBuilding;
	protected static string s_sLockedPrefab;
	protected static int s_iLastWaypointId = -1;
	protected static bool s_bDebugDraw;
	protected static ref array<ref Shape> s_aDebugShapes = {};
	protected static bool s_bDebugTicking;

	static BaseBuilding GetLockedBuilding()
	{
		return s_LockedBuilding;
	}

	static string GetLockedPrefab()
	{
		return s_sLockedPrefab;
	}

	static int GetLastWaypointId()
	{
		return s_iLastWaypointId;
	}

	static bool IsDebugDrawEnabled()
	{
		return s_bDebugDraw;
	}

	static IEntity GetLocalPlayerEntity()
	{
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return null;

		return controller.GetControlledEntity();
	}

	static BaseBuilding ResolveBuildingUnderPlayer()
	{
		IEntity player = GetLocalPlayerEntity();
		if (!player)
			return null;

		BaseBuilding fromParent =
			KK_BuildingResolver.ResolveBuildingRoot(player);

		if (fromParent)
			return fromParent;

		return KK_BuildingResolver.FindNearestBuilding(
			player.GetOrigin(),
			8.0
		);
	}

	static bool LockBuildingUnderPlayer()
	{
		BaseBuilding building = ResolveBuildingUnderPlayer();
		if (!building)
		{
			Print("KK: No building underfoot to lock", LogLevel.WARNING);
			return false;
		}

		string prefabName =
			KK_BuildingWaypointLibrary.ResolvePrefabName(building);

		if (prefabName.IsEmpty())
		{
			Print("KK: Building has no prefab name", LogLevel.WARNING);
			return false;
		}

		s_LockedBuilding = building;
		s_sLockedPrefab = prefabName;
		s_iLastWaypointId = -1;

		PrintFormat(
			"KK: Locked building prefab %1",
			prefabName
		);

		RefreshDebugDraw();
		return true;
	}

	static bool PlaceWaypoint(KK_EBuildingWaypointType type)
	{
		if (!s_LockedBuilding)
		{
			if (!LockBuildingUnderPlayer())
				return false;
		}

		IEntity player = GetLocalPlayerEntity();
		if (!player || !s_LockedBuilding)
			return false;

		vector transform[4];
		player.GetWorldTransform(transform);
		vector facing = transform[2];
		facing[1] = 0;
		if (facing.Length() < 0.01)
			facing = Vector(0, 0, 1);
		else
			facing.Normalize();

		int linkedFrom = s_iLastWaypointId;

		KK_AuthoredBuildingWaypoint waypoint =
			KK_BuildingWaypointLibrary.PlaceWaypoint(
				s_LockedBuilding,
				type,
				player.GetOrigin(),
				facing,
				linkedFrom
			);

		if (!waypoint)
			return false;

		s_iLastWaypointId = waypoint.m_iId;

		PrintFormat(
			"KK: Placed %1 waypoint id=%2 linkedFrom=%3",
			type,
			waypoint.m_iId,
			linkedFrom
		);

		RefreshDebugDraw();
		return true;
	}

	static bool ToggleDebugDraw()
	{
		s_bDebugDraw = !s_bDebugDraw;
		RefreshDebugDraw();
		PrintFormat("KK: Waypoint debug draw=%1", s_bDebugDraw);
		return true;
	}

	static KK_PrefabWaypointSet GetLockedSet()
	{
		if (s_sLockedPrefab.IsEmpty())
			return null;

		return KK_BuildingWaypointLibrary.GetOrCreateSet(s_sLockedPrefab);
	}

	static bool EnsureSampleCache()
	{
		if (!s_LockedBuilding)
		{
			if (!LockBuildingUnderPlayer())
				return false;
		}

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (!prefabSet)
			return false;

		if (prefabSet.m_bHasSampleCache && !prefabSet.m_aSamples.IsEmpty())
			return true;

		SCR_AIGroup group = FindLocalPlayerGroup();
		if (!group)
		{
			Print(
				"KK: Need a local AI group to build the sample cache",
				LogLevel.WARNING
			);
			return false;
		}

		float horizontal = 2.5;
		float vertical = 1.5;
		float dedup = 1.25;
		float cluster = 4.0;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (mode)
		{
			horizontal = mode.KK_GetHorizontalSpacing();
			vertical = mode.KK_GetVerticalSpacing();
			dedup = mode.KK_GetDeduplicateDistance();
			cluster = mode.KK_GetClusterRadius();
		}

		KK_BuildingInteriorPlan plan = new KK_BuildingInteriorPlan();
		AIPathfindingComponent pathfinding =
			AIPathfindingComponent.Cast(
				group.FindComponent(AIPathfindingComponent)
			);

		if (!pathfinding)
			return false;

		plan.EnsureNavmeshLoaded(pathfinding, s_LockedBuilding);
		bool ok = plan.Generate(
			group,
			s_LockedBuilding,
			horizontal,
			vertical,
			dedup,
			cluster,
			false,
			true,
			false
		);

		PrintFormat("KK: Sample cache ensure result=%1", ok);
		RefreshDebugDraw();
		return ok;
	}

	static bool ToggleFloor(int floorIndex)
	{
		if (!EnsureSampleCache())
			return false;

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (!prefabSet)
			return false;

		array<float> bands = {};
		KK_BuildingWaypointLibrary.CollectFloorBands(prefabSet, bands);

		if (floorIndex < 0 || floorIndex >= bands.Count())
		{
			PrintFormat(
				"KK: Floor %1 not present (bands=%2)",
				floorIndex,
				bands.Count()
			);
			return false;
		}

		KK_BuildingWaypointLibrary.ToggleForbiddenFloorIndex(prefabSet, floorIndex);
		bool forbidden =
			KK_BuildingWaypointLibrary.IsForbiddenFloorIndex(prefabSet, floorIndex);

		PrintFormat(
			"KK: Floor %1 localY=%2 forbidden=%3",
			floorIndex,
			bands[floorIndex],
			forbidden
		);

		RefreshDebugDraw();
		return true;
	}

	static string FloorLabel(int floorIndex)
	{
		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (!prefabSet)
			return string.Format("Floor %1", floorIndex + 1);

		array<float> bands = {};
		KK_BuildingWaypointLibrary.CollectFloorBands(prefabSet, bands);

		if (floorIndex < 0 || floorIndex >= bands.Count())
			return string.Format("Floor %1 (none)", floorIndex + 1);

		bool forbidden =
			KK_BuildingWaypointLibrary.IsForbiddenFloorIndex(prefabSet, floorIndex);

		if (forbidden)
		{
			return string.Format(
				"Floor %1 forbid",
				floorIndex + 1
			);
		}

		return string.Format("Floor %1 allow", floorIndex + 1);
	}

	static void RefreshDebugDraw()
	{
		ClearDebugShapes();

		if (!s_bDebugDraw || !s_LockedBuilding)
			return;

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (!prefabSet)
			return;

		ShapeFlags shapeFlags =
			ShapeFlags.NOZBUFFER |
			ShapeFlags.TRANSP |
			ShapeFlags.NOOUTLINE |
			ShapeFlags.VISIBLE;

		foreach (KK_AuthoredBuildingWaypoint waypoint : prefabSet.m_aWaypoints)
		{
			if (!waypoint)
				continue;

			vector world =
				s_LockedBuilding.CoordToParent(waypoint.m_vLocalPosition);
			vector marker = world + Vector(0, 0.35, 0);
			int color = ColorForType(waypoint.m_eType);

			s_aDebugShapes.Insert(
				Shape.CreateSphere(color, shapeFlags, marker, 0.28)
			);

			vector facing =
				KK_BuildingWaypointLibrary.LocalFacingToWorld(
					s_LockedBuilding,
					waypoint.m_vLocalFacing
				);

			if (facing.Length() > 0.01)
			{
				s_aDebugShapes.Insert(
					Shape.CreateArrow(
						marker,
						marker + (facing * 1.4),
						0.08,
						color,
						ShapeFlags.NOZBUFFER | ShapeFlags.VISIBLE
					)
				);
			}

			foreach (int linkId : waypoint.m_aLinks)
			{
				if (linkId < waypoint.m_iId)
					continue;

				KK_AuthoredBuildingWaypoint other =
					KK_BuildingWaypointLibrary.FindWaypoint(prefabSet, linkId);

				if (!other)
					continue;

				vector otherWorld =
					s_LockedBuilding.CoordToParent(other.m_vLocalPosition);

				s_aDebugShapes.Insert(
					Shape.CreateArrow(
						marker,
						otherWorld + Vector(0, 0.35, 0),
						0.1,
						0xFFFFFF88,
						ShapeFlags.NOZBUFFER | ShapeFlags.VISIBLE
					)
				);
			}
		}

		EnsureDebugTick();
	}

	protected static int ColorForType(KK_EBuildingWaypointType type)
	{
		if (type == KK_EBuildingWaypointType.WINDOW)
			return 0xFF44EE66;

		if (type == KK_EBuildingWaypointType.DOOR)
			return 0xFFFF8800;

		if (type == KK_EBuildingWaypointType.ROUTE)
			return 0xFFCC66FF;

		return 0xFF00DDFF;
	}

	protected static void ClearDebugShapes()
	{
		s_aDebugShapes.Clear();
	}

	protected static void EnsureDebugTick()
	{
		if (s_bDebugTicking)
			return;

		s_bDebugTicking = true;
		GetGame().GetCallqueue().CallLater(DebugTick, 500, true);
	}

	protected static void DebugTick()
	{
		if (!s_bDebugDraw)
		{
			ClearDebugShapes();
			GetGame().GetCallqueue().Remove(DebugTick);
			s_bDebugTicking = false;
			return;
		}

		RefreshDebugDraw();
	}

	protected static SCR_AIGroup FindLocalPlayerGroup()
	{
		SCR_PlayerControllerGroupComponent groupController =
			SCR_PlayerControllerGroupComponent.GetLocalPlayerControllerGroupComponent();

		if (!groupController)
			return null;

		SCR_GroupsManagerComponent groupsManager =
			SCR_GroupsManagerComponent.GetInstance();

		if (!groupsManager)
			return null;

		return groupsManager.FindGroup(groupController.GetGroupID());
	}
}

[BaseContainerProps()]
class KK_WaypointAuthorCommand : SCR_BaseGroupCommand
{
	[Attribute("0", UIWidgets.ComboBox, "Authoring action", "", ParamEnumArray.FromEnum(KK_EWaypointAuthorAction))]
	protected KK_EWaypointAuthorAction m_eAction;

	override bool CanBePerformed(notnull SCR_ChimeraCharacter user)
	{
		return user != null;
	}

	override bool CanRoleShow()
	{
		return true;
	}

	override bool Execute(
		IEntity cursorTarget,
		IEntity groupEnt,
		vector targetPosition,
		int playerID,
		bool isClient)
	{
		if (isClient)
			return true;

		switch (m_eAction)
		{
			case KK_EWaypointAuthorAction.LOCK_BUILDING:
				return KK_WaypointAuthoring.LockBuildingUnderPlayer();

			case KK_EWaypointAuthorAction.PLACE_WINDOW:
				return KK_WaypointAuthoring.PlaceWaypoint(
					KK_EBuildingWaypointType.WINDOW
				);

			case KK_EWaypointAuthorAction.PLACE_DOOR:
				return KK_WaypointAuthoring.PlaceWaypoint(
					KK_EBuildingWaypointType.DOOR
				);

			case KK_EWaypointAuthorAction.PLACE_POST:
				return KK_WaypointAuthoring.PlaceWaypoint(
					KK_EBuildingWaypointType.POST
				);

			case KK_EWaypointAuthorAction.PLACE_ROUTE:
				return KK_WaypointAuthoring.PlaceWaypoint(
					KK_EBuildingWaypointType.ROUTE
				);

			case KK_EWaypointAuthorAction.TOGGLE_DEBUG:
				return KK_WaypointAuthoring.ToggleDebugDraw();

			case KK_EWaypointAuthorAction.ENSURE_CACHE:
				return KK_WaypointAuthoring.EnsureSampleCache();

			case KK_EWaypointAuthorAction.TOGGLE_FLOOR_0:
				return KK_WaypointAuthoring.ToggleFloor(0);

			case KK_EWaypointAuthorAction.TOGGLE_FLOOR_1:
				return KK_WaypointAuthoring.ToggleFloor(1);

			case KK_EWaypointAuthorAction.TOGGLE_FLOOR_2:
				return KK_WaypointAuthoring.ToggleFloor(2);

			case KK_EWaypointAuthorAction.TOGGLE_FLOOR_3:
				return KK_WaypointAuthoring.ToggleFloor(3);

			case KK_EWaypointAuthorAction.TOGGLE_FLOOR_4:
				return KK_WaypointAuthoring.ToggleFloor(4);

			case KK_EWaypointAuthorAction.TOGGLE_FLOOR_5:
				return KK_WaypointAuthoring.ToggleFloor(5);

			case KK_EWaypointAuthorAction.TOGGLE_FLOOR_6:
				return KK_WaypointAuthoring.ToggleFloor(6);

			case KK_EWaypointAuthorAction.TOGGLE_FLOOR_7:
				return KK_WaypointAuthoring.ToggleFloor(7);
		}

		return false;
	}
}

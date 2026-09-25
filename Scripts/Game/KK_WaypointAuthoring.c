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
	TOGGLE_ROOF,
	DELETE_WAYPOINT,
	ADD_NODE,
	DELETE_NODE,
	DELETE_CLUSTER,
	UNDO
}

class KK_WaypointAuthoring
{
	protected static BaseBuilding s_LockedBuilding;
	protected static string s_sLockedPrefab;
	protected static int s_iLastWaypointId = -1;
	protected static bool s_bDebugDraw = true;
	protected static ref array<ref Shape> s_aDebugShapes = {};
	protected static bool s_bDebugTicking;
	protected static bool s_bSamplePending;
	protected static bool s_bSampleAnnounce;
	protected static int s_iSampleAttempts;
	protected static const int SAMPLE_ATTEMPTS = 6;

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

	static bool Enabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		return mode && mode.KK_GetWaypointAuthoring();
	}

	static void Notify(string text)
	{
		Print("KK: " + text);
		SCR_HintManagerComponent.ShowCustomHint(text, "Waypoints", 4);
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

	static BaseBuilding ResolveBuildingInSight()
	{
		IEntity player = GetLocalPlayerEntity();
		BaseWorld world = GetGame().GetWorld();
		if (!player || !world)
			return null;

		vector start;
		vector direction;
		if (!LookDirection(player, start, direction))
			return null;

		TraceParam trace = new TraceParam();
		trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		trace.Start = start;
		trace.End = start + (direction * 40.0);
		s_LookIgnore = player;

		float fraction = world.TraceMove(trace, FilterLookTrace);
		s_LookIgnore = null;

		if (fraction >= 1.0 || !trace.TraceEnt)
			return null;

		return KK_BuildingResolver.ResolveBuildingRoot(trace.TraceEnt);
	}

	static bool LockBuildingUnderPlayer()
	{
		BaseBuilding building = ResolveBuildingInSight();
		if (!building)
		{
			Notify("Not looking at a building");
			return false;
		}

		string prefabName =
			KK_BuildingWaypointLibrary.ResolvePrefabName(building);

		if (prefabName.IsEmpty())
		{
			Notify("This building has no prefab name");
			return false;
		}

		s_LockedBuilding = building;
		s_sLockedPrefab = prefabName;
		s_iLastWaypointId = -1;

		KK_PrefabWaypointSet existing =
			KK_BuildingWaypointLibrary.FindSet(prefabName);
		bool hadSamples = existing && existing.m_bHasSampleCache;
		if (!hadSamples)
			KK_AuthorEditHistory.Remember(prefabName, s_iLastWaypointId);

		bool samples = EnsureSampleCache(false);
		if (s_bSamplePending)
			return true;

		if (!hadSamples && !samples)
			KK_AuthorEditHistory.Discard();
		if (samples)
			Notify("Locked this building");
		else
			Notify("Locked this building, but samples failed");

		RefreshDebugDraw();
		return true;
	}

	static bool DeleteWaypointHere()
	{
		BaseBuilding building = s_LockedBuilding;
		if (!building)
			building = ResolveBuildingUnderPlayer();

		IEntity player = GetLocalPlayerEntity();
		if (!building || !player)
		{
			Notify("Lock a building first");
			return false;
		}

		string prefabName = KK_BuildingWaypointLibrary.ResolvePrefabName(building);
		KK_AuthorEditHistory.Remember(prefabName, s_iLastWaypointId);

		KK_EBuildingWaypointType type;
		int removedId;
		bool deleted = KK_BuildingWaypointLibrary.DeleteNearestWaypoint(
			building,
			player.GetOrigin(),
			2.0,
			type,
			removedId
		);

		if (!deleted)
		{
			KK_AuthorEditHistory.Discard();
			Notify("No waypoint within 2 m");
			return false;
		}

		if (s_iLastWaypointId == removedId)
			s_iLastWaypointId = -1;

		Notify(string.Format(
			"Deleted %1, id %2",
			TypeLabel(type),
			removedId
		));
		RefreshDebugDraw();
		return true;
	}

	static bool AddNodeHere()
	{
		if (!s_LockedBuilding && !LockBuildingUnderPlayer())
			return false;

		IEntity player = GetLocalPlayerEntity();
		if (!player || !s_LockedBuilding)
			return false;

		float horizontal;
		float vertical;
		float dedup;
		float cluster;
		CurrentInteriorSettings(horizontal, vertical, dedup, cluster);

		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);

		bool added = KK_BuildingWaypointLibrary.AddSample(
			s_LockedBuilding,
			player.GetOrigin(),
			horizontal,
			vertical,
			dedup
		);

		if (!added)
		{
			KK_AuthorEditHistory.Discard();
			Notify("Could not add a node");
			return false;
		}

		Notify("Added node");
		RefreshDebugDraw();
		return true;
	}

	static bool DeleteNodeInSight()
	{
		if (!s_LockedBuilding)
		{
			Notify("Lock a building first");
			return false;
		}

		int sampleIndex = SampleInSight();
		if (sampleIndex < 0)
		{
			Notify("Not looking at a node");
			return false;
		}

		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);

		bool removed = KK_BuildingWaypointLibrary.RemoveSampleAt(
			s_LockedBuilding,
			sampleIndex
		);

		if (!removed)
		{
			KK_AuthorEditHistory.Discard();
			Notify("Not looking at a node");
			return false;
		}

		Notify("Deleted node");
		RefreshDebugDraw();
		return true;
	}

	static bool DeleteClusterInSight()
	{
		if (!s_LockedBuilding)
		{
			Notify("Lock a building first");
			return false;
		}

		int sampleIndex = SampleInSight();
		if (sampleIndex < 0)
		{
			Notify("Not looking at a node");
			return false;
		}

		float horizontal;
		float vertical;
		float dedup;
		float cluster;
		CurrentInteriorSettings(horizontal, vertical, dedup, cluster);

		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);

		int removed = KK_BuildingWaypointLibrary.RemoveSampleCluster(
			s_LockedBuilding,
			sampleIndex,
			cluster
		);

		if (removed <= 0)
		{
			KK_AuthorEditHistory.Discard();
			Notify("Not looking at a node");
			return false;
		}

		Notify(string.Format("Deleted cluster, %1 nodes", removed));
		RefreshDebugDraw();
		return true;
	}

	static bool UndoLastEdit()
	{
		int lastWaypointId;
		string prefabName;
		bool undone = KK_AuthorEditHistory.Undo(prefabName, lastWaypointId);
		if (!undone)
		{
			Notify("Nothing to undo");
			return false;
		}

		if (prefabName == s_sLockedPrefab)
			s_iLastWaypointId = lastWaypointId;

		Notify("Undid last edit");
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

		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);

		KK_AuthoredBuildingWaypoint waypoint =
			KK_BuildingWaypointLibrary.PlaceWaypoint(
				s_LockedBuilding,
				type,
				player.GetOrigin(),
				facing,
				linkedFrom
			);

		if (!waypoint)
		{
			KK_AuthorEditHistory.Discard();
			return false;
		}

		s_iLastWaypointId = waypoint.m_iId;

		Notify(string.Format(
			"Placed %1, id %2",
			TypeLabel(type),
			waypoint.m_iId
		));

		RefreshDebugDraw();
		return true;
	}

	static bool ToggleDebugDraw()
	{
		s_bDebugDraw = !s_bDebugDraw;
		RefreshDebugDraw();
		if (s_bDebugDraw)
			Notify("Waypoint markers on");
		else
			Notify("Waypoint markers off");
		return true;
	}

	static KK_PrefabWaypointSet GetLockedSet()
	{
		if (s_sLockedPrefab.IsEmpty())
			return null;

		return KK_BuildingWaypointLibrary.GetOrCreateSet(s_sLockedPrefab);
	}

	static bool EnsureSampleCache(bool announce)
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
			Notify("Need a squad with you to build the floor list");
			return false;
		}

		AIPathfindingComponent pathfinding =
			AIPathfindingComponent.Cast(
				group.FindComponent(AIPathfindingComponent)
			);

		if (!pathfinding)
			return false;

		s_bSampleAnnounce = announce;
		s_iSampleAttempts = 0;
		GetGame().GetCallqueue().Remove(RetryBuildSampleCache);
		return BuildSampleCache();
	}

	protected static bool BuildSampleCache()
	{
		if (!s_LockedBuilding)
		{
			s_bSamplePending = false;
			return false;
		}

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (prefabSet && prefabSet.m_bHasSampleCache && !prefabSet.m_aSamples.IsEmpty())
		{
			s_bSamplePending = false;
			return true;
		}

		SCR_AIGroup group = FindLocalPlayerGroup();
		AIPathfindingComponent pathfinding;
		if (group)
		{
			pathfinding = AIPathfindingComponent.Cast(
				group.FindComponent(AIPathfindingComponent)
			);
		}

		if (!group || !pathfinding)
		{
			s_bSamplePending = false;
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
		bool tilesLoaded = plan.EnsureNavmeshLoaded(
			pathfinding,
			s_LockedBuilding
		);

		if (!tilesLoaded && s_iSampleAttempts < SAMPLE_ATTEMPTS)
		{
			s_iSampleAttempts++;
			s_bSamplePending = true;
			GetGame().GetCallqueue().CallLater(RetryBuildSampleCache, 400, false);
			return false;
		}

		s_bSamplePending = false;
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

		RefreshDebugDraw();
		return ok;
	}

	protected static void RetryBuildSampleCache()
	{
		bool ok = BuildSampleCache();
		if (s_bSamplePending)
			return;

		if (!ok)
			KK_AuthorEditHistory.Discard();

		if (s_bSampleAnnounce)
		{
			if (ok)
				Notify("Floor list ready");
			else
				Notify("Could not build the floor list");
		}
		else if (ok)
		{
			Notify("Locked this building");
		}
		else
		{
			Notify("Locked this building, but samples failed");
		}
	}

	static bool ToggleRoof()
	{
		if (!s_LockedBuilding && !LockBuildingUnderPlayer())
			return false;

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (!prefabSet)
			return false;

		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);
		KK_BuildingWaypointLibrary.ToggleDropRoof(prefabSet);
		Notify(RoofLabel());
		return true;
	}

	static string RoofLabel()
	{
		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (prefabSet && prefabSet.m_bDropRoof)
			return "Roof forbid";

		return "Roof allow";
	}

	static bool ToggleFloor(int floorIndex)
	{
		if (!s_LockedBuilding && !LockBuildingUnderPlayer())
			return false;

		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);

		if (!EnsureSampleCache(true))
		{
			if (!s_bSamplePending)
				KK_AuthorEditHistory.Discard();
			return false;
		}

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (!prefabSet)
		{
			KK_AuthorEditHistory.Discard();
			return false;
		}

		array<float> bands = {};
		KK_BuildingWaypointLibrary.CollectFloorBands(prefabSet, bands);

		if (floorIndex < 0 || floorIndex >= bands.Count())
		{
			KK_AuthorEditHistory.Discard();
			Notify(string.Format("No floor %1 on this building", floorIndex + 1));
			return false;
		}

		KK_BuildingWaypointLibrary.ToggleForbiddenFloorIndex(prefabSet, floorIndex);
		bool forbidden =
			KK_BuildingWaypointLibrary.IsForbiddenFloorIndex(prefabSet, floorIndex);

		Notify(FloorLabel(floorIndex));

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

		foreach (KK_CachedInteriorSample sample : prefabSet.m_aSamples)
		{
			if (!sample)
				continue;

			vector sampleWorld =
				s_LockedBuilding.CoordToParent(sample.m_vLocalPosition);

			s_aDebugShapes.Insert(
				Shape.CreateSphere(
					0xFF88AACC,
					shapeFlags,
					sampleWorld + Vector(0, 0.2, 0),
					0.12
				)
			);
		}

		EnsureDebugTick();
	}

	protected static void CurrentInteriorSettings(
		out float horizontal,
		out float vertical,
		out float dedup,
		out float cluster)
	{
		horizontal = 2.5;
		vertical = 1.5;
		dedup = 1.25;
		cluster = 4.0;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return;

		horizontal = mode.KK_GetHorizontalSpacing();
		vertical = mode.KK_GetVerticalSpacing();
		dedup = mode.KK_GetDeduplicateDistance();
		cluster = mode.KK_GetClusterRadius();
	}

	protected static int SampleInSight()
	{
		IEntity player = GetLocalPlayerEntity();
		if (!player || !s_LockedBuilding)
			return -1;

		vector start;
		vector direction;
		if (!LookDirection(player, start, direction))
			return -1;

		return KK_BuildingWaypointLibrary.FindSampleAlongRay(
			s_LockedBuilding,
			start,
			direction,
			40.0,
			1.25
		);
	}

	protected static IEntity s_LookIgnore;

	protected static bool LookDirection(
		notnull IEntity player,
		out vector start,
		out vector direction)
	{
		CameraManager cameraManager = GetGame().GetCameraManager();
		if (cameraManager)
		{
			CameraBase camera = cameraManager.CurrentCamera();
			if (camera)
			{
				vector cameraTransform[4];
				camera.GetWorldTransform(cameraTransform);
				start = cameraTransform[3];
				direction = cameraTransform[2];
				if (direction.Length() > 0.01)
				{
					direction.Normalize();
					return true;
				}
			}
		}

		vector transform[4];
		player.GetWorldTransform(transform);
		start = player.GetOrigin() + Vector(0, 1.6, 0);
		direction = transform[2];
		direction[1] = 0;
		if (direction.Length() < 0.01)
			return false;

		direction.Normalize();
		return true;
	}

	protected static bool FilterLookTrace(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		if (!entity || entity == s_LookIgnore)
			return false;

		IEntity cursor = entity;
		while (cursor)
		{
			if (cursor == s_LookIgnore)
				return false;

			cursor = cursor.GetParent();
		}

		return true;
	}

	static string TypeLabel(KK_EBuildingWaypointType type)
	{
		if (type == KK_EBuildingWaypointType.WINDOW)
			return "window";

		if (type == KK_EBuildingWaypointType.DOOR)
			return "door";

		if (type == KK_EBuildingWaypointType.ROUTE)
			return "route";

		return "post";
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

class KK_AuthorSnapshot
{
	string m_sPrefabName;
	int m_iLastWaypointId;
	bool m_bMissing;
	ref KK_PrefabWaypointSet m_Set;

	static KK_AuthorSnapshot Capture(string prefabName, int lastWaypointId)
	{
		KK_AuthorSnapshot step = new KK_AuthorSnapshot();
		step.m_sPrefabName = prefabName;
		step.m_iLastWaypointId = lastWaypointId;

		KK_PrefabWaypointSet live = KK_BuildingWaypointLibrary.FindSet(prefabName);
		if (!live)
		{
			step.m_bMissing = true;
			return step;
		}

		KK_PrefabWaypointSet copy = new KK_PrefabWaypointSet();
		copy.m_sPrefabName = live.m_sPrefabName;
		copy.m_fScaleX = live.m_fScaleX;
		copy.m_fScaleY = live.m_fScaleY;
		copy.m_fScaleZ = live.m_fScaleZ;
		copy.m_fHorizontalSpacing = live.m_fHorizontalSpacing;
		copy.m_fVerticalSpacing = live.m_fVerticalSpacing;
		copy.m_fDeduplicateDistance = live.m_fDeduplicateDistance;
		copy.m_bHasSampleCache = live.m_bHasSampleCache;
		copy.m_iNextWaypointId = live.m_iNextWaypointId;
		copy.m_bDropRoof = live.m_bDropRoof;

		foreach (float forbiddenY : live.m_aForbiddenFloorYs)
			copy.m_aForbiddenFloorYs.Insert(forbiddenY);

		foreach (KK_AuthoredBuildingWaypoint waypoint : live.m_aWaypoints)
		{
			if (!waypoint)
				continue;

			KK_AuthoredBuildingWaypoint cloned = new KK_AuthoredBuildingWaypoint();
			cloned.m_iId = waypoint.m_iId;
			cloned.m_eType = waypoint.m_eType;
			cloned.m_vLocalPosition = waypoint.m_vLocalPosition;
			cloned.m_vLocalFacing = waypoint.m_vLocalFacing;

			foreach (int linkId : waypoint.m_aLinks)
				cloned.m_aLinks.Insert(linkId);

			copy.m_aWaypoints.Insert(cloned);
		}

		foreach (KK_CachedInteriorSample sample : live.m_aSamples)
		{
			if (!sample)
				continue;

			KK_CachedInteriorSample cloned = new KK_CachedInteriorSample();
			cloned.m_vLocalPosition = sample.m_vLocalPosition;
			cloned.m_eOpening = sample.m_eOpening;
			cloned.m_vLocalFacing = sample.m_vLocalFacing;
			cloned.m_iOpeningHeading = sample.m_iOpeningHeading;
			copy.m_aSamples.Insert(cloned);
		}

		step.m_Set = copy;
		return step;
	}

	void Restore()
	{
		if (m_bMissing)
			KK_BuildingWaypointLibrary.ReplacePrefabSet(m_sPrefabName, null);
		else
			KK_BuildingWaypointLibrary.ReplacePrefabSet(m_sPrefabName, m_Set);
	}
}

class KK_AuthorEditHistory
{
	protected static ref array<ref KK_AuthorSnapshot> s_aSteps = {};
	protected static const int MAX_STEPS = 20;

	static void Remember(string prefabName, int lastWaypointId)
	{
		if (!s_aSteps)
			s_aSteps = new array<ref KK_AuthorSnapshot>();

		s_aSteps.Insert(KK_AuthorSnapshot.Capture(prefabName, lastWaypointId));

		while (s_aSteps.Count() > MAX_STEPS)
			s_aSteps.Remove(0);
	}

	static void Discard()
	{
		if (!s_aSteps || s_aSteps.IsEmpty())
			return;

		s_aSteps.Remove(s_aSteps.Count() - 1);
	}

	static bool Undo(out string prefabName, out int lastWaypointId)
	{
		prefabName = string.Empty;
		lastWaypointId = -1;

		if (!s_aSteps || s_aSteps.IsEmpty())
			return false;

		int last = s_aSteps.Count() - 1;
		KK_AuthorSnapshot step = s_aSteps[last];
		s_aSteps.Remove(last);

		if (!step)
			return false;

		prefabName = step.m_sPrefabName;
		lastWaypointId = step.m_iLastWaypointId;
		step.Restore();
		return true;
	}
}

[BaseContainerProps()]
class KK_WaypointAuthorCommand : SCR_BaseGroupCommand
{
	[Attribute("0", UIWidgets.ComboBox, "Authoring action", "", ParamEnumArray.FromEnum(KK_EWaypointAuthorAction))]
	protected KK_EWaypointAuthorAction m_eAction;

	override bool CanBeShown()
	{
		if (!KK_WaypointAuthoring.Enabled())
			return false;

		return super.CanBeShown();
	}

	override string GetCommandDisplayName()
	{
		if (m_eAction == KK_EWaypointAuthorAction.TOGGLE_ROOF)
			return KK_WaypointAuthoring.RoofLabel();

		int floorIndex = FloorIndex();
		if (floorIndex >= 0)
			return KK_WaypointAuthoring.FloorLabel(floorIndex);

		return super.GetCommandDisplayName();
	}

	protected int FloorIndex()
	{
		if (m_eAction == KK_EWaypointAuthorAction.TOGGLE_FLOOR_0)
			return 0;
		if (m_eAction == KK_EWaypointAuthorAction.TOGGLE_FLOOR_1)
			return 1;
		if (m_eAction == KK_EWaypointAuthorAction.TOGGLE_FLOOR_2)
			return 2;
		if (m_eAction == KK_EWaypointAuthorAction.TOGGLE_FLOOR_3)
			return 3;
		if (m_eAction == KK_EWaypointAuthorAction.TOGGLE_FLOOR_4)
			return 4;
		if (m_eAction == KK_EWaypointAuthorAction.TOGGLE_FLOOR_5)
			return 5;
		if (m_eAction == KK_EWaypointAuthorAction.TOGGLE_FLOOR_6)
			return 6;
		if (m_eAction == KK_EWaypointAuthorAction.TOGGLE_FLOOR_7)
			return 7;

		return -1;
	}

	override bool CanBePerformed(notnull SCR_ChimeraCharacter user)
	{
		return KK_WaypointAuthoring.Enabled() && user != null;
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
		if (!KK_WaypointAuthoring.Enabled())
			return false;

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

			case KK_EWaypointAuthorAction.DELETE_WAYPOINT:
				return KK_WaypointAuthoring.DeleteWaypointHere();

			case KK_EWaypointAuthorAction.ADD_NODE:
				return KK_WaypointAuthoring.AddNodeHere();

			case KK_EWaypointAuthorAction.DELETE_NODE:
				return KK_WaypointAuthoring.DeleteNodeInSight();

			case KK_EWaypointAuthorAction.DELETE_CLUSTER:
				return KK_WaypointAuthoring.DeleteClusterInSight();

			case KK_EWaypointAuthorAction.UNDO:
				return KK_WaypointAuthoring.UndoLastEdit();

			case KK_EWaypointAuthorAction.TOGGLE_ROOF:
				return KK_WaypointAuthoring.ToggleRoof();

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

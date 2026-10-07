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
	UNDO,
	RELEASE_BUILDING,
	FORBID_ABOVE,
	FORBID_BELOW,
	CLEAR_HEIGHTS,
	LINK
}

class KK_WaypointAuthoring
{
	protected static BaseBuilding s_LockedBuilding;
	protected static string s_sLockedPrefab;
	protected static int s_iLastWaypointId = -1;
	protected static int s_iLinkFromId = -1;
	protected static bool s_bDebugDraw = true;
	protected static ref array<ref Shape> s_aDebugShapes = {};
	protected static bool s_bDebugTicking;
	protected static bool s_bSamplePending;
	protected static bool s_bSampleAnnounce;
	protected static int s_iServerSampleAttempts;
	protected static int s_iServerSamplePlayerId = -1;
	protected static int s_iServerGroupId = -1;
	protected static BaseBuilding s_ServerSampleBuilding;
	protected static string s_sServerSamplePrefab;
	protected static const int SAMPLE_ATTEMPTS_BASELINE = 20;
	protected static const int HEIGHT_NONE = 0;
	protected static const int HEIGHT_ABOVE = 1;
	protected static const int HEIGHT_BELOW = 2;
	protected static int s_iHeightMode;
	protected static float s_fHeightPreview;
	protected static float s_fHeightNotified = -100000;
	protected static int s_iHeightHoldFrames;
	protected static bool s_bHeightSliderTicking;
	protected static bool s_bHeightClickListening;
	protected static bool s_bHeightClickArmed;
	protected static float s_fHeightClickReadyAt;
	protected static bool s_bHeightWeaponLocked;
	protected static bool s_bHeightWeaponWasDisabled;

	static bool Enabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		return mode && mode.KK_GetWaypointAuthoring();
	}

	static void Notify(string text)
	{
		if (SCR_BaseGameMode.KK_LogEnabled())
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

		StopHeightSlider();
		s_LockedBuilding = building;
		s_sLockedPrefab = prefabName;
		s_iLastWaypointId = -1;
		s_iLinkFromId = -1;

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

	static bool ReleaseBuilding()
	{
		if (!s_LockedBuilding && s_sLockedPrefab.IsEmpty())
		{
			Notify("No building is locked");
			return false;
		}

		StopHeightSlider();
		s_bSamplePending = false;
		GetGame().GetCallqueue().Remove(RetryServerSample);

		s_LockedBuilding = null;
		s_sLockedPrefab = string.Empty;
		s_iLastWaypointId = -1;
		s_iLinkFromId = -1;
		RefreshDebugDraw();
		Notify("Released the building");
		return true;
	}

	static bool DeleteWaypointHere()
	{
		if (!RequireLockedBuilding())
			return false;

		BaseBuilding building = s_LockedBuilding;
		IEntity player = GetLocalPlayerEntity();
		if (!player)
			return false;

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

		if (s_iLinkFromId == removedId)
			s_iLinkFromId = -1;

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
		if (!RequireLockedBuilding())
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
		if (!RequireLockedBuilding())
			return false;

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
		if (!RequireLockedBuilding())
			return false;

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
		if (!RequireLockedBuilding())
			return false;

		string topPrefab;
		if (!KK_AuthorEditHistory.PeekPrefab(topPrefab))
		{
			Notify("Nothing to undo");
			return false;
		}

		if (topPrefab != s_sLockedPrefab)
		{
			Notify("Nothing to undo on this building");
			return false;
		}

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

		s_iLinkFromId = -1;

		Notify("Undid last edit");
		RefreshDebugDraw();
		return true;
	}

	static bool PlaceWaypoint(KK_EBuildingWaypointType type)
	{
		if (!RequireLockedBuilding())
			return false;

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

		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);

		KK_AuthoredBuildingWaypoint waypoint =
			KK_BuildingWaypointLibrary.PlaceWaypoint(
				s_LockedBuilding,
				type,
				player.GetOrigin(),
				facing
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

	static bool LinkWaypointInSight()
	{
		if (!RequireLockedBuilding())
			return false;

		int aimedId = WaypointInSight();
		if (aimedId < 0)
		{
			Notify("Not looking at a waypoint");
			return false;
		}

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (!prefabSet)
			return false;

		KK_AuthoredBuildingWaypoint aimed =
			KK_BuildingWaypointLibrary.FindWaypoint(prefabSet, aimedId);
		if (!aimed)
		{
			Notify("Not looking at a waypoint");
			return false;
		}

		if (s_iLinkFromId < 0)
		{
			s_iLinkFromId = aimedId;
			Notify(string.Format(
				"Link from %1 %2. Look at the other point",
				TypeLabel(aimed.m_eType),
				aimedId
			));
			RefreshDebugDraw();
			return true;
		}

		if (s_iLinkFromId == aimedId)
		{
			s_iLinkFromId = -1;
			Notify("Link cancelled");
			RefreshDebugDraw();
			return true;
		}

		KK_AuthoredBuildingWaypoint from =
			KK_BuildingWaypointLibrary.FindWaypoint(prefabSet, s_iLinkFromId);
		if (!from)
		{
			s_iLinkFromId = aimedId;
			Notify(string.Format(
				"Link from %1 %2. Look at the other point",
				TypeLabel(aimed.m_eType),
				aimedId
			));
			RefreshDebugDraw();
			return true;
		}

		bool alreadyLinked = from.m_aLinks.Find(aimedId) >= 0;
		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);

		bool changed;
		if (alreadyLinked)
			changed = KK_BuildingWaypointLibrary.UnlinkWaypoints(
				prefabSet,
				s_iLinkFromId,
				aimedId
			);
		else
			changed = KK_BuildingWaypointLibrary.LinkWaypoints(
				prefabSet,
				s_iLinkFromId,
				aimedId
			);

		if (!changed)
		{
			KK_AuthorEditHistory.Discard();
			Notify("Could not change the link");
			return false;
		}

		KK_BuildingWaypointLibrary.SaveToDisk();

		string verb = "Linked";
		if (alreadyLinked)
			verb = "Unlinked";

		Notify(string.Format(
			"%1 %2 %3 and %4 %5",
			verb,
			TypeLabel(from.m_eType),
			from.m_iId,
			TypeLabel(aimed.m_eType),
			aimedId
		));

		s_iLinkFromId = -1;
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

	protected static bool RequireLockedBuilding()
	{
		if (s_LockedBuilding && !s_sLockedPrefab.IsEmpty())
			return true;

		Notify("Lock a building first");
		return false;
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
			return false;

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (!prefabSet)
			return false;

		if (prefabSet.m_bHasSampleCache && !prefabSet.m_aSamples.IsEmpty())
			return true;

		SCR_PlayerControllerGroupComponent groupController =
			SCR_PlayerControllerGroupComponent.GetLocalPlayerControllerGroupComponent();
		SCR_GroupsManagerComponent groupsManager =
			SCR_GroupsManagerComponent.GetInstance();
		SCR_AIGroup group;
		if (groupController && groupsManager)
			group = groupsManager.FindGroup(groupController.GetGroupID());

		if (!group)
		{
			Notify("Need a squad with you to build the floor list");
			return false;
		}

		SCR_PlayerController controller = SCR_PlayerController.Cast(
			GetGame().GetPlayerController()
		);
		if (!controller)
			return false;

		s_bSampleAnnounce = announce;
		s_bSamplePending = true;
		Notify("Sampling this building");
		controller.KK_RequestBuildingSamples(
			SCR_EntityHelper.GetEntityCenterWorld(s_LockedBuilding),
			s_sLockedPrefab,
			groupController.GetGroupID()
		);
		return false;
	}

	protected static int SampleAttemptLimit()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return SAMPLE_ATTEMPTS_BASELINE;

		return mode.KK_GetSampleAttempts();
	}

	static void BeginServerSample(int playerId, vector origin, string prefabName, int groupId)
	{
		s_iServerSamplePlayerId = playerId;
		s_iServerGroupId = groupId;
		s_sServerSamplePrefab = prefabName;
		s_iServerSampleAttempts = 0;
		s_ServerSampleBuilding = FindServerBuilding(origin, prefabName);
		GetGame().GetCallqueue().Remove(RetryServerSample);

		if (!s_ServerSampleBuilding)
		{
			if (SCR_BaseGameMode.KK_LogEnabled())
				Print("KK: Server could not find the locked building", LogLevel.WARNING);
			FinishServerSample(false);
			return;
		}

		KK_BuildingWaypointLibrary.ReloadPrefabFromDisk(prefabName);
		RetryServerSample();
	}

	protected static void RetryServerSample()
	{
		if (!s_ServerSampleBuilding)
		{
			FinishServerSample(false);
			return;
		}

		SCR_AIGroup group = FindServerGroup(s_iServerSamplePlayerId, s_iServerGroupId);
		AIPathfindingComponent pathfinding;
		if (group)
			pathfinding = FindPathfinding(group);

		if (!group || !pathfinding)
		{
			if (SCR_BaseGameMode.KK_LogEnabled())
				Print(
					string.Format(
						"KK: Server squad has no pathfinding for sampling groupId=%1 group=%2",
						s_iServerGroupId,
						group
					),
					LogLevel.WARNING
				);
			FinishServerSample(false);
			return;
		}

		float horizontal = 2.5;
		float vertical = 1.5;
		float dedup = 1.25;
		float cluster = 4.0;
		bool filterSurfaces = true;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (mode)
		{
			horizontal = mode.KK_GetHorizontalSpacing();
			vertical = mode.KK_GetVerticalSpacing();
			dedup = mode.KK_GetDeduplicateDistance();
			cluster = mode.KK_GetClusterRadius();
			filterSurfaces = mode.KK_GetFilterBuildingSurfaces();
		}

		RequestNavmeshAtBuilding(pathfinding, s_ServerSampleBuilding);

		KK_BuildingInteriorPlan plan = new KK_BuildingInteriorPlan();
		bool tilesLoaded = plan.EnsureNavmeshLoaded(
			pathfinding,
			s_ServerSampleBuilding
		);

		bool ok = false;
		if (tilesLoaded)
		{
			ok = plan.Generate(
				group,
				pathfinding,
				s_ServerSampleBuilding,
				horizontal,
				vertical,
				dedup,
				cluster,
				false,
				filterSurfaces,
				false
			);
		}

		if (ok)
		{
			FinishServerSample(true);
			return;
		}

		s_iServerSampleAttempts++;
		if (s_iServerSampleAttempts < SampleAttemptLimit())
		{
			GetGame().GetCallqueue().CallLater(RetryServerSample, 500, false);
			return;
		}

		if (SCR_BaseGameMode.KK_LogEnabled())
			PrintFormat(
				"KK: Server samples failed for %1 tilesLoaded=%2",
				s_ServerSampleBuilding,
				tilesLoaded
			);
		FinishServerSample(false);
	}

	protected static void FinishServerSample(bool ok)
	{
		GetGame().GetCallqueue().Remove(RetryServerSample);

		int playerId = s_iServerSamplePlayerId;
		string prefabName = s_sServerSamplePrefab;
		s_ServerSampleBuilding = null;
		s_sServerSamplePrefab = string.Empty;
		s_iServerSamplePlayerId = -1;
		s_iServerGroupId = -1;

		PlayerController controller =
			GetGame().GetPlayerManager().GetPlayerController(playerId);
		SCR_PlayerController player = SCR_PlayerController.Cast(controller);
		if (!player)
			return;

		player.KK_FinishBuildingSamples(ok, prefabName);
	}

	static void OnServerSampleFinished(bool ok, string prefabName)
	{
		if (!s_bSamplePending)
			return;

		if (prefabName != s_sLockedPrefab)
			return;

		s_bSamplePending = false;

		if (ok)
			KK_BuildingWaypointLibrary.ReloadPrefabFromDisk(prefabName);

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		bool ready = prefabSet &&
			prefabSet.m_bHasSampleCache &&
			!prefabSet.m_aSamples.IsEmpty();

		if (!ready)
			KK_AuthorEditHistory.Discard();

		if (s_bSampleAnnounce)
		{
			if (ready)
				Notify("Floor list ready");
			else
				Notify("Could not build the floor list");
		}
		else if (ready)
		{
			Notify("Locked this building");
		}
		else
		{
			Notify("Locked this building, but samples failed");
		}

		RefreshDebugDraw();
	}

	protected static BaseBuilding FindServerBuilding(vector origin, string prefabName)
	{
		array<BaseBuilding> buildings =
			KK_BuildingResolver.FindOccupiableBuildings(origin, 25.0);

		BaseBuilding prefabMatch;
		float prefabDistance = 100000.0;

		foreach (BaseBuilding candidate : buildings)
		{
			if (!candidate)
				continue;

			if (KK_BuildingWaypointLibrary.ResolvePrefabName(candidate) != prefabName)
				continue;

			float distance = vector.Distance(
				origin,
				SCR_EntityHelper.GetEntityCenterWorld(candidate)
			);

			if (distance < prefabDistance)
			{
				prefabDistance = distance;
				prefabMatch = candidate;
			}
		}

		if (prefabMatch)
			return prefabMatch;

		if (buildings.IsEmpty())
			return null;

		return buildings[0];
	}

	protected static SCR_AIGroup FindServerGroup(int playerId, int groupId)
	{
		SCR_GroupsManagerComponent groupsManager =
			SCR_GroupsManagerComponent.GetInstance();

		if (groupsManager && groupId >= 0)
		{
			SCR_AIGroup fromId = groupsManager.FindGroup(groupId);
			if (fromId)
				return fromId;
		}

		PlayerController controller =
			GetGame().GetPlayerManager().GetPlayerController(playerId);
		if (!controller)
			return null;

		IEntity controlled = controller.GetControlledEntity();
		if (!controlled)
			return null;

		AIControlComponent control = AIControlComponent.Cast(
			controlled.FindComponent(AIControlComponent)
		);
		if (!control)
			return null;

		AIAgent agent = control.GetAIAgent();
		if (!agent)
			return null;

		SCR_AIGroup fromAgent = SCR_AIGroup.Cast(agent);
		if (fromAgent)
			return fromAgent;

		return SCR_AIGroup.Cast(agent.GetParentGroup());
	}

	protected static AIPathfindingComponent FindPathfinding(notnull SCR_AIGroup group)
	{
		AIPathfindingComponent pathfinding = AIPathfindingComponent.Cast(
			group.FindComponent(AIPathfindingComponent)
		);
		if (pathfinding)
			return pathfinding;

		array<AIAgent> agents = {};
		group.GetAgents(agents);

		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			pathfinding = AIPathfindingComponent.Cast(
				agent.FindComponent(AIPathfindingComponent)
			);
			if (pathfinding)
				return pathfinding;

			AIAgent parent = agent.GetParentGroup();
			if (!parent)
				continue;

			pathfinding = AIPathfindingComponent.Cast(
				parent.FindComponent(AIPathfindingComponent)
			);
			if (pathfinding)
				return pathfinding;
		}

		return null;
	}

	protected static void RequestNavmeshAtBuilding(
		notnull AIPathfindingComponent pathfinding,
		notnull BaseBuilding building)
	{
		vector center = SCR_EntityHelper.GetEntityCenterWorld(building);
		vector size = SCR_EntityHelper.GetEntitySize(building);
		vector closest;
		vector extents = Vector(
			Math.Max(size[0] * 0.5, 4.0),
			Math.Max(size[1] * 0.5, 4.0),
			Math.Max(size[2] * 0.5, 4.0)
		);

		pathfinding.GetClosestPositionOnNavmesh(center, extents, closest);
	}

	static bool ToggleRoof()
	{
		if (!RequireLockedBuilding())
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

	static bool BeginOrConfirmHeight(int mode)
	{
		if (!RequireLockedBuilding())
			return false;

		if (s_iHeightMode == mode)
			return ConfirmHeightLimit();

		s_iHeightMode = mode;
		s_iHeightHoldFrames = 0;
		s_fHeightNotified = -100000;
		float aimed;
		if (AimedLocalHeight(aimed))
			s_fHeightPreview = aimed;
		else if (mode == HEIGHT_ABOVE)
			s_fHeightPreview = 0;
		else
			s_fHeightPreview = 0;

		StartHeightSliderTick();
		NotifyHeightPreview(true);
		RefreshDebugDraw();
		return true;
	}

	static bool ClearHeightLimits()
	{
		if (!RequireLockedBuilding())
			return false;

		StopHeightSlider();

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (!prefabSet)
			return false;

		if (!KK_BuildingWaypointLibrary.HasHeightLimit(prefabSet))
		{
			Notify("No height limits");
			RefreshDebugDraw();
			return false;
		}

		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);
		KK_BuildingWaypointLibrary.ClearHeightLimits(prefabSet);
		Notify("Height limits cleared");
		RefreshDebugDraw();
		return true;
	}

	static string HeightCommandLabel(int mode)
	{
		if (s_iHeightMode == mode)
			return "Set " + FormatMeters(s_fHeightPreview);

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (mode == HEIGHT_ABOVE)
		{
			if (prefabSet && prefabSet.m_bForbidAbove)
				return "Above " + FormatMeters(prefabSet.m_fForbidAboveLocalY);

			return "Forbid above";
		}

		if (prefabSet && prefabSet.m_bForbidBelow)
			return "Below " + FormatMeters(prefabSet.m_fForbidBelowLocalY);

		return "Forbid below";
	}

	static string ForbidAboveLabel()
	{
		return HeightCommandLabel(HEIGHT_ABOVE);
	}

	static string ForbidBelowLabel()
	{
		return HeightCommandLabel(HEIGHT_BELOW);
	}

	static string LinkLabel()
	{
		if (s_iLinkFromId < 0)
			return "Link";

		int aimedId = WaypointInSight();
		if (aimedId == s_iLinkFromId)
			return "Cancel link";

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		KK_AuthoredBuildingWaypoint from;
		if (prefabSet)
			from = KK_BuildingWaypointLibrary.FindWaypoint(prefabSet, s_iLinkFromId);
		else
			from = null;

		if (from && aimedId >= 0 && from.m_aLinks.Find(aimedId) >= 0)
			return "Unlink";

		return "Complete link";
	}

	static bool BeginForbidAbove()
	{
		return BeginOrConfirmHeight(HEIGHT_ABOVE);
	}

	static bool BeginForbidBelow()
	{
		return BeginOrConfirmHeight(HEIGHT_BELOW);
	}

	static void RefreshDebugDraw()
	{
		ClearDebugShapes();

		if (s_iHeightMode == HEIGHT_NONE && !s_bDebugDraw)
			return;

		if (!s_LockedBuilding)
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

			if (waypoint.m_iId == s_iLinkFromId)
			{
				s_aDebugShapes.Insert(
					Shape.CreateSphere(0xFFFFFF88, shapeFlags, marker, 0.48)
				);
			}

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

		float roofTopY = -10000.0;
		if (prefabSet.m_bDropRoof)
			roofTopY = HighestOpenSampleY(prefabSet);

		int aimedSample = SampleInSight();

		foreach (KK_CachedInteriorSample sample : prefabSet.m_aSamples)
		{
			if (!sample)
				continue;

			vector sampleWorld =
				s_LockedBuilding.CoordToParent(sample.m_vLocalPosition);
			int sampleColor = 0xFF88AACC;

			if (SampleIsForbidden(prefabSet, sample, sampleWorld, roofTopY))
				sampleColor = 0xFFFF2222;

			vector marker = sampleWorld + Vector(0, 0.2, 0);
			float markerRadius = 0.12;
			if (aimedSample >= 0 && sample == prefabSet.m_aSamples[aimedSample])
			{
				sampleColor = 0xFFFFFF00;
				markerRadius = 0.28;
			}

			s_aDebugShapes.Insert(
				Shape.CreateSphere(
					sampleColor,
					shapeFlags,
					marker,
					markerRadius
				)
			);
		}

		DrawStoredHeightLines(prefabSet);

		if (s_bDebugDraw)
			EnsureDebugTick();
	}

	protected static float HighestOpenSampleY(notnull KK_PrefabWaypointSet prefabSet)
	{
		float topLocalY = -10000.0;

		foreach (KK_CachedInteriorSample sample : prefabSet.m_aSamples)
		{
			if (!sample)
				continue;

			if (ElevationForbidden(prefabSet, sample.m_vLocalPosition[1]))
			{
				continue;
			}

			topLocalY = Math.Max(topLocalY, sample.m_vLocalPosition[1]);
		}

		return topLocalY;
	}

	protected static bool SampleIsForbidden(
		notnull KK_PrefabWaypointSet prefabSet,
		notnull KK_CachedInteriorSample sample,
		vector worldPosition,
		float roofTopY)
	{
		if (ElevationForbidden(prefabSet, sample.m_vLocalPosition[1]))
			return true;

		if (!prefabSet.m_bDropRoof || roofTopY < -9000.0)
			return false;

		const float ROOF_BAND = 2.0;
		if (roofTopY - sample.m_vLocalPosition[1] > ROOF_BAND)
			return false;

		return !SampleHasBuildingOverhead(s_LockedBuilding, worldPosition);
	}

	protected static bool SampleHasBuildingOverhead(
		notnull IEntity building,
		vector position)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return true;

		TraceParam trace = new TraceParam();
		trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		trace.Start = position + Vector(0, 0.3, 0);
		trace.End = position + Vector(0, 4.0, 0);

		float fraction = world.TraceMove(trace, FilterOverheadTrace);
		if (fraction >= 1.0 || !trace.TraceEnt)
			return false;

		if (trace.TraceEnt == building)
			return true;

		BaseBuilding hitBuilding =
			KK_BuildingResolver.ResolveBuildingRoot(trace.TraceEnt);

		return hitBuilding == building;
	}

	protected static bool FilterOverheadTrace(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		if (ChimeraCharacter.Cast(entity))
			return false;

		return true;
	}

	protected static bool ElevationForbidden(
		notnull KK_PrefabWaypointSet prefabSet,
		float localY)
	{
		bool forbidAbove = prefabSet.m_bForbidAbove;
		float aboveY = prefabSet.m_fForbidAboveLocalY;
		bool forbidBelow = prefabSet.m_bForbidBelow;
		float belowY = prefabSet.m_fForbidBelowLocalY;

		if (s_iHeightMode == HEIGHT_ABOVE)
		{
			forbidAbove = true;
			aboveY = s_fHeightPreview;
		}
		else if (s_iHeightMode == HEIGHT_BELOW)
		{
			forbidBelow = true;
			belowY = s_fHeightPreview;
		}

		return KK_BuildingWaypointLibrary.IsElevationForbidden(
			prefabSet,
			localY,
			forbidAbove,
			aboveY,
			forbidBelow,
			belowY
		);
	}

	protected static bool ConfirmHeightLimit()
	{
		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		int mode = s_iHeightMode;
		float localY = s_fHeightPreview;
		StopHeightSlider();

		if (!prefabSet || mode == HEIGHT_NONE)
			return false;

		KK_AuthorEditHistory.Remember(s_sLockedPrefab, s_iLastWaypointId);
		KK_BuildingWaypointLibrary.SetHeightLimit(
			prefabSet,
			mode == HEIGHT_ABOVE,
			true,
			localY
		);

		if (mode == HEIGHT_ABOVE)
			Notify("Forbid above " + FormatMeters(localY));
		else
			Notify("Forbid below " + FormatMeters(localY));

		RefreshDebugDraw();
		return true;
	}

	protected static void StartHeightSliderTick()
	{
		if (s_bHeightSliderTicking)
			return;

		s_bHeightClickArmed = false;
		s_fHeightClickReadyAt = 0;
		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			// The click that chose this radial entry can still be held.
			// Wait it out, then the next press confirms the line.
			s_fHeightClickReadyAt = world.GetWorldTime() + 350;
		}

		BeginHeightClick();
		s_bHeightSliderTicking = true;
		GetGame().GetCallqueue().CallLater(HeightSliderTick, 50, true);
	}

	protected static void StopHeightSlider()
	{
		s_iHeightMode = HEIGHT_NONE;
		s_iHeightHoldFrames = 0;
		s_fHeightNotified = -100000;
		s_bHeightClickArmed = false;
		EndHeightClick();

		if (!s_bHeightSliderTicking)
			return;

		s_bHeightSliderTicking = false;
		GetGame().GetCallqueue().Remove(HeightSliderTick);
	}

	protected static void BeginHeightClick()
	{
		HoldWeaponFire(true);

		if (s_bHeightClickListening)
			return;

		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;

		input.AddActionListener("CharacterFire", EActionTrigger.DOWN, OnHeightConfirmClick);
		s_bHeightClickListening = true;
	}

	protected static void EndHeightClick()
	{
		if (s_bHeightClickListening)
		{
			InputManager input = GetGame().GetInputManager();
			if (input)
				input.RemoveActionListener("CharacterFire", EActionTrigger.DOWN, OnHeightConfirmClick);

			s_bHeightClickListening = false;
		}

		HoldWeaponFire(false);
	}

	protected static void OnHeightConfirmClick()
	{
		if (s_iHeightMode == HEIGHT_NONE || !s_bHeightClickArmed)
			return;

		s_bHeightClickArmed = false;
		GetGame().GetCallqueue().CallLater(ConfirmHeightFromClick, 1, false);
	}

	protected static void ConfirmHeightFromClick()
	{
		if (s_iHeightMode == HEIGHT_NONE)
			return;

		ConfirmHeightLimit();
	}

	protected static void ArmHeightClick(notnull InputManager input)
	{
		if (s_bHeightClickArmed)
			return;

		float now = 0;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			now = world.GetWorldTime();

		if (now < s_fHeightClickReadyAt)
			return;

		if (input.GetActionValue("CharacterFire") > 0.2)
			return;

		s_bHeightClickArmed = true;
	}

	protected static void HoldWeaponFire(bool hold)
	{
		IEntity player = GetLocalPlayerEntity();
		CharacterControllerComponent controller;
		if (player)
		{
			controller = CharacterControllerComponent.Cast(
				player.FindComponent(CharacterControllerComponent)
			);
		}

		if (hold)
		{
			if (!controller)
				return;

			if (!s_bHeightWeaponLocked)
			{
				s_bHeightWeaponWasDisabled = controller.GetDisableWeaponControls();
				s_bHeightWeaponLocked = true;
			}

			controller.SetDisableWeaponControls(true);
			controller.SetWeaponNoFireTime(1.0);
			return;
		}

		if (!s_bHeightWeaponLocked)
			return;

		s_bHeightWeaponLocked = false;
		if (!controller)
			return;

		controller.SetDisableWeaponControls(s_bHeightWeaponWasDisabled);
		controller.SetWeaponNoFireTime(0);
	}

	protected static void HeightSliderTick()
	{
		if (s_iHeightMode == HEIGHT_NONE || !s_LockedBuilding)
		{
			StopHeightSlider();
			RefreshDebugDraw();
			return;
		}

		BeginHeightClick();
		UpdateHeightPreview();
		RefreshDebugDraw();
	}

	protected static void UpdateHeightPreview()
	{
		InputManager input = GetGame().GetInputManager();
		float wheel = 0;
		if (input)
		{
			ArmHeightClick(input);
			wheel = input.GetActionValue("MouseWheel");
		}

		if (wheel > 0.01)
		{
			s_fHeightPreview = s_fHeightPreview + 0.25;
			s_iHeightHoldFrames = 8;
		}
		else if (wheel < -0.01)
		{
			s_fHeightPreview = s_fHeightPreview - 0.25;
			s_iHeightHoldFrames = 8;
		}
		else if (s_iHeightHoldFrames > 0)
		{
			s_iHeightHoldFrames--;
		}
		else
		{
			float aimed;
			if (AimedLocalHeight(aimed))
				s_fHeightPreview = aimed;
		}

		NotifyHeightPreview(false);
	}

	protected static void NotifyHeightPreview(bool force)
	{
		if (!force && Math.AbsFloat(s_fHeightPreview - s_fHeightNotified) < 0.2)
			return;

		s_fHeightNotified = s_fHeightPreview;
		string side = "above";
		if (s_iHeightMode == HEIGHT_BELOW)
			side = "below";

		Notify(string.Format(
			"Aim the %1 line, now %2. Left click to set it.",
			side,
			FormatMeters(s_fHeightPreview)
		));
	}

	protected static bool AimedLocalHeight(out float localY)
	{
		localY = 0;
		IEntity player = GetLocalPlayerEntity();
		if (!player || !s_LockedBuilding)
			return false;

		vector start;
		vector direction;
		if (!LookDirection(player, start, direction))
			return false;

		vector origin = s_LockedBuilding.GetOrigin();
		vector flatDir = direction;
		flatDir[1] = 0;
		float flatLen = flatDir.Length();
		float distance = 4.0;

		if (flatLen > 0.05)
		{
			flatDir = flatDir * (1.0 / flatLen);
			vector toBuilding = origin - start;
			toBuilding[1] = 0;
			distance = vector.Dot(toBuilding, flatDir);
			if (distance < 0.5)
				distance = 0.5;
			if (distance > 40.0)
				distance = 40.0;
		}

		vector point = start + (direction * distance);
		vector local = s_LockedBuilding.CoordToLocal(point);
		localY = Math.Round(local[1] / 0.25) * 0.25;
		return true;
	}

	protected static void DrawStoredHeightLines(notnull KK_PrefabWaypointSet prefabSet)
	{
		if (s_iHeightMode == HEIGHT_ABOVE)
			DrawHeightLine(s_fHeightPreview, 0xFFFFFF00);
		else if (prefabSet.m_bForbidAbove)
			DrawHeightLine(prefabSet.m_fForbidAboveLocalY, 0xFFFF2222);

		if (s_iHeightMode == HEIGHT_BELOW)
			DrawHeightLine(s_fHeightPreview, 0xFF66EEFF);
		else if (prefabSet.m_bForbidBelow)
			DrawHeightLine(prefabSet.m_fForbidBelowLocalY, 0xFFFF8822);
	}

	protected static void DrawHeightLine(float localY, int color)
	{
		if (!s_LockedBuilding)
			return;

		float minX = 10000.0;
		float maxX = -10000.0;
		float minZ = 10000.0;
		float maxZ = -10000.0;
		bool any = false;

		KK_PrefabWaypointSet prefabSet = GetLockedSet();
		if (prefabSet)
		{
			foreach (KK_CachedInteriorSample sample : prefabSet.m_aSamples)
			{
				if (!sample)
					continue;

				any = true;
				minX = Math.Min(minX, sample.m_vLocalPosition[0]);
				maxX = Math.Max(maxX, sample.m_vLocalPosition[0]);
				minZ = Math.Min(minZ, sample.m_vLocalPosition[2]);
				maxZ = Math.Max(maxZ, sample.m_vLocalPosition[2]);
			}
		}

		if (!any)
		{
			minX = -2.0;
			maxX = 2.0;
			minZ = -2.0;
			maxZ = 2.0;
		}

		if (maxX - minX < 1.0)
		{
			minX = minX - 1.0;
			maxX = maxX + 1.0;
		}

		if (maxZ - minZ < 1.0)
		{
			minZ = minZ - 1.0;
			maxZ = maxZ + 1.0;
		}

		vector a = s_LockedBuilding.CoordToParent(Vector(minX, localY, minZ));
		vector b = s_LockedBuilding.CoordToParent(Vector(maxX, localY, minZ));
		vector c = s_LockedBuilding.CoordToParent(Vector(maxX, localY, maxZ));
		vector d = s_LockedBuilding.CoordToParent(Vector(minX, localY, maxZ));
		int flags = ShapeFlags.NOZBUFFER | ShapeFlags.VISIBLE;

		s_aDebugShapes.Insert(Shape.CreateArrow(a, b, 0.04, color, flags));
		s_aDebugShapes.Insert(Shape.CreateArrow(b, c, 0.04, color, flags));
		s_aDebugShapes.Insert(Shape.CreateArrow(c, d, 0.04, color, flags));
		s_aDebugShapes.Insert(Shape.CreateArrow(d, a, 0.04, color, flags));
	}

	protected static string FormatMeters(float meters)
	{
		float shown = Math.Round(meters * 10.0) / 10.0;
		return string.Format("%1 m", shown);
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
			0.35
		);
	}

	protected static int WaypointInSight()
	{
		IEntity player = GetLocalPlayerEntity();
		if (!player || !s_LockedBuilding)
			return -1;

		vector start;
		vector direction;
		if (!LookDirection(player, start, direction))
			return -1;

		return KK_BuildingWaypointLibrary.FindWaypointAlongRay(
			s_LockedBuilding,
			start,
			direction,
			40.0,
			0.45
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
			if (s_iHeightMode == HEIGHT_NONE)
				ClearDebugShapes();

			GetGame().GetCallqueue().Remove(DebugTick);
			s_bDebugTicking = false;
			return;
		}

		RefreshDebugDraw();
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
		copy.m_bForbidAbove = live.m_bForbidAbove;
		copy.m_fForbidAboveLocalY = live.m_fForbidAboveLocalY;
		copy.m_bForbidBelow = live.m_bForbidBelow;
		copy.m_fForbidBelowLocalY = live.m_fForbidBelowLocalY;

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

	static bool PeekPrefab(out string prefabName)
	{
		prefabName = string.Empty;
		if (!s_aSteps || s_aSteps.IsEmpty())
			return false;

		KK_AuthorSnapshot step = s_aSteps[s_aSteps.Count() - 1];
		if (!step)
			return false;

		prefabName = step.m_sPrefabName;
		return true;
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

		if (m_eAction == KK_EWaypointAuthorAction.FORBID_ABOVE)
			return KK_WaypointAuthoring.ForbidAboveLabel();

		if (m_eAction == KK_EWaypointAuthorAction.FORBID_BELOW)
			return KK_WaypointAuthoring.ForbidBelowLabel();

		if (m_eAction == KK_EWaypointAuthorAction.LINK)
			return KK_WaypointAuthoring.LinkLabel();

		return super.GetCommandDisplayName();
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

			case KK_EWaypointAuthorAction.RELEASE_BUILDING:
				return KK_WaypointAuthoring.ReleaseBuilding();

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

			case KK_EWaypointAuthorAction.FORBID_ABOVE:
				return KK_WaypointAuthoring.BeginForbidAbove();

			case KK_EWaypointAuthorAction.FORBID_BELOW:
				return KK_WaypointAuthoring.BeginForbidBelow();

			case KK_EWaypointAuthorAction.CLEAR_HEIGHTS:
				return KK_WaypointAuthoring.ClearHeightLimits();

			case KK_EWaypointAuthorAction.LINK:
				return KK_WaypointAuthoring.LinkWaypointInSight();
		}

		return false;
	}
}

modded class SCR_PlayerController
{
	void KK_RequestBuildingSamples(vector origin, string prefabName, int groupId)
	{
		Rpc(Rpc_KK_AskBuildingSamples, origin, prefabName, groupId);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_KK_AskBuildingSamples(vector origin, string prefabName, int groupId)
	{
		KK_WaypointAuthoring.BeginServerSample(
			GetPlayerId(),
			origin,
			prefabName,
			groupId
		);
	}

	void KK_FinishBuildingSamples(bool ok, string prefabName)
	{
		Rpc(Rpc_KK_BuildingSamplesFinished, ok, prefabName);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void Rpc_KK_BuildingSamplesFinished(bool ok, string prefabName)
	{
		KK_WaypointAuthoring.OnServerSampleFinished(ok, prefabName);
	}
}

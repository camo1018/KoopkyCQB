modded class SCR_BaseGameMode
{
	[Attribute("2.5", UIWidgets.EditBox, "Horizontal sample spacing", category: "Koopky CQB/Interior")]
	protected float m_fKK_HorizontalSpacing;

	[Attribute("1.0", UIWidgets.EditBox, "Vertical sample spacing", category: "Koopky CQB/Interior")]
	protected float m_fKK_VerticalSpacing;

	[Attribute("1.25", UIWidgets.EditBox, "Sample deduplication distance", category: "Koopky CQB/Interior")]
	protected float m_fKK_DeduplicateDistance;

	[Attribute("4", UIWidgets.EditBox, "Room-like cluster radius", category: "Koopky CQB/Interior")]
	protected float m_fKK_ClusterRadius;

	[Attribute("0", UIWidgets.CheckBox, "Drop interior points that are not walk-connected to the squad, including upper floors", category: "Koopky CQB/Interior")]
	protected bool m_bKK_FilterUnreachableIslands;

	[Attribute("1", UIWidgets.CheckBox, "Garrison after the last clear", category: "Koopky CQB/Clear")]
	protected bool m_bKK_GarrisonAfterClear;

	[Attribute("75", UIWidgets.EditBox, "Building search radius", category: "Koopky CQB/Clear")]
	protected float m_fKK_ClearSearchRadius;

	[Attribute("2.5", UIWidgets.EditBox, "Distance considered visited", category: "Koopky CQB/Clear")]
	protected float m_fKK_ClearArrivalRadius;

	[Attribute("8", UIWidgets.EditBox, "Distance at which seeing a point counts as visited", category: "Koopky CQB/Clear")]
	protected float m_fKK_SightVisitRange;

	[Attribute("0.25", UIWidgets.EditBox, "Seconds before a blocked line of sight to the same point is traced again", category: "Koopky CQB/Clear")]
	protected float m_fKK_SightRetry;

	[Attribute("45", UIWidgets.EditBox, "Movement timeout in seconds", category: "Koopky CQB/Clear")]
	protected float m_fKK_ClearMovementTimeout;

	[Attribute("3", UIWidgets.EditBox, "Fail if standing still with no progress this many seconds", category: "Koopky CQB/Clear")]
	protected float m_fKK_ClearStuckTimeout;

	[Attribute("1", UIWidgets.EditBox, "Attempts before a target is unreachable", category: "Koopky CQB/Clear")]
	protected int m_iKK_ClearMaximumRetries;

	[Attribute("1", UIWidgets.CheckBox, "If one node in a room is unreachable, fail the rest of that room", category: "Koopky CQB/Clear")]
	protected bool m_bKK_ClearFailCluster;

	[Attribute("75", UIWidgets.EditBox, "Building search radius", category: "Koopky CQB/Garrison")]
	protected float m_fKK_GarrisonSearchRadius;

	[Attribute("2.5", UIWidgets.EditBox, "Distance considered arrived", category: "Koopky CQB/Garrison")]
	protected float m_fKK_GarrisonArrivalRadius;

	[Attribute("5", UIWidgets.EditBox, "Allowed combat reposition radius", category: "Koopky CQB/Garrison")]
	protected float m_fKK_HoldRadius;

	[Attribute("3", UIWidgets.EditBox, "Seconds between hold corrections", category: "Koopky CQB/Garrison")]
	protected float m_fKK_ReassignmentInterval;

	[Attribute("20", UIWidgets.EditBox, "Minimum seconds at a post before rotating", category: "Koopky CQB/Garrison")]
	protected float m_fKK_RotateIntervalMin;

	[Attribute("60", UIWidgets.EditBox, "Maximum seconds at a post before rotating", category: "Koopky CQB/Garrison")]
	protected float m_fKK_RotateIntervalMax;

	[Attribute("45", UIWidgets.EditBox, "Movement timeout in seconds", category: "Koopky CQB/Garrison")]
	protected float m_fKK_GarrisonMovementTimeout;

	[Attribute("3", UIWidgets.EditBox, "Fail if standing still with no progress this many seconds", category: "Koopky CQB/Garrison")]
	protected float m_fKK_GarrisonStuckTimeout;

	[Attribute("1", UIWidgets.EditBox, "Attempts before a hold is unreachable", category: "Koopky CQB/Garrison")]
	protected int m_iKK_GarrisonMaximumRetries;

	[Attribute("1", UIWidgets.CheckBox, "If one node in a room is unreachable, fail the rest of that room", category: "Koopky CQB/Garrison")]
	protected bool m_bKK_GarrisonFailCluster;

	[Attribute("1", UIWidgets.CheckBox, "While clearing or garrisoning, suppression does not slow recognition and the first shot does not wait", category: "Koopky CQB/Combat")]
	protected bool m_bKK_SharpCombat;

	[Attribute("1", UIWidgets.EditBox, "Recognition speed while clearing or garrisoning. 1 is normal.", category: "Koopky CQB/Combat")]
	protected float m_fKK_PerceptionFactor;

	[Attribute("1", UIWidgets.CheckBox, "Open a closed door in front of a clear or garrison move", category: "Koopky CQB/Combat")]
	protected bool m_bKK_OpenDoors;

	[Attribute("2", UIWidgets.EditBox, "How far ahead a door is searched, in metres", category: "Koopky CQB/Combat")]
	protected float m_fKK_DoorReach;

	[Attribute("0.2", UIWidgets.EditBox, "Seconds before another door search", category: "Koopky CQB/Combat")]
	protected float m_fKK_DoorSearchInterval;

	[Attribute("0.4", UIWidgets.EditBox, "Metres moved before another door search", category: "Koopky CQB/Combat")]
	protected float m_fKK_DoorSearchDistance;

	[Attribute("0", UIWidgets.CheckBox, "Draw interior points while playing from Workbench", category: "Koopky CQB/Debug")]
	protected bool m_bKK_DebugDraw;

	protected static const string KK_CONFIG_PATH = "$profile:KoopkyCQB_config.json";
	protected bool m_bKK_ConfigLoaded;

	override void OnGameModeStart()
	{
		super.OnGameModeStart();
		KK_LoadConfigOnce();
	}

	protected void KK_LoadConfigOnce()
	{
		if (m_bKK_ConfigLoaded)
			return;

		if (!Replication.IsServer())
			return;

		m_bKK_ConfigLoaded = true;

		if (!FileIO.FileExists(KK_CONFIG_PATH))
			KK_WriteConfig();

		KK_ImportConfig();
	}

	void KK_ExportConfig()
	{
		if (!Replication.IsServer())
			return;

		KK_WriteConfig();
	}

	void KK_ImportConfig()
	{
		if (!Replication.IsServer())
			return;

		SCR_JsonLoadContext context = new SCR_JsonLoadContext();
		if (!context.LoadFromFile(KK_CONFIG_PATH))
		{
			Print("KK: Failed to load " + KK_CONFIG_PATH, LogLevel.ERROR);
			return;
		}

		KK_ApplyConfig(context);
		Print("KK: Imported config from " + KK_CONFIG_PATH);
	}

	protected void KK_ApplyConfig(notnull SCR_JsonLoadContext context)
	{
		if (context.StartObject("Interior"))
		{
			KK_ReadFloat(context, "HorizontalSpacing", m_fKK_HorizontalSpacing);
			KK_ReadFloat(context, "VerticalSpacing", m_fKK_VerticalSpacing);
			KK_ReadFloat(context, "DeduplicateDistance", m_fKK_DeduplicateDistance);
			KK_ReadFloat(context, "ClusterRadius", m_fKK_ClusterRadius);
			KK_ReadBool(context, "FilterUnreachableIslands", m_bKK_FilterUnreachableIslands);
			context.EndObject();
		}

		if (context.StartObject("Clear"))
		{
			KK_ReadBool(context, "GarrisonAfterClear", m_bKK_GarrisonAfterClear);
			KK_ReadFloat(context, "SearchRadius", m_fKK_ClearSearchRadius);
			KK_ReadFloat(context, "ArrivalRadius", m_fKK_ClearArrivalRadius);
			KK_ReadFloat(context, "SightVisitRange", m_fKK_SightVisitRange);
			KK_ReadFloat(context, "SightRetry", m_fKK_SightRetry);
			KK_ReadFloat(context, "MovementTimeout", m_fKK_ClearMovementTimeout);
			KK_ReadFloat(context, "StuckTimeout", m_fKK_ClearStuckTimeout);
			KK_ReadInt(context, "MaximumRetries", m_iKK_ClearMaximumRetries);
			KK_ReadBool(context, "FailCluster", m_bKK_ClearFailCluster);
			context.EndObject();
		}

		if (context.StartObject("Garrison"))
		{
			KK_ReadFloat(context, "SearchRadius", m_fKK_GarrisonSearchRadius);
			KK_ReadFloat(context, "ArrivalRadius", m_fKK_GarrisonArrivalRadius);
			KK_ReadFloat(context, "HoldRadius", m_fKK_HoldRadius);
			KK_ReadFloat(context, "ReassignmentInterval", m_fKK_ReassignmentInterval);
			KK_ReadFloat(context, "RotateIntervalMin", m_fKK_RotateIntervalMin);
			KK_ReadFloat(context, "RotateIntervalMax", m_fKK_RotateIntervalMax);
			KK_ReadFloat(context, "MovementTimeout", m_fKK_GarrisonMovementTimeout);
			KK_ReadFloat(context, "StuckTimeout", m_fKK_GarrisonStuckTimeout);
			KK_ReadInt(context, "MaximumRetries", m_iKK_GarrisonMaximumRetries);
			KK_ReadBool(context, "FailCluster", m_bKK_GarrisonFailCluster);
			context.EndObject();
		}

		if (context.StartObject("Combat"))
		{
			KK_ReadBool(context, "SharpCombat", m_bKK_SharpCombat);
			KK_ReadFloat(context, "PerceptionFactor", m_fKK_PerceptionFactor);
			KK_ReadBool(context, "OpenDoors", m_bKK_OpenDoors);
			KK_ReadFloat(context, "DoorReach", m_fKK_DoorReach);
			KK_ReadFloat(context, "DoorSearchInterval", m_fKK_DoorSearchInterval);
			KK_ReadFloat(context, "DoorSearchDistance", m_fKK_DoorSearchDistance);
			context.EndObject();
		}

		if (context.StartObject("Debug"))
		{
			KK_ReadBool(context, "DebugDraw", m_bKK_DebugDraw);
			context.EndObject();
		}
	}

	protected void KK_ReadFloat(
		notnull SCR_JsonLoadContext context,
		string name,
		out float value)
	{
		float read;
		if (context.ReadValue(name, read))
			value = read;
	}

	protected void KK_ReadInt(
		notnull SCR_JsonLoadContext context,
		string name,
		out int value)
	{
		int read;
		if (context.ReadValue(name, read))
			value = read;
	}

	protected void KK_ReadBool(
		notnull SCR_JsonLoadContext context,
		string name,
		out bool value)
	{
		bool read;
		if (context.ReadValue(name, read))
			value = read;
	}

	protected void KK_WriteConfig()
	{
		SCR_JsonSaveContext context = new SCR_JsonSaveContext();
		context.StartObject("Interior");
		context.WriteValue("HorizontalSpacing", m_fKK_HorizontalSpacing);
		context.WriteValue("VerticalSpacing", m_fKK_VerticalSpacing);
		context.WriteValue("DeduplicateDistance", m_fKK_DeduplicateDistance);
		context.WriteValue("ClusterRadius", m_fKK_ClusterRadius);
		context.WriteValue("FilterUnreachableIslands", m_bKK_FilterUnreachableIslands);
		context.EndObject();

		context.StartObject("Clear");
		context.WriteValue("GarrisonAfterClear", m_bKK_GarrisonAfterClear);
		context.WriteValue("SearchRadius", m_fKK_ClearSearchRadius);
		context.WriteValue("ArrivalRadius", m_fKK_ClearArrivalRadius);
		context.WriteValue("SightVisitRange", m_fKK_SightVisitRange);
		context.WriteValue("SightRetry", m_fKK_SightRetry);
		context.WriteValue("MovementTimeout", m_fKK_ClearMovementTimeout);
		context.WriteValue("StuckTimeout", m_fKK_ClearStuckTimeout);
		context.WriteValue("MaximumRetries", m_iKK_ClearMaximumRetries);
		context.WriteValue("FailCluster", m_bKK_ClearFailCluster);
		context.EndObject();

		context.StartObject("Garrison");
		context.WriteValue("SearchRadius", m_fKK_GarrisonSearchRadius);
		context.WriteValue("ArrivalRadius", m_fKK_GarrisonArrivalRadius);
		context.WriteValue("HoldRadius", m_fKK_HoldRadius);
		context.WriteValue("ReassignmentInterval", m_fKK_ReassignmentInterval);
		context.WriteValue("RotateIntervalMin", m_fKK_RotateIntervalMin);
		context.WriteValue("RotateIntervalMax", m_fKK_RotateIntervalMax);
		context.WriteValue("MovementTimeout", m_fKK_GarrisonMovementTimeout);
		context.WriteValue("StuckTimeout", m_fKK_GarrisonStuckTimeout);
		context.WriteValue("MaximumRetries", m_iKK_GarrisonMaximumRetries);
		context.WriteValue("FailCluster", m_bKK_GarrisonFailCluster);
		context.EndObject();

		context.StartObject("Combat");
		context.WriteValue("SharpCombat", m_bKK_SharpCombat);
		context.WriteValue("PerceptionFactor", m_fKK_PerceptionFactor);
		context.WriteValue("OpenDoors", m_bKK_OpenDoors);
		context.WriteValue("DoorReach", m_fKK_DoorReach);
		context.WriteValue("DoorSearchInterval", m_fKK_DoorSearchInterval);
		context.WriteValue("DoorSearchDistance", m_fKK_DoorSearchDistance);
		context.EndObject();

		context.StartObject("Debug");
		context.WriteValue("DebugDraw", m_bKK_DebugDraw);
		context.EndObject();

		if (!context.SaveToFile(KK_CONFIG_PATH))
			Print("KK: Failed to write " + KK_CONFIG_PATH, LogLevel.ERROR);
		else
			Print("KK: Wrote config to " + KK_CONFIG_PATH);
	}

	static SCR_BaseGameMode Get()
	{
		return SCR_BaseGameMode.Cast(GetGame().GetGameMode());
	}

	float KK_GetHorizontalSpacing()
	{
		return m_fKK_HorizontalSpacing;
	}

	float KK_GetVerticalSpacing()
	{
		return m_fKK_VerticalSpacing;
	}

	float KK_GetDeduplicateDistance()
	{
		return m_fKK_DeduplicateDistance;
	}

	float KK_GetClusterRadius()
	{
		return m_fKK_ClusterRadius;
	}

	bool KK_GetFilterUnreachableIslands()
	{
		return m_bKK_FilterUnreachableIslands;
	}

	bool KK_GetGarrisonAfterClear()
	{
		return m_bKK_GarrisonAfterClear;
	}

	float KK_GetClearSearchRadius()
	{
		return m_fKK_ClearSearchRadius;
	}

	float KK_GetClearArrivalRadius()
	{
		return m_fKK_ClearArrivalRadius;
	}

	float KK_GetSightVisitRange()
	{
		return Math.Max(m_fKK_SightVisitRange, 0);
	}

	float KK_GetSightRetry()
	{
		return Math.Max(m_fKK_SightRetry, 0);
	}

	float KK_GetClearMovementTimeout()
	{
		return m_fKK_ClearMovementTimeout;
	}

	float KK_GetClearStuckTimeout()
	{
		return m_fKK_ClearStuckTimeout;
	}

	int KK_GetClearMaximumRetries()
	{
		return m_iKK_ClearMaximumRetries;
	}

	bool KK_GetClearFailCluster()
	{
		return m_bKK_ClearFailCluster;
	}

	float KK_GetGarrisonSearchRadius()
	{
		return m_fKK_GarrisonSearchRadius;
	}

	float KK_GetGarrisonArrivalRadius()
	{
		return m_fKK_GarrisonArrivalRadius;
	}

	float KK_GetHoldRadius()
	{
		return m_fKK_HoldRadius;
	}

	float KK_GetReassignmentInterval()
	{
		return m_fKK_ReassignmentInterval;
	}

	float KK_GetRotateIntervalMin()
	{
		return m_fKK_RotateIntervalMin;
	}

	float KK_GetRotateIntervalMax()
	{
		return m_fKK_RotateIntervalMax;
	}

	float KK_GetGarrisonMovementTimeout()
	{
		return m_fKK_GarrisonMovementTimeout;
	}

	float KK_GetGarrisonStuckTimeout()
	{
		return m_fKK_GarrisonStuckTimeout;
	}

	int KK_GetGarrisonMaximumRetries()
	{
		return m_iKK_GarrisonMaximumRetries;
	}

	bool KK_GetGarrisonFailCluster()
	{
		return m_bKK_GarrisonFailCluster;
	}

	bool KK_GetSharpCombat()
	{
		return m_bKK_SharpCombat;
	}

	float KK_GetPerceptionFactor()
	{
		return Math.Max(m_fKK_PerceptionFactor, 0);
	}

	bool KK_GetOpenDoors()
	{
		return m_bKK_OpenDoors;
	}

	float KK_GetDoorReach()
	{
		return Math.Max(m_fKK_DoorReach, 0.25);
	}

	float KK_GetDoorSearchInterval()
	{
		return Math.Max(m_fKK_DoorSearchInterval, 0);
	}

	float KK_GetDoorSearchDistance()
	{
		return Math.Max(m_fKK_DoorSearchDistance, 0);
	}

	bool KK_GetDebugDraw()
	{
		return m_bKK_DebugDraw;
	}

	void KK_SetHorizontalSpacing(float value) { m_fKK_HorizontalSpacing = value; }
	void KK_SetVerticalSpacing(float value) { m_fKK_VerticalSpacing = value; }
	void KK_SetDeduplicateDistance(float value) { m_fKK_DeduplicateDistance = value; }
	void KK_SetClusterRadius(float value) { m_fKK_ClusterRadius = value; }
	void KK_SetFilterUnreachableIslands(bool value) { m_bKK_FilterUnreachableIslands = value; }
	void KK_SetGarrisonAfterClear(bool value) { m_bKK_GarrisonAfterClear = value; }
	void KK_SetClearSearchRadius(float value) { m_fKK_ClearSearchRadius = value; }
	void KK_SetClearArrivalRadius(float value) { m_fKK_ClearArrivalRadius = value; }
	void KK_SetSightVisitRange(float value) { m_fKK_SightVisitRange = value; }
	void KK_SetSightRetry(float value) { m_fKK_SightRetry = value; }
	void KK_SetClearMovementTimeout(float value) { m_fKK_ClearMovementTimeout = value; }
	void KK_SetClearStuckTimeout(float value) { m_fKK_ClearStuckTimeout = value; }
	void KK_SetClearMaximumRetries(int value) { m_iKK_ClearMaximumRetries = value; }
	void KK_SetClearFailCluster(bool value) { m_bKK_ClearFailCluster = value; }
	void KK_SetGarrisonSearchRadius(float value) { m_fKK_GarrisonSearchRadius = value; }
	void KK_SetGarrisonArrivalRadius(float value) { m_fKK_GarrisonArrivalRadius = value; }
	void KK_SetHoldRadius(float value) { m_fKK_HoldRadius = value; }
	void KK_SetReassignmentInterval(float value) { m_fKK_ReassignmentInterval = value; }
	void KK_SetRotateIntervalMin(float value) { m_fKK_RotateIntervalMin = value; }
	void KK_SetRotateIntervalMax(float value) { m_fKK_RotateIntervalMax = value; }
	void KK_SetGarrisonMovementTimeout(float value) { m_fKK_GarrisonMovementTimeout = value; }
	void KK_SetGarrisonStuckTimeout(float value) { m_fKK_GarrisonStuckTimeout = value; }
	void KK_SetGarrisonMaximumRetries(int value) { m_iKK_GarrisonMaximumRetries = value; }
	void KK_SetGarrisonFailCluster(bool value) { m_bKK_GarrisonFailCluster = value; }
	void KK_SetSharpCombat(bool value) { m_bKK_SharpCombat = value; }
	void KK_SetPerceptionFactor(float value) { m_fKK_PerceptionFactor = value; }
	void KK_SetOpenDoors(bool value) { m_bKK_OpenDoors = value; }
	void KK_SetDoorReach(float value) { m_fKK_DoorReach = value; }
	void KK_SetDoorSearchInterval(float value) { m_fKK_DoorSearchInterval = value; }
	void KK_SetDoorSearchDistance(float value) { m_fKK_DoorSearchDistance = value; }
	void KK_SetDebugDraw(bool value) { m_bKK_DebugDraw = value; }
}

class KK_PerceptionBoost
{
	protected static ref set<IEntity> s_ActiveSoldiers = new set<IEntity>();

	static bool IsActiveSoldier(IEntity controlledEntity)
	{
		return controlledEntity && s_ActiveSoldiers.Contains(controlledEntity);
	}

	static bool UseSharpCombat()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetSharpCombat();
	}

	static void Apply(notnull AIAgent agent, notnull map<AIAgent, float> saved)
	{
		SCR_AICombatComponent combat = Combat(agent);
		if (!combat)
			return;

		IEntity controlledEntity = agent.GetControlledEntity();
		if (controlledEntity)
			s_ActiveSoldiers.Insert(controlledEntity);

		if (!saved.Contains(agent))
			saved.Set(agent, combat.GetPerceptionFactor());

		float factor = Factor();
		if (combat.GetPerceptionFactor() != factor)
			combat.SetPerceptionFactor(factor);

		RefreshThreatPerception(agent, combat);
	}

	static void Restore(AIAgent agent, notnull map<AIAgent, float> saved)
	{
		if (!agent || !saved.Contains(agent))
			return;

		float previous = saved.Get(agent);
		saved.Remove(agent);

		IEntity controlledEntity = agent.GetControlledEntity();
		if (controlledEntity)
			s_ActiveSoldiers.RemoveItem(controlledEntity);

		SCR_AICombatComponent combat = Combat(agent);
		if (combat)
			combat.SetPerceptionFactor(previous);
	}

	static void RestoreAll(notnull map<AIAgent, float> saved)
	{
		array<AIAgent> agents = {};

		for (int i = 0; i < saved.Count(); i++)
			agents.Insert(saved.GetKey(i));

		foreach (AIAgent agent : agents)
			Restore(agent, saved);
	}

	protected static float Factor()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 1;

		return mode.KK_GetPerceptionFactor();
	}

	protected static SCR_AICombatComponent Combat(notnull AIAgent agent)
	{
		IEntity controlledEntity = agent.GetControlledEntity();
		if (!controlledEntity)
			return null;

		return SCR_AICombatComponent.Cast(
			controlledEntity.FindComponent(SCR_AICombatComponent)
		);
	}

	protected static void RefreshThreatPerception(
		notnull AIAgent agent,
		notnull SCR_AICombatComponent combat)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return;

		if (
			!soldier.m_UtilityComponent.m_PerceptionComponent ||
			!soldier.m_UtilityComponent.m_ThreatSystem
		)
			return;

		combat.UpdatePerceptionFactor(
			soldier.m_UtilityComponent.m_PerceptionComponent,
			soldier.m_UtilityComponent.m_ThreatSystem
		);
	}
}

class KK_CQBOrders
{
	static void ApplyLatestClear(IEntity groupEntity, vector position)
	{
		KK_ClearBuildingWaypoint waypoint = KK_ClearBuildingWaypoint.Cast(
			FindNearestWaypoint(groupEntity, position, true)
		);

		if (waypoint)
			waypoint.ApplyScenarioSettings();
	}

	static void ApplyLatestGarrison(IEntity groupEntity, vector position)
	{
		KK_GarrisonBuildingWaypoint waypoint =
			KK_GarrisonBuildingWaypoint.Cast(
				FindNearestWaypoint(groupEntity, position, false)
			);

		if (waypoint)
			waypoint.ApplyScenarioSettings();
	}

	protected static AIWaypoint FindNearestWaypoint(
		IEntity groupEntity,
		vector position,
		bool clearWaypoint)
	{
		SCR_AIGroup group = ResolveGroup(groupEntity);
		if (!group)
			return null;

		array<AIWaypoint> waypoints = {};
		group.GetWaypoints(waypoints);

		AIWaypoint nearest;
		float nearestDistance = 25;

		foreach (AIWaypoint waypoint : waypoints)
		{
			if (!waypoint)
				continue;

			if (clearWaypoint)
			{
				if (!KK_ClearBuildingWaypoint.Cast(waypoint))
					continue;
			}
			else if (!KK_GarrisonBuildingWaypoint.Cast(waypoint))
			{
				continue;
			}

			float distance = vector.Distance(waypoint.GetOrigin(), position);
			if (distance >= nearestDistance)
				continue;

			nearestDistance = distance;
			nearest = waypoint;
		}

		return nearest;
	}

	protected static SCR_AIGroup ResolveGroup(IEntity entity)
	{
		SCR_AIGroup group = SCR_AIGroup.Cast(entity);
		if (group)
			return group;

		if (!entity)
			return null;

		AIControlComponent control = AIControlComponent.Cast(
			entity.FindComponent(AIControlComponent)
		);

		if (!control)
			return null;

		AIAgent agent = control.GetAIAgent();
		if (!agent)
			return null;

		return SCR_AIGroup.Cast(agent.GetParentGroup());
	}
}

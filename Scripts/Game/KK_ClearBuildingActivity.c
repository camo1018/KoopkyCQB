class KK_InteriorAgentAssignment
{
	AIAgent m_Agent;
	ref KK_InteriorTarget m_Target;
	float m_fStartedAt;
	float m_fStillSince;
	float m_fLastTimerUpdate;
	float m_fLastOrderAt;
	float m_fBestDistance;
	vector m_vStillPosition;
	IEntity m_DoorEntity;

	void KK_InteriorAgentAssignment(
		notnull AIAgent agent,
		notnull KK_InteriorTarget target,
		float startedAt,
		vector startPosition)
	{
		m_Agent = agent;
		m_Target = target;
		m_fStartedAt = startedAt;
		m_fStillSince = startedAt;
		m_fLastTimerUpdate = startedAt;
		m_fLastOrderAt = startedAt;
		m_fBestDistance = vector.Distance(
			startPosition,
			target.m_vPosition
		);
		m_vStillPosition = startPosition;
	}
}

class KK_ClearBuildingActivity : SCR_AIActivityBase
{
	protected KK_ClearBuildingWaypoint m_ClearWaypoint;
	protected SCR_AIGroup m_Group;
	protected AIPathfindingComponent m_Pathfinding;

	protected BaseBuilding m_Building;
	protected ref KK_BuildingInteriorPlan m_Plan;
	protected ref array<BaseBuilding> m_aBuildingCandidates = {};
	protected int m_iBuildingCandidateIndex;
	protected int m_iNavmeshLoadAttempts;

	protected ref array<ref KK_InteriorAgentAssignment>
		m_aAssignments = {};

	protected ref map<AIAgent, int> m_mSoloHandlers =
		new map<AIAgent, int>();

	protected ref map<AIAgent, float> m_mPerceptionFactors =
		new map<AIAgent, float>();

	protected ref array<ref Shape> m_aDebugShapes = {};
	protected ref TraceParam m_SightTrace;

	protected bool m_bPlanReady;
	protected bool m_bFinished;
	protected bool m_bCancelled;

	protected float m_fLastPlanningAttempt;

	protected static const float PLANNING_INTERVAL_MS = 1000.0;
	protected static const float STILL_DISTANCE = 0.1;
	protected static const float PROGRESS_DISTANCE = 0.5;

	void KK_ClearBuildingActivity(
		SCR_AIGroupUtilityComponent utility,
		AIWaypoint relatedWaypoint)
	{
		m_ClearWaypoint =
			KK_ClearBuildingWaypoint.Cast(relatedWaypoint);
	
		m_Group = SCR_AIGroup.Cast(utility.GetAIAgent());
	
		if (m_Group)
		{
			m_Pathfinding = AIPathfindingComponent.Cast(
				m_Group.FindComponent(AIPathfindingComponent)
			);
		}
	
		m_Plan = new KK_BuildingInteriorPlan();
	
		SetPriority(
			SCR_AIActionBase.PRIORITY_LEVEL_GAMEMASTER
		);
		SetPriorityLevel(
			SCR_AIActionBase.PRIORITY_LEVEL_GAMEMASTER
		);
	}

	override void OnActionSelected()
	{
		super.OnActionSelected();
	
		// An aborted activity may remain queued until the utility
		// component finishes its current evaluation pass.
		if (
			m_bFinished ||
			m_bCancelled ||
			!m_ClearWaypoint
		)
		{
			return;
		}
	
		PrintFormat(
			"KK: Clear Building activity selected at %1",
			m_ClearWaypoint.GetOrigin()
		);
	}
	
	override float CustomEvaluate()
	{
		if (
			m_bFinished ||
			m_bCancelled ||
			!m_Group ||
			!m_ClearWaypoint
		)
		{
			return 0;
		}

		float currentTime =
			GetGame().GetWorld().GetWorldTime();

		if (!m_bPlanReady)
		{
			if (
				currentTime - m_fLastPlanningAttempt >=
				PLANNING_INTERVAL_MS
			)
			{
				m_fLastPlanningAttempt = currentTime;
				UpdatePlanning();
			}

			return GetPriority();
		}

		MarkSeenTargets(currentTime);
		EvaluateAssignments(currentTime);

		if (m_bFinished || m_bCancelled)
			return 0;

		FillAvailableAssignments(currentTime);

		if (
			m_aAssignments.IsEmpty() &&
			m_Plan.IsFinished()
		)
		{
			CompleteClear();
			return 0;
		}

#ifdef WORKBENCH
		if (m_ClearWaypoint.GetDebugDraw())
			DrawDebug();
		else
			ClearDebug();
#endif

		return GetPriority();
	}

	protected void UpdatePlanning()
	{
		if (m_aBuildingCandidates.IsEmpty())
		{
			array<BaseBuilding> found =
				KK_BuildingResolver.FindOccupiableBuildings(
					m_ClearWaypoint.GetOrigin(),
					m_ClearWaypoint.GetBuildingSearchRadius()
				);

			foreach (BaseBuilding candidate : found)
			{
				m_aBuildingCandidates.Insert(candidate);
			}

			m_iBuildingCandidateIndex = 0;
		}

		if (!m_Pathfinding)
		{
			AbortClear("group has no pathfinding component");
			return;
		}

		while (!m_bPlanReady)
		{
			if (!m_Building)
			{
				if (
					m_iBuildingCandidateIndex >=
					m_aBuildingCandidates.Count()
				)
				{
					AbortClear("no nearby building with interior");
					return;
				}

				m_Building =
					m_aBuildingCandidates[m_iBuildingCandidateIndex];

				PrintFormat(
					"KK: Clear Building selected %1",
					m_Building
				);
			}

			if (!m_Plan.EnsureNavmeshLoaded(
				m_Pathfinding,
				m_Building
			))
			{
				m_iNavmeshLoadAttempts++;

				// One wait lets a streaming tile arrive. If it still
				// is not loaded, this structure has no usable navmesh.
				if (m_iNavmeshLoadAttempts < 2)
					return;

				PrintFormat(
					"KK: Navmesh not available for %1, trying next building",
					m_Building
				);

				m_Building = null;
				m_iBuildingCandidateIndex++;
				m_iNavmeshLoadAttempts = 0;
				continue;
			}

			m_iNavmeshLoadAttempts = 0;

			if (m_Plan.Generate(
				m_Group,
				m_Building,
				m_ClearWaypoint.GetHorizontalSpacing(),
				m_ClearWaypoint.GetVerticalSpacing(),
				m_ClearWaypoint.GetDeduplicateDistance(),
				m_ClearWaypoint.GetClusterRadius(),
				GetFilterUnreachableIslands()
			))
			{
				break;
			}

			PrintFormat(
				"KK: No interior navmesh samples in %1, trying next building",
				m_Building
			);

			m_Building = null;
			m_iBuildingCandidateIndex++;
		}

		m_bPlanReady = true;

		PrintFormat(
			"KK: Clear Building plan ready with %1 targets",
			m_Plan.GetTargets().Count()
		);

		float readyTime = GetGame().GetWorld().GetWorldTime();
		MarkSeenTargets(readyTime);
		FillAvailableAssignments(readyTime);
	}

	protected void EvaluateAssignments(float currentTime)
	{
		int i = m_aAssignments.Count() - 1;

		while (i >= 0)
		{
			if (i >= m_aAssignments.Count())
			{
				i = m_aAssignments.Count() - 1;
				continue;
			}

			int countBefore = m_aAssignments.Count();
			KK_InteriorAgentAssignment assignment =
				m_aAssignments[i];

			if (
				assignment &&
				assignment.m_Agent &&
				assignment.m_Agent.GetControlledEntity()
			)
			{
				KK_PerceptionBoost.Apply(
					assignment.m_Agent,
					m_mPerceptionFactors
				);
			}

			if (
				!assignment ||
				!assignment.m_Agent ||
				!assignment.m_Agent.GetControlledEntity()
			)
			{
				FinishAssignment(i, false);
			}
			else if (
				assignment.m_Target.m_eState ==
				KK_EInteriorTargetState.VISITED
			)
			{
				FinishAssignment(i, true);
			}
			else if (
				vector.Distance(
					assignment.m_Agent.GetControlledEntity().GetOrigin(),
					assignment.m_Target.m_vPosition
				) <= m_ClearWaypoint.GetArrivalRadius()
			)
			{
				FinishAssignment(i, true);
			}
			else
			{
				float timerDelta =
					currentTime - assignment.m_fLastTimerUpdate;
				if (timerDelta < 0)
					timerDelta = 0;
				assignment.m_fLastTimerUpdate = currentTime;

				if (IsEngagingEnemy(assignment.m_Agent))
				{
					assignment.m_fStillSince = currentTime;
					assignment.m_fStartedAt += timerDelta;
				}

				SetWeaponRaised(assignment.m_Agent, true);

				vector unitPosition =
					assignment.m_Agent.GetControlledEntity().GetOrigin();

				float distanceToTarget = vector.Distance(
					unitPosition,
					assignment.m_Target.m_vPosition
				);

				if (
					distanceToTarget + PROGRESS_DISTANCE <
					assignment.m_fBestDistance
				)
				{
					assignment.m_fBestDistance = distanceToTarget;
					assignment.m_fStillSince = currentTime;
				}

				if (
					vector.Distance(
						unitPosition,
						assignment.m_vStillPosition
					) > STILL_DISTANCE
				)
				{
					assignment.m_vStillPosition = unitPosition;
					assignment.m_fStillSince = currentTime;
				}

				IEntity doorEntity = assignment.m_DoorEntity;
				bool waitingOnDoor = KK_DoorAssist.Handle(
					assignment.m_Agent,
					assignment.m_Target.m_vPosition,
					doorEntity
				);
				assignment.m_DoorEntity = doorEntity;

				if (waitingOnDoor)
				{
					assignment.m_fStillSince = currentTime;
					KK_AgentMove.SetWantedSpeed(
						assignment.m_Agent,
						EMovementType.WALK
					);

					if (!IsEngagingEnemy(assignment.m_Agent))
						assignment.m_fStartedAt += timerDelta;
				}

				if (
					currentTime - assignment.m_fStillSince >=
					m_ClearWaypoint.GetStuckTimeout() * 1000.0
				)
				{
					PrintFormat(
						"KK: Unit stood still for %1s heading to interior target %2",
						m_ClearWaypoint.GetStuckTimeout(),
						assignment.m_Target.m_vPosition
					);

					FinishAssignment(i, false);
				}
				else if (
					currentTime - assignment.m_fStartedAt >=
					m_ClearWaypoint.GetMovementTimeout() * 1000.0
				)
				{
					PrintFormat(
						"KK: Unit movement timed out at interior target %1",
						assignment.m_Target.m_vPosition
					);

					FinishAssignment(i, false);
				}
				else if (
					!waitingOnDoor &&
					currentTime - assignment.m_fLastOrderAt >=
						KK_AgentMove.REISSUE_INTERVAL_MS &&
					(
						IsEngagingEnemy(assignment.m_Agent) ||
						currentTime - assignment.m_fStillSince >=
							KK_AgentMove.REISSUE_INTERVAL_MS
					)
				)
				{
					assignment.m_fLastOrderAt = currentTime;
					IssueMoveOrder(
						assignment.m_Agent,
						assignment.m_Target.m_vPosition
					);
				}
			}

			if (m_aAssignments.Count() != countBefore)
			{
				i = m_aAssignments.Count() - 1;
				continue;
			}

			i--;
		}
	}

	protected void FillAvailableAssignments(float currentTime)
	{
		if (
			m_bCancelled ||
			m_bFinished ||
			!m_bPlanReady
		)
		{
			return;
		}

		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);

		foreach (AIAgent agent : agents)
		{
			if (
				!agent ||
				!agent.GetControlledEntity() ||
				HasAssignment(agent)
			)
			{
				continue;
			}

			KK_InteriorTarget target =
				FindSpreadTarget();

			if (!target)
				break;

			target.m_eState =
				KK_EInteriorTargetState.ACTIVE;

			KK_InteriorAgentAssignment assignment =
				new KK_InteriorAgentAssignment(
					agent,
					target,
					currentTime,
					agent.GetControlledEntity().GetOrigin()
				);

			m_aAssignments.Insert(assignment);
			KK_PerceptionBoost.Apply(agent, m_mPerceptionFactors);
			IssueMoveOrder(agent, target.m_vPosition);

			PrintFormat(
				"KK: Unit %1 investigating target %2 floor=%3 cluster=%4 attempt=%5",
				agent,
				target.m_vPosition,
				target.m_iFloor,
				target.m_iCluster,
				target.m_iRetries + 1
			);
		}
	}

	protected bool HasAssignment(notnull AIAgent agent)
	{
		foreach (
			KK_InteriorAgentAssignment assignment :
			m_aAssignments
		)
		{
			if (assignment && assignment.m_Agent == agent)
				return true;
		}

		return false;
	}

	protected KK_InteriorTarget FindSpreadTarget()
	{
		array<ref KK_InteriorTarget> targets =
			m_Plan.GetTargets();

		int lowestUnfinishedFloor = int.MAX;

		foreach (KK_InteriorTarget floorTarget : targets)
		{
			if (
				(
					floorTarget.m_eState ==
						KK_EInteriorTargetState.PENDING ||
					floorTarget.m_eState ==
						KK_EInteriorTargetState.ACTIVE
				) &&
				floorTarget.m_iFloor < lowestUnfinishedFloor
			)
			{
				lowestUnfinishedFloor = floorTarget.m_iFloor;
			}
		}

		if (lowestUnfinishedFloor == int.MAX)
			return null;

		foreach (KK_InteriorTarget spreadTarget : targets)
		{
			if (
				spreadTarget.m_eState !=
					KK_EInteriorTargetState.PENDING ||
				spreadTarget.m_iFloor != lowestUnfinishedFloor ||
				IsClusterAssigned(
					spreadTarget.m_iFloor,
					spreadTarget.m_iCluster
				)
			)
			{
				continue;
			}

			return spreadTarget;
		}

		foreach (KK_InteriorTarget fallbackTarget : targets)
		{
			if (
				fallbackTarget.m_eState ==
					KK_EInteriorTargetState.PENDING &&
				fallbackTarget.m_iFloor == lowestUnfinishedFloor
			)
			{
				return fallbackTarget;
			}
		}

		return null;
	}

	protected bool IsClusterAssigned(
		int floorIndex,
		int clusterIndex)
	{
		foreach (
			KK_InteriorAgentAssignment assignment :
			m_aAssignments
		)
		{
			if (
				assignment &&
				assignment.m_Target &&
				assignment.m_Target.m_iFloor == floorIndex &&
				assignment.m_Target.m_iCluster == clusterIndex
			)
			{
				return true;
			}
		}

		return false;
	}

	protected void IssueMoveOrder(
		notnull AIAgent agent,
		vector position)
	{
		KK_AgentMove.Issue(
			this,
			m_Group,
			agent,
			position,
			m_mSoloHandlers
		);

		SetWeaponRaised(agent, true);
		OrderWeaponRaised(agent, true);
	}

	protected void SetWeaponRaised(notnull AIAgent agent, bool raised)
	{
		IEntity controlledEntity = agent.GetControlledEntity();
		if (!controlledEntity)
			return;

		SCR_CharacterControllerComponent controller =
			SCR_CharacterControllerComponent.Cast(
				controlledEntity.FindComponent(
					SCR_CharacterControllerComponent
				)
			);

		if (!controller)
			return;

		controller.SetWeaponRaised(raised);
	}

	protected void OrderWeaponRaised(notnull AIAgent agent, bool raised)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return;

		SCR_AIOrder_WeaponRaised order = new SCR_AIOrder_WeaponRaised();
		order.m_bWeaponRaised = raised;
		order.SetReceiver(agent);

		soldier.m_UtilityComponent.m_Mailbox.RequestBroadcast(
			order,
			agent
		);
	}

	protected void LowerWeapons()
	{
		if (!m_Group)
			return;

		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);

		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			SetWeaponRaised(agent, false);
			OrderWeaponRaised(agent, false);
		}
	}

	protected bool IsEngagingEnemy(notnull AIAgent agent)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return false;

		SCR_AIThreatSystem threat = soldier.m_UtilityComponent.m_ThreatSystem;
		if (!threat)
			return false;

		EAIThreatState state = threat.GetState();
		return state == EAIThreatState.ALERTED ||
			state == EAIThreatState.THREATENED;
	}

	protected void CancelAgentOrder(notnull AIAgent agent)
	{
		SCR_AIMessage_Cancel message =
			SCR_AIMessage_Cancel.Create(this);

		message.SetReceiver(agent);

		m_Utility.m_Mailbox.RequestBroadcast(
			message,
			agent
		);
	}

	protected override void SendCancelMessagesToAllAgents()
	{
		if (m_Group)
		{
			array<AIAgent> agents = {};
			m_Group.GetAgents(agents);

			foreach (AIAgent agent : agents)
			{
				if (!agent)
					continue;

				CancelAgentOrder(agent);
			}
		}

		KK_AgentMove.ReleaseHandlers(m_Group, m_mSoloHandlers);
	}

	protected void MarkSeenTargets(float currentTime)
	{
		if (!m_Plan || !m_Group)
			return;

		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		float sightRetryMs = SightRetryMs();

		array<ref KK_InteriorTarget> targets =
			m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target.m_eState ==
					KK_EInteriorTargetState.VISITED ||
				target.m_eState ==
					KK_EInteriorTargetState.UNREACHABLE
			)
			{
				continue;
			}

			if (!IsTargetVisibleToSquad(target, agents, currentTime, sightRetryMs))
				continue;

			target.m_eState =
				KK_EInteriorTargetState.VISITED;

			PrintFormat(
				"KK: Interior target seen %1",
				target.m_vPosition
			);
		}
	}

	protected float SightRetryMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 250;

		return mode.KK_GetSightRetry() * 1000.0;
	}

	protected bool IsTargetVisibleToSquad(
		notnull KK_InteriorTarget target,
		array<AIAgent> agents,
		float currentTime,
		float sightRetryMs)
	{
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			IEntity controlledEntity =
				agent.GetControlledEntity();

			if (!controlledEntity)
				continue;

			float distance = vector.Distance(
				controlledEntity.GetOrigin(),
				target.m_vPosition
			);

			if (distance <= m_ClearWaypoint.GetArrivalRadius())
				return true;

			if (distance > GetSightVisitRange())
				continue;

			if (HasLineOfSight(agent, controlledEntity, target, currentTime, sightRetryMs))
				return true;
		}

		return false;
	}

	protected bool HasLineOfSight(
		notnull AIAgent agent,
		notnull IEntity viewer,
		notnull KK_InteriorTarget target,
		float currentTime,
		float sightRetryMs)
	{
		if (
			target.m_mSightMissAt &&
			target.m_mSightMissAt.Contains(agent) &&
			currentTime - target.m_mSightMissAt.Get(agent) < sightRetryMs
		)
		{
			return false;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		vector eyePosition = viewer.GetOrigin() + Vector(0, 1.65, 0);
		vector aimPosition = target.m_vPosition + Vector(0, 1.1, 0);

		if (!IsInFieldOfView(viewer, eyePosition, aimPosition))
			return false;

		if (!m_SightTrace)
			m_SightTrace = new TraceParam();

		m_SightTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		m_SightTrace.Exclude = viewer;
		m_SightTrace.Start = eyePosition;
		m_SightTrace.End = aimPosition;

		float result = world.TraceMove(m_SightTrace, null);
		if (result >= 0.99)
			return true;

		if (!target.m_mSightMissAt)
			target.m_mSightMissAt = new map<AIAgent, float>();

		target.m_mSightMissAt.Set(agent, currentTime);
		return false;
	}

	protected bool IsInFieldOfView(
		notnull IEntity viewer,
		vector eyePosition,
		vector aimPosition)
	{
		// 120 degree cone around the head look direction.
		const float HALF_FOV_COS = 0.5;

		vector toTarget = aimPosition - eyePosition;
		if (toTarget.Length() < 0.05)
			return true;

		toTarget.Normalize();

		vector lookDirection = GetLookDirection(viewer);
		if (lookDirection.Length() < 0.01)
			return false;

		lookDirection.Normalize();
		return vector.Dot(lookDirection, toTarget) >= HALF_FOV_COS;
	}

	protected vector GetLookDirection(notnull IEntity viewer)
	{
		CharacterControllerComponent controller =
			CharacterControllerComponent.Cast(
				viewer.FindComponent(CharacterControllerComponent)
			);

		if (controller)
		{
			CharacterHeadAimingComponent headAim =
				controller.GetHeadAimingComponent();

			if (headAim)
			{
				vector headDirection = headAim.GetAimingDirectionWorld();
				if (headDirection.Length() > 0.01)
					return headDirection;
			}
		}

		vector transform[4];
		viewer.GetWorldTransform(transform);
		return transform[2];
	}

	protected void FinishAssignment(
		int assignmentIndex,
		bool successful)
	{
		KK_InteriorAgentAssignment assignment =
			m_aAssignments[assignmentIndex];

		if (assignment && assignment.m_Agent)
		{
			KK_PerceptionBoost.Restore(
				assignment.m_Agent,
				m_mPerceptionFactors
			);
		}

		if (!assignment || !assignment.m_Target)
		{
			m_aAssignments.Remove(assignmentIndex);
			return;
		}

		KK_InteriorTarget target = assignment.m_Target;

		if (assignment.m_Agent)
			CancelAgentOrder(assignment.m_Agent);

		if (successful)
		{
			target.m_eState =
				KK_EInteriorTargetState.VISITED;
	
			PrintFormat(
				"KK: Interior target visited %1",
				target.m_vPosition
			);
		}
		else
		{
			target.m_iRetries++;
	
			if (
				target.m_iRetries >=
				m_ClearWaypoint.GetMaximumRetries()
			)
			{
				target.m_eState =
					KK_EInteriorTargetState.UNREACHABLE;
	
				PrintFormat(
					"KK: Interior target marked unreachable %1",
					target.m_vPosition
				);

				if (m_ClearWaypoint.GetFailClusterOnUnreachable())
					FailRestOfCluster(target);
			}
			else
			{
				target.m_eState =
					KK_EInteriorTargetState.PENDING;
			}
		}

		int removeIndex = m_aAssignments.Find(assignment);
		if (removeIndex >= 0)
			m_aAssignments.Remove(removeIndex);
	}

	protected void FailRestOfCluster(notnull KK_InteriorTarget failedTarget)
	{
		if (!m_Plan)
			return;

		int failedCount;
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target == failedTarget ||
				target.m_iFloor != failedTarget.m_iFloor ||
				target.m_iCluster != failedTarget.m_iCluster ||
				target.IsFinished()
			)
			{
				continue;
			}

			target.m_eState = KK_EInteriorTargetState.UNREACHABLE;
			failedCount++;
		}

		for (int i = m_aAssignments.Count() - 1; i >= 0; i--)
		{
			KK_InteriorAgentAssignment assignment = m_aAssignments[i];
			if (
				!assignment ||
				!assignment.m_Target ||
				assignment.m_Target == failedTarget ||
				assignment.m_Target.m_iFloor != failedTarget.m_iFloor ||
				assignment.m_Target.m_iCluster != failedTarget.m_iCluster
			)
			{
				continue;
			}

			if (assignment.m_Agent)
			{
				KK_PerceptionBoost.Restore(
					assignment.m_Agent,
					m_mPerceptionFactors
				);
				CancelAgentOrder(assignment.m_Agent);
			}

			m_aAssignments.Remove(i);
		}

		PrintFormat(
			"KK: Floor %1 cluster %2 failed with the unreachable node, dropped %3 other nodes",
			failedTarget.m_iFloor,
			failedTarget.m_iCluster,
			failedCount
		);
	}

	void CancelClear()
	{
		if (m_bCancelled || m_bFinished)
			return;

		m_bCancelled = true;

		SendCancelMessagesToAllAgents();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		LowerWeapons();
		ClearDebug();

		Print("KK: Clear Building activity cancelled");

		SCR_AIGroup group = m_Group;
		KK_ClearBuildingWaypoint waypoint = m_ClearWaypoint;

		Fail(true);

		if (group && waypoint)
			group.CompleteWaypoint(waypoint);
	}

	protected void CompleteClear()
	{
		if (m_bFinished)
			return;
	
		m_bFinished = true;

		SendCancelMessagesToAllAgents();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		LowerWeapons();
		ClearDebug();
	
		PrintFormat(
			"KK: Clear Building completed for %1",
			m_Building
		);
	
		SCR_AIGroup group = m_Group;
		KK_ClearBuildingWaypoint waypoint = m_ClearWaypoint;
		BaseBuilding building = m_Building;
		vector garrisonPosition;
		bool assignGarrison;

		if (building)
		{
			garrisonPosition =
				SCR_EntityHelper.GetEntityCenterWorld(building);
			assignGarrison = true;
		}

		// Count before completion. The finished clear is still in the queue.
		bool hasNextWaypoint = HasWaypointBesides(group, waypoint);
	
		// Mark the activity failed/finished before removing its waypoint,
		// preventing another utility evaluation from selecting it.
		Fail(true);
	
		if (group && waypoint)
			group.CompleteWaypoint(waypoint);

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		bool garrisonAfterClear = !mode || mode.KK_GetGarrisonAfterClear();

		if (assignGarrison && group && !hasNextWaypoint && garrisonAfterClear)
		{
			KK_BuildingOrderService.AssignGarrison(group, garrisonPosition);
		}
		else if (hasNextWaypoint)
		{
			Print("KK: Clear finished with another waypoint queued, skipping garrison");
		}
	}

	protected bool GetFilterUnreachableIslands()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return false;

		return mode.KK_GetFilterUnreachableIslands();
	}

	protected float GetSightVisitRange()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return KK_AgentMove.SIGHT_VISIT_RANGE;

		return mode.KK_GetSightVisitRange();
	}

	protected bool HasWaypointBesides(
		SCR_AIGroup group,
		AIWaypoint ignoredWaypoint)
	{
		if (!group)
			return false;

		array<AIWaypoint> waypoints = {};
		group.GetWaypoints(waypoints);

		foreach (AIWaypoint waypoint : waypoints)
		{
			if (waypoint && waypoint != ignoredWaypoint)
				return true;
		}

		return false;
	}

	protected void AbortClear(string reason)
	{
		if (m_bFinished)
			return;
	
		m_bFinished = true;
		
		SendCancelMessagesToAllAgents();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		LowerWeapons();
		ClearDebug();
	
		PrintFormat(
			"KK: Clear Building aborted: %1",
			reason
		);
	
		SCR_AIGroup group = m_Group;
		KK_ClearBuildingWaypoint waypoint = m_ClearWaypoint;
	
		Fail(true);
	
		if (group && waypoint)
			group.CompleteWaypoint(waypoint);
	}

	override void OnActionDeselected()
	{
		super.OnActionDeselected();

		if (!m_bFinished)
			CancelClear();
	}

	override void OnActionFailed()
	{
		super.OnActionFailed();

		if (!m_bFinished)
			CancelClear();
	}

	override string GetActionDebugInfo()
	{
		if (m_aAssignments.IsEmpty())
			return "KK Clear Building: planning or complete";

		return string.Format(
			"KK Clear Building: %1 active unit assignments",
			m_aAssignments.Count()
		);
	}

	protected void DrawDebug()
	{
		if (!m_Plan)
			return;

		ClearDebug();

		ShapeFlags shapeFlags =
			ShapeFlags.NOZBUFFER |
			ShapeFlags.TRANSP |
			ShapeFlags.NOOUTLINE |
			ShapeFlags.VISIBLE;

		array<ref KK_InteriorTarget> targets =
			m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (!target)
				continue;

			int color = GetDebugColor(target);
			float radius = 0.25;

			if (
				target.m_eState ==
				KK_EInteriorTargetState.ACTIVE
			)
			{
				radius = 0.4;
			}
			else if (
				target.m_eState ==
				KK_EInteriorTargetState.VISITED
			)
			{
				radius = 0.15;
			}

			vector markerPosition =
				target.m_vPosition + Vector(0, 0.35, 0);

			m_aDebugShapes.Insert(
				Shape.CreateSphere(
					color,
					shapeFlags,
					markerPosition,
					radius
				)
			);
		}

		foreach (
			KK_InteriorAgentAssignment assignment :
			m_aAssignments
		)
		{
			if (
				!assignment ||
				!assignment.m_Agent ||
				!assignment.m_Target ||
				!assignment.m_Agent.GetControlledEntity()
			)
			{
				continue;
			}

			vector unitPosition =
				assignment.m_Agent.GetControlledEntity().GetOrigin() +
				Vector(0, 1.6, 0);

			m_aDebugShapes.Insert(
				Shape.CreateArrow(
					unitPosition,
					assignment.m_Target.m_vPosition +
						Vector(0, 0.35, 0),
					0.15,
					0xFFFFFF00,
					ShapeFlags.NOZBUFFER | ShapeFlags.VISIBLE
				)
			);
		}
	}

	protected void ClearDebug()
	{
		m_aDebugShapes.Clear();
	}

	protected int GetDebugColor(notnull KK_InteriorTarget target)
	{
		if (
			target.m_eState ==
			KK_EInteriorTargetState.VISITED
		)
		{
			return 0xFF44FF44;
		}

		if (
			target.m_eState ==
			KK_EInteriorTargetState.UNREACHABLE
		)
		{
			return 0xFFFF3333;
		}

		if (
			target.m_eState ==
			KK_EInteriorTargetState.ACTIVE
		)
		{
			return 0xFFFFFF00;
		}

		if (target.m_iFloor == 1)
			return 0xFFFF66FF;

		if (target.m_iFloor >= 2)
			return 0xFFFFAA00;

		return 0xFF00DDFF;
	}
}
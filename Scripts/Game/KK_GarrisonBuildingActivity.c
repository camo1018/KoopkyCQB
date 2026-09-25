class KK_GarrisonAgentAssignment
{
	AIAgent m_Agent;
	ref KK_InteriorTarget m_Target;
	float m_fStartedAt;
	float m_fLastOrderAt;
	float m_fStillSince;
	float m_fLastTimerUpdate;
	float m_fRotateAt;
	vector m_vStillPosition;
	IEntity m_DoorEntity;
	bool m_bHolding;
	EMovementType m_eApproachSpeed;

	void KK_GarrisonAgentAssignment(
		notnull AIAgent agent,
		notnull KK_InteriorTarget target,
		float startedAt,
		vector startPosition)
	{
		m_Agent = agent;
		m_Target = target;
		m_fStartedAt = startedAt;
		m_fLastOrderAt = startedAt;
		m_fStillSince = startedAt;
		m_fLastTimerUpdate = startedAt;
		m_vStillPosition = startPosition;
		m_eApproachSpeed = EMovementType.SPRINT;
	}
}

class KK_GarrisonBuildingActivity : SCR_AIActivityBase
{
	protected KK_GarrisonBuildingWaypoint m_GarrisonWaypoint;
	protected SCR_AIGroup m_Group;
	protected AIPathfindingComponent m_Pathfinding;

	protected BaseBuilding m_Building;
	protected ref KK_BuildingInteriorPlan m_Plan;
	protected ref array<BaseBuilding> m_aBuildingCandidates = {};
	protected int m_iBuildingCandidateIndex;
	protected int m_iNavmeshLoadAttempts;

	protected ref array<ref KK_GarrisonAgentAssignment>
		m_aAssignments = {};

	protected ref map<AIAgent, int> m_mSoloHandlers =
		new map<AIAgent, int>();

	protected ref map<AIAgent, float> m_mPerceptionFactors =
		new map<AIAgent, float>();

	protected ref array<ref Shape> m_aDebugShapes = {};

	protected bool m_bPlanReady;
	protected bool m_bFinished;
	protected bool m_bCancelled;
	protected int m_iDeferPassesUsed;

	protected float m_fLastPlanningAttempt;

	protected static const float PLANNING_INTERVAL_MS = 1000.0;
	protected static const float STILL_DISTANCE = 0.1;
	// Interior floors are separated by at least 2 m, so 1 m keeps a hold on its own storey.
	protected static const float SAME_FLOOR_HEIGHT = 1.0;

	void KK_GarrisonBuildingActivity(
		SCR_AIGroupUtilityComponent utility,
		AIWaypoint relatedWaypoint)
	{
		m_GarrisonWaypoint =
			KK_GarrisonBuildingWaypoint.Cast(relatedWaypoint);

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

		if (
			m_bFinished ||
			m_bCancelled ||
			!m_GarrisonWaypoint
		)
		{
			return;
		}

		PrintFormat(
			"KK: Garrison activity selected at %1",
			m_GarrisonWaypoint.GetOrigin()
		);
	}

	override float CustomEvaluate()
	{
		if (
			m_bFinished ||
			m_bCancelled ||
			!m_Group ||
			!m_GarrisonWaypoint
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

		PruneInvalidAssignments();
		ReleaseDeferredTargets();
		MaintainAssignments(currentTime);
		FillAvailableAssignments(currentTime);

#ifdef WORKBENCH
		if (m_GarrisonWaypoint.GetDebugDraw())
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
					m_GarrisonWaypoint.GetOrigin(),
					m_GarrisonWaypoint.GetBuildingSearchRadius()
				);

			foreach (BaseBuilding candidate : found)
			{
				m_aBuildingCandidates.Insert(candidate);
			}

			m_iBuildingCandidateIndex = 0;
		}

		if (!m_Pathfinding)
		{
			AbortGarrison("group has no pathfinding component");
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
					AbortGarrison("no nearby building with interior");
					return;
				}

				m_Building =
					m_aBuildingCandidates[m_iBuildingCandidateIndex];

				PrintFormat(
					"KK: Garrison selected %1",
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
				m_GarrisonWaypoint.GetHorizontalSpacing(),
				m_GarrisonWaypoint.GetVerticalSpacing(),
				m_GarrisonWaypoint.GetDeduplicateDistance(),
				m_GarrisonWaypoint.GetClusterRadius(),
				GetFilterUnreachableIslands(),
				GetFilterBuildingSurfaces()
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
			"KK: Garrison plan ready with %1 targets",
			m_Plan.GetTargets().Count()
		);

		FillAvailableAssignments(
			GetGame().GetWorld().GetWorldTime()
		);
	}

	protected void PruneInvalidAssignments()
	{
		int i = m_aAssignments.Count() - 1;

		while (i >= 0)
		{
			KK_GarrisonAgentAssignment assignment =
				m_aAssignments[i];

			if (
				!assignment ||
				!assignment.m_Agent ||
				!assignment.m_Agent.GetControlledEntity()
			)
			{
				ReleaseAssignment(i, false);
			}

			i--;
		}
	}

	protected void MaintainAssignments(float currentTime)
	{
		float holdRadius = m_GarrisonWaypoint.GetHoldRadius();
		float intervalMs =
			m_GarrisonWaypoint.GetReassignmentInterval() * 1000.0;
		float timeoutMs =
			m_GarrisonWaypoint.GetMovementTimeout() * 1000.0;

		int i = m_aAssignments.Count() - 1;

		while (i >= 0)
		{
			// Failing a cluster removes other assignments, so the
			// index from before that removal may no longer exist.
			if (i >= m_aAssignments.Count())
			{
				i = m_aAssignments.Count() - 1;
				continue;
			}

			KK_GarrisonAgentAssignment assignment =
				m_aAssignments[i];

			if (
				!assignment ||
				!assignment.m_Agent ||
				!assignment.m_Target
			)
			{
				i--;
				continue;
			}

			IEntity controlledEntity =
				assignment.m_Agent.GetControlledEntity();

			if (!controlledEntity)
			{
				i--;
				continue;
			}

			KK_PerceptionBoost.Apply(
				assignment.m_Agent,
				m_mPerceptionFactors
			);

			vector unitPosition = controlledEntity.GetOrigin();
			bool atHold = IsAtHold(
				unitPosition,
				assignment.m_Target.m_vPosition,
				holdRadius
			);

			float timerDelta = currentTime - assignment.m_fLastTimerUpdate;
			if (timerDelta < 0)
				timerDelta = 0;
			assignment.m_fLastTimerUpdate = currentTime;

			bool attacking = IsEngagingEnemy(assignment.m_Agent);

			if (atHold)
			{
				if (!assignment.m_bHolding)
				{
					assignment.m_bHolding = true;
					assignment.m_fRotateAt = NextRotateTime(currentTime);
					CancelAgentOrder(assignment.m_Agent);
					KK_AgentMove.SetWantedSpeed(
						assignment.m_Agent,
						EMovementType.RUN
					);
				}
				else if (
					!attacking &&
					currentTime >= assignment.m_fRotateAt &&
					!RotateAssignment(assignment, currentTime, unitPosition)
				)
				{
					assignment.m_fRotateAt = NextRotateTime(currentTime);
				}

				if (!assignment.m_bHolding)
				{
					i--;
					continue;
				}

				assignment.m_fStartedAt = currentTime;
				assignment.m_fStillSince = currentTime;
				assignment.m_vStillPosition = unitPosition;
				i--;
				continue;
			}

			if (assignment.m_bHolding)
			{
				assignment.m_bHolding = false;
				assignment.m_fLastOrderAt = currentTime;
				IssueMoveOrder(assignment);
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
				KK_AgentMove.SetWantedSpeed(
					assignment.m_Agent,
					EMovementType.WALK
				);
			}
			else
			{
				EMovementType approachSpeed = GetApproachSpeed(unitPosition);
				if (approachSpeed != assignment.m_eApproachSpeed)
				{
					assignment.m_fLastOrderAt = currentTime;
					IssueMoveOrder(assignment);
				}
				else
				{
					KK_AgentMove.SetWantedSpeed(
						assignment.m_Agent,
						assignment.m_eApproachSpeed
					);
				}
			}

			if (attacking || waitingOnDoor)
			{
				assignment.m_fStillSince = currentTime;

				if (!attacking)
					assignment.m_fStartedAt += timerDelta;
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
			else if (
				currentTime - assignment.m_fStillSince >=
				m_GarrisonWaypoint.GetStuckTimeout() * 1000.0
			)
			{
				PrintFormat(
					"KK: Garrison unit stood still for %1s heading to %2",
					m_GarrisonWaypoint.GetStuckTimeout(),
					assignment.m_Target.m_vPosition
				);

				ReleaseAssignment(i, true);
				i--;
				continue;
			}

			if (
				currentTime - assignment.m_fStartedAt >=
				timeoutMs
			)
			{
				PrintFormat(
					"KK: Garrison hold timed out at %1",
					assignment.m_Target.m_vPosition
				);

				ReleaseAssignment(i, true);
				i--;
				continue;
			}

			if (
				!waitingOnDoor &&
				currentTime - assignment.m_fLastOrderAt >=
				intervalMs
			)
			{
				assignment.m_fLastOrderAt = currentTime;
				IssueMoveOrder(assignment);
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
				FindGarrisonTarget();

			if (!target)
				break;

			target.m_eState =
				KK_EInteriorTargetState.ACTIVE;

			KK_GarrisonAgentAssignment assignment =
				new KK_GarrisonAgentAssignment(
					agent,
					target,
					currentTime,
					agent.GetControlledEntity().GetOrigin()
				);

			m_aAssignments.Insert(assignment);
			KK_PerceptionBoost.Apply(
				agent,
				m_mPerceptionFactors
			);
			IssueMoveOrder(assignment);

			PrintFormat(
				"KK: Garrison unit %1 holding %2 floor=%3 cluster=%4",
				agent,
				target.m_vPosition,
				target.m_iFloor,
				target.m_iCluster
			);
		}
	}

	protected bool HasAssignment(notnull AIAgent agent)
	{
		foreach (
			KK_GarrisonAgentAssignment assignment :
			m_aAssignments
		)
		{
			if (assignment && assignment.m_Agent == agent)
				return true;
		}

		return false;
	}

	protected bool IsAtHold(
		vector unitPosition,
		vector holdPosition,
		float holdRadius)
	{
		vector flatUnit = unitPosition;
		vector flatHold = holdPosition;
		flatUnit[1] = 0;
		flatHold[1] = 0;

		if (vector.Distance(flatUnit, flatHold) > holdRadius)
			return false;

		return Math.AbsFloat(unitPosition[1] - holdPosition[1]) <=
			SAME_FLOOR_HEIGHT;
	}

	protected float NextRotateTime(float currentTime)
	{
		float minimum = m_GarrisonWaypoint.GetRotateIntervalMin();
		float maximum = m_GarrisonWaypoint.GetRotateIntervalMax();
		float span = maximum - minimum;
		float seconds = minimum;

		if (span > 0)
			seconds = minimum + (Math.RandomFloat01() * span);

		return currentTime + (seconds * 1000.0);
	}

	protected bool RotateAssignment(
		notnull KK_GarrisonAgentAssignment assignment,
		float currentTime,
		vector unitPosition)
	{
		KK_InteriorTarget previous = assignment.m_Target;
		if (!previous)
			return false;

		previous.m_eState = KK_EInteriorTargetState.PENDING;

		KK_InteriorTarget next = FindRotationTarget(previous);
		if (!next)
		{
			previous.m_eState = KK_EInteriorTargetState.ACTIVE;
			return false;
		}

		next.m_eState = KK_EInteriorTargetState.ACTIVE;
		assignment.m_Target = next;
		assignment.m_bHolding = false;
		assignment.m_fStartedAt = currentTime;
		assignment.m_fStillSince = currentTime;
		assignment.m_fLastOrderAt = currentTime;
		assignment.m_vStillPosition = unitPosition;
		IssueMoveOrder(assignment);

		PrintFormat(
			"KK: Garrison unit %1 rotating to %2 floor=%3",
			assignment.m_Agent,
			next.m_vPosition,
			next.m_iFloor
		);

		return true;
	}

	protected KK_InteriorTarget FindRotationTarget(notnull KK_InteriorTarget current)
	{
		if (!m_Plan)
			return null;

		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();
		KK_InteriorTarget farthest;
		float farthestDistance = -1;

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target == current ||
				target.m_eState != KK_EInteriorTargetState.PENDING
			)
			{
				continue;
			}

			float distance = vector.Distance(
				current.m_vPosition,
				target.m_vPosition
			);

			if (distance <= farthestDistance)
				continue;

			farthestDistance = distance;
			farthest = target;
		}

		return farthest;
	}

	protected KK_InteriorTarget FindGarrisonTarget()
	{
		int floorIndex = FindLeastOccupiedFloor();
		if (floorIndex < 0)
			return null;

		KK_InteriorTarget freeClusterTarget =
			FindFreeClusterTarget(floorIndex);

		if (freeClusterTarget)
			return freeClusterTarget;

		return FindFarthestPendingOnFloor(floorIndex);
	}

	protected int FindLeastOccupiedFloor()
	{
		int bestFloor = -1;
		int bestCount = int.MAX;

		array<ref KK_InteriorTarget> targets =
			m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target.m_eState !=
					KK_EInteriorTargetState.PENDING
			)
			{
				continue;
			}

			int assignedCount =
				CountAssignedOnFloor(target.m_iFloor);

			if (
				bestFloor >= 0 &&
				(
					assignedCount > bestCount ||
					(
						assignedCount == bestCount &&
						target.m_iFloor >= bestFloor
					)
				)
			)
			{
				continue;
			}

			bestFloor = target.m_iFloor;
			bestCount = assignedCount;
		}

		return bestFloor;
	}

	protected int CountAssignedOnFloor(int floorIndex)
	{
		int count;

		foreach (
			KK_GarrisonAgentAssignment assignment :
			m_aAssignments
		)
		{
			if (
				assignment &&
				assignment.m_Target &&
				assignment.m_Target.m_iFloor == floorIndex
			)
			{
				count++;
			}
		}

		return count;
	}

	protected KK_InteriorTarget FindFreeClusterTarget(int floorIndex)
	{
		array<ref KK_InteriorCluster> clusters =
			m_Plan.GetClusters();

		foreach (KK_InteriorCluster cluster : clusters)
		{
			if (
				!cluster ||
				cluster.m_iFloor != floorIndex ||
				!cluster.HasPendingTarget() ||
				IsClusterAssigned(
					cluster.m_iFloor,
					cluster.m_iId
				)
			)
			{
				continue;
			}

			return cluster.GetPendingTarget();
		}

		return null;
	}

	protected KK_InteriorTarget FindFarthestPendingOnFloor(int floorIndex)
	{
		array<ref KK_InteriorTarget> targets =
			m_Plan.GetTargets();

		KK_InteriorTarget farthest;
		float farthestDistance = -1;

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target.m_iFloor != floorIndex ||
				target.m_eState !=
					KK_EInteriorTargetState.PENDING
			)
			{
				continue;
			}

			float nearestAssigned =
				DistanceToNearestAssignment(target.m_vPosition);

			if (nearestAssigned <= farthestDistance)
				continue;

			farthestDistance = nearestAssigned;
			farthest = target;
		}

		return farthest;
	}

	protected float DistanceToNearestAssignment(vector position)
	{
		float nearest = float.MAX;
		bool found;

		foreach (
			KK_GarrisonAgentAssignment assignment :
			m_aAssignments
		)
		{
			if (!assignment || !assignment.m_Target)
				continue;

			float distance = vector.Distance(
				position,
				assignment.m_Target.m_vPosition
			);

			if (!found || distance < nearest)
			{
				nearest = distance;
				found = true;
			}
		}

		if (!found)
			return float.MAX;

		return nearest;
	}

	protected bool IsClusterAssigned(
		int floorIndex,
		int clusterIndex)
	{
		foreach (
			KK_GarrisonAgentAssignment assignment :
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

	protected void IssueMoveOrder(
		notnull KK_GarrisonAgentAssignment assignment)
	{
		if (!assignment.m_Agent || !assignment.m_Target)
			return;

		EMovementType speed = EMovementType.SPRINT;
		IEntity controlledEntity =
			assignment.m_Agent.GetControlledEntity();

		if (controlledEntity)
			speed = GetApproachSpeed(controlledEntity.GetOrigin());

		assignment.m_eApproachSpeed = speed;

		KK_AgentMove.Issue(
			this,
			m_Group,
			assignment.m_Agent,
			assignment.m_Target.m_vPosition,
			m_mSoloHandlers,
			KK_AgentMove.AbsolutePriorityLevel(),
			speed
		);
	}

	protected bool GetFilterUnreachableIslands()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return false;

		return mode.KK_GetFilterUnreachableIslands();
	}

	protected bool GetFilterBuildingSurfaces()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetFilterBuildingSurfaces();
	}

	protected EMovementType GetApproachSpeed(vector worldPosition)
	{
		if (IsInsideBuilding(worldPosition))
			return EMovementType.RUN;

		return EMovementType.SPRINT;
	}

	protected bool IsInsideBuilding(vector worldPosition)
	{
		if (!m_Building)
			return false;

		vector mins;
		vector maxs;
		m_Building.GetBounds(mins, maxs);

		vector local = m_Building.CoordToLocal(worldPosition);

		// Feet can sit slightly under the mesh bounds at a doorway.
		return local[0] >= mins[0] &&
			local[0] <= maxs[0] &&
			local[1] >= mins[1] - 1.0 &&
			local[1] <= maxs[1] + 1.0 &&
			local[2] >= mins[2] &&
			local[2] <= maxs[2];
	}

	protected void CancelAgentOrder(notnull AIAgent agent)
	{
		SCR_AIMessage_Cancel message =
			SCR_AIMessage_Cancel.Create(this);

		message.SetReceiver(agent);

		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (soldier && soldier.m_UtilityComponent)
		{
			soldier.m_UtilityComponent.m_Mailbox.RequestBroadcast(
				message,
				agent
			);
			return;
		}

		if (!m_Utility)
			return;

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

	protected void ReleaseAssignment(
		int assignmentIndex,
		bool timedOut)
	{
		KK_GarrisonAgentAssignment assignment =
			m_aAssignments[assignmentIndex];

		if (!assignment)
		{
			m_aAssignments.Remove(assignmentIndex);
			return;
		}

		if (assignment.m_Agent)
		{
			KK_PerceptionBoost.Restore(
				assignment.m_Agent,
				m_mPerceptionFactors
			);
			CancelAgentOrder(assignment.m_Agent);
		}

		if (assignment.m_Target)
		{
			if (timedOut)
			{
				assignment.m_Target.m_iRetries++;

				if (
					assignment.m_Target.m_iRetries <
					m_GarrisonWaypoint.GetMaximumRetries()
				)
				{
					assignment.m_Target.m_eState =
						KK_EInteriorTargetState.PENDING;
				}
				else if (
					m_iDeferPassesUsed <
					m_GarrisonWaypoint.GetDeferRetries()
				)
				{
					if (m_GarrisonWaypoint.GetFailClusterOnUnreachable())
						DeferRestOfCluster(assignment.m_Target);
					else
						DeferTarget(assignment.m_Target);
				}
				else
				{
					assignment.m_Target.m_eState =
						KK_EInteriorTargetState.UNREACHABLE;

					PrintFormat(
						"KK: Garrison hold marked unreachable %1",
						assignment.m_Target.m_vPosition
					);

					if (m_GarrisonWaypoint.GetFailClusterOnUnreachable())
						FailRestOfCluster(assignment.m_Target);
				}
			}
			else if (
				assignment.m_Target.m_eState ==
				KK_EInteriorTargetState.ACTIVE
			)
			{
				assignment.m_Target.m_eState =
					KK_EInteriorTargetState.PENDING;
			}
		}

		int removeIndex = m_aAssignments.Find(assignment);
		if (removeIndex >= 0)
			m_aAssignments.Remove(removeIndex);
	}

	protected void DeferTarget(notnull KK_InteriorTarget failedTarget)
	{
		failedTarget.m_eState = KK_EInteriorTargetState.DEFERRED;

		PrintFormat(
			"KK: Garrison hold deferred %1 floor=%2 cluster=%3",
			failedTarget.m_vPosition,
			failedTarget.m_iFloor,
			failedTarget.m_iCluster
		);
	}

	protected void DeferRestOfCluster(notnull KK_InteriorTarget failedTarget)
	{
		DeferTarget(failedTarget);

		if (!m_Plan)
			return;

		int deferredCount;
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target == failedTarget ||
				target.m_iFloor != failedTarget.m_iFloor ||
				target.m_iCluster != failedTarget.m_iCluster ||
				target.IsFinished() ||
				target.m_eState == KK_EInteriorTargetState.DEFERRED
			)
			{
				continue;
			}

			target.m_eState = KK_EInteriorTargetState.DEFERRED;
			deferredCount++;
		}

		for (int i = m_aAssignments.Count() - 1; i >= 0; i--)
		{
			KK_GarrisonAgentAssignment assignment = m_aAssignments[i];
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
			"KK: Floor %1 cluster %2 deferred with the failed node, held %3 other nodes",
			failedTarget.m_iFloor,
			failedTarget.m_iCluster,
			deferredCount
		);
	}

	protected void ReleaseDeferredTargets()
	{
		if (!m_Plan || m_Plan.HasPending() || !m_Plan.HasDeferred())
			return;

		if (m_iDeferPassesUsed < m_GarrisonWaypoint.GetDeferRetries())
		{
			int released = m_Plan.ReleaseDeferred();
			m_iDeferPassesUsed++;

			PrintFormat(
				"KK: Garrison retrying %1 deferred nodes, pass %2",
				released,
				m_iDeferPassesUsed
			);

			return;
		}

		int dropped = m_Plan.FinalizeDeferred();

		PrintFormat(
			"KK: Garrison dropped %1 deferred nodes",
			dropped
		);
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
			KK_GarrisonAgentAssignment assignment = m_aAssignments[i];
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

	void CancelGarrison()
	{
		if (m_bCancelled || m_bFinished)
			return;

		m_bCancelled = true;

		SendCancelMessagesToAllAgents();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		ClearDebug();

		Print("KK: Garrison activity cancelled");

		SCR_AIGroup group = m_Group;
		KK_GarrisonBuildingWaypoint waypoint = m_GarrisonWaypoint;

		Fail(true);

		if (group && waypoint)
			group.CompleteWaypoint(waypoint);
	}

	protected void AbortGarrison(string reason)
	{
		if (m_bFinished)
			return;

		m_bFinished = true;

		SendCancelMessagesToAllAgents();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		ClearDebug();

		PrintFormat(
			"KK: Garrison aborted: %1",
			reason
		);

		SCR_AIGroup group = m_Group;
		KK_GarrisonBuildingWaypoint waypoint = m_GarrisonWaypoint;

		Fail(true);

		if (group && waypoint)
			group.CompleteWaypoint(waypoint);
	}

	override void OnActionDeselected()
	{
		super.OnActionDeselected();

		if (!m_bFinished)
			CancelGarrison();
	}

	override void OnActionFailed()
	{
		super.OnActionFailed();

		if (!m_bFinished)
			CancelGarrison();
	}

	override string GetActionDebugInfo()
	{
		if (m_aAssignments.IsEmpty())
			return "KK Garrison: planning or holding";

		return string.Format(
			"KK Garrison: %1 soldiers holding interior positions",
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
			KK_GarrisonAgentAssignment assignment :
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
			KK_EInteriorTargetState.UNREACHABLE
		)
		{
			return 0xFFFF3333;
		}

		if (
			target.m_eState ==
			KK_EInteriorTargetState.DEFERRED
		)
		{
			return 0xFFCC66FF;
		}

		if (
			target.m_eState ==
			KK_EInteriorTargetState.ACTIVE
		)
		{
			return 0xFFFFFF00;
		}

		return 0xFF00DDFF;
	}
}

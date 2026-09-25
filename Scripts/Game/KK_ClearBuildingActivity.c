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
	bool m_bClearsPoint;

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
		m_bClearsPoint = true;
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
	protected IEntity m_SightViewer;

	protected bool m_bPlanReady;
	protected bool m_bFinished;
	protected bool m_bCancelled;
	protected int m_iDeferPassesUsed;

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

		// Arrival can put a unit on a node in this pass. Mark what
		// that position can see before the next move turns them away.
		MarkSeenTargets(currentTime);
		FillAvailableAssignments(currentTime);

		if (m_aAssignments.IsEmpty() && ReleaseDeferredTargets())
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
			"KK: Clear Building plan ready with %1 targets",
			m_Plan.GetTargets().Count()
		);

		float readyTime = GetGame().GetWorld().GetWorldTime();
		MarkSeenTargets(readyTime);
		FillAvailableAssignments(readyTime);
	}

	protected void EvaluateAssignments(float currentTime)
	{
		bool passageOn = KK_Passage.Enabled();
		ref map<AIAgent, ref KK_PassageOrder> passageOrders;
		if (passageOn)
		{
			passageOrders = new map<AIAgent, ref KK_PassageOrder>();
			array<ref KK_PassageSoldier> passageSoldiers = {};
			CollectPassageSoldiers(passageSoldiers);
			KK_Passage.Resolve(
				passageSoldiers,
				m_Pathfinding,
				passageOrders
			);
		}

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
				!assignment.m_bClearsPoint &&
				(
					assignment.m_Target.IsFinished() ||
					assignment.m_Target.m_eState ==
						KK_EInteriorTargetState.DEFERRED
				)
			)
			{
				ReleaseAssignment(i);
			}
			else if (
				assignment.m_bClearsPoint &&
				assignment.m_Target.m_eState ==
					KK_EInteriorTargetState.VISITED
			)
			{
				FinishAssignment(i, true);
			}
			else if (
				assignment.m_bClearsPoint &&
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

				KK_PassageOrder passageOrder;
				bool havePassage =
					passageOn &&
					passageOrders &&
					passageOrders.Find(assignment.m_Agent, passageOrder) &&
					passageOrder;

				bool holdingSpare =
					!assignment.m_bClearsPoint &&
					distanceToTarget <= m_ClearWaypoint.GetArrivalRadius() &&
					!(havePassage && passageOrder.m_bOverride);

				if (holdingSpare)
				{
					assignment.m_fStillSince = currentTime;
					assignment.m_fStartedAt = currentTime;
				}

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

				bool waitingOnDoor = false;
				bool passageHold = false;

				if (havePassage)
				{
					passageHold = passageOrder.m_bHoldTimers;
					if (passageOrder.m_bIssueNow)
					{
						EMovementType passageSpeed = EMovementType.RUN;
						if (passageOrder.m_bOverride)
							passageSpeed = EMovementType.WALK;

						assignment.m_fLastOrderAt = currentTime;
						IssueMoveOrder(
							assignment.m_Agent,
							passageOrder.m_vMoveTo,
							passageSpeed
						);
					}
				}
				else if (!passageOn)
				{
					IEntity doorEntity = assignment.m_DoorEntity;
					waitingOnDoor = KK_DoorAssist.Handle(
						assignment.m_Agent,
						assignment.m_Target.m_vPosition,
						doorEntity
					);
					assignment.m_DoorEntity = doorEntity;
				}

				if (passageHold || waitingOnDoor)
				{
					assignment.m_fStillSince = currentTime;

					if (waitingOnDoor)
					{
						KK_AgentMove.SetWantedSpeed(
							assignment.m_Agent,
							EMovementType.WALK
						);
					}

					if (!IsEngagingEnemy(assignment.m_Agent))
						assignment.m_fStartedAt += timerDelta;
				}

				if (
					!holdingSpare &&
					!passageHold &&
					currentTime - assignment.m_fStillSince >=
					m_ClearWaypoint.GetStuckTimeout() * 1000.0
				)
				{
					PrintFormat(
						"KK: Unit stood still for %1s heading to interior target %2",
						m_ClearWaypoint.GetStuckTimeout(),
						assignment.m_Target.m_vPosition
					);

					if (assignment.m_bClearsPoint)
						FinishAssignment(i, false);
					else
						ReleaseAssignment(i);
				}
				else if (
					!holdingSpare &&
					!passageHold &&
					currentTime - assignment.m_fStartedAt >=
					m_ClearWaypoint.GetMovementTimeout() * 1000.0
				)
				{
					PrintFormat(
						"KK: Unit movement timed out at interior target %1",
						assignment.m_Target.m_vPosition
					);

					if (assignment.m_bClearsPoint)
						FinishAssignment(i, false);
					else
						ReleaseAssignment(i);
				}
				else if (
					!havePassage &&
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

		int mode = m_ClearWaypoint.GetSpareMode();
		vector anchor = SquadAnchor();

		array<int> floors = {};
		array<int> clusters = {};
		CollectSweepRooms(floors, clusters);

		int floor = LowestUnfinishedFloor();
		if (floor == int.MAX)
			return;

		int activeIndex = FindActiveRoom(floors, clusters, floor);

		if (
			mode == KK_EClearSpareMode.FREE_PAIRS ||
			activeIndex < 0
		)
		{
			ReleaseSpares();
		}
		else
		{
			array<KK_InteriorTarget> openPoints = {};
			CollectPendingInRoom(
				floors[activeIndex],
				clusters[activeIndex],
				openPoints
			);

			if (
				!openPoints.IsEmpty() &&
				CountClearers(floors[activeIndex], clusters[activeIndex]) < 2
			)
			{
				ReleaseSparesInRoom(
					floors[activeIndex],
					clusters[activeIndex]
				);
			}

			RetargetSpares(
				mode,
				activeIndex,
				floors,
				clusters,
				anchor,
				currentTime
			);
		}

		array<AIAgent> free = {};
		CollectFreeAgents(free);

		if (mode == KK_EClearSpareMode.FREE_PAIRS)
		{
			for (int roomIndex = 0; roomIndex < clusters.Count(); roomIndex++)
			{
				if (floors[roomIndex] != floor)
					continue;

				if (!RoomNeedsClear(floors[roomIndex], clusters[roomIndex]))
					continue;

				AssignClearersToRoom(
					floors[roomIndex],
					clusters[roomIndex],
					free,
					currentTime,
					anchor
				);
			}

			return;
		}

		if (activeIndex < 0)
			return;

		AssignClearersToRoom(
			floors[activeIndex],
			clusters[activeIndex],
			free,
			currentTime,
			anchor
		);

		KK_InteriorTarget post = FindSparePost(
			mode,
			activeIndex,
			floors,
			clusters,
			anchor
		);

		if (!post)
			return;

		foreach (AIAgent spare : free)
		{
			if (!spare || !spare.GetControlledEntity())
				continue;

			AssignSpare(spare, post, currentTime);
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

	protected void CollectSweepRooms(
		notnull array<int> floors,
		notnull array<int> clusters)
	{
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (!target)
				continue;

			bool seen;

			for (int i = 0; i < clusters.Count(); i++)
			{
				if (
					floors[i] == target.m_iFloor &&
					clusters[i] == target.m_iCluster
				)
				{
					seen = true;
					break;
				}
			}

			if (seen)
				continue;

			floors.Insert(target.m_iFloor);
			clusters.Insert(target.m_iCluster);
		}
	}

	protected int LowestUnfinishedFloor()
	{
		int floor = int.MAX;
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				(
					target.m_eState != KK_EInteriorTargetState.PENDING &&
					target.m_eState != KK_EInteriorTargetState.ACTIVE
				) ||
				target.m_iFloor >= floor
			)
			{
				continue;
			}

			floor = target.m_iFloor;
		}

		return floor;
	}

	protected int FindActiveRoom(
		notnull array<int> floors,
		notnull array<int> clusters,
		int floor)
	{
		for (int i = 0; i < clusters.Count(); i++)
		{
			if (floors[i] != floor)
				continue;

			if (RoomNeedsClear(floors[i], clusters[i]))
				return i;
		}

		return -1;
	}

	protected bool RoomNeedsClear(int floor, int cluster)
	{
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target.m_iFloor != floor ||
				target.m_iCluster != cluster
			)
			{
				continue;
			}

			if (
				target.m_eState == KK_EInteriorTargetState.PENDING ||
				target.m_eState == KK_EInteriorTargetState.ACTIVE
			)
			{
				return true;
			}
		}

		return false;
	}

	protected bool RoomIsFinished(int floor, int cluster)
	{
		bool any;
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target.m_iFloor != floor ||
				target.m_iCluster != cluster
			)
			{
				continue;
			}

			any = true;

			if (!target.IsFinished())
				return false;
		}

		return any;
	}

	protected void CollectPendingInRoom(
		int floor,
		int cluster,
		notnull array<KK_InteriorTarget> outTargets)
	{
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target.m_iFloor != floor ||
				target.m_iCluster != cluster ||
				target.m_eState != KK_EInteriorTargetState.PENDING
			)
			{
				continue;
			}

			outTargets.Insert(target);
		}
	}

	protected KK_InteriorTarget FarthestFrom(
		notnull array<KK_InteriorTarget> targets,
		vector anchor)
	{
		KK_InteriorTarget best;
		float bestDistance = -1;

		foreach (KK_InteriorTarget target : targets)
		{
			if (!target)
				continue;

			float distance = vector.Distance(anchor, target.m_vPosition);
			if (distance <= bestDistance)
				continue;

			bestDistance = distance;
			best = target;
		}

		return best;
	}

	protected KK_InteriorTarget NearestTo(
		notnull array<KK_InteriorTarget> targets,
		vector anchor)
	{
		KK_InteriorTarget best;
		float bestDistance = float.MAX;

		foreach (KK_InteriorTarget target : targets)
		{
			if (!target)
				continue;

			float distance = vector.Distance(anchor, target.m_vPosition);
			if (distance >= bestDistance)
				continue;

			bestDistance = distance;
			best = target;
		}

		return best;
	}

	protected KK_InteriorTarget NearestOpenPoint(
		int floor,
		int cluster,
		vector anchor)
	{
		array<KK_InteriorTarget> open = {};
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target.m_iFloor != floor ||
				target.m_iCluster != cluster
			)
			{
				continue;
			}

			if (
				target.m_eState != KK_EInteriorTargetState.PENDING &&
				target.m_eState != KK_EInteriorTargetState.ACTIVE
			)
			{
				continue;
			}

			open.Insert(target);
		}

		return NearestTo(open, anchor);
	}

	protected KK_InteriorTarget NearestTargetInRoom(
		int floor,
		int cluster,
		vector anchor)
	{
		array<KK_InteriorTarget> room = {};
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target.m_iFloor != floor ||
				target.m_iCluster != cluster
			)
			{
				continue;
			}

			room.Insert(target);
		}

		return NearestTo(room, anchor);
	}

	protected int CountClearers(int floor, int cluster)
	{
		int count;

		foreach (KK_InteriorAgentAssignment assignment : m_aAssignments)
		{
			if (
				!assignment ||
				!assignment.m_bClearsPoint ||
				!assignment.m_Target ||
				assignment.m_Target.m_iFloor != floor ||
				assignment.m_Target.m_iCluster != cluster
			)
			{
				continue;
			}

			count++;
		}

		return count;
	}

	protected void AssignClearersToRoom(
		int floor,
		int cluster,
		notnull array<AIAgent> free,
		float currentTime,
		vector anchor)
	{
		while (CountClearers(floor, cluster) < 2)
		{
			array<KK_InteriorTarget> pending = {};
			CollectPendingInRoom(floor, cluster, pending);

			if (pending.IsEmpty())
				return;

			bool lead = CountClearers(floor, cluster) == 0;
			KK_InteriorTarget target;

			if (lead && pending.Count() > 1)
				target = FarthestFrom(pending, anchor);
			else
				target = NearestTo(pending, anchor);

			if (!target)
				return;

			KK_InteriorTarget nearPoint = NearestTo(pending, anchor);
			AIAgent agent = TakeClosestAgent(free, nearPoint.m_vPosition);
			if (!agent)
				return;

			AssignClearer(agent, target, currentTime, lead);
		}
	}

	protected KK_InteriorTarget FindSparePost(
		int mode,
		int activeIndex,
		notnull array<int> floors,
		notnull array<int> clusters,
		vector anchor)
	{
		if (mode == KK_EClearSpareMode.PREVIOUS_ROOM)
		{
			KK_InteriorTarget previous = FindPreviousRoomPost(
				activeIndex,
				floors,
				clusters,
				anchor
			);

			if (previous)
				return previous;
		}
		else if (mode == KK_EClearSpareMode.STAGE_NEXT)
		{
			KK_InteriorTarget next = FindStagePost(
				activeIndex,
				floors,
				clusters,
				anchor
			);

			if (next)
				return next;
		}

		return NearestOpenPoint(
			floors[activeIndex],
			clusters[activeIndex],
			anchor
		);
	}

	protected KK_InteriorTarget FindPreviousRoomPost(
		int activeIndex,
		notnull array<int> floors,
		notnull array<int> clusters,
		vector anchor)
	{
		int previous = -1;

		for (int i = 0; i < activeIndex; i++)
		{
			if (RoomIsFinished(floors[i], clusters[i]))
				previous = i;
		}

		if (previous < 0)
			return null;

		return NearestTargetInRoom(
			floors[previous],
			clusters[previous],
			anchor
		);
	}

	protected KK_InteriorTarget FindStagePost(
		int activeIndex,
		notnull array<int> floors,
		notnull array<int> clusters,
		vector anchor)
	{
		int floor = floors[activeIndex];

		for (int i = activeIndex + 1; i < clusters.Count(); i++)
		{
			if (floors[i] != floor)
				continue;

			array<KK_InteriorTarget> pending = {};
			CollectPendingInRoom(floors[i], clusters[i], pending);

			if (pending.IsEmpty())
				continue;

			return NearestTo(pending, anchor);
		}

		return null;
	}

	protected void RetargetSpares(
		int mode,
		int activeIndex,
		notnull array<int> floors,
		notnull array<int> clusters,
		vector anchor,
		float currentTime)
	{
		KK_InteriorTarget post = FindSparePost(
			mode,
			activeIndex,
			floors,
			clusters,
			anchor
		);

		if (!post)
		{
			ReleaseSpares();
			return;
		}

		foreach (KK_InteriorAgentAssignment assignment : m_aAssignments)
		{
			if (
				!assignment ||
				assignment.m_bClearsPoint ||
				!assignment.m_Agent ||
				!assignment.m_Agent.GetControlledEntity() ||
				assignment.m_Target == post
			)
			{
				continue;
			}

			RetargetAssignment(assignment, post, currentTime);
		}
	}

	protected void ReleaseSpares()
	{
		for (int i = m_aAssignments.Count() - 1; i >= 0; i--)
		{
			KK_InteriorAgentAssignment assignment = m_aAssignments[i];
			if (!assignment || assignment.m_bClearsPoint)
				continue;

			ReleaseAssignment(i);
		}
	}

	protected void ReleaseSparesInRoom(int floor, int cluster)
	{
		for (int i = m_aAssignments.Count() - 1; i >= 0; i--)
		{
			KK_InteriorAgentAssignment assignment = m_aAssignments[i];
			if (
				!assignment ||
				assignment.m_bClearsPoint ||
				!assignment.m_Target ||
				assignment.m_Target.m_iFloor != floor ||
				assignment.m_Target.m_iCluster != cluster
			)
			{
				continue;
			}

			ReleaseAssignment(i);
		}
	}

	protected void CollectFreeAgents(notnull array<AIAgent> free)
	{
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

			free.Insert(agent);
		}
	}

	protected AIAgent TakeClosestAgent(
		notnull array<AIAgent> agents,
		vector position)
	{
		int bestIndex = -1;
		float bestDistance = float.MAX;

		for (int i = 0; i < agents.Count(); i++)
		{
			AIAgent agent = agents[i];
			if (!agent || !agent.GetControlledEntity())
				continue;

			float distance = vector.Distance(
				agent.GetControlledEntity().GetOrigin(),
				position
			);

			if (distance >= bestDistance)
				continue;

			bestDistance = distance;
			bestIndex = i;
		}

		if (bestIndex < 0)
			return null;

		AIAgent chosen = agents[bestIndex];
		agents.Remove(bestIndex);
		return chosen;
	}

	protected void AssignClearer(
		notnull AIAgent agent,
		notnull KK_InteriorTarget target,
		float currentTime,
		bool lead)
	{
		target.m_eState = KK_EInteriorTargetState.ACTIVE;

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

		string role = "trailer";
		if (lead)
			role = "lead";

		PrintFormat(
			"KK: Unit %1 %2 target %3 floor=%4 cluster=%5 attempt=%6",
			agent,
			role,
			target.m_vPosition,
			target.m_iFloor,
			target.m_iCluster,
			target.m_iRetries + 1
		);
	}

	protected void AssignSpare(
		notnull AIAgent agent,
		notnull KK_InteriorTarget target,
		float currentTime)
	{
		KK_InteriorAgentAssignment assignment =
			new KK_InteriorAgentAssignment(
				agent,
				target,
				currentTime,
				agent.GetControlledEntity().GetOrigin()
			);

		assignment.m_bClearsPoint = false;
		m_aAssignments.Insert(assignment);
		KK_PerceptionBoost.Apply(agent, m_mPerceptionFactors);
		IssueMoveOrder(agent, target.m_vPosition);

		PrintFormat(
			"KK: Unit %1 spare target %2 floor=%3 cluster=%4",
			agent,
			target.m_vPosition,
			target.m_iFloor,
			target.m_iCluster
		);
	}

	protected void RetargetAssignment(
		notnull KK_InteriorAgentAssignment assignment,
		notnull KK_InteriorTarget target,
		float currentTime)
	{
		vector origin =
			assignment.m_Agent.GetControlledEntity().GetOrigin();

		assignment.m_Target = target;
		assignment.m_fStartedAt = currentTime;
		assignment.m_fStillSince = currentTime;
		assignment.m_fLastTimerUpdate = currentTime;
		assignment.m_fLastOrderAt = currentTime;
		assignment.m_vStillPosition = origin;
		assignment.m_fBestDistance = vector.Distance(
			origin,
			target.m_vPosition
		);
		assignment.m_DoorEntity = null;
		IssueMoveOrder(assignment.m_Agent, target.m_vPosition);

		PrintFormat(
			"KK: Unit %1 spare retarget %2 floor=%3 cluster=%4",
			assignment.m_Agent,
			target.m_vPosition,
			target.m_iFloor,
			target.m_iCluster
		);
	}

	protected vector SquadAnchor()
	{
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);

		vector sum = vector.Zero;
		int count;

		foreach (AIAgent agent : agents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			sum += agent.GetControlledEntity().GetOrigin();
			count++;
		}

		if (count == 0)
			return m_ClearWaypoint.GetOrigin();

		return sum / count;
	}

	protected void CollectPassageSoldiers(
		notnull array<ref KK_PassageSoldier> soldiers)
	{
		float arrival = m_ClearWaypoint.GetArrivalRadius();

		foreach (KK_InteriorAgentAssignment assignment : m_aAssignments)
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

			if (
				!assignment.m_bClearsPoint &&
				(
					assignment.m_Target.IsFinished() ||
					assignment.m_Target.m_eState ==
						KK_EInteriorTargetState.DEFERRED
				)
			)
			{
				continue;
			}

			if (
				assignment.m_bClearsPoint &&
				assignment.m_Target.m_eState ==
					KK_EInteriorTargetState.VISITED
			)
			{
				continue;
			}

			vector origin =
				assignment.m_Agent.GetControlledEntity().GetOrigin();
			float distance = vector.Distance(
				origin,
				assignment.m_Target.m_vPosition
			);

			if (assignment.m_bClearsPoint && distance <= arrival)
				continue;

			bool settled =
				!assignment.m_bClearsPoint &&
				distance <= arrival;

			soldiers.Insert(
				new KK_PassageSoldier(
					assignment.m_Agent,
					assignment.m_Target.m_vPosition,
					settled
				)
			);
		}
	}

	protected void IssueMoveOrder(
		notnull AIAgent agent,
		vector position,
		EMovementType movementType = EMovementType.RUN)
	{
		KK_AgentMove.Issue(
			this,
			m_Group,
			agent,
			position,
			m_mSoloHandlers,
			KK_AgentMove.PRIORITY_LEVEL,
			movementType
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
				target.m_eState == KK_EInteriorTargetState.VISITED
			)
			{
				continue;
			}

			if (!IsTargetVisibleToSquad(target, agents, currentTime, sightRetryMs))
				continue;

			if (
				!StopWhenSeen() &&
				target.m_eState == KK_EInteriorTargetState.ACTIVE
			)
			{
				continue;
			}

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
		vector aimPosition = target.m_vPosition + Vector(0, GetSightAimHeight(), 0);

		if (!IsInFieldOfView(viewer, eyePosition, aimPosition))
			return false;

		if (!m_SightTrace)
			m_SightTrace = new TraceParam();

		m_SightViewer = viewer;
		m_SightTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		m_SightTrace.Exclude = viewer;
		m_SightTrace.Start = eyePosition;
		m_SightTrace.End = aimPosition;

		float result = world.TraceMove(m_SightTrace, FilterSightTrace);
		m_SightViewer = null;
		if (result >= 0.99)
			return true;

		if (!target.m_mSightMissAt)
			target.m_mSightMissAt = new map<AIAgent, float>();

		target.m_mSightMissAt.Set(agent, currentTime);
		return false;
	}

	protected bool FilterSightTrace(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		IEntity current = entity;
		int depth;

		while (current && depth < 8)
		{
			if (current == m_SightViewer)
				return false;

			if (ChimeraCharacter.Cast(current))
				return false;

			current = current.GetParent();
			depth++;
		}

		return true;
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

	protected void ReleaseAssignment(int assignmentIndex)
	{
		KK_InteriorAgentAssignment assignment =
			m_aAssignments[assignmentIndex];

		if (assignment && assignment.m_Agent)
		{
			KK_PerceptionBoost.Restore(
				assignment.m_Agent,
				m_mPerceptionFactors
			);
			CancelAgentOrder(assignment.m_Agent);
		}

		int removeIndex = m_aAssignments.Find(assignment);
		if (removeIndex >= 0)
			m_aAssignments.Remove(removeIndex);
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
				target.m_iRetries <
				m_ClearWaypoint.GetMaximumRetries()
			)
			{
				target.m_eState =
					KK_EInteriorTargetState.PENDING;
			}
			else if (
				m_iDeferPassesUsed <
				m_ClearWaypoint.GetDeferRetries()
			)
			{
				if (m_ClearWaypoint.GetFailClusterOnUnreachable())
					DeferRestOfCluster(target);
				else
					DeferTarget(target);
			}
			else
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
		}

		int removeIndex = m_aAssignments.Find(assignment);
		if (removeIndex >= 0)
			m_aAssignments.Remove(removeIndex);
	}

	protected void DeferTarget(notnull KK_InteriorTarget failedTarget)
	{
		failedTarget.m_eState = KK_EInteriorTargetState.DEFERRED;

		PrintFormat(
			"KK: Interior target deferred %1 floor=%2 cluster=%3",
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
			"KK: Floor %1 cluster %2 deferred with the failed node, held %3 other nodes",
			failedTarget.m_iFloor,
			failedTarget.m_iCluster,
			deferredCount
		);
	}

	protected bool ReleaseDeferredTargets()
	{
		if (
			!m_Plan ||
			m_Plan.HasPendingOrActive() ||
			!m_Plan.HasDeferred()
		)
		{
			return false;
		}

		if (m_iDeferPassesUsed < m_ClearWaypoint.GetDeferRetries())
		{
			int released = m_Plan.ReleaseDeferred();
			m_iDeferPassesUsed++;

			PrintFormat(
				"KK: Clear retrying %1 deferred nodes, pass %2",
				released,
				m_iDeferPassesUsed
			);

			return true;
		}

		int dropped = m_Plan.FinalizeDeferred();

		PrintFormat(
			"KK: Clear dropped %1 deferred nodes",
			dropped
		);

		return false;
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

	protected bool GetFilterBuildingSurfaces()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetFilterBuildingSurfaces();
	}

	protected float GetSightVisitRange()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return KK_AgentMove.SIGHT_VISIT_RANGE;

		return mode.KK_GetSightVisitRange();
	}

	protected float GetSightAimHeight()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 0.5;

		return mode.KK_GetSightAimHeight();
	}

	protected bool StopWhenSeen()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetStopWhenSeen();
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
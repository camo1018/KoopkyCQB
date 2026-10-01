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
	bool m_bFacingApplied;
	bool m_bCombatOwnsWeapon;
	bool m_bWeaponUp;
	bool m_bRunSet;
	bool m_bReloadMove;
	bool m_bHadContact;
	float m_fContactSeenAt;
	vector m_vReloadGoal;
	ref array<vector> m_aRouteGoals = {};
	int m_iRouteIndex;

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

	// Agent key, world time gun ammo was first seen. -1 is still empty.
	protected ref map<AIAgent, float> m_mAmmoReleased =
		new map<AIAgent, float>();

	// Last spare node that was still a real hold. The nearest open point
	// flips between two nearby nodes as the squad center wobbles.
	protected ref map<AIAgent, ref KK_InteriorTarget> m_mSparePost =
		new map<AIAgent, ref KK_InteriorTarget>();

	protected ref map<AIAgent, vector> m_mLastMoveGoal =
		new map<AIAgent, vector>();

	protected ref map<AIAgent, float> m_mLastMoveAt =
		new map<AIAgent, float>();

	protected ref array<ref Shape> m_aDebugShapes = {};
	protected ref TraceParam m_SightTrace;
	protected IEntity m_SightViewer;

	protected bool m_bPlanReady;
	protected bool m_bFinished;
	protected bool m_bCancelled;
	protected bool m_bRetain;
	protected int m_iDeferPassesUsed;

	protected float m_fLastPlanningAttempt;

	protected static const float PLANNING_INTERVAL_MS = 1000.0;
	protected static const float STILL_DISTANCE = 0.1;
	protected static const float PROGRESS_DISTANCE = 0.5;
	// Suppression falls off over tens of seconds, and a remembered body keeps
	// the soldier alerted the whole time. That is long enough to miss the
	// node. A second covers a corner blocking the trace.
	protected static const float CONTACT_GRACE_MS = 1000;

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
		ClaimPendingBuilding();
	
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
	
		if (SCR_BaseGameMode.KK_LogEnabled())
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

		if (OrderWasReplaced())
		{
			CancelClear();
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
		KK_GarrisonHold.PollRearmReturn(
			m_mAmmoReleased,
			m_Group,
			currentTime
		);
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

	protected void ClaimPendingBuilding()
	{
		if (!m_Group || !m_ClearWaypoint)
			return;

		BaseBuilding building;
		bool locked;
		if (!KK_BuildingResolver.TakePendingOrder(m_Group, building, locked))
			return;

		m_ClearWaypoint.SetOrderBuilding(building, locked);
	}

	// Planning can begin before the aimed building is stored on the waypoint.
	protected void AlignLockedBuilding()
	{
		if (!m_ClearWaypoint || m_bPlanReady)
			return;

		if (!m_ClearWaypoint.IsOrderBuildingLocked())
			return;

		BaseBuilding lockedBuilding = m_ClearWaypoint.GetOrderBuilding();
		if (
			m_aBuildingCandidates.Count() == 1 &&
			m_aBuildingCandidates[0] == lockedBuilding
		)
			return;

		m_aBuildingCandidates.Clear();
		m_Building = null;
		m_iBuildingCandidateIndex = 0;
		m_iNavmeshLoadAttempts = 0;
	}

	protected void UpdatePlanning()
	{
		AlignLockedBuilding();

		if (m_aBuildingCandidates.IsEmpty())
		{
			// The aimed piece stays on that building. A ground click can still move on.
			KK_BuildingResolver.FillOrderCandidates(
				m_aBuildingCandidates,
				m_ClearWaypoint.GetOrderBuilding(),
				m_ClearWaypoint.IsOrderBuildingLocked(),
				m_ClearWaypoint.GetOrigin(),
				m_ClearWaypoint.GetBuildingSearchRadius()
			);

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

				if (SCR_BaseGameMode.KK_LogEnabled())
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

				if (SCR_BaseGameMode.KK_LogEnabled())
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
				m_Pathfinding,
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

			if (SCR_BaseGameMode.KK_LogEnabled())
				PrintFormat(
					"KK: No interior navmesh samples in %1, trying next building",
					m_Building
				);

			m_Building = null;
			m_iBuildingCandidateIndex++;
		}

		m_bPlanReady = true;

		if (SCR_BaseGameMode.KK_LogEnabled())
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
		bool passageOn = KK_Passage.Active();
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
			bool releaseForAmmo = false;

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

				if (m_Building)
				{
					KK_GarrisonHold.SetGarrisonBuilding(
						assignment.m_Agent.GetControlledEntity(),
						m_Building
					);
				}

				releaseForAmmo = KK_GarrisonHold.NeedsAmmoRelease(
					assignment.m_Agent.GetControlledEntity()
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
			else if (releaseForAmmo)
			{
				ReleaseForAmmo(i);
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
				KK_AuthoredRouteHelper.IsOnFinalGoal(
					assignment.m_aRouteGoals,
					assignment.m_iRouteIndex
				) &&
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

				// Alert after the body is gone is not a firefight. A living
				// target still pauses the clocks for a short grace after
				// the trace breaks. The threat bar stays up much longer
				// than that, and it must not hold the route.
				bool contact = InContact(assignment, currentTime);

				if (assignment.m_bHadContact && !contact)
				{
					assignment.m_fStillSince = currentTime;
					assignment.m_fStartedAt = currentTime;
					KK_GarrisonHold.ReleaseLatentCombat(
						assignment.m_Agent.GetControlledEntity()
					);
					IssueMoveOrder(
						assignment.m_Agent,
						AssignmentMoveGoal(assignment),
						EMovementType.RUN,
						true
					);
				}
				else if (!assignment.m_bHadContact && contact)
				{
					IssueMoveOrder(
						assignment.m_Agent,
						AssignmentMoveGoal(assignment),
						EMovementType.RUN,
						true
					);
				}

				assignment.m_bHadContact = contact;

				if (contact)
				{
					assignment.m_fStillSince = currentTime;
					assignment.m_fStartedAt += timerDelta;
					assignment.m_bFacingApplied = false;
				}
				else
				{
					assignment.m_bFacingApplied = false;
				}

				if (
					KK_Passage.Enabled() &&
					HasLivingTarget(assignment.m_Agent)
				)
				{
					if (!assignment.m_bCombatOwnsWeapon)
					{
						assignment.m_bCombatOwnsWeapon = true;
						assignment.m_bWeaponUp = false;
						SetWeaponRaised(assignment.m_Agent, false);
						OrderWeaponRaised(assignment.m_Agent, false);
					}
				}
				else if (
					KK_Passage.Enabled() &&
					assignment.m_bCombatOwnsWeapon
				)
				{
					assignment.m_bCombatOwnsWeapon = false;
					assignment.m_bWeaponUp = true;
					if (
						!KK_GarrisonHold.SprintBeforeReload(
							assignment.m_Agent.GetControlledEntity()
						) &&
						!KK_GarrisonHold.IsReloadBashing(
							assignment.m_Agent.GetControlledEntity()
						)
					)
					{
						SetWeaponRaised(assignment.m_Agent, true);
						OrderWeaponRaised(assignment.m_Agent, true);
						KK_AgentMove.SetWantedSpeed(
							assignment.m_Agent,
							EMovementType.RUN
						);
					}
				}
				else if (
					!assignment.m_bWeaponUp &&
					!KK_GarrisonHold.SprintBeforeReload(
						assignment.m_Agent.GetControlledEntity()
					) &&
					!KK_GarrisonHold.IsReloadBashing(
						assignment.m_Agent.GetControlledEntity()
					)
				)
				{
					assignment.m_bWeaponUp = true;
					SetWeaponRaised(assignment.m_Agent, true);
				}

				vector unitPosition =
					assignment.m_Agent.GetControlledEntity().GetOrigin();

				bool reloadMove = DriveClearReload(
					assignment,
					unitPosition,
					currentTime
				);
				if (assignment.m_bReloadMove && !reloadMove)
				{
					assignment.m_fLastOrderAt = currentTime;
					IssueMoveOrder(
						assignment.m_Agent,
						AssignmentMoveGoal(assignment)
					);
				}
				assignment.m_bReloadMove = reloadMove;

				if (!reloadMove)
				{
				vector moveGoal = AssignmentMoveGoal(assignment);
				bool onFinalGoal = KK_AuthoredRouteHelper.IsOnFinalGoal(
					assignment.m_aRouteGoals,
					assignment.m_iRouteIndex
				);

				bool advancedRoute = false;
				if (
					!onFinalGoal &&
					vector.Distance(unitPosition, moveGoal) <=
						m_ClearWaypoint.GetArrivalRadius()
				)
				{
					assignment.m_iRouteIndex++;
					assignment.m_fLastOrderAt = currentTime;
					assignment.m_fStillSince = currentTime;
					assignment.m_fStartedAt = currentTime;
					moveGoal = AssignmentMoveGoal(assignment);
					assignment.m_fBestDistance = vector.Distance(
						unitPosition,
						moveGoal
					);
					onFinalGoal = KK_AuthoredRouteHelper.IsOnFinalGoal(
						assignment.m_aRouteGoals,
						assignment.m_iRouteIndex
					);
					advancedRoute = true;
					IssueMoveOrder(assignment.m_Agent, moveGoal);
				}

				float distanceToTarget = vector.Distance(
					unitPosition,
					moveGoal
				);

				KK_PassageOrder passageOrder;
				bool havePassage =
					passageOn &&
					passageOrders &&
					passageOrders.Find(assignment.m_Agent, passageOrder) &&
					passageOrder;

				bool holdingSpare =
					!assignment.m_bClearsPoint &&
					onFinalGoal &&
					distanceToTarget <= m_ClearWaypoint.GetArrivalRadius() &&
					!(havePassage && passageOrder.m_bOverride);

				if (holdingSpare)
				{
					assignment.m_fStillSince = currentTime;
					assignment.m_fStartedAt = currentTime;

					if (
						contact ||
						KK_GarrisonHold.FightingInside(
							assignment.m_Agent.GetControlledEntity()
						)
					)
					{
						assignment.m_bFacingApplied = false;
					}
					else if (KK_HoldFacing.HasFacing(assignment.m_Target))
					{
						KK_HoldFacing.Apply(
							assignment.m_Agent,
							assignment.m_Target
						);
						assignment.m_bFacingApplied = true;
					}
				}
				else
				{
					assignment.m_bFacingApplied = false;
				}

				if (
					distanceToTarget + PROGRESS_DISTANCE <
					assignment.m_fBestDistance
				)
				{
					assignment.m_fBestDistance = distanceToTarget;
					assignment.m_fStillSince = currentTime;
					assignment.m_fStartedAt = currentTime;
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

				bool passageSteering = false;
				if (havePassage)
				{
					passageHold = passageOrder.m_bHoldTimers;
					passageSteering =
						passageOrder.m_bHoldTimers ||
						passageOrder.m_bOverride;
					if (passageOrder.m_bIssueNow || advancedRoute)
					{
						EMovementType passageSpeed = EMovementType.RUN;
						if (passageOrder.m_bOverride && passageOrder.m_bWalk)
							passageSpeed = EMovementType.WALK;

						assignment.m_fLastOrderAt = currentTime;
						IssueMoveOrder(
							assignment.m_Agent,
							passageOrder.m_vMoveTo,
							passageSpeed,
							true
						);
					}
				}
				else if (!passageOn)
				{
					IEntity doorEntity = assignment.m_DoorEntity;
					waitingOnDoor = KK_DoorAssist.Handle(
						assignment.m_Agent,
						moveGoal,
						doorEntity
					);
					assignment.m_DoorEntity = doorEntity;
				}

				if (passageHold || waitingOnDoor)
				{
					assignment.m_fStillSince = currentTime;

					if (waitingOnDoor)
					{
						assignment.m_bRunSet = false;
						KK_AgentMove.SetWantedSpeed(
							assignment.m_Agent,
							EMovementType.WALK
						);
					}

					if (!contact)
						assignment.m_fStartedAt += timerDelta;
				}

				bool passageWalk =
					KK_Passage.Enabled() &&
					havePassage &&
					passageOrder.m_bOverride &&
					passageOrder.m_bWalk;
				if (passageWalk)
					assignment.m_bRunSet = false;
				else if (
					KK_Passage.Enabled() &&
					!waitingOnDoor &&
					!assignment.m_bRunSet
				)
				{
					assignment.m_bRunSet = true;
					KK_AgentMove.SetWantedSpeed(
						assignment.m_Agent,
						EMovementType.RUN
					);
				}

				float stuckMs = m_ClearWaypoint.GetStuckTimeout() * 1000.0;
				float travelMs = m_ClearWaypoint.GetMovementTimeout() * 1000.0;

				if (
					!holdingSpare &&
					!passageHold &&
					stuckMs > 0 &&
					currentTime - assignment.m_fStillSince >= stuckMs
				)
				{
					if (SCR_BaseGameMode.KK_LogEnabled())
						PrintFormat(
							"KK: Unit stood still for %1s heading to interior target %2, move goal %3, %4m away",
							m_ClearWaypoint.GetStuckTimeout(),
							assignment.m_Target.m_vPosition,
							moveGoal,
							distanceToTarget
						);

					if (assignment.m_bClearsPoint)
						FinishAssignment(i, false);
					else
						ReleaseAssignment(i);
				}
				else if (
					!holdingSpare &&
					!passageHold &&
					travelMs > 0 &&
					currentTime - assignment.m_fStartedAt >= travelMs
				)
				{
					if (SCR_BaseGameMode.KK_LogEnabled())
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
					!passageSteering &&
					!waitingOnDoor &&
					!holdingSpare &&
					currentTime - assignment.m_fLastOrderAt >=
						KK_AgentMove.REISSUE_INTERVAL_MS &&
					currentTime - assignment.m_fStillSince >=
						KK_AgentMove.REISSUE_INTERVAL_MS
				)
				{
					assignment.m_fLastOrderAt = currentTime;
					IssueMoveOrder(
						assignment.m_Agent,
						AssignmentMoveGoal(assignment),
						EMovementType.RUN,
						true
					);
				}
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
			mode == KK_EClearSpareMode.SPREAD ||
			mode == KK_EClearSpareMode.SPREAD_PAIRS ||
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

		if (mode == KK_EClearSpareMode.SPREAD)
		{
			AssignSpread(free, floor, anchor, currentTime);
			return;
		}

		if (mode == KK_EClearSpareMode.SPREAD_PAIRS)
		{
			AssignSpreadPairs(
				free,
				floor,
				floors,
				clusters,
				anchor,
				currentTime
			);
			return;
		}

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

			AssignSpare(spare, StableSparePost(spare, post), currentTime);
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

	protected void AssignSpread(
		notnull array<AIAgent> free,
		int floor,
		vector anchor,
		float currentTime)
	{
		while (!free.IsEmpty())
		{
			array<KK_InteriorTarget> pending = {};
			CollectPendingOnFloor(floor, pending);

			if (pending.IsEmpty())
				return;

			array<vector> claimed = {};
			CollectAssignedTargets(floor, claimed);

			KK_InteriorTarget target = FarthestFromClaimed(
				pending,
				claimed,
				anchor
			);

			if (!target)
				return;

			AIAgent agent = TakeClosestAgent(free, target.m_vPosition);
			if (!agent)
				return;

			AssignClearer(agent, target, currentTime, "spread");
		}
	}

	protected void CollectPendingOnFloor(
		int floor,
		notnull array<KK_InteriorTarget> outTargets)
	{
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (
				!target ||
				target.m_iFloor != floor ||
				target.m_eState != KK_EInteriorTargetState.PENDING
			)
			{
				continue;
			}

			outTargets.Insert(target);
		}
	}

	protected void CollectAssignedTargets(
		int floor,
		notnull array<vector> claimed)
	{
		foreach (KK_InteriorAgentAssignment assignment : m_aAssignments)
		{
			if (
				!assignment ||
				!assignment.m_bClearsPoint ||
				!assignment.m_Target ||
				assignment.m_Target.m_iFloor != floor
			)
			{
				continue;
			}

			claimed.Insert(assignment.m_Target.m_vPosition);
		}
	}

	protected KK_InteriorTarget FarthestFromClaimed(
		notnull array<KK_InteriorTarget> pending,
		notnull array<vector> claimed,
		vector anchor)
	{
		if (claimed.IsEmpty())
			return FarthestFrom(pending, anchor);

		KK_InteriorTarget best;
		float bestSeparation = -1;

		foreach (KK_InteriorTarget target : pending)
		{
			if (!target)
				continue;

			float nearest = float.MAX;

			foreach (vector claim : claimed)
			{
				float distance = vector.Distance(claim, target.m_vPosition);
				if (distance < nearest)
					nearest = distance;
			}

			if (nearest <= bestSeparation)
				continue;

			bestSeparation = nearest;
			best = target;
		}

		return best;
	}

	protected void AssignSpreadPairs(
		notnull array<AIAgent> free,
		int floor,
		notnull array<int> floors,
		notnull array<int> clusters,
		vector anchor,
		float currentTime)
	{
		while (!free.IsEmpty())
		{
			int cluster = FindSpreadPairRoom(
				floor,
				floors,
				clusters,
				anchor
			);

			if (cluster < 0)
				return;

			array<KK_InteriorTarget> pending = {};
			CollectPendingInRoom(floor, cluster, pending);

			if (pending.IsEmpty())
				return;

			array<vector> claimed = {};
			CollectAssignedTargets(floor, claimed);

			KK_InteriorTarget leadPoint = FarthestFromClaimed(
				pending,
				claimed,
				anchor
			);

			if (!leadPoint)
				return;

			vector roomCenter = vector.Zero;

			foreach (KK_InteriorTarget point : pending)
			{
				if (!point)
					continue;

				roomCenter += point.m_vPosition;
			}

			roomCenter = roomCenter / pending.Count();

			int openSlots = 2 - CountClearers(floor, cluster);

			string leadRole = "spread lead";
			if (openSlots < 2)
				leadRole = "spread trailer";

			AIAgent lead = TakeClosestAgent(free, roomCenter);
			if (!lead)
				return;

			AssignClearer(lead, leadPoint, currentTime, leadRole);

			if (free.IsEmpty() || openSlots < 2)
				continue;

			array<KK_InteriorTarget> others = {};

			foreach (KK_InteriorTarget point : pending)
			{
				if (!point || point == leadPoint)
					continue;

				others.Insert(point);
			}

			KK_InteriorTarget partnerPoint = FarthestFrom(
				others,
				leadPoint.m_vPosition
			);

			if (!partnerPoint)
				continue;

			AIAgent partner = TakeClosestAgent(free, roomCenter);
			if (!partner)
				return;

			AssignClearer(partner, partnerPoint, currentTime, "spread trailer");
		}
	}

	protected int FindSpreadPairRoom(
		int floor,
		notnull array<int> floors,
		notnull array<int> clusters,
		vector anchor)
	{
		int bestCluster = -1;
		float bestSeparation = -1;

		for (int i = 0; i < clusters.Count(); i++)
		{
			if (floors[i] != floor)
				continue;

			int cluster = clusters[i];
			if (CountClearers(floor, cluster) >= 2)
				continue;

			array<KK_InteriorTarget> pending = {};
			CollectPendingInRoom(floor, cluster, pending);

			if (pending.IsEmpty())
				continue;

			array<vector> claimed = {};
			CollectAssignedTargets(floor, claimed);

			KK_InteriorTarget far = FarthestFromClaimed(
				pending,
				claimed,
				anchor
			);

			if (!far)
				continue;

			float separation = vector.Distance(anchor, far.m_vPosition);

			if (!claimed.IsEmpty())
			{
				separation = float.MAX;

				foreach (vector claim : claimed)
				{
					float distance = vector.Distance(claim, far.m_vPosition);
					if (distance < separation)
						separation = distance;
				}
			}

			if (separation <= bestSeparation)
				continue;

			bestSeparation = separation;
			bestCluster = cluster;
		}

		return bestCluster;
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

			string role = "trailer";
			if (lead)
				role = "lead";

			AssignClearer(agent, target, currentTime, role);
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
				!assignment.m_Agent.GetControlledEntity()
			)
			{
				continue;
			}

			if (
				assignment.m_Target == post ||
				SparePostAcceptable(assignment.m_Target, post)
			)
			{
				m_mSparePost.Set(assignment.m_Agent, assignment.m_Target);
				continue;
			}

			m_mSparePost.Set(assignment.m_Agent, post);
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
				HasAssignment(agent) ||
				m_mAmmoReleased.Contains(agent)
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

	protected vector AssignmentMoveGoal(
		notnull KK_InteriorAgentAssignment assignment)
	{
		if (!assignment.m_Target)
			return vector.Zero;

		return KK_AuthoredRouteHelper.CurrentMoveGoal(
			assignment.m_Target,
			assignment.m_aRouteGoals,
			assignment.m_iRouteIndex
		);
	}

	protected void AssignClearer(
		notnull AIAgent agent,
		notnull KK_InteriorTarget target,
		float currentTime,
		string role)
	{
		target.m_eState = KK_EInteriorTargetState.ACTIVE;

		vector origin = agent.GetControlledEntity().GetOrigin();
		KK_InteriorAgentAssignment assignment =
			new KK_InteriorAgentAssignment(
				agent,
				target,
				currentTime,
				origin
			);

		KK_AuthoredRouteHelper.BuildGoals(
			m_Plan,
			origin,
			target,
			assignment.m_aRouteGoals
		);
		assignment.m_iRouteIndex = 0;

		m_aAssignments.Insert(assignment);
		KK_PerceptionBoost.Apply(agent, m_mPerceptionFactors);
		IssueMoveOrder(agent, AssignmentMoveGoal(assignment));

		if (SCR_BaseGameMode.KK_LogEnabled())
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
		vector origin = agent.GetControlledEntity().GetOrigin();
		KK_InteriorAgentAssignment assignment =
			new KK_InteriorAgentAssignment(
				agent,
				target,
				currentTime,
				origin
			);

		assignment.m_bClearsPoint = false;
		KK_AuthoredRouteHelper.BuildGoals(
			m_Plan,
			origin,
			target,
			assignment.m_aRouteGoals
		);
		assignment.m_iRouteIndex = 0;

		m_aAssignments.Insert(assignment);
		KK_PerceptionBoost.Apply(agent, m_mPerceptionFactors);
		IssueMoveOrder(agent, AssignmentMoveGoal(assignment));

		KK_InteriorTarget previousPost;
		bool samePost =
			m_mSparePost.Find(agent, previousPost) &&
			previousPost == target;
		m_mSparePost.Set(agent, target);

		if (!samePost && SCR_BaseGameMode.KK_LogEnabled())
			PrintFormat(
				"KK: Unit %1 spare target %2 floor=%3 cluster=%4",
				agent,
				target.m_vPosition,
				target.m_iFloor,
				target.m_iCluster
			);
	}

	// Keep the node he already has when the newly chosen one is only the
	// other side of a wobbling squad center.
	protected KK_InteriorTarget StableSparePost(
		notnull AIAgent agent,
		KK_InteriorTarget desired)
	{
		KK_InteriorTarget held;
		if (!m_mSparePost.Find(agent, held) || !SparePostAcceptable(held, desired))
			return desired;

		return held;
	}

	protected bool SparePostAcceptable(
		KK_InteriorTarget held,
		KK_InteriorTarget desired)
	{
		if (
			!held ||
			held.IsFinished() ||
			held.m_eState == KK_EInteriorTargetState.DEFERRED
		)
		{
			return false;
		}

		if (!desired || held == desired)
			return true;

		if (held.m_iFloor != desired.m_iFloor)
			return false;

		if (held.m_iCluster == desired.m_iCluster)
			return true;

		return vector.Distance(held.m_vPosition, desired.m_vPosition) <= 6;
	}

	protected void RetargetAssignment(
		notnull KK_InteriorAgentAssignment assignment,
		notnull KK_InteriorTarget target,
		float currentTime)
	{
		vector origin =
			assignment.m_Agent.GetControlledEntity().GetOrigin();

		assignment.m_bFacingApplied = false;
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
		KK_AuthoredRouteHelper.BuildGoals(
			m_Plan,
			origin,
			target,
			assignment.m_aRouteGoals
		);
		assignment.m_iRouteIndex = 0;
		IssueMoveOrder(assignment.m_Agent, AssignmentMoveGoal(assignment));

		if (SCR_BaseGameMode.KK_LogEnabled())
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
			vector moveGoal = AssignmentMoveGoal(assignment);
			float distance = vector.Distance(origin, moveGoal);

			if (
				assignment.m_bClearsPoint &&
				KK_AuthoredRouteHelper.IsOnFinalGoal(
					assignment.m_aRouteGoals,
					assignment.m_iRouteIndex
				) &&
				distance <= arrival
			)
			{
				continue;
			}

			bool settled =
				!assignment.m_bClearsPoint &&
				KK_AuthoredRouteHelper.IsOnFinalGoal(
					assignment.m_aRouteGoals,
					assignment.m_iRouteIndex
				) &&
				distance <= arrival;

			soldiers.Insert(
				new KK_PassageSoldier(
					assignment.m_Agent,
					moveGoal,
					settled
				)
			);
		}
	}

	protected bool IssueReloadBash(
		notnull KK_InteriorAgentAssignment assignment,
		notnull IEntity body,
		float currentTime)
	{
		vector bashGoal;
		EMovementType bashSpeed;
		if (!KK_GarrisonHold.DriveReloadBash(
			assignment.m_Agent,
			body,
			bashGoal,
			bashSpeed
		))
		{
			return false;
		}

		KK_GarrisonHold.LogReload(body, "bash");
		KK_GarrisonHold.SetPinned(body, false);
		KK_GarrisonHold.SetReloadCover(body, false);

		bool newGoal = vector.Distance(assignment.m_vReloadGoal, bashGoal) > 0.4;
		if (
			!assignment.m_bReloadMove ||
			newGoal ||
			currentTime - assignment.m_fLastOrderAt >= 400
		)
		{
			assignment.m_vReloadGoal = bashGoal;
			assignment.m_fLastOrderAt = currentTime;
			KK_AgentMove.Issue(
				this,
				m_Group,
				assignment.m_Agent,
				bashGoal,
				m_mSoloHandlers,
				KK_AgentMove.EnterBuildingPriorityLevel(),
				bashSpeed
			);
		}
		else
		{
			KK_AgentMove.SetWantedSpeed(assignment.m_Agent, bashSpeed);
		}

		return true;
	}

	protected bool DriveClearReload(
		notnull KK_InteriorAgentAssignment assignment,
		vector unitPosition,
		float currentTime)
	{
		IEntity body = assignment.m_Agent.GetControlledEntity();
		if (!body)
			return false;

		if (IssueReloadBash(assignment, body, currentTime))
			return true;

		bool dash = KK_GarrisonHold.MustDashToReload(body);
		if (!dash)
			KK_GarrisonHold.LogReload(body, KK_GarrisonHold.ReloadSkipReason(body));
		else if (KK_GarrisonHold.SprintNodeStuck(body))
		{
			KK_GarrisonHold.LogReload(body, "node");
			KK_GarrisonHold.ClearReloadDash(body);
			KK_GarrisonHold.SetReloadCover(body, true);
			dash = false;
		}
		else if (
			KK_GarrisonHold.ReloadDashExpired(body) &&
			!KK_GarrisonHold.SprintBeforeReload(body)
		)
		{
			KK_GarrisonHold.LogReload(body, "dash-timeout");
			KK_GarrisonHold.ClearReloadDash(body);
			KK_GarrisonHold.SetReloadCover(body, true);
			dash = false;
		}

		if (dash)
		{
			vector goal;
			array<ref KK_InteriorTarget> targets;
			if (m_Plan)
				targets = m_Plan.GetTargets();

			if (!KK_GarrisonHold.KeepReloadMove(
				body,
				targets,
				unitPosition,
				goal
			))
			{
				KK_GarrisonHold.LogReload(body, "hold");
				KK_GarrisonHold.ClearReloadDash(body);
				KK_GarrisonHold.SetReloadCover(body, true);
			}
			else
			{
				EMovementType speed = KK_GarrisonHold.ReloadMoveSpeed(body);
				string choice = "break";
				if (KK_GarrisonHold.SprintBeforeReload(body))
				{
					choice = "sprint";
					KK_GarrisonHold.LowerForReloadSprint(assignment.m_Agent);
				}

				KK_GarrisonHold.LogReload(body, choice);
				KK_GarrisonHold.SetReloadCover(body, false);
				KK_GarrisonHold.SetPinned(body, false);
				assignment.m_fStillSince = currentTime;
				assignment.m_fStartedAt = currentTime;

				IEntity doorEntity = assignment.m_DoorEntity;
				bool waitingOnDoor = KK_DoorAssist.Handle(
					assignment.m_Agent,
					goal,
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

				bool newGoal =
					vector.Distance(assignment.m_vReloadGoal, goal) > 1;
				if (
					!waitingOnDoor &&
					(
						!assignment.m_bReloadMove ||
						newGoal ||
						KK_GarrisonHold.ReloadOrderDue(
							body,
							currentTime - assignment.m_fLastOrderAt
						)
					)
				)
				{
					assignment.m_vReloadGoal = goal;
					assignment.m_fLastOrderAt = currentTime;
					KK_AgentMove.Issue(
						this,
						m_Group,
						assignment.m_Agent,
						goal,
						m_mSoloHandlers,
						KK_GarrisonHold.ReloadMovePriority(body),
						speed
					);
				}
				else if (!waitingOnDoor)
				{
					KK_AgentMove.SetWantedSpeed(
						assignment.m_Agent,
						speed
					);
				}

				return true;
			}
		}

		if (KK_GarrisonHold.ShouldHoldToReload(body, true))
		{
			KK_GarrisonHold.ClearReloadDash(body);
			KK_GarrisonHold.SetPinned(body, true);
			KK_GarrisonHold.ConsiderTopOff(body);
			assignment.m_fStillSince = currentTime;
			assignment.m_fStartedAt = currentTime;
			return true;
		}

		if (assignment.m_bReloadMove)
		{
			KK_GarrisonHold.ClearReloadDash(body);
			KK_GarrisonHold.SetReloadCover(body, false);
			KK_GarrisonHold.SetPinned(body, false);
		}

		return false;
	}

	protected void IssueMoveOrder(
		notnull AIAgent agent,
		vector position,
		EMovementType movementType = EMovementType.RUN,
		bool force = false)
	{
		float now = 0;
		if (GetGame() && GetGame().GetWorld())
			now = GetGame().GetWorld().GetWorldTime();

		vector previousGoal;
		float previousAt;
		bool sameGoal =
			m_mLastMoveGoal.Find(agent, previousGoal) &&
			vector.Distance(previousGoal, position) < 0.75;
		bool recent =
			m_mLastMoveAt.Find(agent, previousAt) &&
			now - previousAt < KK_AgentMove.REISSUE_INTERVAL_MS;

		// Rebuilding the spare every pass was broadcasting a new path to the
		// same point and the soldier stuttered instead of walking it.
		// A pass-through retry is forced: the first order went out while
		// the bodies still blocked each other, and dropping this one leaves
		// him standing on the node.
		if (!force && sameGoal && recent)
		{
			KK_AgentMove.SetWantedSpeed(agent, movementType);
			return;
		}

		m_mLastMoveGoal.Set(agent, position);
		m_mLastMoveAt.Set(agent, now);

		float priority = KK_AgentMove.PRIORITY_LEVEL;
		// Attack stays selected on an unconscious body and outranks the
		// route order. With room combat on, leftover danger does the same
		// while the threat bar is still falling. Room combat off leaves
		// that fight at the normal priorities.
		bool stepOver =
			HoldsDownedTarget(agent) ||
			(
				KK_GarrisonHold.UseRoomCombat() &&
				!AgentInContact(agent, now)
			);
		if (stepOver)
			priority = KK_AgentMove.EnterBuildingPriorityLevel();

		KK_AgentMove.Issue(
			this,
			m_Group,
			agent,
			position,
			m_mSoloHandlers,
			priority,
			movementType
		);

		if (KK_Passage.Enabled() && HasLivingTarget(agent))
			return;

		SetWeaponRaised(agent, true);
		OrderWeaponRaised(agent, true);
	}

	protected void SetWeaponRaised(notnull AIAgent agent, bool raised)
	{
		IEntity controlledEntity = agent.GetControlledEntity();
		if (!controlledEntity)
			return;

		// Raising a frag restarts the throw. Room combat wants the rifle.
		if (
			KK_GarrisonHold.CombatOwnsWeapon(controlledEntity) &&
			KK_GarrisonHold.HoldingThrowable(controlledEntity)
		)
		{
			KK_GarrisonHold.ReturnToPrimary(controlledEntity);
			return;
		}

		SCR_CharacterControllerComponent controller =
			SCR_CharacterControllerComponent.Cast(
				controlledEntity.FindComponent(
					SCR_CharacterControllerComponent
				)
			);

		if (!controller)
			return;

		// A raise or lower from the move order restarts a reload already playing.
		if (KK_GarrisonHold.IsQuietReload(controlledEntity))
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

	protected bool InContact(
		notnull KK_InteriorAgentAssignment assignment,
		float currentTime)
	{
		if (!assignment.m_Agent)
			return false;

		if (HasLivingTarget(assignment.m_Agent))
		{
			assignment.m_fContactSeenAt = currentTime;
			return true;
		}

		if (!HasFightableTarget(assignment.m_Agent))
			return false;

		if (assignment.m_fContactSeenAt <= 0)
			return false;

		return currentTime - assignment.m_fContactSeenAt <= CONTACT_GRACE_MS;
	}

	protected bool AgentInContact(notnull AIAgent agent, float currentTime)
	{
		foreach (KK_InteriorAgentAssignment assignment : m_aAssignments)
		{
			if (assignment && assignment.m_Agent == agent)
				return InContact(assignment, currentTime);
		}

		return false;
	}

	protected bool HasFightableTarget(notnull AIAgent agent)
	{
		BaseTarget target = CurrentTarget(agent);
		if (!target)
			return false;

		return KK_GarrisonHold.IsFightable(target.GetTargetEntity());
	}

	protected BaseTarget CurrentTarget(notnull AIAgent agent)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return null;

		SCR_AICombatComponent combat =
			soldier.m_UtilityComponent.m_CombatComponent;

		if (!combat)
			return null;

		return combat.GetCurrentTarget();
	}

	// The selected enemy is still up and can be seen from here.
	protected bool HasLivingTarget(notnull AIAgent agent)
	{
		BaseTarget target = CurrentTarget(agent);
		if (!target)
			return false;

		IEntity enemy = target.GetTargetEntity();
		if (!enemy || !KK_GarrisonHold.IsFightable(enemy))
			return false;

		return KK_GarrisonHold.SeesTarget(
			agent.GetControlledEntity(),
			target
		);
	}

	// Attack is still holding a body that is dead or down.
	protected bool HoldsDownedTarget(notnull AIAgent agent)
	{
		BaseTarget target = CurrentTarget(agent);
		if (!target)
			return false;

		IEntity enemy = target.GetTargetEntity();
		if (!enemy)
			return false;

		return !KK_GarrisonHold.IsFightable(enemy);
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

			if (SCR_BaseGameMode.KK_LogEnabled())
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
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		vector eyePosition = viewer.GetOrigin() + Vector(0, 1.65, 0);
		vector aimPosition = target.m_vPosition + Vector(0, GetSightAimHeight(), 0);

		if (!IsInFieldOfView(viewer, eyePosition, aimPosition))
			return false;

		// Same place and the same look: a wall that blocked last time
		// still blocks. A step or a turn is a new line, so it traces again.
		if (SightMissFresh(target, agent, viewer, currentTime, sightRetryMs))
			return false;

		if (SightLineClear(world, viewer, eyePosition, aimPosition))
			return true;

		RememberSightMiss(target, agent, viewer, currentTime);
		return false;
	}

	// The center ray clips a door frame or the surface the point sits on.
	// A step to either side is still that point, and a wall blocks all three.
	protected bool SightLineClear(
		notnull BaseWorld world,
		notnull IEntity viewer,
		vector eyePosition,
		vector aimPosition)
	{
		if (SightRayClear(world, viewer, eyePosition, aimPosition))
			return true;

		vector flat = aimPosition - eyePosition;
		flat[1] = 0;
		if (flat.Length() < 0.05)
			return false;

		flat.Normalize();
		vector side = Vector(-flat[2], 0, flat[0]) * 0.25;
		if (SightRayClear(world, viewer, eyePosition, aimPosition + side))
			return true;

		return SightRayClear(world, viewer, eyePosition, aimPosition - side);
	}

	protected bool SightRayClear(
		notnull BaseWorld world,
		notnull IEntity viewer,
		vector eyePosition,
		vector aimPosition)
	{
		vector toAim = aimPosition - eyePosition;
		float distance = toAim.Length();
		if (distance < 0.05)
			return true;

		toAim = toAim * (1 / distance);

		// Start past the soldier's own frame. A ray that begins in the
		// doorway fails or passes with the animation, not the room.
		float inset = 0.4;
		if (inset > distance * 0.45)
			inset = distance * 0.45;

		float traced = distance - inset;
		vector start = eyePosition + (toAim * inset);

		if (!m_SightTrace)
			m_SightTrace = new TraceParam();

		m_SightViewer = viewer;
		m_SightTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		m_SightTrace.Exclude = viewer;
		m_SightTrace.TraceEnt = null;
		m_SightTrace.Start = start;
		m_SightTrace.End = aimPosition;

		float result = world.TraceMove(m_SightTrace, FilterSightTrace);
		m_SightViewer = null;

		// Same graze, every distance. A fraction cutoff gets stricter
		// as the soldier gets closer, so a near point flickers.
		float shortBy = (1 - result) * traced;
		return shortBy <= 0.15;
	}

	protected bool SightMissFresh(
		notnull KK_InteriorTarget target,
		notnull AIAgent agent,
		notnull IEntity viewer,
		float currentTime,
		float sightRetryMs)
	{
		if (!target.m_mSightMissAt || !target.m_mSightMissAt.Contains(agent))
			return false;

		KK_SightMiss miss = target.m_mSightMissAt.Get(agent);
		if (!miss)
			return false;

		if (currentTime - miss.m_fTime >= sightRetryMs)
			return false;

		if (vector.Distance(viewer.GetOrigin(), miss.m_vOrigin) > 0.45)
			return false;

		vector lookDirection = GetLookDirection(viewer);
		lookDirection[1] = 0;
		vector oldLook = miss.m_vLook;
		oldLook[1] = 0;
		if (lookDirection.Length() < 0.01 || oldLook.Length() < 0.01)
			return true;

		lookDirection.Normalize();
		oldLook.Normalize();
		return vector.Dot(lookDirection, oldLook) >= 0.9;
	}

	protected void RememberSightMiss(
		notnull KK_InteriorTarget target,
		notnull AIAgent agent,
		notnull IEntity viewer,
		float currentTime)
	{
		if (!target.m_mSightMissAt)
			target.m_mSightMissAt = new map<AIAgent, ref KK_SightMiss>();

		KK_SightMiss miss = target.m_mSightMissAt.Get(agent);
		if (!miss)
		{
			miss = new KK_SightMiss();
			target.m_mSightMissAt.Set(agent, miss);
		}

		miss.m_fTime = currentTime;
		miss.m_vOrigin = viewer.GetOrigin();
		miss.m_vLook = GetLookDirection(viewer);
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

			// An open door's collision often stays in the ray after the
			// panel has swung clear, so the same doorway clears or not.
			BaseDoorComponent door = BaseDoorComponent.Cast(
				current.FindComponent(BaseDoorComponent)
			);
			if (door && (door.IsOpen() || door.CanCharacterPass(0.5)))
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
		// 120 degrees horizontally. Pitch is ignored: the aim point sits
		// near the floor, and a nod was dropping points still in front.
		const float HALF_FOV_COS = 0.5;

		vector toTarget = aimPosition - eyePosition;
		toTarget[1] = 0;
		if (toTarget.Length() < 0.05)
			return true;

		toTarget.Normalize();

		vector lookDirection = GetLookDirection(viewer);
		lookDirection[1] = 0;
		if (lookDirection.Length() < 0.01)
		{
			vector transform[4];
			viewer.GetWorldTransform(transform);
			lookDirection = transform[2];
			lookDirection[1] = 0;
		}

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

	protected void ReleaseForAmmo(int assignmentIndex)
	{
		KK_InteriorAgentAssignment assignment =
			m_aAssignments[assignmentIndex];

		if (assignment && assignment.m_Agent)
		{
			IEntity body = assignment.m_Agent.GetControlledEntity();
			KK_GarrisonHold.SetPinned(body, false);
			KK_GarrisonHold.SetDoorFiring(body, false);
			KK_GarrisonHold.SetMoveFire(body, false);
			KK_GarrisonHold.SetIgnoringTargets(body, false);
			KK_GarrisonHold.ClearApproachGoal(body);
			m_mAmmoReleased.Set(assignment.m_Agent, -1);
			if (SCR_BaseGameMode.KK_LogEnabled())
			{
				PrintFormat(
					"KK: Clear unit %1 released, no gun ammo",
					assignment.m_Agent
				);
			}
		}

		if (
			assignment &&
			assignment.m_Target &&
			assignment.m_Target.m_eState == KK_EInteriorTargetState.ACTIVE
		)
		{
			assignment.m_Target.m_eState = KK_EInteriorTargetState.PENDING;
		}

		ReleaseAssignment(assignmentIndex);
	}

	protected void ReleaseAssignment(int assignmentIndex)
	{
		KK_InteriorAgentAssignment assignment =
			m_aAssignments[assignmentIndex];

		if (assignment && assignment.m_Agent)
		{
			assignment.m_bFacingApplied = false;

			KK_GarrisonHold.SetGarrisonBuilding(
				assignment.m_Agent.GetControlledEntity(),
				null
			);
			KK_PerceptionBoost.Restore(
				assignment.m_Agent,
				m_mPerceptionFactors
			);

			bool spareStillOpen =
				!assignment.m_bClearsPoint &&
				assignment.m_Target &&
				!assignment.m_Target.IsFinished() &&
				assignment.m_Target.m_eState !=
					KK_EInteriorTargetState.DEFERRED;

			if (!spareStillOpen)
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
			assignment.m_bFacingApplied = false;

			KK_GarrisonHold.SetGarrisonBuilding(
				assignment.m_Agent.GetControlledEntity(),
				null
			);
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
	
			if (SCR_BaseGameMode.KK_LogEnabled())
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

				if (SCR_BaseGameMode.KK_LogEnabled())
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

		if (SCR_BaseGameMode.KK_LogEnabled())
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

		if (SCR_BaseGameMode.KK_LogEnabled())
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

			if (SCR_BaseGameMode.KK_LogEnabled())
				PrintFormat(
					"KK: Clear retrying %1 deferred nodes, pass %2",
					released,
					m_iDeferPassesUsed
				);

			return true;
		}

		int dropped = m_Plan.FinalizeDeferred();

		if (SCR_BaseGameMode.KK_LogEnabled())
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
				KK_GarrisonHold.SetGarrisonBuilding(
					assignment.m_Agent.GetControlledEntity(),
					null
				);
				KK_PerceptionBoost.Restore(
					assignment.m_Agent,
					m_mPerceptionFactors
				);
				CancelAgentOrder(assignment.m_Agent);
			}

			m_aAssignments.Remove(i);
		}

		if (SCR_BaseGameMode.KK_LogEnabled())
			PrintFormat(
				"KK: Floor %1 cluster %2 failed with the unreachable node, dropped %3 other nodes",
				failedTarget.m_iFloor,
				failedTarget.m_iCluster,
				failedCount
			);
	}

	protected void ReleaseOrderBuildings()
	{
		foreach (KK_InteriorAgentAssignment assignment : m_aAssignments)
		{
			if (!assignment || !assignment.m_Agent)
				continue;

			KK_GarrisonHold.SetGarrisonBuilding(
				assignment.m_Agent.GetControlledEntity(),
				null
			);
		}
	}

	void CancelClear()
	{
		if (m_bCancelled || m_bFinished)
			return;

		m_bCancelled = true;

		SendCancelMessagesToAllAgents();
		ReleaseOrderBuildings();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		m_mAmmoReleased.Clear();
		LowerWeapons();
		ClearDebug();

		if (SCR_BaseGameMode.KK_LogEnabled())
			Print("KK: Clear Building activity cancelled");

		SCR_AIGroup group = m_Group;
		KK_ClearBuildingWaypoint waypoint = m_ClearWaypoint;

		Fail(true);

		if (
			!KK_CQBOrders.IsRetiringOrder() &&
			group &&
			waypoint &&
			WaypointStillAssigned()
		)
		{
			group.CompleteWaypoint(waypoint);
		}
	}

	protected void CompleteClear()
	{
		if (m_bFinished)
			return;
	
		m_bFinished = true;

		SendCancelMessagesToAllAgents();
		ReleaseOrderBuildings();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		m_mAmmoReleased.Clear();
		LowerWeapons();
		ClearDebug();
	
		if (SCR_BaseGameMode.KK_LogEnabled())
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
			KK_BuildingOrderService.AssignGarrison(
				group,
				garrisonPosition,
				building
			);
		}
		else if (hasNextWaypoint)
		{
			if (SCR_BaseGameMode.KK_LogEnabled())
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
		ReleaseOrderBuildings();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		m_mAmmoReleased.Clear();
		LowerWeapons();
		ClearDebug();
	
		if (SCR_BaseGameMode.KK_LogEnabled())
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

	// Drops this activity without completing its waypoint, so a restart
	// can build a new one for the squad as it is now.
	void Supersede()
	{
		if (m_bCancelled || m_bFinished)
			return;

		m_bFinished = true;
		SendCancelMessagesToAllAgents();
		ReleaseOrderBuildings();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		m_mAmmoReleased.Clear();
		LowerWeapons();
		ClearDebug();
		SetActionState(EAIActionState.FAILED);
		SetRemoveAction(true);
	}

	// True while this order should keep running. Joining soldiers restart
	// the group activity and the waypoint tree; that is not a cancel.
	bool IsLive()
	{
		if (m_bCancelled || m_bFinished || !m_ClearWaypoint)
			return false;

		EAIActionState state = GetActionState();
		return state != EAIActionState.FAILED &&
			state != EAIActionState.COMPLETED;
	}

	protected bool WaypointStillAssigned()
	{
		if (!m_Group || !m_ClearWaypoint)
			return false;

		array<AIWaypoint> waypoints = {};
		m_Group.GetWaypoints(waypoints);

		foreach (AIWaypoint waypoint : waypoints)
		{
			if (waypoint == m_ClearWaypoint)
				return true;
		}

		return false;
	}

	protected bool OrderWasReplaced()
	{
		return m_ClearWaypoint && m_ClearWaypoint.IsReplaced();
	}

	protected void RetainAfterRestart()
	{
		m_bRetain = true;
		SetActionState(EAIActionState.EVALUATED);
		SetRemoveAction(false);

		if (SCR_BaseGameMode.KK_LogEnabled())
			Print("KK: Clear Building kept running after the group activity restarted");
	}

	override void OnSetActionState(EAIActionState state)
	{
		super.OnSetActionState(state);

		if (!m_bRetain || m_bCancelled || m_bFinished)
			return;

		if (
			state != EAIActionState.FAILED &&
			state != EAIActionState.COMPLETED
		)
		{
			return;
		}

		m_bRetain = false;
		SetActionState(EAIActionState.EVALUATED);
		SetRemoveAction(false);
	}

	override void OnActionDeselected()
	{
		super.OnActionDeselected();

		if (m_bFinished || m_bCancelled)
			return;

		if (!OrderWasReplaced() && WaypointStillAssigned())
		{
			RetainAfterRestart();
			return;
		}

		CancelClear();
	}

	override void OnActionFailed()
	{
		super.OnActionFailed();

		if (m_bFinished || m_bCancelled)
			return;

		if (!OrderWasReplaced() && WaypointStillAssigned())
		{
			RetainAfterRestart();
			return;
		}

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
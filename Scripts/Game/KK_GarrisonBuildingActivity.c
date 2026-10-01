class KK_GarrisonAgentAssignment
{
	AIAgent m_Agent;
	// Kept after a knockout. The agent drops the body, and the speed lock is on it.
	IEntity m_Body;
	ref KK_InteriorTarget m_Target;
	float m_fStartedAt;
	float m_fLastOrderAt;
	float m_fStillSince;
	float m_fLastTimerUpdate;
	float m_fRotateAt;
	vector m_vStillPosition;
	IEntity m_DoorEntity;
	bool m_bHolding;
	bool m_bRotating;
	bool m_bCombatYield;
	bool m_bFacingApplied;
	bool m_bCombatMove;
	bool m_bEntryPriority;
	bool m_bCanFight;
	bool m_bPassageStep;
	bool m_bInteriorHold;
	bool m_bBounding;
	bool m_bHeldDoor;
	bool m_bReloadMove;
	vector m_vReloadGoal;
	float m_fFireUntil;
	float m_fBoundUntil;
	float m_fSteadyUntil;
	EMovementType m_eApproachSpeed;
	ref array<vector> m_aRouteGoals = {};
	int m_iRouteIndex;

	void KK_GarrisonAgentAssignment(
		notnull AIAgent agent,
		notnull KK_InteriorTarget target,
		float startedAt,
		vector startPosition)
	{
		m_Agent = agent;
		m_Body = agent.GetControlledEntity();
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

	// Agent key, world time gun ammo was first seen. -1 is still empty.
	protected ref map<AIAgent, float> m_mAmmoReleased =
		new map<AIAgent, float>();

	protected ref array<ref Shape> m_aDebugShapes = {};

	protected bool m_bPlanReady;
	protected bool m_bFinished;
	protected bool m_bCancelled;
	protected bool m_bRetain;

	protected float m_fLastPlanningAttempt;

	protected static const float PLANNING_INTERVAL_MS = 1000.0;
	protected static const float STILL_DISTANCE = 0.1;
	// Interior floors are separated by at least 2 m, so 1 m keeps a hold on its own storey.
	protected static const float SAME_FLOOR_HEIGHT = 1.0;
	// Long enough to fire, short enough that the fight still closes on the building.
	protected static const float FIGHT_WINDOW_MS = 4000.0;
	protected static const float FIGHT_BOUND_MS = 3000.0;

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

		if (SCR_BaseGameMode.KK_LogEnabled())
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
		MaintainAssignments(currentTime);
		KK_GarrisonHold.PollRearmReturn(
			m_mAmmoReleased,
			m_Group,
			currentTime
		);
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

				if (SCR_BaseGameMode.KK_LogEnabled())
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
				m_GarrisonWaypoint.GetHorizontalSpacing(),
				m_GarrisonWaypoint.GetVerticalSpacing(),
				m_GarrisonWaypoint.GetDeduplicateDistance(),
				m_GarrisonWaypoint.GetClusterRadius(),
				GetFilterUnreachableIslands(),
				GetFilterBuildingSurfaces(),
				m_GarrisonWaypoint.GetClassifyOpenings()
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

			if (!assignment || !assignment.m_Agent)
			{
				ReleaseAssignment(i, false);
				i--;
				continue;
			}

			IEntity body = assignment.m_Agent.GetControlledEntity();
			if (body)
				assignment.m_Body = body;
			else
				body = assignment.m_Body;

			// Knocked out, the agent drops the body. Keep the hold so a
			// cancel can still clear the speed lock, and so he resumes
			// if he wakes under this garrison.
			if (
				!assignment.m_Agent.GetControlledEntity() &&
				KK_GarrisonHold.IsDown(body)
			)
			{
				// The travel clock keeps running while he is down. Without
				// this he is dropped the moment he wakes.
				float now = GetGame().GetWorld().GetWorldTime();
				assignment.m_fStartedAt = now;
				assignment.m_fStillSince = now;
				assignment.m_fLastOrderAt = now;
				i--;
				continue;
			}

			if (!assignment.m_Agent.GetControlledEntity())
				ReleaseAssignment(i, false);

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

			if (m_Building)
			{
				KK_GarrisonHold.SetGarrisonBuilding(
					controlledEntity,
					m_Building
				);
			}

			if (KK_GarrisonHold.NeedsAmmoRelease(controlledEntity))
			{
				ReleaseForAmmo(i);
				i--;
				continue;
			}

			vector unitPosition = controlledEntity.GetOrigin();
			vector moveGoal = AssignmentMoveGoal(assignment);
			bool onFinalGoal = KK_AuthoredRouteHelper.IsOnFinalGoal(
				assignment.m_aRouteGoals,
				assignment.m_iRouteIndex
			);

			bool advancedRoute = false;
			// A fire window, a steady shot, or a door hold is him standing,
			// not arriving. Advancing here would walk the route ahead of him.
			if (
				!onFinalGoal &&
				!KK_GarrisonHold.PausesRoute(controlledEntity) &&
				assignment.m_fFireUntil <= currentTime &&
				assignment.m_fSteadyUntil <= currentTime &&
				!assignment.m_bHeldDoor
			)
			{
				if (
					vector.Distance(unitPosition, moveGoal) <=
					holdRadius
				)
				{
					assignment.m_iRouteIndex++;
					assignment.m_fLastOrderAt = currentTime;
					assignment.m_fStillSince = currentTime;
					assignment.m_fStartedAt = currentTime;
					advancedRoute = true;
					IssueMoveOrder(assignment, assignment.m_bRotating);
				}
			}

			bool insideBuilding = IsInsideBuilding(unitPosition);
			bool atHold =
				insideBuilding &&
				onFinalGoal &&
				IsAtHold(
					unitPosition,
					assignment.m_Target.m_vPosition,
					HoldRadiusFor(assignment.m_Target)
				);

			KK_PassageOrder passageOrder;
			bool havePassage =
				passageOn &&
				passageOrders &&
				passageOrders.Find(assignment.m_Agent, passageOrder) &&
				passageOrder;
			bool passageOverride = havePassage && passageOrder.m_bOverride;

			float timerDelta = currentTime - assignment.m_fLastTimerUpdate;
			if (timerDelta < 0)
				timerDelta = 0;
			assignment.m_fLastTimerUpdate = currentTime;

			bool attacking = IsEngagingEnemy(assignment.m_Agent);

			// The move stays under the attack. In contact he shoots, then runs
			// one leg of the route, and repeats until he is on the post.
			bool settledAtPost = atHold && !passageOverride;
			bool passageHold = false;
			bool waitingOnDoor = false;

			if (!insideBuilding)
			{
				IEntity doorEntity = assignment.m_DoorEntity;
				waitingOnDoor = KK_DoorAssist.Handle(
					assignment.m_Agent,
					AssignmentMoveGoal(assignment),
					doorEntity
				);
				assignment.m_DoorEntity = doorEntity;
			}
			else if (havePassage)
			{
				passageHold = passageOrder.m_bHoldTimers;
			}
			else if (!passageOn)
			{
				IEntity doorEntity = assignment.m_DoorEntity;
				waitingOnDoor = KK_DoorAssist.Handle(
					assignment.m_Agent,
					AssignmentMoveGoal(assignment),
					doorEntity
				);
				assignment.m_DoorEntity = doorEntity;
			}

			if (assignment.m_bCombatYield && !passageOverride)
			{
				assignment.m_bCombatYield = false;
				assignment.m_fLastOrderAt = currentTime;
				IssueMoveOrder(assignment, true);
			}

			// The sidestep is over. Send him to the post instead of leaving
			// him where the door moved him.
			if (assignment.m_bPassageStep && !passageOverride && !settledAtPost)
			{
				assignment.m_fLastOrderAt = currentTime;
				IssueMoveOrder(assignment, true);
			}

			assignment.m_bPassageStep = passageOverride;

			KK_GarrisonHold.SetGarrisonBuilding(controlledEntity, m_Building);
			KK_GarrisonHold.SetIgnoringTargets(controlledEntity, false);
			KK_GarrisonHold.SetMoveFire(controlledEntity, false);
			KK_GarrisonHold.SetDoorFiring(controlledEntity, false);
			KK_GarrisonHold.ClearApproachGoal(controlledEntity);

			if (!settledAtPost)
			{
				KK_GarrisonHold.SetPinned(controlledEntity, false);
				KK_GarrisonHold.SetTraveling(controlledEntity, false);

				if (!passageOverride && !attacking)
				{
					KK_AgentMove.SetWantedSpeed(
						assignment.m_Agent,
						GetApproachSpeed(unitPosition)
					);
				}
			}
			else
			{
				KK_GarrisonHold.SetTraveling(controlledEntity, false);
				KK_GarrisonHold.ClearApproachGoal(controlledEntity);
			}

			bool reloadMove = DriveGarrisonReload(
				assignment,
				controlledEntity,
				unitPosition,
				currentTime,
				atHold
			);
			if (assignment.m_bReloadMove && !reloadMove && !atHold)
			{
				assignment.m_fLastOrderAt = currentTime;
				IssueMoveOrder(assignment, true, assignment.m_bCanFight);
			}
			assignment.m_bReloadMove = reloadMove;
			if (reloadMove)
			{
				i--;
				continue;
			}

			if (atHold && !passageOverride)
			{
				if (!assignment.m_bHolding)
				{
					assignment.m_bHolding = true;
					assignment.m_bRotating = false;
					assignment.m_bCombatYield = false;
					assignment.m_bInteriorHold = false;
					assignment.m_fRotateAt = NextRotateTime(currentTime);
					assignment.m_bFacingApplied = false;

					assignment.m_bEntryPriority = false;

					if (!attacking)
					{
						KK_AgentMove.SetWantedSpeed(
							assignment.m_Agent,
							EMovementType.RUN
						);
					}
				}

				bool indoorFight =
					KK_GarrisonHold.FightingInside(controlledEntity);
				if (attacking || indoorFight)
					assignment.m_bFacingApplied = false;

				if (
					!attacking &&
					!indoorFight &&
					KK_HoldFacing.HasFacing(assignment.m_Target)
				)
				{
					KK_HoldFacing.Apply(
						assignment.m_Agent,
						assignment.m_Target
					);
					assignment.m_bFacingApplied = true;
				}

				bool allowRotate =
					!attacking ||
					m_GarrisonWaypoint.GetRotateDuringCombat();

				if (KK_GarrisonHold.ShouldHoldToReload(controlledEntity, true))
					allowRotate = false;

				if (
					allowRotate &&
					currentTime >= assignment.m_fRotateAt &&
					!RotateAssignment(assignment, currentTime, unitPosition)
				)
				{
					assignment.m_fRotateAt = NextRotateTime(currentTime);
				}

				if (!assignment.m_bHolding)
				{
					KK_GarrisonHold.SetPinned(controlledEntity, false);
					i--;
					continue;
				}

				KK_GarrisonHold.SetPinned(controlledEntity, true);
				if (KK_GarrisonHold.ShouldHoldToReload(controlledEntity, true))
					KK_GarrisonHold.ConsiderTopOff(controlledEntity);
				assignment.m_bBounding = false;
				assignment.m_bHeldDoor = false;
				assignment.m_fFireUntil = 0;
				assignment.m_fBoundUntil = 0;
				assignment.m_fSteadyUntil = 0;
				assignment.m_fStartedAt = currentTime;
				assignment.m_fStillSince = currentTime;
				assignment.m_vStillPosition = unitPosition;
				i--;
				continue;
			}

			if (assignment.m_bHolding && !passageOverride)
			{
				KK_GarrisonHold.SetPinned(controlledEntity, false);
				assignment.m_bFacingApplied = false;
				assignment.m_bHolding = false;
				assignment.m_bInteriorHold = false;
				assignment.m_fLastOrderAt = currentTime;
				IssueMoveOrder(assignment, true, assignment.m_bCanFight);
			}

			if (assignment.m_bInteriorHold)
			{
				assignment.m_bInteriorHold = false;
				KK_GarrisonHold.SetPinned(controlledEntity, false);
				assignment.m_fLastOrderAt = currentTime;
				IssueMoveOrder(assignment, true, assignment.m_bCanFight);
			}

			if (havePassage)
			{
				if (passageOrder.m_bIssueNow || advancedRoute)
				{
					assignment.m_fLastOrderAt = currentTime;
					IssuePassageMove(assignment, passageOrder);
				}

				if (passageOverride)
				{
					KK_GarrisonHold.SetPinned(controlledEntity, false);
					assignment.m_bHolding = false;
				}
			}
			UpdateApproachFight(
				assignment,
				controlledEntity,
				currentTime,
				attacking,
				waitingOnDoor,
				passageOverride || passageHold,
				advancedRoute
			);

			if (waitingOnDoor && !attacking)
			{
				KK_AgentMove.SetWantedSpeed(
					assignment.m_Agent,
					EMovementType.WALK
				);
			}
			else if (!havePassage && !attacking)
			{
				EMovementType approachSpeed = GetApproachSpeed(unitPosition);
				if (approachSpeed != assignment.m_eApproachSpeed)
				{
					assignment.m_fLastOrderAt = currentTime;
					IssueMoveOrder(
						assignment,
						assignment.m_bRotating,
						assignment.m_bCanFight
					);
				}
				else
				{
					KK_AgentMove.SetWantedSpeed(
						assignment.m_Agent,
						assignment.m_eApproachSpeed
					);
				}
			}

			bool fightStepping =
				assignment.m_bBounding ||
				assignment.m_bHeldDoor ||
				assignment.m_fFireUntil > currentTime ||
				assignment.m_fSteadyUntil > currentTime;

			// A visible enemy, a steady room shot, or a closed door pauses
			// the stuck clock. The travel timeout keeps running through a
			// gunfight outside, including one in the doorway. A room shot,
			// a shut door with nobody to shoot, or a passage hold pauses both.
			bool steadyShot = assignment.m_fSteadyUntil > currentTime;
			if (passageHold || steadyShot || (waitingOnDoor && !attacking))
			{
				assignment.m_fStillSince = currentTime;
				assignment.m_fStartedAt += timerDelta;
			}
			else if (
				waitingOnDoor ||
				(attacking && HasVisibleTarget(assignment.m_Agent))
			)
			{
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
			else if (
				!passageHold &&
				!fightStepping &&
				currentTime - assignment.m_fStillSince >=
				m_GarrisonWaypoint.GetStuckTimeout() * 1000.0
			)
			{
				if (SCR_BaseGameMode.KK_LogEnabled())
					PrintFormat(
						"KK: Garrison unit stood still, sending him to %1",
						assignment.m_Target.m_vPosition
					);

				assignment.m_fStillSince = currentTime;
				assignment.m_fStartedAt = currentTime;
				assignment.m_fLastOrderAt = currentTime;
				assignment.m_bCombatYield = false;
				IssueMoveOrder(assignment, true);
			}

			if (
				!passageHold &&
				currentTime - assignment.m_fStartedAt >=
				timeoutMs
			)
			{
				if (SCR_BaseGameMode.KK_LogEnabled())
					PrintFormat(
						"KK: Garrison hold timed out at %1",
						assignment.m_Target.m_vPosition
					);

				ReleaseAssignment(i, true);
				i--;
				continue;
			}

			if (
				!havePassage &&
				!waitingOnDoor &&
				!attacking &&
				currentTime - assignment.m_fLastOrderAt >=
				intervalMs
			)
			{
				assignment.m_fLastOrderAt = currentTime;
				IssueMoveOrder(
					assignment,
					assignment.m_bRotating,
					assignment.m_bCanFight
				);
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
				HasAssignment(agent) ||
				m_mAmmoReleased.Contains(agent)
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

			KK_AuthoredRouteHelper.BuildGoals(
				m_Plan,
				agent.GetControlledEntity().GetOrigin(),
				target,
				assignment.m_aRouteGoals
			);
			assignment.m_iRouteIndex = 0;

			m_aAssignments.Insert(assignment);
			KK_PerceptionBoost.Apply(
				agent,
				m_mPerceptionFactors
			);
			IssueMoveOrder(assignment);

			if (SCR_BaseGameMode.KK_LogEnabled())
				PrintFormat(
					"KK: Garrison unit %1 holding %2 floor=%3 cluster=%4 opening=%5",
					agent,
					target.m_vPosition,
					target.m_iFloor,
					target.m_iCluster,
					target.m_eOpening
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

	protected float HoldRadiusFor(KK_InteriorTarget target)
	{
		if (!m_GarrisonWaypoint)
			return 1;

		if (target && target.m_iAuthoredId >= 0)
			return m_GarrisonWaypoint.GetAuthoredHoldRadius();

		return m_GarrisonWaypoint.GetHoldRadius();
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
		assignment.m_bRotating = true;
		assignment.m_bCombatYield = false;
		assignment.m_bFacingApplied = false;
		assignment.m_fStartedAt = currentTime;
		assignment.m_fStillSince = currentTime;
		assignment.m_fLastOrderAt = currentTime;
		assignment.m_vStillPosition = unitPosition;
		KK_AuthoredRouteHelper.BuildGoals(
			m_Plan,
			unitPosition,
			next,
			assignment.m_aRouteGoals
		);
		assignment.m_iRouteIndex = 0;
		IssueMoveOrder(assignment, true);

		if (SCR_BaseGameMode.KK_LogEnabled())
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

		KK_InteriorTarget uncovered = FindUncoveredOpening(current);
		if (uncovered)
			return uncovered;

		if (
			current.m_eOpening != KK_EInteriorOpening.NONE &&
			CountOpeningSector(current) <= 1
		)
		{
			return null;
		}

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

	protected KK_InteriorTarget FindOpeningPost()
	{
		int floorIndex = FindLeastOccupiedOpeningFloor();
		if (floorIndex < 0)
			return null;

		KK_InteriorTarget uncovered =
			FindUncoveredOpeningOnFloor(floorIndex);

		if (uncovered)
			return uncovered;

		return FindFarthestPendingOpening(floorIndex);
	}

	protected int FindLeastOccupiedOpeningFloor()
	{
		int bestFloor = -1;
		int bestCount = int.MAX;

		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();

		foreach (KK_InteriorTarget target : targets)
		{
			if (!target || !IsPendingOpening(target))
				continue;

			int assignedCount = CountAssignedOnFloor(target.m_iFloor);

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

	protected KK_InteriorTarget FindUncoveredOpeningOnFloor(int floorIndex)
	{
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();
		KK_InteriorTarget window;

		foreach (KK_InteriorTarget target : targets)
		{
			if (!target || target.m_iFloor != floorIndex)
				continue;

			if (!IsPendingOpening(target))
				continue;

			if (CountOpeningSector(target) > 0)
				continue;

			if (target.m_eOpening == KK_EInteriorOpening.DOOR)
				return target;

			if (!window)
				window = target;
		}

		return window;
	}

	protected KK_InteriorTarget FindFarthestPendingOpening(int floorIndex)
	{
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();
		KK_InteriorTarget farthest;
		float farthestDistance = -1;

		foreach (KK_InteriorTarget target : targets)
		{
			if (!target || target.m_iFloor != floorIndex)
				continue;

			if (!IsPendingOpening(target))
				continue;

			float nearestAssigned =
				DistanceToNearestAssignment(target.m_vPosition);

			if (nearestAssigned <= farthestDistance)
				continue;

			farthestDistance = nearestAssigned;
			farthest = target;
		}

		return farthest;
	}

	protected KK_InteriorTarget FindUncoveredOpening(notnull KK_InteriorTarget current)
	{
		array<ref KK_InteriorTarget> targets = m_Plan.GetTargets();
		KK_InteriorTarget farthest;
		float farthestDistance = -1;

		foreach (KK_InteriorTarget target : targets)
		{
			if (!target || target == current)
				continue;

			if (!IsPendingOpening(target))
				continue;

			if (SameOpeningSector(current, target))
				continue;

			if (CountOpeningSector(target) > 0)
				continue;

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

	protected bool IsPendingOpening(notnull KK_InteriorTarget target)
	{
		return target.m_eOpening != KK_EInteriorOpening.NONE &&
			target.m_eState == KK_EInteriorTargetState.PENDING;
	}

	protected bool SameOpeningSector(
		notnull KK_InteriorTarget left,
		notnull KK_InteriorTarget right)
	{
		return left.m_eOpening == right.m_eOpening &&
			left.m_iFloor == right.m_iFloor &&
			left.m_iCluster == right.m_iCluster &&
			left.m_iOpeningHeading == right.m_iOpeningHeading;
	}

	protected int CountOpeningSector(notnull KK_InteriorTarget target)
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
				SameOpeningSector(assignment.m_Target, target)
			)
			{
				count++;
			}
		}

		return count;
	}

	protected KK_InteriorTarget FindGarrisonTarget()
	{
		KK_InteriorTarget opening = FindOpeningPost();
		if (opening)
			return opening;

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

	// Contact keeps the attack in charge of the shot. Between bursts he runs
	// toward the next route step instead of planting outside the building.
	protected void UpdateApproachFight(
		notnull KK_GarrisonAgentAssignment assignment,
		notnull IEntity controlledEntity,
		float currentTime,
		bool attacking,
		bool waitingOnDoor,
		bool passageBusy,
		bool advancedRoute)
	{
		// On the way to the building, a bound runs and shoots. Inside, an
		// enemy in the house is handled below and stops him.
		bool boundRunning =
			assignment.m_bBounding &&
			!advancedRoute &&
			currentTime < assignment.m_fBoundUntil;
		bool outside = !IsInsideBuilding(controlledEntity.GetOrigin());

		if (boundRunning && outside && !waitingOnDoor && !passageBusy)
		{
			assignment.m_fSteadyUntil = 0;
			KK_GarrisonHold.SetDoorFiring(controlledEntity, false);
			KK_GarrisonHold.SetPinned(controlledEntity, false);
			KK_GarrisonHold.SetApproachGoal(
				controlledEntity,
				AssignmentMoveGoal(assignment)
			);
			KK_AgentMove.SetWantedSpeed(
				assignment.m_Agent,
				EMovementType.RUN
			);
			return;
		}

		if (
			HoldSteadyShot(
				assignment,
				controlledEntity,
				currentTime,
				waitingOnDoor
			)
		)
		{
			return;
		}

		if (!attacking)
		{
			assignment.m_bBounding = false;
			assignment.m_bHeldDoor = false;
			assignment.m_fFireUntil = 0;
			assignment.m_fBoundUntil = 0;
			assignment.m_fSteadyUntil = 0;
			KK_GarrisonHold.ClearApproachGoal(controlledEntity);
			KK_GarrisonHold.SetDoorFiring(controlledEntity, false);
			return;
		}

		if (passageBusy)
		{
			KK_GarrisonHold.ClearApproachGoal(controlledEntity);
			KK_GarrisonHold.SetDoorFiring(controlledEntity, false);
			return;
		}

		if (waitingOnDoor)
		{
			assignment.m_bHeldDoor = true;
			KK_GarrisonHold.ClearApproachGoal(controlledEntity);
			KK_GarrisonHold.SetDoorFiring(controlledEntity, true);
			KK_GarrisonHold.SetPinned(controlledEntity, true);
			return;
		}

		KK_GarrisonHold.SetDoorFiring(controlledEntity, false);

		bool cannotShoot = KK_GarrisonHold.CannotShoot(controlledEntity);
		if (cannotShoot)
			assignment.m_fFireUntil = 0;

		// The wait was the burst. The next move is through the doorway.
		if (assignment.m_bHeldDoor)
		{
			BeginFightBound(assignment, controlledEntity, currentTime);
			return;
		}

		bool roomShot = KK_GarrisonHold.OwnsShot(controlledEntity);

		if (!assignment.m_bBounding)
		{
			bool seeEnemy =
				!cannotShoot &&
				HasVisibleTarget(assignment.m_Agent);
			if (!roomShot && seeEnemy && assignment.m_fFireUntil <= 0)
				assignment.m_fFireUntil = currentTime + FIGHT_WINDOW_MS;

			if (currentTime < assignment.m_fFireUntil)
			{
				KK_GarrisonHold.ClearApproachGoal(controlledEntity);
				KK_GarrisonHold.SetPinned(controlledEntity, true);
				return;
			}

			BeginFightBound(assignment, controlledEntity, currentTime);
			return;
		}

		if (advancedRoute || currentTime >= assignment.m_fBoundUntil)
		{
			assignment.m_bBounding = false;

			if (!cannotShoot && !roomShot && HasVisibleTarget(assignment.m_Agent))
			{
				assignment.m_fFireUntil = currentTime + FIGHT_WINDOW_MS;
				KK_GarrisonHold.ClearApproachGoal(controlledEntity);
				KK_GarrisonHold.SetPinned(controlledEntity, true);
				return;
			}

			BeginFightBound(assignment, controlledEntity, currentTime);
			return;
		}

		KK_GarrisonHold.SetPinned(controlledEntity, false);
		KK_GarrisonHold.SetApproachGoal(
			controlledEntity,
			AssignmentMoveGoal(assignment)
		);
		KK_AgentMove.SetWantedSpeed(
			assignment.m_Agent,
			EMovementType.RUN
		);
	}

	// The room gun found someone. Plant until that shot is finished, including
	// a short tail so the aim delay can still break the shot.
	protected bool HoldSteadyShot(
		notnull KK_GarrisonAgentAssignment assignment,
		notnull IEntity controlledEntity,
		float currentTime,
		bool waitingOnDoor)
	{
		if (KK_GarrisonHold.CannotShoot(controlledEntity))
		{
			assignment.m_fSteadyUntil = 0;
			return false;
		}

		bool want = KK_GarrisonHold.WantsSteadyShot(controlledEntity);
		if (want)
		{
			float until = currentTime +
				KK_GarrisonHold.ShotDelaySeconds() * 1000.0 +
				250.0;
			if (until > assignment.m_fSteadyUntil)
				assignment.m_fSteadyUntil = until;
		}

		if (!want && currentTime >= assignment.m_fSteadyUntil)
			return false;

		assignment.m_bBounding = false;
		assignment.m_fFireUntil = 0;
		assignment.m_fBoundUntil = 0;
		KK_GarrisonHold.ClearApproachGoal(controlledEntity);
		KK_GarrisonHold.SetPinned(controlledEntity, true);
		KK_GarrisonHold.SetDoorFiring(controlledEntity, waitingOnDoor);
		return true;
	}

	protected void BeginFightBound(
		notnull KK_GarrisonAgentAssignment assignment,
		notnull IEntity controlledEntity,
		float currentTime)
	{
		assignment.m_bBounding = true;
		assignment.m_bHeldDoor = false;
		assignment.m_fFireUntil = 0;
		assignment.m_fBoundUntil = currentTime + FIGHT_BOUND_MS;
		assignment.m_fLastOrderAt = currentTime;
		KK_GarrisonHold.SetPinned(controlledEntity, false);
		KK_GarrisonHold.SetApproachGoal(
			controlledEntity,
			AssignmentMoveGoal(assignment)
		);
		IssueMoveOrder(assignment, true, assignment.m_bCanFight);
	}

	protected bool HasVisibleTarget(notnull AIAgent agent)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return false;

		SCR_AICombatComponent combat =
			soldier.m_UtilityComponent.m_CombatComponent;

		if (!combat)
			return false;

		BaseTarget target = combat.GetCurrentTarget();
		if (!target)
			return false;

		IEntity enemy = target.GetTargetEntity();
		if (enemy && !KK_GarrisonHold.IsFightable(enemy))
			return false;

		return KK_GarrisonHold.SeesTarget(
			agent.GetControlledEntity(),
			target
		);
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

	protected vector AimPosition(notnull AIAgent agent, vector fallback)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return fallback;

		SCR_AICombatComponent combat =
			soldier.m_UtilityComponent.m_CombatComponent;

		if (!combat)
			return fallback;

		BaseTarget target = combat.GetCurrentTarget();
		if (!target)
			return fallback;

		IEntity targetEntity = target.GetTargetEntity();
		if (targetEntity)
			return targetEntity.GetOrigin();

		return target.GetLastSeenPosition();
	}

	protected void CollectPassageSoldiers(
		notnull array<ref KK_PassageSoldier> soldiers)
	{
		foreach (KK_GarrisonAgentAssignment assignment : m_aAssignments)
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

			vector origin =
				assignment.m_Agent.GetControlledEntity().GetOrigin();

			bool settled = IsAtHold(
				origin,
				assignment.m_Target.m_vPosition,
				HoldRadiusFor(assignment.m_Target)
			);

			soldiers.Insert(
				new KK_PassageSoldier(
					assignment.m_Agent,
					AssignmentMoveGoal(assignment),
					settled
				)
			);
		}
	}

	protected vector AssignmentMoveGoal(
		notnull KK_GarrisonAgentAssignment assignment)
	{
		if (!assignment.m_Target)
			return vector.Zero;

		return KK_AuthoredRouteHelper.CurrentMoveGoal(
			assignment.m_Target,
			assignment.m_aRouteGoals,
			assignment.m_iRouteIndex
		);
	}

	protected bool IssueReloadBash(
		notnull KK_GarrisonAgentAssignment assignment,
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
		assignment.m_bHolding = false;
		assignment.m_bBounding = false;
		assignment.m_bHeldDoor = false;
		assignment.m_fFireUntil = 0;
		assignment.m_fBoundUntil = 0;
		assignment.m_fSteadyUntil = 0;
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

	protected bool DriveGarrisonReload(
		notnull KK_GarrisonAgentAssignment assignment,
		notnull IEntity controlledEntity,
		vector unitPosition,
		float currentTime,
		bool atHold)
	{
		if (IssueReloadBash(assignment, controlledEntity, currentTime))
			return true;

		bool dash = KK_GarrisonHold.MustDashToReload(controlledEntity);
		if (!dash)
			KK_GarrisonHold.LogReload(controlledEntity, KK_GarrisonHold.ReloadSkipReason(controlledEntity));
		else if (KK_GarrisonHold.SprintNodeStuck(controlledEntity))
		{
			KK_GarrisonHold.LogReload(controlledEntity, "node");
			KK_GarrisonHold.ClearReloadDash(controlledEntity);
			KK_GarrisonHold.SetReloadCover(controlledEntity, true);
			dash = false;
		}
		else if (
			KK_GarrisonHold.ReloadDashExpired(controlledEntity) &&
			!KK_GarrisonHold.SprintBeforeReload(controlledEntity)
		)
		{
			KK_GarrisonHold.LogReload(controlledEntity, "dash-timeout");
			KK_GarrisonHold.ClearReloadDash(controlledEntity);
			KK_GarrisonHold.SetReloadCover(controlledEntity, true);
			dash = false;
		}

		if (dash)
		{
			vector goal;
			array<ref KK_InteriorTarget> targets;
			if (m_Plan)
				targets = m_Plan.GetTargets();

			if (!KK_GarrisonHold.KeepReloadMove(
				controlledEntity,
				targets,
				unitPosition,
				goal
			))
			{
				KK_GarrisonHold.LogReload(controlledEntity, "hold");
				KK_GarrisonHold.ClearReloadDash(controlledEntity);
				KK_GarrisonHold.SetReloadCover(controlledEntity, true);
			}
			else
			{
				EMovementType speed =
					KK_GarrisonHold.ReloadMoveSpeed(controlledEntity);
				string choice = "break";
				if (KK_GarrisonHold.SprintBeforeReload(controlledEntity))
				{
					choice = "sprint";
					KK_GarrisonHold.LowerForReloadSprint(assignment.m_Agent);
				}

				KK_GarrisonHold.LogReload(controlledEntity, choice);
				assignment.m_bHolding = false;
				assignment.m_bBounding = false;
				assignment.m_bHeldDoor = false;
				assignment.m_fFireUntil = 0;
				assignment.m_fBoundUntil = 0;
				assignment.m_fSteadyUntil = 0;
				KK_GarrisonHold.SetReloadCover(controlledEntity, false);
				KK_GarrisonHold.SetPinned(controlledEntity, false);
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
							controlledEntity,
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
						KK_GarrisonHold.ReloadMovePriority(controlledEntity),
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

		// Off the post, an empty gun out of sight still waits here. On the
		// post the hold below already keeps him.
		if (!atHold && KK_GarrisonHold.ShouldHoldToReload(controlledEntity, false))
		{
			KK_GarrisonHold.ClearReloadDash(controlledEntity);
			KK_GarrisonHold.SetPinned(controlledEntity, true);
			KK_GarrisonHold.ConsiderTopOff(controlledEntity);
			assignment.m_bBounding = false;
			assignment.m_fFireUntil = 0;
			assignment.m_fSteadyUntil = 0;
			assignment.m_fStillSince = currentTime;
			assignment.m_fStartedAt = currentTime;
			return true;
		}

		if (
			assignment.m_bReloadMove &&
			!KK_GarrisonHold.ShouldHoldToReload(controlledEntity, true)
		)
		{
			KK_GarrisonHold.ClearReloadDash(controlledEntity);
			KK_GarrisonHold.SetReloadCover(controlledEntity, false);
			KK_GarrisonHold.SetPinned(controlledEntity, false);
		}

		return false;
	}

	protected void IssuePassageMove(
		notnull KK_GarrisonAgentAssignment assignment,
		notnull KK_PassageOrder passageOrder,
		bool holdForDoor = false)
	{
		EMovementType speed = assignment.m_eApproachSpeed;
		if (passageOrder.m_bOverride && passageOrder.m_bWalk)
			speed = EMovementType.WALK;
		else if (passageOrder.m_bOverride)
			speed = EMovementType.RUN;

		bool entering;
		float priority = PriorityForSoldier(
			assignment.m_Agent,
			false,
			entering,
			holdForDoor,
			!holdForDoor && assignment.m_bCanFight
		);
		ApplyMovePriority(assignment, entering);

		KK_AgentMove.Issue(
			this,
			m_Group,
			assignment.m_Agent,
			passageOrder.m_vMoveTo,
			m_mSoloHandlers,
			priority,
			speed
		);
	}

	protected void IssueMoveOrder(
		notnull KK_GarrisonAgentAssignment assignment,
		bool forceTravel = false,
		bool yieldToCombat = false)
	{
		if (!assignment.m_Agent || !assignment.m_Target)
			return;

		EMovementType speed = EMovementType.SPRINT;
		IEntity controlledEntity =
			assignment.m_Agent.GetControlledEntity();

		if (controlledEntity)
		{
			// A sprint order drops the weapon and then blocks the shot.
			if (IsEngagingEnemy(assignment.m_Agent))
				speed = EMovementType.RUN;
			else
				speed = GetApproachSpeed(controlledEntity.GetOrigin());
		}

		assignment.m_eApproachSpeed = speed;

		bool entering;
		float priority = PriorityForSoldier(
			assignment.m_Agent,
			forceTravel,
			entering,
			false,
			yieldToCombat
		);
		ApplyMovePriority(assignment, entering);

		KK_AgentMove.Issue(
			this,
			m_Group,
			assignment.m_Agent,
			AssignmentMoveGoal(assignment),
			m_mSoloHandlers,
			priority,
			speed
		);
	}

	// Same score as a clear move. Attack-selected is higher, so a fight
	// takes over and the sprint to the building does not.
	protected float PriorityForSoldier(
		notnull AIAgent agent,
		bool forceTravel,
		out bool entering,
		bool holdForDoor = false,
		bool yieldToCombat = false)
	{
		entering = false;

		if (holdForDoor)
			return 0;

		return KK_AgentMove.PRIORITY_LEVEL;
	}

	protected void ApplyMovePriority(
		notnull KK_GarrisonAgentAssignment assignment,
		bool entering)
	{
		if (assignment.m_bEntryPriority && !entering)
			CancelAgentOrder(assignment.m_Agent);

		assignment.m_bEntryPriority = entering;
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
		return KK_GarrisonHold.PositionInside(m_Building, worldPosition);
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

	protected void UnpinAssignments()
	{
		foreach (KK_GarrisonAgentAssignment assignment : m_aAssignments)
		{
			if (!assignment)
				continue;

			ReleaseSoldierHold(assignment);
		}
	}

	// The body remembered at the post. GetControlledEntity is null while he is down.
	protected IEntity AssignmentBody(KK_GarrisonAgentAssignment assignment)
	{
		if (!assignment)
			return null;

		IEntity body;
		if (assignment.m_Agent)
			body = assignment.m_Agent.GetControlledEntity();

		if (body)
			assignment.m_Body = body;

		return assignment.m_Body;
	}

	protected void ReleaseSoldierHold(KK_GarrisonAgentAssignment assignment)
	{
		IEntity body = AssignmentBody(assignment);
		if (!body)
			return;

		KK_GarrisonHold.SetPinned(body, false);
		KK_GarrisonHold.SetTraveling(body, false);
		KK_GarrisonHold.ClearApproachGoal(body);
		KK_GarrisonHold.SetIgnoringTargets(body, false);
		KK_GarrisonHold.SetDoorFiring(body, false);
		KK_GarrisonHold.SetMoveFire(body, false);
		KK_GarrisonHold.SetGarrisonBuilding(body, null);
	}

	protected void ReleaseForAmmo(int assignmentIndex)
	{
		KK_GarrisonAgentAssignment assignment =
			m_aAssignments[assignmentIndex];

		if (assignment && assignment.m_Agent)
		{
			m_mAmmoReleased.Set(assignment.m_Agent, -1);
			if (SCR_BaseGameMode.KK_LogEnabled())
			{
				PrintFormat(
					"KK: Garrison unit %1 released, no gun ammo",
					assignment.m_Agent
				);
			}
		}

		ReleaseAssignment(assignmentIndex, false);
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

		ReleaseSoldierHold(assignment);

		if (assignment.m_Agent)
		{
			assignment.m_bFacingApplied = false;

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
				else
				{
					assignment.m_Target.m_eState =
						KK_EInteriorTargetState.UNREACHABLE;

					if (SCR_BaseGameMode.KK_LogEnabled())
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

			ReleaseSoldierHold(assignment);

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
		UnpinAssignments();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		m_mAmmoReleased.Clear();
		ClearDebug();

		if (SCR_BaseGameMode.KK_LogEnabled())
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
		UnpinAssignments();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		m_mAmmoReleased.Clear();
		ClearDebug();

		if (SCR_BaseGameMode.KK_LogEnabled())
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

	// Drops this activity without completing its waypoint, so a restart
	// can build a new one for the squad as it is now.
	void Supersede()
	{
		if (m_bCancelled || m_bFinished)
			return;

		m_bFinished = true;
		SendCancelMessagesToAllAgents();
		UnpinAssignments();
		KK_PerceptionBoost.RestoreAll(m_mPerceptionFactors);
		m_aAssignments.Clear();
		m_mAmmoReleased.Clear();
		ClearDebug();
		SetActionState(EAIActionState.FAILED);
		SetRemoveAction(true);
	}

	// True while this order should keep running. Joining soldiers restart
	// the group activity and the waypoint tree; that is not a cancel.
	bool IsLive()
	{
		if (m_bCancelled || m_bFinished || !m_GarrisonWaypoint)
			return false;

		EAIActionState state = GetActionState();
		return state != EAIActionState.FAILED &&
			state != EAIActionState.COMPLETED;
	}

	protected bool WaypointStillAssigned()
	{
		if (!m_Group || !m_GarrisonWaypoint)
			return false;

		array<AIWaypoint> waypoints = {};
		m_Group.GetWaypoints(waypoints);

		foreach (AIWaypoint waypoint : waypoints)
		{
			if (waypoint == m_GarrisonWaypoint)
				return true;
		}

		return false;
	}

	protected void RetainAfterRestart()
	{
		m_bRetain = true;
		SetActionState(EAIActionState.EVALUATED);
		SetRemoveAction(false);

		if (SCR_BaseGameMode.KK_LogEnabled())
			Print("KK: Garrison kept running after the group activity restarted");
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

		if (WaypointStillAssigned())
		{
			RetainAfterRestart();
			return;
		}

		CancelGarrison();
	}

	override void OnActionFailed()
	{
		super.OnActionFailed();

		if (m_bFinished || m_bCancelled)
			return;

		if (WaypointStillAssigned())
		{
			RetainAfterRestart();
			return;
		}

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

			if (
				target.m_eOpening != KK_EInteriorOpening.NONE &&
				target.m_vFacing.Length() > 0.01
			)
			{
				m_aDebugShapes.Insert(
					Shape.CreateArrow(
						markerPosition,
						markerPosition + (target.m_vFacing * 1.4),
						0.08,
						color,
						ShapeFlags.NOZBUFFER | ShapeFlags.VISIBLE
					)
				);
			}
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
			KK_EInteriorTargetState.ACTIVE
		)
		{
			return 0xFFFFFF00;
		}

		if (target.m_eOpening == KK_EInteriorOpening.DOOR)
			return 0xFFFF8800;

		if (target.m_eOpening == KK_EInteriorOpening.WINDOW)
			return 0xFF44EE66;

		return 0xFF00DDFF;
	}
}

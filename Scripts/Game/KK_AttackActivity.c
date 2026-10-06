class KK_AttackPair
{
	int m_iIndex;
	ref array<AIAgent> m_aAgents;
	vector m_vGoal;
	bool m_bHasGoal;
	bool m_bPaused;
	bool m_bHolding;
	bool m_bArrived;
	float m_fPauseSince;
	float m_fFirstStopAt;
	int m_iSide;
	float m_fLastOrderAt;
	float m_fStillSince;
	vector m_vStillPosition;
	int m_iNudges;
	float m_fProgressBoost;

	void KK_AttackPair()
	{
		m_aAgents = new array<AIAgent>();
		m_iSide = 1;
	}
}

class KK_AttackActivity : SCR_AIActivityBase
{
	protected KK_AttackWaypoint m_AttackWaypoint;
	protected SCR_AIGroup m_Group;
	protected AIPathfindingComponent m_Pathfinding;

	protected ref array<ref KK_AttackPair> m_aPairs = {};
	protected ref map<AIAgent, int> m_mPairOf = new map<AIAgent, int>();
	protected ref map<AIAgent, ref KK_FormationSplit> m_mSoloHandlers =
		new map<AIAgent, ref KK_FormationSplit>();
	protected ref map<AIAgent, IEntity> m_mDoors =
		new map<AIAgent, IEntity>();
	protected ref set<AIAgent> m_Crouched = new set<AIAgent>();
	// A bound member who has reached his own slot. His partner may still be
	// sprinting. The map value is the last time his stay was issued.
	protected ref map<AIAgent, float> m_mSettledAt = new map<AIAgent, float>();
	// On the take cover point, and handed to the attack. The push skips him
	// even when the rest of the squad is still on the way.
	protected ref set<AIAgent> m_Released = new set<AIAgent>();
	protected ref map<AIAgent, float> m_mReturnAt = new map<AIAgent, float>();
	protected ref map<AIAgent, float> m_mPerceptionFactors =
		new map<AIAgent, float>();

	protected ref TraceParam m_CoverTrace;

	protected vector m_vOrigin;
	protected vector m_vTarget;
	protected bool m_bOriginSet;
	protected bool m_bNoted;
	protected bool m_bYielding;
	// Contact shoots from here before the normal attack, Reaper, or CRX
	// is allowed to take the fight and run for cover.
	protected float m_fReturnFireSince;
	protected bool m_bReturnFired;
	protected ref set<AIAgent> m_ReturnFireHeld = new set<AIAgent>();
	protected bool m_bFinished;
	protected bool m_bCancelled;
	protected bool m_bRetain;
	protected int m_iRunner;
	protected KK_EAttackPace m_eApplied;
	protected bool m_bPaceSet;
	protected bool m_bAtPoint;
	protected float m_fEmptySince;

	protected static const float STEP_REACH = 4;
	// Below this he has stopped. Faster than this he is still on the sprint,
	// and taking the sprint flag off turns that move into a sidestep.
	protected static const float MEMBER_STOP = 1.25;
	protected static const float STUCK_MS = 8000;
	protected static const float EMPTY_MS = 10000;
	protected static const float SCAN_AHEAD = 40;
	protected static const float SCAN_ASIDE = 30;
	// Below an unidentified-target look (50) and an enemy look (80). Above
	// a danger glance (20) and the lane scan, so a seen man wins the head
	// without restarting the look the attack behavior already owns.
	protected static const float CONTACT_LOOK = 45;
	protected static const float LANE_LOOK = 4;

	void KK_AttackActivity(
		SCR_AIGroupUtilityComponent utility,
		AIWaypoint relatedWaypoint)
	{
		m_AttackWaypoint = KK_AttackWaypoint.Cast(relatedWaypoint);
		m_Group = SCR_AIGroup.Cast(utility.GetAIAgent());

		if (m_Group)
		{
			m_Pathfinding = AIPathfindingComponent.Cast(
				m_Group.FindComponent(AIPathfindingComponent)
			);
		}

		if (m_AttackWaypoint && m_Group)
		{
			m_AttackWaypoint.ApplyScenarioSettings();

			KK_EAttackPace noted;
			if (KK_CQBOrders.ConsumeAttackPace(m_Group, noted))
				m_AttackWaypoint.SetPace(noted);
		}

		SetPriority(SCR_AIActionBase.PRIORITY_LEVEL_GAMEMASTER);
		SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_GAMEMASTER);
	}

	override void OnActionSelected()
	{
		super.OnActionSelected();

		if (m_bFinished || m_bCancelled || !m_AttackWaypoint)
			return;

		if (m_bNoted)
			return;

		m_bNoted = true;
		KK_GarrisonHold.NoteOrderGroup(m_Group);

		if (SCR_BaseGameMode.KK_LogEnabled())
		{
			PrintFormat(
				"KK: Attack activity selected at %1 pace %2",
				m_AttackWaypoint.GetOrigin(),
				m_AttackWaypoint.GetPace()
			);
		}
	}

	override float CustomEvaluate()
	{
		if (m_bFinished || m_bCancelled || !m_Group || !m_AttackWaypoint)
			return 0;

		if (OrderWasReplaced())
		{
			CancelAttack();
			return 0;
		}

		float now = 0;
		if (GetGame() && GetGame().GetWorld())
			now = GetGame().GetWorld().GetWorldTime();

		RefreshPairs();

		if (!HasSoldiers())
		{
			if (m_fEmptySince <= 0)
				m_fEmptySince = now;
			else if (now - m_fEmptySince >= EMPTY_MS)
				AbortAttack("no soldiers");

			return GetPriority();
		}

		m_fEmptySince = 0;

		if (!m_bOriginSet)
			CaptureOrigin();

		bool startedOnPoint = FlatDistance(m_vOrigin, m_vTarget) <= Arrival();
		if (!IsTakeCover() && startedOnPoint)
		{
			CompleteAttack();
			return 0;
		}

		NotePaceChange();

		if (IsTakeCover())
			UpdateArrivals(now);

		// Finish distance ends the push. After that, only the return distance
		// pulls them back. Leaving the finish radius must not restart the push.
		if (IsTakeCover() && (m_bAtPoint || startedOnPoint || AllNearTarget()))
		{
			m_bAtPoint = true;
			UpdateHold(now);
			return GetPriority();
		}
		if (AnyContact() && !IsTakeCover())
		{
			if (!m_bReturnFired)
			{
				float burst = ReturnFireMs();
				if (burst > 0)
				{
					if (m_fReturnFireSince <= 0)
					{
						m_fReturnFireSince = now;

						if (SCR_BaseGameMode.KK_LogEnabled())
							Print("KK: Attack return fire");
					}

					if (now - m_fReturnFireSince < burst)
					{
						UpdateReturnFire();
						return GetPriority();
					}

					EndReturnFire();

					if (SCR_BaseGameMode.KK_LogEnabled())
						Print("KK: Attack handed the fight on");
				}

				m_bReturnFired = true;
			}

			if (!m_bYielding)
			{
				m_bYielding = true;
				CancelLooks();
			}

			// The fight needs the rifle. The bound sprint lowers it.
			ReleaseBoundSprint();
			return GetPriority();
		}

		if (m_bYielding || m_bReturnFired || m_fReturnFireSince > 0)
		{
			EndReturnFire();
			m_bYielding = false;
			m_bReturnFired = false;
			m_fReturnFireSince = 0;

			foreach (KK_AttackPair pair : m_aPairs)
			{
				if (pair)
					pair.m_fLastOrderAt = 0;
			}
		}

		if (AllNearTarget())
		{
			CompleteAttack();
			return 0;
		}

		if (IsLeapfrog())
			UpdateBound(now);
		else if (IsTakeCover())
			UpdateSoloBound(now);
		else
			UpdateAdvance(now);

		return GetPriority();
	}

	// Jog keeps the rifle up. A lone pair on Bound uses this path too.
	protected void UpdateAdvance(float now)
	{
		// Leapfrog may have pinned a soldier who arrived ahead of his partner.
		// This path moves every pair, so that pin has to come off.
		ReleaseSettled();

		EMovementType speed = EMovementType.RUN;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair || !PairAlive(pair))
				continue;

			if (pair.m_bArrived)
			{
				LookDownLane(pair);
				continue;
			}

			if (!pair.m_bHasGoal)
			{
				StartStep(pair, speed, now);
				continue;
			}

			if (Reached(pair))
			{
				if (GoalIsFinal(pair.m_vGoal))
				{
					pair.m_bArrived = true;
					StopPair(pair);
					LookDownLane(pair);
					continue;
				}

				if (PauseMs() <= 0)
				{
					pair.m_iSide = -pair.m_iSide;
					if (pair.m_iSide == 0)
						pair.m_iSide = 1;
					StartStep(pair, speed, now);
					continue;
				}

				if (!pair.m_bPaused)
				{
					pair.m_bPaused = true;
					pair.m_fPauseSince = now;
					pair.m_iSide = -pair.m_iSide;
					if (pair.m_iSide == 0)
						pair.m_iSide = 1;
					StopPair(pair);
				}

				Scan(pair, now);

				if (now - pair.m_fPauseSince >= PauseMs())
				{
					pair.m_bPaused = false;
					pair.m_bHasGoal = false;
					StartStep(pair, speed, now);
				}

				continue;
			}

			MaintainMove(pair, speed, now);
			LookAtContact(pair);
		}
	}

	protected void UpdateBound(float now)
	{
		if (!RunnerIsLive())
		{
			int next = NextRunner(m_iRunner);
			if (next < 0)
				return;

			m_iRunner = next;
		}

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair || !PairAlive(pair))
				continue;

			if (pair.m_iIndex == m_iRunner)
				continue;

			if (IsTakeCover())
			{
				CoverHold(pair, now);
				continue;
			}

			if (!pair.m_bHolding)
				BeginHold(pair);

			LookDownLane(pair);
		}

		KK_AttackPair runner = PairAt(m_iRunner);
		if (!runner)
			return;

		// Same hold a lone pair uses. Several pairs wait it out together,
		// then the next pair sprints.
		if (IsTakeCover() && runner.m_bPaused)
		{
			if (now - runner.m_fPauseSince < PauseMs())
			{
				CoverHold(runner, now);
				return;
			}

			runner.m_bPaused = false;
			int nextRunner = NextRunner(m_iRunner);
			if (nextRunner >= 0)
			{
				m_iRunner = nextRunner;
				KK_AttackPair nextPair = PairAt(m_iRunner);
				if (nextPair)
					StartStep(nextPair, EMovementType.SPRINT, now);
			}

			return;
		}

		if (!runner.m_bHasGoal || runner.m_bHolding || runner.m_bArrived)
			StartStep(runner, EMovementType.SPRINT, now);

		bool stepDone = Reached(runner) && PairStopped(runner);
		if (IsTakeCover())
			stepDone = CoverReady(runner, now);

		if (stepDone)
		{
			ForgetPairSettled(runner);
			if (IsTakeCover())
				CoverHold(runner, now);
			else
				BeginHold(runner);

			bool holdForPause = IsTakeCover() && FightersArrived(runner);
			if (holdForPause && PauseMs() > 0)
			{
				runner.m_fPauseSince = now;
				runner.m_bPaused = true;
				return;
			}

			int next = NextRunner(m_iRunner);
			if (next >= 0)
			{
				m_iRunner = next;
				KK_AttackPair nextPair = PairAt(m_iRunner);
				if (nextPair)
					StartStep(nextPair, EMovementType.SPRINT, now);
			}

			return;
		}

		if (IsTakeCover())
			SettleArrivedMembers(runner, now);

		MaintainMove(runner, EMovementType.SPRINT, now);
	}

	// One pair has nobody to leapfrog with. He sprints one bound, then holds
	// there. The normal attack aims and shoots from that hold when it
	// already has an enemy. Then he sprints the next bound.
	protected void UpdateSoloBound(float now)
	{
		KK_AttackPair pair = null;

		foreach (KK_AttackPair candidate : m_aPairs)
		{
			if (!candidate || !PairAlive(candidate))
				continue;

			if (PairAtTarget(candidate))
				continue;

			pair = candidate;
			break;
		}

		if (!pair)
			return;

		if (pair.m_bArrived)
		{
			CoverHold(pair, now);
			return;
		}

		if (pair.m_bPaused)
		{
			if (now - pair.m_fPauseSince < PauseMs())
			{
				CoverHold(pair, now);
				return;
			}

			pair.m_bPaused = false;
			StartStep(pair, EMovementType.SPRINT, now);
			return;
		}

		if (!pair.m_bHasGoal || pair.m_bHolding)
			StartStep(pair, EMovementType.SPRINT, now);

		if (Reached(pair) && PairStopped(pair))
		{
			ForgetPairSettled(pair);
			if (GoalIsFinal(pair.m_vGoal))
			{
				pair.m_bArrived = true;
				CoverHold(pair, now);
				return;
			}

			if (PauseMs() <= 0)
			{
				StartStep(pair, EMovementType.SPRINT, now);
				return;
			}

			pair.m_fPauseSince = now;
			CoverHold(pair, now);
			pair.m_bPaused = true;
			return;
		}

		SettleArrivedMembers(pair, now);
		MaintainMove(pair, EMovementType.SPRINT, now);
	}

	protected void StartStep(
		notnull KK_AttackPair pair,
		EMovementType speed,
		float now)
	{
		ForgetPairSettled(pair);
		pair.m_bHolding = false;
		pair.m_bPaused = false;
		pair.m_bArrived = false;
		SetPairPinned(pair, false);
		pair.m_vGoal = NextGoal(pair);
		if (FlatDistance(PairCenter(pair), pair.m_vGoal) <= STEP_REACH)
		{
			pair.m_fProgressBoost = pair.m_fProgressBoost + StepLength();
			pair.m_vGoal = NextGoal(pair);
		}

		pair.m_bHasGoal = true;
		pair.m_fStillSince = now;
		pair.m_vStillPosition = PairCenter(pair);
		pair.m_iNudges = 0;
		pair.m_fFirstStopAt = 0;
		SetPairCrouch(pair, false);
		// The step does not shoot. A sprint lowers the rifle. A jog keeps it
		// up, and contact on that jog hands the fight to the normal attack.
		ClearHoldFight(pair);

		IssuePair(pair, speed, now);

		if (SCR_BaseGameMode.KK_LogEnabled())
		{
			PrintFormat(
				"KK: Attack pair %1 stepping to %2",
				pair.m_iIndex,
				pair.m_vGoal
			);
		}
	}

	protected void BeginHold(notnull KK_AttackPair pair)
	{
		if (pair.m_bHolding)
			return;

		pair.m_bHolding = true;
		pair.m_bPaused = false;
		pair.m_bHasGoal = false;
		StopPair(pair);
	}

	protected void MaintainMove(
		notnull KK_AttackPair pair,
		EMovementType speed,
		float now)
	{
		if (UpdateDoors(pair))
		{
			pair.m_fStillSince = now;
			return;
		}

		vector center = PairCenter(pair);
		if (FlatDistance(center, pair.m_vStillPosition) > 0.45)
		{
			pair.m_vStillPosition = center;
			pair.m_fStillSince = now;
		}
		else if (
			pair.m_fStillSince > 0 &&
			now - pair.m_fStillSince >= STUCK_MS
		)
		{
			Nudge(pair, speed, now);
			return;
		}

		if (now - pair.m_fLastOrderAt >= KK_AgentMove.REISSUE_INTERVAL_MS)
			IssuePair(pair, speed, now);
		else
			SetPairSpeed(pair, speed);

		if (speed == EMovementType.SPRINT)
			SnapPairSprint(pair);
	}

	protected void Nudge(
		notnull KK_AttackPair pair,
		EMovementType speed,
		float now)
	{
		pair.m_iNudges++;
		pair.m_fStillSince = now;

		if (pair.m_iNudges >= 3)
		{
			pair.m_fProgressBoost = pair.m_fProgressBoost + StepLength();
			pair.m_bHasGoal = false;
			pair.m_iNudges = 0;

			if (SCR_BaseGameMode.KK_LogEnabled())
			{
				PrintFormat(
					"KK: Attack pair %1 skipped a stuck bound",
					pair.m_iIndex
				);
			}

			return;
		}

		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return;

		float side = 4;
		if (pair.m_iNudges % 2 == 0)
			side = -4;

		pair.m_vGoal = pair.m_vGoal + (right * side);
		IssuePair(pair, speed, now);
	}

	protected vector NextGoal(notnull KK_AttackPair pair)
	{
		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return m_vTarget;

		vector center = PairCenter(pair);
		float progress = vector.Dot(center - m_vOrigin, axis);
		progress = progress + pair.m_fProgressBoost;
		float total = vector.Dot(m_vTarget - m_vOrigin, axis);
		float next = progress + StepLength();

		vector goal;
		bool finalStep = next >= total - 1;
		if (finalStep)
		{
			float stagger = 2;
			if (pair.m_iIndex % 2 == 0)
				stagger = -2;

			goal = m_vTarget + (right * stagger);
		}
		else
		{
			goal = m_vOrigin + (axis * next) + LaneOffset(pair.m_iIndex, right);
		}

		goal[1] = center[1];
		goal = PreferCover(goal, axis, right, finalStep);
		return goal;
	}

	protected vector LaneOffset(int pairIndex, vector right)
	{
		int band = pairIndex / 2 + 1;
		float sign = 1;
		if (pairIndex % 2 == 0)
			sign = -1;

		return right * (LaneSpacing() * band * sign);
	}

	protected vector PreferCover(
		vector goal,
		vector axis,
		vector right,
		bool finalStep)
	{
		vector best = OnNavmesh(goal);
		float bestHit = 99;
		bool found = false;

		array<vector> candidates = {};
		candidates.Insert(goal);
		candidates.Insert(goal + (right * 3));
		candidates.Insert(goal - (right * 3));
		candidates.Insert(goal + (axis * 3));

		foreach (vector candidate : candidates)
		{
			vector place = OnNavmesh(candidate);
			if (finalStep && FlatDistance(place, m_vTarget) > Arrival() - 1)
				continue;

			float hit = CoverDistance(place, right);
			if (hit < 0)
				continue;

			if (found && hit >= bestHit)
				continue;

			found = true;
			bestHit = hit;
			best = place;
		}

		if (finalStep && FlatDistance(best, m_vTarget) > Arrival() - 1)
			return OnNavmesh(m_vTarget);

		return best;
	}

	protected float CoverDistance(vector feet, vector right)
	{
		float left = RayCover(feet, right);
		float other = RayCover(feet, right * -1);
		float best = -1;

		if (left > 0.6 && left < 3.5)
			best = left;

		if (other > 0.6 && other < 3.5 && (best < 0 || other < best))
			best = other;

		return best;
	}

	protected float RayCover(vector feet, vector direction)
	{
		BaseWorld world;
		if (GetGame())
			world = GetGame().GetWorld();

		if (!world)
			return -1;

		direction[1] = 0;
		if (direction.Length() < 0.01)
			return -1;

		direction.Normalize();
		vector start = feet + Vector(0, 1.2, 0) + (direction * 0.35);
		vector end = start + (direction * 4);

		if (!m_CoverTrace)
			m_CoverTrace = new TraceParam();

		m_CoverTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		m_CoverTrace.Start = start;
		m_CoverTrace.End = end;
		m_CoverTrace.Exclude = null;
		m_CoverTrace.TraceEnt = null;

		float fraction = world.TraceMove(m_CoverTrace, FilterCover);
		if (fraction >= 0.98)
			return -1;

		return fraction * 4;
	}

	protected bool FilterCover(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		if (!entity)
			return true;

		if (ChimeraCharacter.Cast(entity))
			return false;

		return true;
	}

	protected vector OnNavmesh(vector position)
	{
		if (!m_Pathfinding)
			return position;

		vector corrected;
		if (!m_Pathfinding.GetClosestPositionOnNavmesh(
			position,
			Vector(2, 2, 2),
			corrected
		))
		{
			return position;
		}

		return corrected;
	}

	protected void IssuePair(
		notnull KK_AttackPair pair,
		EMovementType speed,
		float now)
	{
		vector axis;
		vector right;
		BuildAxis(axis, right);

		int slot = 0;
		float priority = KK_AgentMove.PRIORITY_LEVEL;
		if (IsTakeCover() && speed == EMovementType.SPRINT)
			priority = KK_AgentMove.SprintPriorityLevel();
		else if (IsTakeCover())
			priority = KK_AgentMove.AbsolutePriorityLevel();

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			float side = -1.5;
			if (slot == 1)
				side = 1.5;

			slot++;
			// He is already on this slot, shooting, while a partner catches up.
			// A new sprint would drop the rifle again. A man already on the
			// point stays with the attack.
			if (m_mSettledAt.Contains(agent) || IsReleased(agent))
				continue;

			vector goal = pair.m_vGoal + (right * side);
			// Clear the enemy move before the sprint speed, or this step
			// runs along the facing instead of the lane.
			if (speed == EMovementType.SPRINT)
				KK_GarrisonHold.ReleaseCombatAim(agent);

			KK_AgentMove.Issue(
				this,
				m_Group,
				agent,
				goal,
				m_mSoloHandlers,
				priority,
				speed
			);
			SetCrouch(agent, false);
			ApplyWeaponForSpeed(agent, speed);
			if (speed == EMovementType.SPRINT)
				SnapSprintLook(agent, goal);
		}

		pair.m_fLastOrderAt = now;
	}

	protected void SetPairSpeed(notnull KK_AttackPair pair, EMovementType speed)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || m_mSettledAt.Contains(agent) || IsReleased(agent))
				continue;

			KK_AgentMove.SetWantedSpeed(agent, speed);
			ApplyWeaponForSpeed(agent, speed);
		}
	}

	// A sprint bound drops the rifle so the run is not cut short. Walk, jog,
	// and a hold keep it up.
	protected void ApplyWeaponForSpeed(AIAgent agent, EMovementType speed)
	{
		if (!agent)
			return;

		if (speed == EMovementType.SPRINT)
		{
			KK_GarrisonHold.SetBoundSprint(agent, true);
			KK_GarrisonHold.SetAdvancing(agent, false);
			SetWeapon(agent, false);
			return;
		}

		bool wasSprinting = KK_GarrisonHold.IsBoundSprint(agent);
		KK_GarrisonHold.SetBoundSprint(agent, false);
		KK_GarrisonHold.SetAdvancing(agent, true);
		if (wasSprinting)
			KK_GarrisonHold.RaiseAfterSprint(agent);
	}

	protected bool UpdateDoors(notnull KK_AttackPair pair)
	{
		bool waiting = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity() || IsReleased(agent))
				continue;

			IEntity door;
			if (m_mDoors.Contains(agent))
				door = m_mDoors.Get(agent);

			if (KK_DoorAssist.Handle(agent, pair.m_vGoal, door))
			{
				waiting = true;
				KK_AgentMove.SetWantedSpeed(agent, EMovementType.WALK);
				ApplyWeaponForSpeed(agent, EMovementType.WALK);
			}

			m_mDoors.Set(agent, door);
		}

		return waiting;
	}

	protected void StopPair(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || IsReleased(agent))
				continue;

			CancelAgent(agent);
			KK_AgentMove.SetWantedSpeed(agent, EMovementType.IDLE);
			ApplyWeaponForSpeed(agent, EMovementType.IDLE);
		}

		SetPairCrouch(pair, true);
	}

	protected void SetPairCrouch(notnull KK_AttackPair pair, bool crouch)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (IsReleased(agent))
				continue;

			SetCrouch(agent, crouch);
		}
	}

	protected void SetCrouch(AIAgent agent, bool crouch)
	{
		if (!agent)
			return;

		bool wasCrouched = m_Crouched.Contains(agent);
		if (wasCrouched == crouch)
			return;

		IEntity body = agent.GetControlledEntity();
		if (!body)
			return;

		CharacterControllerComponent controller =
			CharacterControllerComponent.Cast(
				body.FindComponent(CharacterControllerComponent)
			);

		if (!controller)
			return;

		if (crouch)
		{
			controller.SetStanceChange(ECharacterStanceChange.STANCECHANGE_TOCROUCH);
			m_Crouched.Insert(agent);
			return;
		}

		controller.SetStanceChange(ECharacterStanceChange.STANCECHANGE_TOERECTED);
		m_Crouched.RemoveItem(agent);
	}

	protected void Scan(notnull KK_AttackPair pair, float now)
	{
		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return;

		vector direction = axis;
		if (now - pair.m_fPauseSince >= 1000)
		{
			direction = (axis * 0.75) + (right * (0.65 * pair.m_iSide));
			if (direction.Length() > 0.01)
				direction.Normalize();
		}

		LookAlong(pair, direction, SCAN_ASIDE);
	}

	protected void LookDownLane(notnull KK_AttackPair pair)
	{
		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return;

		LookAlong(pair, axis, SCAN_AHEAD);
	}

	protected void LookAlong(
		notnull KK_AttackPair pair,
		vector direction,
		float distance)
	{
		if (direction.Length() < 0.01)
			return;

		direction.Normalize();

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity() || IsReleased(agent))
				continue;

			if (LookAtHostile(agent))
				continue;

			vector eye = agent.GetControlledEntity().GetOrigin();
			eye = eye + Vector(0, 1.6, 0);
			LookAt(agent, eye + (direction * distance), LANE_LOOK);
		}
	}

	protected void LookAtContact(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || IsReleased(agent))
				continue;

			LookAtHostile(agent);
		}
	}

	// A man the perception list has already started on, in the front half
	// of the push. Behind the squad, the danger look still owns the head.
	// A bound sprint does not turn. Once the gun has a target, the attack
	// behavior owns the aim.
	protected bool LookAtHostile(AIAgent agent)
	{
		if (!agent || !agent.GetControlledEntity())
			return false;

		if (KK_GarrisonHold.IsBoundSprint(agent) || HasLivingTarget(agent))
			return false;

		IEntity enemy = KK_GarrisonHold.VisibleContact(agent);
		if (!enemy)
			return false;

		vector axis;
		vector right;
		if (BuildAxis(axis, right))
		{
			vector flat = enemy.GetOrigin() - agent.GetControlledEntity().GetOrigin();
			flat[1] = 0;
			if (flat.Length() > 0.5)
			{
				flat.Normalize();
				if (vector.Dot(flat, axis) < 0)
					return false;
			}
		}

		LookAt(agent, KK_GarrisonHold.ShotAimPoint(enemy), CONTACT_LOOK);
		return true;
	}

	// The hold leaves his head on the enemy. That look outranks the lane,
	// so it has to be replaced, not cancelled, or he finishes the turn
	// before the sprint.
	protected void SnapSprintLook(notnull AIAgent agent, vector goal)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return;

		if (!soldier.m_UtilityComponent.m_LookAction)
			return;

		IEntity body = agent.GetControlledEntity();
		if (!body)
			return;

		vector flat = goal - body.GetOrigin();
		flat[1] = 0;
		if (flat.Length() < 0.5)
			return;

		flat.Normalize();
		vector look = body.GetOrigin() + Vector(0, 1.6, 0) + (flat * 12);
		KK_GarrisonHold.SetSprintLook(body, look);
		soldier.m_UtilityComponent.m_LookAction.KK_Snap(
			look,
			SCR_AILookAction.PRIO_COMMANDER,
			8
		);
	}

	protected void SnapPairSprint(notnull KK_AttackPair pair)
	{
		vector axis;
		vector right;
		BuildAxis(axis, right);

		int slot = 0;
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			if (m_mSettledAt.Contains(agent) || IsReleased(agent))
				continue;

			if (!KK_GarrisonHold.IsBoundSprint(agent))
				continue;

			float side = -1.5;
			if (slot == 1)
				side = 1.5;

			slot++;
			SnapSprintLook(agent, pair.m_vGoal + (right * side));
		}
	}

	protected void LookAt(notnull AIAgent agent, vector point, float priority = 4)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return;

		if (!soldier.m_UtilityComponent.m_LookAction)
			return;

		soldier.m_UtilityComponent.m_LookAction.KK_Track(point, priority, 1.5);
	}

	protected void CancelLooks()
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
				CancelLook(agent);
		}
	}

	// Stop the step and shoot anyone this order can already see. Feet stay
	// put so the cover move cannot start until the burst is over.
	protected void UpdateReturnFire()
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!agent || !agent.GetControlledEntity() || IsReleased(agent))
					continue;

				if (!m_ReturnFireHeld.Contains(agent))
				{
					CancelAgent(agent);
					m_ReturnFireHeld.Insert(agent);
				}

				ApplyWeaponForSpeed(agent, EMovementType.IDLE);
				KK_GarrisonHold.SetPinned(agent.GetControlledEntity(), true);

				bool shooting = CanReturnFire(agent);
				KK_GarrisonHold.SetReturnFire(agent, shooting);
				if (!shooting)
					continue;

				SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
				if (soldier && soldier.m_UtilityComponent)
					KK_GarrisonHold.ApplyMoveFire(soldier.m_UtilityComponent);
			}
		}
	}

	protected bool CanReturnFire(AIAgent agent)
	{
		if (!agent)
			return false;

		IEntity body = agent.GetControlledEntity();
		if (!body)
			return false;

		if (KK_GarrisonHold.VisibleContact(body))
			return true;

		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (
			!soldier ||
			!soldier.m_UtilityComponent ||
			!soldier.m_UtilityComponent.m_CombatComponent
		)
		{
			return false;
		}

		return KK_GarrisonHold.SeesTarget(
			body,
			soldier.m_UtilityComponent.m_CombatComponent.GetCurrentTarget()
		);
	}

	protected void EndReturnFire()
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!agent)
					continue;

				KK_GarrisonHold.SetReturnFire(agent, false);

				IEntity body = agent.GetControlledEntity();
				if (body)
					KK_GarrisonHold.SetPinned(body, false);
			}
		}

		m_ReturnFireHeld.Clear();
	}

	protected void ReleaseBoundSprint()
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
				ApplyWeaponForSpeed(agent, EMovementType.IDLE);
		}
	}

	protected void CancelLook(AIAgent agent)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return;

		if (soldier.m_UtilityComponent.m_LookAction)
			soldier.m_UtilityComponent.m_LookAction.Cancel();
	}

	protected bool Reached(notnull KK_AttackPair pair)
	{
		float worst = 0;
		bool any = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			float distance = FlatDistance(
				agent.GetControlledEntity().GetOrigin(),
				pair.m_vGoal
			);

			if (!any || distance > worst)
				worst = distance;

			any = true;
		}

		if (!any)
			return false;

		return worst <= STEP_REACH;
	}

	protected bool GoalIsFinal(vector goal)
	{
		return FlatDistance(goal, m_vTarget) <= 2.5;
	}

	protected bool PairAtTarget(notnull KK_AttackPair pair)
	{
		float worst = 0;
		bool any = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			float distance = FlatDistance(
				agent.GetControlledEntity().GetOrigin(),
				m_vTarget
			);

			if (!any || distance > worst)
				worst = distance;

			any = true;
		}

		if (!any)
			return false;

		return worst <= Arrival();
	}

	protected bool AllNearTarget()
	{
		if (!HasSoldiers())
			return false;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair || !PairAlive(pair))
				continue;

			if (!PairAtTarget(pair))
				return false;
		}

		return true;
	}

	protected bool IsLeapfrog()
	{
		if (!PushByBounds())
			return false;

		return LivePairCount() >= 2;
	}

	protected bool IsTakeCover()
	{
		if (!m_AttackWaypoint)
			return false;

		return m_AttackWaypoint.GetPace() == KK_EAttackPace.TAKE_COVER;
	}

	// Bound and take cover both leapfrog. A take cover order with one pair
	// sprints alone instead.
	protected bool PushByBounds()
	{
		if (!m_AttackWaypoint)
			return false;

		KK_EAttackPace pace = m_AttackWaypoint.GetPace();
		return pace == KK_EAttackPace.BOUND ||
			pace == KK_EAttackPace.TAKE_COVER;
	}

	// One soldier of the sprinting pair reaches his slot and the move ends.
	// Until his partner arrives the sprint would leave him standing with the
	// rifle down. He holds that slot the same way the pair that is waiting
	// does, so the normal attack can shoot from there.
	protected void SettleArrivedMembers(notnull KK_AttackPair pair, float now)
	{
		// The sprint order from this frame has not started yet. A speed of
		// zero here is him standing at the start, not him waiting at the slot.
		if (now - pair.m_fLastOrderAt < 400)
			return;

		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return;

		int slot = 0;
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity() || IsReleased(agent))
				continue;

			float side = -1.5;
			if (slot == 1)
				side = 1.5;

			slot++;
			vector origin = agent.GetControlledEntity().GetOrigin();
			vector goal = pair.m_vGoal + (right * side);
			bool nearSlot = FlatDistance(origin, goal) <= STEP_REACH;
			bool nearGoal = FlatDistance(origin, pair.m_vGoal) <= STEP_REACH;
			bool stopped = FlatSpeed(agent) <= MEMBER_STOP;

			if ((!nearSlot && !nearGoal) || !stopped)
			{
				if (!m_mSettledAt.Contains(agent))
					continue;

				m_mSettledAt.Remove(agent);
				KK_GarrisonHold.SetPinned(agent.GetControlledEntity(), false);
				SetCrouch(agent, false);
				// The hold cancelled his move. The next maintain gives
				// the sprint back on this pass.
				pair.m_fLastOrderAt = 0;
				continue;
			}

			bool started = !m_mSettledAt.Contains(agent);
			if (started)
			{
				CancelAgent(agent);
				// Sprint keeps the rifle down in the engine. Walk now, and
				// send the stay on a later pass so this cancel cannot drop it.
				KK_AgentMove.SetWantedSpeed(agent, EMovementType.WALK);
				ApplyWeaponForSpeed(agent, EMovementType.WALK);
				KK_GarrisonHold.SetMoveFire(agent, false);
				SetCrouch(agent, true);
				m_mSettledAt.Set(agent, now);
			}
			else if (now - m_mSettledAt.Get(agent) >= KK_AgentMove.REISSUE_INTERVAL_MS)
			{
				IssueSettledStay(agent, now);
			}

			KK_GarrisonHold.SetPinned(agent.GetControlledEntity(), true);
			if (!HasLivingTarget(agent))
				LookAgentDownLane(agent, axis);
		}
	}

	protected bool PairStopped(notnull KK_AttackPair pair)
	{
		bool any = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			any = true;
			if (FlatSpeed(agent) > MEMBER_STOP)
				return false;
		}

		return any;
	}

	// Men who are down do not hold the next bound. A fighter has arrived
	// when he is on the step. One fighter who has stopped is enough cover
	// once the handoff time has run, so a partner still on the way does
	// not stall the pair that is waiting.
	protected bool CoverReady(notnull KK_AttackPair pair, float now)
	{
		int fighters = 0;
		int arrived = 0;
		bool stopped = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!CanFight(agent))
				continue;

			fighters++;
			float distance = FlatDistance(
				agent.GetControlledEntity().GetOrigin(),
				pair.m_vGoal
			);
			if (distance <= STEP_REACH)
				arrived++;

			if (FlatSpeed(agent) <= MEMBER_STOP)
				stopped = true;
		}

		// Nobody left who can cover. The pair that is waiting has to go.
		if (fighters == 0)
			return true;

		if (arrived == fighters)
			return true;

		if (!stopped || CoverHandoffMs() <= 0)
			return false;

		if (pair.m_fFirstStopAt <= 0)
			pair.m_fFirstStopAt = now;

		return now - pair.m_fFirstStopAt >= CoverHandoffMs();
	}

	protected bool FightersArrived(notnull KK_AttackPair pair)
	{
		bool any = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!CanFight(agent))
				continue;

			any = true;
			float distance = FlatDistance(
				agent.GetControlledEntity().GetOrigin(),
				pair.m_vGoal
			);
			if (distance > STEP_REACH)
				return false;
		}

		return any;
	}

	protected bool CanFight(AIAgent agent)
	{
		if (!agent)
			return false;

		IEntity body = agent.GetControlledEntity();
		if (!body)
			return false;

		CharacterControllerComponent controller = CharacterControllerComponent.Cast(
			body.FindComponent(CharacterControllerComponent)
		);
		if (!controller)
			return true;

		if (controller.IsDead() || controller.IsUnconscious())
			return false;

		return true;
	}

	protected float CoverHandoffMs()
	{
		if (!m_AttackWaypoint)
			return 2000;

		return m_AttackWaypoint.GetCoverHandoff() * 1000;
	}

	protected float FlatSpeed(notnull AIAgent agent)
	{
		IEntity body = agent.GetControlledEntity();
		if (!body)
			return 0;

		Physics physics = body.GetPhysics();
		if (!physics)
			return 0;

		vector velocity = physics.GetVelocity();
		velocity[1] = 0;
		return velocity.Length();
	}

	protected void IssueSettledStay(notnull AIAgent agent, float now)
	{
		IEntity body = agent.GetControlledEntity();
		if (!body)
			return;

		KK_AgentMove.Issue(
			this,
			m_Group,
			agent,
			body.GetOrigin(),
			m_mSoloHandlers,
			KK_AgentMove.PRIORITY_LEVEL,
			EMovementType.WALK
		);
		ApplyWeaponForSpeed(agent, EMovementType.WALK);
		m_mSettledAt.Set(agent, now);
	}

	protected void LookAgentDownLane(notnull AIAgent agent, vector axis)
	{
		if (!agent.GetControlledEntity())
			return;

		if (axis.Length() < 0.01)
			return;

		if (LookAtHostile(agent))
			return;

		vector eye = agent.GetControlledEntity().GetOrigin() + Vector(0, 1.6, 0);
		LookAt(agent, eye + (axis * SCAN_AHEAD), LANE_LOOK);
	}

	protected void ForgetPairSettled(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (agent && m_mSettledAt.Contains(agent))
				m_mSettledAt.Remove(agent);
		}
	}

	protected void ReleaseSettled()
	{
		if (m_mSettledAt.Count() == 0)
			return;

		array<AIAgent> waiting = {};
		for (int i = 0; i < m_mSettledAt.Count(); i++)
			waiting.Insert(m_mSettledAt.GetKey(i));

		foreach (AIAgent agent : waiting)
		{
			if (!agent)
				continue;

			IEntity body = agent.GetControlledEntity();
			if (body)
				KK_GarrisonHold.SetPinned(body, false);
		}

		m_mSettledAt.Clear();
	}

	// The pair that is not sprinting. The normal attack fires. A move above
	// that attack was the selected behavior, so the attack never shot, and
	// the indoor shot trace hits the cover he is pinned against.
	protected void CoverHold(notnull KK_AttackPair pair, float now)
	{
		// The cancel and the stay cannot share a frame. The cancel is for this
		// order, and it can drop a move that was queued after it.
		bool started = !pair.m_bHolding;
		if (started)
			BeginHold(pair);

		ClearHoldFight(pair);
		SetPairCrouch(pair, true);

		if (
			!started &&
			now - pair.m_fLastOrderAt >= KK_AgentMove.REISSUE_INTERVAL_MS
		)
			IssueStay(pair, now);

		// After the stay order, so a walk speed does not clear the lock.
		SetPairPinned(pair, true);

		if (!PairHasLivingTarget(pair))
			LookDownLane(pair);
	}

	// Under the normal attack. It keeps him from wandering when he has no
	// target. It does not beat the attack, so a living enemy still gets shot.
	protected void IssueStay(notnull KK_AttackPair pair, float now)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity() || IsReleased(agent))
				continue;

			KK_AgentMove.Issue(
				this,
				m_Group,
				agent,
				agent.GetControlledEntity().GetOrigin(),
				m_mSoloHandlers,
				KK_AgentMove.PRIORITY_LEVEL,
				EMovementType.WALK
			);
			ApplyWeaponForSpeed(agent, EMovementType.WALK);
		}

		pair.m_fLastOrderAt = now;
	}

	protected void SetPairPinned(notnull KK_AttackPair pair, bool pinned)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || IsReleased(agent))
				continue;

			IEntity body = agent.GetControlledEntity();
			if (!body)
				continue;

			KK_GarrisonHold.SetPinned(body, pinned);
		}
	}

	protected bool PairHasLivingTarget(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (HasLivingTarget(agent))
				return true;
		}

		return false;
	}

	protected void ClearHoldFight(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent)
				continue;

			KK_GarrisonHold.SetMoveFire(agent, false);
		}
	}

	protected void UpdateHold(float now)
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (pair)
				SetPairPinned(pair, false);
		}

		if (FightFromPoint())
		{
			UpdateHoldFight(now);
			return;
		}

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair || !PairAlive(pair))
				continue;

			if (!pair.m_bHasGoal)
			{
				pair.m_vGoal = CoverNear(pair);
				pair.m_bHasGoal = true;
			}

			if (FlatDistance(PairCenter(pair), pair.m_vGoal) > STEP_REACH)
			{
				pair.m_bArrived = false;
				IssuePair(pair, EMovementType.WALK, now);
				continue;
			}

			pair.m_bArrived = true;
			if (now - pair.m_fLastOrderAt >= KK_AgentMove.REISSUE_INTERVAL_MS)
				IssuePair(pair, EMovementType.WALK, now);

			SetPairCrouch(pair, true);
			AimHold(pair);
		}
	}

	// Normal attack owns the fight. A soldier who reaches the point is
	// released on his own. One who then leaves the return distance is walked
	// back into cover there, then released again.
	protected void UpdateHoldFight(float now)
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair || !PairAlive(pair))
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!agent || !agent.GetControlledEntity())
					continue;

				float distance = FlatDistance(
					agent.GetControlledEntity().GetOrigin(),
					m_vTarget
				);

				if (IsReleased(agent))
				{
					if (distance > ReturnDistance())
						WalkSoldierBack(agent, pair, now);

					continue;
				}

				if (distance > Arrival())
					continue;

				YieldSoldier(agent);
				m_Released.Insert(agent);
			}
		}
	}

	protected float ReturnDistance()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 40;

		return mode.KK_GetTakeCoverReturn();
	}

	protected bool FightFromPoint()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetTakeCoverAttack();
	}

	protected bool PairOutside(notnull KK_AttackPair pair, float leash)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			float distance = FlatDistance(
				agent.GetControlledEntity().GetOrigin(),
				m_vTarget
			);
			if (distance > leash)
				return true;
		}

		return false;
	}

	protected vector CoverAtTarget(notnull KK_AttackPair pair)
	{
		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return OnNavmesh(m_vTarget);

		float stagger = 2;
		if (pair.m_iIndex % 2 == 0)
			stagger = -2;

		return PreferCover(m_vTarget + (right * stagger), axis, right, true);
	}

	protected void YieldPair(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || IsReleased(agent))
				continue;

			YieldSoldier(agent);
		}
	}

	// He is inside the finish distance. The push lets him go, and the attack
	// takes the fight from here.
	protected void YieldSoldier(AIAgent agent)
	{
		if (!agent)
			return;

		CancelAgent(agent);
		KK_GarrisonHold.SetRecalled(agent, false);
		KK_GarrisonHold.SetMoveFire(agent, false);

		IEntity body = agent.GetControlledEntity();
		if (body)
			KK_GarrisonHold.SetPinned(body, false);

		if (m_mSettledAt.Contains(agent))
			m_mSettledAt.Remove(agent);

		ApplyWeaponForSpeed(agent, EMovementType.IDLE);
		SetCrouch(agent, false);
		CancelLook(agent);
	}

	protected void WalkSoldierBack(
		AIAgent agent,
		notnull KK_AttackPair pair,
		float now)
	{
		if (!agent)
			return;

		if (
			m_mReturnAt.Contains(agent) &&
			now - m_mReturnAt.Get(agent) < KK_AgentMove.REISSUE_INTERVAL_MS
		)
			return;

		IEntity body = agent.GetControlledEntity();
		if (body)
			KK_GarrisonHold.SetPinned(body, false);

		// The mod stays on for anyone still at the point. This run has to
		// stay selected over that fight, or he stops and shoots instead.
		KK_AgentMove.Issue(
			this,
			m_Group,
			agent,
			CoverAtTarget(pair),
			m_mSoloHandlers,
			KK_AgentMove.SprintPriorityLevel(),
			EMovementType.RUN
		);
		ApplyWeaponForSpeed(agent, EMovementType.RUN);
		KK_GarrisonHold.SetRecalled(agent, true);
		SetCrouch(agent, false);
		m_mReturnAt.Set(agent, now);
	}

	// A man who has reached the point leaves the push on his own. The men
	// still short of it keep bounding. With the attack handoff on, he is
	// given to that attack as soon as he is inside the finish distance.
	protected void UpdateArrivals(float now)
	{
		if (!FightFromPoint())
			return;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!agent || !agent.GetControlledEntity())
					continue;

				float distance = FlatDistance(
					agent.GetControlledEntity().GetOrigin(),
					m_vTarget
				);

				if (IsReleased(agent))
				{
					if (distance > ReturnDistance())
					{
						WalkSoldierBack(agent, pair, now);
						continue;
					}

					if (m_mReturnAt.Contains(agent) && distance <= Arrival())
					{
						m_mReturnAt.Remove(agent);
						YieldSoldier(agent);
					}

					continue;
				}

				if (distance > Arrival())
					continue;

				YieldSoldier(agent);
				m_Released.Insert(agent);
			}
		}
	}

	protected bool IsReleased(AIAgent agent)
	{
		return agent && m_Released.Contains(agent);
	}

	// True while someone handed to the attack is still on the point.
	// A man being walked back does not take the mod off the others.
	// Men still on the way do not hold this back. If every handed man
	// is on the way back, the mod waits until one of them is in again.
	bool ReleasedToFight()
	{
		if (!IsTakeCover() || !FightFromPoint())
			return false;

		bool any = false;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!IsReleased(agent) || !agent.GetControlledEntity())
					continue;

				if (m_mReturnAt.Contains(agent))
					continue;

				float distance = FlatDistance(
					agent.GetControlledEntity().GetOrigin(),
					m_vTarget
				);
				if (distance > ReturnDistance())
					continue;

				any = true;
			}
		}

		return any;
	}

	protected vector CoverNear(notnull KK_AttackPair pair)
	{
		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return OnNavmesh(m_vTarget);

		return PreferCover(PairCenter(pair), axis, right, true);
	}

	protected void AimHold(notnull KK_AttackPair pair)
	{
		bool aimed = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			IEntity enemy = LivingEnemy(agent);
			if (!enemy)
				continue;

			vector point = enemy.GetOrigin();
			point = point + Vector(0, 1.4, 0);
			LookAt(agent, point);
			aimed = true;
		}

		if (!aimed)
			LookDownLane(pair);
	}

	protected IEntity LivingEnemy(AIAgent agent)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return null;

		if (!soldier.m_UtilityComponent.m_CombatComponent)
			return null;

		BaseTarget target =
			soldier.m_UtilityComponent.m_CombatComponent.GetCurrentTarget();
		if (!target || !KK_GarrisonHold.IsLivingTarget(target))
			return null;

		return target.GetTargetEntity();
	}

	protected bool RunnerIsLive()
	{
		KK_AttackPair runner = PairAt(m_iRunner);
		if (!runner || !PairAlive(runner))
			return false;

		return !PairAtTarget(runner);
	}

	protected int NextRunner(int from)
	{
		int count = m_aPairs.Count();

		for (int step = 1; step <= count; step++)
		{
			int index = (from + step) % count;
			KK_AttackPair pair = PairAt(index);
			if (!pair || !PairAlive(pair))
				continue;

			if (PairAtTarget(pair))
				continue;

			return index;
		}

		return -1;
	}

	protected int LivePairCount()
	{
		int count = 0;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (pair && PairAlive(pair))
				count++;
		}

		return count;
	}

	protected bool AnyContact()
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (HasLivingTarget(agent))
					return true;
			}
		}

		return false;
	}

	protected bool HasLivingTarget(AIAgent agent)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return false;

		if (!soldier.m_UtilityComponent.m_CombatComponent)
			return false;

		return KK_GarrisonHold.IsLivingTarget(
			soldier.m_UtilityComponent.m_CombatComponent.GetCurrentTarget()
		);
	}

	protected void RefreshPairs()
	{
		array<AIAgent> agents = {};
		if (m_Group)
			m_Group.GetAgents(agents);

		foreach (AIAgent agent : agents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			KK_GarrisonHold.SetAdvancing(agent, true);
			KK_GarrisonHold.SetAttackSearch(agent, true);
			KK_PerceptionBoost.ApplyRecognition(agent, m_mPerceptionFactors);

			if (m_mPairOf.Contains(agent))
				continue;

			int pairIndex = OpenPair();
			m_mPairOf.Set(agent, pairIndex);
			EnsurePair(pairIndex);
			KK_GarrisonHold.NoteOrderSoldier(agent);
			KK_GarrisonHold.ReleaseFollowLocks(agent);
		}

		array<AIAgent> gone = {};
		for (int i = 0; i < m_mPairOf.Count(); i++)
		{
			AIAgent agent = m_mPairOf.GetKey(i);
			if (!agent || !agent.GetControlledEntity())
				gone.Insert(agent);
		}

		foreach (AIAgent agent : gone)
		{
			if (agent)
			{
				IEntity body = agent.GetControlledEntity();
				if (body)
					KK_GarrisonHold.SetPinned(body, false);

				KK_PerceptionBoost.RestoreRecognition(agent, m_mPerceptionFactors);
				KK_GarrisonHold.SetAttackSearch(agent, false);
				KK_GarrisonHold.SetBoundSprint(agent, false);
				KK_GarrisonHold.SetRecalled(agent, false);
				KK_GarrisonHold.SetAdvancing(agent, false);
			}

			m_mPairOf.Remove(agent);
			if (m_mDoors.Contains(agent))
				m_mDoors.Remove(agent);
			if (m_mSettledAt.Contains(agent))
				m_mSettledAt.Remove(agent);
			if (m_Released.Contains(agent))
				m_Released.RemoveItem(agent);
			if (m_mReturnAt.Contains(agent))
				m_mReturnAt.Remove(agent);
		}

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (pair && pair.m_aAgents)
				pair.m_aAgents.Clear();
		}

		for (int i = 0; i < m_mPairOf.Count(); i++)
		{
			AIAgent agent = m_mPairOf.GetKey(i);
			int index = m_mPairOf.GetElement(i);
			KK_AttackPair pair = EnsurePair(index);
			if (pair && agent)
				pair.m_aAgents.Insert(agent);
		}
	}

	protected int OpenPair()
	{
		for (int i = 0; i < m_aPairs.Count(); i++)
		{
			if (PairSize(i) < 2)
				return i;
		}

		return m_aPairs.Count();
	}

	protected int PairSize(int index)
	{
		int count = 0;

		for (int i = 0; i < m_mPairOf.Count(); i++)
		{
			if (m_mPairOf.GetElement(i) == index)
				count++;
		}

		return count;
	}

	protected KK_AttackPair EnsurePair(int index)
	{
		while (m_aPairs.Count() <= index)
		{
			KK_AttackPair created = new KK_AttackPair();
			created.m_iIndex = m_aPairs.Count();
			created.m_iSide = 1;
			m_aPairs.Insert(created);
		}

		return m_aPairs[index];
	}

	protected KK_AttackPair PairAt(int index)
	{
		if (index < 0 || index >= m_aPairs.Count())
			return null;

		return m_aPairs[index];
	}

	protected bool PairAlive(KK_AttackPair pair)
	{
		if (!pair)
			return false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (agent && agent.GetControlledEntity())
				return true;
		}

		return false;
	}

	protected bool HasSoldiers()
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (PairAlive(pair))
				return true;
		}

		return false;
	}

	protected vector PairCenter(notnull KK_AttackPair pair)
	{
		vector sum = "0 0 0";
		int count = 0;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			sum = sum + agent.GetControlledEntity().GetOrigin();
			count++;
		}

		if (count == 0)
			return m_vOrigin;

		return sum * (1.0 / count);
	}

	protected void CaptureOrigin()
	{
		m_vTarget = m_AttackWaypoint.GetOrigin();
		vector sum = "0 0 0";
		int count = 0;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!agent || !agent.GetControlledEntity())
					continue;

				sum = sum + agent.GetControlledEntity().GetOrigin();
				count++;
			}
		}

		if (count == 0)
			m_vOrigin = m_vTarget;
		else
			m_vOrigin = sum * (1.0 / count);

		m_bOriginSet = true;
	}

	protected bool BuildAxis(out vector axis, out vector right)
	{
		axis = "0 0 1";
		right = "1 0 0";

		vector flat = m_vTarget - m_vOrigin;
		flat[1] = 0;
		if (flat.Length() < 0.5)
			return false;

		flat.Normalize();
		axis = flat;
		right = Vector(-axis[2], 0, axis[0]);
		return true;
	}

	protected void NotePaceChange()
	{
		if (!m_AttackWaypoint)
			return;

		KK_EAttackPace pace = m_AttackWaypoint.GetPace();
		if (m_bPaceSet && pace == m_eApplied)
			return;

		EndReturnFire();
		m_bYielding = false;
		m_bReturnFired = false;
		m_fReturnFireSince = 0;
		m_bPaceSet = true;
		m_eApplied = pace;
		m_iRunner = 0;
		m_bAtPoint = false;
		m_Released.Clear();
		m_mReturnAt.Clear();

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
				KK_GarrisonHold.SetRecalled(agent, false);

			pair.m_bHasGoal = false;
			pair.m_bHolding = false;
			pair.m_bPaused = false;
			pair.m_bArrived = false;
			SetPairPinned(pair, false);
			ForgetPairSettled(pair);
			pair.m_fProgressBoost = 0;
			pair.m_iNudges = 0;
			pair.m_fFirstStopAt = 0;
		}
	}

	protected float StepLength()
	{
		if (!m_AttackWaypoint)
			return 20;

		return m_AttackWaypoint.GetStepLength();
	}

	protected float PauseMs()
	{
		if (!m_AttackWaypoint)
			return 2000;

		return m_AttackWaypoint.GetPause() * 1000;
	}

	protected float ReturnFireMs()
	{
		if (!m_AttackWaypoint)
			return 2000;

		return m_AttackWaypoint.GetReturnFire() * 1000;
	}

	protected float LaneSpacing()
	{
		if (!m_AttackWaypoint)
			return 12;

		return m_AttackWaypoint.GetLaneOffset();
	}

	protected float Arrival()
	{
		if (!m_AttackWaypoint)
			return 8;

		return m_AttackWaypoint.GetArrivalRadius();
	}

	protected float FlatDistance(vector a, vector b)
	{
		vector flatA = a;
		vector flatB = b;
		flatA[1] = 0;
		flatB[1] = 0;
		return vector.Distance(flatA, flatB);
	}

	protected void CancelAgent(AIAgent agent)
	{
		if (!agent || !m_Utility)
			return;

		SCR_AIMessage_Cancel message = SCR_AIMessage_Cancel.Create(this);
		message.SetReceiver(agent);
		m_Utility.m_Mailbox.RequestBroadcast(message, agent);
	}

	protected void ReleaseSoldiers()
	{
		EndReturnFire();
		m_bYielding = false;
		m_bReturnFired = false;
		m_fReturnFireSince = 0;

		for (int i = 0; i < m_mPairOf.Count(); i++)
		{
			AIAgent agent = m_mPairOf.GetKey(i);
			if (!agent)
				continue;

			KK_PerceptionBoost.RestoreRecognition(agent, m_mPerceptionFactors);
			KK_GarrisonHold.SetAttackSearch(agent, false);
			KK_GarrisonHold.SetBoundSprint(agent, false);
			KK_GarrisonHold.SetRecalled(agent, false);
			KK_GarrisonHold.SetAdvancing(agent, false);
			KK_GarrisonHold.SetMoveFire(agent, false);
			IEntity body = agent.GetControlledEntity();
			if (body)
				KK_GarrisonHold.SetPinned(body, false);
			SetCrouch(agent, false);
			SetWeapon(agent, false);
			CancelLook(agent);
		}

		SendCancelMessagesToAllAgents();
		KK_GarrisonHold.ReleaseOrderGroup(m_Group);
	}

	protected void SetWeapon(AIAgent agent, bool raised)
	{
		if (!agent)
			return;

		IEntity body = agent.GetControlledEntity();
		if (!body)
			return;

		CharacterControllerComponent controller =
			CharacterControllerComponent.Cast(
				body.FindComponent(CharacterControllerComponent)
			);

		if (controller)
			controller.SetWeaponRaised(raised);
	}

	protected override void SendCancelMessagesToAllAgents()
	{
		if (m_Group && m_Utility)
		{
			array<AIAgent> agents = {};
			m_Group.GetAgents(agents);

			foreach (AIAgent agent : agents)
			{
				if (!agent)
					continue;

				CancelAgent(agent);
				KK_GarrisonHold.ReleaseFollowLocks(agent);
			}
		}

		KK_AgentMove.ReleaseHandlers(m_Group, m_mSoloHandlers);
	}

	void CancelAttack()
	{
		if (m_bCancelled || m_bFinished)
			return;

		m_bCancelled = true;
		ReleaseSoldiers();

		if (SCR_BaseGameMode.KK_LogEnabled())
			Print("KK: Attack activity cancelled");

		SCR_AIGroup group = m_Group;
		KK_AttackWaypoint waypoint = m_AttackWaypoint;
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

	protected void CompleteAttack()
	{
		if (m_bFinished)
			return;

		m_bFinished = true;
		ReleaseSoldiers();

		if (SCR_BaseGameMode.KK_LogEnabled())
			Print("KK: Attack completed");

		SCR_AIGroup group = m_Group;
		KK_AttackWaypoint waypoint = m_AttackWaypoint;
		Fail(true);

		if (group && waypoint)
			group.CompleteWaypoint(waypoint);
	}

	protected void AbortAttack(string reason)
	{
		if (m_bFinished)
			return;

		m_bFinished = true;
		ReleaseSoldiers();

		if (SCR_BaseGameMode.KK_LogEnabled())
			PrintFormat("KK: Attack aborted: %1", reason);

		SCR_AIGroup group = m_Group;
		KK_AttackWaypoint waypoint = m_AttackWaypoint;
		Fail(true);

		if (group && waypoint)
			group.CompleteWaypoint(waypoint);
	}

	void Supersede()
	{
		if (m_bCancelled || m_bFinished)
			return;

		m_bFinished = true;
		ReleaseSoldiers();
		SetActionState(EAIActionState.FAILED);
		SetRemoveAction(true);
	}

	bool IsLive()
	{
		if (m_bCancelled || m_bFinished || !m_AttackWaypoint)
			return false;

		EAIActionState state = GetActionState();
		return state != EAIActionState.FAILED &&
			state != EAIActionState.COMPLETED;
	}

	protected bool WaypointStillAssigned()
	{
		if (!m_Group || !m_AttackWaypoint)
			return false;

		array<AIWaypoint> waypoints = {};
		m_Group.GetWaypoints(waypoints);

		foreach (AIWaypoint waypoint : waypoints)
		{
			if (waypoint == m_AttackWaypoint)
				return true;
		}

		return false;
	}

	protected bool OrderWasReplaced()
	{
		return m_AttackWaypoint && m_AttackWaypoint.IsReplaced();
	}

	protected void RetainAfterRestart()
	{
		m_bRetain = true;
		SetActionState(EAIActionState.EVALUATED);
		SetRemoveAction(false);

		if (SCR_BaseGameMode.KK_LogEnabled())
			Print("KK: Attack kept running after the group activity restarted");
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

		CancelAttack();
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

		CancelAttack();
	}

	override string GetActionDebugInfo()
	{
		return string.Format(
			"KK Attack: %1 pairs",
			m_aPairs.Count()
		);
	}
}

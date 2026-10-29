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
	float m_fRifleAt;
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

// Advance steps one soldier. His partner can be across the squad.
// A shared pair goal made him stand until that partner arrived.
class KK_AttackMover
{
	vector m_vGoal;
	bool m_bHasGoal;
	bool m_bPaused;
	bool m_bArrived;
	float m_fPauseSince;
	int m_iSide;
	float m_fLastOrderAt;
	float m_fStillSince;
	vector m_vStillPosition;
	int m_iNudges;
	float m_fProgressBoost;
	// This step is the one on the point. He holds his own lane there.
	bool m_bFinal;

	void KK_AttackMover()
	{
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
	protected ref map<AIAgent, ref KK_AttackMover> m_mMovers =
		new map<AIAgent, ref KK_AttackMover>();
	protected ref map<AIAgent, ref KK_FormationSplit> m_mSoloHandlers =
		new map<AIAgent, ref KK_FormationSplit>();
	protected ref map<AIAgent, IEntity> m_mDoors =
		new map<AIAgent, IEntity>();
	protected ref set<AIAgent> m_Crouched = new set<AIAgent>();
	// A bound member who has reached his own slot. His partner may still be
	// sprinting. The map value is the last time his stay was issued.
	protected ref map<AIAgent, float> m_mSettledAt = new map<AIAgent, float>();
	// On the take cover point, and handed to the attack. The push skips him
	// even when the rest of the squad is still on the way. Until the attack
	// has a target, he is not in this set: he is walking to cover there.
	protected ref set<AIAgent> m_Released = new set<AIAgent>();
	protected ref map<AIAgent, float> m_mReturnAt = new map<AIAgent, float>();
	protected ref map<AIAgent, float> m_mCoverAt = new map<AIAgent, float>();
	// Cover spots around the point, in order around the circle. Each man
	// who arrives takes the next one.
	protected ref array<vector> m_aPointCovers = {};
	protected bool m_bPointCovers;
	protected ref map<AIAgent, int> m_mCoverSlot = new map<AIAgent, int>();
	protected ref map<AIAgent, float> m_mCoverHold = new map<AIAgent, float>();
	protected int m_iCoverAssign;
	protected bool m_bCoverFought;
	protected float m_fFightClearSince;
	protected ref map<AIAgent, float> m_mPerceptionFactors =
		new map<AIAgent, float>();

	protected ref TraceParam m_CoverTrace;

	protected vector m_vOrigin;
	protected vector m_vTarget;
	protected bool m_bOriginSet;
	protected bool m_bNoted;
	protected bool m_bFinished;
	protected bool m_bCancelled;
	protected bool m_bRetain;
	protected int m_iRunner;
	protected KK_EAttackPace m_eApplied;
	protected bool m_bPaceSet;
	protected bool m_bAtPoint;
	// Set by the last PlanGoal call. The mover copies it onto its own step.
	protected bool m_bLastPlanFinal;
	protected float m_fEmptySince;
	// Wall time of the last CustomEvaluate. Engage plants freeze move
	// clocks by this frame's delta so stuck, pause, and handoff wait.
	protected float m_fEvalAt;
	protected float m_fEvalDt;
	// Medic and the men he is treating. The push does not issue their step.
	protected ref set<AIAgent> m_Treatment = new set<AIAgent>();
	protected AIAgent m_Medic;
	protected AIAgent m_MedicPatient;
	protected float m_fMedicOrderAt;
	protected bool m_bMedicHeld;

	protected static const float STEP_REACH = 4;
	// Close enough to the cover spot to plant. Looser than this and he
	// stops short of the wall and stands in the open.
	protected static const float COVER_REACH = 1.25;
	// Men at the point stand this far apart. Closer than this and the
	// squad stacks on one corner.
	protected static const float COVER_SPACING = 6;
	// After the last target drops, keep the attack in charge this long
	// so a flicker does not plant him back in the cover.
	protected static const float FIGHT_CLEAR_MS = 4000;
	// Threatened at the point, and the round still cannot reach him.
	// He leaves that cover instead of standing in it.
	protected static const float COVER_LANE_STUCK_MS = 2000;
	// Below this he has stopped. Faster than this he is still on the sprint,
	// and taking the sprint flag off turns that move into a sidestep.
	protected static const float MEMBER_STOP = 1.25;
	// Long enough that a raise already in progress is not restarted. The
	// crouch on a bound stop can eat the first one, and this sends another
	// while the rifle is still down.
	protected static const float OVERWATCH_RAISE_MS = 800;
	protected static const float STUCK_MS = 8000;
	protected static const float EMPTY_MS = 10000;
	protected static const float SCAN_AHEAD = 40;
	protected static const float SCAN_ASIDE = 30;
	// Below an unidentified-target look (50) and an enemy look (80). Above
	// a danger glance (20) and the lane scan, so a seen man wins the head
	// without restarting the look the attack behavior already owns.
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

		if (m_fEvalAt > 0 && now >= m_fEvalAt)
			m_fEvalDt = now - m_fEvalAt;
		else
			m_fEvalDt = 0;
		m_fEvalAt = now;

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

		// On the point with another order behind this one, the push ends
		// and that order starts. With nothing queued, every pace holds
		// the way take cover does, and the attack stays in charge.
		// A medic left behind holds that handoff until he and the wounded
		// man are in, and until the squad has treated what it can.
		bool startedOnPoint = FlatDistance(m_vOrigin, m_vTarget) <= Arrival();
		bool squadAtPoint = m_bAtPoint || startedOnPoint || AllNearTarget();
		bool waypointFollows = HasFollowingWaypoint();
		bool medic = KK_GarrisonHold.AttackMedicEnabled();
		bool contact = SquadContactOpen();
		if (!medic || contact)
			EndTreatment(contact);
		else
			UpdateTreatment(now);

		bool fightersIn = FightersInside();
		if (!medic && squadAtPoint && waypointFollows)
		{
			CompleteAttack();
			return 0;
		}

		if (medic && waypointFollows && (fightersIn || squadAtPoint))
		{
			if (contact)
			{
				if ((squadAtPoint || WalkersInside()) && !GestureInProgress())
				{
					CompleteAttack();
					return 0;
				}
			}
			else
			{
				UpdateArrivals(now);
				RecallStragglers(now);
				if (!TreatmentBlocksHandoff() && WalkersInside())
				{
					CompleteAttack();
					return 0;
				}

				return GetPriority();
			}
		}

		NotePaceChange();

		// Take cover plants as each man arrives. Advance and bound keep
		// their push until the squad is there, then use that same plant.
		bool holdAtPoint = !waypointFollows && (IsTakeCover() || squadAtPoint || (medic && fightersIn));
		if (holdAtPoint)
		{
			UpdateArrivals(now);
			if (medic)
				RecallStragglers(now);
		}

		// Finish distance ends the push. After that, only the return distance
		// pulls them back. Leaving the finish radius must not restart the push.
		bool pushFinished = squadAtPoint || (medic && fightersIn);
		if (holdAtPoint && pushFinished)
		{
			m_bAtPoint = true;
			return GetPriority();
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
	// Each soldier steps from where he is. The pair lane is only the
	// spacing. A partner still far behind does not hold him or drop the rifle.
	protected void UpdateAdvance(float now)
	{
		// Leapfrog may have pinned a soldier who arrived ahead of his partner.
		// This path moves every soldier, so that pin has to come off.
		ReleaseSettled();

		EMovementType speed = EMovementType.RUN;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair || !PairAlive(pair))
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!agent || !agent.GetControlledEntity() || IsReleased(agent))
					continue;

				UpdateAdvanceAgent(agent, pair, MoverFor(agent), speed, now);
			}
		}
	}

	protected void UpdateAdvanceAgent(
		notnull AIAgent agent,
		notnull KK_AttackPair pair,
		notnull KK_AttackMover mover,
		EMovementType speed,
		float now)
	{
		if (ServingTreatment(agent))
			return;

		if (mover.m_bArrived)
		{
			LookAgentAlong(agent, AdvanceAxis(), SCAN_AHEAD);
			return;
		}

		// Engage owns his feet. Leave the step, pause, and stuck clock
		// where they were until the plant ends.
		if (EngagePlantHolds(agent))
		{
			HoldEngageMove(agent, mover, now);
			return;
		}

		if (!mover.m_bHasGoal)
		{
			StartAgentStep(agent, pair, mover, speed, now);
			return;
		}

		if (AgentReached(agent, mover))
		{
			if (mover.m_bFinal)
			{
				mover.m_bArrived = true;
				StopAdvanceAgent(agent);
				LookAgentAlong(agent, AdvanceAxis(), SCAN_AHEAD);
				return;
			}

			if (PauseMs() <= 0)
			{
				mover.m_iSide = -mover.m_iSide;
				if (mover.m_iSide == 0)
					mover.m_iSide = 1;
				StartAgentStep(agent, pair, mover, speed, now);
				return;
			}

			if (!mover.m_bPaused)
			{
				mover.m_bPaused = true;
				mover.m_fPauseSince = now;
				mover.m_iSide = -mover.m_iSide;
				if (mover.m_iSide == 0)
					mover.m_iSide = 1;
				StopAdvanceAgent(agent);
			}

			ScanAgent(agent, mover, now);

			if (now - mover.m_fPauseSince >= PauseMs())
			{
				mover.m_bPaused = false;
				mover.m_bHasGoal = false;
				StartAgentStep(agent, pair, mover, speed, now);
			}

			return;
		}

		MaintainAgent(agent, pair, mover, speed, now);
		KK_GarrisonHold.SnapAimPose(agent.GetControlledEntity(), vector.Zero);
	}

	protected KK_AttackMover MoverFor(notnull AIAgent agent)
	{
		if (m_mMovers.Contains(agent))
			return m_mMovers.Get(agent);

		KK_AttackMover mover = new KK_AttackMover();
		m_mMovers.Set(agent, mover);
		return mover;
	}

	protected void StartAgentStep(
		notnull AIAgent agent,
		notnull KK_AttackPair pair,
		notnull KK_AttackMover mover,
		EMovementType speed,
		float now)
	{
		IEntity body = agent.GetControlledEntity();
		if (!body)
			return;

		KK_GarrisonHold.SetPinned(body, false);
		KK_GarrisonHold.SetReturnFire(agent, false);
		mover.m_bPaused = false;
		mover.m_bArrived = false;

		vector from = body.GetOrigin();
		mover.m_vGoal = PlanGoal(from, mover.m_fProgressBoost, pair.m_iIndex, body);
		// He is already on a step that is still short of the point. On the
		// point itself the lane stays, and the side slot is still ahead.
		if (!m_bLastPlanFinal && FlatDistance(from, mover.m_vGoal) <= STEP_REACH)
		{
			mover.m_fProgressBoost = mover.m_fProgressBoost + StepLength();
			mover.m_vGoal = PlanGoal(from, mover.m_fProgressBoost, pair.m_iIndex, body);
		}

		mover.m_bFinal = m_bLastPlanFinal;

		mover.m_bHasGoal = true;
		mover.m_fStillSince = now;
		mover.m_vStillPosition = from;
		mover.m_iNudges = 0;
		SetCrouch(agent, false);
		IssueAgent(agent, pair, mover, speed, now);
	}

	protected void MaintainAgent(
		notnull AIAgent agent,
		notnull KK_AttackPair pair,
		notnull KK_AttackMover mover,
		EMovementType speed,
		float now)
	{
		IEntity body = agent.GetControlledEntity();
		if (!body)
			return;

		if (EngagePlantHolds(agent))
		{
			HoldEngageMove(agent, mover, now);
			return;
		}

		if (AgentDoor(agent, mover.m_vGoal))
		{
			mover.m_fStillSince = now;
			return;
		}

		vector origin = body.GetOrigin();
		// A climb is slow, and a high wall holds him on top. That is not a stall.
		if (KK_AgentMove.CrossingObstacle(agent))
		{
			mover.m_vStillPosition = origin;
			mover.m_fStillSince = now;
		}
		else if (FlatDistance(origin, mover.m_vStillPosition) > 0.45)
		{
			mover.m_vStillPosition = origin;
			mover.m_fStillSince = now;
		}
		else if (
			mover.m_fStillSince > 0 &&
			now - mover.m_fStillSince >= STUCK_MS
		)
		{
			NudgeAgent(agent, pair, mover, speed, now);
			return;
		}

		if (now - mover.m_fLastOrderAt >= KK_AgentMove.REISSUE_INTERVAL_MS)
			IssueAgent(agent, pair, mover, speed, now);
		else
		{
			KK_AgentMove.SetWantedSpeed(agent, speed);
			ApplyWeaponForSpeed(agent, speed);
			if (speed == EMovementType.SPRINT && KK_GarrisonHold.IsBoundSprint(agent))
			{
				vector axis;
				vector right;
				BuildAxis(axis, right);
				if (!KK_AgentMove.CrossingObstacle(agent))
				{
					SnapSprintLook(
						agent,
						mover.m_vGoal + (right * MemberSide(pair, agent))
					);
				}
			}
		}
	}

	protected void NudgeAgent(
		notnull AIAgent agent,
		notnull KK_AttackPair pair,
		notnull KK_AttackMover mover,
		EMovementType speed,
		float now)
	{
		mover.m_iNudges++;
		mover.m_fStillSince = now;

		if (mover.m_iNudges >= 3)
		{
			mover.m_fProgressBoost = mover.m_fProgressBoost + StepLength();
			mover.m_bHasGoal = false;
			mover.m_iNudges = 0;

			if (SCR_BaseGameMode.KK_LogEnabled())
				Print("KK: Attack soldier skipped a stuck step");

			return;
		}

		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return;

		float side = 4;
		if (Math.Mod(mover.m_iNudges, 2) == 0)
			side = -4;

		mover.m_vGoal = KeepForward(
			agent.GetControlledEntity(),
			mover.m_vGoal + (right * side),
			axis
		);
		IssueAgent(agent, pair, mover, speed, now);
	}

	protected void IssueAgent(
		notnull AIAgent agent,
		notnull KK_AttackPair pair,
		notnull KK_AttackMover mover,
		EMovementType speed,
		float now)
	{
		if (!agent.GetControlledEntity() || IsReleased(agent) || AtPointCover(agent))
			return;

		if (ServingTreatment(agent))
			return;

		if (KK_AgentMove.CrossingObstacle(agent))
			return;

		vector axis;
		vector right;
		BuildAxis(axis, right);

		float priority = KK_AgentMove.PRIORITY_LEVEL;
		if (IsTakeCover() && speed == EMovementType.SPRINT)
			priority = KK_AgentMove.SprintPriorityLevel();
		else if (IsTakeCover())
			priority = KK_AgentMove.AbsolutePriorityLevel();

		vector goal = mover.m_vGoal + (right * MemberSide(pair, agent));
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
		mover.m_fLastOrderAt = now;
	}

	// Crouch first. The stance change puts the rifle down, so the raise
	// goes out after it. A partner who has not arrived does not take part.
	protected void StopAdvanceAgent(notnull AIAgent agent)
	{
		if (IsReleased(agent) || AtPointCover(agent))
			return;

		CancelAgent(agent);
		KK_AgentMove.SetWantedSpeed(agent, EMovementType.IDLE);
		SetCrouch(agent, true);
		ApplyWeaponForSpeed(agent, EMovementType.IDLE);
		KK_GarrisonHold.RaiseAfterSprint(agent);
	}

	protected bool AgentReached(notnull AIAgent agent, notnull KK_AttackMover mover)
	{
		IEntity body = agent.GetControlledEntity();
		if (!body)
			return false;

		// Still on the obstacle. The flat distance already says he
		// is at the slot, and stopping here ends the climb.
		if (KK_AgentMove.CrossingObstacle(agent))
			return false;

		KK_AttackPair pair = PairOf(agent);
		if (!pair)
			return FlatDistance(body.GetOrigin(), mover.m_vGoal) <= STEP_REACH;

		return CloseToOwnSlot(agent, pair);
	}

	protected bool AgentDoor(notnull AIAgent agent, vector goal)
	{
		if (!agent.GetControlledEntity() || IsReleased(agent) || AtPointCover(agent))
			return false;

		IEntity door;
		if (m_mDoors.Contains(agent))
			door = m_mDoors.Get(agent);

		bool waiting = KK_DoorAssist.Handle(agent, goal, door);
		m_mDoors.Set(agent, door);
		if (!waiting)
			return false;

		KK_AgentMove.SetWantedSpeed(agent, EMovementType.WALK);
		ApplyWeaponForSpeed(agent, EMovementType.WALK);
		return true;
	}

	protected void ScanAgent(
		notnull AIAgent agent,
		notnull KK_AttackMover mover,
		float now)
	{
		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return;

		vector direction = axis;
		if (now - mover.m_fPauseSince >= 1000)
		{
			direction = (axis * 0.75) + (right * (0.65 * mover.m_iSide));
			if (direction.Length() > 0.01)
				direction.Normalize();
		}

		LookAgentAlong(agent, direction, SCAN_ASIDE);
	}

	protected vector AdvanceAxis()
	{
		vector axis;
		vector right;
		BuildAxis(axis, right);
		return axis;
	}

	protected float MemberSide(notnull KK_AttackPair pair, AIAgent agent)
	{
		for (int slot = 0; slot < pair.m_aAgents.Count(); slot++)
		{
			if (pair.m_aAgents[slot] != agent)
				continue;

			if (slot == 1)
				return 1.5;

			return -1.5;
		}

		return -1.5;
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

			HoldForOverwatch(pair, now);
			LookDownLane(pair);
		}

		KK_AttackPair runner = PairAt(m_iRunner);
		if (!runner)
			return;

		// Same hold a lone pair uses. Several pairs wait it out together,
		// then the next pair sprints.
		if (IsTakeCover() && runner.m_bPaused)
		{
			if (HoldEngagePair(runner, now) || now - runner.m_fPauseSince < PauseMs())
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

		// A plant is not the end of this bound, and it does not start the next.
		if (!PairEngagePlant(runner) && (!runner.m_bHasGoal || runner.m_bHolding || runner.m_bArrived))
			StartStep(runner, EMovementType.SPRINT, now);

		// One man stopped, and the handoff time, is enough. His partner
		// still on the way does not keep the waiting pair planted.
		if (CoverReady(runner, now))
		{
			ForgetPairSettled(runner);
			if (IsTakeCover())
				CoverHold(runner, now);
			else
				HoldForOverwatch(runner, now);

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

		SettleArrivedMembers(runner, now);
		RunLooseMembers(runner, EMovementType.SPRINT, now);
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
			if (HoldEngagePair(pair, now) || now - pair.m_fPauseSince < PauseMs())
			{
				CoverHold(pair, now);
				return;
			}

			pair.m_bPaused = false;
			StartStep(pair, EMovementType.SPRINT, now);
			return;
		}

		// A plant is not the end of this bound, and it does not start the next.
		if (!PairEngagePlant(pair) && (!pair.m_bHasGoal || pair.m_bHolding))
			StartStep(pair, EMovementType.SPRINT, now);

		// Same handoff as several pairs. One man at his own step does not
		// wait until his partner is standing next to him.
		if (CoverReady(pair, now))
		{
			ForgetPairSettled(pair);
			if (MembersFinished(pair))
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
		RunLooseMembers(pair, EMovementType.SPRINT, now);
	}

	// Whoever has not planted yet keeps his own sprint. A man already
	// holding his step is left on that hold, rifle up.
	protected void RunLooseMembers(
		notnull KK_AttackPair pair,
		EMovementType speed,
		float now)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity() || IsReleased(agent) || AtPointCover(agent))
				continue;

			if (ServingTreatment(agent))
				continue;

			if (m_mSettledAt.Contains(agent))
				continue;

			KK_AttackMover mover = MoverFor(agent);
			if (!mover.m_bHasGoal)
			{
				StartAgentStep(agent, pair, mover, speed, now);
				continue;
			}

			MaintainAgent(agent, pair, mover, speed, now);
		}
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
		pair.m_bHasGoal = true;
		pair.m_fStillSince = now;
		pair.m_vStillPosition = PairCenter(pair);
		pair.m_iNudges = 0;
		pair.m_fFirstStopAt = 0;
		pair.m_fRifleAt = 0;
		SetPairCrouch(pair, false);
		// The step does not shoot. A sprint lowers the rifle. The contact
		// shot from the hold has to drop here, or the aim stays on him.
		ClearHoldFight(pair);

		// Each man steps from where he is. The pair center was parking the
		// man already ahead until his partner walked up to him.
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity() || IsReleased(agent) || AtPointCover(agent))
				continue;

			StartAgentStep(agent, pair, MoverFor(agent), speed, now);
		}

		pair.m_fLastOrderAt = now;

		if (SCR_BaseGameMode.KK_LogEnabled())
		{
			PrintFormat(
				"KK: Attack pair %1 stepping",
				pair.m_iIndex
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

	// Bound overwatch. The sprint left the rifle down. Crouch first, then
	// raise, and raise again if that crouch cancelled it.
	protected void HoldForOverwatch(notnull KK_AttackPair pair, float now)
	{
		if (!pair.m_bHolding)
		{
			BeginHold(pair);
			pair.m_fRifleAt = now;
			return;
		}

		if (now - pair.m_fRifleAt < OVERWATCH_RAISE_MS)
			return;

		bool asked = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || IsReleased(agent) || KK_GarrisonHold.IsBoundSprint(agent))
				continue;

			IEntity body = agent.GetControlledEntity();
			if (!body || KK_GarrisonHold.WeaponIsRaised(body))
				continue;

			KK_GarrisonHold.RaiseAfterSprint(agent);
			asked = true;
		}

		if (asked)
			pair.m_fRifleAt = now;
	}

	protected void MaintainMove(
		notnull KK_AttackPair pair,
		EMovementType speed,
		float now)
	{
		if (HoldEngagePair(pair, now))
			return;

		if (UpdateDoors(pair))
		{
			pair.m_fStillSince = now;
			return;
		}

		vector center = PairCenter(pair);
		// A climb is slow, and a high wall holds him on top. That is not a stall.
		if (PairCrossing(pair))
		{
			pair.m_vStillPosition = center;
			pair.m_fStillSince = now;
		}
		else if (FlatDistance(center, pair.m_vStillPosition) > 0.45)
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
		if (Math.Mod(pair.m_iNudges, 2) == 0)
			side = -4;

		pair.m_vGoal = KeepForward(
			PairBody(pair),
			pair.m_vGoal + (right * side),
			axis
		);
		IssuePair(pair, speed, now);
	}

	protected vector NextGoal(notnull KK_AttackPair pair)
	{
		return PlanGoal(
			PairCenter(pair),
			pair.m_fProgressBoost,
			pair.m_iIndex,
			PairBody(pair)
		);
	}

	// from is this soldier, or the pair center when the pair still moves
	// as one. The lane stays with the pair. Progress does not.
	protected vector PlanGoal(
		vector from,
		float progressBoost,
		int laneIndex,
		IEntity body)
	{
		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
		{
			m_bLastPlanFinal = true;
			return m_vTarget;
		}

		float progress = vector.Dot(from - m_vOrigin, axis);
		progress = progress + progressBoost;
		float total = vector.Dot(m_vTarget - m_vOrigin, axis);
		float next = progress + StepLength();

		// The last step keeps the lane. Folding every pair onto the point
		// stacked the squad, and the flat finish radius then called a man
		// still on his lane short of the order.
		bool finalStep = next >= total - 1;
		vector goal = m_vOrigin + (axis * next) + LaneOffset(laneIndex, right);
		if (finalStep)
			goal = m_vTarget + LaneOffset(laneIndex, right);

		m_bLastPlanFinal = finalStep;
		goal[1] = from[1];
		goal = PreferCover(goal, axis, right);
		return KeepForward(body, goal, axis);
	}

	// A goal on the near lip is inside the wall, so the climb drops him
	// back. How far along the climb he is comes from the command state.
	// A push back through the wall is dropped, so the far side does not
	// send him home.
	protected vector KeepForward(
		IEntity body,
		vector goal,
		vector axis)
	{
		vector adjusted = KK_AgentMove.PastObstacle(
			m_Group,
			body,
			goal
		);

		float goalAlong = vector.Dot(goal - m_vOrigin, axis);
		float adjustedAlong = vector.Dot(adjusted - m_vOrigin, axis);
		if (adjustedAlong + 0.75 < goalAlong)
			return goal;

		return adjusted;
	}

	protected IEntity PairBody(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent)
				continue;

			IEntity body = agent.GetControlledEntity();
			if (body)
				return body;
		}

		return null;
	}

	protected bool PairCrossing(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (agent && KK_AgentMove.CrossingObstacle(agent))
				return true;
		}

		return false;
	}

	protected vector LaneOffset(int pairIndex, vector right)
	{
		int band = pairIndex / 2 + 1;
		float sign = 1;
		if (Math.Mod(pairIndex, 2) == 0)
			sign = -1;

		return right * (LaneSpacing() * band * sign);
	}

	// Where this man stands when the push is over. The pair lane, then his
	// own side of that pair, so two men do not share the point.
	protected vector LaneDestination(notnull KK_AttackPair pair, AIAgent agent)
	{
		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return OnNavmesh(m_vTarget);

		vector goal = m_vTarget + LaneOffset(pair.m_iIndex, right);
		if (agent)
			goal = goal + (right * MemberSide(pair, agent));

		goal[1] = m_vTarget[1];
		return OnNavmesh(goal);
	}

	// A wall within a step of this lane. The search stays on the lane.
	// Pulling a far lane back onto the point is what stacked the squad.
	protected vector PreferCover(
		vector goal,
		vector axis,
		vector right)
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
			float hit = CoverDistance(place, right);
			if (hit < 0)
				continue;

			if (found && hit >= bestHit)
				continue;

			found = true;
			bestHit = hit;
			best = place;
		}

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
			// A new sprint would drop the rifle again. A man on the point
			// is walking to cover, or he has been handed to the attack.
			if (m_mSettledAt.Contains(agent) || IsReleased(agent) || AtPointCover(agent))
				continue;

			if (ServingTreatment(agent))
				continue;

			// A new order, a stance change, or a look snap drops him off
			// the wall onto the side he climbed from.
			if (KK_AgentMove.CrossingObstacle(agent))
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
			if (!agent || m_mSettledAt.Contains(agent) || IsReleased(agent) || AtPointCover(agent))
				continue;

			if (ServingTreatment(agent))
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

		// The dash and the rifle raise belong to the push. A bandage
		// has the hands and the feet on advance, bound, and take cover.
		if (KK_GarrisonHold.HealingNow(agent))
		{
			KK_GarrisonHold.SetBoundSprint(agent, false);
			return;
		}

		if (speed == EMovementType.SPRINT)
		{
			KK_GarrisonHold.SetBoundSprint(agent, true);
			KK_GarrisonHold.SetAdvancing(agent, false);
			if (!KK_GarrisonHold.EngageOwnsTurn(agent))
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
			if (!agent || !agent.GetControlledEntity() || IsReleased(agent) || AtPointCover(agent))
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
			if (!agent || IsReleased(agent) || AtPointCover(agent))
				continue;

			CancelAgent(agent);
			KK_AgentMove.SetWantedSpeed(agent, EMovementType.IDLE);
		}

		// Crouch before the raise. A stance change after the raise puts the
		// rifle back down, and the next ask then thinks it is already up.
		SetPairCrouch(pair, true);

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || IsReleased(agent) || AtPointCover(agent))
				continue;

			ApplyWeaponForSpeed(agent, EMovementType.IDLE);
		}
	}

	protected void SetPairCrouch(notnull KK_AttackPair pair, bool crouch)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (IsReleased(agent) || AtPointCover(agent) || ServingTreatment(agent))
				continue;

			SetCrouch(agent, crouch);
		}
	}

	protected void SetCrouch(AIAgent agent, bool crouch)
	{
		if (!agent)
			return;

		// A stance change cancels a climb. He drops back on the near side.
		if (KK_AgentMove.CrossingObstacle(agent))
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
			LookAgentAlong(agent, direction, distance);
	}

	protected void LookAgentAlong(
		notnull AIAgent agent,
		vector direction,
		float distance)
	{
		if (!agent.GetControlledEntity() || IsReleased(agent))
			return;

		if (direction.Length() < 0.01)
			return;

		direction.Normalize();

		KK_GarrisonHold.SnapAimPose(agent.GetControlledEntity(), vector.Zero);
		vector eye = agent.GetControlledEntity().GetOrigin();
		eye = eye + Vector(0, 1.6, 0);
		LookAt(agent, eye + (direction * distance), LANE_LOOK);
	}

	protected void LookAtContact(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || IsReleased(agent))
				continue;

			KK_GarrisonHold.SnapAimPose(agent.GetControlledEntity(), vector.Zero);
		}
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

		// Engage already has this man. The step look would turn him off
		// the contact and the next engage pass would turn him back.
		if (KK_GarrisonHold.EngageOwnsTurn(body))
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

			if (KK_AgentMove.CrossingObstacle(agent))
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
		if (KK_GarrisonHold.PerceivedFireActive(agent) || KK_GarrisonHold.EngageOwnsTurn(agent))
			return;

		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return;

		if (!soldier.m_UtilityComponent.m_LookAction)
			return;

		soldier.m_UtilityComponent.m_LookAction.KK_Track(point, priority, 1.5);
	}

	protected void CancelLook(AIAgent agent)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return;

		if (soldier.m_UtilityComponent.m_LookAction)
			soldier.m_UtilityComponent.m_LookAction.Cancel();

		KK_GarrisonHold.SnapAimPose(agent.GetControlledEntity(), vector.Zero);
	}

	protected bool Reached(notnull KK_AttackPair pair)
	{
		float worst = 0;
		bool any = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			// Still on the obstacle. The flat distance already says he
			// is at the slot, and stopping here ends the climb.
			if (KK_AgentMove.CrossingObstacle(agent))
				return false;

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

	protected bool PairAtTarget(notnull KK_AttackPair pair)
	{
		bool any = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity())
				continue;

			any = true;
			if (!ReachedDestination(agent.GetControlledEntity().GetOrigin()))
				return false;
		}

		return any;
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

	// One soldier of the sprinting pair reaches his own step and the move
	// ends for him. Until his partner arrives the sprint would leave him
	// standing with the rifle down. He holds that step, so he can shoot
	// from there. His partner keeps his own sprint.
	protected void SettleArrivedMembers(notnull KK_AttackPair pair, float now)
	{
		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity() || IsReleased(agent) || AtPointCover(agent))
				continue;

			if (ServingTreatment(agent))
				continue;

			// Engage planted him. That stop is not an arrival on the slot.
			if (EngagePlantHolds(agent))
			{
				HoldEngageMove(agent, MoverFor(agent), now);
				continue;
			}

			// The sprint order from this frame has not started yet. A speed of
			// zero here is him standing at the start, not him waiting at the slot.
			KK_AttackMover mover = MoverFor(agent);
			if (now - mover.m_fLastOrderAt < 400)
				continue;

			// On the wall, and already inside the slot radius. Cancelling
			// here drops him back on the side he climbed from.
			if (KK_AgentMove.CrossingObstacle(agent))
				continue;

			vector origin = agent.GetControlledEntity().GetOrigin();
			vector goal = AgentStepGoal(agent, pair);
			vector slot = goal + (right * MemberSide(pair, agent));
			float reach = STEP_REACH;
			if (mover.m_bFinal)
				reach = COVER_REACH;

			bool nearSlot = FlatDistance(origin, slot) <= reach;
			// A final step is his own side of the lane. The pair center is
			// inside the old reach, and stopping there put both men together.
			bool nearGoal = !mover.m_bFinal && FlatDistance(origin, goal) <= STEP_REACH;
			bool stopped = FlatSpeed(agent) <= MEMBER_STOP;

			if ((!nearSlot && !nearGoal) || !stopped)
			{
				if (!m_mSettledAt.Contains(agent))
					continue;

				m_mSettledAt.Remove(agent);
				KK_GarrisonHold.SetReturnFire(agent, false);
				KK_GarrisonHold.SetPinned(agent.GetControlledEntity(), false);
				SetCrouch(agent, false);
				// The hold cancelled his move. The next maintain gives
				// the sprint back on this pass.
				mover.m_fLastOrderAt = 0;
				continue;
			}

			bool started = !m_mSettledAt.Contains(agent);
			if (started)
			{
				CancelAgent(agent);
				// Sprint keeps the rifle down in the engine. Walk now, and
				// send the stay on a later pass so this cancel cannot drop it.
				// Crouch before the raise, or the stance change puts it down
				// and the next ask thinks it is already up.
				KK_AgentMove.SetWantedSpeed(agent, EMovementType.WALK);
				SetCrouch(agent, true);
				ApplyWeaponForSpeed(agent, EMovementType.WALK);
				m_mSettledAt.Set(agent, now);
				pair.m_fRifleAt = now;
			}
			else if (now - m_mSettledAt.Get(agent) >= KK_AgentMove.REISSUE_INTERVAL_MS)
			{
				IssueSettledStay(agent, now);
			}
			else if (
				now - pair.m_fRifleAt >= OVERWATCH_RAISE_MS &&
				!KK_GarrisonHold.WeaponIsRaised(agent.GetControlledEntity())
			)
			{
				KK_GarrisonHold.RaiseAfterSprint(agent);
				pair.m_fRifleAt = now;
			}

			KK_GarrisonHold.SetPinned(agent.GetControlledEntity(), true);
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
		bool engageHold = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!CanFight(agent))
				continue;

			fighters++;
			// On the wall he is close to the slot and almost stopped.
			// That is not the cover handoff.
			if (KK_AgentMove.CrossingObstacle(agent))
				continue;

			// Stopped by the shot, including inside the slot radius.
			// That is not an arrival and it does not start the handoff.
			if (EngagePlantHolds(agent))
			{
				engageHold = true;
				continue;
			}

			if (CloseToOwnSlot(agent, pair))
				arrived++;

			if (FlatSpeed(agent) <= MEMBER_STOP)
				stopped = true;
		}

		// Nobody left who can cover. The pair that is waiting has to go.
		if (fighters == 0)
			return true;

		if (arrived == fighters)
			return true;

		// An engage plant is not a bound arrival. Hold the handoff clock
		// until that plant ends, then keep waiting out the rest.
		if (engageHold)
		{
			HoldEngagePairClocks(pair, now);
			return false;
		}

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
			if (EngagePlantHolds(agent))
				return false;

			if (KK_AgentMove.CrossingObstacle(agent))
				return false;

			if (!CloseToOwnSlot(agent, pair))
				return false;
		}

		return any;
	}

	// His own step. The pair goal is only a fallback for a man who has
	// not been given one yet.
	protected vector AgentStepGoal(AIAgent agent, notnull KK_AttackPair pair)
	{
		if (agent && m_mMovers.Contains(agent))
		{
			KK_AttackMover mover = m_mMovers.Get(agent);
			if (mover && mover.m_bHasGoal)
				return mover.m_vGoal;
		}

		return pair.m_vGoal;
	}

	// The point he was ordered to, including his side of the pair. On the
	// last step that side is the only place that counts, or both men stop
	// on the center.
	protected bool CloseToOwnSlot(AIAgent agent, notnull KK_AttackPair pair)
	{
		if (!agent || !agent.GetControlledEntity())
			return false;

		vector goal = AgentStepGoal(agent, pair);
		vector slot = goal;
		float reach = STEP_REACH;

		if (m_mMovers.Contains(agent))
		{
			KK_AttackMover mover = m_mMovers.Get(agent);
			if (mover && mover.m_bHasGoal)
			{
				vector axis;
				vector right;
				BuildAxis(axis, right);
				slot = mover.m_vGoal + (right * MemberSide(pair, agent));
				if (mover.m_bFinal)
					reach = COVER_REACH;
			}
		}

		return FlatDistance(agent.GetControlledEntity().GetOrigin(), slot) <= reach;
	}

	protected KK_AttackPair PairOf(AIAgent agent)
	{
		if (!agent || !m_mPairOf.Contains(agent))
			return null;

		return PairAt(m_mPairOf.Get(agent));
	}

	// Every fighter's own step is the last one, on his lane at the point.
	protected bool MembersFinished(notnull KK_AttackPair pair)
	{
		bool any = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!CanFight(agent))
				continue;

			any = true;
			KK_AttackMover mover = MoverFor(agent);
			if (!mover.m_bHasGoal || !mover.m_bFinal)
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

	// Attack engage has locked his feet for a plant. The advance step,
	// bound, and take cover clocks must wait that out.
	protected bool EngagePlantHolds(AIAgent agent)
	{
		return agent && KK_GarrisonHold.IsEngageStopped(agent);
	}

	protected bool PairEngagePlant(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (EngagePlantHolds(agent))
				return true;
		}

		return false;
	}

	// Push a "since" stamp forward so the wait does not elapse while
	// engage has his feet.
	protected void SlideAgentClock(map<AIAgent, float> clocks, AIAgent agent)
	{
		if (!clocks || !agent || m_fEvalDt <= 0 || !clocks.Contains(agent))
			return;

		clocks.Set(agent, clocks.Get(agent) + m_fEvalDt);
	}

	// Freeze the step, pause, and stuck clocks for one soldier. The plant
	// is not an arrival and not a pathing stall.
	protected void HoldEngageMove(AIAgent agent, KK_AttackMover mover, float now)
	{
		if (!mover)
			return;

		IEntity body;
		if (agent)
			body = agent.GetControlledEntity();
		if (body)
		{
			mover.m_vStillPosition = body.GetOrigin();
			mover.m_fStillSince = now;
		}

		if (mover.m_bPaused && m_fEvalDt > 0)
			mover.m_fPauseSince = mover.m_fPauseSince + m_fEvalDt;
	}

	// True when any fighter in the pair is planted by engage. Freezes the
	// pair pause, stuck, and handoff clocks for that stretch.
	protected bool HoldEngagePair(notnull KK_AttackPair pair, float now)
	{
		bool hold = false;

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!EngagePlantHolds(agent))
				continue;

			hold = true;
			HoldEngageMove(agent, MoverFor(agent), now);
		}

		if (!hold)
			return false;

		HoldEngagePairClocks(pair, now);
		return true;
	}

	protected void HoldEngagePairClocks(notnull KK_AttackPair pair, float now)
	{
		pair.m_vStillPosition = PairCenter(pair);
		pair.m_fStillSince = now;

		if (m_fEvalDt <= 0)
			return;

		if (pair.m_bPaused)
			pair.m_fPauseSince = pair.m_fPauseSince + m_fEvalDt;

		if (pair.m_fFirstStopAt > 0)
			pair.m_fFirstStopAt = pair.m_fFirstStopAt + m_fEvalDt;
	}

	// Cover rotate and the stay reissue wait on wall time. Push those
	// starts forward for the plant so he does not leave the moment it ends.
	protected void HoldEngageCover(AIAgent agent, float now)
	{
		if (!agent || m_fEvalDt <= 0)
			return;

		if (m_mCoverHold.Contains(agent))
			m_mCoverHold.Set(agent, m_mCoverHold.Get(agent) + m_fEvalDt);

		if (m_mCoverAt.Contains(agent))
			m_mCoverAt.Set(agent, m_mCoverAt.Get(agent) + m_fEvalDt);
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

	// The pair that is not sprinting. Feet stay put. Anyone he can already
	// see is shot by the contact routine, which does not wait on the attack.
	protected void CoverHold(notnull KK_AttackPair pair, float now)
	{
		// The cancel and the stay cannot share a frame. The cancel is for this
		// order, and it can drop a move that was queued after it.
		bool started = !pair.m_bHolding;
		if (started)
			BeginHold(pair);

		SetPairCrouch(pair, true);

		if (
			!started &&
			now - pair.m_fLastOrderAt >= KK_AgentMove.REISSUE_INTERVAL_MS
		)
			IssueStay(pair, now);

		// After the stay order, so a walk speed does not clear the lock.
		SetPairPinned(pair, true);
		HoldContact(pair);
	}

	// Under the normal attack. It keeps him from wandering when he has no
	// target. It does not beat the attack, so a living enemy still gets shot.
	protected void IssueStay(notnull KK_AttackPair pair, float now)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || !agent.GetControlledEntity() || IsReleased(agent) || AtPointCover(agent))
				continue;

			if (ServingTreatment(agent))
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
			if (!agent || IsReleased(agent) || AtPointCover(agent) || ServingTreatment(agent))
				continue;

			IEntity body = agent.GetControlledEntity();
			if (!body)
				continue;

			KK_GarrisonHold.SetPinned(body, pinned);
		}
	}

	// With nobody there, the head stays down the lane. A man in the fight
	// keeps the rifle on him.
	protected void HoldContact(notnull KK_AttackPair pair)
	{
		vector axis;
		vector right;
		bool haveAxis = BuildAxis(axis, right);

		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent || IsReleased(agent) || ServingTreatment(agent))
				continue;
				LookAgentDownLane(agent, axis);
		}
	}

	protected void ClearHoldFight(notnull KK_AttackPair pair)
	{
		foreach (AIAgent agent : pair.m_aAgents)
		{
			if (!agent)
				continue;

			KK_GarrisonHold.SetReturnFire(agent, false);
		}
	}

	protected float ReturnDistance()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 40;

		return mode.KK_GetTakeCoverReturn();
	}

	// He is inside the finish distance and the attack has a target.
	// The push lets him go, and the attack takes the fight from here.
	// The engage look is left in place. This handoff only gives up the move.
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

		if (m_mCoverAt.Contains(agent))
			m_mCoverAt.Remove(agent);

		if (m_mCoverHold.Contains(agent))
			m_mCoverHold.Remove(agent);

		ApplyWeaponForSpeed(agent, EMovementType.IDLE);
		SetCrouch(agent, false);
		if (!KK_GarrisonHold.PerceivedFireActive(agent))
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
			CoverSlot(pair, agent),
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
	// still short of it keep bounding. Once the attack has a target he is
	// handed over. Until then he walks to cover beside the point. Attack
	// engage keeps running in both of those, until the order ends.
	protected void UpdateArrivals(float now)
	{
		bool fight = CoverFightHolds(now);

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!agent || !agent.GetControlledEntity() || ServingTreatment(agent))
					continue;

				if (ShiftIfStuck(agent, pair, now))
					continue;

				float distance = FlatDistance(
					agent.GetControlledEntity().GetOrigin(),
					m_vTarget
				);

				if (IsReleased(agent))
				{
					if (distance > ReturnDistance())
					{
						if (EngagePlantHolds(agent))
						{
							SlideAgentClock(m_mReturnAt, agent);
							continue;
						}

						WalkSoldierBack(agent, pair, now);
						continue;
					}

					if (m_mReturnAt.Contains(agent))
					{
						if (
							distance > Arrival() &&
							!ReachedDestination(agent.GetControlledEntity().GetOrigin())
						)
							continue;

						m_mReturnAt.Remove(agent);
						if (fight)
						{
							YieldSoldier(agent);
							continue;
						}

						m_Released.RemoveItem(agent);
					}
					else if (fight)
					{
						continue;
					}
					else if (distance <= CoverRadius() || m_mCoverSlot.Contains(agent))
					{
						m_Released.RemoveItem(agent);
					}
					else
					{
						continue;
					}
				}

				if (IsReleased(agent))
					continue;

				// Inside the finish distance he starts. A man already on his
				// lane is there too, even when that lane sits outside the
				// flat radius. Once he has a spot he keeps running to it.
				if (
					!ReachedDestination(agent.GetControlledEntity().GetOrigin()) &&
					!m_mCoverSlot.Contains(agent)
				)
					continue;

				// A fight at the point belongs to the attack, including men
				// whose own cover is blocking the shot. The plant would
				// freeze them there.
				if (fight)
				{
					YieldSoldier(agent);
					m_Released.Insert(agent);
					continue;
				}

				SeekPointCover(agent, pair, now);
			}
		}
	}

	// True while someone at the order can fight, and for a short time
	// after that stops. Cover rotation waits that out.
	protected bool CoverFightHolds(float now)
	{
		if (SquadInFight())
		{
			m_bCoverFought = true;
			m_fFightClearSince = 0;
			return true;
		}

		if (!m_bCoverFought)
			return false;

		if (m_fFightClearSince <= 0)
			m_fFightClearSince = now;

		if (now - m_fFightClearSince < FIGHT_CLEAR_MS)
			return true;

		m_bCoverFought = false;
		m_fFightClearSince = 0;
		return false;
	}

	protected bool SquadInFight()
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!agent || !agent.GetControlledEntity())
					continue;

				if (HasCombatTarget(agent))
					return true;

				if (KK_GarrisonHold.PerceivedFireActive(agent))
					return true;
			}
		}

		return false;
	}

	// A living target, including one the light check has not accepted yet.
	// That check fails while he is planted against cover, and waiting on
	// it is what left him stuck there.
	protected bool HasCombatTarget(AIAgent agent)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return false;

		if (!soldier.m_UtilityComponent.m_CombatComponent)
			return false;

		BaseTarget target =
			soldier.m_UtilityComponent.m_CombatComponent.GetCurrentTarget();
		return KK_GarrisonHold.IsLivingTarget(target);
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
		if (!IsTakeCover() && !m_bAtPoint)
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

	// Inside the finish distance, or already on his way to a cover past it.
	// Not handed to the attack. The bound leaves him alone.
	protected bool AtPointCover(AIAgent agent)
	{
		if (!IsTakeCover() || IsReleased(agent) || !agent || !agent.GetControlledEntity())
			return false;

		if (m_mCoverSlot.Contains(agent))
			return true;

		return ReachedDestination(agent.GetControlledEntity().GetOrigin());
	}

	// The attack has not taken him. Run to a covered spot around the point,
	// then plant. The run outranks the bound sprint, so he does not finish
	// that step in the open. A target hands him to the attack instead.
	protected void SeekPointCover(
		AIAgent agent,
		notnull KK_AttackPair pair,
		float now)
	{
		if (!agent)
			return;

		IEntity body = agent.GetControlledEntity();
		if (!body)
			return;

		// Engage owns the plant. Leave cover walk, stay, and rotate where
		// they were until that shot ends.
		if (EngagePlantHolds(agent))
		{
			HoldEngageCover(agent, now);
			return;
		}

		if (m_mSettledAt.Contains(agent))
			m_mSettledAt.Remove(agent);

		KK_GarrisonHold.SetReturnFire(agent, false);
		KK_GarrisonHold.SetRecalled(agent, false);

		vector axis;
		vector right;
		BuildAxis(axis, right);

		vector slot = CoverSlot(pair, agent);
		bool planted = FlatDistance(body.GetOrigin(), slot) <= COVER_REACH;
		if (planted && CoverRotateDue(agent, now))
		{
			AdvanceCover(agent);
			slot = CoverSlot(pair, agent);
			planted = false;
			m_mCoverAt.Remove(agent);
		}

		bool due = !m_mCoverAt.Contains(agent) ||
			now - m_mCoverAt.Get(agent) >= KK_AgentMove.REISSUE_INTERVAL_MS;

		if (!planted)
		{
			if (m_mCoverHold.Contains(agent))
				m_mCoverHold.Remove(agent);

			KK_GarrisonHold.SetPinned(body, false);
			SetCrouch(agent, false);

			if (due)
			{
				KK_AgentMove.Issue(
					this,
					m_Group,
					agent,
					slot,
					m_mSoloHandlers,
					KK_AgentMove.SprintPriorityLevel(),
					EMovementType.RUN
				);
				ApplyWeaponForSpeed(agent, EMovementType.RUN);
				m_mCoverAt.Set(agent, now);
			}
			else
			{
				KK_AgentMove.SetWantedSpeed(agent, EMovementType.RUN);
			}

			return;
		}

		// The walk outranks the attack. Cancel it once, then hold with a
		// stay the attack can beat. A pin here freezes the fight in place.
		if (!m_mCoverHold.Contains(agent))
		{
			CancelAgent(agent);
			KK_GarrisonHold.SetCoverShift(agent, false);
			KK_GarrisonHold.SetPinned(body, false);
			SetCrouch(agent, true);
			ApplyWeaponForSpeed(agent, EMovementType.WALK);
			m_mCoverHold.Set(agent, now);
			m_mCoverAt.Remove(agent);
			return;
		}

		SetCrouch(agent, true);
		ApplyWeaponForSpeed(agent, EMovementType.WALK);
		KK_GarrisonHold.SetPinned(body, false);

		if (due)
		{
			KK_AgentMove.Issue(
				this,
				m_Group,
				agent,
				body.GetOrigin(),
				m_mSoloHandlers,
				KK_AgentMove.PRIORITY_LEVEL,
				EMovementType.WALK
			);
			m_mCoverAt.Set(agent, now);
		}

		LookAgentDownLane(agent, axis);
	}

	// Each man who reaches the point takes the next cover around it.
	// He keeps that spot until the rotate time, including a walk back.
	protected vector CoverSlot(notnull KK_AttackPair pair, AIAgent agent)
	{
		EnsurePointCovers();

		if (!m_aPointCovers || m_aPointCovers.Count() == 0)
			return OnNavmesh(m_vTarget);

		if (!agent || !m_mCoverSlot.Contains(agent))
		{
			m_mCoverSlot.Set(agent, m_iCoverAssign);
			m_iCoverAssign++;
		}

		int index = 0;
		if (agent)
			index = m_mCoverSlot.Get(agent);

		int count = m_aPointCovers.Count();
		vector spot = m_aPointCovers[Math.Mod(index, count)];
		int lap = index / count;
		vector spread = SpreadCover(spot, lap);
		// The navmesh pulled the next man back onto someone. His lane is
		// still empty, so he stands there instead of on that man.
		if (lap > 0 && FlatDistance(spread, spot) < COVER_SPACING * 0.75)
			return LaneDestination(pair, agent);

		return spread;
	}

	// At the point, and the shot from here has failed long enough. He takes
	// the next cover. One cover has nowhere to go, so he stays and keeps
	// trying the return shot.
	protected bool ShiftIfStuck(
		AIAgent agent,
		notnull KK_AttackPair pair,
		float now)
	{
		if (!agent || !agent.GetControlledEntity())
			return false;

		// A cover shift already released the plant so he can walk. A new
		// engage plant waits; do not start another shift from that stop.
		if (EngagePlantHolds(agent) && !KK_GarrisonHold.IsCoverShift(agent))
			return false;

		float distance = FlatDistance(agent.GetControlledEntity().GetOrigin(), m_vTarget);
		bool atPoint = ReachedDestination(agent.GetControlledEntity().GetOrigin()) ||
			(m_mCoverSlot.Contains(agent) && distance <= CoverRadius());
		if (!atPoint)
			return false;

		if (KK_GarrisonHold.IsCoverShift(agent))
		{
			vector slot = CoverSlot(pair, agent);
			bool planted = FlatDistance(agent.GetControlledEntity().GetOrigin(), slot) <= COVER_REACH;
			if (planted)
			{
				KK_GarrisonHold.SetCoverShift(agent, false);
				return false;
			}

			m_Released.RemoveItem(agent);
			KK_GarrisonHold.ReleaseEngageFeet(agent);
			SeekPointCover(agent, pair, now);
			return true;
		}

		if (KK_GarrisonHold.CannotShoot(agent))
			return false;

		if (!KK_GarrisonHold.EngageLaneBlocked(agent, COVER_LANE_STUCK_MS))
			return false;

		EnsurePointCovers();
		if (!m_aPointCovers || m_aPointCovers.Count() < 2)
			return false;

		if (!m_mCoverSlot.Contains(agent))
			CoverSlot(pair, agent);
		else
			AdvanceCover(agent);

		m_Released.RemoveItem(agent);
		m_mCoverHold.Remove(agent);
		m_mCoverAt.Remove(agent);
		KK_GarrisonHold.SetCoverShift(agent, true);
		KK_GarrisonHold.ClearEngageLaneBlock(agent);
		KK_GarrisonHold.ReleaseEngageFeet(agent);
		SeekPointCover(agent, pair, now);
		return true;
	}

	// Planted long enough, and not in a shot. One cover has nowhere to go.
	protected bool CoverRotateDue(AIAgent agent, float now)
	{
		if (!agent || !m_aPointCovers || m_aPointCovers.Count() < 2)
			return false;

		if (CoverRotateMs() <= 0)
			return false;

		if (!m_mCoverHold.Contains(agent))
			return false;

		if (HasLivingTarget(agent) || KK_GarrisonHold.PerceivedFireActive(agent))
			return false;

		int index = 0;
		if (m_mCoverSlot.Contains(agent))
			index = m_mCoverSlot.Get(agent);

		int count = m_aPointCovers.Count();
		// Later men leave a little after the first, so the squad does not
		// step off together. The setting is the first man's wait.
		float wait = CoverRotateMs() * (1 + (Math.Mod(index, count) * 0.25));
		return now - m_mCoverHold.Get(agent) >= wait;
	}

	protected float CoverRotateMs()
	{
		if (!m_AttackWaypoint)
			return 12000;

		return m_AttackWaypoint.GetCoverRotate() * 1000;
	}

	protected float CoverRadius()
	{
		if (!m_AttackWaypoint)
			return 25;

		return m_AttackWaypoint.GetCoverRadius();
	}

	protected void AdvanceCover(AIAgent agent)
	{
		if (!agent)
			return;

		int index = 0;
		if (m_mCoverSlot.Contains(agent))
			index = m_mCoverSlot.Get(agent);

		m_mCoverSlot.Set(agent, index + 1);
		m_mCoverHold.Remove(agent);
	}

	// Spots in order around the point, far enough apart that the squad
	// does not stack. One direction keeps the sample closest to something
	// solid. A second sample of that same cover is dropped, then the
	// wall is walked so one corner can hold a line of men.
	protected void EnsurePointCovers()
	{
		if (m_bPointCovers)
			return;

		m_bPointCovers = true;
		m_aPointCovers.Clear();

		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
		{
			m_aPointCovers.Insert(OnNavmesh(m_vTarget));
			return;
		}

		float radius = CoverRadius();
		float limit = radius;

		for (int step = 0; step < 8; step++)
		{
			float angle = (Math.PI2 * step) / 8;
			vector dir = (right * Math.Sin(angle)) + (axis * Math.Cos(angle));
			vector place;
			float hit;
			// The outer sample first, so the ring uses the full radius.
			bool found = ReadCover(m_vTarget + (dir * radius), limit, place, hit);
			if (!found)
				found = ReadCover(m_vTarget + (dir * (radius * 0.5)), limit, place, hit);

			if (!found || NearPointCover(place))
				continue;

			m_aPointCovers.Insert(place);
		}

		ExtendPointCovers();
		FillOpenCovers(axis, right);

		if (m_aPointCovers.Count() == 0)
			m_aPointCovers.Insert(OnNavmesh(m_vTarget));
	}

	// One wall can hold more than one man. Step along it at the same
	// spacing, and stop when the next step leaves the cover or the radius.
	protected void ExtendPointCovers()
	{
		array<vector> seeds = {};
		foreach (vector kept : m_aPointCovers)
			seeds.Insert(kept);

		float limit = CoverRadius();

		foreach (vector seed : seeds)
		{
			vector toward;
			float hit;
			if (!CoverDirection(seed, toward, hit))
				continue;

			vector along = Vector(-toward[2], 0, toward[0]);

			for (int side = -1; side <= 1; side += 2)
			{
				for (int step = 1; step <= 4; step++)
				{
					vector shifted = seed + (along * (COVER_SPACING * step * side));
					shifted[1] = seed[1];
					if (FlatDistance(shifted, m_vTarget) > limit)
						break;

					vector place = OnNavmesh(shifted);
					if (FlatDistance(place, m_vTarget) > limit)
						break;

					if (NearPointCover(place))
						break;

					if (CoverDistanceAny(place) < 0)
						break;

					m_aPointCovers.Insert(place);
				}
			}
		}
	}

	// Cover ran out before the squad did. The rest stand on the ring,
	// still at the same spacing, instead of on a man already in cover.
	protected void FillOpenCovers(vector axis, vector right)
	{
		int wanted = FightingCount();
		if (m_aPointCovers.Count() >= wanted)
			return;

		float radius = CoverRadius();

		for (int step = 0; step < 8; step++)
		{
			if (m_aPointCovers.Count() >= wanted)
				return;

			float angle = (Math.PI2 * step) / 8;
			vector dir = (right * Math.Sin(angle)) + (axis * Math.Cos(angle));
			vector place = OnNavmesh(m_vTarget + (dir * radius));
			if (FlatDistance(place, m_vTarget) > radius)
				continue;

			if (NearPointCover(place))
				continue;

			m_aPointCovers.Insert(place);
		}
	}

	protected int FightingCount()
	{
		int count = 0;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (agent && agent.GetControlledEntity())
					count++;
			}
		}

		if (count < 1)
			return 1;

		return count;
	}

	protected bool ReadCover(
		vector candidate,
		float limit,
		out vector place,
		out float hit)
	{
		place = OnNavmesh(candidate);
		hit = -1;

		if (FlatDistance(place, m_vTarget) > limit)
			return false;

		hit = CoverDistanceAny(place);
		return hit >= 0;
	}

	// A wall on any side of the sample. The lane check missed covers
	// that do not sit left or right of the push.
	protected float CoverDistanceAny(vector feet)
	{
		vector toward;
		float hit;
		if (!CoverDirection(feet, toward, hit))
			return -1;

		return hit;
	}

	// Nearest solid thing between an arm's length and a short step.
	// toward points at that thing.
	protected bool CoverDirection(vector feet, out vector toward, out float hit)
	{
		hit = -1;
		toward = vector.Zero;

		for (int step = 0; step < 8; step++)
		{
			float angle = (Math.PI2 * step) / 8;
			vector dir = Vector(Math.Sin(angle), 0, Math.Cos(angle));
			float distance = RayCover(feet, dir);
			if (distance <= 0.6 || distance >= 3.5)
				continue;

			if (hit >= 0 && distance >= hit)
				continue;

			hit = distance;
			toward = dir;
		}

		return hit >= 0;
	}

	protected bool NearPointCover(vector place)
	{
		foreach (vector kept : m_aPointCovers)
		{
			if (FlatDistance(kept, place) < COVER_SPACING)
				return true;
		}

		return false;
	}

	// More men than covers. The next man stands along that same cover
	// instead of on the first man. A step the navmesh pulls back onto
	// the first man is tried the other way, then out on the ring.
	protected vector SpreadCover(vector spot, int lap)
	{
		if (lap <= 0)
			return spot;

		vector flat = spot - m_vTarget;
		flat[1] = 0;
		if (flat.Length() < 0.2)
			flat = "0 0 1";
		else
			flat.Normalize();

		vector side = Vector(-flat[2], 0, flat[0]);
		float limit = CoverRadius();
		float step = COVER_SPACING * lap;

		vector placed = PlaceSpread(spot, side, step, limit);
		if (FlatDistance(placed, spot) >= COVER_SPACING * 0.75)
			return placed;

		placed = PlaceSpread(spot, side * -1, step, limit);
		if (FlatDistance(placed, spot) >= COVER_SPACING * 0.75)
			return placed;

		float angle = (Math.PI2 * lap) / 8;
		vector dir = (side * Math.Sin(angle)) + (flat * Math.Cos(angle));
		float reach = step + COVER_SPACING;
		if (reach > limit)
			reach = limit;

		placed = OnNavmesh(m_vTarget + (dir * reach));
		placed[1] = spot[1];
		if (
			FlatDistance(placed, m_vTarget) <= limit &&
			FlatDistance(placed, spot) >= COVER_SPACING * 0.75
		)
			return placed;

		return spot;
	}

	protected vector PlaceSpread(vector spot, vector side, float step, float limit)
	{
		vector shifted = spot + (side * step);
		shifted[1] = spot[1];

		if (FlatDistance(shifted, m_vTarget) > limit)
		{
			vector fromPoint = shifted - m_vTarget;
			fromPoint[1] = 0;
			if (fromPoint.Length() < 0.2)
				return spot;

			fromPoint.Normalize();
			shifted = m_vTarget + (fromPoint * limit);
			shifted[1] = spot[1];
		}

		vector placed = OnNavmesh(shifted);
		if (FlatDistance(placed, m_vTarget) > limit)
			return spot;

		return placed;
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
			int index = Math.Mod(from + step, count);
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

	protected bool HasLivingTarget(AIAgent agent)
	{
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent)
			return false;

		if (!soldier.m_UtilityComponent.m_CombatComponent)
			return false;

		// Attack engage owns the shot. A combat-component target the light
		// check rejected is not a reason to hold the lane look.
		BaseTarget target =
			soldier.m_UtilityComponent.m_CombatComponent.GetCurrentTarget();
		if (!KK_GarrisonHold.IsLivingTarget(target))
			return false;

		// The ray reached him and the light check still rejected him.
		// He is not a contact, so the push does not stop on him.
		if (KK_GarrisonHold.BlockedByConditions(agent.GetControlledEntity(), target))
			return false;

		return true;
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
			KK_GarrisonHold.PressPerceivedEnemy(agent);

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
			if (m_mMovers.Contains(agent))
				m_mMovers.Remove(agent);
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

		m_bPaceSet = true;
		m_eApplied = pace;
		m_iRunner = 0;
		m_bAtPoint = false;
		m_mMovers.Clear();
		m_Released.Clear();
		m_mReturnAt.Clear();
		m_mCoverAt.Clear();
		m_aPointCovers.Clear();
		m_bPointCovers = false;
		m_mCoverSlot.Clear();
		m_mCoverHold.Clear();
		m_iCoverAssign = 0;
		m_bCoverFought = false;
		m_fFightClearSince = 0;

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
			pair.m_fRifleAt = 0;
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

	// On the destination line, including a man out on his lane. The flat
	// radius treated that lane as short and the recall walked him onto
	// the point with everyone else. Well past the line is not a hold.
	protected bool ReachedDestination(vector origin)
	{
		if (FlatDistance(origin, m_vTarget) <= Arrival())
			return true;

		vector axis;
		vector right;
		if (!BuildAxis(axis, right))
			return false;

		float remaining = vector.Dot(m_vTarget - origin, axis);
		if (remaining > Arrival())
			return false;

		if (remaining < -Arrival())
			return false;

		return true;
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
		EndTreatment();

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

		// Raising or lowering again restarts the pose and cuts off a climb.
		if (KK_AgentMove.CrossingObstacle(agent))
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

	// The push leaves these men. A live contact puts them back on it.
	protected bool ServingTreatment(AIAgent agent)
	{
		return agent && m_Treatment.Contains(agent);
	}

	protected bool SquadContactOpen()
	{
		if (SquadInFight())
			return true;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (agent && KK_GarrisonHold.EngageContactOpen(agent))
					return true;
			}
		}

		return false;
	}

	protected bool NeedsTreatment(AIAgent agent)
	{
		if (!agent || !agent.GetControlledEntity())
			return false;

		if (!CanFight(agent) && !KK_GarrisonHold.IsDown(agent))
			return false;

		if (KK_GarrisonHold.IsDown(agent))
			return true;

		return KK_GarrisonHold.NeedsMedical(agent);
	}

	// Fighters still on the push, all of them inside the finish distance.
	// A medic out with a patient does not count.
	protected bool FightersInside()
	{
		bool any = false;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!CanFight(agent) || ServingTreatment(agent))
					continue;

				if (!agent.GetControlledEntity())
					continue;

				if (!ReachedDestination(agent.GetControlledEntity().GetOrigin()))
					return false;

				any = true;
			}
		}

		return any;
	}

	// Every man who can walk is at the point, including a medic who has finished.
	protected bool WalkersInside()
	{
		bool any = false;

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!CanFight(agent))
					continue;

				if (!agent.GetControlledEntity())
					continue;

				if (!ReachedDestination(agent.GetControlledEntity().GetOrigin()))
					return false;

				any = true;
			}
		}

		return any;
	}

	// Someone can still be helped. A downed man with nobody able to treat him does not hold the next order.
	protected bool TreatmentBlocksHandoff()
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!NeedsTreatment(agent))
					continue;

				if (KK_GarrisonHold.IsDown(agent))
				{
					if (SomeoneCanTreat(agent))
						return true;

					continue;
				}

				if (KK_GarrisonHold.CanTreatPatient(agent, agent) || SomeoneCanTreat(agent))
					return true;
			}
		}

		return false;
	}

	protected bool SomeoneCanTreat(AIAgent patient)
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!agent || agent == patient || !CanFight(agent))
					continue;

				if (KK_GarrisonHold.CanTreatPatient(agent, patient))
					return true;
			}
		}

		return false;
	}

	// An item is already in someone's hands. The push waits for that gesture.
	protected bool GestureInProgress()
	{
		if (m_Medic && KK_GarrisonHold.HealingNow(m_Medic))
			return true;

		foreach (AIAgent agent : m_Treatment)
		{
			if (agent && KK_GarrisonHold.HealingNow(agent))
				return true;
		}

		return false;
	}

	// keepGesture leaves a medic, and the man he is treating, on an item
	// that is already in his hands. Everyone else goes back to the push.
	protected void EndTreatment(bool keepGesture = false)
	{
		bool medicBusy = keepGesture && GestureInProgress();
		array<AIAgent> release = {};
		foreach (AIAgent agent : m_Treatment)
		{
			if (!agent)
				continue;

			if (medicBusy && (agent == m_Medic || agent == m_MedicPatient))
				continue;

			if (keepGesture && KK_GarrisonHold.HealingNow(agent))
				continue;

			release.Insert(agent);
		}

		foreach (AIAgent agent : release)
		{
			KK_GarrisonHold.SetMedicDetail(agent, false);
			RestorePace(agent);
			m_Treatment.RemoveItem(agent);
		}

		if (m_Treatment.Count() > 0)
			return;

		m_Medic = null;
		m_MedicPatient = null;
		m_bMedicHeld = false;
	}

	protected void UpdateTreatment(float now)
	{
		array<AIAgent> patients = {};
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (NeedsTreatment(agent))
					patients.Insert(agent);
			}
		}

		if (patients.Count() == 0)
		{
			EndTreatment(true);
			return;
		}

		bool medicBusy = GestureInProgress();
		AIAgent busyMedic = m_Medic;
		AIAgent busyPatient = m_MedicPatient;

		AIAgent patient = PickPatient(patients);
		AIAgent medic = null;
		bool standingNeedsHelp = patient && !KK_GarrisonHold.IsDown(patient) && !KK_GarrisonHold.CanTreatPatient(patient, patient);
		if (patient && (KK_GarrisonHold.IsDown(patient) || standingNeedsHelp))
			medic = PickMedic(patient);

		ref set<AIAgent> next = new set<AIAgent>();
		foreach (AIAgent agent : patients)
		{
			if (KK_GarrisonHold.IsDown(agent))
			{
				next.Insert(agent);
				continue;
			}

			if (KK_GarrisonHold.CanTreatPatient(agent, agent) || (agent == patient && medic))
				next.Insert(agent);
		}

		if (medic)
			next.Insert(medic);

		if (medicBusy)
		{
			if (busyMedic)
				next.Insert(busyMedic);

			if (busyPatient)
				next.Insert(busyPatient);
		}

		foreach (AIAgent agent : m_Treatment)
		{
			if (agent && !next.Contains(agent))
			{
				KK_GarrisonHold.SetMedicDetail(agent, false);
				RestorePace(agent);
			}
		}

		foreach (AIAgent agent : next)
		{
			if (!m_Treatment.Contains(agent))
				PullOffPush(agent);
		}

		m_Treatment.Clear();
		foreach (AIAgent agent : next)
		{
			m_Treatment.Insert(agent);
			KK_GarrisonHold.SetMedicDetail(agent, true);
		}

		if (medicBusy)
		{
			m_Medic = busyMedic;
			m_MedicPatient = busyPatient;
			return;
		}

		m_Medic = medic;
		m_MedicPatient = patient;

		foreach (AIAgent agent : patients)
		{
			if (next.Contains(agent))
				HoldPatient(agent);
		}

		if (!medic || !patient)
			return;

		IEntity medicBody = medic.GetControlledEntity();
		IEntity patientBody = patient.GetControlledEntity();
		if (!medicBody || !patientBody)
			return;

		float apart = FlatDistance(medicBody.GetOrigin(), patientBody.GetOrigin());
		if (apart > 2.5)
		{
			m_bMedicHeld = false;
			CharacterControllerComponent walking = CharacterControllerComponent.Cast(
				medicBody.FindComponent(CharacterControllerComponent)
			);
			if (walking)
				walking.OverrideMaxSpeed(1);

			if (now - m_fMedicOrderAt >= KK_AgentMove.REISSUE_INTERVAL_MS)
			{
				KK_GarrisonHold.SetBoundSprint(medic, false);
				KK_AgentMove.Issue(
					this,
					m_Group,
					medic,
					patientBody.GetOrigin(),
					m_mSoloHandlers,
					KK_AgentMove.PRIORITY_LEVEL,
					EMovementType.WALK
				);
				m_fMedicOrderAt = now;
			}

			return;
		}

		if (!m_bMedicHeld)
		{
			CancelAgent(medic);
			m_bMedicHeld = true;
		}

		CharacterControllerComponent stopped = CharacterControllerComponent.Cast(
			medicBody.FindComponent(CharacterControllerComponent)
		);
		if (stopped)
			stopped.OverrideMaxSpeed(0);

		KK_GarrisonHold.TryMedicTreat(medic, patient);
	}

	protected AIAgent PickPatient(array<AIAgent> patients)
	{
		AIAgent bestDown;
		float bestDownDist = 0;
		AIAgent bestHurt;
		float bestHurtDist = 0;
		bool haveDown;
		bool haveHurt;

		foreach (AIAgent patient : patients)
		{
			if (!patient || !patient.GetControlledEntity())
				continue;

			float along = FlatDistance(patient.GetControlledEntity().GetOrigin(), m_vTarget);
			if (KK_GarrisonHold.IsDown(patient))
			{
				if (!haveDown || along > bestDownDist)
				{
					bestDown = patient;
					bestDownDist = along;
					haveDown = true;
				}

				continue;
			}

			if (!haveHurt || along > bestHurtDist)
			{
				bestHurt = patient;
				bestHurtDist = along;
				haveHurt = true;
			}
		}

		if (haveDown)
			return bestDown;

		return bestHurt;
	}

	protected AIAgent PickMedic(AIAgent patient)
	{
		if (!patient || !patient.GetControlledEntity())
			return null;

		AIAgent best;
		float bestDist = 0;
		vector origin = patient.GetControlledEntity().GetOrigin();

		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!CanFight(agent) || agent == patient || !agent.GetControlledEntity())
					continue;

				if (!KK_GarrisonHold.CanTreatPatient(agent, patient))
					continue;

				float distance = FlatDistance(agent.GetControlledEntity().GetOrigin(), origin);
				if (best && distance >= bestDist)
					continue;

				best = agent;
				bestDist = distance;
			}
		}

		return best;
	}

	protected void RestorePace(AIAgent agent)
	{
		if (!agent || KK_GarrisonHold.HealingNow(agent))
			return;

		IEntity body = agent.GetControlledEntity();
		if (!body)
			return;

		CharacterControllerComponent controller = CharacterControllerComponent.Cast(
			body.FindComponent(CharacterControllerComponent)
		);
		if (controller)
			controller.OverrideMaxSpeed(1);
	}

	protected void HoldPatient(AIAgent patient)
	{
		if (!patient)
			return;

		KK_GarrisonHold.SetBoundSprint(patient, false);
		if (!KK_GarrisonHold.IsDown(patient))
			KK_GarrisonHold.HoldForHeal(patient);
	}

	protected void PullOffPush(AIAgent agent)
	{
		if (!agent)
			return;

		CancelAgent(agent);
		KK_GarrisonHold.SetBoundSprint(agent, false);
		IEntity body = agent.GetControlledEntity();
		if (body)
			KK_GarrisonHold.SetPinned(body, false);
	}

	// Men who finished treatment, or never left, and are still short of a hold.
	protected void RecallStragglers(float now)
	{
		foreach (KK_AttackPair pair : m_aPairs)
		{
			if (!pair)
				continue;

			foreach (AIAgent agent : pair.m_aAgents)
			{
				if (!CanFight(agent) || ServingTreatment(agent) || IsReleased(agent))
					continue;

				if (!agent.GetControlledEntity())
					continue;

				if (ReachedDestination(agent.GetControlledEntity().GetOrigin()))
					continue;

				if (EngagePlantHolds(agent))
				{
					SlideAgentClock(m_mReturnAt, agent);
					continue;
				}

				if (
					m_mReturnAt.Contains(agent) &&
					now - m_mReturnAt.Get(agent) < KK_AgentMove.REISSUE_INTERVAL_MS
				)
					continue;

				KK_GarrisonHold.SetBoundSprint(agent, false);
				RestorePace(agent);
				KK_AgentMove.Issue(
					this,
					m_Group,
					agent,
					LaneDestination(pair, agent),
					m_mSoloHandlers,
					KK_AgentMove.PRIORITY_LEVEL,
					EMovementType.RUN
				);
				m_mReturnAt.Set(agent, now);
			}
		}
	}

	// Another order sits behind this one. The hold at the point is for
	// the last attack in the queue. A later waypoint has to be reached.
	protected bool HasFollowingWaypoint()
	{
		if (!m_Group || !m_AttackWaypoint)
			return false;

		array<AIWaypoint> waypoints = {};
		m_Group.GetWaypoints(waypoints);

		int index = waypoints.Find(m_AttackWaypoint);
		if (index < 0)
			return false;

		for (int i = index + 1; i < waypoints.Count(); i++)
		{
			if (waypoints[i])
				return true;
		}

		return false;
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

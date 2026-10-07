class KK_PassageSoldier
{
	AIAgent m_Agent;
	vector m_vGoal;
	bool m_bSettled;

	void KK_PassageSoldier(
		notnull AIAgent agent,
		vector goal,
		bool settled)
	{
		m_Agent = agent;
		m_vGoal = goal;
		m_bSettled = settled;
	}
}

class KK_PassageOrder
{
	vector m_vMoveTo;
	bool m_bOverride;
	bool m_bHoldTimers;
	bool m_bIssueNow;
	bool m_bWalk;
	bool m_bDoor;
}

class KK_AsideChoice
{
	bool m_bFound;
	vector m_vSpot;
	float m_fDist;
}

class KK_OpeningLeaf
{
	IEntity m_Entity;
	BaseDoorComponent m_Door;
	vector m_vHinge;
	vector m_vClosedDir;
	vector m_vSwing;
	vector m_vLatchLocal;
	vector m_vClosedFar;
	float m_fRadius;
	bool m_bSwingKnown;
	bool m_bSwingsOut;
}

class KK_Opening
{
	ref array<ref KK_OpeningLeaf> m_aLeaves = {};
	vector m_vHinge;
	vector m_vLatch;
	vector m_vCenter;
	vector m_vWidth;
	vector m_vApproach;
	float m_fWidth;
	float m_fLaneLength;
	float m_fLastState;
	float m_fLastStateTime;
	bool m_bHaveState;
	bool m_bStalled;
	bool m_bOpenCalled;
	float m_fLastOpenCall;
	bool m_bLoggedHold;
}

class KK_PassageAgentState
{
	vector m_vIssued;
	bool m_bHaveIssued;
	bool m_bWasOverride;
	bool m_bWasWalk;
	vector m_vPeelOrigin;
	bool m_bPeelOriginSet;
	vector m_vHoldPoint;
	bool m_bHaveHold;
	vector m_vYieldStart;
	bool m_bYieldStartSet;
	float m_fYieldStartedAt;
	float m_fYieldResumeAt;
	vector m_vStillOrigin;
	float m_fStillSince;
	bool m_bHaveStill;
	vector m_vYieldTarget;
	bool m_bHaveYieldTarget;
	bool m_bMakeWay;
	bool m_bMakeWayFresh;
	bool m_bMakeWayRepathed;
	float m_fMakeWayStartedAt;
	vector m_vMakeWayOrigin;
	vector m_vMakeWayTarget;
	AIAgent m_MakeWayFor;
	bool m_bPassing;
	vector m_vPassOrigin;
	vector m_vStoppedOrigin;
	float m_fStoppedSince;
	bool m_bHaveStopped;
}

class KK_Passage
{
	protected static const float PASS_WIDTH = 0.5;
	protected static const float SLOT_FIRST = 1.0;
	protected static const float SLOT_GAP = 1.2;
	protected static const float BODY_LENGTH = 0.8;
	protected static const float LANE_STEP = 0.5;
	protected static const float AT_SLOT = 0.6;
	// A yield is a short step out of the way, then both soldiers go back to
	// their goals. Waiting for a wide gap, or backing up the whole corridor,
	// leaves the squad standing.
	protected static const float YIELD_RANGE = 1.6;
	protected static const float HOLD_RANGE = 0.9;
	protected static const float FOLLOW_HOLD = 0.75;
	protected static const float YIELD_DONE = 0.5;
	protected static const float YIELD_MAX_MS = 1000;
	protected static const float YIELD_RESUME_MS = 1500;
	protected static const float YIELD_STALE_MS = 500;
	// A new yield waits until the soldiers involved have been stopped this long.
	protected static const float YIELD_STILL_MS = 500;
	protected static const float YIELD_STILL_DISTANCE = 0.3;
	// Pass-through drops character collision after the soldier has been
	// stopped. There is no per-person collision, so who is in front does
	// not matter. A true stop is quicker than shuffling inside 0.3 m.
	protected static const float PASS_STOPPED_MS = 250;
	protected static const float PASS_STOPPED_DISTANCE = 0.1;
	protected static const float PASS_STILL_MS = 500;
	protected static const float PASS_CLEAR_RANGE = 1.6;
	protected static const int YIELD_SEARCH_STEPS = 3;
	protected static const float YIELD_SEARCH_STEP = 0.6;
	protected static const float YIELD_FALLBACK = 1.0;
	// A teammate standing on the path is told to step off it. This covers a
	// posted soldier or a body in a wide room, which corridor yield ignores.
	protected static const float MAKE_WAY_REACH = 2.0;
	protected static const float MAKE_WAY_AHEAD = 0.35;
	protected static const float MAKE_WAY_LATERAL = 1.05;
	protected static const float MAKE_WAY_GOAL = 1.0;
	protected static const float MAKE_WAY_SIDE = 1.3;
	protected static const float MAKE_WAY_MIN_MS = 1200;
	protected static const float MAKE_WAY_MAX_MS = 2800;
	protected static const float SIDE_SAMPLE = 0.8;
	protected static const float SNAP_TOLERANCE = 0.75;
	protected static const float STALL_GAP = 0.08;
	protected static const float STALL_RATE = 0.05;
	protected static const float RAY_HEIGHT = 1.0;
	// Body radius plus a small margin. The leaf still collides with characters
	// when they pass through each other, so anyone in the quarter in front of
	// an outswing door is stepped back once that swing is known.
	protected static const float SWING_CLEARANCE = 0.5;
	protected static const float OPEN_RETRY_MS = 1200;
	// A free door moves its latch far enough to show in or out well inside
	// this. Longer than that, still shut, means a body is in an outward arc.
	protected static const float SWING_REVEAL_MS = 800;

	protected static ref map<IEntity, ref KK_Opening> s_mOpenings =
		new map<IEntity, ref KK_Opening>();

	protected static ref map<AIAgent, ref KK_PassageAgentState> s_mAgents =
		new map<AIAgent, ref KK_PassageAgentState>();

	protected static ref TraceParam s_Trace;
	protected static IEntity s_TraceUser;
	protected static vector s_vProjection = Vector(0.55, 1.0, 0.55);

	static int Mode()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return KK_ENavMode.OFF;

		return mode.KK_GetNavMode();
	}

	static bool Enabled()
	{
		return Mode() == KK_ENavMode.MAKE_WAY;
	}

	static bool PassThrough()
	{
		return Mode() == KK_ENavMode.PASS_THROUGH;
	}

	static void ReleasePass(AIAgent agent)
	{
		if (!agent)
			return;

		KK_PassageAgentState state = s_mAgents.Get(agent);
		if (!state)
			return;

		EndPass(state, agent);
	}

	// Door swing clearance runs with Open doors ahead, and with Make Way.
	// Pass-through only joins that door work when Open doors ahead is on.
	static bool Active()
	{
		if (Enabled() || PassThrough())
			return true;

		return OpenDoors();
	}

	protected static bool OpenDoors()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetOpenDoors();
	}

	static void Resolve(
		notnull array<ref KK_PassageSoldier> soldiers,
		AIPathfindingComponent pathfinding,
		notnull map<AIAgent, ref KK_PassageOrder> orders)
	{
		orders.Clear();

		set<AIAgent> present = new set<AIAgent>();
		foreach (KK_PassageSoldier soldier : soldiers)
		{
			if (soldier && soldier.m_Agent)
				present.Insert(soldier.m_Agent);
		}

		if (!PassThrough())
			ReleasePassHolds();

		bool handleDoors = Enabled() || OpenDoors();
		set<AIAgent> claimed = new set<AIAgent>();
		if (handleDoors)
		{
			DiscoverDoors(soldiers);
			UpdateStalls();

			foreach (IEntity doorEntity, KK_Opening opening : s_mOpenings)
			{
				if (!opening)
					continue;

				ApplyDoor(opening, soldiers, pathfinding, orders, claimed);
			}
		}

		if (Enabled())
			ApplyCorridors(soldiers, pathfinding, orders, claimed);
		else if (PassThrough())
			ApplyPassThrough(soldiers, orders);

		if (handleDoors)
			ResumeReleased(soldiers, orders, claimed);
		ForgetAbsentAgents(present);
	}

	protected static void DiscoverDoors(
		notnull array<ref KK_PassageSoldier> soldiers)
	{
		foreach (KK_PassageSoldier soldier : soldiers)
		{
			if (!soldier || !soldier.m_Agent || soldier.m_bSettled)
				continue;

			IEntity user = soldier.m_Agent.GetControlledEntity();
			if (!user)
				continue;

			IEntity doorEntity = KK_DoorAssist.FindBlockingDoor(
				soldier.m_Agent,
				soldier.m_vGoal
			);

			if (!doorEntity)
				continue;

			KK_Opening opening = CaptureOpening(doorEntity, user.GetOrigin());
			if (!opening || opening.m_aLeaves.IsEmpty())
				continue;

			IEntity key = opening.m_aLeaves[0].m_Entity;
			if (!key || s_mOpenings.Contains(key))
				continue;

			s_mOpenings.Set(key, opening);
		}
	}

	protected static KK_Opening CaptureOpening(
		notnull IEntity doorEntity,
		vector finderOrigin)
	{
		IEntity owner;
		BaseDoorComponent door = FindDoor(doorEntity, owner);
		if (!door || !owner || door.IsOpen() || door.IsOpening())
			return null;

		doorEntity = owner;

		vector transform[4];
		doorEntity.GetWorldTransform(transform);

		vector mins;
		vector maxs;
		doorEntity.GetBounds(mins, maxs);

		vector extent = maxs - mins;
		bool widthAlongX = extent[0] >= extent[2];
		vector widthAxis = transform[0];
		float span = extent[0];
		if (!widthAlongX)
		{
			widthAxis = transform[2];
			span = extent[2];
		}

		widthAxis[1] = 0;
		if (widthAxis.Length() < 0.01)
			return null;

		widthAxis.Normalize();
		if (span < 0.4)
			span = 0.8;

		vector hinge = door.GetDoorPivotPointWS();
		vector centerLocal = (mins + maxs) * 0.5;
		vector worldCenter = doorEntity.CoordToParent(centerLocal);
		vector hingeToCenter = worldCenter - hinge;
		hingeToCenter[1] = 0;
		if (vector.Dot(hingeToCenter, widthAxis) < 0)
			widthAxis = -widthAxis;

		vector latch = hinge + (widthAxis * span);
		vector approach = transform[2];
		if (widthAlongX)
			approach = transform[2];
		else
			approach = transform[0];

		approach[1] = 0;
		if (approach.Length() < 0.01)
			return null;

		approach.Normalize();
		vector toFinder = finderOrigin - worldCenter;
		toFinder[1] = 0;
		if (vector.Dot(toFinder, approach) < 0)
			approach = -approach;

		KK_Opening opening = new KK_Opening();
		opening.m_vHinge = hinge;
		opening.m_vLatch = latch;
		opening.m_vCenter = (hinge + latch) * 0.5;
		opening.m_vWidth = widthAxis;
		opening.m_vApproach = approach;
		opening.m_fWidth = span;
		AddLeaf(opening, doorEntity, door);
		AbsorbSiblingLeaves(opening, doorEntity);
		opening.m_fLaneLength = MeasureLane(opening);
		return opening;
	}

	protected static void AddLeaf(
		notnull KK_Opening opening,
		notnull IEntity entity,
		notnull BaseDoorComponent door)
	{
		foreach (KK_OpeningLeaf existing : opening.m_aLeaves)
		{
			if (existing && existing.m_Entity == entity)
				return;
		}

		KK_OpeningLeaf leaf = new KK_OpeningLeaf();
		leaf.m_Entity = entity;
		leaf.m_Door = door;
		RememberLeaf(opening, leaf, entity);
		opening.m_aLeaves.Insert(leaf);
	}

	protected static void AbsorbSiblingLeaves(
		notnull KK_Opening opening,
		notnull IEntity doorEntity)
	{
		IEntity parent = doorEntity.GetParent();
		if (!parent)
			parent = doorEntity;

		IEntity child = parent.GetChildren();
		while (child)
		{
			ConsiderSiblingLeaf(opening, doorEntity, child);
			child = child.GetSibling();
		}
	}

	protected static void ConsiderSiblingLeaf(
		notnull KK_Opening opening,
		notnull IEntity doorEntity,
		notnull IEntity candidate)
	{
		if (candidate == doorEntity)
			return;

		IEntity owner;
		BaseDoorComponent other = FindDoor(candidate, owner);
		if (!other || !owner)
			return;

		candidate = owner;

		vector otherHinge = other.GetDoorPivotPointWS();
		vector delta = otherHinge - opening.m_vHinge;
		delta[1] = 0;
		float distance = delta.Length();
		if (distance < 0.3 || distance > 1.6)
			return;

		float along = Math.AbsFloat(
			vector.Dot(delta.Normalized(), opening.m_vWidth)
		);
		if (along < 0.7)
			return;

		AddLeaf(opening, candidate, other);
		ExtendOpening(opening, otherHinge, distance);
	}

	protected static void ExtendOpening(
		notnull KK_Opening opening,
		vector otherHinge,
		float hingeDistance)
	{
		vector far = otherHinge + (opening.m_vWidth * 0.8);
		float hingeAlong = vector.Dot(opening.m_vHinge, opening.m_vWidth);
		float latchAlong = vector.Dot(opening.m_vLatch, opening.m_vWidth);
		float otherAlong = vector.Dot(otherHinge, opening.m_vWidth);
		float farAlong = vector.Dot(far, opening.m_vWidth);

		float minAlong = Math.Min(hingeAlong, latchAlong);
		float maxAlong = Math.Max(hingeAlong, latchAlong);
		minAlong = Math.Min(minAlong, Math.Min(otherAlong, farAlong));
		maxAlong = Math.Max(maxAlong, Math.Max(otherAlong, farAlong));

		opening.m_vHinge = opening.m_vCenter +
			(opening.m_vWidth * (minAlong - vector.Dot(opening.m_vCenter, opening.m_vWidth)));
		opening.m_vLatch = opening.m_vCenter +
			(opening.m_vWidth * (maxAlong - vector.Dot(opening.m_vCenter, opening.m_vWidth)));
		opening.m_vCenter = (opening.m_vHinge + opening.m_vLatch) * 0.5;
		opening.m_fWidth = Math.Max(opening.m_fWidth, hingeDistance + 0.8);
	}

	protected static float MeasureLane(notnull KK_Opening opening)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return LANE_STEP;

		vector half = opening.m_vWidth * (opening.m_fWidth * 0.5);
		vector back = opening.m_vCenter + (opening.m_vApproach * 0.3);
		float length = 0.3;

		for (int step = 0; step < 16; step++)
		{
			vector next = back + (opening.m_vApproach * LANE_STEP);
			if (
				WallBetween(world, back, next) ||
				WallBetween(world, back + half, next + half) ||
				WallBetween(world, back - half, next - half)
			)
			{
				break;
			}

			length += LANE_STEP;
			back = next;
		}

		return Math.Max(length, LANE_STEP);
	}

	protected static bool WallBetween(
		notnull BaseWorld world,
		vector from,
		vector to)
	{
		if (!s_Trace)
			s_Trace = new TraceParam();

		s_TraceUser = null;
		s_Trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		s_Trace.Exclude = null;
		s_Trace.Start = from + Vector(0, RAY_HEIGHT, 0);
		s_Trace.End = to + Vector(0, RAY_HEIGHT, 0);

		float result = world.TraceMove(s_Trace, FilterPassageTrace);
		return result < 0.9;
	}

	protected static bool FilterPassageTrace(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		IEntity current = entity;
		int depth;

		while (current && depth < 8)
		{
			if (ChimeraCharacter.Cast(current))
				return false;

			current = current.GetParent();
			depth++;
		}

		return true;
	}

	protected static void UpdateStalls()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		float now = world.GetWorldTime();

		foreach (IEntity doorEntity, KK_Opening opening : s_mOpenings)
		{
			if (!opening)
				continue;

			UpdateSwing(opening);

			if (!opening.m_bOpenCalled)
				continue;

			BaseDoorComponent door = BlockingLeaf(opening);
			if (!door)
			{
				opening.m_bStalled = false;
				opening.m_bHaveState = false;
				continue;
			}

			float state = door.GetNormalizedDoorState();
			float control = door.GetControlValue();

			if (!opening.m_bHaveState)
			{
				opening.m_bHaveState = true;
				opening.m_fLastState = state;
				opening.m_fLastStateTime = now;
				continue;
			}

			float dt = (now - opening.m_fLastStateTime) / 1000.0;
			if (dt < 0.05)
				continue;

			float rate = (state - opening.m_fLastState) / dt;
			float gap = control - state;
			bool shortOfTarget = Math.AbsFloat(gap) > STALL_GAP;
			bool movingToward = gap * rate > 0.01;
			bool stopped = Math.AbsFloat(rate) < STALL_RATE;

			opening.m_bStalled =
				shortOfTarget &&
				stopped &&
				!movingToward &&
				!OpeningPassable(opening);

			opening.m_fLastState = state;
			opening.m_fLastStateTime = now;
		}
	}

	protected static void ApplyDoor(
		notnull KK_Opening opening,
		notnull array<ref KK_PassageSoldier> soldiers,
		AIPathfindingComponent pathfinding,
		notnull map<AIAgent, ref KK_PassageOrder> orders,
		notnull set<AIAgent> claimed)
	{
		array<ref KK_PassageSoldier> queue = {};
		array<float> depth = {};
		array<vector> feet = {};
		bool reserve = ReserveApproach(opening);

		foreach (KK_PassageSoldier soldier : soldiers)
		{
			if (!soldier || !soldier.m_Agent)
				continue;

			IEntity user = soldier.m_Agent.GetControlledEntity();
			if (!user)
				continue;

			vector origin = user.GetOrigin();
			bool inDisk = reserve && InSwingDisk(opening, origin);
			if (HasGoneThrough(opening, origin) && OpeningPassable(opening) && !inDisk)
				continue;

			if (
				!InLane(opening, origin) &&
				!inDisk &&
				!(reserve && HeadingThrough(opening, origin, soldier.m_vGoal))
			)
			{
				continue;
			}

			float along = ApproachDepth(opening, origin);
			InsertByDepth(queue, depth, feet, soldier, along, origin);
		}

		if (queue.IsEmpty())
			return;

		bool passable = OpeningPassable(opening);
		bool releaseFront = passable;

		array<bool> mayMove = {};
		mayMove.Resize(queue.Count());
		bool releasePeel = true;

		for (int i = queue.Count() - 1; i >= 0; i--)
		{
			vector slot = SlotPosition(opening, pathfinding, i);
			bool atSlot = vector.Distance(feet[i], slot) <= AT_SLOT;
			mayMove[i] = false;

			// Anyone standing in the swing has to leave now. Waiting for the
			// file behind them leaves a body against the leaf.
			if (InSwingDisk(opening, feet[i]))
			{
				mayMove[i] = true;
				continue;
			}

			if (atSlot)
				continue;

			if (!releasePeel)
				continue;

			mayMove[i] = true;
			KK_PassageAgentState state = AgentState(queue[i].m_Agent);
			if (!state.m_bPeelOriginSet)
			{
				state.m_bPeelOriginSet = true;
				state.m_vPeelOrigin = feet[i];
			}

			releasePeel =
				vector.Distance(feet[i], state.m_vPeelOrigin) >= BODY_LENGTH;
		}

		for (int index = 0; index < queue.Count(); index++)
		{
			KK_PassageSoldier soldier = queue[index];
			claimed.Insert(soldier.m_Agent);

			bool frontReleased = releaseFront;
			if (index > 0)
				frontReleased = releaseFront && HasGoneThrough(opening, feet[index - 1]);

			if (passable && frontReleased)
			{
				PutOrder(
					orders,
					soldier.m_Agent,
					soldier.m_vGoal,
					false,
					false
				);
				ClearPeel(soldier.m_Agent);
				continue;
			}

			vector slot = SlotPosition(opening, pathfinding, index);
			bool atSlot = vector.Distance(feet[index], slot) <= AT_SLOT;
			bool moving = mayMove[index];

			if (moving)
			{
				vector dest = slot;
				if (atSlot)
					dest = SlotPosition(opening, pathfinding, index + 1);

				PutOrder(orders, soldier.m_Agent, dest, true, false, true, false, true);
				continue;
			}

			PutOrder(
				orders,
				soldier.m_Agent,
				HoldPoint(soldier.m_Agent, feet[index]),
				true,
				true,
				true,
				false,
				true
			);
		}

		TryOpen(opening, queue[0], feet[0], soldiers);
	}

	protected static void TryOpen(
		notnull KK_Opening opening,
		notnull KK_PassageSoldier opener,
		vector openerFeet,
		notnull array<ref KK_PassageSoldier> soldiers)
	{
		if (OpeningPassable(opening))
		{
			opening.m_bStalled = false;
			opening.m_bLoggedHold = false;
			return;
		}

		IEntity user = opener.m_Agent.GetControlledEntity();
		if (!user)
			return;

		BaseWorld world = GetGame().GetWorld();
		float now = 0;
		if (world)
			now = world.GetWorldTime();

		// UseDoorAction toggles. Ask again only after the leaf has settled
		// shut, and not on the same tick the last request went out.
		if (opening.m_bOpenCalled)
		{
			if (now - opening.m_fLastOpenCall < OPEN_RETRY_MS)
				return;

			if (!ClosedAndIdle(opening))
				return;

			opening.m_bOpenCalled = false;
		}

		if (!SwingClear(opening, soldiers))
		{
			if (!opening.m_bLoggedHold && SCR_BaseGameMode.KK_LogEnabled())
			{
				opening.m_bLoggedHold = true;
				PrintFormat(
					"KK: Waiting for a clear swing on %1",
					FirstLeaf(opening)
				);
			}

			return;
		}

		// The opener only has to be near the door, not standing in the swing.
		float limit = ClearanceBack(opening) + 0.8;
		float reach = DoorReach();
		if (reach > limit)
			limit = reach;

		if (FlatDistance(openerFeet, opening.m_vCenter) > limit)
			return;

		AskOpen(opening, user);
		opening.m_fLastOpenCall = now;
	}

	protected static void AskOpen(
		notnull KK_Opening opening,
		notnull IEntity user)
	{
		foreach (KK_OpeningLeaf leaf : opening.m_aLeaves)
		{
			if (!leaf || !leaf.m_Door)
				continue;

			if (!WantsOpenCommand(leaf.m_Door))
				continue;

			leaf.m_Door.UseDoorAction(user);
			if (SCR_BaseGameMode.KK_LogEnabled())
				PrintFormat(
					"KK: Opening door %1 for %2",
					leaf.m_Entity,
					user
				);
		}

		opening.m_bOpenCalled = true;
		opening.m_bHaveState = false;
		opening.m_bStalled = false;
	}

	protected static bool WantsOpenCommand(notnull BaseDoorComponent door)
	{
		if (door.IsOpen() || door.CanCharacterPass(PASS_WIDTH))
			return false;

		if (door.IsOpening())
			return false;

		// A second UseDoorAction toggles the leaf shut. Only ask when it is
		// closed and nothing is already commanding it open.
		if (door.GetNormalizedDoorState() > 0.05)
			return false;

		if (Math.AbsFloat(door.GetControlValue()) > 0.2)
			return false;

		return true;
	}

	protected static bool ClosedAndIdle(notnull KK_Opening opening)
	{
		foreach (KK_OpeningLeaf leaf : opening.m_aLeaves)
		{
			if (!leaf || !leaf.m_Door)
				continue;

			if (leaf.m_Door.IsOpen() || leaf.m_Door.CanCharacterPass(PASS_WIDTH))
				continue;

			if (!WantsOpenCommand(leaf.m_Door))
				return false;
		}

		return true;
	}

	protected static bool SwingClear(
		notnull KK_Opening opening,
		notnull array<ref KK_PassageSoldier> soldiers)
	{
		if (!ReserveApproach(opening))
			return true;

		foreach (KK_PassageSoldier soldier : soldiers)
		{
			if (!soldier || !soldier.m_Agent)
				continue;

			IEntity user = soldier.m_Agent.GetControlledEntity();
			if (!user)
				continue;

			if (InSwingDisk(opening, user.GetOrigin()))
				return false;
		}

		return true;
	}

	protected static bool ReserveApproach(notnull KK_Opening opening)
	{
		foreach (KK_OpeningLeaf leaf : opening.m_aLeaves)
		{
			if (!LeafSweeps(opening, leaf))
				continue;

			return true;
		}

		return false;
	}

	protected static bool LeafSweeps(
		notnull KK_Opening opening,
		KK_OpeningLeaf leaf)
	{
		if (!leaf || !leaf.m_Door)
			return false;

		if (leaf.m_fRadius <= 0.05)
			return false;

		if (leaf.m_Door.IsOpen() || leaf.m_Door.CanCharacterPass(PASS_WIDTH))
			return false;

		// Direction comes from the first latch movement. Until then the squad
		// stays in the lane and the door is asked open. An inward leaf leaves
		// the approach side alone. An outward leaf needs it clear, and so does
		// a leaf that stalls against a body before its swing can be seen.
		if (!leaf.m_bSwingKnown)
			return BlockedBeforeSwing(opening);

		if (!leaf.m_bSwingsOut && !opening.m_bStalled)
			return false;

		return true;
	}

	protected static bool BlockedBeforeSwing(notnull KK_Opening opening)
	{
		if (!opening.m_bOpenCalled || !opening.m_bStalled)
			return false;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return true;

		float now = world.GetWorldTime();
		return now - opening.m_fLastOpenCall >= SWING_REVEAL_MS;
	}

	protected static bool InSwingDisk(
		notnull KK_Opening opening,
		vector origin)
	{
		foreach (KK_OpeningLeaf leaf : opening.m_aLeaves)
		{
			if (LeafSweeps(opening, leaf) && InsideLeaf(leaf, origin))
				return true;
		}

		return false;
	}

	// Quarter-disk from the closed leaf toward the squad, grown by a body width.
	protected static bool InsideLeaf(
		notnull KK_OpeningLeaf leaf,
		vector origin)
	{
		vector from = origin - leaf.m_vHinge;
		from[1] = 0;

		float outward = vector.Dot(from, leaf.m_vSwing);
		if (outward < -0.2)
			return false;

		float across = vector.Dot(from, leaf.m_vClosedDir);
		float clampedAcross = across;
		if (clampedAcross < 0)
			clampedAcross = 0;

		float clampedOut = outward;
		if (clampedOut < 0)
			clampedOut = 0;

		vector clamped =
			(leaf.m_vClosedDir * clampedAcross) +
			(leaf.m_vSwing * clampedOut);

		float length = clamped.Length();
		if (length > leaf.m_fRadius && length > 0.001)
			clamped = clamped * (leaf.m_fRadius / length);

		vector gap = from - clamped;
		return gap.Length() <= SWING_CLEARANCE;
	}

	protected static bool HeadingThrough(
		notnull KK_Opening opening,
		vector origin,
		vector goal)
	{
		float depth = ApproachDepth(opening, origin);
		float reach = opening.m_fWidth + 2.0;
		if (depth < -0.3 || depth > reach)
			return false;

		if (ApproachDepth(opening, goal) > -0.5)
			return false;

		vector goalDelta = goal - opening.m_vCenter;
		goalDelta[1] = 0;
		float goalLateral = Math.AbsFloat(vector.Dot(goalDelta, opening.m_vWidth));
		if (goalLateral > (opening.m_fWidth * 0.5) + 0.75)
			return false;

		vector delta = origin - opening.m_vCenter;
		delta[1] = 0;
		float lateral = Math.AbsFloat(vector.Dot(delta, opening.m_vWidth));
		return lateral <= (opening.m_fWidth * 0.5) + 2.5;
	}

	protected static void UpdateSwing(notnull KK_Opening opening)
	{
		if (!opening.m_bOpenCalled)
			return;

		foreach (KK_OpeningLeaf leaf : opening.m_aLeaves)
		{
			if (!leaf || leaf.m_bSwingKnown || !leaf.m_Entity)
				continue;

			vector now = leaf.m_Entity.CoordToParent(leaf.m_vLatchLocal);
			vector moved = now - leaf.m_vClosedFar;
			moved[1] = 0;
			if (moved.Length() < 0.06)
				continue;

			leaf.m_bSwingKnown = true;
			float toward = vector.Dot(moved.Normalized(), leaf.m_vSwing);
			leaf.m_bSwingsOut = toward > 0;

			if (SCR_BaseGameMode.KK_LogEnabled())
			{
				string direction = "in";
				if (leaf.m_bSwingsOut)
					direction = "out";

				PrintFormat(
					"KK: Door %1 swings %2",
					leaf.m_Entity,
					direction
				);
			}
		}
	}

	protected static void RememberLeaf(
		notnull KK_Opening opening,
		notnull KK_OpeningLeaf leaf,
		notnull IEntity entity)
	{
		vector hinge = leaf.m_Door.GetDoorPivotPointWS();
		vector local;
		vector far;
		FarCorner(entity, hinge, local, far);

		vector closed = far - hinge;
		closed[1] = 0;
		float radius = closed.Length();

		if (radius < 0.4)
		{
			closed = opening.m_vWidth;
			radius = opening.m_fWidth;
			if (radius < 0.75)
				radius = 0.9;

			far = hinge + (closed * radius);
			local = entity.CoordToLocal(far);
		}
		else
		{
			closed = closed.Normalized();
		}

		if (radius < 0.75)
			radius = 0.75;

		vector swing = opening.m_vApproach;
		float parallel = vector.Dot(swing, closed);
		swing = swing - (closed * parallel);
		swing[1] = 0;

		if (swing.Length() < 0.01)
			swing = opening.m_vApproach;
		else
			swing.Normalize();

		if (vector.Dot(swing, opening.m_vApproach) < 0)
			swing = -swing;

		leaf.m_vHinge = hinge;
		leaf.m_vClosedDir = closed;
		leaf.m_vSwing = swing;
		leaf.m_fRadius = radius;
		leaf.m_vLatchLocal = local;
		leaf.m_vClosedFar = far;
	}

	protected static void FarCorner(
		notnull IEntity entity,
		vector hinge,
		out vector local,
		out vector far)
	{
		vector mins;
		vector maxs;
		entity.GetBounds(mins, maxs);
		float y = (mins[1] + maxs[1]) * 0.5;

		local = Vector(mins[0], y, mins[2]);
		far = entity.CoordToParent(local);
		float best = FlatDistance(far, hinge);

		vector candidateLocal;
		vector candidateFar;
		float distance;
		if (ConsiderCorner(entity, hinge, Vector(maxs[0], y, mins[2]), best, candidateLocal, candidateFar, distance))
		{
			best = distance;
			local = candidateLocal;
			far = candidateFar;
		}

		if (ConsiderCorner(entity, hinge, Vector(mins[0], y, maxs[2]), best, candidateLocal, candidateFar, distance))
		{
			best = distance;
			local = candidateLocal;
			far = candidateFar;
		}

		if (ConsiderCorner(entity, hinge, Vector(maxs[0], y, maxs[2]), best, candidateLocal, candidateFar, distance))
		{
			local = candidateLocal;
			far = candidateFar;
		}
	}

	protected static bool ConsiderCorner(
		notnull IEntity entity,
		vector hinge,
		vector candidate,
		float best,
		out vector local,
		out vector far,
		out float distance)
	{
		local = candidate;
		far = entity.CoordToParent(candidate);
		distance = FlatDistance(far, hinge);
		return distance > best;
	}

	protected static float FlatDistance(vector from, vector to)
	{
		vector delta = from - to;
		delta[1] = 0;
		return delta.Length();
	}

	protected static float DoorReach()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 2;

		float reach = mode.KK_GetDoorReach();
		if (reach < 1)
			return 1;

		return reach;
	}

	protected static IEntity FirstLeaf(notnull KK_Opening opening)
	{
		if (opening.m_aLeaves.IsEmpty())
			return null;

		KK_OpeningLeaf leaf = opening.m_aLeaves[0];
		if (!leaf)
			return null;

		return leaf.m_Entity;
	}

	protected static vector SlotPosition(
		notnull KK_Opening opening,
		AIPathfindingComponent pathfinding,
		int index)
	{
		if (ReserveApproach(opening))
			return ClearSlot(opening, pathfinding, index);

		return LaneSlot(opening, pathfinding, index);
	}

	protected static vector ClearSlot(
		notnull KK_Opening opening,
		AIPathfindingComponent pathfinding,
		int index)
	{
		float along = 0.85 + (index * 0.9);
		vector side = opening.m_vWidth;
		vector approach = opening.m_vApproach;
		vector projected;

		// Back along the approach first, so the squad steps away from the leaf
		// instead of crossing the doorway to reach the hinge.
		vector backPocket =
			opening.m_vCenter +
			(approach * (ClearanceBack(opening) + (index * SLOT_GAP)));
		if (AcceptSlot(opening, pathfinding, backPocket, projected))
			return projected;

		vector hingePocket =
			opening.m_vHinge - (side * along) + (approach * 0.3);
		if (AcceptSlot(opening, pathfinding, hingePocket, projected))
			return projected;

		vector latchPocket =
			opening.m_vLatch + (side * along) + (approach * 0.3);
		if (AcceptSlot(opening, pathfinding, latchPocket, projected))
			return projected;

		return backPocket;
	}

	protected static bool AcceptSlot(
		notnull KK_Opening opening,
		AIPathfindingComponent pathfinding,
		vector desired,
		out vector projected)
	{
		projected = desired;
		if (InSwingDisk(opening, desired))
			return false;

		if (!Project(pathfinding, desired, projected))
			return false;

		return !InSwingDisk(opening, projected);
	}

	protected static float ClearanceBack(notnull KK_Opening opening)
	{
		float back = 0.8;
		for (int step = 0; step < 10; step++)
		{
			vector point = opening.m_vCenter + (opening.m_vApproach * back);
			if (!InSwingDisk(opening, point))
				return back;

			back += 0.35;
		}

		return back;
	}

	protected static vector LaneSlot(
		notnull KK_Opening opening,
		AIPathfindingComponent pathfinding,
		int index)
	{
		vector toHinge = opening.m_vHinge - opening.m_vCenter;
		toHinge[1] = 0;
		if (toHinge.Length() > 0.05)
			toHinge.Normalize();

		float back = SLOT_FIRST + (index * SLOT_GAP);
		for (int attempt = 0; attempt < 6; attempt++)
		{
			vector desired =
				opening.m_vCenter + (opening.m_vApproach * (back + (attempt * SLOT_GAP)));

			if (index == 0 && attempt == 0)
				desired = desired + (toHinge * 0.45);

			vector projected;
			if (Project(pathfinding, desired, projected))
				return projected;
		}

		return opening.m_vCenter +
			(opening.m_vApproach * (back + (5 * SLOT_GAP)));
	}

	protected static bool InLane(
		notnull KK_Opening opening,
		vector origin)
	{
		float along = ApproachDepth(opening, origin);
		if (along < -0.4 || along > opening.m_fLaneLength + 0.3)
			return false;

		vector delta = origin - opening.m_vCenter;
		delta[1] = 0;
		float lateral = Math.AbsFloat(vector.Dot(delta, opening.m_vWidth));
		return lateral <= (opening.m_fWidth * 0.5) + 0.4;
	}

	protected static bool HasGoneThrough(
		notnull KK_Opening opening,
		vector origin)
	{
		return ApproachDepth(opening, origin) < -0.5;
	}

	protected static float ApproachDepth(
		notnull KK_Opening opening,
		vector origin)
	{
		vector delta = origin - opening.m_vCenter;
		delta[1] = 0;
		return vector.Dot(delta, opening.m_vApproach);
	}

	protected static bool OpeningPassable(notnull KK_Opening opening)
	{
		if (opening.m_aLeaves.IsEmpty())
			return true;

		foreach (KK_OpeningLeaf leaf : opening.m_aLeaves)
		{
			if (!leaf || !leaf.m_Door)
				continue;

			if (leaf.m_Door.IsOpen())
				continue;

			if (!leaf.m_Door.CanCharacterPass(PASS_WIDTH))
				return false;
		}

		return true;
	}

	protected static BaseDoorComponent BlockingLeaf(notnull KK_Opening opening)
	{
		foreach (KK_OpeningLeaf leaf : opening.m_aLeaves)
		{
			if (!leaf || !leaf.m_Door)
				continue;

			if (leaf.m_Door.IsOpen() || leaf.m_Door.CanCharacterPass(PASS_WIDTH))
				continue;

			return leaf.m_Door;
		}

		return null;
	}

	protected static void InsertByDepth(
		notnull array<ref KK_PassageSoldier> queue,
		notnull array<float> depth,
		notnull array<vector> feet,
		notnull KK_PassageSoldier soldier,
		float along,
		vector origin)
	{
		int index = queue.Count();
		for (int i = 0; i < depth.Count(); i++)
		{
			if (along < depth[i])
			{
				index = i;
				break;
			}
		}

		queue.InsertAt(soldier, index);
		depth.InsertAt(along, index);
		feet.InsertAt(origin, index);
	}

	protected static void ApplyPassThrough(
		notnull array<ref KK_PassageSoldier> soldiers,
		notnull map<AIAgent, ref KK_PassageOrder> orders)
	{
		foreach (KK_PassageSoldier soldier : soldiers)
		{
			if (!soldier || !soldier.m_Agent)
				continue;

			KK_PassageAgentState state = s_mAgents.Get(soldier.m_Agent);
			if (!Usable(soldier) || soldier.m_bSettled)
			{
				if (state)
					EndPass(state, soldier.m_Agent);
				continue;
			}

			vector origin = soldier.m_Agent.GetControlledEntity().GetOrigin();
			NoteMotion(soldier.m_Agent, origin);
			state = AgentState(soldier.m_Agent);

			if (state.m_bPassing)
			{
				if (PassStillOpen(origin, state))
				{
					if (KK_SquadCollision.Hold(soldier.m_Agent))
						KeepPassMove(soldier, orders, false);
					continue;
				}

				EndPass(state, soldier.m_Agent);
				continue;
			}

			if (
				!StoppedFor(soldier.m_Agent, PASS_STOPPED_MS) &&
				!StoodFor(soldier.m_Agent, PASS_STILL_MS)
			)
			{
				continue;
			}

			StartPass(soldier, origin, orders);
		}
	}

	protected static void StartPass(
		notnull KK_PassageSoldier soldier,
		vector origin,
		notnull map<AIAgent, ref KK_PassageOrder> orders)
	{
		if (!KK_SquadCollision.Hold(soldier.m_Agent))
			return;

		KK_PassageAgentState state = AgentState(soldier.m_Agent);
		bool started = !state.m_bPassing;
		state.m_bPassing = true;
		if (started)
			state.m_vPassOrigin = origin;
		// Same-goal force rebuilt the path every time collision dropped.
		// The walk they already have continues once the bodies no longer
		// block. A new point still issues through PutOrder.
		KeepPassMove(soldier, orders, false);
		if (!started)
			return;

		if (SCR_BaseGameMode.KK_LogEnabled())
			PrintFormat(
				"KK: %1 passing through",
				soldier.m_Agent.GetControlledEntity()
			);
	}

	// A door order already in the map stays. Otherwise send the soldier
	// at his own goal again so he walks once character collision drops.
	protected static void KeepPassMove(
		notnull KK_PassageSoldier soldier,
		notnull map<AIAgent, ref KK_PassageOrder> orders,
		bool force)
	{
		if (orders.Contains(soldier.m_Agent))
			return;

		PutOrder(
			orders,
			soldier.m_Agent,
			soldier.m_vGoal,
			false,
			false,
			false,
			force
		);
	}

	protected static void EndPass(
		notnull KK_PassageAgentState state,
		notnull AIAgent agent)
	{
		if (!state.m_bPassing)
			return;

		state.m_bPassing = false;
		KK_SquadCollision.ReleaseHold(agent);
	}

	protected static void ReleasePassHolds()
	{
		array<AIAgent> agents = {};
		foreach (AIAgent agent, KK_PassageAgentState state : s_mAgents)
		{
			if (state && state.m_bPassing)
				agents.Insert(agent);
		}

		foreach (AIAgent agent : agents)
		{
			KK_PassageAgentState state = s_mAgents.Get(agent);
			if (state)
				EndPass(state, agent);
		}
	}

	protected static bool PassStillOpen(
		vector moverPos,
		notnull KK_PassageAgentState state)
	{
		vector moved = moverPos - state.m_vPassOrigin;
		moved[1] = 0;
		return moved.Length() <= PASS_CLEAR_RANGE;
	}

	protected static void ApplyCorridors(
		notnull array<ref KK_PassageSoldier> soldiers,
		AIPathfindingComponent pathfinding,
		notnull map<AIAgent, ref KK_PassageOrder> orders,
		notnull set<AIAgent> claimed)
	{
		map<AIAgent, vector> backups = new map<AIAgent, vector>();
		set<AIAgent> holds = new set<AIAgent>();
		map<AIAgent, vector> asides = new map<AIAgent, vector>();
		set<AIAgent> aside = new set<AIAgent>();
		set<AIAgent> goingThrough = new set<AIAgent>();

		foreach (KK_PassageSoldier soldier : soldiers)
		{
			if (!Usable(soldier))
				continue;

			NoteMotion(
				soldier.m_Agent,
				soldier.m_Agent.GetControlledEntity().GetOrigin()
			);
		}

		ApplyMakeWay(
			soldiers,
			pathfinding,
			claimed,
			asides,
			aside,
			goingThrough
		);

		for (int i = 0; i < soldiers.Count(); i++)
		{
			KK_PassageSoldier first = soldiers[i];
			if (!Usable(first) || claimed.Contains(first.m_Agent))
				continue;

			IEntity firstUser = first.m_Agent.GetControlledEntity();
			vector firstPos = firstUser.GetOrigin();

			for (int j = i + 1; j < soldiers.Count(); j++)
			{
				KK_PassageSoldier second = soldiers[j];
				if (!Usable(second) || claimed.Contains(second.m_Agent))
					continue;

				IEntity secondUser = second.m_Agent.GetControlledEntity();
				vector secondPos = secondUser.GetOrigin();
				if (
					aside.Contains(first.m_Agent) ||
					aside.Contains(second.m_Agent) ||
					goingThrough.Contains(first.m_Agent) ||
					goingThrough.Contains(second.m_Agent)
				)
				{
					continue;
				}

				float separation = vector.Distance(firstPos, secondPos);
				if (separation > YIELD_RANGE)
					continue;

				bool secondBlocksFirst = BlocksSegment(
					secondPos,
					firstPos,
					first.m_vGoal,
					pathfinding
				);
				bool firstBlocksSecond = BlocksSegment(
					firstPos,
					secondPos,
					second.m_vGoal,
					pathfinding
				);

				if (secondBlocksFirst && firstBlocksSecond)
				{
					bool firstYields = CloserToWidening(
						firstPos,
						second.m_vGoal,
						secondPos,
						first.m_vGoal,
						pathfinding
					);

					if (firstYields)
					{
						YieldTo(
							backups,
							holds,
							first,
							firstPos,
							second.m_vGoal,
							pathfinding,
							second.m_Agent,
							separation
						);
					}
					else
					{
						YieldTo(
							backups,
							holds,
							second,
							secondPos,
							first.m_vGoal,
							pathfinding,
							first.m_Agent,
							separation
						);
					}

					continue;
				}

				if (secondBlocksFirst)
				{
					YieldTo(
						backups,
						holds,
						second,
						secondPos,
						first.m_vGoal,
						pathfinding,
						first.m_Agent,
						separation
					);
					continue;
				}

				if (firstBlocksSecond)
				{
					YieldTo(
						backups,
						holds,
						first,
						firstPos,
						second.m_vGoal,
						pathfinding,
						second.m_Agent,
						separation
					);
					continue;
				}

				if (separation > FOLLOW_HOLD)
					continue;

				if (!NarrowAt(pathfinding, firstPos, secondPos - firstPos))
					continue;

				if (!SameDirection(firstPos, first.m_vGoal, secondPos, second.m_vGoal))
					continue;

				if (IsBehind(firstPos, first.m_vGoal, secondPos))
				{
					if (YieldWindowOpen(first.m_Agent, firstPos))
						holds.Insert(first.m_Agent);
				}
				else if (IsBehind(secondPos, second.m_vGoal, firstPos))
				{
					if (YieldWindowOpen(second.m_Agent, secondPos))
						holds.Insert(second.m_Agent);
				}
			}
		}

		for (int asideIndex = 0; asideIndex < asides.Count(); asideIndex++)
		{
			AIAgent agent = asides.GetKey(asideIndex);
			holds.RemoveItem(agent);
			backups.Remove(agent);

			KK_PassageAgentState state = AgentState(agent);
			if (!state.m_MakeWayFor)
				continue;

			holds.RemoveItem(state.m_MakeWayFor);
			backups.Remove(state.m_MakeWayFor);
		}

		foreach (AIAgent agent, vector backup : backups)
		{
			holds.RemoveItem(agent);
			claimed.Insert(agent);
			PutOrder(orders, agent, backup, true, false, false);
		}

		foreach (AIAgent agent : holds)
		{
			if (claimed.Contains(agent))
				continue;

			IEntity user = agent.GetControlledEntity();
			if (!user)
				continue;

			claimed.Insert(agent);
			PutOrder(
				orders,
				agent,
				HoldPoint(agent, user.GetOrigin()),
				true,
				true
			);
		}

		foreach (AIAgent agent, vector spot : asides)
		{
			claimed.Insert(agent);
			PutOrder(orders, agent, spot, true, true, false);
			ReleaseMakeWayMover(soldiers, orders, claimed, agent);
		}
	}

	protected static void ApplyMakeWay(
		notnull array<ref KK_PassageSoldier> soldiers,
		AIPathfindingComponent pathfinding,
		notnull set<AIAgent> claimed,
		notnull map<AIAgent, vector> asides,
		notnull set<AIAgent> aside,
		notnull set<AIAgent> goingThrough)
	{
		foreach (KK_PassageSoldier soldier : soldiers)
		{
			if (!Usable(soldier))
				continue;

			if (claimed.Contains(soldier.m_Agent))
			{
				KK_PassageAgentState claimedState =
					s_mAgents.Get(soldier.m_Agent);
				if (claimedState && claimedState.m_bMakeWay)
					ClearMakeWay(claimedState);
				continue;
			}

			KK_PassageAgentState state = s_mAgents.Get(soldier.m_Agent);
			if (!state || !state.m_bMakeWay)
				continue;

			vector origin = soldier.m_Agent.GetControlledEntity().GetOrigin();
			if (!KeepMakeWay(state, soldiers, origin))
				continue;

			asides.Set(soldier.m_Agent, state.m_vMakeWayTarget);
			aside.Insert(soldier.m_Agent);
			if (state.m_MakeWayFor)
				goingThrough.Insert(state.m_MakeWayFor);
		}

		for (int i = 0; i < soldiers.Count(); i++)
		{
			KK_PassageSoldier first = soldiers[i];
			if (!Usable(first) || claimed.Contains(first.m_Agent))
				continue;

			vector firstPos = first.m_Agent.GetControlledEntity().GetOrigin();

			for (int j = i + 1; j < soldiers.Count(); j++)
			{
				KK_PassageSoldier second = soldiers[j];
				if (!Usable(second) || claimed.Contains(second.m_Agent))
					continue;

				if (
					aside.Contains(first.m_Agent) ||
					aside.Contains(second.m_Agent)
				)
				{
					continue;
				}

				vector secondPos =
					second.m_Agent.GetControlledEntity().GetOrigin();
				KK_PassageSoldier mover;
				KK_PassageSoldier blocker;
				vector moverPos;
				vector blockerPos;
				if (!ChooseMakeWay(
					first,
					firstPos,
					second,
					secondPos,
					mover,
					blocker,
					moverPos,
					blockerPos
				))
				{
					continue;
				}

				if (
					aside.Contains(blocker.m_Agent) ||
					goingThrough.Contains(blocker.m_Agent) ||
					claimed.Contains(blocker.m_Agent)
				)
				{
					continue;
				}

				if (
					!StoodStill(mover.m_Agent) ||
					!StoodStill(blocker.m_Agent)
				)
				{
					continue;
				}

				vector spot;
				if (!StepAside(
					blockerPos,
					moverPos,
					mover.m_vGoal,
					pathfinding,
					spot
				))
				{
					continue;
				}

				KK_PassageAgentState state = AgentState(blocker.m_Agent);
				state.m_bMakeWay = true;
				state.m_bMakeWayFresh = true;
				state.m_bMakeWayRepathed = false;
				state.m_fMakeWayStartedAt = Now();
				state.m_vMakeWayOrigin = blockerPos;
				state.m_vMakeWayTarget = spot;
				state.m_MakeWayFor = mover.m_Agent;
				asides.Set(blocker.m_Agent, spot);
				aside.Insert(blocker.m_Agent);
				goingThrough.Insert(mover.m_Agent);

				if (SCR_BaseGameMode.KK_LogEnabled())
					PrintFormat(
						"KK: %1 stepping aside for %2",
						blocker.m_Agent.GetControlledEntity(),
						mover.m_Agent.GetControlledEntity()
					);
			}
		}
	}

	protected static bool KeepMakeWay(
		notnull KK_PassageAgentState state,
		notnull array<ref KK_PassageSoldier> soldiers,
		vector blockerPos)
	{
		float age = Now() - state.m_fMakeWayStartedAt;
		if (age >= MAKE_WAY_MAX_MS)
		{
			ClearMakeWay(state);
			return false;
		}

		if (age < MAKE_WAY_MIN_MS)
			return true;

		KK_PassageSoldier mover = FindSoldier(soldiers, state.m_MakeWayFor);
		if (!Usable(mover))
		{
			ClearMakeWay(state);
			return false;
		}

		vector moverPos = mover.m_Agent.GetControlledEntity().GetOrigin();
		if (!MoverHasPassed(blockerPos, moverPos, mover.m_vGoal))
			return true;

		ClearMakeWay(state);
		return false;
	}

	protected static bool MoverHasPassed(
		vector blockerPos,
		vector moverPos,
		vector moverGoal)
	{
		vector toBlocker = blockerPos - moverPos;
		toBlocker[1] = 0;
		if (toBlocker.Length() > MAKE_WAY_REACH + 1.0)
			return true;

		vector toGoal = moverGoal - moverPos;
		toGoal[1] = 0;
		if (toGoal.Length() < 0.3)
			return true;

		float ahead = vector.Dot(toBlocker, toGoal) / toGoal.Length();
		return ahead < -0.35;
	}

	protected static void ClearMakeWay(notnull KK_PassageAgentState state)
	{
		state.m_bMakeWay = false;
		state.m_bMakeWayFresh = false;
		state.m_bMakeWayRepathed = false;
		state.m_MakeWayFor = null;
	}

	protected static bool ChooseMakeWay(
		notnull KK_PassageSoldier first,
		vector firstPos,
		notnull KK_PassageSoldier second,
		vector secondPos,
		out KK_PassageSoldier mover,
		out KK_PassageSoldier blocker,
		out vector moverPos,
		out vector blockerPos)
	{
		mover = null;
		blocker = null;
		moverPos = vector.Zero;
		blockerPos = vector.Zero;

		bool secondInFirst = InTheWay(secondPos, firstPos, first.m_vGoal);
		bool firstInSecond = InTheWay(firstPos, secondPos, second.m_vGoal);
		if (!secondInFirst && !firstInSecond)
			return false;

		if (secondInFirst && !firstInSecond)
		{
			mover = first;
			blocker = second;
			moverPos = firstPos;
			blockerPos = secondPos;
		}
		else if (firstInSecond && !secondInFirst)
		{
			mover = second;
			blocker = first;
			moverPos = secondPos;
			blockerPos = firstPos;
		}
		else if (first.m_bSettled && !second.m_bSettled)
		{
			mover = second;
			blocker = first;
			moverPos = secondPos;
			blockerPos = firstPos;
		}
		else if (second.m_bSettled && !first.m_bSettled)
		{
			mover = first;
			blocker = second;
			moverPos = firstPos;
			blockerPos = secondPos;
		}
		else if (
			vector.Distance(firstPos, first.m_vGoal) >=
			vector.Distance(secondPos, second.m_vGoal)
		)
		{
			mover = first;
			blocker = second;
			moverPos = firstPos;
			blockerPos = secondPos;
		}
		else
		{
			mover = second;
			blocker = first;
			moverPos = secondPos;
			blockerPos = firstPos;
		}

		// Only a soldier who still has a node to reach can demand the way.
		if (mover.m_bSettled)
			return false;

		// Two soldiers moving the same way are a file. Pulling the lead
		// off his node makes him turn around and breaks the flow.
		if (SameWayFile(mover, moverPos, blocker, blockerPos))
			return false;

		return true;
	}

	protected static bool SameWayFile(
		notnull KK_PassageSoldier mover,
		vector moverPos,
		notnull KK_PassageSoldier blocker,
		vector blockerPos)
	{
		if (blocker.m_bSettled)
			return false;

		if (!SameDirection(
			moverPos,
			mover.m_vGoal,
			blockerPos,
			blocker.m_vGoal
		))
		{
			return false;
		}

		return IsBehind(moverPos, mover.m_vGoal, blockerPos);
	}

	protected static bool InTheWay(vector body, vector from, vector goal)
	{
		vector toGoal = goal - from;
		toGoal[1] = 0;
		float goalLength = toGoal.Length();
		if (goalLength < MAKE_WAY_GOAL)
			return false;

		vector toBody = body - from;
		toBody[1] = 0;
		float separation = toBody.Length();
		if (separation < 0.05 || separation > MAKE_WAY_REACH)
			return false;

		float ahead = vector.Dot(toBody, toGoal) / goalLength;
		if (ahead < MAKE_WAY_AHEAD || ahead > goalLength - 0.3)
			return false;

		vector off = toBody - (toGoal * (ahead / goalLength));
		off[1] = 0;
		if (off.Length() <= MAKE_WAY_LATERAL)
			return true;

		if (separation > 1.5)
			return false;

		toGoal.Normalize();
		toBody.Normalize();
		return vector.Dot(toGoal, toBody) >= 0.55;
	}

	protected static bool StepAside(
		vector blockerPos,
		vector moverPos,
		vector moverGoal,
		AIPathfindingComponent pathfinding,
		out vector spot)
	{
		spot = blockerPos;

		vector forward = moverGoal - moverPos;
		forward[1] = 0;
		if (forward.Length() < 0.05)
		{
			forward = blockerPos - moverPos;
			forward[1] = 0;
		}

		if (forward.Length() < 0.05)
			return false;

		forward.Normalize();
		vector side = Vector(-forward[2], 0, forward[0]);
		KK_AsideChoice choice = new KK_AsideChoice();

		TakeAside(
			choice,
			blockerPos + (side * MAKE_WAY_SIDE),
			moverPos,
			moverGoal,
			pathfinding
		);
		TakeAside(
			choice,
			blockerPos - (side * MAKE_WAY_SIDE),
			moverPos,
			moverGoal,
			pathfinding
		);

		if (!choice.m_bFound)
		{
			TakeAside(
				choice,
				blockerPos + (side * (MAKE_WAY_SIDE + 0.6)),
				moverPos,
				moverGoal,
				pathfinding
			);
			TakeAside(
				choice,
				blockerPos - (side * (MAKE_WAY_SIDE + 0.6)),
				moverPos,
				moverGoal,
				pathfinding
			);
		}

		if (!choice.m_bFound)
		{
			vector rear = moverPos - (forward * 1.2);
			TakeAside(
				choice,
				rear + (side * 0.8),
				moverPos,
				moverGoal,
				pathfinding
			);
			TakeAside(
				choice,
				rear - (side * 0.8),
				moverPos,
				moverGoal,
				pathfinding
			);
		}

		if (!choice.m_bFound)
			return false;

		spot = choice.m_vSpot;
		return true;
	}

	protected static void TakeAside(
		notnull KK_AsideChoice choice,
		vector desired,
		vector moverPos,
		vector moverGoal,
		AIPathfindingComponent pathfinding)
	{
		vector candidate;
		if (!AsideCandidate(
			desired,
			moverPos,
			moverGoal,
			pathfinding,
			candidate
		))
		{
			return;
		}

		vector flat = candidate - moverPos;
		flat[1] = 0;
		float dist = flat.Length();
		if (choice.m_bFound && dist <= choice.m_fDist)
			return;

		choice.m_bFound = true;
		choice.m_vSpot = candidate;
		choice.m_fDist = dist;
	}

	protected static bool AsideCandidate(
		vector desired,
		vector moverPos,
		vector moverGoal,
		AIPathfindingComponent pathfinding,
		out vector spot)
	{
		spot = desired;
		vector projected;
		bool onMesh = Project(pathfinding, desired, projected);
		if (!onMesh)
			onMesh = ProjectLoose(pathfinding, desired, projected);

		if (!onMesh)
			return false;

		vector flat = projected - moverPos;
		flat[1] = 0;
		if (flat.Length() < 0.95)
			return false;

		if (StillOnPath(projected, moverPos, moverGoal))
			return false;

		spot = projected;
		return true;
	}

	protected static bool StillOnPath(vector body, vector from, vector goal)
	{
		vector toGoal = goal - from;
		toGoal[1] = 0;
		float goalLength = toGoal.Length();
		if (goalLength < 0.3)
			return false;

		vector toBody = body - from;
		toBody[1] = 0;
		float ahead = vector.Dot(toBody, toGoal) / goalLength;
		if (ahead < 0.0)
			return false;

		vector off = toBody - (toGoal * (ahead / goalLength));
		off[1] = 0;
		return off.Length() <= 0.9;
	}

	protected static void ReleaseMakeWayMover(
		notnull array<ref KK_PassageSoldier> soldiers,
		notnull map<AIAgent, ref KK_PassageOrder> orders,
		notnull set<AIAgent> claimed,
		notnull AIAgent blocker)
	{
		KK_PassageAgentState state = AgentState(blocker);
		if (!state.m_bMakeWay || !state.m_MakeWayFor)
			return;

		if (claimed.Contains(state.m_MakeWayFor))
			return;

		KK_PassageSoldier mover = FindSoldier(soldiers, state.m_MakeWayFor);
		if (!Usable(mover))
			return;

		state.m_bMakeWayFresh = false;
		claimed.Insert(mover.m_Agent);

		if (state.m_bMakeWayRepathed)
			return;

		IEntity blockerUser = blocker.GetControlledEntity();
		bool stepped =
			blockerUser &&
			vector.Distance(
				blockerUser.GetOrigin(),
				state.m_vMakeWayOrigin
			) >= 0.4;

		if (!stepped)
		{
			IEntity moverUser = mover.m_Agent.GetControlledEntity();
			PutOrder(
				orders,
				mover.m_Agent,
				HoldPoint(mover.m_Agent, moverUser.GetOrigin()),
				true,
				true,
				false
			);
			return;
		}

		state.m_bMakeWayRepathed = true;
		PutOrder(
			orders,
			mover.m_Agent,
			mover.m_vGoal,
			false,
			false,
			true,
			true
		);
	}

	protected static KK_PassageSoldier FindSoldier(
		notnull array<ref KK_PassageSoldier> soldiers,
		AIAgent agent)
	{
		if (!agent)
			return null;

		foreach (KK_PassageSoldier soldier : soldiers)
		{
			if (soldier && soldier.m_Agent == agent)
				return soldier;
		}

		return null;
	}

	protected static bool Usable(KK_PassageSoldier soldier)
	{
		return soldier &&
			soldier.m_Agent &&
			soldier.m_Agent.GetControlledEntity();
	}

	protected static bool BlocksSegment(
		vector body,
		vector from,
		vector goal,
		AIPathfindingComponent pathfinding)
	{
		if (!OnSegment(body, from, goal))
			return false;

		vector toward = goal - from;
		return NarrowAt(pathfinding, body, toward);
	}

	protected static bool OnSegment(
		vector body,
		vector from,
		vector goal)
	{
		vector toGoal = goal - from;
		toGoal[1] = 0;
		float goalLength = toGoal.Length();
		if (goalLength < 0.2)
			return false;

		vector toBody = body - from;
		toBody[1] = 0;
		float t = vector.Dot(toBody, toGoal) / (goalLength * goalLength);
		if (t <= 0.05 || t >= 0.98)
			return false;

		vector closest = from + (toGoal * t);
		vector off = body - closest;
		off[1] = 0;
		return off.Length() <= SIDE_SAMPLE;
	}

	protected static bool NarrowAt(
		AIPathfindingComponent pathfinding,
		vector origin,
		vector direction)
	{
		direction[1] = 0;
		if (direction.Length() < 0.05)
			return true;

		direction.Normalize();
		vector side = Vector(-direction[2], 0, direction[0]);
		vector left;
		vector right;
		bool leftClear = Project(pathfinding, origin + (side * SIDE_SAMPLE), left);
		bool rightClear = Project(pathfinding, origin - (side * SIDE_SAMPLE), right);
		return !leftClear && !rightClear;
	}

	protected static bool SameDirection(
		vector firstPos,
		vector firstGoal,
		vector secondPos,
		vector secondGoal)
	{
		vector firstDir = firstGoal - firstPos;
		vector secondDir = secondGoal - secondPos;
		firstDir[1] = 0;
		secondDir[1] = 0;
		if (firstDir.Length() < 0.05 || secondDir.Length() < 0.05)
			return false;

		firstDir.Normalize();
		secondDir.Normalize();
		return vector.Dot(firstDir, secondDir) > 0.5;
	}

	protected static bool IsBehind(
		vector behindPos,
		vector behindGoal,
		vector aheadPos)
	{
		vector forward = behindGoal - behindPos;
		forward[1] = 0;
		if (forward.Length() < 0.05)
			return false;

		forward.Normalize();
		vector toAhead = aheadPos - behindPos;
		toAhead[1] = 0;
		return vector.Dot(toAhead, forward) > 0.3;
	}

	protected static bool CloserToWidening(
		vector firstPos,
		vector firstOtherGoal,
		vector secondPos,
		vector secondOtherGoal,
		AIPathfindingComponent pathfinding)
	{
		float firstDistance = WideningDistance(
			firstPos,
			firstOtherGoal,
			pathfinding
		);
		float secondDistance = WideningDistance(
			secondPos,
			secondOtherGoal,
			pathfinding
		);

		return firstDistance <= secondDistance;
	}

	protected static void YieldTo(
		notnull map<AIAgent, vector> backups,
		notnull set<AIAgent> holds,
		notnull KK_PassageSoldier yielder,
		vector yielderPos,
		vector awayFromGoal,
		AIPathfindingComponent pathfinding,
		notnull AIAgent priorityAgent,
		float separation)
	{
		if (!YieldWindowOpen(yielder.m_Agent, yielderPos, priorityAgent))
			return;

		AssignBackup(
			backups,
			yielder,
			yielderPos,
			awayFromGoal,
			pathfinding
		);

		if (separation <= HOLD_RANGE)
			holds.Insert(priorityAgent);
	}

	protected static bool YieldWindowOpen(
		notnull AIAgent agent,
		vector origin,
		AIAgent other = null)
	{
		KK_PassageAgentState state = AgentState(agent);
		float now = Now();

		if (now < state.m_fYieldResumeAt)
			return false;

		if (state.m_bYieldStartSet)
		{
			float age = now - state.m_fYieldStartedAt;
			bool stepped =
				vector.Distance(origin, state.m_vYieldStart) >= YIELD_DONE;
			bool finished = stepped || age >= YIELD_MAX_MS;

			if (!finished)
				return true;

			state.m_bYieldStartSet = false;
			state.m_bHaveYieldTarget = false;

			// Soldiers who separated and came back are a new meeting.
			// Only a yield that just finished keeps them moving.
			if (age < YIELD_MAX_MS + YIELD_STALE_MS)
			{
				state.m_fYieldResumeAt = now + YIELD_RESUME_MS;
				return false;
			}
		}

		if (!StoodStill(agent))
			return false;

		if (other && !StoodStill(other))
			return false;

		state.m_bYieldStartSet = true;
		state.m_bHaveYieldTarget = false;
		state.m_vYieldStart = origin;
		state.m_fYieldStartedAt = now;
		return true;
	}

	protected static void NoteMotion(notnull AIAgent agent, vector origin)
	{
		KK_PassageAgentState state = AgentState(agent);
		float now = Now();

		if (!state.m_bHaveStill)
		{
			state.m_bHaveStill = true;
			state.m_vStillOrigin = origin;
			state.m_fStillSince = now;
			state.m_bHaveStopped = true;
			state.m_vStoppedOrigin = origin;
			state.m_fStoppedSince = now;
			return;
		}

		if (vector.Distance(origin, state.m_vStillOrigin) > YIELD_STILL_DISTANCE)
		{
			state.m_vStillOrigin = origin;
			state.m_fStillSince = now;
		}

		if (vector.Distance(origin, state.m_vStoppedOrigin) <= PASS_STOPPED_DISTANCE)
			return;

		state.m_vStoppedOrigin = origin;
		state.m_fStoppedSince = now;
	}

	protected static bool StoodStill(notnull AIAgent agent)
	{
		return StoodFor(agent, YIELD_STILL_MS);
	}

	protected static bool StoodFor(notnull AIAgent agent, float duration)
	{
		KK_PassageAgentState state = s_mAgents.Get(agent);
		if (!state || !state.m_bHaveStill)
			return false;

		return Now() - state.m_fStillSince >= duration;
	}

	protected static bool StoppedFor(notnull AIAgent agent, float duration)
	{
		KK_PassageAgentState state = s_mAgents.Get(agent);
		if (!state || !state.m_bHaveStopped)
			return false;

		return Now() - state.m_fStoppedSince >= duration;
	}

	protected static float Now()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;

		return world.GetWorldTime();
	}

	protected static void AssignBackup(
		notnull map<AIAgent, vector> backups,
		notnull KK_PassageSoldier soldier,
		vector origin,
		vector awayFromGoal,
		AIPathfindingComponent pathfinding)
	{
		if (backups.Contains(soldier.m_Agent))
			return;

		KK_PassageAgentState state = AgentState(soldier.m_Agent);
		if (!state.m_bHaveYieldTarget)
		{
			state.m_bHaveYieldTarget = true;
			state.m_vYieldTarget = FindWidening(
				origin,
				awayFromGoal,
				pathfinding
			);
		}

		backups.Set(soldier.m_Agent, state.m_vYieldTarget);
	}

	protected static float WideningDistance(
		vector origin,
		vector otherGoal,
		AIPathfindingComponent pathfinding)
	{
		vector away = origin - otherGoal;
		away[1] = 0;
		if (away.Length() < 0.05)
			return 8;

		away.Normalize();
		vector widening = FindWidening(origin, otherGoal, pathfinding);
		return vector.Distance(origin, widening);
	}

	protected static vector FindWidening(
		vector origin,
		vector otherGoal,
		AIPathfindingComponent pathfinding)
	{
		vector away = origin - otherGoal;
		away[1] = 0;
		if (away.Length() < 0.05)
			away = Vector(0, 0, 1);
		else
			away.Normalize();

		vector side = Vector(-away[2], 0, away[0]);

		for (int step = 1; step <= YIELD_SEARCH_STEPS; step++)
		{
			vector point = origin + (away * (step * YIELD_SEARCH_STEP));
			vector center;
			if (!Project(pathfinding, point, center))
				continue;

			vector left;
			vector right;
			if (
				Project(pathfinding, center + (side * SIDE_SAMPLE), left) ||
				Project(pathfinding, center - (side * SIDE_SAMPLE), right)
			)
			{
				return center;
			}
		}

		vector fallback;
		vector fallbackDesired = origin + (away * YIELD_FALLBACK);
		if (ProjectLoose(pathfinding, fallbackDesired, fallback))
			return fallback;

		return fallbackDesired;
	}

	protected static bool Project(
		AIPathfindingComponent pathfinding,
		vector desired,
		out vector projected)
	{
		projected = desired;
		if (!pathfinding)
			return false;

		vector corrected;
		if (!pathfinding.GetClosestPositionOnNavmesh(
			desired,
			s_vProjection,
			corrected
		))
		{
			return false;
		}

		vector flat = corrected - desired;
		flat[1] = 0;
		if (flat.Length() > SNAP_TOLERANCE)
			return false;

		projected = corrected;
		return true;
	}

	protected static bool ProjectLoose(
		AIPathfindingComponent pathfinding,
		vector desired,
		out vector projected)
	{
		projected = desired;
		if (!pathfinding)
			return false;

		vector corrected;
		if (!pathfinding.GetClosestPositionOnNavmesh(
			desired,
			Vector(1.5, 1.5, 1.5),
			corrected
		))
		{
			return false;
		}

		projected = corrected;
		return true;
	}

	protected static void ResumeReleased(
		notnull array<ref KK_PassageSoldier> soldiers,
		notnull map<AIAgent, ref KK_PassageOrder> orders,
		notnull set<AIAgent> claimed)
	{
		foreach (KK_PassageSoldier soldier : soldiers)
		{
			if (!soldier || !soldier.m_Agent)
				continue;

			if (claimed.Contains(soldier.m_Agent))
				continue;

			KK_PassageAgentState state = s_mAgents.Get(soldier.m_Agent);
			if (!state || !state.m_bWasOverride)
				continue;

			ClearPeel(soldier.m_Agent);
			state.m_bHaveHold = false;
			PutOrder(
				orders,
				soldier.m_Agent,
				soldier.m_vGoal,
				false,
				false
			);
		}
	}

	protected static void PutOrder(
		notnull map<AIAgent, ref KK_PassageOrder> orders,
		notnull AIAgent agent,
		vector moveTo,
		bool overrideMove,
		bool holdTimers,
		bool walk = true,
		bool force = false,
		bool door = false)
	{
		KK_PassageAgentState state = AgentState(agent);
		bool changed = !state.m_bHaveIssued ||
			vector.Distance(state.m_vIssued, moveTo) > 0.35 ||
			state.m_bWasOverride != overrideMove ||
			state.m_bWasWalk != walk;

		KK_PassageOrder order = new KK_PassageOrder();
		order.m_vMoveTo = moveTo;
		order.m_bOverride = overrideMove;
		order.m_bHoldTimers = holdTimers;
		order.m_bWalk = walk;
		order.m_bDoor = door;
		order.m_bIssueNow = changed || force;
		orders.Set(agent, order);

		state.m_bHaveIssued = true;
		state.m_vIssued = moveTo;
		state.m_bWasOverride = overrideMove;
		state.m_bWasWalk = walk;

		if (!holdTimers)
			state.m_bHaveHold = false;
	}

	protected static vector HoldPoint(notnull AIAgent agent, vector origin)
	{
		KK_PassageAgentState state = AgentState(agent);
		if (!state.m_bHaveHold)
		{
			state.m_bHaveHold = true;
			state.m_vHoldPoint = origin;
		}

		return state.m_vHoldPoint;
	}

	protected static void ClearPeel(notnull AIAgent agent)
	{
		KK_PassageAgentState state = s_mAgents.Get(agent);
		if (!state)
			return;

		state.m_bPeelOriginSet = false;
	}

	protected static KK_PassageAgentState AgentState(notnull AIAgent agent)
	{
		KK_PassageAgentState state = s_mAgents.Get(agent);
		if (!state)
		{
			state = new KK_PassageAgentState();
			s_mAgents.Set(agent, state);
		}

		return state;
	}

	protected static void ForgetAbsentAgents(notnull set<AIAgent> present)
	{
		array<AIAgent> stale = {};
		foreach (AIAgent agent, KK_PassageAgentState state : s_mAgents)
		{
			if (!present.Contains(agent))
				stale.Insert(agent);
		}

		foreach (AIAgent agent : stale)
		{
			KK_SquadCollision.ReleaseHold(agent);
			s_mAgents.Remove(agent);
		}
	}

	protected static BaseDoorComponent FindDoor(
		IEntity entity,
		out IEntity owner)
	{
		owner = null;
		IEntity current = entity;
		int depth;

		while (current && depth < 4)
		{
			BaseDoorComponent door = BaseDoorComponent.Cast(
				current.FindComponent(BaseDoorComponent)
			);

			if (door)
			{
				owner = current;
				return door;
			}

			current = current.GetParent();
			depth++;
		}

		return null;
	}
}

class KK_DoorSearchStamp
{
	vector m_vOrigin;
	float m_fTime;
}

class KK_DoorAssist
{
	protected static const float PASS_WIDTH = 0.5;
	protected static const float RAY_HEIGHT = 1.0;

	protected static vector s_Unit;
	protected static vector s_Target;
	protected static BaseDoorComponent s_Door;
	protected static IEntity s_DoorEntity;
	protected static float s_BestDistance;
	protected static float s_Reach = 2;

	protected static ref TraceParam s_Trace;
	protected static ref map<AIAgent, ref KK_DoorSearchStamp> s_mStamps =
		new map<AIAgent, ref KK_DoorSearchStamp>();

	// True while a closed door ahead should pause the move clocks.
	// openedDoor remembers the door already asked to open.
	static bool Handle(
		notnull AIAgent agent,
		vector targetPosition,
		out IEntity openedDoor)
	{
		if (!IsEnabled())
			return false;

		IEntity user = agent.GetControlledEntity();
		BaseWorld world = GetGame().GetWorld();
		if (!user || !world)
			return false;

		s_Unit = user.GetOrigin();
		s_Target = targetPosition;
		float now = world.GetWorldTime();

		if (!DueForSearch(agent, s_Unit, now))
			return KnownDoorStillBlocks(openedDoor);

		RememberSearch(agent, s_Unit, now);
		s_Door = null;
		s_DoorEntity = null;
		s_BestDistance = s_Reach + 1.0;

		if (!TraceDoor(world, user))
		{
			openedDoor = null;
			return false;
		}

		bool alreadyAsked = openedDoor == s_DoorEntity;
		bool called = false;

		if (!alreadyAsked && !s_Door.IsOpening())
		{
			s_Door.UseDoorAction(user);
			openedDoor = s_DoorEntity;
			called = true;

			PrintFormat(
				"KK: Opening door %1 for %2",
				s_DoorEntity,
				agent
			);
		}

		return called || s_Door.IsOpening();
	}

	protected static bool IsEnabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		s_Reach = mode.KK_GetDoorReach();
		return mode.KK_GetOpenDoors();
	}

	protected static bool DueForSearch(
		notnull AIAgent agent,
		vector origin,
		float now)
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		float intervalMs = 200;
		float moveDistance = 0.4;

		if (mode)
		{
			intervalMs = mode.KK_GetDoorSearchInterval() * 1000.0;
			moveDistance = mode.KK_GetDoorSearchDistance();
			s_Reach = mode.KK_GetDoorReach();
		}

		KK_DoorSearchStamp stamp = s_mStamps.Get(agent);
		if (!stamp)
			return true;

		if (now - stamp.m_fTime >= intervalMs)
			return true;

		return vector.Distance(origin, stamp.m_vOrigin) >= moveDistance;
	}

	protected static void RememberSearch(
		notnull AIAgent agent,
		vector origin,
		float now)
	{
		KK_DoorSearchStamp stamp = s_mStamps.Get(agent);
		if (!stamp)
		{
			stamp = new KK_DoorSearchStamp();
			s_mStamps.Set(agent, stamp);
		}

		stamp.m_vOrigin = origin;
		stamp.m_fTime = now;
	}

	protected static bool KnownDoorStillBlocks(IEntity openedDoor)
	{
		if (!openedDoor)
			return false;

		BaseDoorComponent door = FindDoor(openedDoor);
		if (!door)
			return false;

		if (door.IsOpen() || door.CanCharacterPass(PASS_WIDTH))
			return false;

		return true;
	}

	protected static bool TraceDoor(BaseWorld world, IEntity user)
	{
		vector toTarget = s_Target - s_Unit;
		toTarget[1] = 0;
		if (toTarget.Length() < 0.05)
			return false;

		toTarget.Normalize();
		vector start = s_Unit + Vector(0, RAY_HEIGHT, 0);

		if (!s_Trace)
			s_Trace = new TraceParam();

		s_Trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		s_Trace.Exclude = user;
		s_Trace.Start = start;
		s_Trace.End = start + (toTarget * s_Reach);

		float result = world.TraceMove(s_Trace, null);
		if (result >= 0.98)
			return false;

		ConsiderDoor(s_Trace.TraceEnt);
		return s_Door != null;
	}

	protected static void ConsiderDoor(IEntity entity)
	{
		BaseDoorComponent door = FindDoor(entity);
		if (!door)
			return;

		if (door.IsOpen() || door.CanCharacterPass(PASS_WIDTH))
			return;

		vector pivot = door.GetDoorPivotPointWS();
		vector toDoor = pivot - s_Unit;
		toDoor[1] = 0;
		float distance = toDoor.Length();

		if (distance > s_Reach || distance < 0.05)
			return;

		vector toTarget = s_Target - s_Unit;
		toTarget[1] = 0;
		if (toTarget.Length() < 0.05)
			return;

		float ahead = vector.Dot(
			toDoor.Normalized(),
			toTarget.Normalized()
		);

		if (ahead < 0.15)
			return;

		s_Door = door;
		s_DoorEntity = entity;
		s_BestDistance = distance;
	}

	protected static BaseDoorComponent FindDoor(IEntity entity)
	{
		IEntity current = entity;
		int depth;

		while (current && depth < 4)
		{
			BaseDoorComponent door = BaseDoorComponent.Cast(
				current.FindComponent(BaseDoorComponent)
			);

			if (door)
				return door;

			current = current.GetParent();
			depth++;
		}

		return null;
	}
}

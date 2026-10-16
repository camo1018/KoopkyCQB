class KK_FormationSplit
{
	int m_iHome;
	int m_iSolo;

	void KK_FormationSplit()
	{
		m_iHome = -1;
		m_iSolo = -1;
	}
}

class KK_AgentMove
{
	// MoveIndividually scores 58 plus this, so the order lands at 80.
	// That beats formation, heal, observe, and attack-not-selected.
	// Attack-selected is 90, so a soldier still shoots the target he is engaging.
	// Investigate is not used: CRX replaces that behavior and walks only the group leader.
	static const float PRIORITY_LEVEL = 22;

	static const float REISSUE_INTERVAL_MS = 2000.0;

	protected static const float OBSTACLE_TRACE = 0.5;
	// Above a climb, and still into a building wall.
	protected static const float OBSTACLE_OVER = 3.0;
	protected static const float OBSTACLE_THICK = 1.4;
	protected static const float OBSTACLE_LANDING = 1.35;

	protected static ref TraceParam s_ObstacleTrace;

	// MoveIndividually adds the priority level to its base of 58.
	// Attack-high is the emergent-threat attack, at 120 plus player level.
	// One below that still beats every normal attack, heal, and observe.
	static float AbsolutePriorityLevel()
	{
		return SCR_AIActionBase.PRIORITY_BEHAVIOR_ATTACK_HIGH_PRIORITY
			- SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_INDIVIDUALLY
			- 1;
	}

	// Above the emergent attack. A take cover sprint that loses to that
	// attack stops where it is: combat movement is locked, and the sprint
	// keeps the rifle down, so he neither runs nor shoots.
	static float SprintPriorityLevel()
	{
		return SCR_AIActionBase.PRIORITY_BEHAVIOR_ATTACK_HIGH_PRIORITY
			- SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_INDIVIDUALLY
			+ 1;
	}

	// Above the sidestep and the melee retreat. Both of those outrank a
	// danger move, and a nearby enemy uses them to shove him back from the door.
	static float EnterBuildingPriorityLevel()
	{
		float ceiling =
			SCR_AIActionBase.PRIORITY_BEHAVIOR_RETREAT_MELEE;

		float avoid =
			SCR_AIActionBase.PRIORITY_BEHAVIOR_AVOID_CHARACTER;
		if (avoid > ceiling)
			ceiling = avoid;

		float danger =
			SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_FROM_DANGER;
		if (danger > ceiling)
			ceiling = danger;

		float attack =
			SCR_AIActionBase.PRIORITY_BEHAVIOR_ATTACK_HIGH_PRIORITY;
		if (attack > ceiling)
			ceiling = attack;

		return ceiling
			- SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_INDIVIDUALLY
			+ 1;
	}

	// A target counts as seen only from inside the space being cleared.
	// CRX observe turns the head and would otherwise finish nodes through a doorway.
	static const float SIGHT_VISIT_RANGE = 8.0;

	static void Issue(
		notnull SCR_AIActivityBase activity,
		SCR_AIGroup group,
		notnull AIAgent agent,
		vector position,
		map<AIAgent, ref KK_FormationSplit> soloHandlers,
		float priorityLevel = PRIORITY_LEVEL,
		EMovementType movementType = EMovementType.RUN)
	{
		ClaimSoloHandler(group, agent, soloHandlers);
		if (KK_GarrisonHold.HealingNow(agent))
			return;

		if (CrossingObstacle(agent))
			return;

		position = CommitPastObstacle(group, agent, position);

		SCR_AIMessage_Move message = SCR_AIMessage_Move.Create(
			null,
			position,
			movementType,
			false,
			activity
		);

		SetWantedSpeed(agent, movementType);

		message.m_fPriorityLevel = priorityLevel;
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

		if (!group)
			return;

		SCR_AIGroupUtilityComponent utility =
			SCR_AIGroupUtilityComponent.Cast(
				group.FindComponent(SCR_AIGroupUtilityComponent)
			);

		if (!utility)
			return;

		utility.m_Mailbox.RequestBroadcast(message, agent);
	}

	// An authored hop that missed the mesh is stored in the air. A move
	// to that point fails immediately and the soldier never steps off.
	protected static vector OnNavmesh(SCR_AIGroup group, vector position)
	{
		if (!group)
			return position;

		AIPathfindingComponent pathfinding = AIPathfindingComponent.Cast(
			group.FindComponent(AIPathfindingComponent)
		);

		if (!pathfinding)
			return position;

		vector corrected;
		if (!pathfinding.GetClosestPositionOnNavmesh(
			position,
			Vector(1.25, 0.9, 1.25),
			corrected
		))
		{
			return position;
		}

		return corrected;
	}

	static void SetWantedSpeed(
		notnull AIAgent agent,
		EMovementType movementType)
	{
		if (KK_GarrisonHold.HealingNow(agent))
			return;

		if (CrossingObstacle(agent))
			return;

		AICharacterMovementComponent movement =
			AICharacterMovementComponent.Cast(
				agent.FindComponent(AICharacterMovementComponent)
			);

		if (!movement)
		{
			IEntity controlledEntity = agent.GetControlledEntity();
			if (!controlledEntity)
				return;

			movement = AICharacterMovementComponent.Cast(
				controlledEntity.FindComponent(
					AICharacterMovementComponent
				)
			);
		}

		if (!movement)
			return;

		if (movement.GetMovementTypeWanted() == movementType)
			return;

		movement.SetMovementTypeWanted(movementType);
	}

	// A hop and a climb are the same command. It stays up through the
	// landing. STATE_FINISH is the handoff back to a normal move.
	static bool CrossingObstacle(notnull AIAgent agent)
	{
		IEntity body = agent.GetControlledEntity();
		if (!body)
			return false;

		CharacterCommandHandlerComponent handler =
			CharacterCommandHandlerComponent.Cast(
				body.FindComponent(CharacterCommandHandlerComponent)
			);
		if (!handler)
			return false;

		CharacterCommandClimb climb = handler.GetCommandClimb();
		if (!climb)
			return false;

		return climb.GetState() != ClimbStates.STATE_FINISH;
	}

	// A goal on the far lip of a vault or a climb lands inside the obstacle,
	// and he drops back on the near side. The landing is a body-length past
	// the far face. A building wall is taller than a climb, so it is left.
	protected static vector CommitPastObstacle(
		SCR_AIGroup group,
		notnull AIAgent agent,
		vector position)
	{
		IEntity body = agent.GetControlledEntity();
		BaseWorld world = GetGame().GetWorld();
		if (!body || !world || !group)
			return OnNavmesh(group, position);

		vector origin = body.GetOrigin();
		vector toGoal = position - origin;
		toGoal[1] = 0;
		float distance = toGoal.Length();
		if (distance < 2.0 || distance > 45)
			return OnNavmesh(group, position);

		toGoal.Normalize();
		float far;
		if (!ObstacleFarFace(world, body, origin, toGoal, distance, far))
			return OnNavmesh(group, position);

		float past = distance - far;
		if (past >= OBSTACLE_LANDING + 0.25)
			return OnNavmesh(group, position);

		float landing = far + OBSTACLE_LANDING;
		vector desired = origin + (toGoal * landing);
		desired[1] = position[1];
		vector landed = OnNavmesh(group, desired);
		float landedPast = vector.Dot(landed - origin, toGoal) - far;
		if (landedPast < 0.9)
		{
			desired = origin + (toGoal * (far + OBSTACLE_LANDING + 0.6));
			desired[1] = position[1];
			landed = OnNavmesh(group, desired);
			landedPast = vector.Dot(landed - origin, toGoal) - far;
		}

		if (landedPast < 0.9)
			return OnNavmesh(group, position);

		return landed;
	}

	// Thin, and clear above it. Fences, rails, and walls he can climb.
	protected static bool ObstacleFarFace(
		notnull BaseWorld world,
		notnull IEntity body,
		vector origin,
		vector direction,
		float distance,
		out float far)
	{
		far = 0;
		if (!s_ObstacleTrace)
			s_ObstacleTrace = new TraceParam();

		vector lowStart = origin + Vector(0, OBSTACLE_TRACE, 0);
		s_ObstacleTrace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		s_ObstacleTrace.Exclude = body;
		s_ObstacleTrace.Start = lowStart;
		s_ObstacleTrace.End = lowStart + (direction * distance);

		float low = world.TraceMove(s_ObstacleTrace, FilterObstacle);
		if (low >= 0.98)
			return false;

		float near = low * distance;
		vector overStart = origin + Vector(0, OBSTACLE_OVER, 0);
		s_ObstacleTrace.Start = overStart;
		s_ObstacleTrace.End = overStart + (direction * distance);
		float over = world.TraceMove(s_ObstacleTrace, FilterObstacle);
		if (over * distance < near + 0.8)
			return false;

		float cursor = near + 0.15;
		float limit = near + OBSTACLE_THICK;
		while (cursor < limit)
		{
			s_ObstacleTrace.Start = lowStart + (direction * cursor);
			s_ObstacleTrace.End = s_ObstacleTrace.Start + (direction * 0.3);
			float step = world.TraceMove(s_ObstacleTrace, FilterObstacle);
			if (step >= 0.98)
			{
				far = cursor;
				return true;
			}

			cursor += 0.3;
		}

		return false;
	}

	protected static bool FilterObstacle(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		IEntity current = entity;
		int depth;

		while (current && depth < 6)
		{
			if (ChimeraCharacter.Cast(current))
				return false;

			if (current.FindComponent(BaseDoorComponent))
				return false;

			current = current.GetParent();
			depth++;
		}

		return true;
	}

	static void ReleaseHandlers(
		SCR_AIGroup group,
		map<AIAgent, ref KK_FormationSplit> soloHandlers)
	{
		if (!soloHandlers || soloHandlers.Count() == 0)
			return;

		AIGroupMovementComponent movement = MovementOf(group);
		array<AIAgent> agents = {};
		for (int i = 0; i < soloHandlers.Count(); i++)
			agents.Insert(soloHandlers.GetKey(i));

		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			ReturnToFormation(movement, agent, soloHandlers.Get(agent));
		}

		soloHandlers.Clear();
	}

	protected static void ClaimSoloHandler(
		SCR_AIGroup group,
		notnull AIAgent agent,
		map<AIAgent, ref KK_FormationSplit> soloHandlers)
	{
		if (!group || !soloHandlers)
			return;

		AIGroupMovementComponent movement = MovementOf(group);
		if (!movement)
			return;

		int currentHandler = movement.GetAgentMoveHandlerId(agent);
		if (currentHandler < 0)
			return;

		KK_FormationSplit split;
		if (soloHandlers.Find(agent, split) && split)
		{
			if (currentHandler == split.m_iSolo)
				return;

			// Combat AI merged him off the handler this order created.
			RemoveHandler(movement, split.m_iSolo, split.m_iHome);
			split.m_iSolo = -1;
			currentHandler = movement.GetAgentMoveHandlerId(agent);
			if (currentHandler < 0)
				return;

			if (movement.GetMoveHandlerAgentCount(currentHandler) <= 1)
			{
				// Still off the squad formation. Remember where he is so
				// the order can put him back on the one he left.
				split.m_iSolo = currentHandler;
				return;
			}
		}
		else if (movement.GetMoveHandlerAgentCount(currentHandler) <= 1)
		{
			// Already separated from the formation, so CRX will treat him as a leader.
			return;
		}

		int createdHandler = movement.CreateGroupMoveHandler("Column");
		if (createdHandler <= 0)
			return;

		int home = currentHandler;
		if (split)
			home = split.m_iHome;

		movement.SetMoveHandlerLeader(
			agent,
			currentHandler,
			createdHandler
		);

		if (!split)
		{
			split = new KK_FormationSplit();
			split.m_iHome = home;
			soloHandlers.Set(agent, split);
		}

		split.m_iSolo = createdHandler;

		if (SCR_BaseGameMode.KK_LogEnabled())
			PrintFormat(
				"KK: Unit %1 split from the formation so it can move on its own",
				agent
			);
	}

	protected static void ReturnToFormation(
		AIGroupMovementComponent movement,
		notnull AIAgent agent,
		KK_FormationSplit split)
	{
		if (!movement || !split)
			return;

		int currentHandler = movement.GetAgentMoveHandlerId(agent);
		int home = split.m_iHome;
		if (
			currentHandler >= 0 &&
			home >= 0 &&
			currentHandler != home
		)
		{
			movement.SetMoveHandlerLeader(agent, currentHandler, home);
		}

		RemoveHandler(movement, split.m_iSolo, home);
	}

	protected static void RemoveHandler(
		AIGroupMovementComponent movement,
		int handlerId,
		int keepId)
	{
		if (!movement || handlerId < 0 || handlerId == keepId)
			return;

		movement.RemoveGroupMoveHandler(handlerId);
	}

	protected static AIGroupMovementComponent MovementOf(SCR_AIGroup group)
	{
		if (!group)
			return null;

		return AIGroupMovementComponent.Cast(
			group.FindComponent(AIGroupMovementComponent)
		);
	}
}

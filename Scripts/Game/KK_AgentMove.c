class KK_AgentMove
{
	// MoveIndividually scores 58 plus this, so the order lands at 80.
	// That beats formation, heal, observe, and attack-not-selected.
	// Attack-selected is 90, so a soldier still shoots the target he is engaging.
	// Investigate is not used: CRX replaces that behavior and walks only the group leader.
	static const float PRIORITY_LEVEL = 22;

	static const float REISSUE_INTERVAL_MS = 2000.0;

	// MoveIndividually adds the priority level to its base of 58.
	// Attack-high is the emergent-threat attack, at 120 plus player level.
	// One below that still beats every normal attack, heal, and observe.
	static float AbsolutePriorityLevel()
	{
		return SCR_AIActionBase.PRIORITY_BEHAVIOR_ATTACK_HIGH_PRIORITY
			- SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_INDIVIDUALLY
			- 1;
	}

	// A target counts as seen only from inside the space being cleared.
	// CRX observe turns the head and would otherwise finish nodes through a doorway.
	static const float SIGHT_VISIT_RANGE = 8.0;

	static void Issue(
		notnull SCR_AIActivityBase activity,
		SCR_AIGroup group,
		notnull AIAgent agent,
		vector position,
		map<AIAgent, int> soloHandlers,
		float priorityLevel = PRIORITY_LEVEL,
		EMovementType movementType = EMovementType.RUN)
	{
		ClaimSoloHandler(group, agent, soloHandlers);

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

	static void SetWantedSpeed(
		notnull AIAgent agent,
		EMovementType movementType)
	{
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

		movement.SetMovementTypeWanted(movementType);
	}

	static void ReleaseHandlers(
		SCR_AIGroup group,
		map<AIAgent, int> soloHandlers)
	{
		if (!soloHandlers || soloHandlers.Count() == 0)
			return;

		for (int i = soloHandlers.Count() - 1; i >= 0; i--)
		{
			int handlerId = soloHandlers.GetElement(i);
			ReleaseHandler(group, handlerId);
		}

		soloHandlers.Clear();
	}

	protected static void ClaimSoloHandler(
		SCR_AIGroup group,
		notnull AIAgent agent,
		map<AIAgent, int> soloHandlers)
	{
		if (!group || !soloHandlers)
			return;

		AIGroupMovementComponent movement =
			AIGroupMovementComponent.Cast(
				group.FindComponent(AIGroupMovementComponent)
			);

		if (!movement)
			return;

		int currentHandler = movement.GetAgentMoveHandlerId(agent);
		if (currentHandler < 0)
			return;

		if (soloHandlers.Contains(agent))
		{
			int recordedHandler = soloHandlers.Get(agent);
			if (currentHandler == recordedHandler)
				return;

			// Combat AI merged him back into the squad formation.
			ReleaseHandler(group, recordedHandler);
			soloHandlers.Remove(agent);
			currentHandler = movement.GetAgentMoveHandlerId(agent);
			if (currentHandler < 0)
				return;
		}

		// Already separated from the formation, so CRX will treat him as a leader.
		if (movement.GetMoveHandlerAgentCount(currentHandler) <= 1)
			return;

		int createdHandler = movement.CreateGroupMoveHandler("Column");
		if (createdHandler <= 0)
			return;

		movement.SetMoveHandlerLeader(
			agent,
			currentHandler,
			createdHandler
		);

		soloHandlers.Set(agent, createdHandler);

		PrintFormat(
			"KK: Unit %1 split from the formation so it can move on its own",
			agent
		);
	}

	protected static void ReleaseHandler(
		SCR_AIGroup group,
		int handlerId)
	{
		if (!group || handlerId <= 0)
			return;

		AIGroupMovementComponent movement =
			AIGroupMovementComponent.Cast(
				group.FindComponent(AIGroupMovementComponent)
			);

		if (!movement)
			return;

		movement.RemoveGroupMoveHandler(handlerId);
	}
}

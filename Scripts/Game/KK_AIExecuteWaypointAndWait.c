class KK_AIExecuteWaypointAndWait : AITaskScripted
{
	protected SCR_AIGroupUtilityComponent m_Utility;
	protected bool m_bExecuted;

	override void OnInit(AIAgent owner)
	{
		SCR_AIGroup group = SCR_AIGroup.Cast(owner);
		if (!group)
		{
			SCR_AgentMustBeAIGroup(this, owner);
			return;
		}

		m_Utility = SCR_AIGroupUtilityComponent.Cast(
			group.FindComponent(SCR_AIGroupUtilityComponent)
		);
	}

	override void OnEnter(AIAgent owner)
	{
		m_bExecuted = false;
	}

	override ENodeResult EOnTaskSimulate(
		AIAgent owner,
		float dt)
	{
		if (!m_Utility)
			return NodeError(
				this,
				owner,
				"No group utility component found!"
			);

		if (!m_bExecuted)
		{
			m_bExecuted = true;
			m_Utility.OnExecuteWaypointTree();
		}

		// Keep the waypoint active until its custom activity
		// completes it or the command is cancelled.
		return ENodeResult.RUNNING;
	}

	protected static override bool CanReturnRunning()
	{
		return true;
	}

	protected static override bool VisibleInPalette()
	{
		return true;
	}

	protected static override string GetOnHoverDescription()
	{
		return "Starts the custom waypoint state and remains active.";
	}
}
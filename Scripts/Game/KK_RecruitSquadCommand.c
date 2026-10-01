[BaseContainerProps(), SCR_BaseGroupCommandTitleField("m_sCommandName")]
class KK_RecruitSquadCommand : SCR_RecruitAIGroupCommand
{
	override bool Execute(
		IEntity cursorTarget,
		IEntity groupEnt,
		vector targetPosition,
		int playerID,
		bool isClient)
	{
		if (isClient)
			return true;

		SCR_PlayerController playerController = SCR_PlayerController.Cast(
			GetGame().GetPlayerManager().GetPlayerController(playerID)
		);
		if (!playerController)
			return false;

		SCR_PlayerControllerGroupComponent groupController =
			SCR_PlayerControllerGroupComponent.Cast(
				playerController.FindComponent(SCR_PlayerControllerGroupComponent)
			);
		if (!groupController)
			return false;

		SCR_AIGroup playerGroup = groupController.GetPlayersGroup();
		if (!playerGroup)
			return false;

		SCR_AIGroup slaveGroup = playerGroup.GetSlave();
		if (!slaveGroup)
			return false;

		SCR_Faction playerFaction = PlayerFaction(playerID);
		array<SCR_ChimeraCharacter> members = {};
		CollectSquad(cursorTarget, members);

		int incoming = 0;
		foreach (SCR_ChimeraCharacter member : members)
		{
			if (CanRecruitMember(member, groupController, playerGroup, playerFaction))
				incoming++;
		}

		if (!SquadFits(slaveGroup, incoming))
			return false;

		foreach (SCR_ChimeraCharacter member : members)
		{
			if (!CanRecruitMember(member, groupController, playerGroup, playerFaction))
				continue;

			groupController.RequestAddAIAgent(member);
		}

		return incoming > 0;
	}

	override bool CanBeExecuted(IEntity target)
	{
		if (!super.CanBeExecuted(target))
			return false;

		SCR_PlayerControllerGroupComponent groupController =
			SCR_PlayerControllerGroupComponent.GetLocalPlayerControllerGroupComponent();
		if (!groupController)
			return false;

		SCR_AIGroup playerGroup = groupController.GetPlayersGroup();
		if (!playerGroup)
			return false;

		SCR_AIGroup slaveGroup = playerGroup.GetSlave();
		if (!slaveGroup)
			return false;

		int playerID = GetGame().GetPlayerController().GetPlayerId();
		SCR_Faction playerFaction = PlayerFaction(playerID);
		array<SCR_ChimeraCharacter> members = {};
		CollectSquad(target, members);

		int incoming = 0;
		foreach (SCR_ChimeraCharacter member : members)
		{
			if (CanRecruitMember(member, groupController, playerGroup, playerFaction))
				incoming++;
		}

		if (SquadFits(slaveGroup, incoming))
			return true;

		SetCannotExecuteReason(SCR_BaseRadialCommand.CANNOT_EXECUTE_SOLDIER_NA);
		return false;
	}

	protected void CollectSquad(
		IEntity cursorTarget,
		array<SCR_ChimeraCharacter> members)
	{
		members.Clear();

		SCR_ChimeraCharacter aimed = SCR_ChimeraCharacter.Cast(cursorTarget);
		if (!aimed)
			return;

		AIGroup source = GroupOf(aimed);
		if (!source)
		{
			members.Insert(aimed);
			return;
		}

		array<AIAgent> agents = {};
		source.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(
				agent.GetControlledEntity()
			);
			if (member)
				members.Insert(member);
		}

		if (members.Find(aimed) == -1)
			members.Insert(aimed);
	}

	protected bool CanRecruitMember(
		SCR_ChimeraCharacter character,
		SCR_PlayerControllerGroupComponent groupController,
		SCR_AIGroup playerGroup,
		SCR_Faction playerFaction)
	{
		if (!character)
			return false;

		if (!character.IsRecruitable() || character.IsRecruited())
			return false;

		if (GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(character) != 0)
			return false;

		Faction targetFaction = character.GetFaction();
		Faction controllerFaction = playerGroup.GetFaction();
		if (targetFaction && controllerFaction && targetFaction.GetFactionKey() != controllerFaction.GetFactionKey())
			return false;

		return !groupController.IsAICharacterInAnyGroup(character, playerFaction);
	}

	protected bool SquadFits(SCR_AIGroup slaveGroup, int incoming)
	{
		if (incoming <= 0 || !slaveGroup)
			return false;

		SCR_CommandingManagerComponent commandingManager =
			SCR_CommandingManagerComponent.GetInstance();
		if (!commandingManager)
			return false;

		int maxAI = commandingManager.GetMaxAIPerGroup();
		if (maxAI == -1)
			return true;

		return slaveGroup.GetServerAgentsCount() + incoming <= maxAI;
	}

	protected SCR_Faction PlayerFaction(int playerID)
	{
		SCR_FactionManager factionManager = SCR_FactionManager.Cast(
			GetGame().GetFactionManager()
		);
		if (!factionManager)
			return null;

		return SCR_Faction.Cast(factionManager.GetPlayerFaction(playerID));
	}

	protected AIGroup GroupOf(IEntity body)
	{
		AIControlComponent control = AIControlComponent.Cast(
			body.FindComponent(AIControlComponent)
		);
		if (!control)
			return null;

		AIAgent agent = control.GetAIAgent();
		if (!agent)
			return null;

		return agent.GetParentGroup();
	}
}

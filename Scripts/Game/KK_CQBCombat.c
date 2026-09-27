modded class SCR_AICombatComponent
{
	void KK_ClearTarget()
	{
		m_SelectedTarget = null;
	}

	override void UpdatePerceptionFactor(
		PerceptionComponent perceptionComp,
		SCR_AIThreatSystem threatSystem)
	{
		if (KK_GarrisonHold.IsIgnoringTargets(GetOwner()))
		{
			if (perceptionComp)
				perceptionComp.SetPerceptionFactor(0);

			return;
		}

		if (
			!perceptionComp ||
			!threatSystem ||
			!KK_PerceptionBoost.IsActiveSoldier(GetOwner()) ||
			!KK_PerceptionBoost.UseSharpCombat()
		)
		{
			super.UpdatePerceptionFactor(perceptionComp, threatSystem);
			return;
		}

		EAIThreatState threatState = threatSystem.GetState();
		float perceptionFactor = PERCEPTION_FACTOR_SAFE;

		switch (threatState)
		{
			case EAIThreatState.VIGILANT:
				perceptionFactor = PERCEPTION_FACTOR_VIGILANT;
				break;
			case EAIThreatState.ALERTED:
				perceptionFactor = PERCEPTION_FACTOR_ALERTED;
				break;
			case EAIThreatState.THREATENED:
				perceptionFactor = PERCEPTION_FACTOR_ALERTED;
				break;
		}

		perceptionFactor *= m_fEquipmentPerceptionFactor;
		perceptionFactor *= m_fPerceptionFactor;
		perceptionComp.SetPerceptionFactor(perceptionFactor);
	}

	override void EvaluateWeaponAndTarget(
		out bool outWeaponEvent,
		out bool outSelectedTargetChanged,
		out BaseTarget outPrevTarget,
		out BaseTarget outCurrentTarget,
		out bool outRetreatTargetChanged,
		out bool outCompartmentChanged)
	{
		if (KK_GarrisonHold.IsIgnoringTargets(GetOwner()))
		{
			KK_GarrisonHold.NoteTarget(GetOwner(), GetCurrentTarget());
			KK_ClearTarget();
			outWeaponEvent = false;
			outSelectedTargetChanged = false;
			outPrevTarget = null;
			outCurrentTarget = null;
			outRetreatTargetChanged = false;
			outCompartmentChanged = false;
			return;
		}

		super.EvaluateWeaponAndTarget(
			outWeaponEvent,
			outSelectedTargetChanged,
			outPrevTarget,
			outCurrentTarget,
			outRetreatTargetChanged,
			outCompartmentChanged
		);
	}
}

class KK_GarrisonHold
{
	protected static ref set<IEntity> s_Pinned = new set<IEntity>();
	protected static ref set<IEntity> s_Traveling = new set<IEntity>();
	protected static ref set<IEntity> s_IgnoringTargets = new set<IEntity>();
	protected static ref set<IEntity> s_DoorFiring = new set<IEntity>();
	protected static ref set<IEntity> s_InteriorContact = new set<IEntity>();
	protected static ref map<IEntity, IEntity> s_RushBuilding =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, vector> s_ApproachGoals =
		new map<IEntity, vector>();

	static void SetPinned(IEntity soldier, bool pinned)
	{
		if (!soldier)
			return;

		if (pinned)
		{
			s_Traveling.RemoveItem(soldier);
			s_Pinned.Insert(soldier);
		}
		else
		{
			s_Pinned.RemoveItem(soldier);
		}

		ApplyFootLock(soldier, pinned);
	}

	static bool IsPinned(IEntity soldier)
	{
		return soldier && s_Pinned.Contains(soldier);
	}

	// On the way to a post, combat must not steer them off the route.
	// Speed stays free so they can still sprint. A foot lock would stop the run.
	static void SetTraveling(IEntity soldier, bool traveling)
	{
		if (!soldier)
			return;

		if (!traveling)
		{
			s_Traveling.RemoveItem(soldier);
			return;
		}

		if (s_Pinned.Contains(soldier))
			return;

		s_Traveling.Insert(soldier);
		CancelCombatMove(soldier);
	}

	static bool IsTraveling(IEntity soldier)
	{
		return soldier && s_Traveling.Contains(soldier);
	}

	// Outside the building the sprint ignores enemies. A target raises the
	// weapon and drops the sprint, and the sprint order then blocks the shot.
	// The flag is checked every AI evaluation, not only on the garrison tick.
	static void SetIgnoringTargets(IEntity soldier, bool ignore)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (ignore)
			s_IgnoringTargets.Insert(body);
		else
			s_IgnoringTargets.RemoveItem(body);
	}

	static bool IsIgnoringTargets(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_IgnoringTargets.Contains(body);
	}

	// A door wait is allowed to shoot. The sidestep otherwise outranks the attack,
	// so he looks and raises the weapon without firing.
	static void SetDoorFiring(IEntity soldier, bool firing)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (firing)
			s_DoorFiring.Insert(body);
		else
			s_DoorFiring.RemoveItem(body);
	}

	static bool IsDoorFiring(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_DoorFiring.Contains(body);
	}

	// The sprint ignore clears the selected target. Remember an enemy who
	// was already inside so the rush can still stop for him.
	static void SetRushBuilding(IEntity soldier, IEntity building)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (building)
			s_RushBuilding.Set(body, building);
		else
			s_RushBuilding.Remove(body);
	}

	static void NoteTarget(IEntity soldier, BaseTarget target)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !target || !s_RushBuilding.Contains(body))
			return;

		IEntity building = s_RushBuilding.Get(body);
		if (!building)
			return;

		vector position = target.GetLastSeenPosition();
		IEntity targetEntity = target.GetTargetEntity();
		if (targetEntity)
			position = targetEntity.GetOrigin();

		if (PositionInside(building, position))
			s_InteriorContact.Insert(body);
	}

	static bool HasInteriorContact(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_InteriorContact.Contains(body);
	}

	static void ClearInteriorContact(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (body)
			s_InteriorContact.RemoveItem(body);
	}

	static bool PositionInside(IEntity building, vector worldPosition)
	{
		if (!building)
			return false;

		vector mins;
		vector maxs;
		building.GetBounds(mins, maxs);

		vector local = building.CoordToLocal(worldPosition);

		return local[0] >= mins[0] &&
			local[0] <= maxs[0] &&
			local[1] >= mins[1] - 1.0 &&
			local[1] <= maxs[1] + 1.0 &&
			local[2] >= mins[2] &&
			local[2] <= maxs[2];
	}

	static void SuppressTargeting(notnull AIAgent agent)
	{
		SetIgnoringTargets(agent, true);
		SetIgnoringTargets(agent.GetControlledEntity(), true);

		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (soldier)
			EnforceSprintIgnore(soldier.m_UtilityComponent);
	}

	// Called from the soldier's own evaluation so a later target reaction
	// cannot bring the weapon back up before the next garrison tick.
	static void EnforceSprintIgnore(SCR_AIUtilityComponent utility)
	{
		if (!utility)
			return;

		if (
			!IsIgnoringTargets(utility.m_OwnerEntity) &&
			!IsIgnoringTargets(utility.GetOwner())
		)
		{
			return;
		}

		SetIgnoringTargets(utility.m_OwnerEntity, true);
		SetIgnoringTargets(utility.GetOwner(), true);

		if (utility.m_PerceptionComponent)
			utility.m_PerceptionComponent.SetPerceptionFactor(0);

		if (utility.m_CombatComponent)
		{
			KK_GarrisonHold.NoteTarget(
				utility.m_OwnerEntity,
				utility.m_CombatComponent.GetCurrentTarget()
			);
			utility.m_CombatComponent.SetPerceptionFactor(0);
			utility.m_CombatComponent.KK_ClearTarget();
		}

		if (utility.m_ThreatSystem)
			utility.m_ThreatSystem.KK_IgnoreForSprint();

		if (utility.m_LookAction)
			utility.m_LookAction.Cancel();

		// A nearby enemy may already have started a sidestep. Drop it so the
		// sprint order can take the door.
		if (utility.m_CombatMoveState && utility.m_CombatMoveState.IsExecutingRequest())
			utility.m_CombatMoveState.CancelRequest();

		IEntity body = utility.m_OwnerEntity;
		if (!body)
		{
			AIAgent agent = AIAgent.Cast(utility.GetOwner());
			if (agent)
				body = agent.GetControlledEntity();
		}

		if (!body)
			return;

		CharacterControllerComponent controller =
			CharacterControllerComponent.Cast(
				body.FindComponent(CharacterControllerComponent)
			);

		if (controller)
			controller.SetWeaponRaised(false);
	}

	// The threat system and the behavior tree do not always pass the same
	// entity. The flag is the character, and an agent resolves to it.
	protected static IEntity CharacterBody(IEntity entity)
	{
		if (!entity)
			return null;

		AIAgent agent = AIAgent.Cast(entity);
		if (!agent)
			return entity;

		IEntity body = agent.GetControlledEntity();
		if (body)
			return body;

		return entity;
	}

	// While he is shooting on the way in, combat movement uses this
	// instead of a flank. The point is the next step toward the post.
	static void SetApproachGoal(IEntity soldier, vector goal)
	{
		if (!soldier)
			return;

		s_ApproachGoals.Set(soldier, goal);
	}

	static void ClearApproachGoal(IEntity soldier)
	{
		if (!soldier)
			return;

		s_ApproachGoals.Remove(soldier);
	}

	static bool GetApproachGoal(IEntity soldier, out vector goal)
	{
		if (!soldier || !s_ApproachGoals.Contains(soldier))
			return false;

		goal = s_ApproachGoals.Get(soldier);
		return true;
	}

	// Keep an attack moving toward the garrison. Aiming stays on, so this
	// is a run, not a sprint. Cover search is off, or he peels away.
	static void SteerToward(SCR_AIUtilityComponent utility, vector goal, vector aimPos)
	{
		if (!utility || !utility.m_CombatMoveState)
			return;

		SCR_AICombatMoveState state = utility.m_CombatMoveState;
		SCR_AICombatMoveRequest_Move current =
			SCR_AICombatMoveRequest_Move.Cast(state.GetRequest());

		if (
			current &&
			current.m_eState == SCR_EAICombatMoveRequestState.EXECUTING &&
			current.m_eDirection == SCR_EAICombatMoveDirection.CUSTOM_POS &&
			!current.m_bTryFindCover &&
			vector.Distance(current.m_vMovePos, goal) < 2
		)
		{
			return;
		}

		SCR_AICombatMoveRequest_Move request =
			new SCR_AICombatMoveRequest_Move();

		request.m_eReason = SCR_EAICombatMoveReason.STANDARD;
		request.m_eUnitType = SCR_EAICombatMoveUnitType.CHARACTER;
		request.m_vMovePos = goal;
		request.m_vTargetPos = aimPos;
		request.m_eDirection = SCR_EAICombatMoveDirection.CUSTOM_POS;
		request.m_bTryFindCover = false;
		request.m_bFailIfNoCover = false;
		request.m_eStanceMoving = ECharacterStance.STAND;
		request.m_eStanceEnd = ECharacterStance.STAND;
		request.m_eMovementType = EMovementType.RUN;
		request.m_bAimAtTarget = true;
		request.m_bAimAtTargetEnd = true;
		request.m_fMoveDuration_s = 6;
		request.m_vAvoidStraightPathDir = vector.Zero;

		state.ApplyNewRequest(request);
	}

	// Attack movement ignores the post order. Zero walk speed leaves aiming alone.
	protected static void ApplyFootLock(IEntity soldier, bool locked)
	{
		CharacterControllerComponent controller =
			CharacterControllerComponent.Cast(
				soldier.FindComponent(CharacterControllerComponent)
			);

		if (controller)
		{
			if (locked)
				controller.OverrideMaxSpeed(0);
			else
				controller.OverrideMaxSpeed(1);
		}

		AICharacterMovementComponent movement =
			AICharacterMovementComponent.Cast(
				soldier.FindComponent(AICharacterMovementComponent)
			);

		if (movement && locked)
			movement.SetMovementTypeWanted(EMovementType.IDLE);
	}

	protected static void CancelCombatMove(IEntity soldier)
	{
		SCR_AIUtilityComponent utility =
			SCR_AIUtilityComponent.Cast(
				soldier.FindComponent(SCR_AIUtilityComponent)
			);

		if (utility && utility.m_CombatMoveState)
			utility.m_CombatMoveState.CancelRequest();
	}
}

modded class SCR_AIAttackBehavior
{
	override float CustomEvaluate()
	{
		float score = super.CustomEvaluate();

		IEntity character;
		IEntity agentEntity;
		if (m_Utility)
		{
			character = m_Utility.m_OwnerEntity;
			agentEntity = m_Utility.GetOwner();
		}

		// Getting to the post outranks aiming. He can shoot once he is there,
		// or sooner when an enemy is already inside.
		if (
			KK_GarrisonHold.IsIgnoringTargets(character) ||
			KK_GarrisonHold.IsIgnoringTargets(agentEntity)
		)
		{
			return 0;
		}

		if (
			KK_GarrisonHold.IsPinned(character) ||
			KK_GarrisonHold.IsDoorFiring(character)
		)
		{
			m_bUseCombatMove = false;
			if (KK_GarrisonHold.IsPinned(character))
				KK_GarrisonHold.SetPinned(character, true);
		}
		else if (
			KK_GarrisonHold.IsPinned(agentEntity) ||
			KK_GarrisonHold.IsDoorFiring(agentEntity)
		)
		{
			m_bUseCombatMove = false;
			if (KK_GarrisonHold.IsPinned(agentEntity))
				KK_GarrisonHold.SetPinned(agentEntity, true);
		}

		return score;
	}

	override void InitWaitTime(SCR_AIUtilityComponent utility)
	{
		if (
			utility &&
			KK_PerceptionBoost.IsActiveSoldier(utility.m_OwnerEntity) &&
			KK_PerceptionBoost.UseSharpCombat()
		)
		{
			m_fWaitTime.m_Value = 0;
			return;
		}

		super.InitWaitTime(utility);
	}
}

modded class SCR_AICombatMoveLogicBase
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		IEntity body;
		if (owner)
			body = owner.GetControlledEntity();

		if (KK_GarrisonHold.IsPinned(body))
		{
			if (m_State && m_State.IsExecutingRequest())
				m_State.CancelRequest();

			return ENodeResult.RUNNING;
		}

		vector goal;
		if (
			m_Utility &&
			KK_GarrisonHold.GetApproachGoal(body, goal)
		)
		{
			SCR_AIBehaviorBase executed =
				SCR_AIBehaviorBase.Cast(m_Utility.GetExecutedAction());

			if (executed && executed.m_bUseCombatMove)
			{
				vector aimPos = goal;
				if (m_CombatComp)
				{
					BaseTarget target = m_CombatComp.GetCurrentTarget();
					if (target)
					{
						IEntity targetEntity = target.GetTargetEntity();
						if (targetEntity)
							aimPos = targetEntity.GetOrigin();
						else
							aimPos = target.GetLastSeenPosition();
					}
				}

				KK_GarrisonHold.SteerToward(m_Utility, goal, aimPos);
				return ENodeResult.RUNNING;
			}
		}

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIAvoidCharacterBehavior
{
	override float CustomEvaluate()
	{
		if (DoorFiring())
			return 0;

		return super.CustomEvaluate();
	}

	protected bool DoorFiring()
	{
		return m_Utility &&
			(
				KK_GarrisonHold.IsDoorFiring(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsDoorFiring(m_Utility.GetOwner())
			);
	}
}

modded class SCR_AIRetreatWhileLookAtBehavior
{
	override float CustomEvaluate()
	{
		if (
			m_Utility &&
			(
				KK_GarrisonHold.IsDoorFiring(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsDoorFiring(m_Utility.GetOwner())
			)
		)
		{
			return 0;
		}

		return super.CustomEvaluate();
	}
}

modded class SCR_AIThreatSystem
{
	void KK_IgnoreForSprint()
	{
		SetThreatValues(0, 0, 0, 0);
		m_fThreatTotal = 0;
		UpdateState();
	}

	override void Update(SCR_AIUtilityComponent utility, float timeSlice)
	{
		if (
			m_Utility &&
			(
				KK_GarrisonHold.IsIgnoringTargets(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsIgnoringTargets(m_Utility.GetOwner())
			)
		)
		{
			if (m_Agent && m_Agent.GetDangerEventsCount() > 0)
				m_Agent.ClearDangerEvents(m_Agent.GetDangerEventsCount() + 1);

			KK_IgnoreForSprint();
			return;
		}

		super.Update(utility, timeSlice);
	}

	override void ThreatBulletImpact(int count)
	{
		if (IsSprintIgnoring())
			return;

		super.ThreatBulletImpact(count);
	}

	override void ThreatExplosion(float distance)
	{
		if (IsSprintIgnoring())
			return;

		super.ThreatExplosion(distance);
	}

	override void ThreatShotFired(float distance, int count)
	{
		if (IsSprintIgnoring())
			return;

		super.ThreatShotFired(distance, count);
	}

	override void ThreatProjectileFlyby(int count)
	{
		if (IsSprintIgnoring())
			return;

		super.ThreatProjectileFlyby(count);
	}

	protected bool IsSprintIgnoring()
	{
		return m_Utility &&
			(
				KK_GarrisonHold.IsIgnoringTargets(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsIgnoringTargets(m_Utility.GetOwner())
			);
	}
}

modded class SCR_AIUtilityComponent
{
	override SCR_AIBehaviorBase EvaluateBehavior(BaseTarget unknownTarget)
	{
		bool ignore =
			KK_GarrisonHold.IsIgnoringTargets(m_OwnerEntity) ||
			KK_GarrisonHold.IsIgnoringTargets(GetOwner());

		if (ignore && m_CombatComponent)
		{
			KK_GarrisonHold.NoteTarget(
				m_OwnerEntity,
				m_CombatComponent.GetCurrentTarget()
			);
			m_CombatComponent.KK_ClearTarget();
		}

		SCR_AIBehaviorBase result;
		if (ignore)
			result = super.EvaluateBehavior(null);
		else
			result = super.EvaluateBehavior(unknownTarget);

		if (ignore)
			KK_GarrisonHold.EnforceSprintIgnore(this);

		return result;
	}
}

modded class SCR_AISetWeaponRaised
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		IEntity body;
		if (owner)
			body = owner.GetControlledEntity();

		if (KK_GarrisonHold.IsIgnoringTargets(body) || KK_GarrisonHold.IsIgnoringTargets(owner))
		{
			if (body)
			{
				CharacterControllerComponent controller =
					CharacterControllerComponent.Cast(
						body.FindComponent(CharacterControllerComponent)
					);

				if (controller)
					controller.SetWeaponRaised(false);
			}

			return ENodeResult.SUCCESS;
		}

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AILookAction
{
	override void LookAt(vector pos, float priority, float duration = 0.8)
	{
		if (SprintIgnoring())
			return;

		super.LookAt(pos, priority, duration);
	}

	override void LookAt(IEntity ent, float priority, float duration = 0.8)
	{
		if (SprintIgnoring())
			return;

		super.LookAt(ent, priority, duration);
	}

	protected bool SprintIgnoring()
	{
		return m_Utility &&
			(
				KK_GarrisonHold.IsIgnoringTargets(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsIgnoringTargets(m_Utility.GetOwner())
			);
	}
}

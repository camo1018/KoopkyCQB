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

		if (!KK_GarrisonHold.UseRoomCombat() || !KK_GarrisonHold.HasBuilding(GetOwner()))
			return;

		PerceptionComponent perception = PerceptionComponent.Cast(
			GetOwner().FindComponent(PerceptionComponent)
		);
		KK_GarrisonHold.RefreshShot(GetOwner(), perception, m_SelectedTarget);

		BaseTarget preferred = KK_GarrisonHold.ShotTarget(GetOwner());
		if (!preferred || preferred == m_SelectedTarget)
			return;

		if (!KK_GarrisonHold.ShotStillVisible(GetOwner()))
			return;

		// The selector stays on one enemy. In the building, the nearest one
		// he can see replaces that fixation before the attack behavior runs.
		// The change reaction is not fired. It would run on every evaluation
		// while the selector keeps picking someone else, and that hitch stops
		// the move.
		m_SelectedTarget = preferred;
		m_SelectedTargetVisible = true;
		IEntity preferredEntity = preferred.GetTargetEntity();
		if (preferredEntity)
			m_SelectedTargetDestinationPos = preferredEntity.GetOrigin();
		else
			m_SelectedTargetDestinationPos = preferred.GetLastSeenPosition();
		outCurrentTarget = preferred;
		outSelectedTargetChanged = false;
	}
}

class KK_GarrisonHold
{
	protected static ref set<IEntity> s_Pinned = new set<IEntity>();
	protected static ref set<IEntity> s_Traveling = new set<IEntity>();
	protected static ref set<IEntity> s_IgnoringTargets = new set<IEntity>();
	protected static ref set<IEntity> s_DoorFiring = new set<IEntity>();
	protected static ref set<IEntity> s_MoveFire = new set<IEntity>();
	protected static ref set<IEntity> s_MoveWeaponUp = new set<IEntity>();
	protected static ref set<IEntity> s_MoveFiring = new set<IEntity>();
	protected static ref set<IEntity> s_MoveWeaponKnown = new set<IEntity>();
	protected static ref TraceParam s_SightTrace;
	protected static IEntity s_SightViewer;
	protected static ref map<IEntity, vector> s_ApproachGoals =
		new map<IEntity, vector>();
	protected static ref map<IEntity, IEntity> s_Buildings =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, float> s_ShotAt =
		new map<IEntity, float>();
	protected static ref map<IEntity, IEntity> s_ShotLook =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, ref BaseTarget> s_ShotBase =
		new map<IEntity, ref BaseTarget>();
	protected static ref set<IEntity> s_ShotLive = new set<IEntity>();
	protected static ref set<IEntity> s_RoomFire = new set<IEntity>();
	protected static ref array<BaseTarget> s_Perceived = new array<BaseTarget>();
	protected static ref array<IEntity> s_Candidates = new array<IEntity>();
	protected static ref array<float> s_CandidateDist = new array<float>();
	protected static ref map<IEntity, ref BaseTarget> s_CandidateKnown =
		new map<IEntity, ref BaseTarget>();
	protected static IEntity s_ScanBody;
	protected static IEntity s_ScanBuilding;
	protected static ref set<IEntity> s_ScanSeen = new set<IEntity>();
	// Milliseconds the next shot check was scheduled with.
	protected static int s_iShotTickMs;
	protected static bool s_bShotTicking;
	protected static const float ROOM_SCAN_RADIUS = 35;
	protected static const int SHOT_CANDIDATES = 8;
	protected static const float LEAN_OFFSET = 1.0;
	protected static ref map<IEntity, float> s_ShotLean = new map<IEntity, float>();
	protected static ref map<IEntity, IEntity> s_AimTarget = new map<IEntity, IEntity>();
	protected static ref map<IEntity, float> s_AimSince = new map<IEntity, float>();
	protected static ref set<IEntity> s_LeanHeld = new set<IEntity>();

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

	// Inside, he shoots without giving the attack the behavior. The route
	// stays selected, so a shot cannot plant him short of the post.
	static void SetMoveFire(IEntity soldier, bool enabled)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		bool was = s_MoveFire.Contains(body);
		if (enabled)
			s_MoveFire.Insert(body);
		else
			s_MoveFire.RemoveItem(body);

		if (was && !enabled)
		{
			SetFireWanted(body, false);
			s_MoveWeaponKnown.RemoveItem(body);
			s_MoveFiring.RemoveItem(body);
			if (s_MoveWeaponUp.Contains(body))
			{
				s_MoveWeaponUp.RemoveItem(body);
				SetWeaponRaised(body, false);
			}
		}
	}

	static bool IsMoveFire(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_MoveFire.Contains(body);
	}

	static void ApplyMoveFire(SCR_AIUtilityComponent utility)
	{
		if (!utility)
			return;

		bool firing =
			IsMoveFire(utility.m_OwnerEntity) ||
			IsMoveFire(utility.GetOwner()) ||
			IsRoomFire(utility.m_OwnerEntity) ||
			IsRoomFire(utility.GetOwner());

		if (!firing)
			return;

		if (utility.m_CombatMoveState && utility.m_CombatMoveState.IsExecutingRequest())
			utility.m_CombatMoveState.CancelRequest();

		BaseTarget selected = null;
		if (utility.m_CombatComponent)
			selected = utility.m_CombatComponent.GetCurrentTarget();

		IEntity body = utility.m_OwnerEntity;
		if (!body)
		{
			AIAgent agent = AIAgent.Cast(utility.GetOwner());
			if (agent)
				body = agent.GetControlledEntity();
		}

		RefreshShot(body, utility.m_PerceptionComponent, selected);

		firing =
			IsMoveFire(body) ||
			IsRoomFire(body);
		if (!firing)
			return;

		// Threat keeps the gun up. He fires only when a sightline reaches
		// someone. The picked enemy is checked again, so a wall that he
		// walks behind still blocks the shot.
		bool visible = ShotStillVisible(body);
		bool raise = visible || FeelsThreatened(utility);
		bool wasFiring = body && s_MoveFiring.Contains(body);
		CommandWeapon(body, raise, visible);

		if (!visible)
		{
			if (wasFiring && utility.m_LookAction)
				utility.m_LookAction.Cancel();

			return;
		}

		if (!utility.m_LookAction)
			return;

		IEntity lookAt = null;
		if (body && s_ShotLook.Contains(body))
			lookAt = s_ShotLook.Get(body);

		if (lookAt)
		{
			utility.m_LookAction.LookAt(lookAt, 100, 3);
			return;
		}

		BaseTarget shot = ShotTarget(body);
		if (shot)
			utility.m_LookAction.LookAt(shot.GetLastSeenPosition(), 100, 3);
	}

	// Only command the weapon when the stance changes. Repeating the same
	// raise or lower restarts the animation.
	protected static void CommandWeapon(IEntity body, bool raised, bool fire)
	{
		if (!body)
			return;

		bool known = s_MoveWeaponKnown.Contains(body);
		bool wasRaised = s_MoveWeaponUp.Contains(body);
		if (!known || wasRaised != raised)
		{
			s_MoveWeaponKnown.Insert(body);
			if (raised)
				s_MoveWeaponUp.Insert(body);
			else
				s_MoveWeaponUp.RemoveItem(body);

			SetWeaponRaised(body, raised);
		}

		bool wasFiring = s_MoveFiring.Contains(body);
		if (fire)
			s_MoveFiring.Insert(body);
		else
			s_MoveFiring.RemoveItem(body);

		// The raise is sent once. The trigger is sent every update, or a
		// later node clears it and the shot never starts.
		if (fire || wasFiring)
			SetFireWanted(body, fire);
	}

	protected static bool FeelsThreatened(SCR_AIUtilityComponent utility)
	{
		if (!utility || !utility.m_ThreatSystem)
			return false;

		EAIThreatState state = utility.m_ThreatSystem.GetState();
		return state == EAIThreatState.ALERTED ||
			state == EAIThreatState.THREATENED;
	}

	static bool HasBuilding(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_Buildings.Contains(body);
	}

	static bool UseRoomCombat()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetRoomCombat();
	}

	static float ShotDelaySeconds()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 0.15;

		return mode.KK_GetShotDelay();
	}

	static int ShotIntervalMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 75;

		return (int)Math.Round(mode.KK_GetShotInterval());
	}

	// Inside the order's building, this soldier's gun is ours. The attack
	// behavior can still stop him, but it does not decide the shot.
	static bool OwnsShot(IEntity soldier)
	{
		if (!UseRoomCombat())
			return false;

		IEntity body = CharacterBody(soldier);
		if (!body || !s_Buildings.Contains(body))
			return false;

		return PositionInside(s_Buildings.Get(body), body.GetOrigin());
	}

	static void ApplyRoomShot(SCR_AIUtilityComponent utility)
	{
		if (!utility)
			return;

		IEntity body = utility.m_OwnerEntity;
		if (!body)
		{
			AIAgent agent = AIAgent.Cast(utility.GetOwner());
			if (agent)
				body = agent.GetControlledEntity();
		}

		if (!OwnsShot(body))
		{
			ReleaseLean(body);
			return;
		}

		BaseTarget selected = null;
		if (utility.m_CombatComponent)
			selected = utility.m_CombatComponent.GetCurrentTarget();

		RefreshShot(body, utility.m_PerceptionComponent, selected);

		if (!s_ShotLive.Contains(body))
		{
			ClearAim(body);
			SetLean(body, 0);
			CommandWeapon(body, FeelsThreatened(utility), false);
			if (utility.m_LookAction)
				utility.m_LookAction.Cancel();
			return;
		}

		IEntity enemy = null;
		if (s_ShotLook.Contains(body))
			enemy = s_ShotLook.Get(body);

		float lean = 0;
		if (s_ShotLean.Contains(body))
			lean = s_ShotLean.Get(body);

		bool canShoot = ShotStillVisible(body);
		if (!canShoot && lean != 0)
			canShoot = SideStillClear(body, enemy, lean);

		if (canShoot)
			SetLean(body, lean);
		else
			SetLean(body, 0);

		bool fire = AimReady(body, enemy, canShoot);
		CommandWeapon(body, canShoot || FeelsThreatened(utility), fire);
		if (!fire)
			SetFireWanted(body, false);

		if (!canShoot || !utility.m_LookAction)
			return;

		if (enemy)
			utility.m_LookAction.LookAt(enemy, 100, 3);
		else
		{
			BaseTarget shot = ShotTarget(body);
			if (shot)
				utility.m_LookAction.LookAt(shot.GetLastSeenPosition(), 100, 3);
		}
	}

	static void SetGarrisonBuilding(IEntity soldier, IEntity building)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (!building)
		{
			s_Buildings.Remove(body);
			ClearShot(body);
			return;
		}

		s_Buildings.Set(body, building);
		EnsureShotTick();
	}

	// Soldiers on a clear or garrison stay in the building map, including
	// while they are still outside. The tick drops itself once that map
	// is empty. Inside, each pass raises, leans, and fires.
	protected static void EnsureShotTick()
	{
		if (s_bShotTicking || s_Buildings.Count() == 0 || !GetGame())
			return;

		s_bShotTicking = true;
		s_iShotTickMs = ShotIntervalMs();
		GetGame().GetCallqueue().CallLater(ShotTick, s_iShotTickMs, false);
	}

	static void ShotTick()
	{
		if (!GetGame() || s_Buildings.Count() == 0 || !UseRoomCombat())
		{
			s_bShotTicking = false;
			return;
		}

		array<IEntity> bodies = {};
		for (int i = 0; i < s_Buildings.Count(); i++)
			bodies.Insert(s_Buildings.GetKey(i));

		foreach (IEntity body : bodies)
		{
			if (!body)
				continue;

			SCR_AIUtilityComponent utility = UtilityOf(body);
			if (!utility)
				continue;

			ApplyRoomShot(utility);
		}

		if (!GetGame() || s_Buildings.Count() == 0 || !UseRoomCombat())
		{
			s_bShotTicking = false;
			return;
		}

		s_iShotTickMs = ShotIntervalMs();
		GetGame().GetCallqueue().CallLater(ShotTick, s_iShotTickMs, false);
	}

	protected static SCR_AIUtilityComponent UtilityOf(IEntity body)
	{
		if (!body)
			return null;

		AIControlComponent control = AIControlComponent.Cast(
			body.FindComponent(AIControlComponent)
		);

		if (!control)
			return null;

		SCR_ChimeraAIAgent agent = SCR_ChimeraAIAgent.Cast(control.GetAIAgent());
		if (!agent)
			return null;

		return agent.m_UtilityComponent;
	}

	static bool IsRoomFire(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_RoomFire.Contains(body);
	}

	static BaseTarget ShotTarget(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_ShotBase.Contains(body))
			return null;

		return s_ShotBase.Get(body);
	}

	static bool ShotStillVisible(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_ShotLive.Contains(body))
			return false;

		if (s_ShotLook.Contains(body))
			return CanSeeEntity(body, s_ShotLook.Get(body));

		BaseTarget shot = ShotTarget(body);
		return shot && CanSeeTarget(body, shot);
	}

	// True while the room gun has an enemy it is going to shoot. The approach
	// stops for that shot instead of carrying him past it.
	static bool WantsSteadyShot(IEntity soldier)
	{
		if (!OwnsShot(soldier))
			return false;

		IEntity body = CharacterBody(soldier);
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (utility)
			ApplyRoomShot(utility);

		return body && s_ShotLive.Contains(body);
	}

	// The selected target is one enemy. Inside, check every perceived enemy
	// and anyone hostile in the room, then keep the nearest one a ray can reach.
	static void RefreshShot(
		IEntity soldier,
		PerceptionComponent perception,
		BaseTarget selected)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		float now = 0;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			now = world.GetWorldTime();

		if (s_ShotAt.Contains(body) && now - s_ShotAt.Get(body) < ShotIntervalMs())
		{
			UpdateRoomFire(body);
			return;
		}

		s_ShotAt.Set(body, now);
		s_ShotLook.Remove(body);
		s_ShotBase.Remove(body);
		s_ShotLean.Remove(body);
		s_ShotLive.RemoveItem(body);

		if (IsIgnoringTargets(body))
		{
			UpdateRoomFire(body);
			return;
		}

		IEntity building = null;
		if (s_Buildings.Contains(body))
			building = s_Buildings.Get(body);

		if (!building || !PositionInside(building, body.GetOrigin()))
		{
			ConsiderSelected(body, selected);
			UpdateRoomFire(body);
			return;
		}

		if (!perception)
		{
			perception = PerceptionComponent.Cast(
				body.FindComponent(PerceptionComponent)
			);
		}

		s_Candidates.Clear();
		s_CandidateDist.Clear();
		s_CandidateKnown.Clear();
		ConsiderIndoor(perception, body, building, ETargetCategory.ENEMY, false);
		ConsiderIndoor(perception, body, building, ETargetCategory.DETECTED, true);
		ConsiderIndoor(perception, body, building, ETargetCategory.UNKNOWN, true);
		CollectRoom(body, building);

		for (int i = 0; i < s_Candidates.Count(); i++)
		{
			IEntity enemy = s_Candidates[i];
			if (!enemy)
				continue;

			float lean;
			if (!CanEngage(body, enemy, lean))
				continue;

			s_ShotLive.Insert(body);
			s_ShotLook.Set(body, enemy);
			s_ShotLean.Set(body, lean);

			BaseTarget known = null;
			if (s_CandidateKnown.Contains(enemy))
				known = s_CandidateKnown.Get(enemy);

			if (!known && perception)
				known = perception.FindTargetPerceptionObject(enemy);

			if (known)
				s_ShotBase.Set(body, known);

			UpdateRoomFire(body);
			return;
		}

		ConsiderSelected(body, selected);
		UpdateRoomFire(body);
	}

	protected static void ConsiderSelected(IEntity body, BaseTarget selected)
	{
		if (!selected || !CanSeeTarget(body, selected))
			return;

		s_ShotLive.Insert(body);
		s_ShotBase.Set(body, selected);
		IEntity enemy = selected.GetTargetEntity();
		if (enemy)
		{
			s_ShotLook.Set(body, enemy);
			float lean;
			CanEngage(body, enemy, lean);
			s_ShotLean.Set(body, lean);
		}
		else
			s_ShotLean.Set(body, 0);
	}

	protected static void UpdateRoomFire(IEntity body)
	{
		// At the post the attack behavior shoots a perception target. Someone
		// in the room it has not registered yet still has to be shot, and the
		// old selected target must not pull that shot back.
		bool want =
			s_ShotLive.Contains(body) &&
			!s_ShotBase.Contains(body) &&
			!IsMoveFire(body) &&
			!IsIgnoringTargets(body);

		bool was = s_RoomFire.Contains(body);
		if (want)
			s_RoomFire.Insert(body);
		else
			s_RoomFire.RemoveItem(body);

		if (was && !want && !IsMoveFire(body))
		{
			SetFireWanted(body, false);
			s_MoveFiring.RemoveItem(body);
		}
	}

	protected static void ClearShot(IEntity body)
	{
		if (!body)
			return;

		bool wasRoom = s_RoomFire.Contains(body);
		s_ShotAt.Remove(body);
		s_ShotLook.Remove(body);
		s_ShotBase.Remove(body);
		s_ShotLean.Remove(body);
		s_ShotLive.RemoveItem(body);
		s_RoomFire.RemoveItem(body);
		ClearAim(body);
		ReleaseLean(body);

		if (wasRoom && !IsMoveFire(body))
		{
			SetFireWanted(body, false);
			s_MoveFiring.RemoveItem(body);
		}
	}

	protected static void ConsiderIndoor(
		PerceptionComponent perception,
		IEntity body,
		IEntity building,
		ETargetCategory category,
		bool requireHostile)
	{
		if (!perception)
			return;

		s_Perceived.Clear();
		perception.GetTargetsList(s_Perceived, category);

		foreach (BaseTarget candidate : s_Perceived)
		{
			if (!candidate)
				continue;

			ChimeraCharacter character = CharacterOf(candidate.GetTargetEntity());
			if (!character || character == body)
				continue;

			if (!IsLiving(character))
				continue;

			if (requireHostile && !IsHostile(body, character))
				continue;

			if (!PositionInside(building, character.GetOrigin()))
				continue;

			AddCandidate(
				character,
				vector.Distance(body.GetOrigin(), character.GetOrigin()),
				candidate
			);
		}
	}

	protected static void CollectRoom(IEntity body, IEntity building)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		s_ScanBody = body;
		s_ScanBuilding = building;
		s_ScanSeen.Clear();

		world.QueryEntitiesBySphere(
			body.GetOrigin(),
			ROOM_SCAN_RADIUS,
			OnRoomEnemy,
			null,
			EQueryEntitiesFlags.DYNAMIC | EQueryEntitiesFlags.WITH_OBJECT
		);

		s_ScanBody = null;
		s_ScanBuilding = null;
		s_ScanSeen.Clear();
	}

	protected static bool OnRoomEnemy(IEntity entity)
	{
		ChimeraCharacter character = CharacterOf(entity);
		if (!character || character == s_ScanBody)
			return true;

		if (s_ScanSeen.Contains(character))
			return true;

		s_ScanSeen.Insert(character);

		if (!IsLiving(character) || !IsHostile(s_ScanBody, character))
			return true;

		if (!PositionInside(s_ScanBuilding, character.GetOrigin()))
			return true;

		AddCandidate(
			character,
			vector.Distance(s_ScanBody.GetOrigin(), character.GetOrigin()),
			null
		);
		return true;
	}

	// Nearest first, and only a few of them. The sight rays then stop at the
	// first one that is actually clear.
	protected static void AddCandidate(IEntity enemy, float distance, BaseTarget target)
	{
		if (!enemy)
			return;

		int existing = s_Candidates.Find(enemy);
		if (existing >= 0)
		{
			if (target && !s_CandidateKnown.Contains(enemy))
				s_CandidateKnown.Set(enemy, target);

			return;
		}

		int index = s_Candidates.Count();
		for (int i = 0; i < s_Candidates.Count(); i++)
		{
			if (distance < s_CandidateDist[i])
			{
				index = i;
				break;
			}
		}

		if (index >= SHOT_CANDIDATES)
			return;

		s_Candidates.InsertAt(enemy, index);
		s_CandidateDist.InsertAt(distance, index);
		if (target)
			s_CandidateKnown.Set(enemy, target);

		while (s_Candidates.Count() > SHOT_CANDIDATES)
		{
			int last = s_Candidates.Count() - 1;
			IEntity dropped = s_Candidates[last];
			s_Candidates.Remove(last);
			s_CandidateDist.Remove(last);
			s_CandidateKnown.Remove(dropped);
		}
	}

	protected static ChimeraCharacter CharacterOf(IEntity entity)
	{
		IEntity current = entity;
		int depth;
		while (current && depth < 6)
		{
			ChimeraCharacter character = ChimeraCharacter.Cast(current);
			if (character)
				return character;

			current = current.GetParent();
			depth++;
		}

		return null;
	}

	protected static bool IsLiving(IEntity character)
	{
		CharacterControllerComponent controller = Controller(character);
		if (!controller)
			return false;

		return controller.GetLifeState() != ECharacterLifeState.DEAD;
	}

	protected static bool IsHostile(IEntity self, IEntity other)
	{
		if (!self || !other || self == other)
			return false;

		SCR_Faction mine = SCR_Faction.Cast(FactionOf(self));
		SCR_Faction theirs = SCR_Faction.Cast(FactionOf(other));
		if (!mine || !theirs)
			return false;

		return mine.IsFactionEnemy(theirs);
	}

	protected static Faction FactionOf(IEntity entity)
	{
		FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(
			entity.FindComponent(FactionAffiliationComponent)
		);
		if (!affiliation)
			return null;

		return affiliation.GetAffiliatedFaction();
	}

	protected static bool CanEngage(IEntity body, IEntity enemy, out float lean)
	{
		lean = 0;
		if (!body || !enemy)
			return false;

		bool left;
		bool right;
		SidesClear(body, enemy, left, right);
		if (left && !right)
			lean = -1;
		else if (right && !left)
			lean = 1;

		if (CanSeeEntity(body, enemy))
			return true;

		return left || right;
	}

	// Rays start a meter to either side. He leans toward the only side
	// that can see the enemy.
	protected static void SidesClear(
		IEntity body,
		IEntity enemy,
		out bool left,
		out bool right)
	{
		left = false;
		right = false;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			world = body.GetWorld();

		if (!world)
			return;

		vector transform[4];
		body.GetWorldTransform(transform);
		vector side = transform[0] * LEAN_OFFSET;
		vector eye = body.GetOrigin() + Vector(0, 1.6, 0);
		vector feet = enemy.GetOrigin();

		left = SideRay(world, body, eye - side, feet);
		right = SideRay(world, body, eye + side, feet);
	}

	protected static bool SideRay(
		BaseWorld world,
		IEntity body,
		vector eye,
		vector feet)
	{
		if (SightClear(world, body, eye, feet + Vector(0, 1.2, 0)))
			return true;

		return SightClear(world, body, eye, feet + Vector(0, 1.7, 0));
	}

	protected static bool SideStillClear(IEntity body, IEntity enemy, float lean)
	{
		if (!body || !enemy || lean == 0)
			return false;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			world = body.GetWorld();

		if (!world)
			return false;

		vector transform[4];
		body.GetWorldTransform(transform);
		vector eye = body.GetOrigin() + Vector(0, 1.6, 0);
		vector side = transform[0] * LEAN_OFFSET;
		if (lean < 0)
			eye = eye - side;
		else
			eye = eye + side;

		return SideRay(world, body, eye, enemy.GetOrigin());
	}

	protected static bool AimReady(IEntity body, IEntity enemy, bool canShoot)
	{
		if (!canShoot || !enemy)
			return false;

		float now = 0;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			now = world.GetWorldTime();

		if (!s_AimTarget.Contains(body) || s_AimTarget.Get(body) != enemy)
		{
			s_AimTarget.Set(body, enemy);
			s_AimSince.Set(body, now);
		}

		return now - s_AimSince.Get(body) >= ShotDelaySeconds() * 1000;
	}

	protected static void ClearAim(IEntity body)
	{
		if (!body)
			return;

		s_AimTarget.Remove(body);
		s_AimSince.Remove(body);
	}

	protected static void SetLean(IEntity body, float lean)
	{
		CharacterControllerComponent controller = Controller(body);
		if (!controller)
			return;

		s_LeanHeld.Insert(body);
		controller.SetWantedLeaning(lean);
	}

	protected static void ReleaseLean(IEntity body)
	{
		if (!body || !s_LeanHeld.Contains(body))
			return;

		s_LeanHeld.RemoveItem(body);
		CharacterControllerComponent controller = Controller(body);
		if (controller)
			controller.SetWantedLeaning(0);
	}

	static bool SeesTarget(IEntity soldier, BaseTarget target)
	{
		return CanSeeTarget(CharacterBody(soldier), target);
	}

	protected static bool CanSeeTarget(IEntity body, BaseTarget target)
	{
		if (!body || !target)
			return false;

		IEntity enemy = target.GetTargetEntity();
		if (enemy)
			return CanSeeEntity(body, enemy);

		return CanSeePoint(body, target.GetLastSeenPosition());
	}

	protected static bool CanSeeEntity(IEntity body, IEntity enemy)
	{
		if (!enemy)
			return false;

		return CanSeePoint(body, enemy.GetOrigin());
	}

	protected static bool CanSeePoint(IEntity body, vector feet)
	{
		if (!body)
			return false;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			world = body.GetWorld();

		if (!world)
			return false;

		vector eye = body.GetOrigin() + Vector(0, 1.6, 0);
		if (vector.Distance(eye, feet) < 0.4)
			return true;

		// Chest, then head. A frame can clip one of them.
		if (SightClear(world, body, eye, feet + Vector(0, 1.2, 0)))
			return true;

		return SightClear(world, body, eye, feet + Vector(0, 1.7, 0));
	}

	protected static bool SightClear(
		BaseWorld world,
		IEntity body,
		vector eye,
		vector aim)
	{
		vector toAim = aim - eye;
		float distance = toAim.Length();
		if (distance < 0.05)
			return true;

		toAim = toAim * (1 / distance);
		vector start = eye + (toAim * 0.4);

		if (!s_SightTrace)
			s_SightTrace = new TraceParam();

		s_SightViewer = body;
		s_SightTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		s_SightTrace.Exclude = body;
		s_SightTrace.TraceEnt = null;
		s_SightTrace.Start = start;
		s_SightTrace.End = aim;

		float fraction = world.TraceMove(s_SightTrace, FilterMoveSight);
		s_SightViewer = null;

		// A wall stops the ray well short. A graze on the frame near the
		// target still counts as a clear shot.
		return fraction >= 0.9;
	}

	protected static bool FilterMoveSight(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		if (!entity || entity == s_SightViewer)
			return false;

		IEntity current = entity;
		int depth;
		while (current && depth < 8)
		{
			if (current == s_SightViewer)
				return false;

			if (ChimeraCharacter.Cast(current))
				return false;

			current = current.GetParent();
			depth++;
		}

		return true;
	}

	protected static void SetWeaponRaised(IEntity body, bool raised)
	{
		CharacterControllerComponent controller = Controller(body);
		if (controller)
			controller.SetWeaponRaised(raised);
	}

	protected static void SetFireWanted(IEntity body, bool wanted)
	{
		CharacterControllerComponent controller = Controller(body);
		if (controller)
			controller.SetFireWeaponWanted(wanted);
	}

	protected static CharacterControllerComponent Controller(IEntity body)
	{
		if (!body)
			return null;

		return CharacterControllerComponent.Cast(
			body.FindComponent(CharacterControllerComponent)
		);
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
		request.m_fMoveDuration_s = 3;
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

		// A held post, a doorway, or a burst on the way in stands and shoots.
		// A bound keeps the attack, and combat movement is forced on so the
		// run toward the next step actually starts.
		bool pinned =
			KK_GarrisonHold.IsPinned(character) ||
			KK_GarrisonHold.IsPinned(agentEntity);
		bool doorFiring =
			KK_GarrisonHold.IsDoorFiring(character) ||
			KK_GarrisonHold.IsDoorFiring(agentEntity);

		vector approachGoal;
		bool steering =
			KK_GarrisonHold.GetApproachGoal(character, approachGoal) ||
			KK_GarrisonHold.GetApproachGoal(agentEntity, approachGoal);

		if (pinned || doorFiring)
		{
			m_bUseCombatMove = false;
			if (KK_GarrisonHold.IsPinned(character))
				KK_GarrisonHold.SetPinned(character, true);
			if (KK_GarrisonHold.IsPinned(agentEntity))
				KK_GarrisonHold.SetPinned(agentEntity, true);
		}
		else if (steering)
		{
			m_bUseCombatMove = true;
		}

		return score;
	}

	override void InitWaitTime(SCR_AIUtilityComponent utility)
	{
		if (utility && KK_GarrisonHold.OwnsShot(utility.m_OwnerEntity))
		{
			m_fWaitTime.m_Value = KK_GarrisonHold.ShotDelaySeconds();
			return;
		}

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
			m_CombatComponent.KK_ClearTarget();

		SCR_AIBehaviorBase result;
		if (ignore)
			result = super.EvaluateBehavior(null);
		else
			result = super.EvaluateBehavior(unknownTarget);

		if (ignore)
			KK_GarrisonHold.EnforceSprintIgnore(this);
		else if (KK_GarrisonHold.OwnsShot(m_OwnerEntity) || KK_GarrisonHold.OwnsShot(GetOwner()))
			KK_GarrisonHold.ApplyRoomShot(this);
		else if (
			KK_GarrisonHold.IsMoveFire(m_OwnerEntity) ||
			KK_GarrisonHold.IsMoveFire(GetOwner()) ||
			KK_GarrisonHold.IsRoomFire(m_OwnerEntity) ||
			KK_GarrisonHold.IsRoomFire(GetOwner())
		)
			KK_GarrisonHold.ApplyMoveFire(this);

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

		if (KK_GarrisonHold.OwnsShot(body) || KK_GarrisonHold.OwnsShot(owner))
		{
			SCR_ChimeraAIAgent roomSoldier = SCR_ChimeraAIAgent.Cast(owner);
			if (roomSoldier)
				KK_GarrisonHold.ApplyRoomShot(roomSoldier.m_UtilityComponent);

			return ENodeResult.SUCCESS;
		}

		if (
			KK_GarrisonHold.IsMoveFire(body) ||
			KK_GarrisonHold.IsMoveFire(owner) ||
			KK_GarrisonHold.IsRoomFire(body) ||
			KK_GarrisonHold.IsRoomFire(owner)
		)
		{
			SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(owner);
			if (soldier)
				KK_GarrisonHold.ApplyMoveFire(soldier.m_UtilityComponent);

			// The move order would lower the gun, then the next target
			// refresh would raise it again. This node owns it instead.
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

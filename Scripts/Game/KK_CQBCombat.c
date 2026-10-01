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

		BaseWeaponComponent previousWeapon = m_SelectedWeaponComp;
		int previousMuzzle = m_iSelectedMuzzle;

		super.EvaluateWeaponAndTarget(
			outWeaponEvent,
			outSelectedTargetChanged,
			outPrevTarget,
			outCurrentTarget,
			outRetreatTargetChanged,
			outCompartmentChanged
		);

		// A reload already in the gun shows up as a new magazine. That catch-up
		// is not another swap, or he would start the animation twice.
		if (
			outWeaponEvent &&
			m_SelectedWeaponComp == previousWeapon &&
			m_iSelectedMuzzle == previousMuzzle &&
			KK_SelectedMagazineIsLoaded()
		)
		{
			outWeaponEvent = false;
		}

		if (KK_GarrisonHold.ShouldKeepRifle(GetOwner()) && KK_KeepPrimary())
		{
			// Publishing the frag would equip it. The empty rifle is then
			// forced back, and the two swaps never finish a reload.
			outWeaponEvent = false;
		}

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

	// A held trigger cooks a frag. Room combat never releases it, so the
	// selector is put back on the primary weapon and the hands follow.
	protected bool KK_KeepPrimary()
	{
		if (
			!m_SelectedWeaponComp ||
			!KK_GarrisonHold.IsThrowableType(m_SelectedWeaponComp.GetWeaponType())
		)
		{
			return false;
		}

		BaseWeaponComponent primary = KK_GarrisonHold.PrimaryWeapon(GetOwner());
		if (!primary)
			return false;

		m_SelectedWeaponComp = primary;
		m_iSelectedMuzzle = 0;
		m_SelectedMagazineComp = null;
		m_fSelectedWeaponMinDist = 0;
		m_fSelectedWeaponMaxDist = 800;
		m_bSelectedWeaponDirectDamage = true;

		EMuzzleType muzzleType = EMuzzleType.MT_BaseMuzzle;
		array<BaseMuzzleComponent> muzzles = {};
		primary.GetMuzzlesList(muzzles);
		if (muzzles.Count() > 0 && muzzles[0])
		{
			m_SelectedMagazineComp = muzzles[0].GetMagazine();
			muzzleType = muzzles[0].GetMuzzleType();
		}

		if (m_ConfigComponent)
		{
			m_SelectedWeaponResource = m_ConfigComponent.GetTreeNameForWeaponType(
				primary.GetWeaponType(),
				muzzleType
			);
		}

		KK_GarrisonHold.ReturnToPrimary(GetOwner());
		return true;
	}

	protected bool KK_SelectedMagazineIsLoaded()
	{
		if (!m_SelectedMagazineComp || !m_SelectedWeaponComp)
			return false;

		array<BaseMuzzleComponent> muzzles = {};
		m_SelectedWeaponComp.GetMuzzlesList(muzzles);
		if (m_iSelectedMuzzle < 0 || m_iSelectedMuzzle >= muzzles.Count())
			return false;

		BaseMuzzleComponent muzzle = muzzles[m_iSelectedMuzzle];
		return muzzle && muzzle.GetMagazine() == m_SelectedMagazineComp;
	}
}

class KK_GarrisonHold
{
	protected static ref set<IEntity> s_Pinned = new set<IEntity>();
	// Unlocked while unconscious. The speed override does not stick until he is up.
	protected static ref array<IEntity> s_SpeedRelease = {};
	protected static bool s_bSpeedReleaseTicking;
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
	protected static const float LEAN_OFFSET = 0.4;
	// Origin is the feet. A look at that point from arm's length points the rifle down.
	protected static const float SHOT_AIM_HEIGHT = 1.5;
	protected static ref map<IEntity, float> s_ShotLean = new map<IEntity, float>();
	protected static ref map<IEntity, IEntity> s_AimTarget = new map<IEntity, IEntity>();
	protected static ref map<IEntity, float> s_AimSince = new map<IEntity, float>();
	protected static ref set<IEntity> s_LeanHeld = new set<IEntity>();
	protected static ref map<IEntity, float> s_WantedLean = new map<IEntity, float>();
	protected static ref map<IEntity, IEntity> s_LookEntity = new map<IEntity, IEntity>();
	protected static bool s_bLeanPumping;
	protected static ref map<IEntity, float> s_PrimarySwitchAt =
		new map<IEntity, float>();
	protected static const float PRIMARY_SWITCH_RETRY_MS = 1000;
	protected static ref map<IEntity, float> s_ReloadAt =
		new map<IEntity, float>();
	protected static ref set<IEntity> s_ToppingOff = new set<IEntity>();
	protected static ref set<IEntity> s_EmptyReload = new set<IEntity>();
	protected static ref map<IEntity, float> s_SeenEnemyAt =
		new map<IEntity, float>();
	protected static ref map<IEntity, IEntity> s_PressureFrom =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, vector> s_PressureAt =
		new map<IEntity, vector>();
	protected static ref map<IEntity, vector> s_ReloadDash =
		new map<IEntity, vector>();
	protected static ref map<IEntity, vector> s_ReloadThreat =
		new map<IEntity, vector>();
	protected static ref map<IEntity, float> s_ReloadDashAt =
		new map<IEntity, float>();
	protected static ref set<IEntity> s_ReloadCover = new set<IEntity>();
	protected static ref set<IEntity> s_SprintNode = new set<IEntity>();
	protected static ref map<IEntity, float> s_SprintDist =
		new map<IEntity, float>();
	protected static ref map<IEntity, float> s_SprintDistAt =
		new map<IEntity, float>();
	protected static ref set<IEntity> s_ReloadBash = new set<IEntity>();
	protected static ref map<IEntity, IEntity> s_BashTarget =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, string> s_ReloadNote =
		new map<IEntity, string>();
	protected static ref map<IEntity, int> s_CoverChecked =
		new map<IEntity, int>();
	protected static ref map<IEntity, int> s_CoverHidden =
		new map<IEntity, int>();
	protected static const float RELOAD_RETRY_MS = 1500;
	protected static const float TOPOFF_START_MS = 750;
	// An empty reload outlasts both of those. IsReloading() drops between
	// stages, and a second order in that gap plays the animation again.
	protected static const float EMPTY_RELOAD_MS = 4500;
	protected static const float RELOAD_COVER_RADIUS = 0.8;
	protected static const float RELOAD_NODE_RADIUS = 0.2;
	protected static const float RELOAD_NODE_THERE = 1.5;
	protected static const float RELOAD_NODE_ARRIVE = 3;
	// Center to center. He only swings when he has run into the other body.
	// A couple of meters is still a lunge, and he should keep going to cover.
	protected static const float RELOAD_BASH_RANGE = 1.25;
	protected static const float RELOAD_BASH_KEEP = 1.6;
	protected static const float RELOAD_BASH_WALK = 1.6;
	protected static const float RELOAD_BREAK_RADIUS = 2;
	protected static const float RELOAD_DASH_MS = 8000;
	protected static const float RELOAD_THREAT_MOVE = 4;
	protected static const float RELOAD_MATE_BESIDE = 2.5;
	protected static const float RELOAD_MATE_RANGE = 12;
	protected static const string PRIMARY_SLOT = "primary";

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

	// Unconscious, still alive. Dead is handled by the caller that drops the hold.
	static bool IsDown(IEntity soldier)
	{
		CharacterControllerComponent controller = Controller(soldier);
		if (!controller || controller.IsDead())
			return false;

		if (controller.IsUnconscious())
			return true;

		return controller.GetLifeState() == ECharacterLifeState.INCAPACITATED;
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

		IEntity dryBody = utility.m_OwnerEntity;
		if (!dryBody)
		{
			AIAgent dryAgent = AIAgent.Cast(utility.GetOwner());
			if (dryAgent)
				dryBody = dryAgent.GetControlledEntity();
		}

		// An empty gun does not cancel the move to stand and shoot.
		if (CannotShoot(dryBody))
		{
			CommandWeapon(dryBody, false, false);
			return;
		}

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
			AimLook(utility, body, lookAt);
			return;
		}

		BaseTarget shot = ShotTarget(body);
		if (shot)
			LookAtAim(utility, ShotAimPoint(shot.GetLastSeenPosition()), 3);
	}

	static bool CombatOwnsWeapon(IEntity soldier)
	{
		return OwnsShot(soldier) ||
			IsMoveFire(soldier) ||
			IsRoomFire(soldier);
	}

	// Room combat, the cover sprint, and a bash stay on the rifle. A frag
	// selected for an empty gun would swap forever with the primary.
	static bool ShouldKeepRifle(IEntity soldier)
	{
		if (CombatOwnsWeapon(soldier))
			return true;

		if (SprintBeforeReload(soldier) || IsReloadBashing(soldier))
			return true;

		IEntity body = CharacterBody(soldier);
		return body && s_SprintNode.Contains(body);
	}

	static bool IsThrowableType(EWeaponType type)
	{
		return type == EWeaponType.WT_FRAGGRENADE ||
			type == EWeaponType.WT_SMOKEGRENADE;
	}

	static bool HoldingThrowable(IEntity soldier)
	{
		BaseWeaponComponent current = CurrentWeapon(soldier);
		return current && IsThrowableType(current.GetWeaponType());
	}

	// The slot named primary, or the first gun if that slot is empty.
	static BaseWeaponComponent PrimaryWeapon(IEntity soldier)
	{
		BaseWeaponManagerComponent manager = WeaponManager(soldier);
		if (!manager)
			return null;

		array<WeaponSlotComponent> slots = {};
		manager.GetWeaponsSlots(slots);

		BaseWeaponComponent fallback;
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot)
				continue;

			BaseWeaponComponent weapon = WeaponInSlot(slot);
			if (!weapon || IsThrowableType(weapon.GetWeaponType()))
				continue;

			if (slot.GetWeaponSlotType() == PRIMARY_SLOT)
				return weapon;

			if (!fallback)
				fallback = weapon;
		}

		return fallback;
	}

	// One request at a time. A repeat while the swap is playing restarts it.
	static void ReturnToPrimary(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (!HoldingThrowable(body))
		{
			s_PrimarySwitchAt.Remove(body);
			return;
		}

		CharacterControllerComponent controller = Controller(body);
		if (!controller || controller.IsChangingItem())
			return;

		float now = 0;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			now = world.GetWorldTime();

		if (
			s_PrimarySwitchAt.Contains(body) &&
			now - s_PrimarySwitchAt.Get(body) < PRIMARY_SWITCH_RETRY_MS
		)
		{
			return;
		}

		BaseWeaponComponent primary = PrimaryWeapon(body);
		if (!primary)
			return;

		s_PrimarySwitchAt.Set(body, now);
		controller.SetFireWeaponWanted(false);
		SCR_AIWeaponHandling.StartWeaponSwitchCharacter(controller, primary);
	}

	protected static BaseWeaponComponent CurrentWeapon(IEntity soldier)
	{
		BaseWeaponManagerComponent manager = WeaponManager(soldier);
		if (!manager)
			return null;

		return manager.GetCurrentWeapon();
	}

	protected static BaseWeaponManagerComponent WeaponManager(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return null;

		return BaseWeaponManagerComponent.Cast(
			body.FindComponent(BaseWeaponManagerComponent)
		);
	}

	protected static BaseWeaponComponent WeaponInSlot(WeaponSlotComponent slot)
	{
		if (!slot)
			return null;

		IEntity weaponEntity = slot.GetWeaponEntity();
		if (!weaponEntity)
			return null;

		return BaseWeaponComponent.Cast(
			weaponEntity.FindComponent(BaseWeaponComponent)
		);
	}

	// Swap a partial magazine for a fuller one only after the enemy has stayed
	// out of sight and the loaded mag is low. An empty gun reloads anyway.
	// The sprint-to-cover experiment holds that empty reload until he arrives.
	// Otherwise the reload starts now, and he runs while it plays.
	static void ConsiderTopOff(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_Buildings.Contains(body))
			return;

		bool dry = GunIsDry(body);
		NoteEnemySight(body);
		if (IsIgnoringTargets(body) || s_ReloadBash.Contains(body))
			return;

		// Experimental. The target is still in sight and the gun is empty, so
		// the sprint finishes before the magazine change starts.
		if (SprintBeforeReload(body))
			return;

		// A partial magazine waits until he has been off the enemy for a
		// while, and until the mag is actually low. An empty gun does not.
		if (!dry && !EarlyReloadAllowed(body))
			return;

		CharacterControllerComponent controller = Controller(body);
		if (
			!controller ||
			controller.IsDead() ||
			controller.IsUnconscious() ||
			controller.IsChangingItem() ||
			controller.IsMeleeAttack() ||
			controller.IsUsingItem()
		)
		{
			return;
		}

		// Arrival leaves the sprint flag up. That must not add another wait
		// once he is already planted on the node.
		if (controller.IsSprinting() && !s_ReloadCover.Contains(body))
			return;

		if (controller.IsReloading())
		{
			if (dry)
				s_EmptyReload.Insert(body);

			return;
		}

		// The order already went out. Another one while the gun is still
		// empty restarts the magazine change.
		if (IsEmptyReload(body))
			return;

		if (HoldingThrowable(body))
			return;

		float now = WorldTime();

		if (
			s_ReloadAt.Contains(body) &&
			now - s_ReloadAt.Get(body) < RELOAD_RETRY_MS
		)
		{
			return;
		}

		IEntity spare = FullerMagazine(body);
		if (!spare)
		{
			s_ReloadAt.Set(body, now);
			return;
		}

		BaseMagazineComponent loaded = LoadedMagazine(body);
		bool forceDetach = loaded && loaded.GetAmmoCount() > 0;
		controller.SetFireWeaponWanted(false);
		s_MoveFiring.RemoveItem(body);
		if (controller.ReloadWeaponWith(spare, forceDetach))
		{
			s_ReloadAt.Set(body, now);
			LogReload(body, "start-reload");
			if (dry)
				s_EmptyReload.Insert(body);
			else
				s_ToppingOff.Insert(body);
		}
	}

	// True while a clear or garrison reload should be left to finish.
	// A loaded gun drops the reload when contact starts, unless he is
	// already reloading under pressure and running to another cluster.
	static bool IsQuietReload(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		if (IsEmptyReload(body))
			return true;

		bool started = s_ToppingOff.Contains(body);
		if (!started && !s_Buildings.Contains(body))
			return false;

		if (ReloadInterrupted(body))
		{
			bool relocating =
				started &&
				ReloadCoverEnabled() &&
				SeesPressure(body) &&
				!s_ReloadCover.Contains(body);
			if (!relocating)
			{
				if (started)
					s_ToppingOff.RemoveItem(body);

				return false;
			}
		}

		CharacterControllerComponent controller = Controller(body);
		if (controller && controller.IsReloading())
			return true;

		if (!started)
			return false;

		float now = 0;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			now = world.GetWorldTime();

		if (
			s_ReloadAt.Contains(body) &&
			now - s_ReloadAt.Get(body) < TOPOFF_START_MS
		)
		{
			return true;
		}

		s_ToppingOff.RemoveItem(body);
		return false;
	}

	// The building shot keeps the trigger down. That has to stay off until a
	// dry gun has a magazine again, or the reload never starts. The flag
	// also has to outlast the animation: IsReloading() is false between
	// stages, and the gun is still empty then.
	protected static bool IsEmptyReload(IEntity body)
	{
		if (!body || !s_EmptyReload.Contains(body))
			return false;

		CharacterControllerComponent controller = Controller(body);
		if (controller && controller.IsReloading())
			return true;

		if (!GunIsDry(body))
		{
			s_EmptyReload.RemoveItem(body);
			return false;
		}

		if (
			s_ReloadAt.Contains(body) &&
			WorldTime() - s_ReloadAt.Get(body) < EMPTY_RELOAD_MS
		)
		{
			return true;
		}

		s_EmptyReload.RemoveItem(body);
		return false;
	}

	protected static bool GunIsDry(IEntity body)
	{
		BaseMuzzleComponent muzzle = PrimaryMuzzle(body);
		if (!muzzle || muzzle.GetAmmoCount() > 0)
			return false;

		BaseMagazineComponent loaded = muzzle.GetMagazine();
		return !loaded || loaded.GetAmmoCount() <= 0;
	}

	// No round left to fire, or the empty gun is still being reloaded.
	// He should move, wait, or reload instead of planting on the enemy.
	static bool CannotShoot(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		return GunIsDry(body) || IsEmptyReload(body);
	}

	protected static bool ReloadInterrupted(IEntity body)
	{
		if (
			s_ShotLive.Contains(body) ||
			s_MoveFiring.Contains(body) ||
			s_RoomFire.Contains(body)
		)
		{
			return true;
		}

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_ThreatSystem)
			return false;

		return utility.m_ThreatSystem.GetState() == EAIThreatState.THREATENED;
	}

	protected static bool HasVisibleTarget(IEntity body)
	{
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatComponent)
			return false;

		BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
		return target && CanSeeTarget(body, target);
	}

	// A live shot or a target he can still see. Losing that starts the wait.
	protected static bool SeesEnemy(IEntity body)
	{
		if (!body)
			return false;

		if (s_ShotLive.Contains(body))
			return true;

		return HasVisibleTarget(body);
	}

	protected static void NoteEnemySight(IEntity body)
	{
		if (!SeesEnemy(body))
			return;

		s_SeenEnemyAt.Set(body, WorldTime());
		RememberPressure(body);
	}

	// He tops off only after the enemy has stayed out of sight, the mag is
	// under the limit, and nobody is shooting him.
	protected static bool EarlyReloadAllowed(IEntity body)
	{
		if (SeesEnemy(body) || ReloadInterrupted(body))
			return false;

		if (!MagazineLow(body))
			return false;

		if (!s_SeenEnemyAt.Contains(body))
			return true;

		return WorldTime() - s_SeenEnemyAt.Get(body) >= OutOfSightMs();
	}

	protected static float OutOfSightMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		float seconds = 8;
		if (mode)
			seconds = mode.KK_GetOutOfSight();

		return Math.Max(seconds, 0) * 1000;
	}

	// Pressured, and a reload is due or already playing. He keeps moving
	// until sight breaks or a squadmate is reached. The sprint experiment
	// sends an empty gun to a hidden node before the reload starts. Once
	// that reload is playing, a visible threat still breaks sight.
	static bool MustDashToReload(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !ReloadCoverEnabled())
			return false;

		if (s_ReloadBash.Contains(body))
			return false;

		if (s_ReloadCover.Contains(body))
		{
			if (!BreakSightWhileReloading(body))
				return false;

			s_ReloadCover.RemoveItem(body);
		}

		// A hidden node is already chosen. He finishes that move, then reloads.
		if (CommittedSprintNode(body))
			return true;

		if (!SeesPressure(body))
			return false;

		CharacterControllerComponent controller = Controller(body);
		if (controller && controller.IsReloading())
			return true;

		if (IsQuietReload(body) || IsEmptyReload(body))
			return true;

		if (!GunIsDry(body))
			return false;

		return FullerMagazine(body) != null;
	}

	// The node only holds him until the magazine change starts. A line of
	// sight during that reload is the normal break-sight run.
	protected static bool BreakSightWhileReloading(IEntity body)
	{
		if (!body || !SeesPressure(body))
			return false;

		CharacterControllerComponent controller = Controller(body);
		if (controller && controller.IsReloading())
			return true;

		return IsEmptyReload(body);
	}

	// Experimental. Empty gun, the target he would still shoot is visible,
	// and the reload has not started. Once a node is chosen he stays on it
	// until he is standing there, then the reload starts.
	static bool SprintBeforeReload(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !ReloadSprintEnabled() || !ReloadCoverEnabled() || !GunIsDry(body))
			return false;

		if (s_ReloadBash.Contains(body))
			return false;

		if (IsQuietReload(body) || IsEmptyReload(body))
			return false;

		CharacterControllerComponent controller = Controller(body);
		if (controller && controller.IsReloading())
			return false;

		if (s_SprintNode.Contains(body))
			return true;

		if (!SeesEnemy(body) || !MustDashToReload(body))
			return false;

		return !controller || !controller.IsReloading();
	}

	protected static bool CommittedSprintNode(IEntity body)
	{
		if (!body || !s_SprintNode.Contains(body) || !GunIsDry(body))
			return false;

		if (IsQuietReload(body) || IsEmptyReload(body))
			return false;

		CharacterControllerComponent controller = Controller(body);
		return !controller || !controller.IsReloading();
	}

	// The whole approach is a sprint. A walk or jog at the end, or a look
	// back at the threat, turns it into a strafe and the reload never starts.
	static EMovementType ReloadMoveSpeed(IEntity soldier)
	{
		if (SprintBeforeReload(soldier))
			return EMovementType.SPRINT;

		return EMovementType.RUN;
	}

	// Beats attack, sidestep, and the melee shove. The node is the only move.
	static float ReloadMovePriority(IEntity soldier)
	{
		if (SprintBeforeReload(soldier))
			return KK_AgentMove.EnterBuildingPriorityLevel();

		return KK_AgentMove.PRIORITY_LEVEL;
	}

	// Far away, leave the path alone. Once he has stopped short of the node,
	// send the order again so the move does not finish early.
	static bool ReloadOrderDue(IEntity soldier, float sinceOrderMs)
	{
		if (sinceOrderMs >= KK_AgentMove.REISSUE_INTERVAL_MS)
			return true;

		if (!SprintBeforeReload(soldier))
			return false;

		IEntity body = CharacterBody(soldier);
		if (!body || !s_ReloadDash.Contains(body))
			return false;

		float dist = vector.Distance(body.GetOrigin(), s_ReloadDash.Get(body));
		if (dist <= RELOAD_NODE_RADIUS)
			return false;

		Physics physics = body.GetPhysics();
		if (!physics)
			return true;

		vector velocity = physics.GetVelocity();
		velocity[1] = 0;
		return velocity.Length() < 0.35;
	}

	static bool IsReloadBashing(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_ReloadBash.Contains(body);
	}

	// He ran into someone on the way to the node. The reload is dropped.
	// moveTo is that enemy, and he keeps swinging while they stay in reach.
	static bool DriveReloadBash(
		AIAgent agent,
		IEntity soldier,
		out vector moveTo,
		out EMovementType speed)
	{
		moveTo = vector.Zero;
		speed = EMovementType.RUN;
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		bool bashing = s_ReloadBash.Contains(body);
		if (!bashing && !SprintBeforeReload(body))
			return false;

		float reach = RELOAD_BASH_RANGE;
		if (bashing)
			reach = RELOAD_BASH_KEEP;

		IEntity enemy = BashTarget(body, reach);
		if (!enemy)
		{
			if (bashing)
				EndReloadBash(body);

			return false;
		}

		if (!bashing)
		{
			ClearReloadDash(body);
			s_ToppingOff.RemoveItem(body);
			s_EmptyReload.RemoveItem(body);
			s_ReloadCover.RemoveItem(body);
		}

		s_ReloadBash.Insert(body);
		s_BashTarget.Set(body, enemy);
		CommandReloadBash(agent, body, enemy, true);
		moveTo = enemy.GetOrigin();
		if (vector.Distance(body.GetOrigin(), moveTo) <= RELOAD_BASH_WALK)
			speed = EMovementType.WALK;

		return true;
	}

	protected static void EndReloadBash(IEntity body)
	{
		if (!body)
			return;

		s_ReloadBash.RemoveItem(body);
		s_BashTarget.Remove(body);
		CommandReloadBash(null, body, null, false);
	}

	protected static void CommandReloadBash(
		AIAgent agent,
		IEntity body,
		IEntity enemy,
		bool attack)
	{

		CharacterControllerComponent controller = Controller(body);
		if (controller)
		{
			controller.SetFireWeaponWanted(false);
			if (!attack)
				controller.SetMeleeAttack(false);
			else if (!controller.IsMeleeAttack())
				controller.SetMeleeAttack(true);
		}

		if (attack)
			CancelCombatMove(body);

		if (!attack || !agent || !enemy)
			return;

		SetWeaponRaised(body, false);
		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (!soldier || !soldier.m_UtilityComponent || !soldier.m_UtilityComponent.m_LookAction)
			return;

		soldier.m_UtilityComponent.m_LookAction.LookAt(enemy, 100, 5);
	}

	protected static IEntity BashTarget(IEntity body, float reach)
	{
		IEntity best = null;
		if (s_BashTarget.Contains(body))
			best = NearerBash(body, s_BashTarget.Get(body), best, reach);

		if (s_PressureFrom.Contains(body))
			best = NearerBash(body, s_PressureFrom.Get(body), best, reach);

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (utility && utility.m_CombatComponent)
		{
			BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
			if (target)
				best = NearerBash(body, target.GetTargetEntity(), best, reach);
		}

		PerceptionComponent perception = PerceptionComponent.Cast(
			body.FindComponent(PerceptionComponent)
		);
		if (!perception)
			return best;

		array<BaseTarget> perceived = {};
		perception.GetTargetsList(perceived, ETargetCategory.ENEMY);
		foreach (BaseTarget candidate : perceived)
		{
			if (!candidate)
				continue;

			best = NearerBash(body, candidate.GetTargetEntity(), best, reach);
		}

		return best;
	}

	protected static IEntity NearerBash(
		IEntity body,
		IEntity enemy,
		IEntity best,
		float reach)
	{
		if (!CanBash(body, enemy))
			return best;

		float dist = vector.Distance(body.GetOrigin(), enemy.GetOrigin());
		if (dist > reach)
			return best;

		if (!best)
			return enemy;

		float bestDist = vector.Distance(body.GetOrigin(), best.GetOrigin());
		if (dist < bestDist)
			return enemy;

		return best;
	}

	protected static bool CanBash(IEntity body, IEntity enemy)
	{
		if (!body || !enemy || enemy == body)
			return false;

		if (!IsHostile(body, enemy) || !IsLiving(enemy))
			return false;

		CharacterControllerComponent controller = Controller(enemy);
		return !controller || !controller.IsUnconscious();
	}

	// A raised weapon will not sprint. Drop it and keep the move order from
	// putting it back up while he is still short of cover.
	static void LowerForReloadSprint(AIAgent agent)
	{
		if (!agent)
			return;

		IEntity body = agent.GetControlledEntity();
		if (!body)
			return;

		SetWeaponRaised(body, false);
		s_MoveWeaponKnown.Insert(body);
		s_MoveWeaponUp.RemoveItem(body);
		CancelCombatMove(body);
		ReleaseLean(body);

		SCR_ChimeraAIAgent soldier = SCR_ChimeraAIAgent.Cast(agent);
		if (soldier && soldier.m_UtilityComponent)
			CancelLook(soldier.m_UtilityComponent, body);
		if (!soldier || !soldier.m_UtilityComponent)
			return;

		SCR_AIOrder_WeaponRaised order = new SCR_AIOrder_WeaponRaised();
		order.m_bWeaponRaised = false;
		order.SetReceiver(agent);
		soldier.m_UtilityComponent.m_Mailbox.RequestBroadcast(order, agent);
	}

	// Out of sight. Garrison stays on the post. Clear waits here until the
	// reload is finished. holdingArea is the post, or any clear step.
	static bool ShouldHoldToReload(IEntity soldier, bool holdingArea)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_Buildings.Contains(body))
			return false;

		if (s_ReloadBash.Contains(body))
			return false;

		if (MustDashToReload(body))
			return false;

		CharacterControllerComponent controller = Controller(body);
		if (s_ReloadCover.Contains(body))
		{
			bool reloading =
				IsQuietReload(body) ||
				(controller && controller.IsReloading());
			if (reloading || (GunIsDry(body) && FullerMagazine(body)))
				return true;

			s_ReloadCover.RemoveItem(body);
		}

		if (IsQuietReload(body) || (controller && controller.IsReloading()))
			return true;

		if (SeesPressure(body))
			return false;

		if (GunIsDry(body))
			return FullerMagazine(body) != null;

		if (!holdingArea || !EarlyReloadAllowed(body))
			return false;

		return FullerMagazine(body) != null;
	}

	protected static bool ReloadCoverEnabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetReloadCover();
	}

	protected static bool ReloadSprintEnabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return false;

		return mode.KK_GetReloadSprint();
	}

	// One line when the reload decision changes, if debug log is on.
	static void LogReload(IEntity soldier, string choice)
	{
		if (!SCR_BaseGameMode.KK_LogEnabled())
			return;

		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		bool reloading = false;
		CharacterControllerComponent controller = Controller(body);
		if (controller)
			reloading = controller.IsReloading();

		if (choice == "skip-not-dry" && !reloading)
			return;

		bool dry = GunIsDry(body);
		int dryBit = 0;
		if (dry)
			dryBit = 1;
		int setting = 0;
		if (ReloadCoverEnabled())
			setting = 1;
		int sees = 0;
		if (SeesEnemy(body))
			sees = 1;
		int pressure = 0;
		if (SeesPressure(body))
			pressure = 1;
		int spare = 0;
		if (FullerMagazine(body))
			spare = 1;
		int reloadBit = 0;
		if (reloading)
			reloadBit = 1;

		int muzzleAmmo = -1;
		int magAmmo = -1;
		BaseMuzzleComponent muzzle = PrimaryMuzzle(body);
		if (muzzle)
		{
			muzzleAmmo = muzzle.GetAmmoCount();
			BaseMagazineComponent loaded = muzzle.GetMagazine();
			if (loaded)
				magAmmo = loaded.GetAmmoCount();
		}

		int checked = 0;
		int hidden = 0;
		if (s_CoverChecked.Contains(body))
			checked = s_CoverChecked.Get(body);
		if (s_CoverHidden.Contains(body))
			hidden = s_CoverHidden.Get(body);

		float dist = -1;
		if (s_ReloadDash.Contains(body))
			dist = vector.Distance(body.GetOrigin(), s_ReloadDash.Get(body));

		string note = string.Format(
			"%1 dry=%2 set=%3 sees=%4 pressure=%5 spare=%6",
			choice,
			dryBit,
			setting,
			sees,
			pressure,
			spare
		);
		note = note + string.Format(
			" reloading=%1 muzzle=%2 mag=%3 hidden=%4/%5 dist=%6",
			reloadBit,
			muzzleAmmo,
			magAmmo,
			hidden,
			checked,
			dist
		);

		if (s_ReloadNote.Contains(body) && s_ReloadNote.Get(body) == note)
			return;

		s_ReloadNote.Set(body, note);
		if (SCR_BaseGameMode.KK_LogEnabled())
			PrintFormat("KK reload %1 %2", body, note);
	}

	static string ReloadSkipReason(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !GunIsDry(body))
			return "skip-not-dry";

		if (!ReloadCoverEnabled())
			return "skip-setting-off";

		if (s_ReloadCover.Contains(body))
			return "skip-cover-flag";

		if (IsEmptyReload(body))
			return "skip-empty-reload";

		CharacterControllerComponent controller = Controller(body);
		if (controller && controller.IsReloading())
			return "skip-reloading";

		if (!SeesPressure(body))
			return "skip-no-sight";

		if (!FullerMagazine(body))
			return "skip-no-spare";

		return "would-dash";
	}

	static bool PausesRoute(IEntity soldier)
	{
		return MustDashToReload(soldier) ||
			ShouldHoldToReload(soldier, false);
	}

	static void SetReloadCover(IEntity soldier, bool allow)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (allow)
			s_ReloadCover.Insert(body);
		else
			s_ReloadCover.RemoveItem(body);
	}

	static void ClearReloadDash(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		s_ReloadDash.Remove(body);
		s_ReloadThreat.Remove(body);
		s_ReloadDashAt.Remove(body);
		s_SprintNode.RemoveItem(body);
		s_SprintDist.Remove(body);
		s_SprintDistAt.Remove(body);
	}

	// On the node, or stopped beside it. The reload starts now. A sprint
	// that never gets close still gives up on the dash clock.
	static bool SprintNodeStuck(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_SprintNode.Contains(body) || !s_ReloadDash.Contains(body))
			return false;

		float dist = vector.Distance(body.GetOrigin(), s_ReloadDash.Get(body));
		if (dist <= RELOAD_NODE_THERE)
			return true;

		bool stopped = true;
		Physics physics = body.GetPhysics();
		if (physics)
		{
			vector velocity = physics.GetVelocity();
			velocity[1] = 0;
			stopped = velocity.Length() < 0.35;
		}

		if (stopped && dist <= RELOAD_NODE_ARRIVE)
			return true;

		float now = WorldTime();
		bool improved =
			!s_SprintDist.Contains(body) ||
			dist < s_SprintDist.Get(body) - 0.25;
		if (improved)
		{
			s_SprintDist.Set(body, dist);
			s_SprintDistAt.Set(body, now);
			return false;
		}

		if (!s_SprintDistAt.Contains(body))
		{
			s_SprintDist.Set(body, dist);
			s_SprintDistAt.Set(body, now);
			return false;
		}

		return now - s_SprintDistAt.Get(body) >= RELOAD_DASH_MS;
	}

	static bool ReloadDashExpired(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_ReloadDashAt.Contains(body))
			return false;

		return WorldTime() - s_ReloadDashAt.Get(body) >= RELOAD_DASH_MS;
	}

	static bool AtReloadCover(IEntity soldier, vector position)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_ReloadDash.Contains(body))
			return false;

		return vector.Distance(position, s_ReloadDash.Get(body)) <=
			RELOAD_COVER_RADIUS;
	}

	// True while he should keep moving. He runs to the cluster node farthest
	// from the threat. The sprint experiment uses that node before the reload.
	static bool KeepReloadMove(
		IEntity soldier,
		array<ref KK_InteriorTarget> targets,
		vector position,
		out vector goal)
	{
		goal = vector.Zero;
		bool sprint = SprintBeforeReload(soldier);
		IEntity body = CharacterBody(soldier);
		if (!sprint && BesideSquadMate(body))
			return false;

		bool have;
		if (sprint)
			have = SelectReloadCover(soldier, targets, goal);
		else
			have = SelectReloadBreak(soldier, targets, goal);

		if (!have)
			return false;

		float radius = RELOAD_NODE_RADIUS;
		if (!sprint)
			radius = RELOAD_BREAK_RADIUS;

		if (vector.Distance(position, goal) > radius)
			return true;

		if (
			sprint ||
			!body ||
			!SeesPressure(body) ||
			BesideSquadMate(body)
		)
		{
			return false;
		}

		// Still in sight, and not with a squadmate. Step farther away.
		// The dash clock stays so he does not run forever.
		float started = 0;
		bool hadStart = body && s_ReloadDashAt.Contains(body);
		if (hadStart)
			started = s_ReloadDashAt.Get(body);

		ClearReloadDash(body);
		if (!SelectReloadBreak(body, targets, goal))
			return false;

		if (hadStart)
			s_ReloadDashAt.Set(body, started);

		return vector.Distance(position, goal) > radius;
	}

	// The hidden node farthest from the threat. A node he can still be seen
	// from is used when nothing is hidden. Once chosen, that node stays
	// until he is standing on it.
	static bool SelectReloadCover(
		IEntity soldier,
		array<ref KK_InteriorTarget> targets,
		out vector goal)
	{
		IEntity body = CharacterBody(soldier);
		goal = vector.Zero;
		if (!body)
			return false;

		if (s_SprintNode.Contains(body) && s_ReloadDash.Contains(body))
		{
			goal = s_ReloadDash.Get(body);
			return true;
		}

		if (!targets || targets.IsEmpty())
			return false;

		RememberPressure(body);
		vector threat;
		if (!s_PressureAt.Contains(body))
			return false;

		threat = s_PressureAt.Get(body);
		if (!FindReloadCover(body, targets, threat, goal))
		{
			ClearReloadDash(body);
			return false;
		}

		bool sameGoal =
			s_ReloadDash.Contains(body) &&
			vector.Distance(s_ReloadDash.Get(body), goal) < 0.5;
		s_ReloadDash.Set(body, goal);
		s_ReloadThreat.Set(body, threat);
		s_SprintNode.Insert(body);
		if (!sameGoal || !s_ReloadDashAt.Contains(body))
			s_ReloadDashAt.Set(body, WorldTime());

		return true;
	}

	protected static void RememberPressure(IEntity body)
	{
		if (!body)
			return;

		IEntity enemy = null;
		if (s_ShotLook.Contains(body))
			enemy = s_ShotLook.Get(body);

		if (!enemy)
		{
			SCR_AIUtilityComponent utility = UtilityOf(body);
			if (utility && utility.m_CombatComponent)
			{
				BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
				if (target)
					enemy = target.GetTargetEntity();

				if (!enemy && target)
					s_PressureAt.Set(body, target.GetLastSeenPosition());
			}
		}

		if (!enemy)
			return;

		s_PressureFrom.Set(body, enemy);
		s_PressureAt.Set(body, enemy.GetOrigin());
	}

	// In sight now, or the enemy he just broke from is still visible.
	protected static bool SeesPressure(IEntity body)
	{
		if (!body)
			return false;

		if (SeesEnemy(body))
		{
			RememberPressure(body);
			return true;
		}

		if (s_PressureFrom.Contains(body))
		{
			IEntity enemy = s_PressureFrom.Get(body);
			if (!enemy || !CanSeeEntity(body, enemy))
				return false;

			s_PressureAt.Set(body, enemy.GetOrigin());
			return true;
		}

		if (!s_PressureAt.Contains(body))
			return false;

		return CanSeePoint(body, s_PressureAt.Get(body));
	}

	// A living squadmate within reach, behind cover or farther from the
	// threat than he is. He runs to that soldier instead of a spot on the floor.
	protected static IEntity FindSupportMate(IEntity body, vector threat)
	{
		SCR_AIGroup group = GroupOf(body);
		if (!group)
			return null;

		BaseWorld world = null;
		if (GetGame())
			world = GetGame().GetWorld();

		if (!world)
			world = body.GetWorld();

		vector eye = threat + Vector(0, 1.6, 0);
		float soldierAway = vector.Distance(body.GetOrigin(), threat);
		IEntity bestHidden = null;
		float bestHiddenDist = 0;
		IEntity bestSafer = null;
		float bestSaferDist = 0;

		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			IEntity mate = agent.GetControlledEntity();
			if (!mate || mate == body || !MateAlive(mate))
				continue;

			float distance = vector.Distance(body.GetOrigin(), mate.GetOrigin());
			if (distance < RELOAD_MATE_BESIDE || distance > RELOAD_MATE_RANGE)
				continue;

			bool hidden =
				world &&
				!SightClear(world, body, eye, mate.GetOrigin() + Vector(0, 1.2, 0));
			if (hidden && (!bestHidden || distance < bestHiddenDist))
			{
				bestHidden = mate;
				bestHiddenDist = distance;
			}

			float mateAway = vector.Distance(mate.GetOrigin(), threat);
			if (
				mateAway > soldierAway + 1 &&
				(!bestSafer || distance < bestSaferDist)
			)
			{
				bestSafer = mate;
				bestSaferDist = distance;
			}
		}

		if (bestHidden)
			return bestHidden;

		return bestSafer;
	}

	protected static bool BesideSquadMate(IEntity body)
	{
		SCR_AIGroup group = GroupOf(body);
		if (!group)
			return false;

		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			IEntity mate = agent.GetControlledEntity();
			if (!mate || mate == body || !MateAlive(mate))
				continue;

			if (vector.Distance(body.GetOrigin(), mate.GetOrigin()) <= RELOAD_MATE_BESIDE)
				return true;
		}

		return false;
	}

	protected static bool MateAlive(IEntity mate)
	{
		CharacterControllerComponent controller = Controller(mate);
		return !controller ||
			(!controller.IsDead() && !controller.IsUnconscious());
	}

	// A cluster node as far from the threat as he can get. A squadmate or a
	// short step is the fallback when the building has no node to run to.
	protected static bool SelectReloadBreak(
		IEntity soldier,
		array<ref KK_InteriorTarget> targets,
		out vector goal)
	{
		IEntity body = CharacterBody(soldier);
		goal = vector.Zero;
		if (!body)
			return false;

		RememberPressure(body);
		if (!s_PressureAt.Contains(body))
			return false;

		vector threat = s_PressureAt.Get(body);
		if (
			s_ReloadDash.Contains(body) &&
			s_ReloadThreat.Contains(body) &&
			vector.Distance(s_ReloadThreat.Get(body), threat) < RELOAD_THREAT_MOVE
		)
		{
			goal = s_ReloadDash.Get(body);
			return true;
		}

		if (
			targets &&
			!targets.IsEmpty() &&
			FindReloadCover(body, targets, threat, goal)
		)
		{
			RememberReloadGoal(body, goal, threat);
			return true;
		}

		IEntity mate = FindSupportMate(body, threat);
		if (mate)
		{
			goal = mate.GetOrigin();
			RememberReloadGoal(body, goal, threat);
			return true;
		}

		if (!FindBreakPoint(body, threat, goal))
			return false;

		RememberReloadGoal(body, goal, threat);
		return true;
	}

	protected static void RememberReloadGoal(
		IEntity body,
		vector goal,
		vector threat)
	{
		s_ReloadDash.Set(body, goal);
		s_ReloadThreat.Set(body, threat);
		if (!s_ReloadDashAt.Contains(body))
			s_ReloadDashAt.Set(body, WorldTime());
	}

	protected static bool FindBreakPoint(
		IEntity body,
		vector threat,
		out vector goal)
	{
		BaseWorld world = null;
		if (GetGame())
			world = GetGame().GetWorld();

		if (!world)
			world = body.GetWorld();

		vector eye = threat + Vector(0, 1.6, 0);
		for (int d = 0; d < 3; d++)
		{
			float distance = 4;
			if (d == 1)
				distance = 7;
			else if (d == 2)
				distance = 11;

			for (int a = 0; a < 5; a++)
			{
				float radians = 0;
				if (a == 1)
					radians = 0.7;
				else if (a == 2)
					radians = -0.7;
				else if (a == 3)
					radians = 1.3;
				else if (a == 4)
					radians = -1.3;

				vector point = StepAway(
					body.GetOrigin(),
					threat,
					distance,
					radians
				);
				if (vector.Distance(body.GetOrigin(), point) < 1.5)
					continue;

				if (
					!world ||
					!SightClear(world, body, eye, point + Vector(0, 1.2, 0))
				)
				{
					goal = point;
					return true;
				}
			}
		}

		goal = StepAway(body.GetOrigin(), threat, 8, 0);
		return true;
	}

	protected static vector StepAway(
		vector origin,
		vector threat,
		float distance,
		float radians)
	{
		vector away = origin - threat;
		float x = away[0];
		float z = away[2];
		float len = Math.Sqrt(x * x + z * z);
		if (len < 0.25)
		{
			x = 1;
			z = 0;
			len = 1;
		}

		x = x / len;
		z = z / len;
		float c = Math.Cos(radians);
		float s = Math.Sin(radians);
		vector dir = Vector(x * c - z * s, 0, x * s + z * c);
		vector point = origin + (dir * distance);
		point[1] = origin[1];
		return point;
	}

	protected static SCR_AIGroup GroupOf(IEntity body)
	{
		if (!body)
			return null;

		AIControlComponent control = AIControlComponent.Cast(
			body.FindComponent(AIControlComponent)
		);

		if (!control)
			return null;

		AIAgent agent = control.GetAIAgent();
		if (!agent)
			return null;

		return SCR_AIGroup.Cast(agent.GetParentGroup());
	}

	protected static bool FindReloadCover(
		IEntity body,
		array<ref KK_InteriorTarget> targets,
		vector threat,
		out vector goal)
	{
		BaseWorld world = null;
		if (GetGame())
			world = GetGame().GetWorld();

		if (!world)
			world = body.GetWorld();

		if (!world)
			return false;

		vector eye = threat + Vector(0, 1.6, 0);
		bool foundHidden = false;
		float bestHiddenAway = -1;
		vector bestHiddenGoal = vector.Zero;
		bool foundAny = false;
		float bestAnyAway = -1;
		vector bestAnyGoal = vector.Zero;
		int checked = 0;
		int hidden = 0;

		// Every reachable node, including his own cluster. Hidden nodes come
		// first, and inside that set the one farthest from the threat wins.
		// The walk is allowed to cross the threat. That is still the spot.
		foreach (KK_InteriorTarget target : targets)
		{
			if (!target || target.m_eState == KK_EInteriorTargetState.UNREACHABLE)
				continue;

			vector node = target.m_vPosition;
			if (vector.Distance(body.GetOrigin(), node) < 0.75)
				continue;

			checked++;
			bool blocked = !SightClear(world, body, eye, node + Vector(0, 1.2, 0));
			if (blocked)
				hidden++;

			float away = vector.Distance(node, threat);
			if (!foundAny || away > bestAnyAway)
			{
				foundAny = true;
				bestAnyAway = away;
				bestAnyGoal = node;
			}

			if (!blocked)
				continue;

			if (foundHidden && away <= bestHiddenAway)
				continue;

			foundHidden = true;
			bestHiddenAway = away;
			bestHiddenGoal = node;
		}

		s_CoverChecked.Set(body, checked);
		s_CoverHidden.Set(body, hidden);

		if (foundHidden)
		{
			goal = bestHiddenGoal;
			return true;
		}

		if (!foundAny)
			return false;

		goal = bestAnyGoal;
		return true;
	}

	protected static bool MagazineLow(IEntity body)
	{
		float remainder = ReloadRemainder();
		if (remainder <= 0)
			return false;

		BaseMagazineComponent loaded = LoadedMagazine(body);
		if (!loaded)
			return true;

		int maxAmmo = loaded.GetMaxAmmoCount();
		if (maxAmmo <= 0)
			return false;

		int percent = (int)Math.Round(remainder * 100);
		return loaded.GetAmmoCount() * 100 < maxAmmo * percent;
	}

	protected static float ReloadRemainder()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 0.3;

		return mode.KK_GetReloadRemainder();
	}

	protected static float WorldTime()
	{
		if (!GetGame())
			return 0;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;

		return world.GetWorldTime();
	}

	protected static IEntity FullerMagazine(IEntity body)
	{
		BaseMuzzleComponent muzzle = PrimaryMuzzle(body);
		if (!muzzle)
			return null;

		BaseMagazineWell well = muzzle.GetMagazineWell();
		if (!well)
			return null;

		int loadedAmmo = 0;
		IEntity loadedEntity = null;
		BaseMagazineComponent loaded = muzzle.GetMagazine();
		if (loaded)
		{
			int maxAmmo = loaded.GetMaxAmmoCount();
			loadedAmmo = loaded.GetAmmoCount();
			if (maxAmmo <= 0 || loadedAmmo >= maxAmmo)
				return null;

			loadedEntity = loaded.GetOwner();
		}

		SCR_InventoryStorageManagerComponent inventory =
			SCR_InventoryStorageManagerComponent.Cast(
				body.FindComponent(SCR_InventoryStorageManagerComponent)
			);
		if (!inventory)
			return null;

		SCR_MagazinePredicate predicate = new SCR_MagazinePredicate();
		predicate.magWellType = well.Type();

		array<IEntity> found = {};
		inventory.FindItems(found, predicate, EStoragePurpose.PURPOSE_DEPOSIT);

		IEntity best = null;
		int bestAmmo = loadedAmmo;
		foreach (IEntity item : found)
		{
			if (!item || item == loadedEntity || !inventory.Contains(item))
				continue;

			BaseMagazineComponent magazine = BaseMagazineComponent.Cast(
				item.FindComponent(BaseMagazineComponent)
			);
			if (!magazine)
				continue;

			int ammo = magazine.GetAmmoCount();
			if (ammo <= bestAmmo)
				continue;

			bestAmmo = ammo;
			best = item;
		}

		return best;
	}

	protected static BaseMagazineComponent LoadedMagazine(IEntity body)
	{
		BaseMuzzleComponent muzzle = PrimaryMuzzle(body);
		if (!muzzle)
			return null;

		return muzzle.GetMagazine();
	}

	protected static BaseMuzzleComponent PrimaryMuzzle(IEntity body)
	{
		BaseWeaponComponent weapon = CurrentWeapon(body);
		if (!weapon || IsThrowableType(weapon.GetWeaponType()))
			weapon = PrimaryWeapon(body);

		if (!weapon || IsThrowableType(weapon.GetWeaponType()))
			return null;

		array<BaseMuzzleComponent> muzzles = {};
		weapon.GetMuzzlesList(muzzles);
		if (muzzles.Count() == 0 || !muzzles[0] || muzzles[0].IsDisposable())
			return null;

		return muzzles[0];
	}

	// Only command the weapon when the stance changes. Repeating the same
	// raise or lower restarts the animation.
	protected static void CommandWeapon(IEntity body, bool raised, bool fire)
	{
		if (!body)
			return;

		// The trigger is a cook on a frag. Get the rifle back before it.
		if (HoldingThrowable(body))
		{
			ReturnToPrimary(body);
			raised = false;
			fire = false;
		}

		// A sprint to a node keeps the rifle down. A raise will not sprint,
		// and it would start the reload before he is there.
		if (SprintBeforeReload(body) || s_ReloadBash.Contains(body))
		{
			raised = false;
			fire = false;
		}

		// The building shot would hold the trigger on an empty gun and the
		// reload would never leave the magazine well.
		bool dry = GunIsDry(body);
		if (dry || IsEmptyReload(body))
		{
			fire = false;
			if (dry)
				ConsiderTopOff(body);
		}

		// Another raise while the magazine is coming out restarts the reload.
		// A shot still goes through, so contact cancels a loaded gun's reload.
		bool known = s_MoveWeaponKnown.Contains(body);
		bool wasRaised = s_MoveWeaponUp.Contains(body);
		bool holdReload = !fire && IsQuietReload(body);
		if (!holdReload && (!known || wasRaised != raised))
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
		// later node clears it and the shot never starts. A dry gun stays
		// off the trigger for the whole reload.
		if (dry || IsEmptyReload(body))
			SetFireWanted(body, false);
		else if (fire || wasFiring)
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

	// An enemy inside this building. The shot owns his facing, even when
	// the threat state has dropped below alerted.
	static bool FightingInside(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_Buildings.Contains(body))
			return false;

		IEntity building = s_Buildings.Get(body);
		if (!building || !PositionInside(building, body.GetOrigin()))
			return false;

		if (s_ShotLive.Contains(body))
			return true;

		if (s_ShotLook.Contains(body))
		{
			IEntity shot = s_ShotLook.Get(body);
			if (shot && PositionInside(building, shot.GetOrigin()))
				return true;
		}

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatComponent)
			return false;

		BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
		if (!target)
			return false;

		IEntity enemy = target.GetTargetEntity();
		if (enemy)
			return PositionInside(building, enemy.GetOrigin());

		return PositionInside(building, target.GetLastSeenPosition());
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

		if (SprintBeforeReload(body))
		{
			RememberPressure(body);
			ClearShot(body);
			RememberLean(body, 0);
			ReleaseLean(body);
			CancelLook(utility, body);
			CommandWeapon(body, false, false);
			return;
		}

		if (!OwnsShot(body))
		{
			ReleaseLean(body);
			ReleaseLook(utility, body);
			return;
		}

		// An empty gun does not get a firing pose. The route and the reload
		// can run instead of a stand-and-shoot.
		if (CannotShoot(body))
		{
			RememberPressure(body);
			ClearShot(body);
			RememberLean(body, 0);
			ReleaseLook(utility, body);
			CommandWeapon(body, false, false);
			return;
		}

		BaseTarget selected = null;
		if (utility.m_CombatComponent)
			selected = utility.m_CombatComponent.GetCurrentTarget();

		RefreshShot(body, utility.m_PerceptionComponent, selected);

		if (!s_ShotLive.Contains(body))
		{
			ClearAim(body);
			RememberLean(body, 0);
			CommandWeapon(body, FeelsThreatened(utility), false);
			ReleaseLook(utility, body);
			return;
		}

		IEntity enemy = null;
		if (s_ShotLook.Contains(body))
			enemy = s_ShotLook.Get(body);

		float lean = 0;
		if (s_ShotLean.Contains(body))
			lean = s_ShotLean.Get(body);

		bool center = ShotStillVisible(body);
		bool canShoot = center;
		if (!canShoot && lean != 0)
			canShoot = SideStillClear(body, enemy, lean);

		float command = 0;
		if (canShoot)
			command = lean;

		RememberLean(body, command);

		bool fire = AimReady(body, enemy, canShoot);
		CommandWeapon(body, canShoot || FeelsThreatened(utility), fire);
		if (!fire)
			SetFireWanted(body, false);

		if (!canShoot)
			ReleaseLook(utility, body);
		else if (enemy)
			AimLook(utility, body, enemy);
		else
		{
			BaseTarget shot = ShotTarget(body);
			if (shot)
				LookAtAim(utility, ShotAimPoint(shot.GetLastSeenPosition()), 5);
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
			s_ReloadAt.Remove(body);
			s_ToppingOff.RemoveItem(body);
			s_EmptyReload.RemoveItem(body);
			s_SeenEnemyAt.Remove(body);
			s_PressureFrom.Remove(body);
			s_PressureAt.Remove(body);
			s_ReloadDash.Remove(body);
			s_ReloadThreat.Remove(body);
			s_ReloadDashAt.Remove(body);
			s_ReloadCover.RemoveItem(body);
			s_SprintNode.RemoveItem(body);
			s_SprintDist.Remove(body);
			s_SprintDistAt.Remove(body);
			EndReloadBash(body);
			s_ReloadNote.Remove(body);
			s_CoverChecked.Remove(body);
			s_CoverHidden.Remove(body);
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
		if (!OwnsShot(soldier) || CannotShoot(soldier))
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

		IEntity enemy = selected.GetTargetEntity();
		if (enemy && !IsLiving(enemy))
			return;

		s_ShotLive.Insert(body);
		s_ShotBase.Set(body, selected);
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

	// Dead and unconscious are not targets. Vanilla combat leaves both alone.
	protected static bool IsLiving(IEntity character)
	{
		CharacterControllerComponent controller = Controller(character);
		if (!controller || controller.IsUnconscious())
			return false;

		return controller.GetLifeState() == ECharacterLifeState.ALIVE;
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

	// The side point has to be in open air. A trace that starts inside a
	// wall never hits that wall, so both sides were reporting clear.
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

		left = SideRay(world, body, eye, eye - side, feet);
		right = SideRay(world, body, eye, eye + side, feet);
	}

	protected static bool SideRay(
		BaseWorld world,
		IEntity body,
		vector eye,
		vector sideEye,
		vector feet)
	{
		if (!SightClear(world, body, eye, sideEye, 0.05))
			return false;

		if (SightClear(world, body, sideEye, feet + Vector(0, 1.2, 0), 0.05))
			return true;

		return SightClear(world, body, sideEye, feet + Vector(0, 1.7, 0), 0.05);
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
		vector sideEye = eye + (transform[0] * LEAN_OFFSET);
		if (lean < 0)
			sideEye = eye - (transform[0] * LEAN_OFFSET);

		return SideRay(world, body, eye, sideEye, enemy.GetOrigin());
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

	// The shot check is too slow to keep a lean pose. A behavior abort also
	// clears it. While a lean is wanted, write it again every frame.
	protected static void RememberLean(IEntity body, float lean)
	{
		if (!body)
			return;

		if (lean == 0)
		{
			if (!s_WantedLean.Contains(body))
				return;

			s_WantedLean.Remove(body);
			SetLean(body, 0);
			return;
		}

		s_WantedLean.Set(body, lean);
		SetLean(body, lean);
		EnsureLeanPump();
	}

	protected static void EnsureLeanPump()
	{
		if (s_bLeanPumping || !GetGame())
			return;

		s_bLeanPumping = true;
		GetGame().GetCallqueue().CallLater(LeanPump, 0, false);
	}

	static void LeanPump()
	{
		if (!GetGame() || s_WantedLean.Count() == 0)
		{
			s_bLeanPumping = false;
			return;
		}

		array<IEntity> bodies = {};
		for (int i = 0; i < s_WantedLean.Count(); i++)
			bodies.Insert(s_WantedLean.GetKey(i));

		foreach (IEntity body : bodies)
		{
			if (!body || !s_WantedLean.Contains(body))
			{
				s_WantedLean.Remove(body);
				continue;
			}

			SetLean(body, s_WantedLean.Get(body));
		}

		if (s_WantedLean.Count() == 0 || !GetGame())
		{
			s_bLeanPumping = false;
			return;
		}

		GetGame().GetCallqueue().CallLater(LeanPump, 0, false);
	}

	// Sent on every shot check. Each call restarts the turn. The point is
	// the eyes. Looking at the entity uses the bounds center, and from
	// arm's length that sits under the muzzle.
	protected static void AimLook(
		SCR_AIUtilityComponent utility,
		IEntity body,
		IEntity enemy)
	{
		if (!utility || !utility.m_LookAction || !body || !enemy)
			return;

		s_LookEntity.Set(body, enemy);
		utility.m_LookAction.LookAt(ShotAimPoint(enemy), 100, 5);
	}

	// Last place he was seen is on the ground. Lift it or a close shot looks down.
	protected static void LookAtAim(
		SCR_AIUtilityComponent utility,
		vector aim,
		float duration)
	{
		if (!utility || !utility.m_LookAction)
			return;

		utility.m_LookAction.LookAt(aim, 100, duration);
	}

	static vector ShotAimPoint(IEntity enemy)
	{
		if (!enemy)
			return vector.Zero;

		ChimeraCharacter character = ChimeraCharacter.Cast(enemy);
		if (character)
			return character.EyePosition();

		return ShotAimPoint(enemy.GetOrigin());
	}

	static vector ShotAimPoint(vector feet)
	{
		return feet + Vector(0, SHOT_AIM_HEIGHT, 0);
	}

	protected static void ReleaseLook(SCR_AIUtilityComponent utility, IEntity body)
	{
		if (!body || !s_LookEntity.Contains(body))
			return;

		s_LookEntity.Remove(body);
		if (utility && utility.m_LookAction)
			utility.m_LookAction.Cancel();
	}

	// Drops a look we did not start. Facing the threat while sprinting
	// to cover holds him in a strafe.
	protected static void CancelLook(SCR_AIUtilityComponent utility, IEntity body)
	{
		if (body)
			s_LookEntity.Remove(body);

		if (utility && utility.m_LookAction)
			utility.m_LookAction.Cancel();
	}

	protected static void ReleaseLean(IEntity body)
	{
		if (!body)
			return;

		s_WantedLean.Remove(body);
		if (!s_LeanHeld.Contains(body))
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
		vector aim,
		float inset = 0.4)
	{
		vector toAim = aim - eye;
		float distance = toAim.Length();
		if (distance < 0.05)
			return true;

		toAim = toAim * (1 / distance);
		if (inset > distance * 0.5)
			inset = distance * 0.5;

		vector start = eye + (toAim * inset);

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
	// A release while he is down does not stick, so it is applied again once he is up.
	protected static void ApplyFootLock(IEntity soldier, bool locked)
	{
		if (!soldier)
			return;

		int releaseIndex = s_SpeedRelease.Find(soldier);
		if (locked)
		{
			if (releaseIndex >= 0)
				s_SpeedRelease.Remove(releaseIndex);
		}
		else if (IsDown(soldier) && releaseIndex < 0)
		{
			s_SpeedRelease.Insert(soldier);
			EnsureSpeedReleaseTick();
		}

		WriteFootLock(soldier, locked);
	}

	protected static void WriteFootLock(IEntity soldier, bool locked)
	{
		CharacterControllerComponent controller = Controller(soldier);
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

	protected static void EnsureSpeedReleaseTick()
	{
		if (s_bSpeedReleaseTicking || s_SpeedRelease.Count() == 0 || !GetGame())
			return;

		s_bSpeedReleaseTicking = true;
		GetGame().GetCallqueue().CallLater(SpeedReleaseTick, 250, false);
	}

	static void SpeedReleaseTick()
	{
		s_bSpeedReleaseTicking = false;
		if (!GetGame())
			return;

		array<IEntity> pending = {};
		foreach (IEntity soldier : s_SpeedRelease)
			pending.Insert(soldier);

		foreach (IEntity soldier : pending)
		{
			int releaseIndex = s_SpeedRelease.Find(soldier);
			if (!soldier || s_Pinned.Contains(soldier))
			{
				if (releaseIndex >= 0)
					s_SpeedRelease.Remove(releaseIndex);
				continue;
			}

			WriteFootLock(soldier, false);
			if (IsDown(soldier))
				continue;

			// The hold left idle speed. A new order issued while he was down
			// did not replace it, so he can walk again once he is up.
			AICharacterMovementComponent movement =
				AICharacterMovementComponent.Cast(
					soldier.FindComponent(AICharacterMovementComponent)
				);
			if (movement)
				movement.SetMovementTypeWanted(EMovementType.RUN);

			if (releaseIndex >= 0)
				s_SpeedRelease.Remove(releaseIndex);
		}

		if (s_SpeedRelease.Count() > 0)
			EnsureSpeedReleaseTick();
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
		IEntity character;
		IEntity agentEntity;
		if (m_Utility)
		{
			character = m_Utility.m_OwnerEntity;
			agentEntity = m_Utility.GetOwner();
		}

		// Out of ammo, or a reload under pressure. Leave the behavior so the
		// move to another cluster can run.
		if (
			(KK_GarrisonHold.HasBuilding(character) && KK_GarrisonHold.CannotShoot(character)) ||
			(KK_GarrisonHold.HasBuilding(agentEntity) && KK_GarrisonHold.CannotShoot(agentEntity)) ||
			(KK_GarrisonHold.HasBuilding(character) && KK_GarrisonHold.MustDashToReload(character)) ||
			(KK_GarrisonHold.HasBuilding(agentEntity) && KK_GarrisonHold.MustDashToReload(agentEntity))
		)
		{
			return 0;
		}

		float score = super.CustomEvaluate();

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

		if (
			KK_GarrisonHold.SprintBeforeReload(body) ||
			KK_GarrisonHold.SprintBeforeReload(owner) ||
			KK_GarrisonHold.IsReloadBashing(body) ||
			KK_GarrisonHold.IsReloadBashing(owner)
		)
		{
			if (m_State && m_State.IsExecutingRequest())
				m_State.CancelRequest();

			return ENodeResult.RUNNING;
		}

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
							aimPos = KK_GarrisonHold.ShotAimPoint(targetEntity);
						else
							aimPos = KK_GarrisonHold.ShotAimPoint(target.GetLastSeenPosition());
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
		if (DoorFiring() || RunningToReloadNode() || BashingReload())
			return 0;

		return super.CustomEvaluate();
	}

	protected bool RunningToReloadNode()
	{
		return m_Utility &&
			(
				KK_GarrisonHold.SprintBeforeReload(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.SprintBeforeReload(m_Utility.GetOwner())
			);
	}

	protected bool BashingReload()
	{
		return m_Utility &&
			(
				KK_GarrisonHold.IsReloadBashing(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsReloadBashing(m_Utility.GetOwner())
			);
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

modded class SCR_AIThrowGrenadeToBehavior
{
	override float CustomEvaluate()
	{
		if (
			m_Utility &&
			(
				KK_GarrisonHold.SprintBeforeReload(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.SprintBeforeReload(m_Utility.GetOwner()) ||
				KK_GarrisonHold.IsReloadBashing(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsReloadBashing(m_Utility.GetOwner())
			)
		)
		{
			return 0;
		}

		return super.CustomEvaluate();
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
				KK_GarrisonHold.IsDoorFiring(m_Utility.GetOwner()) ||
				KK_GarrisonHold.SprintBeforeReload(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.SprintBeforeReload(m_Utility.GetOwner()) ||
				KK_GarrisonHold.IsReloadBashing(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsReloadBashing(m_Utility.GetOwner())
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

		if (!ignore)
		{
			IEntity soldier = m_OwnerEntity;
			if (!soldier)
				soldier = GetOwner();

			KK_GarrisonHold.ConsiderTopOff(soldier);
		}

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

		// The move order raises on a loop. That raise restarts a reload,
		// and a raised weapon will not take the sprint to cover.
		if (KK_GarrisonHold.IsQuietReload(body) || KK_GarrisonHold.IsQuietReload(owner))
			return ENodeResult.SUCCESS;

		if (
			KK_GarrisonHold.SprintBeforeReload(body) ||
			KK_GarrisonHold.SprintBeforeReload(owner) ||
			KK_GarrisonHold.IsReloadBashing(body) ||
			KK_GarrisonHold.IsReloadBashing(owner)
		)
		{
			KK_GarrisonHold.LowerForReloadSprint(owner);
			return ENodeResult.SUCCESS;
		}

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AILookAction
{
	override void LookAt(vector pos, float priority, float duration = 0.8)
	{
		if (SprintIgnoring() || SprintingToCover())
			return;

		super.LookAt(pos, priority, duration);
	}

	override void LookAt(IEntity ent, float priority, float duration = 0.8)
	{
		if (SprintIgnoring() || SprintingToCover())
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

	protected bool SprintingToCover()
	{
		return m_Utility &&
			(
				KK_GarrisonHold.SprintBeforeReload(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.SprintBeforeReload(m_Utility.GetOwner())
			);
	}
}

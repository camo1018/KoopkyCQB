modded class SCR_AICombatComponent
{
	void KK_ClearTarget()
	{
		m_SelectedTarget = null;
	}

	// A last-seen point is not a target once the building order is over.
	// A living enemy he can see stays. A cover hold keeps a living one
	// the trace cannot confirm.
	bool KK_DropStaleTarget()
	{
		if (!m_SelectedTarget)
			return false;

		if (
			KK_GarrisonHold.IsLivingTarget(m_SelectedTarget) &&
			(
				KK_GarrisonHold.IsPinned(GetOwner()) ||
				KK_GarrisonHold.SeesTarget(GetOwner(), m_SelectedTarget)
			)
		)
		{
			return false;
		}

		KK_ClearTarget();
		m_SelectedTargetVisible = false;
		KK_GarrisonHold.DropRememberedAim(GetOwner());
		m_SelectedTargetDestinationPos = KK_GarrisonHold.LevelAimPoint(GetOwner());
		return true;
	}

	// The sprint hid the enemy so the run would not aim. If he is still
	// alive and in sight, he is the target again on the frame the sprint ends.
	void KK_HoldSprintEnemy(out BaseTarget outCurrentTarget)
	{
		if (KK_GarrisonHold.IsBoundSprint(GetOwner()))
			return;

		BaseTarget known = KK_GarrisonHold.SprintKnown(GetOwner());
		if (!known)
			return;

		if (!KK_GarrisonHold.IsLivingTarget(known))
		{
			KK_GarrisonHold.ForgetSprintEnemy(GetOwner());
			return;
		}

		if (m_SelectedTarget && KK_GarrisonHold.IsLivingTarget(m_SelectedTarget))
		{
			KK_GarrisonHold.ForgetSprintEnemy(GetOwner());
			return;
		}

		if (!KK_GarrisonHold.SeesTarget(GetOwner(), known))
			return;

		IEntity enemy = known.GetTargetEntity();
		if (!enemy)
			return;

		m_SelectedTarget = known;
		m_SelectedTargetVisible = true;
		m_SelectedTargetDestinationPos = KK_GarrisonHold.ShotAimPoint(GetOwner(), enemy);
		outCurrentTarget = known;
	}

	override void UpdatePerceptionFactor(
		PerceptionComponent perceptionComp,
		SCR_AIThreatSystem threatSystem)
	{
		KK_WriteHousePerception(perceptionComp, threatSystem);
	}

	// Recognition rates for a house order. Compat calls this and skips the
	// other addon, whose super would replace these rates.
	void KK_WriteHousePerception(
		PerceptionComponent perceptionComp,
		SCR_AIThreatSystem threatSystem)
	{
		if (KK_GarrisonHold.IsIgnoringTargets(GetOwner()))
		{
			if (perceptionComp)
				perceptionComp.SetPerceptionFactor(0);

			return;
		}

		// Vanilla threatened recognition is slower than safe, so a squad
		// that is already being shot at is the last to pick the enemy up.
		// With sharp combat on, an attack stays on the alerted rate. The
		// scenario multiplier still scales it.
		if (
			perceptionComp &&
			threatSystem &&
			KK_GarrisonHold.IsAttackSearch(GetOwner()) &&
			KK_PerceptionBoost.UseSharpCombat()
		)
		{
			float attackFactor = PERCEPTION_FACTOR_ALERTED;
			attackFactor *= m_fEquipmentPerceptionFactor;
			attackFactor *= m_fPerceptionFactor;
			perceptionComp.SetPerceptionFactor(attackFactor);
			return;
		}

		if (
			!perceptionComp ||
			!threatSystem ||
			!KK_PerceptionBoost.IsActiveSoldier(GetOwner()) ||
			!KK_PerceptionBoost.UseSharpCombat()
		)
		{
			vanilla.UpdatePerceptionFactor(perceptionComp, threatSystem);
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
		KK_EvaluateHouseOrder(
			outWeaponEvent,
			outSelectedTargetChanged,
			outPrevTarget,
			outCurrentTarget,
			outRetreatTargetChanged,
			outCompartmentChanged
		);
	}

	// Target and aim for a house order. Compat calls this so the other
	// addon's pass, which rewrites the aim, does not run.
	void KK_EvaluateHouseOrder(
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

		// Downed men stay on the order. This pass does not aim, swap, or shoot.
		if (
			KK_GarrisonHold.IsDown(GetOwner()) &&
			KK_GarrisonHold.OnHouseOrder(GetOwner())
		)
		{
			KK_GarrisonHold.PauseDownedCQB(GetOwner());
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

		vanilla.EvaluateWeaponAndTarget(
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

		if (KK_HoldFightWeapon())
		{
			// Hands and the selector stay on the gun that can actually fire.
			// Publishing another weapon here would pull that gun back out.
			outWeaponEvent = false;
		}
		else if (KK_KeepSidearm())
		{
			// The empty rifle must not be published. That swap would put it
			// back in his hands before the sidearm can fire.
			outWeaponEvent = false;
		}
		else if (KK_GarrisonHold.ShouldKeepRifle(GetOwner()) && KK_KeepPrimary())
		{
			// Publishing the frag would equip it. The empty rifle is then
			// forced back, and the two swaps never finish a reload.
			outWeaponEvent = false;
		}

		// A bound sprint is the move. A selected enemy here is what aims
		// that move, and an aimed move will not sprint. He is remembered
		// so the stop does not have to recognize him again.
		if (KK_GarrisonHold.IsBoundSprint(GetOwner()))
		{
			KK_GarrisonHold.RememberSprintEnemy(GetOwner(), m_SelectedTarget);
			KK_ClearTarget();
			m_SelectedTargetVisible = false;
			outCurrentTarget = null;
			outSelectedTargetChanged = false;
			vector lane;
			if (KK_GarrisonHold.GetSprintLook(GetOwner(), lane))
				m_SelectedTargetDestinationPos = lane;
			return;
		}

		KK_HoldSprintEnemy(outCurrentTarget);

		// A repeated order starts clean. The selector just put the last
		// fight back. A last-seen point is not a target of this order.
		// Someone he can see right now is.
		if (KK_GarrisonHold.IsFreshOrder(GetOwner()))
		{
			// A cover hold stands against something solid. This order's sight
			// trace hits that cover, so it must not delete a living target
			// the normal attack can still see.
			bool coverHold = KK_GarrisonHold.IsPinned(GetOwner());
			if (
				m_SelectedTarget &&
				KK_GarrisonHold.IsLivingTarget(m_SelectedTarget) &&
				(
					coverHold ||
					KK_GarrisonHold.SeesTarget(GetOwner(), m_SelectedTarget)
				)
			)
			{
				KK_GarrisonHold.EndFreshOrder(GetOwner());
			}
			else
			{
				KK_ClearTarget();
				m_SelectedTargetVisible = false;
				outCurrentTarget = null;
				outSelectedTargetChanged = false;
				KK_GarrisonHold.DropRememberedAim(GetOwner());
				m_SelectedTargetDestinationPos =
					KK_GarrisonHold.LevelAimPoint(GetOwner());
			}
		}

		if (!KK_GarrisonHold.UseRoomCombat() || !KK_GarrisonHold.HasBuilding(GetOwner()))
		{
			KK_StampPathLook();
			return;
		}

		// A lost body is not a target. A living one who is behind a wall this
		// frame still is. The look is dropped so it does not turn against
		// the hold facing. The destination stays level and ahead of him,
		// so the base aim is not handed the body's feet.
		BaseTarget selected = m_SelectedTarget;
		if (selected && !KK_GarrisonHold.IsLivingTarget(selected))
		{
			KK_ClearTarget();
			m_SelectedTargetVisible = false;
			outCurrentTarget = null;
			outSelectedTargetChanged = false;
			KK_GarrisonHold.DropRememberedAim(GetOwner());
			m_SelectedTargetDestinationPos =
				KK_GarrisonHold.LevelAimPoint(GetOwner());
			selected = null;
		}
		else if (selected && !KK_GarrisonHold.SeesTarget(GetOwner(), selected))
		{
			// The trace missed, so the shot stays blocked. He is still the
			// target. A point along our own facing was turning the rifle
			// down the hall.
			m_SelectedTargetVisible = false;
			IEntity hiddenBody = selected.GetTargetEntity();
			if (hiddenBody)
				m_SelectedTargetDestinationPos =
					KK_GarrisonHold.ShotAimPoint(GetOwner(), hiddenBody);
		}
		else if (selected)
		{
			IEntity seenBody = selected.GetTargetEntity();
			if (seenBody)
				m_SelectedTargetDestinationPos =
					KK_GarrisonHold.ShotAimPoint(GetOwner(), seenBody);
		}

		PerceptionComponent perception = PerceptionComponent.Cast(
			GetOwner().FindComponent(PerceptionComponent)
		);
		KK_GarrisonHold.RefreshShot(GetOwner(), perception, selected);

		BaseTarget preferred = KK_GarrisonHold.ShotTarget(GetOwner());
		if (!preferred || preferred == m_SelectedTarget)
		{
			KK_StampPathLook();
			return;
		}

		if (!KK_GarrisonHold.ShotStillVisible(GetOwner()))
		{
			KK_StampPathLook();
			return;
		}

		IEntity preferredEntity = preferred.GetTargetEntity();
		if (!preferredEntity || !KK_GarrisonHold.IsFightable(preferredEntity))
		{
			KK_StampPathLook();
			return;
		}

		// The selector stays on one enemy. In the building, the nearest one
		// he can see replaces that fixation before the attack behavior runs.
		// The change reaction is not fired. It would run on every evaluation
		// while the selector keeps picking someone else, and that hitch stops
		// the move.
		m_SelectedTarget = preferred;
		m_SelectedTargetVisible = true;
		m_SelectedTargetDestinationPos =
			KK_GarrisonHold.ShotAimPoint(GetOwner(), preferredEntity);
		outCurrentTarget = preferred;
		outSelectedTargetChanged = false;
		KK_StampPathLook();
	}

	// The clear route owns the aim point. CRX writes the threat here on
	// the same evaluation, and this puts the next uncleared node back.
	// A living enemy he can see takes that point instead. The plant is
	// already holding his feet. A man past CQB range keeps the aim the
	// normal attack already has.
	protected void KK_StampPathLook()
	{
		if (KK_GarrisonHold.DistantBaseFight(GetOwner()))
		{
			KK_GarrisonHold.YieldLookToAttack(GetOwner());
			return;
		}

		vector contactAim;
		if (KK_GarrisonHold.TakeContactLook(GetOwner(), contactAim))
		{
			m_SelectedTargetDestinationPos = contactAim;
			return;
		}

		vector look;
		if (!KK_GarrisonHold.GetPathLook(GetOwner(), look))
			return;

		m_SelectedTargetDestinationPos = look;
		KK_GarrisonHold.RefreshPathLook(GetOwner());
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

	// The gun he can shoot, in his hands and on the selector. Primary when
	// a magazine for it has rounds. A sidearm when the primary is empty.
	// After he gets up the current weapon is often missing, so this puts
	// it back. False when the selector was already on that gun.
	protected bool KK_HoldFightWeapon()
	{
		if (!KK_GarrisonHold.RunningCQB(GetOwner()))
			return false;

		BaseWeaponComponent want = KK_GarrisonHold.WeaponToHold(GetOwner());
		if (!want)
			return false;

		bool changed = m_SelectedWeaponComp != want;
		if (!changed)
		{
			KK_GarrisonHold.SwitchToWeapon(GetOwner(), want);
			return false;
		}

		m_SelectedWeaponComp = want;
		m_iSelectedMuzzle = 0;
		m_SelectedMagazineComp = null;
		m_fSelectedWeaponMinDist = 0;
		m_fSelectedWeaponMaxDist = 800;
		m_bSelectedWeaponDirectDamage = true;

		EMuzzleType muzzleType = EMuzzleType.MT_BaseMuzzle;
		array<BaseMuzzleComponent> muzzles = {};
		want.GetMuzzlesList(muzzles);
		if (muzzles.Count() > 0 && muzzles[0])
		{
			m_SelectedMagazineComp = muzzles[0].GetMagazine();
			muzzleType = muzzles[0].GetMuzzleType();
		}

		if (m_ConfigComponent)
		{
			m_SelectedWeaponResource = m_ConfigComponent.GetTreeNameForWeaponType(
				want.GetWeaponType(),
				muzzleType
			);
		}

		KK_GarrisonHold.SwitchToWeapon(GetOwner(), want);
		return true;
	}

	// Primary is empty and another gun has rounds. The selector stays on
	// that gun, or the empty rifle is equipped again on the next evaluation.
	protected bool KK_KeepSidearm()
	{
		BaseWeaponComponent sidearm = KK_GarrisonHold.ForcedSidearm(GetOwner());
		if (!sidearm)
			return false;

		bool changed = m_SelectedWeaponComp != sidearm;
		if (!changed)
		{
			KK_GarrisonHold.SwitchToWeapon(GetOwner(), sidearm);
			return false;
		}

		m_SelectedWeaponComp = sidearm;
		m_iSelectedMuzzle = 0;
		m_SelectedMagazineComp = null;
		m_fSelectedWeaponMinDist = 0;
		m_fSelectedWeaponMaxDist = 800;
		m_bSelectedWeaponDirectDamage = true;

		EMuzzleType muzzleType = EMuzzleType.MT_BaseMuzzle;
		array<BaseMuzzleComponent> muzzles = {};
		sidearm.GetMuzzlesList(muzzles);
		if (muzzles.Count() > 0 && muzzles[0])
		{
			m_SelectedMagazineComp = muzzles[0].GetMagazine();
			muzzleType = muzzles[0].GetMuzzleType();
		}

		if (m_ConfigComponent)
		{
			m_SelectedWeaponResource = m_ConfigComponent.GetTreeNameForWeaponType(
				sidearm.GetWeaponType(),
				muzzleType
			);
		}

		KK_GarrisonHold.SwitchToWeapon(GetOwner(), sidearm);
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

// The pose a snap round is launched along from the fire callback.
class KK_SnapShotAim
{
	IEntity m_Shooter;
	vector m_Origin;
	vector m_Direction;
}

class KK_GarrisonHold
{
	protected static ref set<IEntity> s_Pinned = new set<IEntity>();
	// Unlocked while unconscious. The speed override does not stick until he is up.
	protected static ref array<IEntity> s_SpeedRelease = {};
	protected static bool s_bSpeedReleaseTicking;
	protected static ref set<IEntity> s_IgnoringTargets = new set<IEntity>();
	protected static ref set<IEntity> s_DoorFiring = new set<IEntity>();
	protected static ref set<IEntity> s_MoveFire = new set<IEntity>();
	protected static ref set<IEntity> s_MoveWeaponUp = new set<IEntity>();
	protected static ref set<IEntity> s_MoveFiring = new set<IEntity>();
	protected static ref set<IEntity> s_MoveWeaponKnown = new set<IEntity>();
	// The raise was requested and the pose has since been up. Until then
	// another SetWeaponRaised restarts the animation and cuts off the step.
	protected static ref set<IEntity> s_RaiseSettled = new set<IEntity>();
	protected static ref map<IEntity, float> s_RaiseSentAt = new map<IEntity, float>();
	// This order has not seen a living enemy yet. Last-seen points from
	// the previous clear of this house stay out of the selector.
	protected static ref set<IEntity> s_FreshOrder = new set<IEntity>();
	protected static ref set<AIAgent> s_OrderNoted = new set<AIAgent>();
	protected static ref TraceParam s_SightTrace;
	protected static ref TraceParam s_ConditionTrace;
	protected static IEntity s_ConditionBody;
	protected static IEntity s_SightViewer;
	// The man this trace is trying to see. His own body is not a wall.
	protected static IEntity s_SightTarget;
	protected static ref map<IEntity, vector> s_ApproachGoals =
		new map<IEntity, vector>();
	protected static ref map<IEntity, IEntity> s_Buildings =
		new map<IEntity, IEntity>();
	// On a clear order. Garrison uses the building map too, and a quiet
	// post is allowed to lower the rifle. A clear is not.
	protected static ref set<IEntity> s_Clearing = new set<IEntity>();
	// On an attack order. The rifle stays up. Shooting stays with vanilla attack.
	protected static ref set<IEntity> s_Advancing = new set<IEntity>();
	// Recognition stays at the alerted rate for the whole attack, including
	// the bound sprint. The sprint does not turn the head toward a contact.
	protected static ref set<IEntity> s_AttackSearch = new set<IEntity>();
	protected static ref array<BaseTarget> s_AttackSeen = new array<BaseTarget>();
	protected static const float ATTACK_CONTACT_RANGE = 180;
	// The pair that is bounding. The rifle stays down so the sprint can run.
	protected static ref set<IEntity> s_BoundSprint = new set<IEntity>();
	// Combat move states that must refuse every request for that sprint.
	// A request still in this state is what turns the sprint into a strafe.
	protected static ref set<SCR_AICombatMoveState> s_SprintMoveLock =
		new set<SCR_AICombatMoveState>();
	// A clear refuses a combat move when it is submitted. Cancelling one
	// that is already running, every tick, is a fight with CRX.
	protected static ref set<SCR_AICombatMoveState> s_ClearMoveLock =
		new set<SCR_AICombatMoveState>();
	// Where the sprint is looking. CRX writes the enemy into the look
	// between order updates, and this puts the lane back.
	protected static ref map<IEntity, vector> s_SprintLook =
		new map<IEntity, vector>();
	// Where a clear or garrison is looking. While this is set, no other
	// look is accepted.
	protected static ref map<IEntity, vector> s_PathLook =
		new map<IEntity, vector>();
	// Last place a man was actually seen, and when that sight was lost.
	protected static ref map<IEntity, vector> s_LostLook =
		new map<IEntity, vector>();
	protected static ref map<IEntity, float> s_LostLookAt =
		new map<IEntity, float>();
	protected static bool s_bPathLookWrite;
	// Point the experimental snap keeps writing. The look solver eases the
	// rifle off a single write before the next shot check.
	protected static ref map<IEntity, vector> s_SnapAim =
		new map<IEntity, vector>();
	protected static ref set<SCR_AICombatMoveState> s_SnapTurn =
		new set<SCR_AICombatMoveState>();
	protected static bool s_bSnapAimPumping;
	// Set while a snap round is launched, so that launch's own fire callback
	// does not launch the same round again.
	protected static bool s_bSnapShotApply;
	// The skill scatter is held, or a new roll every frame shakes the rifle.
	protected static ref map<IEntity, vector> s_SnapError =
		new map<IEntity, vector>();
	protected static ref map<IEntity, float> s_SnapErrorAt =
		new map<IEntity, float>();
	protected static ref map<IEntity, IEntity> s_SnapErrorEnemy =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, float> s_SnapLogAt =
		new map<IEntity, float>();
	protected static const float SNAP_AIM_ERROR_MS = 400;
	protected static const float SNAP_AIM_LOG_MS = 1000;
	static const float PATH_LOOK_PRIORITY = 1000;
	// An enemy already selected when the bound started. The sprint does not
	// aim at him, and perception then drops him, so the stop had to spot
	// him again. This puts that same target back.
	protected static ref map<IEntity, ref BaseTarget> s_SprintKnown =
		new map<IEntity, ref BaseTarget>();
	// Handed to the fight, then walked back inside the return distance.
	// His run is his. The others at the point keep the mod.
	protected static ref set<IEntity> s_Recalled = new set<IEntity>();
	protected static ref map<IEntity, float> s_ShotAt =
		new map<IEntity, float>();
	protected static ref map<IEntity, IEntity> s_ShotLook =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, ref BaseTarget> s_ShotBase =
		new map<IEntity, ref BaseTarget>();
	protected static ref set<IEntity> s_ShotLive = new set<IEntity>();
	// He is shooting this man and the man has not taken his damage yet.
	// After the try he moves, and the cool-off is how long that move
	// lasts before he plants and shoots again.
	protected static ref map<IEntity, float> s_BarrelBlockedSince =
		new map<IEntity, float>();
	protected static ref map<IEntity, IEntity> s_BarrelBlockedTarget =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, float> s_BarrelCoolUntil =
		new map<IEntity, float>();
	protected static ref set<IEntity> s_RoomFire = new set<IEntity>();
	protected static ref array<BaseTarget> s_Perceived = new array<BaseTarget>();
	protected static ref array<IEntity> s_Candidates = new array<IEntity>();
	protected static ref array<float> s_CandidateDist = new array<float>();
	protected static ref map<IEntity, ref BaseTarget> s_CandidateKnown =
		new map<IEntity, ref BaseTarget>();
	// A man confirmed through glass stays in the list when nearer men are
	// behind walls. Those wall hits are the ones that get dropped.
	protected static ref set<IEntity> s_CandidateKept = new set<IEntity>();
	protected static IEntity s_ScanBody;
	protected static IEntity s_ScanBuilding;
	protected static ref set<IEntity> s_ScanSeen = new set<IEntity>();
	protected static ref array<IEntity> s_NearBodies = new array<IEntity>();
	protected static IEntity s_NearViewer;
	protected static ref array<IEntity> s_CloseBodies = new array<IEntity>();
	protected static ref array<float> s_CloseDist = new array<float>();
	protected static const int GLASS_CHECKS = 12;
	// Milliseconds the next shot check was scheduled with.
	protected static int s_iShotTickMs;
	protected static bool s_bShotTicking;
	protected static const float ROOM_SCAN_RADIUS = 35;
	protected static const int SHOT_CANDIDATES = 8;
	protected static const float LEAN_OFFSET = 0.4;
	// Origin is the feet. A look at that point from arm's length points the rifle down.
	protected static const float SHOT_AIM_HEIGHT = 1.5;
	// Upper chest first. The head is used only when the chest is behind cover.
	// The head point is the skull, not the eyes. Spine2 sits on the abdomen,
	// and from a muzzle above it a close shot pitches down into the stomach.
	protected static const float CHEST_AIM_HEIGHT = 1.4;
	protected static const float HEAD_AIM_HEIGHT = 1.5;
	// Long enough that a fight does not reissue the look and restart the turn.
	protected static const float SHOT_LOOK_DURATION = 30;
	// The post facing waits this long after the room gun last looked at him.
	// A missed trace in that window was turning him back to the post.
	protected static const float ROOM_LOOK_HOLD_MS = 2000;
	protected static ref map<IEntity, float> s_RoomLookAt =
		new map<IEntity, float>();
	protected static ref map<IEntity, float> s_ShotLean = new map<IEntity, float>();
	// A moving shot loses the ray for a frame at a time. The trigger stays
	// with the last accepted enemy through that gap.
	protected static ref map<IEntity, float> s_MoveShotSeen = new map<IEntity, float>();
	protected static const float MOVE_SHOT_HOLD_MS = 400;
	protected static ref map<IEntity, IEntity> s_AimTarget = new map<IEntity, IEntity>();
	protected static ref map<IEntity, float> s_AimSince = new map<IEntity, float>();
	// Attack contact shoots on the same frame the enemy is in sight.
	// The indoor shot keeps its own delay.
	protected static ref set<IEntity> s_InstantAim = new set<IEntity>();
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
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (pinned)
			s_Pinned.Insert(body);
		else
		{
			s_Pinned.RemoveItem(body);
		}

		ApplyFootLock(body, pinned);
	}

	static bool IsPinned(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_Pinned.Contains(body);
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

	// The assignment stays. The shot, the plant, the raised rifle, and the
	// move lock do not. The next pass after he is up puts those back.
	static void PauseDownedCQB(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		AbortShot(UtilityOf(body), body);
		SetPinned(body, false);
		SetMoveFire(body, false);
		SetDoorFiring(body, false);
		SetReturnFire(body, false);
		SetIgnoringTargets(body, false);
		SetBoundSprint(body, false);
		ClearApproachGoal(body);
		LockClearMove(body, false);
		ClearPathLook(body);
		DropRememberedAim(body);
	}

	// Alive and on his feet. A downed enemy is not a reason to hold the route.
	static bool IsFightable(IEntity character)
	{
		return IsLiving(character);
	}

	// A contact with no body is a last-seen point. That point stays "visible"
	// in open air, and the attack keeps firing at it.
	static bool IsLivingTarget(BaseTarget target)
	{
		if (!target)
			return false;

		return IsFightable(target.GetTargetEntity());
	}

	// True only while a living enemy is actually in sight. The attack
	// otherwise keeps the last place a burst was fired.
	static bool HasVisibleEnemy(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatComponent)
			return false;

		return SeesTarget(body, utility.m_CombatComponent.GetCurrentTarget());
	}

	// A level point ahead of him. The feet read as aiming at the floor.
	static vector LevelAimPoint(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return vector.Zero;

		vector transform[4];
		body.GetWorldTransform(transform);
		return body.GetOrigin() + Vector(0, 1.6, 0) + (transform[2] * 8);
	}

	// The look and the trigger stay on the last burst after the body is gone.
	static void DropRememberedAim(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		SetFireWanted(body, false);
		s_MoveFiring.RemoveItem(body);
		ClearAim(body);
		ClearSnapAim(body);

		SCR_AIUtilityComponent utility = UtilityOf(body);
		CancelLook(utility, body);
	}

	// Threat stays up after the body is gone. That leftover strafe and the
	// look at the last point are what make the next move wander.
	// A remembered contact also keeps him alert, and alert keeps sending
	// him back to the last place he looked.
	static void DropStaleAlert(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (HasVisibleEnemy(body) || s_ShotLive.Contains(body))
			return;

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_ThreatSystem)
			return;

		utility.m_ThreatSystem.KK_DropStaleAlert();
	}

	// A clear can leave him speed-locked, idle, or ignoring targets. Follow
	// and the next fight both need that undone.
	static void ReleaseFollowLocks(AIAgent agent)
	{
		if (!agent)
			return;

		SetIgnoringTargets(agent, false);
		SetMoveFire(agent, false);
		SetDoorFiring(agent, false);
		ClearApproachGoal(agent);

		IEntity body = agent.GetControlledEntity();
		SetPinned(body, false);
		if (!body)
			return;

		AICharacterMovementComponent movement =
			AICharacterMovementComponent.Cast(
				body.FindComponent(AICharacterMovementComponent)
			);
		if (movement)
			movement.SetMovementTypeWanted(EMovementType.RUN);
	}

	// A new clear or garrison. The last fight in this house is over unless
	// a living enemy is in sight right now.
	static void NoteOrderGroup(SCR_AIGroup group)
	{
		if (!group)
			return;

		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents)
			NoteOrderSoldier(agent);
	}

	static void NoteOrderSoldier(AIAgent agent)
	{
		if (!agent || s_OrderNoted.Contains(agent))
			return;

		s_OrderNoted.Insert(agent);

		IEntity body = CharacterBody(agent);
		if (!body)
			return;

		if (HasVisibleEnemy(body) || s_ShotLive.Contains(body))
			return;

		s_FreshOrder.Insert(body);
		ClearSelectedTarget(body);
		ReleaseLatentCombat(body);
		DropStaleAlert(body);
		s_MoveWeaponKnown.RemoveItem(body);
		s_MoveWeaponUp.RemoveItem(body);
		s_RaiseSettled.RemoveItem(body);
	}

	static void ReleaseOrderGroup(SCR_AIGroup group)
	{
		if (!group)
			return;

		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			s_OrderNoted.RemoveItem(agent);
			EndFreshOrder(agent);
		}
	}

	static bool IsFreshOrder(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_FreshOrder.Contains(body);
	}

	static void EndFreshOrder(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (body)
			s_FreshOrder.RemoveItem(body);
	}

	protected static void ClearSelectedTarget(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatComponent)
			return;

		utility.m_CombatComponent.KK_ClearTarget();
	}

	// The building order is over. Keep the last-seen point from arming
	// the next move, until a living enemy is actually in sight.
	static void ReleaseStaleAim(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || HasVisibleEnemy(body))
			return;

		s_FreshOrder.Insert(body);

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatComponent)
			return;

		utility.m_CombatComponent.KK_DropStaleTarget();
	}

	static void ReleaseLatentCombat(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (HasVisibleEnemy(body) || s_ShotLive.Contains(body))
			return;

		if (SprintBeforeReload(body) || s_ReloadBash.Contains(body))
			return;

		DropRememberedAim(body);
		CancelCombatMove(body);
		ClearApproachGoal(body);
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

	// The first burst on an attack. Move fire pulls the trigger, and the
	// aim delay is skipped so the shot is not waiting on the attack behavior.
	// Turning it off drops the trigger and leaves the rifle where it is.
	static void SetReturnFire(IEntity soldier, bool enabled)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (enabled)
		{
			SetMoveFire(body, true);
			s_InstantAim.Insert(body);
			return;
		}

		if (!s_InstantAim.Contains(body) && !s_MoveFire.Contains(body))
			return;

		s_InstantAim.RemoveItem(body);
		s_MoveFire.RemoveItem(body);
		s_MoveFiring.RemoveItem(body);
		SetFireWanted(body, false);
		ClearShot(body);
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

		// Down, or no gun left that can fire. The move stays, the shot does not.
		if (IsDown(dryBody) || !HasGunAmmo(dryBody))
		{
			AbortShot(utility, dryBody);
			return;
		}

		EnsureHeldGun(dryBody);

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

		// Threat keeps the gun up. He fires when a sightline reaches someone.
		// A center-only check hits cover beside him and leaves the trigger off.
		IEntity lookAt = null;
		if (body && s_ShotLook.Contains(body))
			lookAt = s_ShotLook.Get(body);

		if (!lookAt || !IsLiving(lookAt))
		{
			RememberLean(body, 0);
			bool raise = FeelsThreatened(utility);
			if (ClearWeaponStaysUp(body) || AttackWeaponStaysUp(body))
				raise = true;

			bool wasFiring = body && s_MoveFiring.Contains(body);
			CommandWeapon(body, raise, false);
			if (wasFiring && utility.m_LookAction)
				utility.m_LookAction.Cancel();

			return;
		}

		float lean = 0;
		if (s_ShotLean.Contains(body))
			lean = s_ShotLean.Get(body);

		// The commit already required a clear shot. Checking it again every
		// frame drops the trigger while he is moving, so the gun never cycles.
		// It stays down until the target refresh gives that enemy up.
		// A plant is standing still, so the trace is checked again and a
		// wall he is now behind drops the trigger.
		bool canShoot = IsMoveFire(body) && !IsPinned(body);
		if (!canShoot)
		{
			canShoot = ShotStillVisible(body);
			if (!canShoot && lean != 0)
				canShoot = SideStillClear(body, lookAt, lean);
		}

		float command = 0;
		if (canShoot)
			command = lean;

		RememberLean(body, command);
		if (canShoot && lookAt)
			NoteNoDamage(body, lookAt);

		bool fire = AimReady(body, lookAt, canShoot);
		bool raise = canShoot || FeelsThreatened(utility);
		if (ClearWeaponStaysUp(body) || AttackWeaponStaysUp(body))
			raise = true;

		CommandWeapon(body, raise, fire);
		if (!fire)
			SetFireWanted(body, false);

		AimLook(utility, body, lookAt);
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

	// One request at a time. A repeat while the swap is playing restarts it.
	static void SwitchToWeapon(IEntity soldier, BaseWeaponComponent weapon)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !weapon)
			return;

		if (CurrentWeapon(body) == weapon)
		{
			s_PrimarySwitchAt.Remove(body);
			return;
		}

		CharacterControllerComponent controller = Controller(body);
		if (!controller || controller.IsChangingItem())
			return;

		float now = WorldTime();
		if (
			s_PrimarySwitchAt.Contains(body) &&
			now - s_PrimarySwitchAt.Get(body) < PRIMARY_SWITCH_RETRY_MS
		)
		{
			return;
		}

		s_PrimarySwitchAt.Set(body, now);
		controller.SetFireWeaponWanted(false);
		SCR_AIWeaponHandling.StartWeaponSwitchCharacter(controller, weapon);
	}

	// The slot named primary. Another gun in a different slot is a sidearm.
	static BaseWeaponComponent PrimarySlotWeapon(IEntity soldier)
	{
		BaseWeaponManagerComponent manager = WeaponManager(soldier);
		if (!manager)
			return null;

		array<WeaponSlotComponent> slots = {};
		manager.GetWeaponsSlots(slots);
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot || slot.GetWeaponSlotType() != PRIMARY_SLOT)
				continue;

			BaseWeaponComponent weapon = WeaponInSlot(slot);
			if (!weapon || IsThrowableType(weapon.GetWeaponType()))
				return null;

			return weapon;
		}

		return null;
	}

	// Rounds in the gun, or in a magazine for it. An empty magazine is zero.
	static int AvailableAmmo(IEntity soldier, BaseWeaponComponent weapon)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !weapon || IsThrowableType(weapon.GetWeaponType()))
			return 0;

		array<BaseMuzzleComponent> muzzles = {};
		weapon.GetMuzzlesList(muzzles);
		if (muzzles.Count() == 0 || !muzzles[0])
			return 0;

		BaseMuzzleComponent muzzle = muzzles[0];
		int ammo = muzzle.GetAmmoCount();
		if (muzzle.IsDisposable())
			return ammo;

		IEntity loadedEntity = null;
		BaseMagazineComponent loaded = muzzle.GetMagazine();
		if (loaded)
		{
			loadedEntity = loaded.GetOwner();
			if (loaded.GetAmmoCount() > ammo)
				ammo = loaded.GetAmmoCount();
		}

		BaseMagazineWell well = muzzle.GetMagazineWell();
		int spare = MagazineAmmoInInventory(body, well, loadedEntity);
		if (spare > 0)
			ammo += spare;

		return ammo;
	}

	static bool HasGunAmmo(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		BaseWeaponManagerComponent manager = WeaponManager(body);
		if (!body || !manager)
			return false;

		array<WeaponSlotComponent> slots = {};
		manager.GetWeaponsSlots(slots);
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot)
				continue;

			if (AvailableAmmo(body, WeaponInSlot(slot)) > 0)
				return true;
		}

		return false;
	}

	// Another non-throwable gun that still has rounds. The fullest one wins.
	static BaseWeaponComponent BestSidearm(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		BaseWeaponManagerComponent manager = WeaponManager(body);
		if (!body || !manager)
			return null;

		BaseWeaponComponent primary = PrimarySlotWeapon(body);
		array<WeaponSlotComponent> slots = {};
		manager.GetWeaponsSlots(slots);

		BaseWeaponComponent best = null;
		int bestAmmo = 0;
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot)
				continue;

			BaseWeaponComponent weapon = WeaponInSlot(slot);
			if (!weapon || weapon == primary || IsThrowableType(weapon.GetWeaponType()))
				continue;

			int ammo = AvailableAmmo(body, weapon);
			if (ammo <= 0)
				continue;

			if (best)
			{
				if (ammo <= bestAmmo)
					continue;
			}

			best = weapon;
			bestAmmo = ammo;
		}

		return best;
	}

	// On a clear, a garrison, or a move-and-shoot. Attack lane looks are not this.
	static bool RunningCQB(IEntity soldier)
	{
		return OrderOwnsBehavior(soldier) ||
			IsMoveFire(soldier) ||
			IsRoomFire(soldier);
	}

	// Primary when a magazine for it still has rounds, including a spare.
	// An empty magazine is not enough. A sidearm only when that primary
	// cannot be fed and the sidearm option is on. Null while he is down.
	static BaseWeaponComponent WeaponToHold(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || IsDown(body))
			return null;

		BaseWeaponComponent primary = PrimarySlotWeapon(body);
		if (!primary)
			primary = PrimaryWeapon(body);

		if (primary && AvailableAmmo(body, primary) > 0)
			return primary;

		if (!SidearmThenReleaseEnabled() || !HasBuilding(body))
			return null;

		return BestSidearm(body);
	}

	// Puts that gun in his hands. A repeat while the swap is playing waits.
	// False when there is nothing he should be holding.
	static bool EnsureHeldGun(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		BaseWeaponComponent want = WeaponToHold(body);
		if (!want)
			return false;

		if (CurrentWeapon(body) != want)
			SwitchToWeapon(body, want);

		return true;
	}

	// Primary cannot be fed, and a sidearm can. The selector holds that gun.
	static BaseWeaponComponent ForcedSidearm(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !SidearmThenReleaseEnabled() || !HasBuilding(body))
			return null;

		BaseWeaponComponent primary = PrimarySlotWeapon(body);
		if (!primary || AvailableAmmo(body, primary) > 0)
			return null;

		return BestSidearm(body);
	}

	// True when he should leave the clear or garrison. A loaded sidearm keeps
	// the order, and the hands move to it here.
	static bool NeedsAmmoRelease(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !SidearmThenReleaseEnabled())
			return false;

		CharacterControllerComponent controller = Controller(body);
		if (controller && controller.IsReloading())
			return false;

		// The swap does not take while he is down. Checked again once he is up.
		if (IsDown(body))
			return false;

		if (EnsureHeldGun(body))
			return false;

		return !HasGunAmmo(body);
	}

	// Released men stay out until gun ammo has been back for the wait.
	// Clearing the map puts them on the next fill. The order calls this
	// while it is still the active activity.
	static void PollRearmReturn(
		map<AIAgent, float> released,
		SCR_AIGroup group,
		float currentTime)
	{
		if (!released)
			return;

		if (!SidearmThenReleaseEnabled())
		{
			released.Clear();
			return;
		}

		if (released.Count() == 0)
			return;

		array<AIAgent> members = {};
		if (group)
			group.GetAgents(members);

		array<AIAgent> waiting = {};
		for (int i = 0; i < released.Count(); i++)
			waiting.Insert(released.GetKey(i));

		float delay = RearmReturnMs();
		foreach (AIAgent agent : waiting)
		{
			if (!agent || members.Find(agent) < 0)
			{
				released.Remove(agent);
				continue;
			}

			IEntity body = agent.GetControlledEntity();
			CharacterControllerComponent controller = Controller(body);
			if (!body || !controller || controller.IsDead())
			{
				released.Remove(agent);
				continue;
			}

			if (!HasGunAmmo(body))
			{
				released.Set(agent, -1);
				continue;
			}

			float seen = released.Get(agent);
			if (seen < 0)
			{
				released.Set(agent, currentTime);
				seen = currentTime;
			}

			if (currentTime - seen < delay)
				continue;

			released.Remove(agent);
			if (SCR_BaseGameMode.KK_LogEnabled())
			{
				PrintFormat(
					"KK: Unit %1 has gun ammo again, returning to the order",
					agent
				);
			}
		}
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

		soldier.m_UtilityComponent.m_LookAction.KK_Track(
			ShotAimPoint(body, enemy),
			100,
			SHOT_LOOK_DURATION
		);
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

	protected static bool SidearmThenReleaseEnabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetSidearmThenRelease();
	}

	protected static float RearmReturnMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		float seconds = 10;
		if (mode)
			seconds = mode.KK_GetRearmReturn();

		return Math.Max(seconds, 0) * 1000;
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

	protected static int MagazineAmmoInInventory(
		IEntity body,
		BaseMagazineWell well,
		IEntity skip)
	{
		if (!body || !well)
			return 0;

		SCR_InventoryStorageManagerComponent inventory =
			SCR_InventoryStorageManagerComponent.Cast(
				body.FindComponent(SCR_InventoryStorageManagerComponent)
			);
		if (!inventory)
			return 0;

		SCR_MagazinePredicate predicate = new SCR_MagazinePredicate();
		predicate.magWellType = well.Type();

		array<IEntity> found = {};
		inventory.FindItems(found, predicate, EStoragePurpose.PURPOSE_DEPOSIT);

		int best = 0;
		foreach (IEntity item : found)
		{
			if (!item || item == skip || !inventory.Contains(item))
				continue;

			BaseMagazineComponent magazine = BaseMagazineComponent.Cast(
				item.FindComponent(BaseMagazineComponent)
			);
			if (!magazine || magazine.GetAmmoCount() <= best)
				continue;

			best = magazine.GetAmmoCount();
		}

		return best;
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

		// The trigger is a cook on a frag. Get a gun back before it.
		if (HoldingThrowable(body))
		{
			BaseWeaponComponent sidearm = ForcedSidearm(body);
			if (sidearm)
				SwitchToWeapon(body, sidearm);
			else
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
		bool holdReload = !fire && IsQuietReload(body);
		if (!holdReload)
			RequestWeaponStance(body, raised);

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

	// Alerted or threatened, or a living enemy already selected. A wall
	// between them still counts. The sight trace is a separate question.
	static bool IsAware(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (FeelsThreatened(utility))
			return true;

		if (!utility || !utility.m_CombatComponent)
			return false;

		return IsLivingTarget(utility.m_CombatComponent.GetCurrentTarget());
	}

	// A clear or a garrison keeps the route. Threat hide, retreat, and
	// observe would plant him once he knows someone is there.
	static bool ClearerPushing(SCR_AIUtilityComponent utility)
	{
		if (!utility || !UseRoomCombat())
			return false;

		return OrderOwnsBehavior(utility.m_OwnerEntity) ||
			OrderOwnsBehavior(utility.GetOwner());
	}

	// Set the moment a clear or garrison is selected, before a route exists.
	protected static ref set<IEntity> s_OrderBehavior = new set<IEntity>();

	// Clearing a house, or holding one. Attack uses neither of these.
	static bool OnHouseOrder(IEntity soldier)
	{
		return IsClearing(soldier) || HasBuilding(soldier);
	}

	// The clear or garrison owns movement, aim, and the rifle. Perception
	// still runs. This is on outside the building too.
	static void SetOrderBehavior(IEntity soldier, bool active)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (active)
		{
			s_OrderBehavior.Insert(body);
			LockClearMove(body, true);
			return;
		}

		s_OrderBehavior.RemoveItem(body);
		LockClearMove(body, false);
	}

	static bool OrderOwnsBehavior(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || IsDown(body))
			return false;

		return s_OrderBehavior.Contains(body) || OnHouseOrder(body);
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

	static float ShotDelayMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 150;

		return mode.KK_GetShotDelay();
	}

	// The scenario value is a regular soldier. Vanilla does not scale its
	// first-shot wait by skill (that wait is threat, then distance). Skill
	// there only sets the aim-error sigma, and these are that same ratio.
	static float ShotDelayFor(IEntity soldier)
	{
		float delay = ShotDelayMs();
		if (delay <= 0 || !ShotDelaySkillEnabled())
			return delay;

		IEntity body = CharacterBody(soldier);
		if (!body)
			return delay;

		return delay * SkillDelayFactor(SnapSkill(body));
	}

	protected static bool ShotDelaySkillEnabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetShotDelaySkill();
	}

	protected static float SkillDelayFactor(EAISkill skill)
	{
		switch (skill)
		{
			case EAISkill.NOOB:
				return 4;
			case EAISkill.ROOKIE:
				return 2;
			case EAISkill.VETERAN:
				return 0.5;
			case EAISkill.EXPERT:
				return 0.25;
			case EAISkill.CYLON:
				return 0;
		}

		return 1;
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
			if (shot && IsTargetable(shot) && PositionInside(building, shot.GetOrigin()))
				return true;
		}

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatComponent)
			return false;

		BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
		if (!target)
			return false;

		IEntity enemy = target.GetTargetEntity();
		if (!enemy || !IsTargetable(enemy))
			return false;

		return PositionInside(building, enemy.GetOrigin());
	}

	// How far a clear or garrison plants and shoots. Attack waypoints
	// keep ATTACK_CONTACT_RANGE.
	static float OrderContactRange()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 50;

		return mode.KK_GetRoomCombatRange();
	}

	// The circle around the building's footprint, plus the CQB range.
	static bool WithinBuildingReach(IEntity building, vector worldPosition)
	{
		if (!building)
			return false;

		if (PositionInside(building, worldPosition))
			return true;

		vector mins;
		vector maxs;
		building.GetBounds(mins, maxs);
		vector center = building.CoordToParent((mins + maxs) * 0.5);
		float dx = (maxs[0] - mins[0]) * 0.5;
		float dz = (maxs[2] - mins[2]) * 0.5;
		float reach = Math.Sqrt(dx * dx + dz * dz) + OrderContactRange();
		vector flat = worldPosition - center;
		flat[1] = 0;
		return flat.Length() <= reach;
	}

	// The whole clear or garrison, including the walk in. The attack
	// behavior can still stop him, but it does not decide the shot.
	static bool OwnsShot(IEntity soldier)
	{
		if (!UseRoomCombat())
			return false;

		return OrderOwnsBehavior(soldier);
	}

	// Below zero means this shot is not ours. Otherwise 0 is a quiet contact
	// and 1 is threatened, or a target that is endangering him right now.
	static float AimThreat(IEntity soldier)
	{
		if (!OwnsShot(soldier))
			return -1;

		IEntity body = CharacterBody(soldier);
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_ThreatSystem)
			return 0;

		float threat = utility.m_ThreatSystem.GetThreatMeasure();
		if (threat < 0)
			threat = 0;
		if (threat > 1)
			threat = 1;

		if (!utility.m_CombatComponent)
			return threat;

		BaseTarget current = utility.m_CombatComponent.GetCurrentTarget();
		if (current && current.IsEndangering())
			return 1;

		return threat;
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

		// Unconscious still owns the order. The shot, the lean, and the snap
		// do not. A body on the ground was being turned onto the target.
		if (IsDown(body))
		{
			AbortShot(utility, body);
			return;
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

		if (!HasGunAmmo(body))
		{
			AbortShot(utility, body);
			return;
		}

		EnsureHeldGun(body);

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

		if (
			!s_ShotLive.Contains(body) &&
			selected &&
			SeesTarget(body, selected)
		)
		{
			IEntity selectedEnemy = selected.GetTargetEntity();
			if (selectedEnemy && !PastOrderContact(body, selectedEnemy))
				ConsiderSelected(body, selected);
		}

		if (!s_ShotLive.Contains(body))
		{
			ClearAim(body);
			ClearSnapAim(body);
			RememberLean(body, 0);
			// Past CQB range the normal attack aims and sets the trigger.
			// Forcing either here cuts that burst off.
			if (DistantBaseFight(body))
			{
				YieldLookToAttack(body);
				return;
			}

			bool raised = FeelsThreatened(utility);
			if (ClearWeaponStaysUp(body))
				raised = true;
			CommandWeapon(body, raised, false);
			// No room target. A base look left running turns against the
			// hold facing.
			CancelLook(utility, body);
			return;
		}

		IEntity enemy = null;
		if (s_ShotLook.Contains(body))
			enemy = s_ShotLook.Get(body);

		if (enemy && (!IsTargetable(enemy) || !IsHostile(body, enemy)))
		{
			ClearShot(body);
			CancelLook(utility, body);
			return;
		}

		float lean = 0;
		if (s_ShotLean.Contains(body))
			lean = s_ShotLean.Get(body);

		bool center = ShotStillVisible(body);
		bool canShoot = center;
		if (!canShoot && lean != 0)
			canShoot = SideStillClear(body, enemy, lean);

		// The selected man is who the plant and the aim are holding.
		// The shot record can be a side peek that the second trace drops,
		// and that left him aimed with the trigger off.
		if (!canShoot && selected && SeesTarget(body, selected))
		{
			IEntity seen = selected.GetTargetEntity();
			if (seen && IsTargetable(seen) && !PastOrderContact(body, seen))
			{
				enemy = seen;
				canShoot = true;
			}
		}

		float command = 0;
		if (canShoot)
			command = lean;

		RememberLean(body, command);
		if (canShoot && enemy)
			NoteNoDamage(body, enemy);

		bool fire = AimReady(body, enemy, canShoot);
		bool raised = canShoot || FeelsThreatened(utility);
		if (ClearWeaponStaysUp(body))
			raised = true;
		CommandWeapon(body, raised, fire);
		if (!fire)
			SetFireWanted(body, false);

		// The trace blocks the trigger. While he is alive the rifle stays
		// on his eyes. With nobody left the look is dropped, so it does
		// not turn against the hold facing.
		if (enemy && IsTargetable(enemy))
			AimLook(utility, body, enemy);
		else
			CancelLook(utility, body);
	}

	static void SetClearing(IEntity soldier, bool clearing)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (clearing)
		{
			s_Clearing.Insert(body);
			// The route is the move. A combat move is CRX's strafe at
			// wherever it thinks the threat is, so it is refused for the
			// whole clear.
			LockClearMove(body, true);
			return;
		}

		if (!s_Clearing.Contains(body))
			return;

		s_Clearing.RemoveItem(body);
		LockClearMove(body, false);
		ClearPathLook(body);
		// The clear lowered him on the way out. Forget the raise, or the
		// next order thinks the rifle is still up and never sends it.
		s_MoveWeaponKnown.RemoveItem(body);
		s_MoveWeaponUp.RemoveItem(body);
		s_RaiseSettled.RemoveItem(body);
	}

	static bool IsClearing(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_Clearing.Contains(body);
	}

	static void SetAdvancing(IEntity soldier, bool advancing)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (advancing)
		{
			s_Advancing.Insert(body);
			SetIgnoringTargets(body, false);
			return;
		}

		if (!s_Advancing.Contains(body))
			return;

		s_Advancing.RemoveItem(body);
		// The order lowered him on the way out. Forget the raise, or the
		// next order thinks the rifle is still up and never sends it.
		s_MoveWeaponKnown.RemoveItem(body);
		s_MoveWeaponUp.RemoveItem(body);
		s_RaiseSettled.RemoveItem(body);
	}

	static bool IsAdvancing(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_Advancing.Contains(body);
	}

	// True for every soldier still on an attack, including a bound sprint.
	// Clearing it refreshes perception, so the alerted rate does not stick.
	// Restore the combat multiplier before clearing, or that refresh scales
	// the vanilla rate by the attack multiplier.
	static void SetAttackSearch(IEntity soldier, bool searching)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		bool active = s_AttackSearch.Contains(body);
		if (searching == active)
			return;

		if (searching)
		{
			s_AttackSearch.Insert(body);
			return;
		}

		s_AttackSearch.RemoveItem(body);
		RefreshSearchPerception(body);
	}

	static bool IsAttackSearch(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_AttackSearch.Contains(body);
	}

	protected static void RefreshSearchPerception(IEntity body)
	{
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (
			!utility ||
			!utility.m_CombatComponent ||
			!utility.m_PerceptionComponent ||
			!utility.m_ThreatSystem
		)
		{
			return;
		}

		utility.m_CombatComponent.UpdatePerceptionFactor(
			utility.m_PerceptionComponent,
			utility.m_ThreatSystem
		);
	}

	static void SetBoundSprint(IEntity soldier, bool sprinting)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (sprinting)
		{
			s_BoundSprint.Insert(body);
			// The fight may already be walking him at the enemy. A sprint
			// on top of that request runs the same way, off the waypoint.
			LockSprintMove(body, true);
			ReleaseCombatAim(body);
			return;
		}

		bool wasSprinting = s_BoundSprint.Contains(body);
		s_BoundSprint.RemoveItem(body);
		if (wasSprinting)
			LockSprintMove(body, false);
		// The lane look outranks a man. Leave it up and the hold cannot
		// turn onto a target.
		if (wasSprinting)
			DropSprintLook(body);
	}

	// While this is set, that combat move state will not take a request and
	// will not aim. The bound is the move. A strafe at the enemy is not.
	static void LockSprintMove(IEntity soldier, bool locked)
	{
		SCR_AIUtilityComponent utility = UtilityOf(soldier);
		if (!utility || !utility.m_CombatMoveState)
			return;

		if (locked)
			s_SprintMoveLock.Insert(utility.m_CombatMoveState);
		else
			s_SprintMoveLock.RemoveItem(utility.m_CombatMoveState);
	}

	static bool IsSprintMoveLocked(SCR_AICombatMoveState state)
	{
		return state && s_SprintMoveLock.Contains(state);
	}

	// Refuses the next combat move for a clearer. An already running one
	// is dropped once, when the lock goes on, and not on later ticks.
	static void LockClearMove(IEntity soldier, bool locked)
	{
		SCR_AIUtilityComponent utility = UtilityOf(soldier);
		if (!utility || !utility.m_CombatMoveState)
			return;

		SCR_AICombatMoveState state = utility.m_CombatMoveState;
		if (locked)
		{
			bool already = s_ClearMoveLock.Contains(state);
			s_ClearMoveLock.Insert(state);
			if (!already && state.IsExecutingRequest())
				state.CancelRequest();
			return;
		}

		s_ClearMoveLock.RemoveItem(state);
	}

	static bool IsClearMoveLocked(SCR_AICombatMoveState state)
	{
		return state && s_ClearMoveLock.Contains(state);
	}

	// Drops a combat move that is still steering him at a target. The sprint
	// has to follow the order, and an aimed step will not take that sprint.
	static void ReleaseCombatAim(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility)
			return;

		if (!utility.m_CombatMoveState)
			return;

		if (utility.m_CombatMoveState.IsExecutingRequest())
			utility.m_CombatMoveState.CancelRequest();

		utility.m_CombatMoveState.EnableAiming(false);
	}

	protected static void DropSprintLook(IEntity body)
	{
		s_SprintLook.Remove(body);

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_LookAction)
			return;

		utility.m_LookAction.Cancel();
	}

	static void SetSprintLook(IEntity soldier, vector look)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		s_SprintLook.Set(body, look);
	}

	static bool GetSprintLook(IEntity soldier, out vector look)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_SprintLook.Contains(body))
			return false;

		look = s_SprintLook.Get(body);
		return true;
	}

	static void SetPathLook(IEntity soldier, vector look)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || look == vector.Zero)
			return;

		s_PathLook.Set(body, look);
		RefreshPathLook(body);
	}

	// Remembers the route point while a contact look is using the head.
	// Refreshing it here would turn him back off the man.
	static void HoldPathLook(IEntity soldier, vector look)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || look == vector.Zero)
			return;

		s_PathLook.Set(body, look);
	}

	static void ClearPathLook(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		ForgetLostLook(body);
		if (!body || !s_PathLook.Contains(body))
			return;

		s_PathLook.Remove(body);

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_LookAction)
			return;

		utility.m_LookAction.Cancel();
	}

	static bool GetPathLook(IEntity soldier, out vector look)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_PathLook.Contains(body))
			return false;

		look = s_PathLook.Get(body);
		return look != vector.Zero;
	}

	static bool PathLookActive(SCR_AIUtilityComponent utility)
	{
		if (!utility)
			return false;

		vector look;
		return GetPathLook(utility.m_OwnerEntity, look) ||
			GetPathLook(utility.GetOwner(), look);
	}

	static bool IsPathLookWrite()
	{
		return s_bPathLookWrite;
	}

	// A living enemy this soldier can actually see. Last-seen points and
	// a hot sector do not count. The plant uses the same sight.
	static bool EngagingContact(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		if (ShotStillVisible(body) && s_ShotLook.Contains(body))
		{
			IEntity shot = s_ShotLook.Get(body);
			if (shot && !PastOrderContact(body, shot))
				return true;
		}

		if (HasVisibleEnemy(body))
		{
			IEntity seen = null;
			SCR_AIUtilityComponent utility = UtilityOf(body);
			if (utility && utility.m_CombatComponent)
			{
				BaseTarget current = utility.m_CombatComponent.GetCurrentTarget();
				if (current)
					seen = current.GetTargetEntity();
			}

			if (seen && !PastOrderContact(body, seen))
				return true;
		}

		float range = ATTACK_CONTACT_RANGE;
		if (UseRoomCombat() && OrderOwnsBehavior(body))
			range = OrderContactRange();

		return VisibleContact(body, range) != null;
	}

	// Points the rifle at that man and reports the aim point. The route
	// point stays stored. After the sight line breaks, the last place
	// he was seen holds the rifle until the lost-contact time runs out.
	static bool TakeContactLook(IEntity soldier, out vector aim)
	{
		aim = vector.Zero;

		IEntity body = CharacterBody(soldier);
		if (!body || IsDown(body) || !HasGunAmmo(body))
			return false;

		if (!EngagingContact(body))
			return LostContactLook(body, aim);

		IEntity enemy = null;
		if (ShotStillVisible(body) && s_ShotLook.Contains(body))
			enemy = s_ShotLook.Get(body);

		if (!enemy)
		{
			SCR_AIUtilityComponent utility = UtilityOf(body);
			if (utility && utility.m_CombatComponent)
			{
				BaseTarget selected = utility.m_CombatComponent.GetCurrentTarget();
				if (SeesTarget(body, selected))
					enemy = selected.GetTargetEntity();
			}
		}

		if (!enemy || !IsTargetable(enemy))
			enemy = VisibleContact(body);

		if (!enemy || !IsTargetable(enemy) || !IsHostile(body, enemy))
			return false;

		// The route point stays stored, so CRX still cannot write a look.
		// This snap moves that look onto his eyes. When the trace breaks,
		// the route point is put back.
		aim = ShotAimPoint(body, enemy);
		RememberLostLook(body, aim);
		SnapLook(body, aim);
		return true;
	}

	protected static void RememberLostLook(IEntity body, vector aim)
	{
		if (!body || aim == vector.Zero)
			return;

		s_LostLook.Set(body, aim);
		s_LostLookAt.Set(body, WorldTime());
	}

	protected static void ForgetLostLook(IEntity body)
	{
		if (!body)
			return;

		s_LostLook.Remove(body);
		s_LostLookAt.Remove(body);
	}

	// Seconds the rifle stays on the last seen point. 0 looks away at once.
	static float LostContactLookSeconds()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 2;

		return mode.KK_GetLostContactLook();
	}

	protected static bool LostContactLook(IEntity body, out vector aim)
	{
		aim = vector.Zero;
		if (!body || !s_LostLook.Contains(body) || !s_LostLookAt.Contains(body))
			return false;

		float holdMs = LostContactLookSeconds() * 1000;
		if (holdMs <= 0 || WorldTime() - s_LostLookAt.Get(body) > holdMs)
		{
			ForgetLostLook(body);
			return false;
		}

		aim = s_LostLook.Get(body);
		SnapLook(body, aim);
		return true;
	}

	protected static void SnapLook(IEntity body, vector aim)
	{
		if (!body || aim == vector.Zero)
			return;

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_LookAction)
			return;

		s_bPathLookWrite = true;
		utility.m_LookAction.KK_Snap(aim, PATH_LOOK_PRIORITY, SHOT_LOOK_DURATION);
		s_bPathLookWrite = false;
		SnapAimPose(body, aim);
	}

	// Puts the stored node back onto the head. Same priority only moves
	// the point, so the turn he already started is not restarted.
	static void RefreshPathLook(IEntity soldier)
	{
		vector look;
		if (!GetPathLook(soldier, look))
			return;

		IEntity body = CharacterBody(soldier);
		if (!body || SprintBeforeReload(body) || IsReloadBashing(body))
			return;

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_LookAction)
			return;

		s_bPathLookWrite = true;
		utility.m_LookAction.KK_Snap(look, PATH_LOOK_PRIORITY, 30);
		s_bPathLookWrite = false;
	}

	// The man he was already on. Later frames overwrite this while the
	// selector still has him. An empty frame does not erase it.
	static void RememberSprintEnemy(IEntity soldier, BaseTarget target)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !IsLivingTarget(target))
			return;

		s_SprintKnown.Set(body, target);
	}

	static BaseTarget SprintKnown(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_SprintKnown.Contains(body))
			return null;

		return s_SprintKnown.Get(body);
	}

	static void ForgetSprintEnemy(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		s_SprintKnown.Remove(body);
	}

	// The sprint lowered the rifle on its own. The hold has to forget that
	// pose, or it still believes the rifle is down and never sends the raise.
	static void RaiseAfterSprint(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		SetBoundSprint(body, false);
		SetAdvancing(body, true);
		s_MoveWeaponKnown.RemoveItem(body);
		s_MoveWeaponUp.RemoveItem(body);
		s_RaiseSettled.RemoveItem(body);
		RequestWeaponStance(body, true);
	}

	static bool IsBoundSprint(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_BoundSprint.Contains(body);
	}

	static void SetRecalled(IEntity soldier, bool recalled)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (recalled)
		{
			s_Recalled.Insert(body);
			return;
		}

		s_Recalled.RemoveItem(body);
	}

	static bool IsRecalled(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_Recalled.Contains(body);
	}

	// A clear or garrison holds the rifle up, including the walk in.
	// These are the lowers that still win: the reload sprint, a bash,
	// an empty gun, a reload already playing, and a frag in hand.
	static bool ClearWeaponStaysUp(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!UseRoomCombat() || !OrderOwnsBehavior(body))
			return false;

		if (IsDown(body))
			return false;

		if (IsIgnoringTargets(body))
			return false;

		if (SprintBeforeReload(body) || IsReloadBashing(body))
			return false;

		if (CannotShoot(body) || IsQuietReload(body))
			return false;

		if (HoldingThrowable(body))
			return false;

		return true;
	}

	// An attack holds the rifle up the same way, without the clear's
	// move-and-shoot. A reload, an empty gun, and a grenade still lower it.
	static bool AttackWeaponStaysUp(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!IsAdvancing(body) || IsDown(body))
			return false;

		// A bound sprint lowers the rifle so he can run. Walk and jog stay up.
		if (IsBoundSprint(body))
			return false;

		if (IsIgnoringTargets(body))
			return false;

		if (SprintBeforeReload(body) || IsReloadBashing(body))
			return false;

		if (CannotShoot(body) || IsQuietReload(body))
			return false;

		if (HoldingThrowable(body))
			return false;

		return true;
	}

	// The raise node runs after the controller. While a clear or an attack
	// wants the rifle up, that node is held off. The controller is touched
	// only when the rifle is actually down after having been up, because
	// repeating the raise restarts it and cuts off the step.
	static bool KeepClearWeaponRaised(IEntity soldier)
	{
		if (!ClearWeaponStaysUp(soldier) && !AttackWeaponStaysUp(soldier))
			return false;

		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		RequestWeaponStance(body, true);
		return true;
	}

	// True when a raise or lower still has to be sent. A raise that has
	// not finished still reports down, and sending it again restarts it.
	static bool NeedsWeaponStance(IEntity soldier, bool raised)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		bool known = s_MoveWeaponKnown.Contains(body);
		bool wasRaised = s_MoveWeaponUp.Contains(body);
		bool liveRaised = WeaponIsRaised(body);
		if (raised && liveRaised)
		{
			s_RaiseSettled.Insert(body);
			s_MoveWeaponKnown.Insert(body);
			s_MoveWeaponUp.Insert(body);
			return false;
		}

		// Snap aim was making the raised flag flicker. Sending the raise
		// again restarts it, and the shot never leaves the rifle.
		if (raised && !liveRaised && s_RaiseSentAt.Contains(body))
		{
			if (WorldTime() - s_RaiseSentAt.Get(body) < 500)
				return false;
		}

		if (!known || wasRaised != raised)
			return true;

		if (liveRaised == raised)
			return false;

		// Already asked for it up. Wait until that pose has been seen,
		// then one more raise if a later node put the rifle down.
		if (raised)
			return s_RaiseSettled.Contains(body);

		return true;
	}

	// Sends the stance when it is still due. The clear uses the result
	// to decide whether a weapon order has to go out with the move.
	static bool RequestClearRaise(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		return RequestWeaponStance(body, true);
	}

	protected static bool RequestWeaponStance(IEntity body, bool raised)
	{
		if (!NeedsWeaponStance(body, raised))
			return false;

		s_MoveWeaponKnown.Insert(body);
		if (raised)
		{
			if (!WeaponIsRaised(body))
				s_RaiseSettled.RemoveItem(body);
			s_MoveWeaponUp.Insert(body);
		}
		else
		{
			s_MoveWeaponUp.RemoveItem(body);
			s_RaiseSettled.RemoveItem(body);
		}

		SetWeaponRaised(body, raised);
		if (raised)
			s_RaiseSentAt.Set(body, WorldTime());
		return true;
	}

	static bool WeaponIsRaised(IEntity soldier)
	{
		CharacterControllerComponent controller = Controller(CharacterBody(soldier));
		return controller && controller.IsWeaponRaised();
	}

	static void SetGarrisonBuilding(IEntity soldier, IEntity building)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (!building)
		{
			ClearPathLook(body);
			LockClearMove(body, false);
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
			ReleaseStaleAim(body);
			ReleaseLatentCombat(body);
			DropStaleAlert(body);
			return;
		}

		s_Buildings.Set(body, building);
		// Same refusal as a clear. A garrison that is still an attack
		// soldier is not in this map.
		if (!IsAdvancing(body))
			LockClearMove(body, true);
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

		if (!body || !s_ShotLive.Contains(body))
			return false;

		// The route stays free while he is walking off a blocked shot.
		return !BarrelWantsMove(body);
	}

	// The selected target is one enemy. Inside, check every perceived enemy
	// and anyone hostile in the room or visible through glass, then keep
	// the nearest one a ray can reach.
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
		IEntity previousLook = null;
		float previousLean = 0;
		if (s_ShotLook.Contains(body))
		{
			previousLook = s_ShotLook.Get(body);
			if (s_ShotLean.Contains(body))
				previousLean = s_ShotLean.Get(body);
		}

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
			// Outside, the route still owns the feet. Anyone he can see
			// is still a shot, including a man behind glass.
			if (UseRoomCombat() && OrderOwnsBehavior(body))
				ConsiderOpen(body, perception, true);
			// A jog has no building, and the selected target may be empty.
			// Reaper often keeps a sector there instead of a body. The
			// perceived enemies are still the ones he can shoot.
			else if (IsMoveFire(body))
				ConsiderOpen(body, perception, false);

			if (!s_ShotLive.Contains(body))
				ConsiderSelected(body, selected);

			if (IsMoveFire(body))
				HoldMoveShot(body, now, previousLook, previousLean);

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
		s_CandidateKept.Clear();
		ConsiderIndoor(perception, body, building, ETargetCategory.ENEMY, true);
		ConsiderIndoor(perception, body, building, ETargetCategory.DETECTED, true);
		ConsiderIndoor(perception, body, building, ETargetCategory.UNKNOWN, true);
		CollectRoom(body, building);
		CollectThroughGlass(body);

		if (CommitNearestShot(body, perception))
		{
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
		if (!enemy || !IsTargetable(enemy) || !IsHostile(body, enemy))
			return;

		if (PastOrderContact(body, enemy))
			return;

		s_ShotLive.Insert(body);
		s_ShotBase.Set(body, selected);
		s_ShotLook.Set(body, enemy);
		float lean;
		CanEngage(body, enemy, lean);
		s_ShotLean.Set(body, lean);
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

	// Drops the room shot and the snap. Used when he is down or no gun
	// he is carrying can fire. The order itself is released separately.
	protected static void AbortShot(SCR_AIUtilityComponent utility, IEntity body)
	{
		if (!body)
			return;

		ClearShot(body);
		RememberLean(body, 0);
		ReleaseLean(body);
		CancelLook(utility, body);
		SetFireWanted(body, false);
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
		s_MoveShotSeen.Remove(body);
		ForgetBarrelBlock(body);
		s_RoomFire.RemoveItem(body);
		ClearAim(body);
		ReleaseLean(body);

		if (wasRoom && !IsMoveFire(body))
		{
			SetFireWanted(body, false);
			s_MoveFiring.RemoveItem(body);
		}
	}

	// The moving ray misses for a few checks and then finds him again. Dropping
	// the enemy on the first miss releases the trigger before the gun fires.
	protected static void HoldMoveShot(
		IEntity body,
		float now,
		IEntity previousLook,
		float previousLean)
	{
		if (!body)
			return;

		if (s_ShotLive.Contains(body))
		{
			s_MoveShotSeen.Set(body, now);
			return;
		}

		if (!previousLook || !IsTargetable(previousLook))
			return;

		float seen = 0;
		if (s_MoveShotSeen.Contains(body))
			seen = s_MoveShotSeen.Get(body);

		if (now - seen > MOVE_SHOT_HOLD_MS)
			return;

		s_ShotLive.Insert(body);
		s_ShotLook.Set(body, previousLook);
		s_ShotLean.Set(body, previousLean);
	}

	protected static void ConsiderOpen(
		IEntity body,
		PerceptionComponent perception,
		bool enemiesMustBeHostile)
	{
		if (!body)
			return;

		if (!perception)
		{
			perception = PerceptionComponent.Cast(
				body.FindComponent(PerceptionComponent)
			);
		}

		s_Candidates.Clear();
		s_CandidateDist.Clear();
		s_CandidateKnown.Clear();
		s_CandidateKept.Clear();
		ConsiderOpenCategory(
			perception,
			body,
			ETargetCategory.ENEMY,
			enemiesMustBeHostile
		);
		ConsiderOpenCategory(perception, body, ETargetCategory.DETECTED, true);
		ConsiderOpenCategory(perception, body, ETargetCategory.UNKNOWN, true);
		CollectThroughGlass(body);
		CommitNearestShot(body, perception);
	}

	protected static void ConsiderOpenCategory(
		PerceptionComponent perception,
		IEntity body,
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

			if (!IsTargetable(character))
				continue;

			if (requireHostile && !IsHostile(body, character))
				continue;

			AddCandidate(
				character,
				vector.Distance(body.GetOrigin(), character.GetOrigin()),
				candidate
			);
		}
	}

	protected static bool CommitNearestShot(
		IEntity body,
		PerceptionComponent perception)
	{
		for (int i = 0; i < s_Candidates.Count(); i++)
		{
			IEntity enemy = s_Candidates[i];
			if (!enemy)
				continue;

			float lean;
			if (PastOrderContact(body, enemy))
				continue;

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

			return true;
		}

		return false;
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

			if (!IsTargetable(character))
				continue;

			if (requireHostile && !IsHostile(body, character))
				continue;

			float distance = vector.Distance(
				body.GetOrigin(),
				character.GetOrigin()
			);
			// A man outside the shell is still a shot when he is in range.
			// The sight trace is what decides the window.
			if (
				!PositionInside(building, character.GetOrigin()) &&
				distance > OrderContactRange()
			)
			{
				continue;
			}

			AddCandidate(character, distance, candidate);
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

		if (!IsTargetable(character) || !IsHostile(s_ScanBody, character))
			return true;

		// A downed man sinks. His head is the place to test, and the
		// standing capsule is often still inside the shell.
		vector place = character.GetOrigin();
		if (IsDown(character))
		{
			vector eye = character.EyePosition();
			if (eye != vector.Zero)
				place = eye;
		}

		if (
			!PositionInside(s_ScanBuilding, place) &&
			!PositionInside(s_ScanBuilding, character.GetOrigin())
		)
		{
			return true;
		}

		AddCandidate(
			character,
			vector.Distance(s_ScanBody.GetOrigin(), character.GetOrigin()),
			null
		);
		return true;
	}

	// Perception does not register a man behind glass. The house order
	// still looks, and a clear pane is enough to take the shot.
	protected static void CollectThroughGlass(IEntity body)
	{
		if (!body || !OnGlassWatch(body))
			return;

		float range = OrderContactRange();
		if (range < 1)
			return;

		FillCloseBodies(body, range);
		int kept = 0;
		for (int i = 0; i < s_CloseBodies.Count(); i++)
		{
			if (kept >= GLASS_CHECKS)
				break;

			IEntity enemy = s_CloseBodies[i];
			if (!enemy || s_Candidates.Find(enemy) >= 0)
				continue;

			if (!CanSeeEntity(body, enemy))
				continue;

			AddCandidate(enemy, s_CloseDist[i], null, true);
			kept++;
		}
	}

	protected static bool OnGlassWatch(IEntity body)
	{
		return OrderOwnsBehavior(body) || OnHouseOrder(body);
	}

	protected static void FillCloseBodies(IEntity body, float range)
	{
		s_CloseBodies.Clear();
		s_CloseDist.Clear();
		if (!body || range < 1)
			return;

		GatherBodies(body, range);
		foreach (IEntity enemy : s_NearBodies)
		{
			if (!enemy || enemy == body)
				continue;

			if (!IsTargetable(enemy) || !IsHostile(body, enemy))
				continue;

			float distance = vector.Distance(body.GetOrigin(), enemy.GetOrigin());
			if (distance > range)
				continue;

			NoteClose(enemy, distance, GLASS_CHECKS);
		}
	}

	protected static void GatherBodies(IEntity body, float radius)
	{
		s_NearBodies.Clear();
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			world = body.GetWorld();

		if (!world)
			return;

		s_NearViewer = body;
		world.QueryEntitiesBySphere(
			body.GetOrigin(),
			radius,
			OnNearBody,
			null,
			EQueryEntitiesFlags.DYNAMIC | EQueryEntitiesFlags.WITH_OBJECT
		);
		s_NearViewer = null;
	}

	protected static bool OnNearBody(IEntity entity)
	{
		ChimeraCharacter character = CharacterOf(entity);
		if (!character || character == s_NearViewer)
			return true;

		if (s_NearBodies.Contains(character))
			return true;

		s_NearBodies.Insert(character);
		return true;
	}

	protected static void NoteClose(IEntity enemy, float distance, int limit)
	{
		if (!enemy || limit < 1)
			return;

		int index = s_CloseBodies.Count();
		for (int i = 0; i < s_CloseBodies.Count(); i++)
		{
			if (distance < s_CloseDist[i])
			{
				index = i;
				break;
			}
		}

		if (index >= limit)
			return;

		s_CloseBodies.InsertAt(enemy, index);
		s_CloseDist.InsertAt(distance, index);
		while (s_CloseBodies.Count() > limit)
		{
			int last = s_CloseBodies.Count() - 1;
			s_CloseBodies.Remove(last);
			s_CloseDist.Remove(last);
		}
	}

	// Nearest first, and only a few of them. The sight rays then stop at the
	// first one that is actually clear.
	protected static void AddCandidate(
		IEntity enemy,
		float distance,
		BaseTarget target,
		bool keep = false)
	{
		if (!enemy)
			return;

		int existing = s_Candidates.Find(enemy);
		if (existing >= 0)
		{
			if (target && !s_CandidateKnown.Contains(enemy))
				s_CandidateKnown.Set(enemy, target);

			if (keep)
				s_CandidateKept.Insert(enemy);

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

		if (!keep && index >= SHOT_CANDIDATES)
			return;

		s_Candidates.InsertAt(enemy, index);
		s_CandidateDist.InsertAt(distance, index);
		if (target)
			s_CandidateKnown.Set(enemy, target);

		if (keep)
			s_CandidateKept.Insert(enemy);

		while (s_Candidates.Count() > SHOT_CANDIDATES)
		{
			int drop = s_Candidates.Count() - 1;
			if (s_CandidateKept.Contains(s_Candidates[drop]))
			{
				drop = -1;
				for (int i = s_Candidates.Count() - 1; i >= 0; i--)
				{
					if (!s_CandidateKept.Contains(s_Candidates[i]))
					{
						drop = i;
						break;
					}
				}

				if (drop < 0)
					break;
			}

			IEntity dropped = s_Candidates[drop];
			s_Candidates.Remove(drop);
			s_CandidateDist.Remove(drop);
			s_CandidateKnown.Remove(dropped);
			s_CandidateKept.RemoveItem(dropped);
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

	protected static bool SameCharacter(IEntity a, IEntity b)
	{
		if (!a || !b)
			return false;

		if (a == b)
			return true;

		ChimeraCharacter left = CharacterOf(a);
		ChimeraCharacter right = CharacterOf(b);
		return left && left == right;
	}

	// Dead and unconscious are not a standing fight. Vanilla combat leaves both alone.
	protected static bool IsLiving(IEntity character)
	{
		CharacterControllerComponent controller = Controller(character);
		if (!controller || controller.IsDead() || controller.IsUnconscious())
			return false;

		return controller.GetLifeState() == ECharacterLifeState.ALIVE;
	}

	// Still a body to shoot. Unconscious and incapacitated count. Dead does not.
	protected static bool IsTargetable(IEntity character)
	{
		CharacterControllerComponent controller = Controller(character);
		if (!controller || controller.IsDead())
			return false;

		if (controller.IsUnconscious())
			return true;

		ECharacterLifeState life = controller.GetLifeState();
		return life == ECharacterLifeState.ALIVE ||
			life == ECharacterLifeState.INCAPACITATED;
	}

	protected static bool IsHostile(IEntity self, IEntity other)
	{
		if (!self || !other || self == other)
			return false;

		if (SharesGroup(self, other))
			return false;

		SCR_Faction mine = SCR_Faction.Cast(FactionOf(self));
		SCR_Faction theirs = SCR_Faction.Cast(FactionOf(other));
		if (!mine || !theirs)
			return false;

		return mine.IsFactionEnemy(theirs);
	}

	// The squad clearing the house, including the player's slave group.
	protected static bool SharesGroup(IEntity self, IEntity other)
	{
		AIGroup mine = GroupOf(self);
		AIGroup theirs = GroupOf(other);
		if (!mine || !theirs)
			return false;

		if (mine == theirs)
			return true;

		SCR_AIGroup myGroup = SCR_AIGroup.Cast(mine);
		SCR_AIGroup theirGroup = SCR_AIGroup.Cast(theirs);
		if (!myGroup || !theirGroup)
			return false;

		return myGroup.GetSlave() == theirGroup || theirGroup.GetSlave() == myGroup;
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

	// A clear or garrison does not plant or use the room gun past CQB range.
	// Attack waypoints are not in this. On a garrison, a man past that
	// range is a normal attack instead, and he does not get snap aim.
	// A clear leaves him alone.
	static bool PastOrderContact(IEntity body, IEntity enemy)
	{
		if (!body || !enemy)
			return false;

		if (!UseRoomCombat() || !OrderOwnsBehavior(body))
			return false;

		return vector.Distance(body.GetOrigin(), enemy.GetOrigin()) > OrderContactRange();
	}

	// Garrison only. The room gun has nobody it will shoot, and the
	// selected man is past CQB range and in sight. The normal attack
	// then aims and fires. The route still owns his feet. A clear does
	// not take this.
	static bool DistantBaseFight(IEntity soldier)
	{
		if (!UseRoomCombat() || !OrderOwnsBehavior(soldier) || IsClearing(soldier))
			return false;

		// Zero range does not shoot on sight, including through the normal attack.
		if (OrderContactRange() <= 0)
			return false;

		IEntity body = CharacterBody(soldier);
		if (!body || IsDown(body) || CannotShoot(body))
			return false;

		if (s_ShotLive.Contains(body))
			return false;

		if (VisibleContact(body, OrderContactRange()))
			return false;

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatComponent)
			return false;

		BaseTarget current = utility.m_CombatComponent.GetCurrentTarget();
		if (!current || !IsLivingTarget(current))
			return false;

		IEntity enemy = current.GetTargetEntity();
		if (!enemy || !CanSeeEntity(body, enemy))
			return false;

		return PastOrderContact(body, enemy);
	}

	// Drops the route look once, so the normal attack can take the head.
	// Later calls leave the look it started.
	static void YieldLookToAttack(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		ForgetLostLook(body);
		if (!s_PathLook.Contains(body))
			return;

		s_PathLook.Remove(body);
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_LookAction)
			return;

		utility.m_LookAction.Cancel();
	}

	protected static bool CanEngage(IEntity body, IEntity enemy, out float lean)
	{
		lean = 0;
		if (!body || !enemy)
			return false;

		bool left;
		bool right;
		SidesClear(body, enemy, left, right);

		if (CanSeeEntity(body, enemy))
			return true;

		// The center ray hits the cover he is standing against. A clear
		// side is still a shot, and the trigger needs that side remembered.
		// The same light and concealment check follows that ray.
		if (!left && !right)
			return false;

		if (!ConditionsAllow(enemy))
			return false;

		if (left && !right)
			lean = -1;
		else if (right && !left)
			lean = 1;
		else if (left && right)
			lean = -1;

		return true;
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

		if (s_InstantAim.Contains(body))
			return true;

		float now = 0;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			now = world.GetWorldTime();

		if (!s_AimTarget.Contains(body) || s_AimTarget.Get(body) != enemy)
		{
			s_AimTarget.Set(body, enemy);
			s_AimSince.Set(body, now);
		}

		return now - s_AimSince.Get(body) >= ShotDelayFor(body);
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

	// The point is his eyes. Looking at the entity uses the bounds center,
	// and from arm's length that sits under the muzzle. A new enemy starts
	// the turn. The same enemy only moves that point, or each shot check
	// restarts the turn and the rifle never arrives. The shot does not wait.
	protected static void AimLook(
		SCR_AIUtilityComponent utility,
		IEntity body,
		IEntity enemy)
	{
		if (!utility || !utility.m_LookAction || !body || !enemy)
			return;

		NoteRoomLook(body);
		vector point = ShotAimPoint(body, enemy);
		s_LookEntity.Set(body, enemy);

		// Same priority as the route look, so this moves the point onto
		// his eyes instead of losing to the node.
		s_bPathLookWrite = true;
		utility.m_LookAction.KK_Snap(point, PATH_LOOK_PRIORITY, SHOT_LOOK_DURATION);
		s_bPathLookWrite = false;
		SnapAimPose(body, point);
	}

	protected static void NoteRoomLook(IEntity body)
	{
		if (!body)
			return;

		s_RoomLookAt.Set(body, WorldTime());
	}

	// True while the room gun looked at a living enemy inside the hold window.
	// The post facing stays off so it does not turn him away from that aim.
	static bool RoomLookHolding(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_RoomLookAt.Contains(body))
			return false;

		return WorldTime() - s_RoomLookAt.Get(body) < ROOM_LOOK_HOLD_MS;
	}

	// The look point only asks him to turn. With snap aim on, the heading
	// and the weapon angle are written onto the point, so the rifle does
	// not ease there. A sprint still owns the body.
	static void SnapAimPose(IEntity soldier, vector point)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		// The contact look runs for every soldier. The snap is the clear
		// and the garrison only. A man on the ground, or with no gun left
		// that can fire, does not get it.
		if (
			!SnapAimEnabled() ||
			!OwnsShot(body) ||
			point == vector.Zero ||
			IsDown(body) ||
			!HasGunAmmo(body) ||
			IsBoundSprint(body) ||
			SprintBeforeReload(body) ||
			vector.Distance(body.GetOrigin(), point) > OrderContactRange()
		)
		{
			ClearSnapAim(body);
			return;
		}

		s_SnapAim.Set(body, point);
		ApplySnapAim(body, point);
		HoldSnapLook(body, SnapPosePoint(body, point));
		EnsureSnapAimPump();
	}

	protected static void ClearSnapAim(IEntity body)
	{
		if (!body)
			return;

		s_SnapAim.Remove(body);
		s_SnapError.Remove(body);
		s_SnapErrorAt.Remove(body);
		s_SnapErrorEnemy.Remove(body);
		s_SnapLogAt.Remove(body);
		DropSnapTurn(body);
	}

	protected static void EnsureSnapAimPump()
	{
		if (s_bSnapAimPumping || !GetGame())
			return;

		s_bSnapAimPumping = true;
		GetGame().GetCallqueue().CallLater(SnapAimPump, 0, false);
	}

	static void SnapAimPump()
	{
		if (!GetGame() || s_SnapAim.Count() == 0 || !SnapAimEnabled())
		{
			s_SnapAim.Clear();
			s_SnapError.Clear();
			s_SnapErrorAt.Clear();
			s_SnapErrorEnemy.Clear();
			s_SnapLogAt.Clear();
			s_SnapTurn.Clear();
			s_bSnapAimPumping = false;
			return;
		}

		array<IEntity> bodies = {};
		for (int i = 0; i < s_SnapAim.Count(); i++)
			bodies.Insert(s_SnapAim.GetKey(i));

		foreach (IEntity body : bodies)
		{
			if (!body || !s_SnapAim.Contains(body))
			{
				ClearSnapAim(body);
				continue;
			}

			if (
				!OwnsShot(body) ||
				IsDown(body) ||
				!HasGunAmmo(body) ||
				IsBoundSprint(body) ||
				SprintBeforeReload(body)
			)
			{
				ClearSnapAim(body);
				continue;
			}

			IEntity engaged = EngagedEnemy(body);
			vector held = s_SnapAim.Get(body);
			if (
				(engaged && PastOrderContact(body, engaged)) ||
				vector.Distance(body.GetOrigin(), held) > OrderContactRange()
			)
			{
				ClearSnapAim(body);
				continue;
			}

			// The stored point is only a fallback. A man the trace no longer
			// reaches is not a lock, so the rifle does not stay on him.
			vector point = s_SnapAim.Get(body);
			IEntity enemy = EngagedEnemy(body);
			if (enemy && IsTargetable(enemy))
			{
				if (!CanSeeEntity(body, enemy))
				{
					ClearSnapAim(body);
					continue;
				}

				vector live = ShotAimPoint(body, enemy);
				if (live != vector.Zero)
				{
					point = live;
					s_SnapAim.Set(body, point);
				}
			}

			ApplySnapAim(body, point);
		}

		if (s_SnapAim.Count() == 0 || !GetGame())
		{
			s_bSnapAimPumping = false;
			return;
		}

		GetGame().GetCallqueue().CallLater(SnapAimPump, 0, false);
	}

	protected static void ApplySnapAim(IEntity body, vector point)
	{
		CharacterControllerComponent controller = Controller(body);
		if (!controller || IsDown(body))
			return;

		vector pose = SnapPosePoint(body, point);
		vector flat = pose - body.GetOrigin();
		flat[1] = 0;
		if (flat.Length() < 0.05)
			return;

		flat.Normalize();
		float heading = flat.ToYaw();
		StopSnapTurn(body);
		SnapFacing(body, controller, heading);
		// The weapon is parented to the facing he has now. Using the heading
		// just requested aims the barrel off to the side until that turn lands.
		float bodyYaw = body.GetYawPitchRoll()[0];
		vector muzzle = MuzzlePosition(body, 0);
		WriteAimAt(controller.GetAimingComponent(), muzzle, pose, bodyYaw);
		HoldSnapLook(body, pose);
		LogSnapAim(body, controller, pose, muzzle, heading, bodyYaw);
	}

	// Combat movement also requests an aim turn. That request is dropped
	// while the snap holds him. The weapon bone stays with the animation
	// graph; the shot is aimed on its own.
	protected static void StopSnapTurn(IEntity body)
	{
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatMoveState)
			return;

		utility.m_CombatMoveState.KK_StopAimTurn();
		s_SnapTurn.Insert(utility.m_CombatMoveState);
	}

	protected static void DropSnapTurn(IEntity body)
	{
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatMoveState)
			return;

		s_SnapTurn.RemoveItem(utility.m_CombatMoveState);
	}

	static bool SnapTurnHeld(SCR_AICombatMoveState state)
	{
		return state && s_SnapTurn.Contains(state);
	}

	protected static void SnapFacing(
		IEntity body,
		CharacterControllerComponent controller,
		float headingDeg)
	{
		vector angles = body.GetYawPitchRoll();
		angles[0] = headingDeg;
		body.SetYawPitchRoll(angles);

		CharacterInputContext input = controller.GetInputContext();
		if (input)
			input.SetHeadingAngle(HeadingRadians(headingDeg));
	}

	protected static float HeadingRadians(float headingDeg)
	{
		float radians = headingDeg * Math.DEG2RAD;
		while (radians < 0)
			radians = radians + Math.PI2;
		while (radians >= Math.PI2)
			radians = radians - Math.PI2;

		return radians;
	}

	// The look behavior keeps the last point it was given. Write the pose
	// every frame, and restart only when that point has moved.
	protected static void HoldSnapLook(IEntity body, vector pose)
	{
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_LookAction || pose == vector.Zero)
			return;

		utility.m_LookAction.KK_HoldSnapPoint(pose);
	}

	// Once a second, the pose we wrote against the direction the rifle reports.
	protected static void LogSnapAim(
		IEntity body,
		CharacterControllerComponent controller,
		vector pose,
		vector muzzle,
		float headingDeg,
		float bodyYaw)
	{
		if (!SCR_BaseGameMode.KK_LogEnabled())
			return;

		float now = WorldTime();
		if (s_SnapLogAt.Contains(body) && now - s_SnapLogAt.Get(body) < SNAP_AIM_LOG_MS)
			return;

		s_SnapLogAt.Set(body, now);

		vector want = pose - muzzle;
		vector wantAng = vector.Zero;
		if (want.Length() > 0.05)
			wantAng = want.VectorToAngles();

		vector haveAng = vector.Zero;
		vector rotation = vector.Zero;
		AimingComponent aiming = controller.GetAimingComponent();
		if (aiming)
		{
			vector dir = aiming.GetAimingDirectionWorld();
			if (dir.Length() > 0.01)
				haveAng = dir.VectorToAngles();

			rotation = aiming.GetAimingRotation();
		}

		float yawErr = wantAng[0] - haveAng[0];
		while (yawErr > 180)
			yawErr = yawErr - 360;
		while (yawErr < -180)
			yawErr = yawErr + 360;

		vector look = vector.Zero;
		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (utility && utility.m_LookAction)
			look = utility.m_LookAction.KK_LookPosition();

		string note = string.Format(
			"%1 pose=%2 look=%3 muzzle=%4",
			body.GetName(),
			pose,
			look,
			muzzle
		);
		note = note + string.Format(
			" wantYaw=%1 wantPitch=%2 dirYaw=%3 dirPitch=%4 yawErr=%5 pitchErr=%6",
			wantAng[0],
			wantAng[1],
			haveAng[0],
			haveAng[1],
			yawErr,
			wantAng[1] - haveAng[1]
		);
		note = note + string.Format(" rot=%1 cmd=%2 body=%3", rotation, headingDeg, bodyYaw);
		PrintFormat("KK: Snap %1", note);
	}

	// Aim point plus the held skill offset. The look, the heading, and the
	// rifle all use this, so the offset is not a second point beside the look.
	static vector SnapPosePoint(IEntity soldier, vector point)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return point;

		if (point == vector.Zero && s_SnapAim.Contains(body))
			point = s_SnapAim.Get(body);

		if (point == vector.Zero)
			return point;

		return point + SnapAimError(body, point);
	}

	// Yaw is the gap between the heading and the line from the muzzle to the
	// pose. Pitch is that line's own pitch. Negative pitch is up.
	protected static void WriteAimAt(AimingComponent aiming, vector from, vector point, float headingDeg)
	{
		vector dir = point - from;
		if (dir.Length() < 0.05)
			return;

		vector angles = dir.VectorToAngles();
		float yaw = angles[0] - headingDeg;
		while (yaw > 180)
			yaw = yaw - 360;
		while (yaw < -180)
			yaw = yaw + 360;

		WriteAimRotation(aiming, yaw, angles[1]);
	}

	// Held offset around the aim point. A new man, or the hold running
	// out, rolls again. The axes match the aim-error node. The size is
	// the skill angle, so a long shot is not a laser.
	protected static vector SnapAimError(IEntity body, vector point)
	{
		if (!SnapAimErrorEnabled())
			return vector.Zero;

		IEntity enemy = EngagedEnemy(body);
		float now = WorldTime();
		bool held =
			s_SnapError.Contains(body) &&
			s_SnapErrorAt.Contains(body) &&
			now - s_SnapErrorAt.Get(body) < SNAP_AIM_ERROR_MS &&
			(
				!enemy ||
				(s_SnapErrorEnemy.Contains(body) && s_SnapErrorEnemy.Get(body) == enemy)
			);
		if (held)
			return s_SnapError.Get(body);

		vector offset = ComputeSnapAimError(body, enemy, point);
		s_SnapError.Set(body, offset);
		s_SnapErrorAt.Set(body, now);
		if (enemy)
			s_SnapErrorEnemy.Set(body, enemy);
		else
			s_SnapErrorEnemy.Remove(body);

		return offset;
	}

	protected static vector ComputeSnapAimError(IEntity body, IEntity enemy, vector point)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(body);
		vector eye = body.GetOrigin() + Vector(0, 1.6, 0);
		if (character)
			eye = character.EyePosition();

		vector join = point - eye;
		float distance = join.Length();
		if (distance < 0.05)
			return vector.Zero;

		vector joinNorm = join * (1 / distance);
		vector axisX = vector.Up * joinNorm;
		if (axisX.Length() < 0.01)
			axisX = Vector(1, 0, 0);
		axisX.Normalize();
		vector axisY = joinNorm * axisX;
		if (axisY.Length() < 0.01)
			return vector.Zero;
		axisY.Normalize();

		float span = AimErrorSpan(enemy, point);
		EAISkill skill = SnapSkill(body);
		float sigma = SnapScatterSigma(skill, distance, span);
		sigma = sigma * SnapWeaponFactor(body) * SnapIllumination(body);
		if (sigma <= 0)
			return vector.Zero;

		vector offset =
			(axisX * Math.RandomGaussFloat(sigma, 0)) +
			(axisY * Math.RandomGaussFloat(sigma, 0));
		return offset;
	}

	protected static EAISkill SnapSkill(IEntity body)
	{
		SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(
			body.FindComponent(SCR_AICombatComponent)
		);
		if (!combat)
			return EAISkill.REGULAR;

		return combat.GetAISkill();
	}

	// Diameters the aim node scatters across. Two times the aim point radius.
	protected static float AimErrorSpan(IEntity enemy, vector point)
	{
		float fallback = 0.45;
		if (!enemy)
			return fallback;

		PerceivableComponent perceivable = PerceivableComponent.Cast(
			enemy.FindComponent(PerceivableComponent)
		);
		if (!perceivable)
			return fallback;

		array<ref AimPoint> points = {};
		EAimPointType types[3];
		types[0] = EAimPointType.NORMAL;
		types[1] = EAimPointType.WEAK;
		types[2] = EAimPointType.INCAPACITATE;

		AimPoint nearest;
		float best = 4;
		for (int t = 0; t < 3; t++)
		{
			points.Clear();
			perceivable.GetAimpointsOfType(points, types[t]);
			foreach (AimPoint candidate : points)
			{
				if (!candidate)
					continue;

				float gap = vector.Distance(candidate.GetPosition(), point);
				if (gap >= best)
					continue;

				best = gap;
				nearest = candidate;
			}
		}

		if (!nearest || nearest.GetDimension() <= 0)
			return fallback;

		return 2 * nearest.GetDimension();
	}

	// Close-range share of the aim-point diameter. Past 15 m it grows.
	// The skill angle replaces this once it is the larger miss.
	protected static float SnapDistanceFactor(float distance)
	{
		if (distance < 15)
			return Math.Map(distance, 0, 15, 0.05, 0.4);

		float along = Math.Clamp((distance - 15) / 200, 0, 1);
		return Math.Lerp(0.4, 1.4, along);
	}

	protected static float SnapWeaponFactor(IEntity body)
	{
		BaseWeaponComponent weapon = CurrentWeapon(body);
		if (weapon && weapon.GetWeaponType() == EWeaponType.WT_ROCKETLAUNCHER)
			return 0;

		return 1;
	}

	protected static float SnapIllumination(IEntity body)
	{
		BaseTarget target = ShotTarget(body);
		if (!target)
			return 1;

		PerceivableComponent perceivable = target.GetPerceivableComponent();
		if (!perceivable)
			return 1;

		if (perceivable.GetIlluminationFactor() < 0.5)
			return 2;

		return 1;
	}

	// Snap launches the round on this point, so the angle is the whole miss.
	// The aim node's scatter is only centimeters inside a room and a fraction
	// of a degree past it. A regular is 1.25 degrees. Lower skill opens that
	// up, and the same angle is a larger miss farther out. Up close the
	// aim-point diameter can still be the larger of the two, on the vanilla
	// skill ratios. A cylon does not miss. Threat is not applied: that scale
	// is what pulls a threatened shot onto the point.
	protected static float SnapScatterSigma(EAISkill skill, float distance, float span)
	{
		if (skill == EAISkill.CYLON)
			return 0;

		float angular = distance * Math.Tan(SkillAimDegrees(skill) * Math.DEG2RAD);
		float body = span * SkillScatterShare(skill) * SnapDistanceFactor(distance);
		if (angular > body)
			return angular;

		return body;
	}

	protected static float SkillAimDegrees(EAISkill skill)
	{
		switch (skill)
		{
			case EAISkill.NOOB:
				return 3.5;
			case EAISkill.ROOKIE:
				return 2.2;
			case EAISkill.VETERAN:
				return 0.65;
			case EAISkill.EXPERT:
				return 0.32;
			case EAISkill.CYLON:
				return 0;
		}

		return 1.25;
	}

	// The aim node's skill sigma, relative to a regular. Used on the
	// aim-point diameter when that miss is larger than the angle.
	protected static float SkillScatterShare(EAISkill skill)
	{
		switch (skill)
		{
			case EAISkill.NOOB:
				return 4;
			case EAISkill.ROOKIE:
				return 2;
			case EAISkill.VETERAN:
				return 0.5;
			case EAISkill.EXPERT:
				return 0.25;
			case EAISkill.CYLON:
				return 0;
		}

		return 1;
	}

	// Debug log only. The barrel, the aiming component, the pose, and the
	// round's velocity. The next line is the velocity after the engine has
	// launched the round. Nothing here changes the shot.
	static void LogSnapShot(
		IEntity effectEntity,
		BaseMuzzleComponent muzzle,
		IEntity projectileEntity)
	{
		if (!SCR_BaseGameMode.KK_LogEnabled())
			return;

		IEntity shooter = SnapLogShooter(effectEntity, muzzle);
		if (!shooter || !s_SnapAim.Contains(shooter))
			return;

		PrintSnapShot(shooter, projectileEntity, "now");
		if (GetGame() && projectileEntity)
			GetGame().GetCallqueue().CallLater(LogSnapShotLater, 0, false, shooter, projectileEntity);
	}

	static void LogSnapShotLater(IEntity shooter, IEntity projectileEntity)
	{
		if (!shooter || !SCR_BaseGameMode.KK_LogEnabled())
			return;

		PrintSnapShot(shooter, projectileEntity, "next");
	}

	protected static void PrintSnapShot(IEntity shooter, IEntity projectileEntity, string when)
	{
		vector barrelPos;
		vector barrelDir;
		bool haveBarrel = SnapBarrel(shooter, barrelPos, barrelDir);

		vector aimDir = vector.Zero;
		CharacterControllerComponent controller = Controller(shooter);
		if (controller)
		{
			AimingComponent aiming = controller.GetAimingComponent();
			if (aiming)
				aimDir = aiming.GetAimingDirectionWorld();
		}

		vector pose = SnapPosePoint(shooter, vector.Zero);
		vector poseDir = vector.Zero;
		if (pose != vector.Zero && haveBarrel)
			poseDir = pose - barrelPos;

		vector bulletDir = vector.Zero;
		float speed = 0;
		if (projectileEntity)
		{
			ProjectileMoveComponent move = ProjectileMoveComponent.Cast(
				projectileEntity.FindComponent(ProjectileMoveComponent)
			);
			if (move)
			{
				bulletDir = move.GetVelocity();
				speed = bulletDir.Length();
			}
		}

		string note = string.Format(
			"%1 %2 barrel=%3 aim=%4 pose=%5",
			when,
			shooter.GetName(),
			YawPitchOf(barrelDir),
			YawPitchOf(aimDir),
			YawPitchOf(poseDir)
		);
		note = note + string.Format(
			" bullet=%1 offBarrel=%2 offPose=%3 offAim=%4 spd=%5",
			YawPitchOf(bulletDir),
			YawGap(barrelDir, bulletDir),
			YawGap(poseDir, bulletDir),
			YawGap(aimDir, bulletDir),
			speed
		);
		PrintFormat("KK: Shot %1", note);
	}

	protected static bool SnapBarrel(IEntity shooter, out vector position, out vector direction)
	{
		position = vector.Zero;
		direction = vector.Zero;

		BaseWeaponManagerComponent manager = WeaponManager(shooter);
		if (!manager)
			return false;

		vector muzzle[4];
		if (!manager.GetCurrentMuzzleTransform(muzzle))
			return false;

		position = muzzle[3];
		direction = muzzle[2];
		return direction.Length() > 0.01;
	}

	protected static IEntity SnapLogShooter(IEntity effectEntity, BaseMuzzleComponent muzzle)
	{
		IEntity cursor = effectEntity;
		while (cursor)
		{
			if (ChimeraCharacter.Cast(cursor))
				return cursor;

			cursor = cursor.GetParent();
		}

		if (!muzzle)
			return null;

		cursor = muzzle.GetOwner();
		while (cursor)
		{
			if (ChimeraCharacter.Cast(cursor))
				return cursor;

			cursor = cursor.GetParent();
		}

		return null;
	}

	protected static vector YawPitchOf(vector direction)
	{
		if (direction.Length() < 0.01)
			return vector.Zero;

		vector angles = direction.VectorToAngles();
		float pitch = angles[1];
		while (pitch > 180)
			pitch = pitch - 360;
		while (pitch < -180)
			pitch = pitch + 360;

		return Vector(angles[0], pitch, 0);
	}

	// Signed yaw from the first direction to the second. Positive is to the right.
	protected static float YawGap(vector from, vector to)
	{
		if (from.Length() < 0.01 || to.Length() < 0.01)
			return 0;

		vector fromAng = from.VectorToAngles();
		vector toAng = to.VectorToAngles();
		float gap = toAng[0] - fromAng[0];
		while (gap > 180)
			gap = gap - 360;
		while (gap < -180)
			gap = gap + 360;

		return gap;
	}

	// Launch along the pose from the fire callback. The flag is held across
	// the launch so the callback that launch raises does not launch again.
	static void QueueSnapShot(
		IEntity effectEntity,
		BaseMuzzleComponent muzzle,
		IEntity projectileEntity)
	{
		if (s_bSnapShotApply || !projectileEntity)
			return;

		IEntity shooter = SnapLogShooter(effectEntity, muzzle);
		if (!shooter || !s_SnapAim.Contains(shooter))
			return;

		vector origin;
		vector barrelDir;
		if (!SnapBarrel(shooter, origin, barrelDir))
			return;

		vector pose = SnapPosePoint(shooter, vector.Zero);
		vector direction = pose - origin;
		if (direction.Length() < 0.05)
			return;

		direction.Normalize();
		KK_SnapShotAim shot = new KK_SnapShotAim();
		shot.m_Shooter = shooter;
		shot.m_Origin = origin;
		shot.m_Direction = direction;

		s_bSnapShotApply = true;
		LaunchSnapShot(projectileEntity, shot);
		s_bSnapShotApply = false;
	}

	protected static void LaunchSnapShot(IEntity projectile, KK_SnapShotAim shot)
	{
		if (!projectile || !shot || shot.m_Direction.Length() < 0.01)
			return;

		ProjectileMoveComponent move = ProjectileMoveComponent.Cast(
			projectile.FindComponent(ProjectileMoveComponent)
		);
		if (!move)
			return;

		projectile.SetYawPitchRoll(shot.m_Direction.VectorToAngles());
		projectile.SetOrigin(shot.m_Origin);
		move.Launch(
			shot.m_Direction,
			vector.Zero,
			1,
			projectile,
			shot.m_Shooter,
			shot.m_Shooter,
			null,
			null
		);
	}

	static bool SnapAimClaimed(IEntity soldier)
	{
		if (!SnapAimEnabled())
			return false;

		IEntity body = CharacterBody(soldier);
		if (
			!body ||
			!OwnsShot(body) ||
			IsDown(body) ||
			!HasGunAmmo(body) ||
			!s_SnapAim.Contains(body)
		)
		{
			return false;
		}

		IEntity enemy = EngagedEnemy(body);
		return enemy && CanSeeEntity(body, enemy);
	}

	protected static bool SnapAimEnabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return false;

		return mode.KK_GetSnapAim();
	}

	protected static bool SnapAimErrorEnabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetSnapAimError();
	}

	// GetAimingRotation is degrees. SetAimingRotation takes radians.
	// Limits follow whichever unit their magnitude is in.
	protected static void WriteAimRotation(AimingComponent aiming, float yawDeg, float pitchDeg)
	{
		if (!aiming)
			return;

		// VectorToAngles pitch for a nearly level shot is just under 360.
		// The weapon limit is about -80 to 80, so that value clamps to the
		// top and the rifle stays pitched at the limit.
		while (yawDeg > 180)
			yawDeg = yawDeg - 360;
		while (yawDeg < -180)
			yawDeg = yawDeg + 360;
		while (pitchDeg > 180)
			pitchDeg = pitchDeg - 360;
		while (pitchDeg < -180)
			pitchDeg = pitchDeg + 360;

		vector limitsH;
		vector limitsV;
		aiming.GetAimingLimits(limitsH, limitsV);
		bool hasLimits = limitsH[0] != 0 || limitsH[1] != 0 || limitsV[0] != 0 || limitsV[1] != 0;
		if (hasLimits)
		{
			bool degrees = Math.AbsFloat(limitsH[1]) > 6.5 || Math.AbsFloat(limitsV[1]) > 6.5;
			if (degrees)
			{
				yawDeg = Math.Clamp(yawDeg, limitsH[0], limitsH[1]);
				pitchDeg = Math.Clamp(pitchDeg, limitsV[0], limitsV[1]);
			}
			else
			{
				yawDeg = Math.Clamp(yawDeg * Math.DEG2RAD, limitsH[0], limitsH[1]) / Math.DEG2RAD;
				pitchDeg = Math.Clamp(pitchDeg * Math.DEG2RAD, limitsV[0], limitsV[1]) / Math.DEG2RAD;
			}
		}

		vector rotation = Vector(yawDeg, pitchDeg, 0) * Math.DEG2RAD;
		aiming.SetAimingRotation(rotation);
		aiming.SetAimingRotationWanted(rotation);
	}

	static vector ShotAimPoint(IEntity shooter, IEntity enemy)
	{
		vector chest = ChestPoint(enemy);
		if (!shooter || chest == vector.Zero)
			return chest;

		vector head = HeadPoint(enemy);
		if (PointVisible(shooter, enemy, chest))
			return chest;

		if (PointVisible(shooter, enemy, head))
			return head;

		return chest;
	}

	// Chest unless the caller has no body to test. The eyes sit above the
	// barrel and were the miss.
	static vector ShotAimPoint(IEntity enemy)
	{
		return ChestPoint(enemy);
	}

	protected static vector ChestPoint(IEntity enemy)
	{
		return BodyPoint(enemy, "Spine3", CHEST_AIM_HEIGHT);
	}

	protected static vector HeadPoint(IEntity enemy)
	{
		return BodyPoint(enemy, "Head", HEAD_AIM_HEIGHT);
	}

	protected static vector BodyPoint(IEntity enemy, string bone, float height)
	{
		if (!enemy)
			return vector.Zero;

		vector fallback = enemy.GetOrigin() + Vector(0, height, 0);
		Animation anim = enemy.GetAnimation();
		if (!anim)
			return fallback;

		int index = anim.GetBoneIndex(bone);
		if (index < 0)
			return fallback;

		vector mat[4];
		anim.GetBoneMatrix(index, mat);
		vector point = enemy.CoordToParent(mat[3]);
		if (point == vector.Zero)
			return fallback;

		return point;
	}

	protected static bool PointVisible(IEntity shooter, IEntity enemy, vector point)
	{
		if (!shooter || point == vector.Zero)
			return false;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			world = shooter.GetWorld();

		if (!world)
			return false;

		ChimeraCharacter character = ChimeraCharacter.Cast(shooter);
		vector eye = shooter.GetOrigin() + Vector(0, 1.6, 0);
		if (character)
			eye = character.EyePosition();

		s_SightTarget = enemy;
		bool clear = SightClear(world, shooter, eye, point);
		s_SightTarget = null;
		return clear;
	}

	// True while he should leave the plant. This man took none of his
	// damage through the try, and the cool-off is still walking him off.
	static bool BarrelWantsMove(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_BarrelCoolUntil.Contains(body))
			return false;

		if (WorldTime() >= s_BarrelCoolUntil.Get(body))
		{
			s_BarrelCoolUntil.Remove(body);
			s_BarrelBlockedTarget.Remove(body);
			return false;
		}

		IEntity enemy = EngagedEnemy(body);
		IEntity blocked = null;
		if (s_BarrelBlockedTarget.Contains(body))
			blocked = s_BarrelBlockedTarget.Get(body);

		if (!enemy || enemy != blocked || !CanSeeEntity(body, enemy))
		{
			ForgetBarrelBlock(body);
			return false;
		}

		return true;
	}

	protected static IEntity EngagedEnemy(IEntity body)
	{
		if (!body)
			return null;

		if (s_ShotLook.Contains(body))
		{
			IEntity shot = s_ShotLook.Get(body);
			if (shot && IsTargetable(shot))
				return shot;
		}

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatComponent)
			return null;

		BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
		if (!target)
			return null;

		return target.GetTargetEntity();
	}

	// Shooting this man. The try is how long he may go without his own
	// damage landing. A hit from him clears it.
	protected static void NoteNoDamage(IEntity body, IEntity enemy)
	{
		if (!body || !enemy)
			return;

		float now = WorldTime();
		if (s_BarrelCoolUntil.Contains(body) && now < s_BarrelCoolUntil.Get(body))
			return;

		bool same =
			s_BarrelBlockedSince.Contains(body) &&
			s_BarrelBlockedTarget.Contains(body) &&
			s_BarrelBlockedTarget.Get(body) == enemy;
		if (!same)
		{
			s_BarrelBlockedSince.Set(body, now);
			s_BarrelBlockedTarget.Set(body, enemy);
			if (BarrelStuckMs() <= 0)
				StartBarrelMove(body, enemy, now);

			return;
		}

		if (now - s_BarrelBlockedSince.Get(body) < BarrelStuckMs())
			return;

		StartBarrelMove(body, enemy, now);
	}

	// A real hit from this shooter. Another man's round does not count,
	// and neither does damage he deals to himself.
	static void NoteTargetDamaged(IEntity hitOwner, BaseDamageContext damageContext)
	{
		if (!damageContext || damageContext.damageValue <= 0 || !damageContext.instigator)
			return;

		IEntity victim = hitOwner;
		if (!ChimeraCharacter.Cast(victim))
			victim = damageContext.hitEntity;

		victim = CharacterBody(victim);
		IEntity shooter = InstigatorBody(damageContext.instigator);
		if (!victim || !shooter || shooter == victim)
			return;

		if (!s_BarrelBlockedTarget.Contains(shooter))
			return;

		if (s_BarrelBlockedTarget.Get(shooter) != victim)
			return;

		ForgetBarrelBlock(shooter);
	}

	protected static IEntity InstigatorBody(Instigator instigator)
	{
		if (!instigator)
			return null;

		IEntity cursor = instigator.GetInstigatorEntity();
		while (cursor)
		{
			if (ChimeraCharacter.Cast(cursor))
				return cursor;

			cursor = cursor.GetParent();
		}

		return null;
	}

	protected static void StartBarrelMove(IEntity body, IEntity enemy, float now)
	{
		if (!body)
			return;

		s_BarrelBlockedSince.Remove(body);
		s_BarrelBlockedTarget.Set(body, enemy);
		// A zero cool-off still has to outlast the plant check that
		// releases him, or he plants again on the same step.
		float cool = BarrelCoolMs();
		if (cool < 100)
			cool = 100;

		s_BarrelCoolUntil.Set(body, now + cool);

		if (SCR_BaseGameMode.KK_LogEnabled())
			Print("KK: No hit, leaving the spot");
	}

	protected static void ForgetBarrelBlock(IEntity body)
	{
		if (!body)
			return;

		s_BarrelBlockedSince.Remove(body);
		s_BarrelBlockedTarget.Remove(body);
		s_BarrelCoolUntil.Remove(body);
	}

	protected static float BarrelStuckMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 1000;

		return mode.KK_GetBarrelStuck() * 1000;
	}

	protected static float BarrelCoolMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 2000;

		return mode.KK_GetBarrelCool() * 1000;
	}

	protected static vector MuzzlePosition(IEntity body, float lean)
	{
		vector bone;
		if (lean == 0 && MuzzleBone(body, bone) && MuzzleNearBody(body, bone))
			return bone;

		vector transform[4];
		body.GetWorldTransform(transform);
		float side = 0.18;
		if (lean < 0)
			side = -LEAN_OFFSET;
		else if (lean > 0)
			side = LEAN_OFFSET;

		vector shoulder = body.GetOrigin() + Vector(0, 1.45, 0);
		return shoulder + (transform[0] * side) + (transform[2] * 0.45);
	}

	protected static bool MuzzleBone(IEntity body, out vector muzzle)
	{
		muzzle = vector.Zero;
		BaseWeaponComponent weapon = CurrentWeapon(body);
		if (!weapon)
			return false;

		IEntity weaponEnt = weapon.GetOwner();
		if (!weaponEnt)
			return false;

		Animation anim = weaponEnt.GetAnimation();
		if (!anim)
			return false;

		int index = anim.GetBoneIndex("muzzle");
		if (index < 0)
			index = anim.GetBoneIndex("slot_muzzle");

		if (index < 0)
			return false;

		vector mat[4];
		anim.GetBoneMatrix(index, mat);
		muzzle = weaponEnt.CoordToParent(mat[3]);
		return muzzle != vector.Zero;
	}

	// A detached weapon or a bad bone sits tens of meters away. The barrel
	// trace from there never clears, so the shot stays off.
	protected static bool MuzzleNearBody(IEntity body, vector muzzle)
	{
		vector gap = muzzle - body.GetOrigin();
		float height = Math.AbsFloat(gap[1]);
		gap[1] = 0;
		return gap.Length() <= 2 && height <= 2.5;
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
		ClearSnapAim(body);
		if (utility && utility.m_LookAction)
			utility.m_LookAction.Cancel();
	}

	// Drops a look we did not start. Facing the threat while sprinting
	// to cover holds him in a strafe.
	protected static void CancelLook(SCR_AIUtilityComponent utility, IEntity body)
	{
		if (body)
		{
			s_LookEntity.Remove(body);
			ClearSnapAim(body);
		}

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

	// Nearest living enemy perception has already started on, and this
	// order can trace. Identified, detected, and unknown all count.
	// Far contacts stay on the lane look. A house order also takes a man
	// perception missed because a window is in the way.
	static IEntity VisibleContact(IEntity soldier, float maxRange = ATTACK_CONTACT_RANGE)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return null;

		IEntity nearest = null;
		float best = maxRange;

		PerceptionComponent perception = PerceptionComponent.Cast(
			body.FindComponent(PerceptionComponent)
		);
		if (perception)
			nearest = NearestPerceived(body, perception, maxRange, best);

		float search = maxRange;
		if (nearest)
			search = best;

		IEntity throughGlass = NearestGlassContact(body, search);
		if (!throughGlass)
			return nearest;

		if (!nearest || vector.Distance(body.GetOrigin(), throughGlass.GetOrigin()) < best)
			return throughGlass;

		return nearest;
	}

	protected static IEntity NearestPerceived(
		IEntity body,
		PerceptionComponent perception,
		float maxRange,
		out float best)
	{
		IEntity nearest = null;
		best = maxRange;

		for (int category = 0; category < 3; category++)
		{
			ETargetCategory kind = ETargetCategory.ENEMY;
			bool requireHostile = true;
			if (category == 1)
			{
				kind = ETargetCategory.DETECTED;
				requireHostile = true;
			}
			else if (category == 2)
			{
				kind = ETargetCategory.UNKNOWN;
				requireHostile = true;
			}

			s_AttackSeen.Clear();
			perception.GetTargetsList(s_AttackSeen, kind);

			foreach (BaseTarget candidate : s_AttackSeen)
			{
				if (!candidate)
					continue;

				IEntity enemy = candidate.GetTargetEntity();
				if (!enemy || enemy == body || !IsTargetable(enemy))
					continue;

				if (requireHostile && !IsHostile(body, enemy))
					continue;

				if (!CanSeeEntity(body, enemy))
					continue;

				float distance = vector.Distance(
					body.GetOrigin(),
					enemy.GetOrigin()
				);
				if (distance > best)
					continue;

				best = distance;
				nearest = enemy;
			}
		}

		return nearest;
	}

	// Closest hostile the glass-aware ray can see. Perception never lists
	// him when the pane blocks its own check.
	protected static IEntity NearestGlassContact(IEntity body, float maxRange)
	{
		if (!body || maxRange < 1 || !OnGlassWatch(body))
			return null;

		FillCloseBodies(body, maxRange);
		for (int i = 0; i < s_CloseBodies.Count(); i++)
		{
			IEntity enemy = s_CloseBodies[i];
			if (!enemy)
				continue;

			if (s_CloseDist[i] > maxRange)
				continue;

			if (CanSeeEntity(body, enemy))
				return enemy;
		}

		return null;
	}

	protected static bool CanSeeTarget(IEntity body, BaseTarget target)
	{
		if (!body || !target)
			return false;

		IEntity enemy = target.GetTargetEntity();
		if (!enemy || !IsTargetable(enemy))
			return false;

		return CanSeeEntity(body, enemy);
	}

	protected static bool CanSeeEntity(IEntity body, IEntity enemy)
	{
		if (!body || !enemy || !IsTargetable(enemy))
			return false;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			world = body.GetWorld();

		if (!world)
			return false;

		vector eye = body.GetOrigin() + Vector(0, 1.6, 0);
		vector aim = enemy.GetOrigin() + Vector(0, 0.35, 0);
		ChimeraCharacter character = ChimeraCharacter.Cast(enemy);
		if (character)
		{
			vector theirEye = character.EyePosition();
			if (theirEye != vector.Zero)
				aim = theirEye;
		}

		// His collider sits on the ray. Counting that hit as a block made
		// a man on the ground invisible, so the finish never started.
		s_SightTarget = enemy;
		bool seen = SightClear(world, body, eye, aim);
		if (!seen)
			seen = SightClear(world, body, eye, enemy.GetOrigin() + Vector(0, 0.35, 0));
		s_SightTarget = null;
		if (!seen)
			return false;

		return ConditionsAllow(enemy);
	}

	// The ray already succeeded. This asks whether he can pick the man
	// out. Distance is not part of it. He sees the man when the light on
	// him, reduced by concealment, is still enough.
	protected static bool ConditionsAllow(IEntity enemy)
	{
		if (!UseSightConditions())
			return true;

		if (!enemy)
			return false;

		float conceal = ConcealmentOf(enemy);
		if (conceal < 0)
			conceal = 0;
		if (conceal > 0.85)
			conceal = 0.85;

		float apparent = IlluminationOf(enemy) * (1 - conceal);
		return apparent >= 0.2;
	}

	protected static bool UseSightConditions()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return false;

		return mode.KK_GetSightConditions();
	}

	protected static float IlluminationOf(IEntity enemy)
	{
		PerceivableComponent perceivable = PerceivableComponent.Cast(
			enemy.FindComponent(PerceivableComponent)
		);
		if (!perceivable)
			return 1;

		float light = perceivable.GetIlluminationFactor();
		if (light < 0)
			return 0;
		if (light > 1)
			return 1;

		return light;
	}

	// 0 is standing in the open. Prone, still, and foliage push it toward 1.
	// A man who is moving gives that concealment back.
	protected static float ConcealmentOf(IEntity enemy)
	{
		float conceal = 0;
		bool low = false;
		CharacterControllerComponent controller = Controller(enemy);
		if (controller)
		{
			ECharacterStance stance = controller.GetStance();
			if (stance == ECharacterStance.PRONE)
			{
				conceal = 0.55;
				low = true;
			}
			else if (stance == ECharacterStance.CROUCH)
			{
				conceal = 0.3;
				low = true;
			}
		}

		conceal = conceal + FoliageConcealment(enemy, low);

		float speed = FlatSpeed(enemy);
		if (speed >= 2.5)
			conceal = conceal * 0.4;
		else if (speed >= 0.6)
			conceal = conceal * 0.75;
		else if (conceal > 0)
			conceal = conceal + 0.15;

		return conceal;
	}

	protected static float FlatSpeed(IEntity enemy)
	{
		Physics physics = enemy.GetPhysics();
		if (!physics)
			return 0;

		vector velocity = physics.GetVelocity();
		velocity[1] = 0;
		return velocity.Length();
	}

	// A bush around the body counts for anyone. Grass at the feet counts
	// only when he is already low.
	protected static float FoliageConcealment(IEntity enemy, bool low)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			world = enemy.GetWorld();

		if (!world)
			return 0;

		if (!s_ConditionTrace)
			s_ConditionTrace = new TraceParam();

		vector origin = enemy.GetOrigin();
		s_ConditionBody = enemy;
		s_ConditionTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		s_ConditionTrace.Exclude = enemy;
		s_ConditionTrace.TraceEnt = null;
		s_ConditionTrace.Start = origin + Vector(0, 2.2, 0);
		s_ConditionTrace.End = origin + Vector(0, 0.15, 0);

		float fraction = world.TraceMove(s_ConditionTrace, FilterConditionTrace);
		s_ConditionBody = null;
		if (fraction >= 1)
			return 0;

		if (!HitIsFoliage())
			return 0;

		float hitHeight = 2.2 - (fraction * 2.05);
		if (hitHeight >= 0.45 && hitHeight <= 1.8)
			return 0.4;

		if (low && hitHeight < 0.45)
			return 0.15;

		return 0;
	}

	protected static bool HitIsFoliage()
	{
		if (NameIsFoliage(s_ConditionTrace.TraceMaterial))
			return true;

		return EntityIsFoliage(s_ConditionTrace.TraceEnt);
	}

	protected static bool EntityIsFoliage(IEntity entity)
	{
		if (!entity)
			return false;

		if (NameIsFoliage(entity.GetName()))
			return true;

		if (NameIsFoliage(entity.ClassName()))
			return true;

		EntityPrefabData prefabData = entity.GetPrefabData();
		if (!prefabData)
			return false;

		return NameIsFoliage(string.Format("%1", prefabData.GetPrefabName()));
	}

	protected static bool NameIsFoliage(string name)
	{
		if (name.IsEmpty())
			return false;

		string lower = name;
		lower.ToLower();
		return lower.Contains("grass") ||
			lower.Contains("bush") ||
			lower.Contains("foliage") ||
			lower.Contains("leaf") ||
			lower.Contains("shrub") ||
			lower.Contains("hedge") ||
			lower.Contains("plant") ||
			lower.Contains("fern") ||
			lower.Contains("reed") ||
			lower.Contains("crop");
	}

	protected static bool FilterConditionTrace(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		if (!entity || entity == s_ConditionBody)
			return false;

		IEntity current = entity;
		int depth;
		while (current && depth < 6)
		{
			if (current == s_ConditionBody)
				return false;

			if (ChimeraCharacter.Cast(current))
				return false;

			current = current.GetParent();
			depth++;
		}

		return true;
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

		s_SightViewer = body;
		BeginSightTrace(start, aim);

		float fraction = TraceThroughGlass(world);
		s_SightViewer = null;

		// A wall stops the ray well short. A graze on the frame near the
		// target still counts as a clear shot. Glass is stepped through
		// before that test.
		return fraction >= 0.9;
	}

	// A pane stops the ray on the first hit. Step past it and trace again.
	// A wall behind the glass still blocks. Eight hits covers a double
	// window and the door behind it when each pane has two faces.
	protected static float TraceThroughGlass(BaseWorld world)
	{
		float fraction = world.TraceMove(s_SightTrace, FilterMoveSight);
		vector end = s_SightTrace.End;
		for (int hop = 0; hop < 8; hop++)
		{
			if (fraction >= 0.9)
				return fraction;

			if (!HitIsTransparent())
				return fraction;

			vector from = s_SightTrace.Start;
			vector dir = end - from;
			float length = dir.Length();
			if (length < 0.05)
				return 1;

			dir = dir * (1 / length);
			vector next = from + (dir * (fraction * length)) + (dir * 0.06);
			if (vector.Distance(next, end) < 0.1)
				return 1;

			BeginSightTrace(next, end);
			fraction = world.TraceMove(s_SightTrace, FilterMoveSight);
		}

		return fraction;
	}

	// A new param starts with empty hit fields. Writing TraceMaterial,
	// ColliderName, or SurfaceProps on a used param corrupts the heap.
	protected static void BeginSightTrace(vector start, vector aim)
	{
		s_SightTrace = new TraceParam();
		s_SightTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		s_SightTrace.Exclude = s_SightViewer;
		s_SightTrace.Start = start;
		s_SightTrace.End = aim;
	}

	// TraceMaterial is filled for the ground. A pane on a house is the
	// surface that was hit, so the game material and the window collider
	// are what name it.
	protected static bool HitIsTransparent()
	{
		if (MaterialIsTransparent(s_SightTrace.TraceMaterial))
			return true;

		if (MaterialIsTransparent(s_SightTrace.ColliderName))
			return true;

		if (PropsAreTransparent(s_SightTrace.SurfaceProps))
			return true;

		if (SurfaceIsTransparent(s_SightTrace.TraceEnt))
			return true;

		return WindowCollider(s_SightTrace.TraceEnt);
	}

	protected static bool PropsAreTransparent(SurfaceProperties properties)
	{
		if (!properties)
			return false;

		BaseContainer current = properties;
		int depth;
		while (current && depth < 4)
		{
			if (MaterialIsTransparent(current.GetName()))
				return true;

			ResourceName resource = current.GetResourceName();
			if (MaterialIsTransparent(string.Format("%1", resource)))
				return true;

			current = current.GetAncestor();
			depth++;
		}

		return false;
	}

	protected static bool WindowCollider(IEntity entity)
	{
		IEntity current = entity;
		int depth;
		while (current && depth < 4)
		{
			HitZoneContainerComponent container = HitZoneContainerComponent.Cast(
				current.FindComponent(HitZoneContainerComponent)
			);
			if (container && ZoneIsWindow(container))
				return true;

			current = current.GetParent();
			depth++;
		}

		return false;
	}

	protected static bool ZoneIsWindow(notnull HitZoneContainerComponent container)
	{
		HitZone zone = null;
		if (!s_SightTrace.ColliderName.IsEmpty())
			zone = container.GetHitZone(s_SightTrace.ColliderName);

		if (!zone && s_SightTrace.ColliderIndex >= 0)
			zone = container.GetHitZoneByColliderID(s_SightTrace.ColliderIndex);

		if (!zone)
			return false;

		if (SCR_WindowHitZone.Cast(zone))
			return true;

		return NameIsTransparent(zone.GetName());
	}

	protected static bool MaterialIsTransparent(string material)
	{
		if (material.IsEmpty())
			return false;

		string lower = material;
		lower.ToLower();
		return lower.Contains("glass") ||
			lower.Contains("window") ||
			lower.Contains("transparent");
	}

	// A glass pane or a window piece. A house mesh stays solid; the pane
	// on that mesh is named by the hit surface, not by the house.
	protected static bool SurfaceIsTransparent(IEntity entity)
	{
		if (!entity)
			return false;

		if (NameIsTransparent(entity.GetName()))
			return true;

		if (NameIsTransparent(entity.ClassName()))
			return true;

		EntityPrefabData prefabData = entity.GetPrefabData();
		if (!prefabData)
			return false;

		return NameIsTransparent(string.Format("%1", prefabData.GetPrefabName()));
	}

	protected static bool NameIsTransparent(string name)
	{
		if (name.IsEmpty())
			return false;

		string lower = name;
		lower.ToLower();
		if (lower.Contains("glass"))
			return true;

		if (!lower.Contains("window"))
			return false;

		if (lower.Contains("house_") || lower.Contains("_house") || lower.Contains("wall_"))
			return false;

		return true;
	}

	protected static bool FilterMoveSight(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		if (!entity || entity == s_SightViewer)
			return false;

		// The pane is not cover. Skipping it lets the ray reach the man
		// on the other side. The frame and the wall still stop it.
		if (SurfaceIsTransparent(entity))
			return false;

		IEntity current = entity;
		int depth;
		while (current && depth < 8)
		{
			if (current == s_SightViewer)
				return false;

			// Another soldier stops the ray. Passing through him made a
			// clear shot at the man behind, and the bullets hit the man
			// in front. The man being checked is not that wall.
			if (ChimeraCharacter.Cast(current))
			{
				if (SameCharacter(current, s_SightTarget))
					return false;

				return true;
			}

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

		// A man on the ground does not take the attack. CQB is paused,
		// and this behavior would aim and walk him anyway.
		if (
			KK_GarrisonHold.IsDown(character) ||
			KK_GarrisonHold.IsDown(agentEntity)
		)
		{
			m_bUseCombatMove = false;
			return 0;
		}

		// The bound is a sprint to the next spot. The attack aims that
		// step, and the aimed step walks.
		if (
			KK_GarrisonHold.IsBoundSprint(character) ||
			KK_GarrisonHold.IsBoundSprint(agentEntity)
		)
		{
			m_bUseCombatMove = false;
			return 0;
		}

		// A clear and a garrison do not take this behavior for a man inside
		// CQB range. The room gun fires, and this attack must not carry him
		// forward. On a garrison, past that range the normal attack aims
		// and fires, and the route still owns his feet. A clear never does.
		bool houseOrder =
			KK_GarrisonHold.UseRoomCombat() &&
			(
				KK_GarrisonHold.OrderOwnsBehavior(character) ||
				KK_GarrisonHold.OrderOwnsBehavior(agentEntity)
			);
		bool distantFight =
			KK_GarrisonHold.DistantBaseFight(character) ||
			KK_GarrisonHold.DistantBaseFight(agentEntity);
		if (houseOrder && !distantFight)
		{
			m_bUseCombatMove = false;
			return 0;
		}

		// A remembered burst is not a target. The attack would keep firing
		// at that spot after the body is gone.
		bool onOrder =
			KK_GarrisonHold.UseRoomCombat() &&
			(
				KK_GarrisonHold.HasBuilding(character) ||
				KK_GarrisonHold.HasBuilding(agentEntity)
			);
		if (
			onOrder &&
			!KK_GarrisonHold.HasVisibleEnemy(character) &&
			!KK_GarrisonHold.HasVisibleEnemy(agentEntity)
		)
		{
			return 0;
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
		// Within range of the building, a clear does not take the combat
		// step. That step is a short standing strafe, and the route never
		// gets going. Farther out, the attack keeps the step.
		bool clearingInside =
			(KK_GarrisonHold.IsClearing(character) && KK_GarrisonHold.OwnsShot(character)) ||
			(KK_GarrisonHold.IsClearing(agentEntity) && KK_GarrisonHold.OwnsShot(agentEntity));
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

		if (clearingInside || pinned || doorFiring || distantFight)
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
		if (
			utility &&
			KK_GarrisonHold.OwnsShot(utility.m_OwnerEntity) &&
			!KK_GarrisonHold.DistantBaseFight(utility.m_OwnerEntity)
		)
		{
			m_fWaitTime.m_Value = KK_GarrisonHold.ShotDelayFor(utility.m_OwnerEntity) / 1000;
			return;
		}

		bool attackSearch =
			utility &&
			(
				KK_GarrisonHold.IsAttackSearch(utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsAttackSearch(utility.GetOwner())
			);

		bool distantFight =
			utility &&
			(
				KK_GarrisonHold.DistantBaseFight(utility.m_OwnerEntity) ||
				KK_GarrisonHold.DistantBaseFight(utility.GetOwner())
			);
		if (
			!distantFight &&
			KK_PerceptionBoost.UseSharpCombat() &&
			(
				attackSearch ||
				(utility && KK_PerceptionBoost.IsActiveSoldier(utility.m_OwnerEntity))
			)
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

		// The route is the move for the whole clear or garrison, outside
		// as well. A combat-move step is the strafe.
		if (
			KK_GarrisonHold.UseRoomCombat() &&
			(
				KK_GarrisonHold.OrderOwnsBehavior(body) ||
				KK_GarrisonHold.OrderOwnsBehavior(owner)
			)
		)
		{
			if (m_State && m_State.IsExecutingRequest())
				m_State.CancelRequest();

			return ENodeResult.RUNNING;
		}

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

		// A raised threat keeps requesting a short strafe after the enemy
		// is gone. Finishing the node lets that request start again, and
		// the step-raise-stop is the shuffle. Hold the node instead.
		// A clear does not sit in this loop. Its combat move is refused
		// when CRX submits it.
		bool onOrder =
			KK_GarrisonHold.UseRoomCombat() &&
			(
				KK_GarrisonHold.HasBuilding(body) ||
				KK_GarrisonHold.HasBuilding(owner)
			);
		bool clearing =
			KK_GarrisonHold.IsClearing(body) ||
			KK_GarrisonHold.IsClearing(owner);
		if (
			onOrder &&
			!clearing &&
			!KK_GarrisonHold.HasVisibleEnemy(body) &&
			!KK_GarrisonHold.HasVisibleEnemy(owner)
		)
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
					if (target && KK_GarrisonHold.IsLivingTarget(target))
					{
						IEntity targetEntity = target.GetTargetEntity();
						if (targetEntity)
							aimPos = KK_GarrisonHold.ShotAimPoint(body, targetEntity);
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
		if (KK_GarrisonHold.ClearerPushing(m_Utility))
			return 0;

		if (DoorFiring() || RunningToReloadNode() || BashingReload() || HeldInCover())
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

	protected bool HeldInCover()
	{
		return m_Utility &&
			(
				KK_GarrisonHold.IsPinned(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsPinned(m_Utility.GetOwner())
			);
	}
}

modded class SCR_AIThrowGrenadeToBehavior
{
	override float CustomEvaluate()
	{
		if (KK_GarrisonHold.ClearerPushing(m_Utility))
			return 0;

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
		if (KK_GarrisonHold.ClearerPushing(m_Utility))
			return 0;

		if (
			m_Utility &&
			(
				KK_GarrisonHold.IsDoorFiring(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsDoorFiring(m_Utility.GetOwner()) ||
				KK_GarrisonHold.IsPinned(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsPinned(m_Utility.GetOwner()) ||
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

// Knowing a sector is hot, with nobody in sight, is enough for these to
// take the body and hold. A clear keeps the route instead.
modded class SCR_AIRetreatFromTargetBehavior
{
	override float CustomEvaluate()
	{
		if (KK_GarrisonHold.ClearerPushing(m_Utility))
			return 0;

		return super.CustomEvaluate();
	}
}

modded class SCR_AIObserveThreatSystemBehavior
{
	override float CustomEvaluate()
	{
		if (KK_GarrisonHold.ClearerPushing(m_Utility))
			return 0;

		return super.CustomEvaluate();
	}
}

modded class SCR_AIMoveFromDangerBehavior
{
	override float CustomEvaluate()
	{
		if (KK_GarrisonHold.ClearerPushing(m_Utility))
			return 0;

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

	// The body is already gone. Drop the bar and the danger events, or
	// alert keeps an investigate on the last point inside the building.
	void KK_DropStaleAlert()
	{
		if (m_Agent && m_Agent.GetDangerEventsCount() > 0)
			m_Agent.ClearDangerEvents(m_Agent.GetDangerEventsCount() + 1);

		KK_IgnoreForSprint();
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

		// A selected last-seen point sets endangered back to full on every
		// update. Drop it before that, until this order sees someone.
		bool coverHold =
			m_Utility &&
			(
				KK_GarrisonHold.IsPinned(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsPinned(m_Utility.GetOwner())
			);
		if (
			m_Utility &&
			!coverHold &&
			(
				KK_GarrisonHold.IsFreshOrder(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsFreshOrder(m_Utility.GetOwner())
			) &&
			!KK_GarrisonHold.HasVisibleEnemy(m_Utility.m_OwnerEntity) &&
			!KK_GarrisonHold.HasVisibleEnemy(m_Utility.GetOwner()) &&
			m_Utility.m_CombatComponent
		)
		{
			m_Utility.m_CombatComponent.KK_ClearTarget();
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
		return KK_SelectHouseBehavior(unknownTarget);
	}

	// Behavior pick for a house order. Compat calls this and skips the
	// other addon's selector.
	SCR_AIBehaviorBase KK_SelectHouseBehavior(BaseTarget unknownTarget)
	{
		bool ignore =
			KK_GarrisonHold.IsIgnoringTargets(m_OwnerEntity) ||
			KK_GarrisonHold.IsIgnoringTargets(GetOwner());

		if (ignore && m_CombatComponent)
			m_CombatComponent.KK_ClearTarget();

		// The previous fight's unknown point would start an investigate
		// on a house he was just told to clear again.
		bool freshOrder =
			KK_GarrisonHold.IsFreshOrder(m_OwnerEntity) ||
			KK_GarrisonHold.IsFreshOrder(GetOwner());
		IEntity viewer = m_OwnerEntity;
		if (!viewer)
			viewer = GetOwner();
		bool staleUnknown =
			freshOrder &&
			(
				!unknownTarget ||
				!KK_GarrisonHold.IsLivingTarget(unknownTarget) ||
				!KK_GarrisonHold.SeesTarget(viewer, unknownTarget)
			);

		bool downed =
			KK_GarrisonHold.IsDown(m_OwnerEntity) ||
			KK_GarrisonHold.IsDown(GetOwner());

		SCR_AIBehaviorBase result;
		if (ignore || staleUnknown || downed)
			result = vanilla.EvaluateBehavior(null);
		else
			result = vanilla.EvaluateBehavior(unknownTarget);

		if (downed)
		{
			if (
				KK_GarrisonHold.OnHouseOrder(m_OwnerEntity) ||
				KK_GarrisonHold.OnHouseOrder(GetOwner())
			)
			{
				IEntity downedBody = m_OwnerEntity;
				if (!downedBody)
					downedBody = GetOwner();
				KK_GarrisonHold.PauseDownedCQB(downedBody);
			}

			return result;
		}

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
			KK_GarrisonHold.KeepClearWeaponRaised(soldier);
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

		if (KK_GarrisonHold.IsDown(body) || KK_GarrisonHold.IsDown(owner))
			return ENodeResult.SUCCESS;

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

		// A bound sprint cannot keep the rifle up. The raise would cut the run.
		// Sending the lower again restarts it and cuts the step off.
		if (KK_GarrisonHold.IsBoundSprint(body) || KK_GarrisonHold.IsBoundSprint(owner))
		{
			if (body)
			{
				CharacterControllerComponent controller =
					CharacterControllerComponent.Cast(
						body.FindComponent(CharacterControllerComponent)
					);

				if (controller && (controller.IsWeaponRaised() || controller.IsWeaponADS()))
				{
					controller.SetWeaponADS(false);
					controller.SetWeaponRaised(false);
				}
			}

			return ENodeResult.SUCCESS;
		}

		if (
			(KK_GarrisonHold.OwnsShot(body) || KK_GarrisonHold.OwnsShot(owner)) &&
			!KK_GarrisonHold.DistantBaseFight(body) &&
			!KK_GarrisonHold.DistantBaseFight(owner)
		)
		{
			SCR_ChimeraAIAgent roomSoldier = SCR_ChimeraAIAgent.Cast(owner);
			if (roomSoldier)
				KK_GarrisonHold.ApplyRoomShot(roomSoldier.m_UtilityComponent);

			// ApplyRoomShot can leave the rifle down when nothing is in
			// sight. The clear puts it back up unless a sprint, a bash,
			// an empty gun, or a reload owns it.
			IEntity clearer = body;
			if (!clearer)
				clearer = owner;
			KK_GarrisonHold.KeepClearWeaponRaised(clearer);
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

		// Outside the room shot, the move behavior lowers a weapon that
		// has no target. A clear still carries it up.
		IEntity clearer = body;
		if (!clearer)
			clearer = owner;
		if (KK_GarrisonHold.KeepClearWeaponRaised(clearer))
			return ENodeResult.SUCCESS;

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AILookAction
{
	// LookAt restarts a turn that has not finished. A shot check does that
	// every pass, so the rifle never arrives on the target. Move the point
	// and leave a restart that already started this look.
	void KK_Track(vector pos, float priority, float duration)
	{
		if (SnapClaimed() || SprintIgnoring() || SprintingToCover() || PathLookBlocks())
			return;

		if (pos == vector.Zero)
			return;

		if (m_vPosition == vector.Zero || priority < m_fPriority)
		{
			LookAt(pos, priority, duration);
			return;
		}

		m_vPosition = pos;
		m_fPriority = priority;
		m_fDuration = duration;
	}

	// Replaces a look at a man. A lower priority is ignored, so the head
	// stays on him until that look ends. One restart turns him onto the
	// lane. Later calls only move the point.
	vector KK_LookPosition()
	{
		return m_vPosition;
	}

	// Moves this look onto the snap pose. A restart cancels the turn he
	// already has and starts another, which is the sweep off the route
	// when a shot begins. The point is updated and that restart is dropped.
	void KK_HoldSnapPoint(vector pos)
	{
		if (pos == vector.Zero || LookOwnerDown())
			return;

		m_bRestartLook = false;
		m_vPosition = pos;
		m_fDuration = 30;
		m_bCancelLook = false;
		if (m_fPriority < 1000)
			m_fPriority = 1000;
	}

	void KK_Snap(vector pos, float priority, float duration)
	{
		if (LookOwnerDown() || SnapClaimed())
			return;

		if (
			!KK_GarrisonHold.IsPathLookWrite() &&
			(SprintIgnoring() || SprintingToCover() || PathLookBlocks())
		)
		{
			return;
		}

		if (pos == vector.Zero)
			return;

		// A new point while this look is already his does not start the
		// turn again. Restarting it is the head snapping off the lane.
		if (m_fPriority == priority && m_vPosition != vector.Zero)
		{
			m_vPosition = pos;
			m_fDuration = duration;
			m_bCancelLook = false;
			m_bRestartLook = false;
			return;
		}

		m_bCancelLook = false;
		m_bRestartLook = true;
		m_vPosition = pos;
		m_fPriority = priority;
		m_fDuration = duration;
	}

	override void LookAt(vector pos, float priority, float duration = 0.8)
	{
		if (SnapClaimed())
			return;

		if (!KK_GarrisonHold.IsPathLookWrite() && PathLookBlocks())
			return;

		if (SprintIgnoring() || SprintingToCover() || BoundSprinting())
			return;

		super.LookAt(pos, priority, duration);
	}

	override void LookAt(IEntity ent, float priority, float duration = 0.8)
	{
		if (SnapClaimed() || PathLookBlocks())
			return;

		if (SprintIgnoring() || SprintingToCover() || BoundSprinting())
			return;

		super.LookAt(ent, priority, duration);
	}

	override void Cancel()
	{
		if (PathLookBlocks())
			return;

		super.Cancel();
	}

	// The look behavior reads this every tick. While snap has the sight
	// line, the look stays on that point. Cancelling it left the attack
	// behavior with nothing to shoot at.
	override void MoveLookParametersToNode(
		out bool outCanLook,
		out vector outLookPos,
		out float outLookDuration,
		out bool outCancelLook,
		out bool outRestartLook)
	{
		super.MoveLookParametersToNode(
			outCanLook,
			outLookPos,
			outLookDuration,
			outCancelLook,
			outRestartLook
		);

		if (LookOwnerDown() || !SnapClaimed() || !m_Utility)
			return;

		vector snap = KK_GarrisonHold.SnapPosePoint(m_Utility.m_OwnerEntity, vector.Zero);
		if (snap == vector.Zero)
			snap = KK_GarrisonHold.SnapPosePoint(m_Utility.GetOwner(), vector.Zero);
		if (snap == vector.Zero)
			return;

		outCanLook = true;
		outLookPos = snap;
		m_vPosition = snap;
		outCancelLook = false;
		outRestartLook = false;
		m_bRestartLook = false;
	}

	protected bool LookOwnerDown()
	{
		if (!m_Utility)
			return false;

		return KK_GarrisonHold.IsDown(m_Utility.m_OwnerEntity) ||
			KK_GarrisonHold.IsDown(m_Utility.GetOwner());
	}

	protected bool SnapClaimed()
	{
		if (!m_Utility)
			return false;

		return KK_GarrisonHold.SnapAimClaimed(m_Utility.m_OwnerEntity) ||
			KK_GarrisonHold.SnapAimClaimed(m_Utility.GetOwner());
	}

	// A clear or garrison look is already on this head. Anything else,
	// including a threat look, is dropped. A distant fight leaves the
	// head to the normal attack.
	protected bool PathLookBlocks()
	{
		if (!m_Utility)
			return false;

		if (
			KK_GarrisonHold.DistantBaseFight(m_Utility.m_OwnerEntity) ||
			KK_GarrisonHold.DistantBaseFight(m_Utility.GetOwner())
		)
		{
			return false;
		}

		return KK_GarrisonHold.PathLookActive(m_Utility);
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

	protected bool BoundSprinting()
	{
		return m_Utility &&
			(
				KK_GarrisonHold.IsBoundSprint(m_Utility.m_OwnerEntity) ||
				KK_GarrisonHold.IsBoundSprint(m_Utility.GetOwner())
			);
	}
}

// The sprint is this move. Combat movement on it is a strafe at the enemy,
// and that strafe will not take a sprint.
// A plant pins him, but this order's speed is run and the behavior applies
// that speed on its own evaluation. Idle from the clear is gone by then,
// so the route has to lose the behavior while the pin is on.
modded class SCR_AIMoveIndividuallyBehavior
{
	override float CustomEvaluate()
	{
		if (PlantHoldsFeet())
			return 0;

		float score = super.CustomEvaluate();
		if (!m_Utility)
			return score;

		if (
			KK_GarrisonHold.IsBoundSprint(m_Utility.m_OwnerEntity) ||
			KK_GarrisonHold.IsBoundSprint(m_Utility.GetOwner()) ||
			(
				KK_GarrisonHold.UseRoomCombat() &&
				(
					KK_GarrisonHold.OrderOwnsBehavior(m_Utility.m_OwnerEntity) ||
					KK_GarrisonHold.OrderOwnsBehavior(m_Utility.GetOwner())
				)
			)
		)
			m_bUseCombatMove = false;

		return score;
	}

	protected bool PlantHoldsFeet()
	{
		if (!m_Utility)
			return false;

		IEntity body = m_Utility.m_OwnerEntity;
		if (!KK_GarrisonHold.IsPinned(body) && !KK_GarrisonHold.IsPinned(m_Utility.GetOwner()))
			return false;

		if (!body)
		{
			AIAgent agent = AIAgent.Cast(m_Utility.GetOwner());
			if (agent)
				body = agent.GetControlledEntity();
		}

		if (!body)
			return true;

		AICharacterMovementComponent movement =
			AICharacterMovementComponent.Cast(
				body.FindComponent(AICharacterMovementComponent)
			);
		if (movement)
			movement.SetMovementTypeWanted(EMovementType.IDLE);

		return true;
	}
}

// A take cover bound owns the body. Combat movement is how the enemy
// steers him, and aiming that steer is what refuses the sprint.
modded class SCR_AICombatMoveState
{
	override void ApplyNewRequest(notnull SCR_AICombatMoveRequestBase request)
	{
		if (KK_GarrisonHold.IsSprintMoveLocked(this) || KK_GarrisonHold.IsClearMoveLocked(this))
		{
			request.m_eState = SCR_EAICombatMoveRequestState.CANCELED;
			if (m_Request && m_Request.m_eState == SCR_EAICombatMoveRequestState.EXECUTING)
				m_Request.m_eState = SCR_EAICombatMoveRequestState.CANCELED;

			m_Request = null;
			m_bAimAtTarget = false;
			return;
		}

		SCR_AICombatMoveRequest_Move move = SCR_AICombatMoveRequest_Move.Cast(request);
		if (move && SnapTurnBlocked())
		{
			move.m_bAimAtTarget = false;
			move.m_bAimAtTargetEnd = false;
		}

		super.ApplyNewRequest(request);
		if (SnapTurnBlocked())
			m_bAimAtTarget = false;
	}

	override void EnableAiming(bool enable)
	{
		if (enable && (SnapTurnBlocked() || KK_GarrisonHold.IsSprintMoveLocked(this) || KK_GarrisonHold.IsClearMoveLocked(this)))
		{
			m_bAimAtTarget = false;
			return;
		}

		super.EnableAiming(enable);
	}

	void KK_StopAimTurn()
	{
		m_bAimAtTarget = false;
	}

	protected bool SnapTurnBlocked()
	{
		return KK_GarrisonHold.SnapTurnHeld(this);
	}
}

// Close range accepts ten degrees of miss, so the rifle stops short of the
// target. A greater threat pulls that onto him. The shot timer is unchanged.
modded class SCR_AIGetAimErrorOffset
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		ENodeResult result = super.EOnTaskSimulate(owner, dt);
		if (result != ENodeResult.SUCCESS)
			return result;

		IEntity body;
		if (owner)
			body = owner.GetControlledEntity();

		if (!KK_GarrisonHold.SnapAimClaimed(body) && !KK_GarrisonHold.SnapAimClaimed(owner))
			return result;

		// The snap already placed the skill offset. This node would add
		// another one and the rifle would settle beside the pose.
		vector pose = KK_GarrisonHold.SnapPosePoint(body, vector.Zero);
		if (pose == vector.Zero)
			pose = KK_GarrisonHold.SnapPosePoint(owner, vector.Zero);
		if (pose == vector.Zero)
			return result;

		AimPoint aimPoint;
		if (GetVariableIn(PORT_AIM_POINT, aimPoint) && aimPoint)
			SetVariableOut(PORT_ERROR_OFFSET, pose - aimPoint.GetPosition());
		else
			SetVariableOut(PORT_ERROR_OFFSET, vector.Zero);

		return ENodeResult.SUCCESS;
	}

	override float GetRandomFactor(EAISkill skill, float mu)
	{
		float factor = super.GetRandomFactor(skill, mu);
		if (DistantAiming())
			return factor;

		float threat = RoomThreat();
		if (threat < 0)
			return factor;

		return factor * Math.Lerp(0.5, 0, threat);
	}

	override float GetTolerance(
		IEntity observer,
		IEntity target,
		float angularSize,
		float distance,
		EWeaponType weaponType)
	{
		float tolerance = super.GetTolerance(
			observer,
			target,
			angularSize,
			distance,
			weaponType
		);

		if (DistantAiming())
			return tolerance;

		float threat = KK_GarrisonHold.AimThreat(observer);
		if (threat < 0)
			return tolerance;

		return Math.Min(tolerance, Math.Lerp(3, 0.6, threat));
	}

	// Past CQB range the normal attack owns the aim, including its scatter.
	protected bool DistantAiming()
	{
		if (!m_CombatComponent)
			return false;

		return KK_GarrisonHold.DistantBaseFight(m_CombatComponent.GetOwner());
	}

	protected float RoomThreat()
	{
		if (!m_CombatComponent)
			return -1;

		return KK_GarrisonHold.AimThreat(m_CombatComponent.GetOwner());
	}
}

// A snap round is launched along the pose from this callback.
modded class SCR_MuzzleEffectComponent
{
	override void OnFired(IEntity effectEntity, BaseMuzzleComponent muzzle, IEntity projectileEntity)
	{
		super.OnFired(effectEntity, muzzle, projectileEntity);
		KK_GarrisonHold.LogSnapShot(effectEntity, muzzle, projectileEntity);
		KK_GarrisonHold.QueueSnapShot(effectEntity, muzzle, projectileEntity);
	}
}

// His own round landed. The no-hit try on this man starts over.
modded class SCR_CharacterHitZone
{
	override void OnDamage(notnull BaseDamageContext damageContext)
	{
		super.OnDamage(damageContext);
		KK_GarrisonHold.NoteTargetDamaged(GetOwner(), damageContext);
	}
}

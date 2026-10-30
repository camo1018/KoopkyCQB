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

		// The remembered man is put back when the sprint ends, while the
		// attack is still his. After that order he is a normal target.
		if (!KK_GarrisonHold.IsAttackSearch(GetOwner()))
		{
			KK_GarrisonHold.ForgetSprintEnemy(GetOwner());
			return;
		}

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
		// An attack does not use that curve. Alerted is still a slow pickup
		// outdoors, so the attack scale fills a plain-view man on the next
		// perception tick. The scenario multiplier still scales it.
		if (
			perceptionComp &&
			threatSystem &&
			KK_GarrisonHold.IsAttackSearch(GetOwner())
		)
		{
			float attackFactor = PERCEPTION_FACTOR_ALERTED * KK_GarrisonHold.ATTACK_SPOT_SCALE;
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
		if (!KK_PerfLog.On())
		{
			KK_EvaluateHouseOrder(
				outWeaponEvent,
				outSelectedTargetChanged,
				outPrevTarget,
				outCurrentTarget,
				outRetreatTargetChanged,
				outCompartmentChanged
			);
			return;
		}

		int started = System.GetTickCount();
		KK_EvaluateHouseOrder(
			outWeaponEvent,
			outSelectedTargetChanged,
			outPrevTarget,
			outCurrentTarget,
			outRetreatTargetChanged,
			outCompartmentChanged
		);
		KK_PerfLog.End("CQB.WeaponTarget", started);
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
		KK_GarrisonHold.TickCustomHeal(GetOwner());

		// Weapon selection raises the rifle and pulls it back into his hands.
		// Either one cuts the bandage off, and he then waits on a gesture
		// that is no longer playing.
		if (KK_GarrisonHold.YieldsWeaponForHeal(GetOwner()))
		{
			outWeaponEvent = false;
			outSelectedTargetChanged = false;
			outPrevTarget = null;
			outCurrentTarget = null;
			outRetreatTargetChanged = false;
			outCompartmentChanged = false;
			return;
		}

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
		// Outside an order, that event is the normal reload.
		if (
			KK_GarrisonHold.OnCqbOrder(GetOwner()) &&
			outWeaponEvent &&
			m_SelectedWeaponComp == previousWeapon &&
			m_iSelectedMuzzle == previousMuzzle &&
			KK_SelectedMagazineIsLoaded()
		)
		{
			outWeaponEvent = false;
		}

		if (KK_GarrisonHold.YieldsWeaponForHeal(GetOwner()))
		{
			// The bandage is the item in his hands. Publishing a gun here
			// pulls that gun back out and the heal never starts.
			outWeaponEvent = false;
		}
		else if (KK_HoldFightWeapon())
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
		// so the stop does not have to recognize him again. A live engage
		// keeps that enemy. The step must not turn him off the man.
		if (KK_GarrisonHold.IsBoundSprint(GetOwner()) && !KK_GarrisonHold.EngageOwnsTurn(GetOwner()))
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

		// The ray reached him and vanilla kept him. The light check still
		// says he is not visible. That is the same as the ray missing:
		// he is not a target, and holding him here only stops the unit.
		if (
			m_SelectedTarget &&
			KK_GarrisonHold.BlockedByConditions(GetOwner(), m_SelectedTarget)
		)
		{
			KK_ClearTarget();
			m_SelectedTargetVisible = false;
			outCurrentTarget = null;
			outSelectedTargetChanged = false;
			KK_GarrisonHold.DropRememberedAim(GetOwner());
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
		// Seeing a man is not enough. This look is the clear, garrison, or
		// attack route. Anyone else keeps the aim the base game just wrote.
		if (!KK_GarrisonHold.OnCqbOrder(GetOwner()))
			return;

		if (KK_GarrisonHold.DistantBaseFight(GetOwner()))
		{
			KK_GarrisonHold.YieldLookToAttack(GetOwner());
			return;
		}

		// The plant already aimed. A contact snap here is a second look
		// at the same priority, and the two restart each other. Between
		// plants the step look is left alone.
		if (KK_GarrisonHold.EngageOwnsTurn(GetOwner()))
		{
			vector engageAim;
			if (KK_GarrisonHold.GetEngageAim(GetOwner(), engageAim))
				m_SelectedTargetDestinationPos = engageAim;

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

		if (KK_GarrisonHold.YieldsWeaponForHeal(GetOwner()))
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

class KK_PerfBucket
{
	int m_iCalls;
	int m_iTotalMs;
	int m_iMaxMs;
	int m_iUnits;
}

// Wall-clock samples for the CQB fight and snap aim. One summary a second.
// Off, the clock is not read. A parent total includes the samples under it.
// Short calls often add 0 until the clock crosses a millisecond, so read
// the one-second total.
class KK_PerfLog
{
	protected static const int WINDOW_MS = 1000;
	protected static int s_iWindowAt;
	protected static bool s_bFlushQueued;
	protected static ref map<string, ref KK_PerfBucket> s_Buckets =
		new map<string, ref KK_PerfBucket>();

	static bool On()
	{
		return SCR_BaseGameMode.KK_PerfLogEnabled();
	}

	// Negative means the timings are off.
	static int Begin()
	{
		if (!On())
			return -1;

		return System.GetTickCount();
	}

	static void End(string name, int started, int units = 0)
	{
		if (started < 0)
			return;

		int elapsed = System.GetTickCount() - started;
		if (elapsed < 0)
			elapsed = 0;

		KK_PerfBucket bucket = Bucket(name);
		bucket.m_iCalls = bucket.m_iCalls + 1;
		bucket.m_iTotalMs = bucket.m_iTotalMs + elapsed;
		if (elapsed > bucket.m_iMaxMs)
			bucket.m_iMaxMs = elapsed;
		if (units > 0)
			bucket.m_iUnits = bucket.m_iUnits + units;

		QueueFlush();
	}

	static void Reset()
	{
		s_iWindowAt = 0;
		s_bFlushQueued = false;
		for (int i = 0; i < s_Buckets.Count(); i++)
		{
			KK_PerfBucket bucket = s_Buckets.GetElement(i);
			if (!bucket)
				continue;

			bucket.m_iCalls = 0;
			bucket.m_iTotalMs = 0;
			bucket.m_iMaxMs = 0;
			bucket.m_iUnits = 0;
		}
	}

	protected static KK_PerfBucket Bucket(string name)
	{
		if (s_Buckets.Contains(name))
			return s_Buckets.Get(name);

		KK_PerfBucket created = new KK_PerfBucket();
		s_Buckets.Set(name, created);
		return created;
	}

	protected static void QueueFlush()
	{
		int now = System.GetTickCount();
		if (s_iWindowAt == 0)
			s_iWindowAt = now;

		if (s_bFlushQueued || !GetGame())
			return;

		if (now - s_iWindowAt < WINDOW_MS)
			return;

		s_bFlushQueued = true;
		GetGame().GetCallqueue().CallLater(FlushQueued, 0, false);
	}

	static void FlushQueued()
	{
		s_bFlushQueued = false;
		if (!On())
		{
			Reset();
			return;
		}

		int now = System.GetTickCount();
		int windowMs = now - s_iWindowAt;
		if (windowMs < 1)
			windowMs = 1;
		s_iWindowAt = now;

		array<string> names = {};
		for (int i = 0; i < s_Buckets.Count(); i++)
		{
			string key = s_Buckets.GetKey(i);
			KK_PerfBucket bucket = s_Buckets.Get(key);
			if (!bucket || bucket.m_iCalls == 0)
				continue;

			names.Insert(key);
		}

		if (names.Count() == 0)
			return;

		Print(string.Format(
			"KK perf %1ms. A caller total includes the samples it makes.",
			windowMs
		));

		while (names.Count() > 0)
		{
			int best = 0;
			int bestMs = s_Buckets.Get(names.Get(0)).m_iTotalMs;
			for (int b = 1; b < names.Count(); b++)
			{
				int sampleMs = s_Buckets.Get(names.Get(b)).m_iTotalMs;
				if (sampleMs <= bestMs)
					continue;

				best = b;
				bestMs = sampleMs;
			}

			string name = names.Get(best);
			names.Remove(best);
			KK_PerfBucket printed = s_Buckets.Get(name);
			string line = string.Format(
				"KK perf %1 x%2 %3ms max %4",
				name,
				printed.m_iCalls,
				printed.m_iTotalMs,
				printed.m_iMaxMs
			);
			if (printed.m_iUnits > 0)
				line = line + string.Format(" bodies %1", printed.m_iUnits);

			Print(line);
			printed.m_iCalls = 0;
			printed.m_iTotalMs = 0;
			printed.m_iMaxMs = 0;
			printed.m_iUnits = 0;
		}
	}
}

// The pose a snap round is launched along from the fire callback.
class KK_SnapShotAim
{
	IEntity m_Shooter;
	vector m_Origin;
	vector m_Direction;
}

// Characters near one building for this shot interval. Men on the house
// share the first sphere. Men still on the road share the second.
class KK_NearBubble
{
	float m_At = -1;
	vector m_Center;
	float m_Radius;
	ref array<IEntity> m_Bodies = new array<IEntity>();
	vector m_RoadCenter;
	float m_RoadRadius;
	ref array<IEntity> m_RoadBodies = new array<IEntity>();
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
	// A lower already asked for. Sending it again restarts the pose.
	protected static ref map<IEntity, float> s_LowerSentAt = new map<IEntity, float>();
	// Long enough for a raise to report up. A second call inside this
	// restarts the animation. Past it, a rifle that is still down never
	// got the command.
	protected static const float RAISE_RESEND_MS = 1500;
	// This order has not seen a living enemy yet. Last-seen points from
	// the previous clear of this house stay out of the selector.
	protected static ref set<IEntity> s_FreshOrder = new set<IEntity>();
	protected static ref map<AIAgent, SCR_AIGroup> s_OrderNoted =
		new map<AIAgent, SCR_AIGroup>();
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
	// Recognition stays fast for the whole attack, including the bound
	// sprint. The sprint does not turn the head toward a contact.
	protected static ref set<IEntity> s_AttackSearch = new set<IEntity>();
	protected static ref array<BaseTarget> s_AttackSeen = new array<BaseTarget>();
	// The trigger this engage is holding. Cleared when he should be moving
	// again, or when the contact is no longer current.
	protected static ref set<IEntity> s_PerceivedFire = new set<IEntity>();
	// A perceived hostile this attack has not dropped yet.
	protected static ref set<IEntity> s_EngageEpisode = new set<IEntity>();
	// When that contact dropped. A new one inside the recent gap is not
	// another first sighting.
	protected static ref map<IEntity, float> s_EngageEpisodeEnded =
		new map<IEntity, float>();
	// World time the forced stop lasts until, and when the next one may start.
	protected static ref map<IEntity, float> s_EngageHoldUntil =
		new map<IEntity, float>();
	protected static ref map<IEntity, float> s_EngageNextStop =
		new map<IEntity, float>();
	// Men who have already taken the engage shot. A later man stops him
	// only the first time he becomes that shot and is visible.
	protected static ref map<IEntity, ref set<IEntity>> s_EngageKnown =
		new map<IEntity, ref set<IEntity>>();
	// Who the engage shot was on last pass. A new man has to beat him.
	protected static ref map<IEntity, IEntity> s_EngageAim =
		new map<IEntity, IEntity>();
	// Where that shot is aimed. The contact stamp and the lane scan read
	// this and leave the head alone, so they do not restart the turn.
	protected static ref map<IEntity, vector> s_EngageAimPoint =
		new map<IEntity, vector>();
	protected static ref map<IEntity, float> s_EngageAimAt =
		new map<IEntity, float>();
	protected static ref array<BaseTarget> s_EngageScan = new array<BaseTarget>();
	// Feet locked for a forced engage stop. A cover pin is a different set.
	protected static ref set<IEntity> s_EngageStop = new set<IEntity>();
	// Walking to another cover at the point. The shot from this spot cannot
	// reach the man who is shooting, so the engage stop stays off until he
	// is planted or that lane opens.
	protected static ref set<IEntity> s_CoverShift = new set<IEntity>();
	// World time the projectile lane to that man first failed. Cleared when
	// a round can get there, or when he leaves the spot.
	protected static ref map<IEntity, float> s_LaneBlockedAt =
		new map<IEntity, float>();
	// No carried gun can be fed. Engage stays off until one can.
	protected static ref set<IEntity> s_EngageUnarmed = new set<IEntity>();
	// A shot this attack soldier heard. The shooter can still be unknown
	// to perception when the bush hides every aim point. The note is how
	// long he keeps firing at that man. The place is the muzzle and the
	// direction marks that shot. A hidden target traces this line once.
	protected static ref map<IEntity, IEntity> s_HeardShooter =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, vector> s_HeardPlace =
		new map<IEntity, vector>();
	protected static ref map<IEntity, vector> s_HeardDir =
		new map<IEntity, vector>();
	protected static ref map<IEntity, float> s_HeardWhen =
		new map<IEntity, float>();
	// Who last sent a bullet at this soldier, and when. A landed hit, a
	// strike near his feet, and an open path from a shooter he heard all
	// write this. The lane ray can still miss a man in brush after one
	// of those.
	protected static ref map<IEntity, IEntity> s_HitBy =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, int> s_HitByKind =
		new map<IEntity, int>();
	protected static ref map<IEntity, float> s_HitByAt =
		new map<IEntity, float>();
	// Why a hidden man may still be shot. A landed hit, a nearby strike,
	// and an open path from his muzzle are recorded separately.
	protected static const int THREAT_LANDED = 1;
	protected static const int THREAT_IMPACT = 2;
	protected static const int THREAT_PATH = 4;
	protected static const float THREAT_CONTACT_S = 8;
	protected static const float ATTACK_CONTACT_RANGE = 180;
	// Times the alerted rate. 2.5 alone still leaves a man in the open
	// unrecognized for seconds. This is the attack order's own rate.
	static const float ATTACK_SPOT_SCALE = 8;
	// The outdoor scan is a sphere, not a perception tick. Repeating it
	// for every caller in the same moment walks the same men again.
	protected static const float ATTACK_SCAN_MS = 100;
	protected static ref map<IEntity, float> s_AttackGatherAt =
		new map<IEntity, float>();
	protected static ref map<IEntity, ref array<IEntity>> s_AttackGather =
		new map<IEntity, ref array<IEntity>>();
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
	protected static ref map<IEntity, bool> s_SnapErrorUnseen =
		new map<IEntity, bool>();
	// Random aim picks chest or head once per man. A new roll each frame
	// would saw the rifle between the two.
	protected static ref map<IEntity, IEntity> s_AimPickEnemy =
		new map<IEntity, IEntity>();
	protected static ref map<IEntity, int> s_AimPick =
		new map<IEntity, int>();
	protected static ref map<IEntity, float> s_SnapLogAt =
		new map<IEntity, float>();
	protected static ref map<IEntity, float> s_EngageLogAt =
		new map<IEntity, float>();
	protected static ref map<IEntity, float> s_FireLogAt = new map<IEntity, float>();
	protected static ref map<IEntity, float> s_EngageShotLogAt =
		new map<IEntity, float>();
	// Last threat note printed per soldier. The same gunshot and the same
	// strike sit on the agent for many frames, so a repeat is not logged.
	protected static ref map<IEntity, string> s_ThreatLineLog =
		new map<IEntity, string>();
	protected static ref map<IEntity, string> s_ThreatImpactLog =
		new map<IEntity, string>();
	protected static ref map<IEntity, string> s_ThreatHitLog =
		new map<IEntity, string>();
	protected static ref map<IEntity, string> s_ThreatPathLog =
		new map<IEntity, string>();
	// One path result per stored shot. The order evaluates much more
	// often than the line changes.
	protected static ref map<IEntity, string> s_PathKey =
		new map<IEntity, string>();
	protected static ref map<IEntity, bool> s_PathOpen =
		new map<IEntity, bool>();
	protected static IEntity s_PathExclude;
	protected static IEntity s_PathTarget;
	// Keys copied out of the live maps. Reused so a tick does not allocate.
	protected static ref array<IEntity> s_SnapBodies = new array<IEntity>();
	protected static ref array<IEntity> s_ShotBodies = new array<IEntity>();
	protected static ref array<IEntity> s_LeanBodies = new array<IEntity>();
	protected static IEntity s_UtilityBody;
	protected static SCR_AIUtilityComponent s_UtilityCache;
	protected static IEntity s_ControllerBody;
	protected static CharacterControllerComponent s_ControllerCache;
	// One sight answer per world-time for the same pair. A behavior tick
	// asks again for the man it just traced.
	protected static IEntity s_SeeViewer;
	protected static IEntity s_SeeTarget;
	protected static float s_SeeAt = -1;
	protected static bool s_SeeResult;
	// The ray reached him and the light check still rejected him.
	// A blocked ray leaves this false. Callers treat the two differently.
	protected static bool s_SeeConditionBlock;
	// One gear answer per world-time. The sight check asks again for the
	// same man while it walks the room. Viewer and target stay apart so
	// a new target does not throw away the viewer's answer.
	protected static IEntity s_ViewerGearBody;
	protected static float s_ViewerGearAt = -1;
	protected static bool s_ViewerNightVision;
	protected static bool s_ViewerFlashlight;
	protected static IEntity s_TargetGearBody;
	protected static float s_TargetGearAt = -1;
	protected static bool s_TargetFlashlight;
	protected static IEntity s_MuzzleEnt;
	protected static int s_MuzzleBone = -1;
	protected static ref array<WeaponSlotComponent> s_WeaponSlots =
		new array<WeaponSlotComponent>();
	protected static const float SNAP_AIM_ERROR_MS = 400;
	protected static const float SNAP_AIM_LOG_MS = 1000;
	// Below this, the heading and the weapon angle are already what we
	// would write. Setting them again dirties the character for no change.
	protected static const float SNAP_AIM_WRITE_DEG = 0.25;
	// The burst may shoot once he is this close to the man. Waiting for
	// the heading to match exactly kept the trigger off through the turn.
	protected static const float RETURN_FIRE_AIM_DEG = 30;
	// How far the rifle may be from the point before the trigger is allowed.
	// A close man is a wide cone. It tightens out to the far distance, and
	// around 25 m it is the old 20 degrees.
	protected static const float ENGAGE_AIM_NEAR_M = 2;
	protected static const float ENGAGE_AIM_FAR_M = 40;
	protected static const float ENGAGE_AIM_NEAR_DEG = 35;
	protected static const float ENGAGE_AIM_FAR_DEG = 8;
	// Nothing is exposed, so the point is only a guess. Each burst picks
	// somewhere in this patch around it, then the next burst picks again.
	protected static const float ENGAGE_AREA_DEG = 12;
	protected static const float ENGAGE_AREA_MIN_M = 3;
	protected static const float ENGAGE_AREA_HOLD_MS = 1600;
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
	// Garrison has him on the side node and the trigger is allowed,
	// so that spot can be tested before the cool-off ends.
	protected static ref set<IEntity> s_BarrelTest = new set<IEntity>();
	protected static ref set<IEntity> s_RoomFire = new set<IEntity>();
	protected static ref array<BaseTarget> s_Perceived = new array<BaseTarget>();
	protected static ref array<IEntity> s_Candidates = new array<IEntity>();
	protected static ref array<float> s_CandidateDist = new array<float>();
	protected static ref map<IEntity, ref BaseTarget> s_CandidateKnown =
		new map<IEntity, ref BaseTarget>();
	protected static ref array<IEntity> s_NearBodies = new array<IEntity>();
	protected static IEntity s_NearViewer;
	protected static ref array<IEntity> s_QueryBodies;
	protected static ref array<IEntity> s_RoadScratch = new array<IEntity>();
	protected static ref map<IEntity, ref KK_NearBubble> s_NearBubbles =
		new map<IEntity, ref KK_NearBubble>();
	// Past the footprint, he is still on the house. Farther than this, he
	// is on the road and uses the approach sphere.
	protected static const float HOUSE_NEAR_MARGIN = 8;
	// Men walking in together stay inside this spread and share one sphere.
	// A man farther back searches on his own, so one straggler does not
	// pull the query across the map.
	protected static const float ROAD_SHARE_SPREAD = 80;
	// Milliseconds the next shot check was scheduled with.
	protected static int s_iShotTickMs;
	protected static bool s_bShotTicking;
	// When this tick last ran him. Due men are spread across frames.
	protected static ref map<IEntity, float> s_ShotServedAt =
		new map<IEntity, float>();
	// Last interval found him with a full search. This interval keeps him
	// when the center ray still hits, and the interval after searches again.
	protected static ref set<IEntity> s_MayHoldShot = new set<IEntity>();
	// The room shot already traced this man during the call running now.
	protected static IEntity s_FreshSightBody;
	// Bodies whose trigger stays down between shot checks. The check decides
	// to fire. Other behaviors clear the flag, and a full-auto gun was then
	// limited to one shot each time the check wrote it again.
	protected static ref set<IEntity> s_TriggerHeld = new set<IEntity>();
	protected static ref array<IEntity> s_TriggerBodies = new array<IEntity>();
	protected static bool s_bTriggerPumping;
	// Semi and burst fire one cycle per pull. A held button does not start
	// the next one. Full auto stays held and is not in these maps.
	protected static ref map<IEntity, int> s_TriggerRounds = new map<IEntity, int>();
	protected static ref map<IEntity, float> s_TriggerRoundAt = new map<IEntity, float>();
	protected static ref set<IEntity> s_TriggerRearm = new set<IEntity>();
	// Attack engage used to hold automatic until the contact ended. A rifle
	// past close range is moved to burst or single shot. A gun that only
	// has automatic, and a machine gun, lets go after a short burst.
	protected static ref map<IEntity, int> s_AutoBurstCap = new map<IEntity, int>();
	protected static ref map<IEntity, int> s_AutoBurstFrom = new map<IEntity, int>();
	protected static ref map<IEntity, float> s_AutoBurstGap = new map<IEntity, float>();
	protected static ref map<IEntity, float> s_AutoBurstResume = new map<IEntity, float>();
	protected static ref map<IEntity, float> s_FireModeTryAt = new map<IEntity, float>();
	protected static ref map<IEntity, vector> s_RateAim = new map<IEntity, vector>();
	protected static const float TRIGGER_REPULL_MS = 400;
	protected static const float TRIGGER_RELEASE_MS = 150;
	// A rifle may stay on automatic inside this. Farther, it uses a burst
	// if it has one, and single shots past the next band.
	protected static const float ENGAGE_AUTO_M = 30;
	protected static const float ENGAGE_BURST_M = 80;
	protected static const float MG_BURST_NEAR_M = 40;
	protected static const float MG_BURST_FAR_M = 100;
	// Teammates domed in the back are the man just ahead. The ray stops here.
	protected static const float LANE_NEAR_M = 20;
	// A bullet that hit him, struck nearby, or has an open path from a
	// shooter he heard. A squadmate this close counts as the same shot.
	protected static const float SQUAD_RETURN_M = 80;
	// Five rays around the muzzle. One that reaches him means the path
	// is open. Farther apart starts to test a different gap.
	protected static const float PATH_RAY_SPREAD = 0.3;
	// Vanilla nearby impact. The strike is inside this of his feet.
	protected static const float IMPACT_RADIUS = 10;
	protected static ref TraceParam s_LaneTrace;
	protected static IEntity s_LaneViewer;
	protected static IEntity s_LaneTarget;
	protected static bool s_LaneHitFriendly;
	// The ray reached something that is not brush. A heard shot does not
	// fire through that.
	protected static bool s_LaneHitSolid;
	protected static const float ROOM_SCAN_RADIUS = 35;
	protected static const int SHOT_CANDIDATES = 8;
	protected static const float LEAN_OFFSET = 0.4;
	// Origin is the feet. A look at that point from arm's length points the rifle down.
	protected static const float SHOT_AIM_HEIGHT = 1.5;
	// Upper chest. Spine2 sits on the abdomen, and from a muzzle above it a
	// close shot pitches down into the stomach. The head point is the skull.
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
	// Heading commanded for the burst. The live yaw stays down the lane
	// while he is planted, so the shot cannot wait on that pose.
	protected static ref map<IEntity, float> s_ReturnFacing = new map<IEntity, float>();
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
	protected static ref set<IEntity> s_DidReload = new set<IEntity>();
	// Reload was in progress last check. The frame it ends, the rifle is
	// already up and the aiming component is still on the reload pose.
	protected static ref set<IEntity> s_ReloadWasActive = new set<IEntity>();
	protected static ref map<IEntity, float> s_ReloadReadyAt = new map<IEntity, float>();
	protected static const float RELOAD_AIM_SETTLE_MS = 2000;
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
		return LifeIsDown(Controller(soldier));
	}

	protected static bool LifeIsDown(CharacterControllerComponent controller)
	{
		if (!controller || controller.IsDead())
			return false;

		if (controller.IsUnconscious())
			return true;

		return controller.GetLifeState() == ECharacterLifeState.INCAPACITATED;
	}

	// On his feet and in the CQB fight. Dead, unconscious, and incapacitated
	// are not. Snap aim writes the body, so it stays off for all three.
	protected static bool LifeStanding(CharacterControllerComponent controller)
	{
		if (!controller || controller.IsDead() || controller.IsUnconscious())
			return false;

		return controller.GetLifeState() == ECharacterLifeState.ALIVE;
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
		if (!agent)
			return;

		SCR_AIGroup group = SCR_AIGroup.Cast(agent.GetParentGroup());
		bool seen = s_OrderNoted.Contains(agent);
		s_OrderNoted.Set(agent, group);
		if (seen)
			return;

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

			s_OrderNoted.Remove(agent);
			EndFreshOrder(agent);
		}

		// A man who left this group is not in that list. His fresh-order
		// flag would keep clearing targets on the squad he joined.
		array<AIAgent> noted = {};
		for (int i = 0; i < s_OrderNoted.Count(); i++)
			noted.Insert(s_OrderNoted.GetKey(i));

		foreach (AIAgent agent : noted)
		{
			if (!agent || s_OrderNoted.Get(agent) != group)
				continue;

			s_OrderNoted.Remove(agent);
			EndFreshOrder(agent);
		}
	}

	static bool IsFreshOrder(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_FreshOrder.Contains(body))
			return false;

		if (!LeftNotedGroup(soldier))
			return true;

		s_FreshOrder.RemoveItem(body);
		return false;
	}

	// True when this man was noted on a squad and he is no longer in it.
	protected static bool LeftNotedGroup(IEntity soldier)
	{
		AIAgent agent = AIAgent.Cast(soldier);
		if (!agent)
		{
			IEntity body = CharacterBody(soldier);
			AIControlComponent control;
			if (body)
			{
				control = AIControlComponent.Cast(
					body.FindComponent(AIControlComponent)
				);
			}

			if (control)
				agent = control.GetAIAgent();
		}

		if (!agent || !s_OrderNoted.Contains(agent))
			return false;

		SCR_AIGroup noted = s_OrderNoted.Get(agent);
		SCR_AIGroup parent = SCR_AIGroup.Cast(agent.GetParentGroup());
		if (noted && parent == noted)
			return false;

		s_OrderNoted.Remove(agent);
		return true;
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
		s_ReturnFacing.Remove(body);
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

		// Rounds are not landing. The trigger stays off for the walk.
		// A garrison side node turns it back on once he is standing there.
		if (BarrelWantsMove(body) && !IsBarrelTest(body))
			canShoot = false;

		float command = 0;
		if (canShoot)
			command = lean;

		RememberLean(body, command);
		if (canShoot && lookAt)
		{
			NoteNoDamage(body, lookAt);
			vector ratePoint = ShotAimPoint(body, lookAt);
			if (ratePoint != vector.Zero)
				ApplyEngageDiscipline(body, ratePoint);
		}

		// The plant keeps his feet. It also refuses the combat-move turn,
		// so the burst has to put the body on the man itself. The trigger
		// waits until that heading is actually his.
		if (canShoot && s_InstantAim.Contains(body))
			FaceReturnShot(body, lookAt);

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
		SetFireWanted(body, false);
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
		SetFireWanted(body, false);
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

		s_WeaponSlots.Clear();
		manager.GetWeaponsSlots(s_WeaponSlots);
		foreach (WeaponSlotComponent slot : s_WeaponSlots)
		{
			if (!slot)
				continue;

			// A round already in the gun is enough. The spare-magazine
			// search is only for a weapon that is empty in hand.
			if (LoadedRounds(WeaponInSlot(slot)) > 0)
				return true;
		}

		foreach (WeaponSlotComponent slot : s_WeaponSlots)
		{
			if (!slot)
				continue;

			if (AvailableAmmo(body, WeaponInSlot(slot)) > 0)
				return true;
		}

		return false;
	}

	// Rounds in the chamber or the loaded magazine. Spares in the vest
	// are not counted.
	protected static int LoadedRounds(BaseWeaponComponent weapon)
	{
		if (!weapon || IsThrowableType(weapon.GetWeaponType()))
			return 0;

		array<BaseMuzzleComponent> muzzles = {};
		weapon.GetMuzzlesList(muzzles);
		if (muzzles.Count() == 0 || !muzzles[0])
			return 0;

		BaseMuzzleComponent muzzle = muzzles[0];
		int ammo = muzzle.GetAmmoCount();
		if (muzzle.IsDisposable())
			return ammo;

		BaseMagazineComponent loaded = muzzle.GetMagazine();
		if (loaded && loaded.GetAmmoCount() > ammo)
			ammo = loaded.GetAmmoCount();

		return ammo;
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
		if (YieldsWeaponForHeal(body))
			return false;

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

		if (YieldsWeaponForHeal(body))
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
		SetFireWanted(body, false);
		s_MoveFiring.RemoveItem(body);
		if (controller.ReloadWeaponWith(spare, forceDetach))
		{
			s_ReloadAt.Set(body, now);
			s_DidReload.Insert(body);
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

	// IsReloading stays true after the magazine is seated and the rifle is
	// back up. That tail is not a reload: the count is already in the gun.
	protected static bool ReloadBlocksFire(IEntity body)
	{
		CharacterControllerComponent controller = Controller(body);
		if (!controller || !controller.IsReloading())
			return false;

		if (!GunIsDry(body) && WeaponIsRaised(body))
			return false;

		return true;
	}

	// The building shot keeps the trigger down while an empty reload is
	// playing. A raised rifle with rounds in it is done, even if the
	// controller still says the reload is going.
	protected static bool IsEmptyReload(IEntity body)
	{
		if (!body || !s_EmptyReload.Contains(body))
			return false;

		if (ReloadBlocksFire(body))
			return true;

		s_EmptyReload.RemoveItem(body);
		return false;
	}

	// The magazine change reports as an item change after IsReloading has
	// already dropped. The rifle is up. The aim direction is still the
	// pose from the reload, so the cone check stays false and the trigger
	// pump clears a shot it just asked for.
	protected static void NoteReloadPose(IEntity body)
	{
		if (!body)
			return;

		bool active = ReloadBlocksFire(body) || IsEmptyReload(body);
		bool was = s_ReloadWasActive.Contains(body);
		if (active)
		{
			s_ReloadWasActive.Insert(body);
			return;
		}

		if (!was)
			return;

		s_ReloadWasActive.RemoveItem(body);
		if (!GunIsDry(body))
			s_ReloadReadyAt.Set(body, WorldTime());
	}

	protected static bool ReloadAimSettling(IEntity body)
	{
		if (!body || !s_ReloadReadyAt.Contains(body))
			return false;

		if (WorldTime() - s_ReloadReadyAt.Get(body) > RELOAD_AIM_SETTLE_MS)
		{
			s_ReloadReadyAt.Remove(body);
			return false;
		}

		return WeaponIsRaised(body);
	}

	// Body heading, used when the aiming component has not left the reload
	// pose yet. A man behind him does not count.
	protected static bool FacingAimPoint(IEntity body, vector point)
	{
		vector flat = point - body.GetOrigin();
		flat[1] = 0;
		float distance = flat.Length();
		if (distance < 0.05)
			return true;

		flat.Normalize();
		float gap = YawDelta(body.GetYawPitchRoll()[0], flat.ToYaw());
		return Math.AbsFloat(gap) <= EngageAimDegrees(distance);
	}

	// True while an empty reload this mod started is still finishing.
	static bool EngageReloadRunning(IEntity soldier)
	{
		return IsEmptyReload(CharacterBody(soldier));
	}

	protected static bool GunIsDry(IEntity body)
	{
		BaseMuzzleComponent muzzle = PrimaryMuzzle(body);
		if (!muzzle)
		{
			BaseWeaponComponent weapon = CurrentWeapon(body);
			if (!weapon || IsThrowableType(weapon.GetWeaponType()))
				return false;

			return LoadedRounds(weapon) <= 0;
		}

		if (muzzle.GetAmmoCount() > 0)
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
		// The cover run is a clear or garrison step. Anyone else keeps
		// the combat move he already has.
		if (!body || !ReloadCoverEnabled() || !OrderOwnsBehavior(body))
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
		if (!body || !OrderOwnsBehavior(body) || !ReloadSprintEnabled() || !ReloadCoverEnabled() || !GunIsDry(body))
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
		SetFireWanted(body, false);
		if (controller)
		{
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

	// IsWeaponRaised stays true through low ready. The game's own raise
	// also clears that, and a fire input does nothing until it is clear.
	// A full magazine with an empty chamber is the same: the count stays
	// put while the trigger is held.
	protected static void MakeWeaponReady(IEntity body)
	{
		CharacterControllerComponent controller = Controller(body);
		if (!controller)
			return;

		if (controller.IsPartiallyLowered())
			controller.SetPartialLower(false);

		BaseWeaponComponent weapon = CurrentWeapon(body);
		if (weapon && weapon.GetCurrentFireModeType() == EWeaponFiremodeType.Safety)
			controller.SetSafety(false, false);

		if (controller.IsReloading() || controller.IsChangingItem())
			return;

		if (!weapon)
			return;

		// The shot uses the current muzzle. A rifle magazine on the first
		// muzzle does not feed a launcher muzzle that is selected instead.
		BaseMuzzleComponent muzzle = weapon.GetCurrentMuzzle();
		array<BaseMuzzleComponent> muzzles = {};
		weapon.GetMuzzlesList(muzzles);
		if (
			muzzle &&
			muzzle.GetAmmoCount() <= 0 &&
			muzzles.Count() > 1 &&
			muzzles[0] &&
			muzzles[0] != muzzle &&
			MuzzleHasRound(muzzles[0])
		)
		{
			LogFire(body, "switch-muzzle");
			controller.SetMuzzle(0);
		}

		muzzle = weapon.GetCurrentMuzzle();
		if (!NeedsChamber(muzzle) || s_DidReload.Contains(body))
			return;

		float now = WorldTime();
		if (s_ReloadAt.Contains(body) && now - s_ReloadAt.Get(body) < RELOAD_RETRY_MS)
			return;

		s_ReloadAt.Set(body, now);
		LogFire(body, "chamber");
		controller.ReloadWeapon();
	}

	// Magazine is in, the muzzle that would fire has no round. Holding the
	// trigger does not chamber it, and the logged count stays on the magazine.
	protected static bool MuzzleHasRound(BaseMuzzleComponent muzzle)
	{
		if (!muzzle)
			return false;

		if (muzzle.GetAmmoCount() > 0)
			return true;

		if (muzzle.IsDisposable())
			return false;

		BaseMagazineComponent loaded = muzzle.GetMagazine();
		return loaded && loaded.GetAmmoCount() > 0 && muzzle.IsCurrentBarrelChambered();
	}

	protected static bool NeedsChamber(BaseMuzzleComponent muzzle)
	{
		if (!muzzle || muzzle.IsDisposable() || !muzzle.IsChamberingPossible())
			return false;

		if (muzzle.GetAmmoCount() > 0 || muzzle.IsCurrentBarrelChambered())
			return false;

		BaseMagazineComponent loaded = muzzle.GetMagazine();
		return loaded && loaded.GetAmmoCount() > 0;
	}

	protected static bool WeaponCanAcceptFire(IEntity body)
	{
		CharacterControllerComponent controller = Controller(body);
		if (!controller || controller.IsPartiallyLowered() || !controller.GetCanFireWeapon())
			return false;

		BaseWeaponComponent weapon = CurrentWeapon(body);
		if (!weapon)
			return false;

		if (weapon.GetCurrentFireModeType() == EWeaponFiremodeType.Safety)
			return false;

		if (NeedsChamber(weapon.GetCurrentMuzzle()))
			return false;

		return true;
	}

	// Only command the weapon when the stance changes. Repeating the same
	// raise or lower restarts the animation.
	protected static void CommandWeapon(IEntity body, bool raised, bool fire)
	{
		if (!body)
			return;

		if (YieldsWeaponForHeal(body))
		{
			LeaveWeaponForHeal(body);
			return;
		}

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

		// Attack engage waits until the rifle reports up. A clear or a
		// garrison already decided this shot, and holding the trigger off
		// for that report is what kept snap fire from leaving the barrel.
		if (IsAttackSearch(body))
		{
			if (fire)
				MakeWeaponReady(body);

			if (fire && (!WeaponIsRaised(body) || !WeaponCanAcceptFire(body)))
				fire = false;
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

	// The whole clear, including the gap between one node and the next.
	// IsClearing drops in that gap, and heal would take it.
	protected static ref set<IEntity> s_ClearOrder = new set<IEntity>();

	// World time the cleared cluster started waiting on a bandage.
	// Absent means this man is still clearing, or he does not need one.
	protected static ref map<IEntity, float> s_ClearHealSince =
		new map<IEntity, float>();

	// A garrison hold. Binoculars are allowed only here.
	protected static ref set<IEntity> s_GarrisonPost = new set<IEntity>();

	protected static const float CLEAR_HEAL_HOLD_MS = 30000;

	protected static ref map<IEntity, float> s_NextHealAt = new map<IEntity, float>();

	// The item whose gesture is playing. Absent means he is not in one.
	protected static ref map<IEntity, IEntity> s_HealGestureItem = new map<IEntity, IEntity>();

	protected static ref map<IEntity, float> s_HealGestureAt = new map<IEntity, float>();

	protected static ref set<IEntity> s_HealGestureSeen = new set<IEntity>();

	protected static ref set<IEntity> s_HealFootLock = new set<IEntity>();

	// No usable item. He is not held in the heal pose until this time.
	protected static ref map<IEntity, float> s_HealEmptyUntil = new map<IEntity, float>();

	protected static ref array<IEntity> s_HealItems = new array<IEntity>();

	// Clearing a house, or holding one. Attack uses neither of these.
	static bool OnHouseOrder(IEntity soldier)
	{
		return IsClearing(soldier) || HasBuilding(soldier);
	}

	// Clear, garrison, or attack, including while he is down. OrderOwnsBehavior
	// drops out then, and these sets are what still mark the order.
	static bool OnCqbOrder(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		if (IsAttackSearch(body))
			return true;

		return s_OrderBehavior.Contains(body) || OnHouseOrder(body);
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

	// On for every man from the moment the clear is selected until it ends.
	static void SetClearOrder(IEntity soldier, bool active)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (active)
		{
			s_ClearOrder.Insert(body);
			return;
		}

		s_ClearOrder.RemoveItem(body);
		s_ClearHealSince.Remove(body);
	}

	static bool OnClearOrder(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_ClearOrder.Contains(body);
	}

	// Bleeding, or health already taken off. A full man does not stay behind.
	static bool NeedsMedical(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || IsDown(body))
			return false;

		SCR_CharacterDamageManagerComponent damage =
			SCR_CharacterDamageManagerComponent.Cast(
				body.FindComponent(SCR_CharacterDamageManagerComponent)
			);
		if (!damage)
			return false;

		if (damage.IsBleeding())
			return true;

		return damage.GetHealthScaled() < 0.999;
	}

	// The cluster he was clearing is done, and he still needs a bandage.
	// The next cluster waits. The hold ends when he is patched, or when
	// it has gone on long enough that he is going on without one.
	static void OfferClearHeal(IEntity soldier, float now)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (!NeedsMedical(body))
		{
			s_ClearHealSince.Remove(body);
			return;
		}

		if (!s_ClearHealSince.Contains(body))
			s_ClearHealSince.Set(body, now);
	}

	static void CloseClearHeal(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		s_ClearHealSince.Remove(body);
	}

	static bool ClearHealOpen(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_ClearHealSince.Contains(body))
			return false;

		float now = 0;
		if (GetGame() && GetGame().GetWorld())
			now = GetGame().GetWorld().GetWorldTime();

		float started = s_ClearHealSince.Get(body);
		if (!NeedsMedical(body) || now - started > CLEAR_HEAL_HOLD_MS)
		{
			s_ClearHealSince.Remove(body);
			return false;
		}

		return true;
	}

	// Vanilla heal stays off while a clear is still in the room, and while
	// our own treatment is the thing using his medical items.
	static bool ClearHealSuppressed(SCR_AIUtilityComponent utility)
	{
		IEntity body = UtilityBody(utility);
		if (OnClearOrder(body) && !ClearHealOpen(body))
			return true;

		return CustomHealAllowed(body);
	}

	// Our gesture is the bandage. The vanilla action would start a second
	// one, and the two cut each other off.
	static bool ClearHealBoost(SCR_AIUtilityComponent utility)
	{
		return false;
	}

	// Hurt, and on a clear, garrison, or attack. A live clear cluster
	// finishes first. Anyone outside those orders keeps the normal heal.
	static bool CustomHealAllowed(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!NeedsMedical(body))
			return false;

		// Attack heals on its own clock. A live engage contact is still the
		// fight, so the bandage does not start and he does not count as healing.
		if (IsAttackSearch(body))
			return !AttackContactOpen(body);

		if (!OrderOwnsBehavior(body))
			return false;

		if (OnClearOrder(body) && !ClearHealOpen(body))
			return false;

		return true;
	}

	static void TickCustomHeal(IEntity soldier)
	{
		if (!Replication.IsServer())
			return;

		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (s_HealGestureItem.Contains(body))
		{
			WatchHealGesture(body);
			return;
		}

		// A retry timer must not keep the speed lock on a soldier who is
		// not on a clear, garrison, or attack.
		if (!CustomHealAllowed(body))
		{
			ReleaseHealHold(body);
			return;
		}

		float now = WorldTime();
		if (s_NextHealAt.Contains(body) && now < s_NextHealAt.Get(body))
			return;

		HoldForHeal(body);

		CharacterControllerComponent controller = Controller(body);
		if (!controller)
			return;

		// The gesture will not start while the rifle is up, a swap is
		// playing, or he is still in the run. Wait for those to finish.
		if (
			controller.IsWeaponRaised() ||
			controller.IsWeaponADS() ||
			controller.IsChangingItem() ||
			controller.IsReloading() ||
			controller.IsSprinting() ||
			controller.IsUsingItem()
		)
		{
			return;
		}

		IEntity item = FindMedicalItem(body);
		if (!item)
		{
			// Nothing he is carrying can treat this. Holding the pose
			// will not grow a bandage.
			ReleaseHealHold(body);
			s_HealEmptyUntil.Set(body, now + 2000);
			s_NextHealAt.Set(body, now + 2000);
			if (OnClearOrder(body))
				CloseClearHeal(body);
			return;
		}

		s_HealEmptyUntil.Remove(body);
		if (!StartMedicalGesture(body, item))
		{
			s_NextHealAt.Set(body, now + 400);
			return;
		}

		s_HealGestureItem.Set(body, item);
		s_HealGestureAt.Set(body, now);
		s_HealGestureSeen.RemoveItem(body);
	}

	// The gesture owns him until the item use ends. A second start
	// while it is playing cuts the first one off.
	protected static void WatchHealGesture(IEntity body)
	{
		HoldForHeal(body);

		CharacterControllerComponent controller = Controller(body);
		if (!controller)
			return;

		float now = WorldTime();
		float started = 0;
		if (s_HealGestureAt.Contains(body))
			started = s_HealGestureAt.Get(body);

		if (controller.IsUsingItem())
		{
			s_HealGestureSeen.Insert(body);
			return;
		}

		// Taking the bandage out is still the gesture. A change that
		// never finishes is the lower being asked for again.
		if (controller.IsChangingItem() && now - started < 1500)
			return;

		if (!s_HealGestureSeen.Contains(body) && now - started < 800)
			return;

		s_HealGestureItem.Remove(body);
		s_HealGestureAt.Remove(body);
		s_HealGestureSeen.RemoveItem(body);

		// A short gap so the next item does not cut the recover.
		s_NextHealAt.Set(body, now + 400);
		if (!CustomHealAllowed(body))
			ReleaseHealHold(body);
	}

	// Bandage or tourniquet while he is bleeding. Morphine once it can
	// treat the leg. A health item after that, or when the wound is
	// only missing health.
	protected static IEntity FindMedicalItem(IEntity body)
	{
		SCR_InventoryStorageManagerComponent inventory =
			SCR_InventoryStorageManagerComponent.Cast(
				body.FindComponent(SCR_InventoryStorageManagerComponent)
			);
		if (!inventory)
			return null;

		s_HealItems.Clear();
		inventory.GetItems(s_HealItems);

		bool bleeding = false;
		SCR_CharacterDamageManagerComponent damage =
			SCR_CharacterDamageManagerComponent.Cast(
				body.FindComponent(SCR_CharacterDamageManagerComponent)
			);
		if (damage)
			bleeding = damage.IsBleeding();

		IEntity best;
		int bestRank = 0;
		foreach (IEntity item : s_HealItems)
		{
			if (!item)
				continue;

			SCR_ConsumableItemComponent consumable =
				SCR_ConsumableItemComponent.Cast(
					item.FindComponent(SCR_ConsumableItemComponent)
				);
			if (!consumable)
				continue;

			SCR_ConsumableEffectHealthItems effect =
				SCR_ConsumableEffectHealthItems.Cast(consumable.GetConsumableEffect());
			if (!effect)
				continue;

			int rank = MedicalRank(consumable.GetConsumableType(), bleeding);
			if (rank <= 0)
				continue;

			if (best && rank >= bestRank)
				continue;

			SCR_EConsumableFailReason reason;
			if (!effect.CanApplyEffect(body, body, reason))
				continue;

			if (!effect.GetAnimationParameters(item, body))
				continue;

			best = item;
			bestRank = rank;
		}

		return best;
	}

	protected static int MedicalRank(SCR_EConsumableType type, bool bleeding)
	{
		if (bleeding)
		{
			if (type == SCR_EConsumableType.BANDAGE)
				return 1;

			if (type == SCR_EConsumableType.TOURNIQUET)
				return 2;

			if (type == SCR_EConsumableType.MED_KIT)
				return 3;
		}

		// Skipped unless the leg will take it. A bleed is stopped first.
		if (type == SCR_EConsumableType.MORPHINE)
			return 4;

		if (type == SCR_EConsumableType.SALINE)
			return 5;

		if (type == SCR_EConsumableType.MED_KIT)
			return 6;

		if (type == SCR_EConsumableType.HEALTH)
			return 7;

		return 0;
	}

	// Starts the consumable gesture. The effect lands when that gesture
	// finishes. Applying it here would skip the animation.
	protected static bool StartMedicalGesture(IEntity body, IEntity item)
	{
		if (!body || !item)
			return false;

		SCR_ConsumableItemComponent consumable =
			SCR_ConsumableItemComponent.Cast(
				item.FindComponent(SCR_ConsumableItemComponent)
			);
		if (!consumable)
			return false;

		SCR_ConsumableEffectHealthItems effect =
			SCR_ConsumableEffectHealthItems.Cast(consumable.GetConsumableEffect());
		if (!effect)
			return false;

		return effect.ActivateEffect(body, body, item, null);
	}

	protected static ref set<IEntity> s_MedicDetail = new set<IEntity>();

	// Off the push, treating or being treated. Engage does not take his rifle.
	static void SetMedicDetail(IEntity soldier, bool on)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (on)
			s_MedicDetail.Insert(body);
		else
			s_MedicDetail.RemoveItem(body);
	}

	static bool MedicDetail(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_MedicDetail.Contains(body);
	}

	// True when this man is carrying something that can be used on that patient.
	static bool CanTreatPatient(IEntity medic, IEntity patient)
	{
		return FindTreatItem(medic, patient) != null;
	}

	// Plays the medic's item on the patient. False when nothing he carries applies.
	static bool TryMedicTreat(IEntity medic, IEntity patient)
	{
		IEntity medicBody = CharacterBody(medic);
		IEntity patientBody = CharacterBody(patient);
		if (!medicBody || !patientBody || medicBody == patientBody)
			return false;

		CharacterControllerComponent controller = Controller(medicBody);
		if (!controller || controller.IsDead() || controller.IsUnconscious())
			return false;

		if (controller.IsUsingItem() || controller.IsChangingItem())
			return true;

		if (controller.IsWeaponRaised() || controller.IsWeaponADS() || controller.IsSprinting())
		{
			LeaveWeaponForHeal(medicBody);
			return false;
		}

		IEntity item = FindTreatItem(medicBody, patientBody);
		if (!item)
			return false;

		if (!ActivateTreat(patientBody, medicBody, item))
			return false;

		// Any treatment item, not only a bandage. A contact must not cut the gesture off.
		s_HealGestureItem.Set(medicBody, item);
		s_HealGestureAt.Set(medicBody, WorldTime());
		s_HealGestureSeen.RemoveItem(medicBody);
		return true;
	}

	protected static IEntity FindTreatItem(IEntity medic, IEntity patient)
	{
		SCR_InventoryStorageManagerComponent inventory =
			SCR_InventoryStorageManagerComponent.Cast(
				medic.FindComponent(SCR_InventoryStorageManagerComponent)
			);
		if (!inventory || !patient)
			return null;

		bool bleeding = false;
		SCR_CharacterDamageManagerComponent damage =
			SCR_CharacterDamageManagerComponent.Cast(
				patient.FindComponent(SCR_CharacterDamageManagerComponent)
			);
		if (damage)
			bleeding = damage.IsBleeding();

		s_HealItems.Clear();
		inventory.GetItems(s_HealItems);

		IEntity best;
		int bestRank = 0;
		foreach (IEntity item : s_HealItems)
		{
			if (!item)
				continue;

			SCR_ConsumableItemComponent consumable =
				SCR_ConsumableItemComponent.Cast(
					item.FindComponent(SCR_ConsumableItemComponent)
				);
			if (!consumable)
				continue;

			SCR_ConsumableEffectHealthItems effect =
				SCR_ConsumableEffectHealthItems.Cast(consumable.GetConsumableEffect());
			if (!effect)
				continue;

			int rank = MedicalRank(consumable.GetConsumableType(), bleeding);
			if (rank <= 0)
				continue;

			if (best && rank >= bestRank)
				continue;

			SCR_EConsumableFailReason reason;
			if (!effect.CanApplyEffect(patient, medic, reason))
				continue;

			if (!effect.GetAnimationParameters(item, medic))
				continue;

			best = item;
			bestRank = rank;
		}

		return best;
	}

	protected static bool ActivateTreat(IEntity patient, IEntity medic, IEntity item)
	{
		if (!patient || !medic || !item)
			return false;

		SCR_ConsumableItemComponent consumable =
			SCR_ConsumableItemComponent.Cast(
				item.FindComponent(SCR_ConsumableItemComponent)
			);
		if (!consumable)
			return false;

		SCR_ConsumableEffectHealthItems effect =
			SCR_ConsumableEffectHealthItems.Cast(consumable.GetConsumableEffect());
		if (!effect)
			return false;

		return effect.ActivateEffect(patient, medic, item, null);
	}

	// True while he should be bandaging, or a gesture is already playing.
	// An empty inventory does not count, or he would stand in the pose
	// with nothing to use.
	static bool HealingNow(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		if (s_HealGestureItem.Contains(body))
			return true;

		float now = WorldTime();
		if (s_HealEmptyUntil.Contains(body) && now < s_HealEmptyUntil.Get(body))
			return false;

		return CustomHealAllowed(body);
	}

	// Rifle down, feet stopped. Repeated while the gesture plays so a
	// later move or raise does not cut it off.
	static void HoldForHeal(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		LeaveWeaponForHeal(body);

		// A bound or take-cover dash is still flagged after the contact
		// closes. That flag puts the sprint back and the bandage never starts.
		SetBoundSprint(body, false);

		CharacterControllerComponent controller = Controller(body);
		if (controller && controller.IsWeaponADS())
			controller.SetWeaponADS(false);

		// The plant end writes full speed every refresh. The lock has to
		// be put back, or a hurt man keeps walking the order.
		if (controller)
		{
			s_HealFootLock.Insert(body);
			controller.OverrideMaxSpeed(0);
		}

		AICharacterMovementComponent movement =
			AICharacterMovementComponent.Cast(
				body.FindComponent(AICharacterMovementComponent)
			);
		if (movement)
			movement.SetMovementTypeWanted(EMovementType.IDLE);
	}

	static void ReleaseHealHold(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_HealFootLock.Contains(body))
			return;

		s_HealFootLock.RemoveItem(body);
		if (IsPinned(body) || IsEngageStopped(body))
			return;

		CharacterControllerComponent controller = Controller(body);
		if (controller)
			controller.OverrideMaxSpeed(1);

		// The hold left idle speed. Put a run back so he is not stuck
		// after the bandage, including a hold that should not have started.
		AICharacterMovementComponent movement =
			AICharacterMovementComponent.Cast(
				body.FindComponent(AICharacterMovementComponent)
			);
		if (movement)
			movement.SetMovementTypeWanted(EMovementType.RUN);
	}

	// A bandage needs the hands. Drawing the gun, or holding it up,
	// restarts that swap and the item never comes out.
	static bool YieldsWeaponForHeal(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		if (HealingNow(body))
			return true;

		// A vanilla heal on someone outside these orders is his own.
		if (!OnCqbOrder(body))
			return false;

		return HealBehaviorRunning(body);
	}

	static bool HealBehaviorRunning(IEntity soldier)
	{
		SCR_AIUtilityComponent utility = UtilityOf(CharacterBody(soldier));
		if (!utility)
			return false;

		SCR_AIActionBase action =
			SCR_AIActionBase.Cast(utility.GetExecutedAction());
		return SCR_AIHealBehavior.Cast(action) ||
			SCR_AIHealWaitBehavior.Cast(action) ||
			SCR_AIMedicHealBehavior.Cast(action);
	}

	// Lowers once. A repeat while it is already down does not restart it.
	static void LeaveWeaponForHeal(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		SetFireWanted(body, false);

		// The gesture has the hands. Another lower restarts the draw
		// and the bandage never finishes.
		if (s_HealGestureItem.Contains(body))
			return;

		CharacterControllerComponent controller = Controller(body);
		if (controller && (controller.IsUsingItem() || controller.IsChangingItem()))
			return;

		RequestWeaponStance(body, false);
	}

	static void SetGarrisonPost(IEntity soldier, bool holding)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (holding)
			s_GarrisonPost.Insert(body);
		else
			s_GarrisonPost.RemoveItem(body);
	}

	static bool AllowsBinoculars(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_GarrisonPost.Contains(body);
	}

	protected static IEntity UtilityBody(SCR_AIUtilityComponent utility)
	{
		if (!utility)
			return null;

		if (utility.m_OwnerEntity)
			return utility.m_OwnerEntity;

		AIAgent agent = AIAgent.Cast(utility.GetOwner());
		if (!agent)
			return null;

		return agent.GetControlledEntity();
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

	// 0 runs every due man in the one wake.
	static int ShotMenPerTick()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 2;

		return mode.KK_GetShotMenPerTick();
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
		if (!KK_PerfLog.On())
		{
			ApplyRoomShotRun(utility);
			return;
		}

		int started = System.GetTickCount();
		ApplyRoomShotRun(utility);
		KK_PerfLog.End("CQB.RoomShot", started);
	}

	protected static void ApplyRoomShotRun(SCR_AIUtilityComponent utility)
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

		s_FreshSightBody = null;
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

		// The search already traced him. A second ray in this same call
		// was half of the room shot, and it could only repeat that answer.
		bool canShoot;
		if (s_FreshSightBody == body)
			canShoot = true;
		else
		{
			bool center = ShotStillVisible(body);
			canShoot = center;
			if (!canShoot && lean != 0)
				canShoot = SideStillClear(body, enemy, lean);
		}

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

		// Rounds are not landing. The trigger stays off for the walk.
		// A garrison side node turns it back on once he is standing there.
		if (BarrelWantsMove(body) && !IsBarrelTest(body))
			canShoot = false;

		float command = 0;
		if (canShoot)
			command = lean;

		RememberLean(body, command);
		if (canShoot && enemy)
		{
			NoteNoDamage(body, enemy);
			vector ratePoint = ShotAimPoint(body, enemy);
			if (ratePoint != vector.Zero)
				ApplyEngageDiscipline(body, ratePoint);
		}

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
		s_AttackGatherAt.Remove(body);
		s_AttackGather.Remove(body);
		s_CoverShift.RemoveItem(body);
		s_LaneBlockedAt.Remove(body);
		s_HitBy.Remove(body);
		s_HitByKind.Remove(body);
		s_HitByAt.Remove(body);
		EndEngageEpisode(body, false);
		ReleasePerceivedEnemy(body);
		ForgetSprintEnemy(body);
		RefreshSearchPerception(body);
	}

	// Perception has noticed him, identified or not. Aim and fire while he
	// is already stopped. A first sighting, and a later interval, stop him
	// for a short burst and then let the move go. A dry gun is his again,
	// so the reload is not held down by this trigger.
	static void PressPerceivedEnemy(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !AttackEngageEnabled() || !IsAttackSearch(body))
		{
			if (body)
				s_EngageUnarmed.RemoveItem(body);

			EndEngageEpisode(body, false);
			ReleasePerceivedEnemy(body);
			return;
		}

		// Nothing he is carrying can shoot. Leave him to the base attack,
		// which can scavenge. A gun with rounds puts engage back.
		// A reload in progress hides the magazine from the ammo check.
		// That is not an empty vest, and bailing here never logs again.
		if (!HasGunAmmo(body) && !ReloadBlocksFire(body))
		{
			LogEngage(body, "no-ammo");
			if (!s_EngageUnarmed.Contains(body))
			{
				s_EngageUnarmed.Insert(body);
				EndEngageEpisode(body, false);
			}

			SetEngageStop(body, false);
			ReleasePerceivedEnemy(body);
			return;
		}

		s_EngageUnarmed.RemoveItem(body);
		NoteReloadPose(body);

		// Hands on a bandage or a grenade report a raised rifle and then
		// refuse the shot. The rifle has to be the current weapon first.
		EnsureEngageGun(body);

		if (CannotShoot(body))
		{
			SetEngageStop(body, false);
			// The look is dropped for the reload. The stored point has to
			// keep its clock, or the 400 ms grace ends before the magazine
			// is in and he has to spot the man again.
			CharacterControllerComponent reloading = Controller(body);
			if (
				(reloading && reloading.IsReloading()) ||
				IsEmptyReload(body)
			)
				HoldEngageAimClock(body);

			LogEngage(
				body,
				string.Format(
					"reload rounds=%1 reloading=%2 raised=%3",
					LoadedRounds(CurrentWeapon(body)),
					reloading && reloading.IsReloading(),
					WeaponIsRaised(body)
				)
			);
			ReleasePerceivedEnemy(body);
			// The base attack keeps this behavior, so its reload never
			// gets selected. Start the magazine change from here.
			StartEngageReload(body);
			return;
		}

		IEntity enemy;
		vector aim;
		bool perceived = PerceivedEnemy(body, enemy, aim);
		if (!perceived && !RecentEngageAim(body, aim))
		{
			LogEngage(body, "no-target");
			EndEngageEpisode(body, true);
			ReleasePerceivedEnemy(body);
			return;
		}

		if (!perceived && s_EngageAim.Contains(body))
			enemy = s_EngageAim.Get(body);

		if (perceived)
			RememberEngageAim(body, aim);

		float now = WorldTime();
		NoteEngageContact(body, now);
		NotePriorityTarget(body, enemy, now);

		if (EngageHoldActive(body, now))
		{
			SetEngageStop(body, !IsCoverShift(body));
			HoldEngageShot(body, enemy, aim);
			return;
		}

		// The next plant starts on the interval, including while the step
		// is still the move. Waiting for him to slow down never starts it.
		if (EngagePeriodicDue(body, now))
		{
			BeginEngageHold(body, now, EngageRepeatMs());
			SetEngageStop(body, !IsCoverShift(body));
			HoldEngageShot(body, enemy, aim);
			return;
		}

		SetEngageStop(body, false);

		// A step that wants him moving owns the body until that plant.
		// Writing the enemy look here lasts the whole contact, and the
		// advance order turns him the other way for that same stretch.
		if (StepWantsMove(body))
		{
			if (s_PerceivedFire.Contains(body))
				ReleasePerceivedEnemy(body);

			vector point = aim;
			if (point == vector.Zero && enemy)
				point = ShotAimPoint(enemy);

			bool laneClear = point == vector.Zero || !FriendlyInEngageLane(body, enemy, point);
			NoteEngageLane(body, point != vector.Zero && !laneClear);
			if (
				point != vector.Zero &&
				EngageAimOnPoint(body, point) &&
				WeaponIsRaised(body) &&
				WeaponCanAcceptFire(body) &&
				!LauncherAgainstInfantry(body, enemy) &&
				laneClear
			)
			{
				ApplyEngageDiscipline(body, point);
				CharacterControllerComponent moving = Controller(body);
				if (moving && moving.IsChangingFireMode())
					return;

				PressFire(body);
				s_MoveFiring.Insert(body);
				LogEngageShot(body);
			}

			return;
		}

		// Paused or arrived. Nothing is asking him to walk, so the shot
		// does not fight a step.
		HoldEngageShot(body, enemy, aim);
	}

	// Rocket, disposable, or RPG muzzle. A vehicle stays on the launcher.
	// No body is a person-sized point, so that is not a rocket either.
	protected static bool LauncherAgainstInfantry(IEntity body, IEntity enemy)
	{
		if (enemy && !ChimeraCharacter.Cast(enemy))
			return false;

		return LauncherInHand(body);
	}

	protected static bool LauncherInHand(IEntity body)
	{
		BaseWeaponComponent weapon = CurrentWeapon(body);
		if (!weapon)
			return false;

		if (weapon.GetWeaponType() == EWeaponType.WT_ROCKETLAUNCHER)
			return true;

		BaseMuzzleComponent muzzle = weapon.GetCurrentMuzzle();
		if (!muzzle)
			return false;

		if (muzzle.IsDisposable())
			return true;

		return muzzle.GetMuzzleType() == EMuzzleType.MT_RPGMuzzle;
	}

	protected static void HoldEngageShot(IEntity body, IEntity enemy, vector aim)
	{
		s_PerceivedFire.Insert(body);
		vector point = AimPerceived(body, enemy, aim);
		if (point != vector.Zero)
			s_EngageAimPoint.Set(body, point);
		// Raise while the rifle is still coming around. The trigger waits
		// until that aim is actually near the point.
		bool onPoint = EngageAimOnPoint(body, point);
		// A loaded gun is past the empty-magazine bash. Leaving that flag
		// up clears the trigger on every later shot.
		if (!GunIsDry(body))
			EndReloadBash(body);

		// A rocket at a man is not this shot. The rifle comes back in,
		// and the launcher is not raised while that swap plays.
		if (LauncherAgainstInfantry(body, enemy))
		{
			BaseWeaponComponent rifle = PrimaryWeapon(body);
			if (!rifle || LauncherInHand(body) && rifle == CurrentWeapon(body))
				rifle = EngageSwapWeapon(body);

			if (
				rifle &&
				rifle != CurrentWeapon(body) &&
				AvailableAmmo(body, rifle) > 0 &&
				rifle.GetWeaponType() != EWeaponType.WT_ROCKETLAUNCHER
			)
				SwitchToWeapon(body, rifle);

			SetFireWanted(body, false);
			LogEngage(body, "launcher " + FireDiag(body));
			return;
		}

		// The round has to reach him without a friendly in front of the
		// muzzle. The rifle still stays on the point while that lane is shut.
		bool laneClear = point == vector.Zero || !FriendlyInEngageLane(body, enemy, point);
		NoteEngageLane(body, point != vector.Zero && !laneClear);
		bool changing = false;
		CharacterControllerComponent engageController = Controller(body);
		if (engageController && engageController.IsChangingFireMode())
			changing = true;

		if (onPoint && laneClear && !changing)
			ApplyEngageDiscipline(body, point);

		CommandWeapon(body, true, onPoint && laneClear && !changing);
		// CommandWeapon can refuse the trigger and still leave the rifle
		// ready. The aim check is what decides the shot. A dry gun stays
		// off the trigger, or the reload never leaves the magazine well.
		bool shoot =
			onPoint &&
			laneClear &&
			!changing &&
			WeaponIsRaised(body) &&
			WeaponCanAcceptFire(body) &&
			!GunIsDry(body) &&
			!IsEmptyReload(body) &&
			!YieldsWeaponForHeal(body) &&
			!HoldingThrowable(body);
		if (shoot)
		{
			PressFire(body);
			s_MoveFiring.Insert(body);
			LogEngageShot(body);
		}
		else if (!laneClear)
			LogEngage(body, EngageLaneNote());
		else if (onPoint && WeaponIsRaised(body))
			LogEngage(body, "nofire " + FireDiag(body));
		else if (onPoint)
			LogEngage(body, "raise " + FireDiag(body));
		else
			LogEngage(body, EngageAimMiss(body, point));
	}

	// Nothing in hand, or a grenade. Puts a gun that still has rounds there.
	// A rocket is left for the infantry check, which will not fire it at a man.
	protected static void EnsureEngageGun(IEntity body)
	{
		if (!body || YieldsWeaponForHeal(body))
			return;

		BaseWeaponComponent current = CurrentWeapon(body);
		if (current && !IsThrowableType(current.GetWeaponType()))
			return;

		BaseWeaponComponent rifle = PrimaryWeapon(body);
		if (!rifle || rifle.GetWeaponType() == EWeaponType.WT_ROCKETLAUNCHER)
			rifle = EngageSwapWeapon(body);

		if (
			!rifle ||
			rifle == current ||
			rifle.GetWeaponType() == EWeaponType.WT_ROCKETLAUNCHER ||
			AvailableAmmo(body, rifle) <= 0
		)
			return;

		SwitchToWeapon(body, rifle);
	}

	// Entity id plus the prefab. GetName() is empty on these characters.
	protected static string SoldierLabel(IEntity body)
	{
		if (!body)
			return "?";

		string prefab = string.Empty;
		EntityPrefabData data = body.GetPrefabData();
		if (data)
			prefab = string.Format("%1", data.GetPrefabName());

		int cut = -1;
		for (int i = 0; i < prefab.Length(); i++)
		{
			if (prefab[i] == "/")
				cut = i;
		}

		if (cut >= 0 && cut < prefab.Length() - 1)
			prefab = prefab.Substring(cut + 1, prefab.Length() - cut - 1);

		return string.Format("%1 %2", body.GetID(), prefab);
	}

	// One decimal. The threat log is compared as text, so a raw float
	// would look like a new line every frame.
	protected static float ThreatTenth(float value)
	{
		return Math.Round(value * 10) / 10;
	}

	protected static string ThreatVec(vector value)
	{
		return string.Format(
			"%1 %2 %3",
			ThreatTenth(value[0]),
			ThreatTenth(value[1]),
			ThreatTenth(value[2])
		);
	}

	protected static string ThreatWho(IEntity body)
	{
		if (!body)
			return "unknown";

		return SoldierLabel(body);
	}

	// The debug log switch gates this. The same note for the same soldier
	// is printed once, because the danger event stays on the agent.
	protected static void LogThreatNote(map<IEntity, string> seen, IEntity body, string note)
	{
		if (!body || !seen || note == string.Empty || !SCR_BaseGameMode.KK_LogEnabled())
			return;

		if (seen.Contains(body) && seen.Get(body) == note)
			return;

		seen.Set(body, note);
		PrintFormat("KK: Threat %1 %2", SoldierLabel(body), note);
	}

	// Once a second per soldier. The debug log switch has to be on.
	protected static void LogEngage(IEntity body, string reason)
	{
		if (!body || !SCR_BaseGameMode.KK_LogEnabled())
			return;

		float now = WorldTime();
		if (s_EngageLogAt.Contains(body) && now - s_EngageLogAt.Get(body) < 1000)
			return;

		s_EngageLogAt.Set(body, now);
		PrintFormat("KK: Engage %1 %2", SoldierLabel(body), reason);
	}

	// Separate from the other engage lines, so a reload note in the same
	// second does not hide the shot. Still once a second while he holds it.
	protected static void LogEngageShot(IEntity body)
	{
		if (!body || !SCR_BaseGameMode.KK_LogEnabled())
			return;

		float now = WorldTime();
		if (s_EngageShotLogAt.Contains(body) && now - s_EngageShotLogAt.Get(body) < 1000)
			return;

		s_EngageShotLogAt.Set(body, now);
		PrintFormat(
			"KK: Engage %1 shot rounds=%2 %3",
			SoldierLabel(body),
			LoadedRounds(CurrentWeapon(body)),
			FireDiag(body)
		);
	}

	// Once a second, separate from the shot line, for a rack, a muzzle
	// switch, or the trigger being let go.
	protected static void LogFire(IEntity body, string action)
	{
		if (!body || !SCR_BaseGameMode.KK_LogEnabled())
			return;

		float now = WorldTime();
		if (s_FireLogAt.Contains(body) && now - s_FireLogAt.Get(body) < 1000)
			return;

		s_FireLogAt.Set(body, now);
		PrintFormat("KK: Fire %1 %2 %3", SoldierLabel(body), action, FireDiag(body));
	}

	// Muzzle that would fire, the magazine behind it, and the controller
	// gates around a shot. muzzle=-1 means there is no current muzzle.
	protected static string FireDiag(IEntity body)
	{
		CharacterControllerComponent controller = Controller(body);
		BaseWeaponComponent weapon = CurrentWeapon(body);
		BaseMuzzleComponent muzzle;
		int muzzleAmmo = -1;
		int magAmmo = -1;
		int chamber = -1;
		int need = 0;
		int mode = -1;
		int wtype = -1;
		int mtype = -1;
		int idx = -1;
		if (weapon)
		{
			wtype = weapon.GetWeaponType();
			mode = weapon.GetCurrentFireModeType();
			muzzle = weapon.GetCurrentMuzzle();
			array<BaseMuzzleComponent> muzzles = {};
			weapon.GetMuzzlesList(muzzles);
			for (int i = 0; i < muzzles.Count(); i++)
			{
				if (muzzles[i] == muzzle)
					idx = i;
			}
		}

		if (muzzle)
		{
			muzzleAmmo = muzzle.GetAmmoCount();
			mtype = muzzle.GetMuzzleType();
			if (muzzle.IsCurrentBarrelChambered())
				chamber = 1;
			else
				chamber = 0;
			if (NeedsChamber(muzzle))
				need = 1;
			if (!muzzle.IsDisposable())
			{
				BaseMagazineComponent loaded = muzzle.GetMagazine();
				if (loaded)
					magAmmo = loaded.GetAmmoCount();
			}
		}

		int canfire = 0;
		int partial = 0;
		int raised = 0;
		int reloading = 0;
		int changing = 0;
		int sprint = 0;
		int disabled = 0;
		int obstruct = -1;
		if (controller)
		{
			if (controller.GetCanFireWeapon())
				canfire = 1;
			if (controller.IsPartiallyLowered())
				partial = 1;
			if (controller.IsWeaponRaised())
				raised = 1;
			if (controller.IsReloading())
				reloading = 1;
			if (controller.IsChangingItem())
				changing = 1;
			if (controller.IsSprinting())
				sprint = 1;
			if (controller.GetDisableWeaponControls())
				disabled = 1;
			obstruct = controller.GetWeaponObstructedState();
		}

		string rounds = string.Format(
			"muzzle=%1 mag=%2 chamber=%3 need=%4 canfire=%5 partial=%6 mode=%7 raised=%8",
			muzzleAmmo,
			magAmmo,
			chamber,
			need,
			canfire,
			partial,
			mode,
			raised
		);
		string state = string.Format(
			"rel=%1 chg=%2 wtype=%3 mtype=%4 idx=%5 held=%6 obs=%7 sprint=%8",
			reloading,
			changing,
			wtype,
			mtype,
			idx,
			s_TriggerHeld.Contains(body),
			obstruct,
			sprint
		);
		return rounds + " " + state + string.Format(" dis=%1", disabled);
	}

	// Why the raised rifle did not get a trigger. Distances are meters,
	// angles are degrees.
	protected static string EngageAimMiss(IEntity body, vector point)
	{
		CharacterControllerComponent controller = Controller(body);
		string flags = string.Format(
			"raised=%1 reloading=%2 changing=%3 dry=%4 empty=%5 settle=%6",
			WeaponIsRaised(body),
			controller && controller.IsReloading(),
			controller && controller.IsChangingItem(),
			GunIsDry(body),
			IsEmptyReload(body),
			ReloadAimSettling(body)
		);
		flags = flags + " " + FireDiag(body);
		if (!controller || point == vector.Zero)
			return "no-aim " + flags;

		AimingComponent aiming = controller.GetAimingComponent();
		if (!aiming)
			return "no-component " + flags;

		vector dir = aiming.GetAimingDirectionWorld();
		vector want = point - AimLineOrigin(body, point);
		float distance = want.Length();
		float off = -1;
		if (dir.Length() > 0.01 && distance > 0.05)
		{
			dir.Normalize();
			want.Normalize();
			float dot = vector.Dot(dir, want);
			if (dot > 1)
				dot = 1;
			if (dot < -1)
				dot = -1;
			off = Math.Acos(dot) / Math.DEG2RAD;
		}

		return string.Format(
			"aim dist=%1 cone=%2 off=%3 %4",
			distance,
			EngageAimDegrees(distance),
			off,
			flags
		);
	}

	// A new contact after the recent gap stops him for the initial duration.
	// One that comes back inside that gap waits for the interval.
	protected static void NoteEngageContact(IEntity body, float now)
	{
		if (s_EngageEpisode.Contains(body))
			return;

		s_EngageEpisode.Insert(body);

		bool fresh = true;
		if (s_EngageEpisodeEnded.Contains(body))
			fresh = now - s_EngageEpisodeEnded.Get(body) >= EngageRecentMs();

		if (fresh && EngageInitialMs() > 0)
		{
			BeginEngageHold(body, now, EngageInitialMs());
			return;
		}

		if (!s_EngageNextStop.Contains(body) || s_EngageNextStop.Get(body) <= now)
			s_EngageNextStop.Set(body, now + EngageIntervalMs());
	}

	// A man who has not yet been the one he fights. When that man takes the
	// shot and is visible, he stops for the initial duration. The first man
	// is the episode stop, so he is recorded without a second one.
	protected static void NotePriorityTarget(IEntity body, IEntity enemy, float now)
	{
		if (!enemy)
			return;

		set<IEntity> known = EngageKnown(body);
		if (known.Contains(enemy))
		{
			s_EngageAim.Set(body, enemy);
			return;
		}

		bool first = !s_EngageAim.Contains(body);
		s_EngageAim.Set(body, enemy);
		if (first)
		{
			known.Insert(enemy);
			return;
		}

		if (EngageInitialMs() <= 0 || !TargetVisible(body, enemy))
			return;

		known.Insert(enemy);
		BeginEngageHold(body, now, EngageInitialMs());
	}

	protected static set<IEntity> EngageKnown(IEntity body)
	{
		if (s_EngageKnown.Contains(body))
			return s_EngageKnown.Get(body);

		set<IEntity> known = new set<IEntity>();
		s_EngageKnown.Set(body, known);
		return known;
	}

	// The native seen check. Exposure and the detection clock are not enough
	// for a new man to stop the move.
	protected static bool TargetVisible(IEntity body, IEntity enemy)
	{
		PerceptionComponent perception = PerceptionComponent.Cast(
			body.FindComponent(PerceptionComponent)
		);
		SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(
			body.FindComponent(SCR_AICombatComponent)
		);
		if (!perception || !combat || !enemy)
			return false;

		s_EngageScan.Clear();
		perception.GetTargetsList(s_EngageScan, ETargetCategory.ENEMY);
		if (ScanVisible(combat, enemy))
			return true;

		s_EngageScan.Clear();
		perception.GetTargetsList(s_EngageScan, ETargetCategory.DETECTED);
		return ScanVisible(combat, enemy);
	}

	protected static bool ScanVisible(SCR_AICombatComponent combat, IEntity enemy)
	{
		foreach (BaseTarget candidate : s_EngageScan)
		{
			if (!candidate || candidate.GetTargetEntity() != enemy)
				continue;

			return combat.IsTargetVisible(candidate);
		}

		return false;
	}

	protected static void BeginEngageHold(IEntity body, float now, float durationMs)
	{
		s_EngageHoldUntil.Set(body, now + durationMs);
		float interval = EngageIntervalMs();
		if (interval > 0)
			s_EngageNextStop.Set(body, now + durationMs + interval);
		else
			s_EngageNextStop.Remove(body);
	}

	protected static bool EngageHoldActive(IEntity body, float now)
	{
		return s_EngageHoldUntil.Contains(body) && now < s_EngageHoldUntil.Get(body);
	}

	protected static bool EngagePeriodicDue(IEntity body, float now)
	{
		if (EngageIntervalMs() <= 0 || EngageRepeatMs() <= 0)
			return false;

		if (!s_EngageNextStop.Contains(body))
			return false;

		return now >= s_EngageNextStop.Get(body);
	}

	// The plant, or a shot taken while he is already stopped. A step
	// between plants owns the head. Leaving it on the man for the whole
	// contact is what the advance order turns back the other way.
	static bool EngageOwnsTurn(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !AttackEngageEnabled() || !IsAttackSearch(body))
			return false;

		return IsEngageStopped(body) || s_PerceivedFire.Contains(body);
	}

	// The engage still has this man. Attack healing stays off for that stretch.
	protected static bool AttackContactOpen(IEntity body)
	{
		return body && s_EngageEpisode.Contains(body);
	}

	protected static void EndEngageEpisode(IEntity body, bool remember)
	{
		if (!body)
			return;

		bool had = s_EngageEpisode.Contains(body);
		s_EngageEpisode.RemoveItem(body);
		s_LaneBlockedAt.Remove(body);
		// After the episode is gone, a man who still needs a bandage
		// counts as healing, and this stop must not give him his speed back.
		SetEngageStop(body, false);
		if (remember && had)
			s_EngageEpisodeEnded.Set(body, WorldTime());

		if (remember)
			return;

		s_EngageEpisodeEnded.Remove(body);
		s_EngageHoldUntil.Remove(body);
		s_EngageNextStop.Remove(body);
		s_EngageKnown.Remove(body);
		s_EngageAim.Remove(body);
	}

	protected static float EngageInitialMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 2000;

		return mode.KK_GetAttackEngageInitial() * 1000;
	}

	protected static float EngageIntervalMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 6000;

		return mode.KK_GetAttackEngageInterval() * 1000;
	}

	protected static float EngageRepeatMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 2000;

		return mode.KK_GetAttackEngageRepeat() * 1000;
	}

	// How long a heard shot stays a contact. The setting is seconds.
	protected static float HeardShotMs()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return 8000;

		return mode.KK_GetHeardShot() * 1000;
	}

	// Long enough that a flicker of the same man is not a new first sighting.
	protected static float EngageRecentMs()
	{
		float interval = EngageIntervalMs();
		if (interval > 0)
			return interval;

		float initial = EngageInitialMs();
		if (initial > 0)
			return initial;

		return 2000;
	}

	static bool IsEngageStopped(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_EngageStop.Contains(body);
	}

	// Max speed only. The attack step stays queued, and a cover pin is left
	// as it was. Speed comes back when this stop ends.
	protected static void SetEngageStop(IEntity body, bool stop)
	{
		if (!body)
			return;

		if (stop)
			s_EngageStop.Insert(body);
		else
			s_EngageStop.RemoveItem(body);

		CharacterControllerComponent controller = Controller(body);
		if (!controller)
			return;

		// A cover pin stays put. A bandage does too: ending the plant
		// would otherwise put the walk back while he is still bleeding.
		if (stop || IsPinned(body) || s_HealFootLock.Contains(body) || HealingNow(body))
			controller.OverrideMaxSpeed(0);
		else
			controller.OverrideMaxSpeed(1);
	}

	// The cover walk at the point. Engage had locked his feet on the threat.
	static void ReleaseEngageFeet(IEntity soldier)
	{
		SetEngageStop(CharacterBody(soldier), false);
	}

	static void SetCoverShift(IEntity soldier, bool shifting)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (shifting)
			s_CoverShift.Insert(body);
		else
			s_CoverShift.RemoveItem(body);
	}

	static bool IsCoverShift(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_CoverShift.Contains(body);
	}

	// True once the shot at this spot has failed for that long. A clear
	// lane, or leaving the spot, drops the clock.
	static bool EngageLaneBlocked(IEntity soldier, float forMs)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_LaneBlockedAt.Contains(body))
			return false;

		if (forMs <= 0)
			return true;

		return WorldTime() - s_LaneBlockedAt.Get(body) >= forMs;
	}

	static void ClearEngageLaneBlock(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (body)
			s_LaneBlockedAt.Remove(body);
	}

	// A blocked lane while he is walking to the next cover does not start
	// the clock. An open lane ends that walk, so the next pass can plant
	// and shoot.
	protected static void NoteEngageLane(IEntity body, bool blocked)
	{
		if (!body)
			return;

		if (s_CoverShift.Contains(body))
		{
			if (!blocked)
				s_CoverShift.RemoveItem(body);

			return;
		}

		// A bound is already a move. The clock is the time he spends
		// stopped on a spot the round cannot leave.
		if (AttackTraveling(body))
		{
			s_LaneBlockedAt.Remove(body);
			return;
		}

		// The engage plant is why he is standing. That time does not
		// count toward leaving the cover.
		if (IsEngageStopped(body))
		{
			if (s_LaneBlockedAt.Contains(body))
				s_LaneBlockedAt.Set(body, WorldTime());

			return;
		}

		if (!blocked)
		{
			s_LaneBlockedAt.Remove(body);
			return;
		}

		if (!s_LaneBlockedAt.Contains(body))
			s_LaneBlockedAt.Set(body, WorldTime());
	}

	protected static bool AttackEngageEnabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetAttackEngage();
	}

	// Medic and patient leave the attack push. Off keeps the whole squad on it.
	static bool AttackMedicEnabled()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetAttackMedic();
	}

	// An empty gun on this order. The building reload does not run here,
	// and the attack behavior does not hand the magazine change off.
	protected static void StartEngageReload(IEntity body)
	{
		if (!body || YieldsWeaponForHeal(body) || HoldingThrowable(body))
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

		if (controller.IsReloading())
		{
			if (GunIsDry(body))
				s_EmptyReload.Insert(body);

			return;
		}

		if (IsEmptyReload(body))
			return;

		// A reload while the rifle is up is refused, and the next raise
		// cancels a magazine change that did start. The engage raise stays
		// off until this gun can shoot again.
		if (controller.IsWeaponRaised() || controller.IsWeaponADS())
		{
			controller.SetWeaponADS(false);
			controller.SetWeaponRaised(false);
			s_MoveWeaponUp.RemoveItem(body);
			s_LowerSentAt.Set(body, WorldTime());
		}

		BaseWeaponComponent inHand = CurrentWeapon(body);
		if (!inHand || IsThrowableType(inHand.GetWeaponType()))
		{
			EnsureEngageGun(body);
			return;
		}

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
			// The magazine search can miss one the weapon will still take.
			if (GunIsDry(body) && controller.ReloadWeapon())
			{
				s_ReloadAt.Set(body, now);
				s_DidReload.Insert(body);
				s_EmptyReload.Insert(body);
				return;
			}

			BaseWeaponComponent other = EngageSwapWeapon(body);
			// The rocket is not a substitute magazine. Swapping to it and
			// back leaves the empty rifle in hand and the reload never starts.
			if (other && other.GetWeaponType() != EWeaponType.WT_ROCKETLAUNCHER)
				SwitchToWeapon(body, other);
			else
				s_ReloadAt.Set(body, now);

			return;
		}

		BaseMagazineComponent loaded = LoadedMagazine(body);
		// An empty magazine still fills the well. It has to come out, or
		// the spare never seats and IsReloading stays false.
		bool forceDetach = loaded != null;
		// Sampled before the call. A magazine that is already counted when
		// ReloadWeaponWith returns would skip the guard, and the next tick
		// would fire while the animation is still playing.
		bool dry = GunIsDry(body);
		SetFireWanted(body, false);
		s_MoveFiring.RemoveItem(body);
		if (!controller.ReloadWeaponWith(spare, forceDetach))
		{
			s_ReloadAt.Set(body, now);
			return;
		}

		s_ReloadAt.Set(body, now);
		s_DidReload.Insert(body);
		if (dry)
			s_EmptyReload.Insert(body);
	}

	// Current gun cannot be fed. Another gun that still has rounds, primary
	// first, then the fullest of the rest. A throwable is not a swap.
	protected static BaseWeaponComponent EngageSwapWeapon(IEntity body)
	{
		BaseWeaponComponent current = CurrentWeapon(body);
		BaseWeaponComponent primary = PrimarySlotWeapon(body);
		if (!primary)
			primary = PrimaryWeapon(body);

		if (primary && primary != current && AvailableAmmo(body, primary) > 0)
			return primary;

		BaseWeaponManagerComponent manager = WeaponManager(body);
		if (!manager)
			return null;

		array<WeaponSlotComponent> slots = {};
		manager.GetWeaponsSlots(slots);

		BaseWeaponComponent best;
		int bestAmmo = 0;
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot)
				continue;

			BaseWeaponComponent weapon = WeaponInSlot(slot);
			if (!weapon || weapon == current || IsThrowableType(weapon.GetWeaponType()))
				continue;

			int ammo = AvailableAmmo(body, weapon);
			if (ammo <= bestAmmo)
				continue;

			best = weapon;
			bestAmmo = ammo;
		}

		return best;
	}

	// True while the empty-gun swap is still playing, so the attack does
	// not pull the trigger and restart it.
	static bool EngageSwapping(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !CannotShoot(body))
			return false;

		CharacterControllerComponent controller = Controller(body);
		return controller && controller.IsChangingItem();
	}

	protected static void ReleasePerceivedEnemy(IEntity body, bool cancelLook = true)
	{
		if (!body || !s_PerceivedFire.Contains(body))
			return;

		s_PerceivedFire.RemoveItem(body);
		s_EngageAimPoint.Remove(body);
		s_EngageAimAt.Remove(body);
		s_MoveFiring.RemoveItem(body);
		SetFireWanted(body, false);
		DropSnapTurn(body);

		// A sprint that has earned the lane writes that look itself.
		// Cancelling here would wipe it on the same pass.
		if (!cancelLook)
			return;

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (utility && utility.m_LookAction)
			utility.m_LookAction.Cancel();
	}

	static bool PerceivedFireActive(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_PerceivedFire.Contains(body);
	}

	// The engage episode is still open. A medic does not walk off while it is.
	static bool EngageContactOpen(IEntity soldier)
	{
		return AttackContactOpen(CharacterBody(soldier));
	}

	// Armed attack engage. The route contact look is not used, including
	// between stops, or it takes the head back as soon as the stop lets go.
	static bool AttackEngageOwnsLook(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !AttackEngageEnabled() || !IsAttackSearch(body))
			return false;

		// Treatment owns his hands. A plant look would take the bandage off.
		// A gesture already playing stays through a new contact.
		if (s_MedicDetail.Contains(body) || HealingNow(body))
			return false;

		if (!HasGunAmmo(body))
			return false;

		// A bound sprint keeps the head only while engage has no man.
		// A live contact stays on that man, sprint included.
		if (IsBoundSprint(body) && !EngageOwnsTurn(body))
			return false;

		return true;
	}

	// The rifle stays up for the whole attack engage. A bound sprint, an
	// empty gun, a reload, and a grenade still lower it. Nothing else does:
	// the move behavior was putting it down between shots and the next
	// raise then thought it was already on the way up.
	static bool AttackEngageOwnsWeapon(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !AttackEngageOwnsLook(body))
			return false;

		if (YieldsWeaponForHeal(body) || IsDown(body))
			return false;

		if (CannotShoot(body) || IsQuietReload(body) || HoldingThrowable(body))
			return false;

		return true;
	}

	// Raises and reports that engage owns the rifle, so the caller does
	// not run a lower.
	static bool EngageHoldWeapon(IEntity soldier)
	{
		if (!AttackEngageOwnsWeapon(soldier))
			return false;

		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		RequestWeaponStance(body, true);
		return true;
	}

	// The point the engage stop is holding. Empty while he is between stops.
	static bool GetEngageAim(IEntity soldier, out vector aim)
	{
		aim = vector.Zero;
		IEntity body = CharacterBody(soldier);
		if (!body || !s_PerceivedFire.Contains(body) || !s_EngageAimPoint.Contains(body))
			return false;

		aim = s_EngageAimPoint.Get(body);
		return aim != vector.Zero;
	}

	// One empty perception pass is not a lost man. Cancelling on that pass
	// and snapping again on the next one is the head thrash.
	protected static const float ENGAGE_AIM_GRACE_MS = 400;

	protected static void RememberEngageAim(IEntity body, vector point)
	{
		if (!body || point == vector.Zero)
			return;

		s_EngageAimPoint.Set(body, point);
		s_EngageAimAt.Set(body, WorldTime());
	}

	// The reload cancels the look for several seconds. The grace is only
	// long enough for one empty perception pass, so the clock is held
	// while that reload is the reason the look is down.
	protected static void HoldEngageAimClock(IEntity body)
	{
		if (!body || !s_EngageAimPoint.Contains(body))
			return;

		if (s_EngageAimPoint.Get(body) == vector.Zero)
			return;

		s_EngageAimAt.Set(body, WorldTime());
	}

	protected static bool RecentEngageAim(IEntity body, out vector aim)
	{
		aim = vector.Zero;
		if (!body || !s_EngageAimPoint.Contains(body) || !s_EngageAimAt.Contains(body))
			return false;

		if (WorldTime() - s_EngageAimAt.Get(body) > ENGAGE_AIM_GRACE_MS)
			return false;

		aim = s_EngageAimPoint.Get(body);
		return aim != vector.Zero;
	}

	// The man the normal attack is already on, then a sure contact, then
	// the man who just fired, then a suspected one. Sure means seen,
	// exposed, or just detected. A bush can keep the shooter unknown
	// because no aim point is exposed, so the shot he fired is what makes
	// him a target. A quiet unknown track is not. A suspected man does
	// not pull the rifle off someone already in the open.
	protected static bool PerceivedEnemy(IEntity body, out IEntity enemy, out vector aim)
	{
		enemy = null;
		aim = vector.Zero;

		PerceptionComponent perception = PerceptionComponent.Cast(
			body.FindComponent(PerceptionComponent)
		);
		SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(
			body.FindComponent(SCR_AICombatComponent)
		);
		if (!perception || !combat)
			return false;

		float window = Math.Max(0.5, perception.GetUpdateInterval()) + 0.02;
		BaseTarget selected = combat.GetCurrentTarget();
		if (selected && SelectedEngageTarget(selected))
		{
			if (TakeContact(body, selected, combat, window, false, false, enemy, aim))
				return true;
		}

		if (ClosestContact(body, perception, combat, ETargetCategory.ENEMY, window, true, true, enemy, aim))
			return true;

		if (ClosestContact(body, perception, combat, ETargetCategory.DETECTED, window, true, true, enemy, aim))
			return true;

		if (AttackEngageHeardShot() && HeardShooter(body, enemy, aim))
			return true;

		if (AttackEngageSuspected())
		{
			if (ClosestContact(body, perception, combat, ETargetCategory.ENEMY, window, false, false, enemy, aim))
				return true;

			if (ClosestContact(body, perception, combat, ETargetCategory.DETECTED, window, false, false, enemy, aim))
				return true;
		}

		if (!AttackEngageThreatening())
			return false;

		return ClosestThreatening(body, perception, combat, window, enemy, aim);
	}

	// A known man is always a shot. An unknown one is only when he is
	// shooting, and only while that criterion is on.
	protected static bool SelectedEngageTarget(BaseTarget selected)
	{
		if (!selected)
			return false;

		if (selected.GetTargetCategory() != ETargetCategory.UNKNOWN)
			return true;

		return AttackEngageThreatening() && ContactThreatening(selected);
	}

	protected static bool AttackEngageHeardShot()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetAttackEngageHeardShot();
	}

	protected static bool AttackEngageSuspected()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetAttackEngageSuspected();
	}

	protected static bool AttackEngageThreatening()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetAttackEngageThreatening();
	}

	protected static bool AttackEngageReturnHit()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetAttackEngageReturnHit();
	}

	protected static bool AttackEngageHitMate()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetAttackEngageHitMate();
	}

	protected static bool AttackEngageShotImpact()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetAttackEngageShotImpact();
	}

	protected static bool AttackEngageHeardPath()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return true;

		return mode.KK_GetAttackEngageHeardPath();
	}

	// Heard a shot. The vanilla reaction drops it when his side has not
	// been seen yet, which is a man firing from a bush. The real faction
	// is enough. The muzzle place is kept when the body is not on the event.
	static void NoteAttackGunshot(IEntity listener, IEntity shooter, vector shotPos, vector shotDir)
	{
		IEntity body = CharacterBody(listener);
		if (!body || !IsAttackSearch(body))
			return;

		IEntity source = shooter;
		shooter = ShotCharacter(shooter);
		if (shooter == body || (shooter && SharesGroup(body, shooter)))
			return;

		if (shooter && KnownFriendly(body, shooter))
			return;

		// ShotCharacter skips a corpse. The danger event stays after the
		// kill, and stamping it as an unknown muzzle keeps the plant up.
		if (!shooter)
		{
			IEntity actor = ShotActor(source);
			if (actor && !IsTargetable(actor))
			{
				if (s_HeardShooter.Contains(body) && s_HeardShooter.Get(body) == actor)
					ForgetHeardShot(body);

				return;
			}
		}

		vector place = shotPos;
		if (place == vector.Zero && shooter)
			place = shooter.GetOrigin();

		if (place == vector.Zero)
			return;

		if (vector.Distance(body.GetOrigin(), place) > ATTACK_CONTACT_RANGE)
			return;

		if (shooter)
			s_HeardShooter.Set(body, shooter);
		else
			s_HeardShooter.Remove(body);

		s_HeardPlace.Set(body, place);
		vector dir = shotDir;
		if (dir.Length() > 0.01)
			dir.Normalize();
		else
			dir = vector.Zero;

		s_HeardDir.Set(body, dir);
		s_HeardWhen.Set(body, WorldTime());
		LogThreatNote(
			s_ThreatLineLog,
			body,
			string.Format(
				"line shooter=%1 muzzle=%2 dir=%3",
				ThreatWho(shooter),
				ThreatVec(place),
				ThreatVec(dir)
			)
		);
	}

	// A round struck within the impact radius. The event often has no
	// shooter, so the man he just heard firing is the one who owns it.
	static void NoteNearbyImpact(IEntity listener, AIDangerEvent danger)
	{
		IEntity body = CharacterBody(listener);
		if (!body || !IsAttackSearch(body) || !danger)
			return;

		vector place = danger.GetPosition();
		if (place == vector.Zero)
			return;

		IEntity named = ShotCharacter(danger.GetObject());
		if (!named)
			named = ShotCharacter(danger.GetVictim());

		string source = "heard";
		IEntity shooter = named;
		if (named)
			source = "event";

		if (shooter && (SameCharacter(shooter, body) || SharesGroup(body, shooter) || KnownFriendly(body, shooter)))
		{
			LogThreatNote(s_ThreatImpactLog, body, "impact skip self-or-friendly");
			return;
		}

		if (!shooter && s_HeardShooter.Contains(body) && s_HeardWhen.Contains(body))
		{
			if (WorldTime() - s_HeardWhen.Get(body) <= HeardShotMs())
				shooter = s_HeardShooter.Get(body);
		}

		if (!shooter || SameCharacter(shooter, body))
		{
			LogThreatNote(s_ThreatImpactLog, body, "impact no-shooter");
			return;
		}

		if (SharesGroup(body, shooter) || KnownFriendly(body, shooter))
		{
			LogThreatNote(s_ThreatImpactLog, body, "impact skip friendly");
			return;
		}

		bool near = vector.DistanceSq(body.GetOrigin(), place) <= IMPACT_RADIUS * IMPACT_RADIUS;
		string verdict = "drop";
		if (near)
			verdict = "record";

		LogThreatNote(
			s_ThreatImpactLog,
			body,
			string.Format(
				"impact %1 feet=%2 shooter=%3 %4",
				verdict,
				ThreatTenth(vector.Distance(body.GetOrigin(), place)),
				ThreatWho(shooter),
				source
			)
		);

		if (!near)
			return;

		NoteBulletThreat(body, shooter, THREAT_IMPACT);
	}

	// The hidden man is the one engage is deciding on, and his own shot
	// toward that man is blocked. Five projectile rays leave the muzzle
	// on a plane facing this soldier. One that arrives means the path is
	// open. The same stored shot is not traced again.
	protected static bool HeardPathOpen(IEntity body, IEntity shooter)
	{
		if (!body || !shooter)
			return false;

		if (!s_HeardShooter.Contains(body) || !s_HeardWhen.Contains(body) || !s_HeardPlace.Contains(body) || !s_HeardDir.Contains(body))
			return false;

		if (WorldTime() - s_HeardWhen.Get(body) > HeardShotMs())
			return false;

		if (!SameCharacter(s_HeardShooter.Get(body), shooter))
			return false;

		vector shotPos = s_HeardPlace.Get(body);
		vector shotDir = s_HeardDir.Get(body);
		string key = string.Format(
			"%1|%2|%3",
			shooter.GetID(),
			ThreatVec(shotPos),
			ThreatVec(shotDir)
		);
		if (s_PathKey.Contains(body) && s_PathKey.Get(body) == key && s_PathOpen.Contains(body))
			return s_PathOpen.Get(body);

		bool open = PathReaches(body, shooter, shotPos);
		s_PathKey.Set(body, key);
		s_PathOpen.Set(body, open);
		string pathNote = "blocked";
		if (open)
			pathNote = "open";

		LogThreatNote(
			s_ThreatPathLog,
			body,
			string.Format("path %1 shooter=%2", pathNote, ThreatWho(shooter))
		);
		return open;
	}

	protected static bool PathReaches(IEntity body, IEntity shooter, vector shotPos)
	{
		vector toSoldier = body.GetOrigin() - shotPos;
		float distance = toSoldier.Length();
		if (distance < 0.6)
			return false;

		vector dir = toSoldier * (1 / distance);
		vector worldUp = "0 1 0";
		if (Math.AbsFloat(vector.Dot(dir, worldUp)) > 0.99)
			worldUp = "1 0 0";

		vector right = PathCross(dir, worldUp);
		right.Normalize();
		vector up = PathCross(right, dir);
		up.Normalize();
		float step = 0.3;
		float reach = distance - step;
		if (reach < 0.3)
			return false;

		BaseWorld world = null;
		if (GetGame())
			world = GetGame().GetWorld();

		if (!world)
			world = body.GetWorld();

		if (!world)
			return false;

		s_PathExclude = shooter;
		s_PathTarget = body;
		bool open =
			PathRay(world, shotPos + (dir * step), dir, reach) ||
			PathRay(world, shotPos + (dir * step) + (right * PATH_RAY_SPREAD), dir, reach) ||
			PathRay(world, shotPos + (dir * step) - (right * PATH_RAY_SPREAD), dir, reach) ||
			PathRay(world, shotPos + (dir * step) + (up * PATH_RAY_SPREAD), dir, reach) ||
			PathRay(world, shotPos + (dir * step) - (up * PATH_RAY_SPREAD), dir, reach);
		s_PathExclude = null;
		s_PathTarget = null;
		return open;
	}

	protected static vector PathCross(vector a, vector b)
	{
		return Vector(
			a[1] * b[2] - a[2] * b[1],
			a[2] * b[0] - a[0] * b[2],
			a[0] * b[1] - a[1] * b[0]
		);
	}

	protected static bool PathRay(BaseWorld world, vector start, vector dir, float reach)
	{
		TraceParam trace = new TraceParam();
		trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		trace.LayerMask = EPhysicsLayerPresets.Projectile;
		trace.Exclude = s_PathExclude;
		trace.Start = start;
		trace.End = start + (dir * reach);
		float fraction = world.TraceMove(trace, FilterPathBlock);
		if (fraction >= 1)
			return true;

		IEntity hit = trace.TraceEnt;
		int depth;
		while (hit && depth < 8)
		{
			if (SameCharacter(hit, s_PathTarget))
				return true;

			hit = hit.GetParent();
			depth++;
		}

		return false;
	}

	protected static bool FilterPathBlock(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		if (!entity)
			return true;

		IEntity current = entity;
		int depth;
		while (current && depth < 8)
		{
			if (current == s_PathExclude)
				return false;

			current = current.GetParent();
			depth++;
		}

		return true;
	}

	protected static bool HeardShooter(IEntity body, out IEntity enemy, out vector aim)
	{
		enemy = null;
		aim = vector.Zero;
		HarvestGunshots(body);

		if (!body || !s_HeardWhen.Contains(body))
			return false;

		if (WorldTime() - s_HeardWhen.Get(body) > HeardShotMs())
		{
			ForgetHeardShot(body);
			return false;
		}

		IEntity shooter = null;
		if (s_HeardShooter.Contains(body))
			shooter = s_HeardShooter.Get(body);

		if (shooter && !IsTargetable(shooter))
		{
			// He was the man who fired. The place is his, and leaving it
			// keeps the stop after he is dead.
			ForgetHeardShot(body);
			return false;
		}

		if (shooter && (SharesGroup(body, shooter) || KnownFriendly(body, shooter)))
			shooter = null;

		vector place = vector.Zero;
		if (s_HeardPlace.Contains(body))
			place = s_HeardPlace.Get(body);

		if (shooter)
			place = shooter.GetOrigin();

		if (place == vector.Zero)
			return false;

		if (vector.Distance(body.GetOrigin(), place) > ATTACK_CONTACT_RANGE)
			return false;

		if (shooter)
			aim = ShotAimPoint(shooter);
		else
			aim = place;

		if (aim == vector.Zero)
			return false;

		enemy = shooter;
		return true;
	}

	// Events still on the agent. The reaction can return before it tells
	// anyone, and the shot is still sitting here.
	protected static void HarvestGunshots(IEntity body)
	{
		if (!body)
			return;

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility)
			return;

		AIAgent agent = AIAgent.Cast(utility.GetOwner());
		if (!agent)
			return;

		int count = agent.GetDangerEventsCount();
		for (int i = 0; i < count; i++)
		{
			int repeats;
			AIDangerEvent danger = agent.GetDangerEvent(i, repeats);
			AIDangerEventWeaponFire shot = AIDangerEventWeaponFire.Cast(danger);
			if (!shot)
				continue;

			IEntity shooter = shot.GetInstigatorEntity();
			if (!shooter)
				shooter = shot.GetObject();

			NoteAttackGunshot(body, shooter, shot.GetPosition(), shot.GetDirection());
		}

		for (int hit = 0; hit < count; hit++)
		{
			int hitRepeats;
			AIDangerEvent hitEvent = agent.GetDangerEvent(hit, hitRepeats);
			if (!hitEvent || hitEvent.GetDangerType() != EAIDangerEventType.Danger_ProjectileHit)
				continue;

			NoteNearbyImpact(body, hitEvent);
		}
	}

	protected static void ForgetHeardShot(IEntity body)
	{
		if (!body)
			return;

		s_HeardShooter.Remove(body);
		s_HeardPlace.Remove(body);
		s_HeardDir.Remove(body);
		s_HeardWhen.Remove(body);
		s_PathKey.Remove(body);
		s_PathOpen.Remove(body);
	}

	// A faction we already know is not an enemy. An unresolved side is
	// still the man who just fired.
	protected static bool KnownFriendly(IEntity self, IEntity other)
	{
		if (!self || !other)
			return false;

		if (!SCR_Faction.Cast(FactionOf(other)))
			return false;

		return !IsHostile(self, other);
	}

	protected static IEntity ShotCharacter(IEntity shooter)
	{
		IEntity current = shooter;
		int depth;
		while (current && depth < 6)
		{
			if (IsTargetable(current))
				return current;

			current = current.GetParent();
			depth++;
		}

		return null;
	}

	// The character on the event, dead included. A weapon or an agent
	// is walked up to the body that owns it.
	protected static IEntity ShotActor(IEntity shooter)
	{
		IEntity current = shooter;
		int depth;
		while (current && depth < 6)
		{
			IEntity body = CharacterBody(current);
			if (body && Controller(body))
				return body;

			current = current.GetParent();
			depth++;
		}

		return null;
	}

	// Still unknown, but he is shooting or just was. A quiet unknown
	// shape in a bush is not this.
	protected static bool ContactThreatening(BaseTarget candidate)
	{
		if (!candidate)
			return false;

		if (candidate.IsEndangering())
			return true;

		float since = candidate.GetTimeSinceEndangered();
		return since > 0 && since < THREAT_CONTACT_S;
	}

	protected static bool ClosestThreatening(
		IEntity body,
		PerceptionComponent perception,
		SCR_AICombatComponent combat,
		float window,
		out IEntity enemy,
		out vector aim)
	{
		enemy = null;
		aim = vector.Zero;
		s_AttackSeen.Clear();
		perception.GetTargetsList(s_AttackSeen, ETargetCategory.UNKNOWN);

		BaseTarget best = null;
		float bestDist = 0;

		foreach (BaseTarget candidate : s_AttackSeen)
		{
			if (!ContactThreatening(candidate))
				continue;

			IEntity ignored;
			vector ignoredAim;
			if (!TakeContact(body, candidate, combat, window, false, false, ignored, ignoredAim))
				continue;

			float distance = ContactDistance(body, candidate);
			if (best && distance >= bestDist)
				continue;

			best = candidate;
			bestDist = distance;
		}

		if (!best)
			return false;

		return TakeContact(body, best, combat, window, false, false, enemy, aim);
	}

	protected static bool TakeContact(
		IEntity body,
		BaseTarget candidate,
		SCR_AICombatComponent combat,
		float window,
		bool sureOnly,
		bool knownFactionOnly,
		out IEntity enemy,
		out vector aim)
	{
		enemy = null;
		aim = vector.Zero;
		if (!candidate || !ContactCurrent(combat, candidate, window, sureOnly))
			return false;

		if (!EngageFactionOk(body, candidate, knownFactionOnly))
			return false;

		// A sure man keeps the old reach. A suspected one stays inside
		// the attack's own contact distance.
		if (!sureOnly && !ContactCurrent(combat, candidate, window, true))
		{
			float distance = ContactDistance(body, candidate);
			if (distance < 0 || distance > ATTACK_CONTACT_RANGE)
				return false;
		}

		aim = EngageAim(candidate);
		if (aim == vector.Zero)
			return false;

		enemy = candidate.GetTargetEntity();
		if (enemy == body || (enemy && !IsTargetable(enemy)))
			enemy = null;

		return true;
	}

	// Puts the look and the rifle on that man, or on the last place a
	// suspected contact was put. The lane look is a lower priority and
	// must not run while this hold is up, or the rifle leaves him. The
	// body heading is only written once he has stopped. Writing it on a
	// step overrides the move and leaves him planted. Returns the point
	// the rifle was told to use.
	protected static vector AimPerceived(IEntity body, IEntity enemy, vector point)
	{
		if (point == vector.Zero && enemy)
			point = ShotAimPoint(enemy);

		if (point == vector.Zero)
			return vector.Zero;

		// Skill scatter is always on for a man he can see. Nothing exposed
		// is only a place, so the shot is somewhere in that area.
		point = point + HeldSnapAimError(body, point, enemy, !EngageSees(body, enemy));

		SCR_AIUtilityComponent utility = UtilityOf(body);
		StopSnapTurn(utility);
		if (utility && utility.m_LookAction)
		{
			s_bPathLookWrite = true;
			utility.m_LookAction.KK_Snap(point, PATH_LOOK_PRIORITY, SHOT_LOOK_DURATION);
			s_bPathLookWrite = false;
		}

		CharacterControllerComponent controller = Controller(body);
		if (controller)
		{
			// The weapon is parented to the facing he has now. The heading
			// just requested would aim the barrel off to the side.
			float bodyYaw = body.GetYawPitchRoll()[0];
			WriteAimAt(controller.GetAimingComponent(), AimLineOrigin(body, point), point, bodyYaw);
		}

		// A step is still carrying him. Writing the heading here yaws him
		// onto the man, the move yaws him back, and that lasts until the
		// contact ends.
		if (AttackTraveling(body) || IsCoverShift(body) || !LifeStanding(controller))
			return point;

		vector flat = point - body.GetOrigin();
		flat[1] = 0;
		if (flat.Length() < 0.05)
			return point;

		flat.Normalize();
		float heading = flat.ToYaw();
		SnapFacing(body, controller, heading);
		// Snap facing no longer forces the controller. This plant still does.
		controller.SetHeadingAngle(heading * Math.DEG2RAD, true);
		return point;
	}

	// The muzzle sits ahead of the shoulder. A man inside that length is
	// behind the muzzle, so a line from the muzzle to him points back into
	// our own body. The rifle is on him, and that line never matches it.
	protected static vector AimLineOrigin(IEntity body, vector point)
	{
		if (!body)
			return point;

		vector shoulder = body.GetOrigin() + Vector(0, 1.45, 0);

		vector muzzle = MuzzlePosition(body, 0);
		vector fromShoulder = point - shoulder;
		vector fromMuzzle = point - muzzle;
		fromShoulder[1] = 0;
		fromMuzzle[1] = 0;
		if (fromShoulder.Length() < 0.2 || fromMuzzle.Length() < 0.2)
			return shoulder;

		fromShoulder.Normalize();
		fromMuzzle.Normalize();
		if (vector.Dot(fromMuzzle, fromShoulder) < 0.5)
			return shoulder;

		return muzzle;
	}

	// True when the rifle's own direction is near the point engage wrote.
	// Another aim can still own the barrel for a moment after the look moves.
	protected static bool EngageAimOnPoint(IEntity body, vector point)
	{
		if (!body || point == vector.Zero)
			return false;

		CharacterControllerComponent controller = Controller(body);
		if (!controller)
			return false;

		AimingComponent aiming = controller.GetAimingComponent();
		if (!aiming)
			return false;

		if (ReloadAimSettling(body) && FacingAimPoint(body, point))
			return true;

		vector dir = aiming.GetAimingDirectionWorld();
		if (dir.Length() < 0.01)
			return false;

		dir.Normalize();
		vector want = point - AimLineOrigin(body, point);
		float distance = want.Length();
		if (distance < 0.05)
			return true;

		want.Normalize();
		float limit = Math.Cos(EngageAimDegrees(distance) * Math.DEG2RAD);
		return vector.Dot(dir, want) >= limit;
	}

	// Wide at arm's length, tight at the far end. Past the far distance
	// the cone stays at the tight value.
	protected static float EngageAimDegrees(float distance)
	{
		float span = ENGAGE_AIM_FAR_M - ENGAGE_AIM_NEAR_M;
		float along = 1;
		if (span > 0)
			along = (distance - ENGAGE_AIM_NEAR_M) / span;

		if (along < 0)
			along = 0;
		if (along > 1)
			along = 1;

		return Math.Lerp(ENGAGE_AIM_NEAR_DEG, ENGAGE_AIM_FAR_DEG, along);
	}

	// An exposed aim point is a visual. A heard shot, or a man the bush
	// still hides, is not. No body at all is the place the shot came from.
	protected static bool EngageSees(IEntity body, IEntity enemy)
	{
		if (!body || !enemy)
			return false;

		PerceptionComponent perception = PerceptionComponent.Cast(
			body.FindComponent(PerceptionComponent)
		);
		if (!perception)
			return false;

		BaseTarget target = perception.FindTargetPerceptionObject(enemy);
		if (!target)
			return false;

		return target.GetExposure() > 0;
	}

	// Above this he is still on a step. The body is not turned onto the
	// target until that step has let him stop.
	protected static bool AttackTraveling(IEntity body)
	{
		Physics physics = body.GetPhysics();
		if (!physics)
			return false;

		vector velocity = physics.GetVelocity();
		velocity[1] = 0;
		return velocity.Length() > 1.25;
	}

	// The advance order is still asking him to walk. Idle is a pause or
	// the arrival, and that is the only time a shot between plants is
	// allowed to turn him.
	protected static bool StepWantsMove(IEntity body)
	{
		if (!body)
			return false;

		AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(
			body.FindComponent(AICharacterMovementComponent)
		);
		if (!movement)
			return false;

		EMovementType wanted = movement.GetMovementTypeWanted();
		return wanted == EMovementType.WALK ||
			wanted == EMovementType.RUN ||
			wanted == EMovementType.SPRINT;
	}

	protected static bool ClosestContact(
		IEntity body,
		PerceptionComponent perception,
		SCR_AICombatComponent combat,
		ETargetCategory category,
		float window,
		bool sureOnly,
		bool knownFactionOnly,
		out IEntity enemy,
		out vector aim)
	{
		enemy = null;
		aim = vector.Zero;
		s_AttackSeen.Clear();
		perception.GetTargetsList(s_AttackSeen, category);

		BaseTarget best = null;
		float bestDist = 0;

		foreach (BaseTarget candidate : s_AttackSeen)
		{
			IEntity ignored;
			vector ignoredAim;
			if (!TakeContact(body, candidate, combat, window, sureOnly, knownFactionOnly, ignored, ignoredAim))
				continue;

			float distance = ContactDistance(body, candidate);
			if (best && distance >= bestDist)
				continue;

			best = candidate;
			bestDist = distance;
		}

		if (!best)
			return false;

		return TakeContact(body, best, combat, window, sureOnly, knownFactionOnly, enemy, aim);
	}

	// Seen, an exposed aim point, or a detection that just landed. With
	// sureOnly off, a contact perception has not dropped still counts.
	// A bush does that: nothing is exposed, and the seen timer has failed.
	protected static bool ContactCurrent(
		SCR_AICombatComponent combat,
		BaseTarget candidate,
		float window,
		bool sureOnly)
	{
		if (!candidate)
			return false;

		bool sure =
			combat.IsTargetVisible(candidate) ||
			candidate.GetExposure() > 0 ||
			candidate.GetTimeSinceDetected() < window;
		if (sureOnly)
			return sure;

		if (sure)
			return true;

		if (IsTargetable(candidate.GetTargetEntity()))
			return true;

		return ContactPlace(candidate) != vector.Zero;
	}

	// A known enemy, or a contact whose side is not resolved yet. A
	// resolved friendly is not a suspected shot.
	protected static bool EngageFactionOk(
		IEntity self,
		BaseTarget candidate,
		bool knownFactionOnly)
	{
		if (!candidate)
			return false;

		IEntity other = candidate.GetTargetEntity();
		if (other)
		{
			if (other == self || SharesGroup(self, other) || !IsTargetable(other))
				return false;

			if (IsHostile(self, other))
				return true;

			if (knownFactionOnly || SCR_Faction.Cast(FactionOf(other)))
				return false;

			return true;
		}

		if (knownFactionOnly)
			return false;

		Faction perceived = candidate.GetPerceivedFaction();
		SCR_Faction mine = SCR_Faction.Cast(FactionOf(self));
		if (!perceived || !mine)
			return ContactPlace(candidate) != vector.Zero;

		SCR_Faction theirs = SCR_Faction.Cast(perceived);
		return theirs && mine.IsFactionEnemy(theirs);
	}

	protected static float ContactDistance(IEntity body, BaseTarget candidate)
	{
		if (!body || !candidate)
			return -1;

		vector place = vector.Zero;
		IEntity enemy = candidate.GetTargetEntity();
		if (enemy)
			place = enemy.GetOrigin();
		else
			place = ContactPlace(candidate);

		if (place == vector.Zero)
			return -1;

		return vector.Distance(body.GetOrigin(), place);
	}

	// The body when perception still has one, including a man in a bush.
	// Otherwise the last place it put him.
	protected static vector EngageAim(BaseTarget candidate)
	{
		if (!candidate)
			return vector.Zero;

		IEntity enemy = candidate.GetTargetEntity();
		if (enemy && IsTargetable(enemy))
			return ShotAimPoint(enemy);

		vector place = ContactPlace(candidate);
		if (place == vector.Zero)
			return vector.Zero;

		return ShotAimPoint(place);
	}

	protected static vector ContactPlace(BaseTarget candidate)
	{
		if (!candidate)
			return vector.Zero;

		vector place = candidate.GetLastDetectedPosition();
		if (place == vector.Zero)
			place = candidate.GetLastSeenPosition();

		return place;
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
		// A stored sprint look is the next step. Cancelling it while he
		// still has a man would take the head off that man.
		if (wasSprinting && !EngageOwnsTurn(body))
			DropSprintLook(body);
		else if (wasSprinting)
			s_SprintLook.Remove(body);
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
		if (!body || !s_SprintLook.Contains(body))
			return;

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
	// A clear or garrison reads the man the shot tick already chose.
	// An attack waypoint does not search from here. Attack engage aims.
	static bool EngagingContact(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return false;

		// The area search belongs to attack waypoints. A house order
		// already chose its man, or it has no custom shot and only
		// confirms the one the combat component selected.
		if (OnHouseOrder(body))
		{
			if (UseRoomCombat())
				return OrderContactSeen(body);

			return HasVisibleEnemy(body);
		}

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

		return false;
	}

	// The shot tick's man, or the selected target, inside CQB range and
	// still on the sight ray. This does not search the area.
	static bool OrderContactSeen(IEntity soldier)
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

		if (!HasVisibleEnemy(body))
			return false;

		SCR_AIUtilityComponent utility = UtilityOf(body);
		if (!utility || !utility.m_CombatComponent)
			return false;

		BaseTarget current = utility.m_CombatComponent.GetCurrentTarget();
		if (!current)
			return false;

		IEntity seen = current.GetTargetEntity();
		return seen && !PastOrderContact(body, seen);
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
		if (!body || YieldsWeaponForHeal(body))
			return false;

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
		if (!body || YieldsWeaponForHeal(body) || IsDown(body))
			return false;

		// Engage is already on a man. The advancing flag can be off for
		// that one soldier, and CRX then lowers him because the fire tree
		// has not produced a shot yet. The look stays on the man.
		bool engaging = IsEngageStopped(body) || PerceivedFireActive(body);
		if (!engaging && !IsAdvancing(body))
			return false;

		// A bound sprint lowers the rifle so he can run. Walk and jog stay up.
		if (IsBoundSprint(body))
			return false;

		if (IsIgnoringTargets(body))
			return false;

		if (SprintBeforeReload(body) || IsReloadBashing(body))
			return false;

		if (CannotShoot(body) || IsQuietReload(body) || !HasGunAmmo(body))
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

		// Attack engage asks again the moment a settled rifle drops. A clear
		// or garrison does not: snap aim flickers that flag, and another
		// raise restarts the animation so the shot never leaves.
		if (IsAttackSearch(body) && raised && !liveRaised && s_RaiseSettled.Contains(body))
		{
			s_RaiseSettled.RemoveItem(body);
			s_MoveWeaponUp.RemoveItem(body);
			return true;
		}

		if (!IsAttackSearch(body) && raised && !liveRaised && s_RaiseSentAt.Contains(body))
		{
			if (WorldTime() - s_RaiseSentAt.Get(body) < 500)
				return false;
		}

		// The raise is still playing while the flag is down. Sending it
		// again restarts that animation, so the rifle never finishes and
		// the trigger never gets a round out. A lower that landed after
		// the raise is the reason to ask again.
		if (IsAttackSearch(body) && raised && !liveRaised && known && wasRaised && s_RaiseSentAt.Contains(body))
		{
			float raiseAt = s_RaiseSentAt.Get(body);
			float lowerAt = -1;
			if (s_LowerSentAt.Contains(body))
				lowerAt = s_LowerSentAt.Get(body);

			if (lowerAt > raiseAt)
				return true;

			// Still inside the raise. Another call restarts it.
			if (WorldTime() - raiseAt < RAISE_RESEND_MS)
				return false;

			// It never reported up. Ask again.
			return true;
		}

		if (!known || wasRaised != raised)
			return true;

		if (liveRaised == raised)
			return false;

		// The 500 ms above has passed. A later node put the rifle down, or
		// the first raise never settled. Attack engage uses its own wait.
		if (raised && !IsAttackSearch(body))
			return true;

		// Already asked for it down. Another lower restarts the pose.
		// A raise that went out after that ask is the reason to send one more.
		float lowerAt = -1;
		if (s_LowerSentAt.Contains(body))
			lowerAt = s_LowerSentAt.Get(body);

		float raiseAt = -1;
		if (s_RaiseSentAt.Contains(body))
			raiseAt = s_RaiseSentAt.Get(body);

		return lowerAt < 0 || raiseAt > lowerAt;
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
			s_LowerSentAt.Set(body, WorldTime());
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
			if (s_Buildings.Count() == 0)
				s_NearBubbles.Clear();
			s_ShotServedAt.Remove(body);
			s_ReloadAt.Remove(body);
			s_ToppingOff.RemoveItem(body);
			s_EmptyReload.RemoveItem(body);
			s_DidReload.RemoveItem(body);
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
	// is empty. A wake runs the configured number of due men. A longer
	// pass continues next frame. 0 runs every due man in that wake.
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
		if (!KK_PerfLog.On())
		{
			ShotTickRun();
			return;
		}

		int started = System.GetTickCount();
		ShotTickRun();
		KK_PerfLog.End("CQB.ShotTick", started);
	}

	protected static void ShotTickRun()
	{
		if (!GetGame() || s_Buildings.Count() == 0 || !UseRoomCombat())
		{
			s_bShotTicking = false;
			s_ShotServedAt.Clear();
			return;
		}

		float now = WorldTime();
		int interval = ShotIntervalMs();
		if (interval < 1)
			interval = 1;

		s_ShotBodies.Clear();
		float soonest = interval;
		for (int i = 0; i < s_Buildings.Count(); i++)
		{
			IEntity body = s_Buildings.GetKey(i);
			if (!body)
				continue;

			if (!s_ShotServedAt.Contains(body))
			{
				s_ShotBodies.Insert(body);
				continue;
			}

			float age = now - s_ShotServedAt.Get(body);
			if (age >= interval)
			{
				s_ShotBodies.Insert(body);
				continue;
			}

			float remain = interval - age;
			if (remain < soonest)
				soonest = remain;
		}

		int ran = 0;
		int budget = ShotMenPerTick();
		foreach (IEntity body : s_ShotBodies)
		{
			if (budget > 0 && ran >= budget)
				break;

			SCR_AIUtilityComponent utility = UtilityOf(body);
			if (utility)
				ApplyRoomShot(utility);

			s_ShotServedAt.Set(body, now);
			ran++;
		}

		if (!GetGame() || s_Buildings.Count() == 0 || !UseRoomCombat())
		{
			s_bShotTicking = false;
			s_ShotServedAt.Clear();
			return;
		}

		int delay = interval;
		if (s_ShotBodies.Count() > ran)
			delay = 0;
		else
		{
			delay = (int)soonest;
			if (soonest > delay)
				delay = delay + 1;
		}

		s_iShotTickMs = delay;
		GetGame().GetCallqueue().CallLater(ShotTick, delay, false);
	}

	protected static SCR_AIUtilityComponent UtilityOf(IEntity body)
	{
		if (!body)
			return null;

		if (body == s_UtilityBody && s_UtilityCache && s_UtilityCache.m_OwnerEntity == body)
			return s_UtilityCache;

		s_UtilityBody = body;
		s_UtilityCache = null;

		AIControlComponent control = AIControlComponent.Cast(
			body.FindComponent(AIControlComponent)
		);

		if (!control)
			return null;

		SCR_ChimeraAIAgent agent = SCR_ChimeraAIAgent.Cast(control.GetAIAgent());
		if (!agent)
			return null;

		s_UtilityCache = agent.m_UtilityComponent;
		return s_UtilityCache;
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
	// and anyone hostile nearby. The sight ray steps through glass, then
	// the nearest man that ray can reach is the shot.
	static void RefreshShot(
		IEntity soldier,
		PerceptionComponent perception,
		BaseTarget selected)
	{
		if (!KK_PerfLog.On())
		{
			RefreshShotRun(soldier, perception, selected);
			return;
		}

		int started = System.GetTickCount();
		RefreshShotRun(soldier, perception, selected);
		KK_PerfLog.End("CQB.RefreshShot", started);
	}

	protected static void RefreshShotRun(
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
		BaseTarget previousBase = null;
		float previousLean = 0;
		if (s_ShotLook.Contains(body))
		{
			previousLook = s_ShotLook.Get(body);
			if (s_ShotLean.Contains(body))
				previousLean = s_ShotLean.Get(body);
		}

		if (s_ShotBase.Contains(body))
			previousBase = s_ShotBase.Get(body);

		// The last search put him here. One center ray keeps him. The next
		// interval walks the room again, so a closer man is not ignored.
		if (KeepHeldShot(body, previousLook))
		{
			UpdateRoomFire(body);
			return;
		}

		s_ShotLook.Remove(body);
		s_ShotBase.Remove(body);
		s_ShotLean.Remove(body);
		s_ShotLive.RemoveItem(body);

		if (IsIgnoringTargets(body))
		{
			s_MayHoldShot.RemoveItem(body);
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
				ConsiderOpen(body, perception, true, previousLook, previousBase);
			// A jog has no building, and the selected target may be empty.
			// Reaper often keeps a sector there instead of a body. The
			// perceived enemies are still the ones he can shoot.
			else if (IsMoveFire(body))
				ConsiderOpen(body, perception, false, previousLook, previousBase);

			if (!s_ShotLive.Contains(body))
				ConsiderSelected(body, selected);

			if (IsMoveFire(body))
				HoldMoveShot(body, now, previousLook, previousLean);

			if (s_ShotLive.Contains(body) && s_FreshSightBody == body)
				NoteHeldSearch(body);
			else if (!s_ShotLive.Contains(body))
				s_MayHoldShot.RemoveItem(body);
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
		ConsiderIndoor(perception, body, building, ETargetCategory.ENEMY, true);
		ConsiderIndoor(perception, body, building, ETargetCategory.DETECTED, true);
		ConsiderIndoor(perception, body, building, ETargetCategory.UNKNOWN, true);
		// Perception misses a man whose own ray stopped on a pane. One
		// nearby query still finds him. While the current man is in sight
		// that query stays off.
		if (!HoldPreviousShot(body, previousLook, previousBase))
			CollectNearby(body, building);

		if (CommitNearestShot(body, perception))
		{
			NoteHeldSearch(body);
			UpdateRoomFire(body);
			return;
		}

		ConsiderSelected(body, selected);
		if (s_ShotLive.Contains(body))
			NoteHeldSearch(body);
		else
			s_MayHoldShot.RemoveItem(body);
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
		NoteHeldSearch(body);
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
		s_MayHoldShot.RemoveItem(body);
		if (s_FreshSightBody == body)
			s_FreshSightBody = null;
		s_MoveShotSeen.Remove(body);
		// The cool-off is the walk off a blocked shot. Dropping the shot
		// record is part of that walk, so it must not cancel the move.
		if (!BarrelCooling(body))
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
		bool enemiesMustBeHostile,
		IEntity previousLook,
		BaseTarget previousBase)
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
		ConsiderOpenCategory(
			perception,
			body,
			ETargetCategory.ENEMY,
			enemiesMustBeHostile
		);
		ConsiderOpenCategory(perception, body, ETargetCategory.DETECTED, true);
		ConsiderOpenCategory(perception, body, ETargetCategory.UNKNOWN, true);
		if (!HoldPreviousShot(body, previousLook, previousBase))
			CollectNearby(body, null);

		CommitNearestShot(body, perception);
	}

	// True when the last full search's man is still on the center ray.
	// The room walk waits until the next interval.
	protected static bool KeepHeldShot(IEntity body, IEntity previousLook)
	{
		if (!body || !s_MayHoldShot.Contains(body))
			return false;

		s_MayHoldShot.RemoveItem(body);
		if (!previousLook || !IsTargetable(previousLook) || !IsHostile(body, previousLook))
			return false;

		if (PastOrderContact(body, previousLook))
			return false;

		if (!CanSeeEntity(body, previousLook))
			return false;

		if (s_ShotLean.Contains(body))
			s_ShotLean.Set(body, 0);

		s_FreshSightBody = body;
		return true;
	}

	// A search that found a man on the center ray. The next interval may
	// keep him. A lean stays on the full search, because the center is blocked.
	protected static void NoteHeldSearch(IEntity body)
	{
		if (!body)
			return;

		s_FreshSightBody = body;
		float lean = 0;
		if (s_ShotLean.Contains(body))
			lean = s_ShotLean.Get(body);

		if (lean == 0)
			s_MayHoldShot.Insert(body);
		else
			s_MayHoldShot.RemoveItem(body);
	}

	// True when the man from the last check is still a shot. He is put
	// back on the list, and the sphere searches stay off for this pass.
	protected static bool HoldPreviousShot(IEntity body, IEntity previousLook, BaseTarget previousBase)
	{
		if (!body || !previousLook || !IsTargetable(previousLook))
			return false;

		if (PastOrderContact(body, previousLook))
			return false;

		if (!CanSeeEntity(body, previousLook))
			return false;

		AddCandidate(
			previousLook,
			vector.Distance(body.GetOrigin(), previousLook.GetOrigin()),
			previousBase
		);
		return true;
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

			s_FreshSightBody = body;
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

	// One query for men perception did not list. Indoors that is the room.
	// Outside the shell it is CQB range. The sight ray, not a second list,
	// decides whether a pane is in the way.
	protected static void CollectNearby(IEntity body, IEntity building)
	{
		if (!body)
			return;

		float contact = OrderContactRange();
		bool watch = OnGlassWatch(body);
		bool attackWatch = IsAttackSearch(body);
		float radius = 0;
		if (building)
			radius = ROOM_SCAN_RADIUS;
		if (watch && contact > radius)
			radius = contact;
		if (attackWatch && ATTACK_CONTACT_RANGE > radius)
			radius = ATTACK_CONTACT_RANGE;

		if (radius < 1)
			return;

		if (attackWatch && !watch)
			GatherAttack(body, radius);
		else
			GatherBodies(body, radius);
		foreach (IEntity enemy : s_NearBodies)
		{
			if (!enemy || !IsTargetable(enemy) || !IsHostile(body, enemy))
				continue;

			float distance = vector.Distance(body.GetOrigin(), enemy.GetOrigin());
			bool inside = false;
			if (building)
			{
				vector place = enemy.GetOrigin();
				ChimeraCharacter character = ChimeraCharacter.Cast(enemy);
				if (character && IsDown(character))
				{
					vector eye = character.EyePosition();
					if (eye != vector.Zero)
						place = eye;
				}

				inside = PositionInside(building, place) ||
					PositionInside(building, enemy.GetOrigin());
			}

			if (inside)
			{
				AddCandidate(enemy, distance, null);
				continue;
			}

			float limit = contact;
			if (attackWatch && ATTACK_CONTACT_RANGE > limit)
				limit = ATTACK_CONTACT_RANGE;

			if ((!watch && !attackWatch) || limit < 1 || distance > limit)
				continue;

			AddCandidate(enemy, distance, null);
		}
	}

	protected static bool OnGlassWatch(IEntity body)
	{
		return OrderOwnsBehavior(body) || OnHouseOrder(body);
	}

	// One sphere for the men on the building, and one for the men still
	// walking in. Sight stays per soldier. A man the shared sphere does
	// not cover searches around himself.
	protected static void GatherBodies(IEntity body, float radius)
	{
		s_NearBodies.Clear();
		if (!body || radius < 1)
			return;

		IEntity building;
		if (s_Buildings.Contains(body))
			building = s_Buildings.Get(body);

		if (building && CopySharedBodies(body, building, radius))
			return;

		QueryBodies(body.GetOrigin(), radius, s_NearBodies, body);
	}

	protected static bool CopySharedBodies(IEntity body, IEntity building, float radius)
	{
		KK_NearBubble bubble = EnsureNearBubble(building);
		if (!bubble)
			return false;

		// The bubble was built for the room scan and the CQB range.
		// A wider search has to look for itself.
		if (radius > SharedSearchRadius() + 0.5)
			return false;

		if (SphereCovers(bubble.m_Center, bubble.m_Radius, body.GetOrigin(), radius))
		{
			CopyInRange(bubble.m_Bodies, body, radius);
			return true;
		}

		if (
			bubble.m_RoadRadius > 0 &&
			SphereCovers(bubble.m_RoadCenter, bubble.m_RoadRadius, body.GetOrigin(), radius)
		)
		{
			CopyInRange(bubble.m_RoadBodies, body, radius);
			return true;
		}

		return false;
	}

	protected static float SharedSearchRadius()
	{
		float search = OrderContactRange();
		if (ROOM_SCAN_RADIUS > search)
			search = ROOM_SCAN_RADIUS;

		return search;
	}

	protected static bool SphereCovers(vector center, float reach, vector point, float radius)
	{
		if (reach < 1)
			return false;

		return vector.Distance(center, point) + radius <= reach + 0.5;
	}

	protected static void CopyInRange(array<IEntity> source, IEntity body, float radius)
	{
		if (!source || !body)
			return;

		float radiusSq = radius * radius;
		vector origin = body.GetOrigin();
		foreach (IEntity character : source)
		{
			if (!character || character == body)
				continue;

			vector gap = origin - character.GetOrigin();
			if (gap.LengthSq() > radiusSq)
				continue;

			s_NearBodies.Insert(character);
		}
	}

	protected static KK_NearBubble EnsureNearBubble(IEntity building)
	{
		if (!building)
			return null;

		DropUnusedBubbles();

		KK_NearBubble bubble = s_NearBubbles.Get(building);
		if (!bubble)
		{
			bubble = new KK_NearBubble();
			bubble.m_At = -1;
			if (!bubble.m_Bodies)
				bubble.m_Bodies = new array<IEntity>();
			if (!bubble.m_RoadBodies)
				bubble.m_RoadBodies = new array<IEntity>();
			s_NearBubbles.Set(building, bubble);
		}

		float now = WorldTime();
		int interval = ShotIntervalMs();
		if (interval < 1)
			interval = 1;

		if (bubble.m_At >= 0 && now >= bubble.m_At && now - bubble.m_At < interval)
			return bubble;

		bubble.m_At = now;

		vector center;
		float boundRadius;
		BuildingSpan(building, center, boundRadius);
		float search = SharedSearchRadius();
		float houseReach = boundRadius + HOUSE_NEAR_MARGIN + search;
		bubble.m_Center = center;
		bubble.m_Radius = houseReach;

		bool anyoneHome = false;
		s_RoadScratch.Clear();
		for (int i = 0; i < s_Buildings.Count(); i++)
		{
			if (s_Buildings.GetElement(i) != building)
				continue;

			IEntity other = s_Buildings.GetKey(i);
			if (!other)
				continue;

			if (SphereCovers(center, houseReach, other.GetOrigin(), search))
			{
				anyoneHome = true;
				continue;
			}

			s_RoadScratch.Insert(other);
		}

		if (anyoneHome)
			QueryBodies(center, houseReach, bubble.m_Bodies, null);
		else
		{
			bubble.m_Radius = 0;
			bubble.m_Bodies.Clear();
		}

		TrimRoadCluster(search, search + ROAD_SHARE_SPREAD);
		if (s_RoadScratch.Count() == 0)
		{
			bubble.m_RoadRadius = 0;
			bubble.m_RoadBodies.Clear();
			return bubble;
		}

		vector centroid = RoadCentroid();
		float need = search;
		foreach (IEntity other : s_RoadScratch)
		{
			float reach = vector.Distance(centroid, other.GetOrigin()) + search;
			if (reach > need)
				need = reach;
		}

		bubble.m_RoadCenter = centroid;
		bubble.m_RoadRadius = need;
		QueryBodies(centroid, need, bubble.m_RoadBodies, null);
		return bubble;
	}

	protected static void TrimRoadCluster(float search, float cap)
	{
		while (s_RoadScratch.Count() > 1)
		{
			vector centroid = RoadCentroid();
			int farthest = 0;
			float farthestDistance = -1;
			for (int i = 0; i < s_RoadScratch.Count(); i++)
			{
				float distance = vector.Distance(
					centroid,
					s_RoadScratch[i].GetOrigin()
				);
				if (distance <= farthestDistance)
					continue;

				farthestDistance = distance;
				farthest = i;
			}

			if (farthestDistance + search <= cap)
				return;

			s_RoadScratch.Remove(farthest);
		}
	}

	protected static vector RoadCentroid()
	{
		vector centroid = vector.Zero;
		int count = s_RoadScratch.Count();
		if (count < 1)
			return centroid;

		for (int i = 0; i < count; i++)
			centroid = centroid + s_RoadScratch[i].GetOrigin();

		return centroid * (1.0 / count);
	}

	protected static void BuildingSpan(IEntity building, out vector center, out float boundRadius)
	{
		center = vector.Zero;
		boundRadius = 0;
		if (!building)
			return;

		vector mins;
		vector maxs;
		building.GetBounds(mins, maxs);
		center = building.CoordToParent((mins + maxs) * 0.5);
		float dx = (maxs[0] - mins[0]) * 0.5;
		float dy = (maxs[1] - mins[1]) * 0.5;
		float dz = (maxs[2] - mins[2]) * 0.5;
		boundRadius = Math.Sqrt((dx * dx) + (dy * dy) + (dz * dz));
	}

	protected static void DropUnusedBubbles()
	{
		for (int i = s_NearBubbles.Count() - 1; i >= 0; i--)
		{
			IEntity building = s_NearBubbles.GetKey(i);
			if (BuildingHeld(building))
				continue;

			s_NearBubbles.Remove(building);
		}
	}

	protected static bool BuildingHeld(IEntity building)
	{
		if (!building)
			return false;

		for (int i = 0; i < s_Buildings.Count(); i++)
		{
			if (s_Buildings.GetElement(i) == building)
				return true;
		}

		return false;
	}

	// Men around an attack soldier. The same sphere is reused until the
	// scan interval passes, because contact and the shot both ask.
	protected static void GatherAttack(IEntity body, float radius)
	{
		s_NearBodies.Clear();
		if (!body || radius < 1)
			return;

		float now = WorldTime();
		if (
			s_AttackGatherAt.Contains(body) &&
			now - s_AttackGatherAt.Get(body) < ATTACK_SCAN_MS &&
			s_AttackGather.Contains(body)
		)
		{
			array<IEntity> cached = s_AttackGather.Get(body);
			if (!cached)
				return;

			float radiusSq = radius * radius;
			vector origin = body.GetOrigin();
			foreach (IEntity character : cached)
			{
				if (!character || character == body)
					continue;

				vector gap = origin - character.GetOrigin();
				if (gap.LengthSq() > radiusSq)
					continue;

				s_NearBodies.Insert(character);
			}

			return;
		}

		GatherBodies(body, radius);
		array<IEntity> copy = new array<IEntity>();
		foreach (IEntity character : s_NearBodies)
		{
			if (character)
				copy.Insert(character);
		}

		s_AttackGather.Set(body, copy);
		s_AttackGatherAt.Set(body, now);
	}

	protected static void QueryBodies(vector origin, float radius, array<IEntity> into, IEntity skip)
	{
		if (!into)
			return;

		into.Clear();
		if (!s_QueryBodies)
			s_QueryBodies = new array<IEntity>();

		s_QueryBodies.Clear();
		if (radius < 1)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world && skip)
			world = skip.GetWorld();

		if (!world)
			return;

		s_NearViewer = skip;
		world.QueryEntitiesBySphere(
			origin,
			radius,
			OnNearBody,
			null,
			EQueryEntitiesFlags.DYNAMIC | EQueryEntitiesFlags.WITH_OBJECT
		);
		s_NearViewer = null;

		foreach (IEntity character : s_QueryBodies)
		{
			if (!character)
				continue;

			into.Insert(character);
		}
	}

	protected static bool OnNearBody(IEntity entity)
	{
		if (!s_QueryBodies)
			return true;

		ChimeraCharacter character = CharacterOf(entity);
		if (!character || character == s_NearViewer)
			return true;

		if (s_QueryBodies.Contains(character))
			return true;

		s_QueryBodies.Insert(character);
		return true;
	}

	// Nearest first, and only a few of them. The sight rays then stop at the
	// first one that is actually clear.
	protected static void AddCandidate(
		IEntity enemy,
		float distance,
		BaseTarget target)
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
			int drop = s_Candidates.Count() - 1;
			IEntity dropped = s_Candidates[drop];
			s_Candidates.Remove(drop);
			s_CandidateDist.Remove(drop);
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
		return LifeStanding(Controller(character));
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

		// A clear center line does not need the lean rays. Those rays are
		// the cost that grew with every extra man in the room.
		if (CanSeeEntity(body, enemy))
			return true;

		bool left;
		bool right;
		SidesClear(body, enemy, left, right);

		// The center ray hits the cover he is standing against. A clear
		// side is still a shot, and the trigger needs that side remembered.
		// The same light and concealment check follows that ray.
		if (!left && !right)
			return false;

		if (!ConditionsAllow(body, enemy))
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

		// The plant will not turn him. The trigger stays off until the
		// heading written for this burst is the one he is standing on.
		if (s_InstantAim.Contains(body))
			return FacingReturnShot(body, enemy);

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
		if (!KK_PerfLog.On())
		{
			LeanPumpRun();
			return;
		}

		int started = System.GetTickCount();
		LeanPumpRun();
		KK_PerfLog.End("CQB.LeanPump", started);
	}

	protected static void LeanPumpRun()
	{
		if (!GetGame() || s_WantedLean.Count() == 0)
		{
			s_bLeanPumping = false;
			return;
		}

		s_LeanBodies.Clear();
		for (int i = 0; i < s_WantedLean.Count(); i++)
			s_LeanBodies.Insert(s_WantedLean.GetKey(i));

		foreach (IEntity body : s_LeanBodies)
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
		// and the garrison only. Dead, downed, or otherwise out of the
		// CQB fight does not get it, and neither does a gun that cannot fire.
		if (
			!SnapAimEnabled() ||
			!OwnsShot(body) ||
			point == vector.Zero ||
			!IsLiving(body) ||
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
		ApplySnapAim(body, point, null, null, null);
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

	// The game callqueue keeps ticking while the simulation is paused.
	// Game time is the pause the menu and the workbench use.
	protected static bool SimPaused()
	{
		if (!GetGame())
			return false;

		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return false;

		return world.IsGameTimePaused() || world.GetTimeScale() <= 0;
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
		if (!KK_PerfLog.On())
		{
			SnapAimPumpRun();
			return;
		}

		int started = System.GetTickCount();
		SnapAimPumpRun();
		KK_PerfLog.End("SnapAim.Pump", started);
	}

	protected static void SnapAimPumpRun()
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

		// Stay scheduled, but do not write the body or the rifle.
		if (SimPaused())
		{
			GetGame().GetCallqueue().CallLater(SnapAimPump, 0, false);
			return;
		}

		// The shot tick chooses chest or head and stores that point. This
		// pass only writes it again, so a frame does not trace or search
		// his inventory. A dry gun is dropped on that same tick.
		bool roomCombat = UseRoomCombat();
		float range = OrderContactRange();
		float rangeSq = range * range;

		s_SnapBodies.Clear();
		for (int i = 0; i < s_SnapAim.Count(); i++)
			s_SnapBodies.Insert(s_SnapAim.GetKey(i));

		int applyPerf = KK_PerfLog.Begin();
		int applyCount = s_SnapBodies.Count();
		foreach (IEntity body : s_SnapBodies)
		{
			if (!body || !s_SnapAim.Contains(body))
			{
				ClearSnapAim(body);
				continue;
			}

			CharacterControllerComponent controller = Controller(body);
			if (
				!roomCombat ||
				!SnapStillOwned(body, controller) ||
				IsBoundSprint(body) ||
				SprintBeforeReload(body)
			)
			{
				ClearSnapAim(body);
				continue;
			}

			SCR_AIUtilityComponent utility = UtilityOf(body);
			IEntity engaged = EngagedFrom(body, utility);
			vector origin = body.GetOrigin();
			vector held = s_SnapAim.Get(body);
			vector fromHeld = origin - held;
			bool heldFar = fromHeld.LengthSq() > rangeSq;
			bool enemyFar = false;
			if (engaged)
			{
				vector fromEnemy = origin - engaged.GetOrigin();
				enemyFar = fromEnemy.LengthSq() > rangeSq;
			}
			if (enemyFar || heldFar)
			{
				ClearSnapAim(body);
				continue;
			}

			ApplySnapAim(body, held, controller, utility, engaged);
		}

		KK_PerfLog.End("SnapAim.Apply", applyPerf, applyCount);

		if (s_SnapAim.Count() == 0 || !GetGame())
		{
			s_bSnapAimPumping = false;
			return;
		}

		GetGame().GetCallqueue().CallLater(SnapAimPump, 0, false);
	}

	// Still on the clear or the garrison, and still in the CQB fight.
	// The controller is the one the pump already resolved.
	protected static bool SnapStillOwned(IEntity body, CharacterControllerComponent controller)
	{
		if (!body || !LifeStanding(controller))
			return false;

		return s_OrderBehavior.Contains(body) || OnHouseOrder(body);
	}

	protected static void ApplySnapAim(
		IEntity body,
		vector point,
		CharacterControllerComponent controller,
		SCR_AIUtilityComponent utility,
		IEntity enemy)
	{
		if (SimPaused())
			return;

		if (!controller)
			controller = Controller(body);
		if (!LifeStanding(controller))
		{
			ClearSnapAim(body);
			return;
		}

		if (!utility)
			utility = UtilityOf(body);
		if (!enemy)
			enemy = EngagedFrom(body, utility);

		vector pose = point + SnapAimError(body, point, enemy);
		vector flat = pose - body.GetOrigin();
		flat[1] = 0;
		if (flat.Length() < 0.05)
		{
			HoldSnapLook(utility, pose);
			return;
		}

		flat.Normalize();
		float heading = flat.ToYaw();
		StopSnapTurn(utility);
		SnapFacing(body, controller, heading);
		// The weapon is parented to the facing he has now. Using the heading
		// just requested aims the barrel off to the side until that turn lands.
		float bodyYaw = body.GetYawPitchRoll()[0];
		vector muzzle = MuzzlePosition(body, 0);
		WriteAimAt(controller.GetAimingComponent(), muzzle, pose, bodyYaw);
		HoldSnapLook(utility, pose);
		LogSnapAim(body, controller, utility, pose, muzzle, heading, bodyYaw);
	}

	// Combat movement also requests an aim turn. That request is dropped
	// while the snap holds him. The weapon bone stays with the animation
	// graph; the shot is aimed on its own.
	protected static void StopSnapTurn(SCR_AIUtilityComponent utility)
	{
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

	// The attack burst cannot use the combat-move aim. That aim is the
	// step the plant is holding off. Write the heading onto him instead.
	protected static void FaceReturnShot(IEntity body, IEntity enemy)
	{
		if (!body || !enemy || !s_InstantAim.Contains(body))
			return;

		vector point = ShotAimPoint(body, enemy);
		if (point == vector.Zero)
			return;

		CharacterControllerComponent controller = Controller(body);
		if (!LifeStanding(controller))
			return;

		vector flat = point - body.GetOrigin();
		flat[1] = 0;
		if (flat.Length() < 0.05)
			return;

		flat.Normalize();
		float heading = flat.ToYaw();
		SnapFacing(body, controller, heading);
		// The plant owns the pose, so a plain heading write does not turn
		// him. The forced heading is what a hold uses to turn in place.
		controller.SetHeadingAngle(heading * Math.DEG2RAD, true);
		s_ReturnFacing.Set(body, heading);
	}

	protected static bool FacingReturnShot(IEntity body, IEntity enemy)
	{
		if (!body || !enemy)
			return false;

		vector point = ShotAimPoint(body, enemy);
		if (point == vector.Zero)
			return false;

		vector flat = point - body.GetOrigin();
		flat[1] = 0;
		if (flat.Length() < 0.05)
			return true;

		flat.Normalize();
		float want = flat.ToYaw();
		float gap = YawDelta(body.GetYawPitchRoll()[0], want);
		if (Math.AbsFloat(gap) < RETURN_FIRE_AIM_DEG)
			return true;

		if (!s_ReturnFacing.Contains(body))
			return false;

		float commanded = YawDelta(s_ReturnFacing.Get(body), want);
		return Math.AbsFloat(commanded) < RETURN_FIRE_AIM_DEG;
	}

	protected static void SnapFacing(
		IEntity body,
		CharacterControllerComponent controller,
		float headingDeg)
	{
		vector angles = body.GetYawPitchRoll();
		if (Math.AbsFloat(YawDelta(angles[0], headingDeg)) < SNAP_AIM_WRITE_DEG)
			return;

		angles[0] = headingDeg;
		body.SetYawPitchRoll(angles);

		CharacterInputContext input = controller.GetInputContext();
		if (input)
			input.SetHeadingAngle(HeadingRadians(headingDeg));
	}

	// The input heading is 0..360. Snap aim only writes that. Forcing the
	// controller heading is the attack plant, and it fights this turn.
	protected static float HeadingRadians(float headingDeg)
	{
		float radians = headingDeg * Math.DEG2RAD;
		while (radians < 0)
			radians = radians + Math.PI2;
		while (radians >= Math.PI2)
			radians = radians - Math.PI2;

		return radians;
	}

	// Shortest signed gap, in degrees. Positive is toDeg to the right of fromDeg.
	protected static float YawDelta(float fromDeg, float toDeg)
	{
		float gap = toDeg - fromDeg;
		while (gap > 180)
			gap = gap - 360;
		while (gap < -180)
			gap = gap + 360;

		return gap;
	}

	// The look behavior keeps the last point it was given. Write the pose
	// every frame, and restart only when that point has moved.
	protected static void HoldSnapLook(SCR_AIUtilityComponent utility, vector pose)
	{
		if (!utility || !utility.m_LookAction || pose == vector.Zero)
			return;

		utility.m_LookAction.KK_HoldSnapPoint(pose);
	}

	// Once a second, the pose we wrote against the direction the rifle reports.
	protected static void LogSnapAim(
		IEntity body,
		CharacterControllerComponent controller,
		SCR_AIUtilityComponent utility,
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

		return point + SnapAimError(body, point, EngagedEnemy(body));
	}

	// Yaw is the gap between the heading and the line from the muzzle to the
	// pose. Pitch is that line's own pitch. Negative pitch is up.
	protected static void WriteAimAt(AimingComponent aiming, vector from, vector point, float headingDeg)
	{
		vector dir = point - from;
		if (dir.Length() < 0.05)
			return;

		vector angles = dir.VectorToAngles();
		float yaw = YawDelta(headingDeg, angles[0]);
		// Written every frame. A level shot reports a pitch just under 360,
		// and the component's current pitch is near 0, so those two look
		// like the same angle. Skipping that write left the rifle unarmed
		// and the trigger never started.
		WriteAimRotation(aiming, yaw, angles[1]);
	}

	// Snap aim only. The error switch puts that shot on the point itself.
	protected static vector SnapAimError(IEntity body, vector point, IEntity enemy)
	{
		if (!SnapAimErrorEnabled())
			return vector.Zero;

		return HeldSnapAimError(body, point, enemy, false);
	}

	// Held offset around the aim point. A new man, or the hold running
	// out, rolls again. The axes match the aim-error node. The size is
	// the skill angle, so a long shot is not a laser. An unseen place
	// stays on one spot in the area long enough to shoot, then moves.
	protected static vector HeldSnapAimError(IEntity body, vector point, IEntity enemy, bool unseen)
	{
		float now = WorldTime();
		float holdMs = SNAP_AIM_ERROR_MS;
		if (unseen)
			holdMs = ENGAGE_AREA_HOLD_MS;

		bool sameUnseen =
			s_SnapErrorUnseen.Contains(body) &&
			s_SnapErrorUnseen.Get(body) == unseen;
		bool held =
			sameUnseen &&
			s_SnapError.Contains(body) &&
			s_SnapErrorAt.Contains(body) &&
			now - s_SnapErrorAt.Get(body) < holdMs &&
			(
				!enemy ||
				(s_SnapErrorEnemy.Contains(body) && s_SnapErrorEnemy.Get(body) == enemy)
			);
		if (held)
			return s_SnapError.Get(body);

		vector offset = ComputeSnapAimError(body, enemy, point, unseen);
		s_SnapError.Set(body, offset);
		s_SnapErrorAt.Set(body, now);
		s_SnapErrorUnseen.Set(body, unseen);
		if (enemy)
			s_SnapErrorEnemy.Set(body, enemy);
		else
			s_SnapErrorEnemy.Remove(body);

		return offset;
	}

	protected static vector ComputeSnapAimError(IEntity body, IEntity enemy, vector point, bool unseen)
	{
		if (!KK_PerfLog.On())
			return ComputeSnapAimErrorRun(body, enemy, point, unseen);

		int started = System.GetTickCount();
		vector offset = ComputeSnapAimErrorRun(body, enemy, point, unseen);
		KK_PerfLog.End("SnapAim.Error", started);
		return offset;
	}

	protected static vector ComputeSnapAimErrorRun(IEntity body, IEntity enemy, vector point, bool unseen)
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

		if (unseen)
			return AreaAimOffset(axisX, axisY, distance);

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

	// A spot in the patch around the guess. Spread flat across the disk,
	// so the rounds are not grouped on the hidden man.
	protected static vector AreaAimOffset(vector axisX, vector axisY, float distance)
	{
		float radius = distance * Math.Tan(ENGAGE_AREA_DEG * Math.DEG2RAD);
		if (radius < ENGAGE_AREA_MIN_M)
			radius = ENGAGE_AREA_MIN_M;

		// The trigger only closes inside the engage cone. A 3 m patch at
		// arm's length is a wider angle than that cone, so a raised rifle
		// that is already on him never counts.
		float fit = distance * Math.Tan(EngageAimDegrees(distance) * 0.5 * Math.DEG2RAD);
		if (fit < 0.2)
			fit = 0.2;
		if (radius > fit)
			radius = fit;

		float reach = radius * Math.Sqrt(Math.RandomFloat01());
		float turn = Math.RandomFloat01() * 360 * Math.DEG2RAD;
		return (axisX * (reach * Math.Cos(turn))) + (axisY * (reach * Math.Sin(turn)));
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

		if (!IsLiving(shooter))
		{
			ClearSnapAim(shooter);
			return;
		}

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
			!IsLiving(body) ||
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
		if (!enemy)
			return vector.Zero;

		int mode = AimMode();
		if (mode == KK_EAimMode.RANDOM)
			mode = LatchedAim(shooter, enemy);

		vector chest = ChestPoint(enemy);
		vector head = HeadPoint(enemy);
		if (mode == KK_EAimMode.HEAD)
		{
			if (head == vector.Zero)
				return chest;

			return head;
		}

		if (mode == KK_EAimMode.CHEST)
		{
			if (chest == vector.Zero)
				return head;

			return chest;
		}

		vector preferred = chest;
		vector other = head;
		if (mode == KK_EAimMode.HEAD_THEN_CHEST)
		{
			preferred = head;
			other = chest;
		}

		if (preferred != vector.Zero && (!shooter || PointVisible(shooter, enemy, preferred)))
			return preferred;

		if (other != vector.Zero && shooter && PointVisible(shooter, enemy, other))
			return other;

		if (preferred != vector.Zero)
			return preferred;

		return other;
	}

	// Preferred point when there is no shooter to test cover with. The eyes
	// sit above the barrel and were the miss, so this is still not the eyes.
	static vector ShotAimPoint(IEntity enemy)
	{
		int mode = AimMode();
		if (mode == KK_EAimMode.HEAD || mode == KK_EAimMode.HEAD_THEN_CHEST)
			return HeadPoint(enemy);

		return ChestPoint(enemy);
	}

	protected static int AimMode()
	{
		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return KK_EAimMode.CHEST_THEN_HEAD;

		return mode.KK_GetAimMode();
	}

	// Chest or head first for this man, and that choice stays. The other
	// point is used when the first is behind cover.
	protected static int LatchedAim(IEntity shooter, IEntity enemy)
	{
		IEntity body = CharacterBody(shooter);
		if (!body)
			body = shooter;

		if (
			body &&
			s_AimPick.Contains(body) &&
			s_AimPickEnemy.Contains(body) &&
			s_AimPickEnemy.Get(body) == enemy
		)
			return s_AimPick.Get(body);

		int pick = KK_EAimMode.CHEST_THEN_HEAD;
		if (Math.RandomFloat01() < 0.5)
			pick = KK_EAimMode.HEAD_THEN_CHEST;

		if (!body)
			return pick;

		s_AimPick.Set(body, pick);
		s_AimPickEnemy.Set(body, enemy);
		return pick;
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

	// True for the whole cool-off. He left because the eye ray or the
	// shot record failed, so neither of those cancels the walk. A hit
	// still ends it.
	static bool BarrelWantsMove(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (BarrelCooling(body))
			return true;

		if (body && s_BarrelCoolUntil.Contains(body))
		{
			s_BarrelCoolUntil.Remove(body);
			s_BarrelBlockedTarget.Remove(body);
			s_BarrelTest.RemoveItem(body);
		}

		return false;
	}

	// World time the cool-off ends. 0 when he is not in one.
	static float BarrelCoolDeadline(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_BarrelCoolUntil.Contains(body))
			return 0;

		return s_BarrelCoolUntil.Get(body);
	}

	// The man the cool-off is about. Null once that move has ended.
	static IEntity BarrelMoveTarget(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		if (!body || !s_BarrelBlockedTarget.Contains(body))
			return null;

		return s_BarrelBlockedTarget.Get(body);
	}

	// Garrison sets this once he is on the side node. The cool-off
	// still ends the test, and a hit still ends it early.
	static void SetBarrelTest(IEntity soldier, bool testing)
	{
		IEntity body = CharacterBody(soldier);
		if (!body)
			return;

		if (testing)
			s_BarrelTest.Insert(body);
		else
			s_BarrelTest.RemoveItem(body);
	}

	static bool IsBarrelTest(IEntity soldier)
	{
		IEntity body = CharacterBody(soldier);
		return body && s_BarrelTest.Contains(body);
	}

	protected static bool BarrelCooling(IEntity body)
	{
		if (!body || !s_BarrelCoolUntil.Contains(body))
			return false;

		return WorldTime() < s_BarrelCoolUntil.Get(body);
	}

	protected static IEntity EngagedEnemy(IEntity body)
	{
		return EngagedFrom(body, null);
	}

	// utility may already be resolved. A null one is looked up only when the
	// shot map does not already name the man.
	protected static IEntity EngagedFrom(IEntity body, SCR_AIUtilityComponent utility)
	{
		if (!body)
			return null;

		if (s_ShotLook.Contains(body))
		{
			IEntity shot = s_ShotLook.Get(body);
			if (shot && IsTargetable(shot))
				return shot;
		}

		if (!utility)
			utility = UtilityOf(body);
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
		NoteReturnHit(hitOwner, damageContext);

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

	// A bullet that landed. The lane ray is unchanged. This is only the
	// record that lets him shoot back after that hit.
	protected static void NoteReturnHit(IEntity hitOwner, BaseDamageContext damageContext)
	{
		if (!damageContext || damageContext.damageValue <= 0 || !damageContext.instigator)
			return;

		if (damageContext.damageType != EDamageType.KINETIC)
			return;

		IEntity victim = hitOwner;
		if (!ChimeraCharacter.Cast(victim))
			victim = damageContext.hitEntity;

		victim = CharacterBody(victim);
		IEntity shooter = InstigatorBody(damageContext.instigator);
		if (!victim || !shooter || SameCharacter(shooter, victim))
			return;

		if (SharesGroup(victim, shooter) || KnownFriendly(victim, shooter))
			return;

		LogThreatNote(
			s_ThreatHitLog,
			victim,
			string.Format("hit shooter=%1", ThreatWho(shooter))
		);
		NoteBulletThreat(victim, shooter, THREAT_LANDED);
	}

	protected static void NoteBulletThreat(IEntity victim, IEntity shooter, int kind)
	{
		if (!victim || !shooter || kind == 0)
			return;

		int kept = kind;
		if (s_HitBy.Contains(victim) && SameCharacter(s_HitBy.Get(victim), shooter) && s_HitByKind.Contains(victim))
			kept = kind | s_HitByKind.Get(victim);

		s_HitBy.Set(victim, shooter);
		s_HitByKind.Set(victim, kept);
		s_HitByAt.Set(victim, WorldTime());
	}

	// A bullet record of this kind from this man. Self and a nearby
	// squadmate are chosen separately. Landed hits, nearby strikes, and
	// open muzzle paths are separate bits.
	protected static bool ShooterHitSquad(IEntity shooter, IEntity body, int kind, bool self, bool mates)
	{
		if (!shooter || !body || kind == 0)
			return false;

		if (self && HitByShooter(body, shooter, kind))
			return true;

		if (!mates)
			return false;

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
			if (!mate || mate == body)
				continue;

			if (vector.Distance(body.GetOrigin(), mate.GetOrigin()) > SQUAD_RETURN_M)
				continue;

			if (HitByShooter(mate, shooter, kind))
				return true;
		}

		return false;
	}

	protected static bool HitByShooter(IEntity victim, IEntity shooter, int kind)
	{
		if (!victim || !shooter || kind == 0 || !s_HitBy.Contains(victim) || !s_HitByAt.Contains(victim) || !s_HitByKind.Contains(victim))
			return false;

		if (WorldTime() - s_HitByAt.Get(victim) > HeardShotMs())
			return false;

		if ((s_HitByKind.Get(victim) & kind) == 0)
			return false;

		return SameCharacter(s_HitBy.Get(victim), shooter);
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
		s_BarrelTest.RemoveItem(body);
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

		int index = -1;
		if (weaponEnt == s_MuzzleEnt)
			index = s_MuzzleBone;

		if (index < 0)
		{
			index = anim.GetBoneIndex("muzzle");
			if (index < 0)
				index = anim.GetBoneIndex("slot_muzzle");

			s_MuzzleEnt = weaponEnt;
			s_MuzzleBone = index;
		}

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

		float now = WorldTime();
		if (body == s_SeeViewer && enemy == s_SeeTarget && now == s_SeeAt)
			return s_SeeResult;

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
		bool rayClear = SightClear(world, body, eye, aim);
		if (!rayClear)
			rayClear = SightClear(world, body, eye, enemy.GetOrigin() + Vector(0, 0.35, 0));
		s_SightTarget = null;

		bool seen = rayClear;
		if (seen)
			seen = ConditionsAllow(body, enemy);

		s_SeeViewer = body;
		s_SeeTarget = enemy;
		s_SeeAt = now;
		s_SeeResult = seen;
		s_SeeConditionBlock = rayClear && !seen;
		return seen;
	}

	// The ray is clear and he still does not count as seen. A blocked
	// ray is not this. Callers that only ask SeesTarget cannot tell them apart.
	static bool BlockedByConditions(IEntity soldier, BaseTarget target)
	{
		IEntity body = CharacterBody(soldier);
		if (!target || CanSeeTarget(body, target))
			return false;

		IEntity enemy = target.GetTargetEntity();
		return enemy &&
			s_SeeViewer == body &&
			s_SeeTarget == enemy &&
			s_SeeConditionBlock;
	}

	// The ray already succeeded. This asks whether he can pick the man
	// out. Full dark still shows a standing man across a room. Farther
	// than that needs light on him, a flashlight, or night vision.
	// Concealment shortens that reach. A body at arm's length is seen.
	protected static bool ConditionsAllow(IEntity viewer, IEntity enemy)
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

		float distance = 0;
		if (viewer)
			distance = vector.Distance(viewer.GetOrigin(), enemy.GetOrigin());

		if (distance <= 2.5)
			return true;

		float light = IlluminationOf(enemy);
		bool nightVision = ViewerNightVision(viewer);
		if (nightVision)
			light = 1;

		bool lit = TargetFlashlight(enemy);
		if (!lit && ViewerFlashlight(viewer) && FlashlightCone(viewer, enemy, distance))
			lit = true;

		if (lit)
			light = Math.Max(light, 0.75);

		// Night vision still shows a man who is low or in cover. The
		// remaining concealment is bushes and a still, prone body.
		if (nightVision)
			conceal = conceal * 0.35;

		// 12 m in full dark, about 110 m in daylight. Night vision uses
		// the day reach, then keeps most of it through concealment. A
		// light lands near the day reach only while the beam is on him.
		float reach = 12 + (light * 100);
		reach = reach * (1 - conceal);
		return distance <= reach;
	}

	protected static bool ViewerNightVision(IEntity viewer)
	{
		RefreshViewerGear(viewer);
		return s_ViewerNightVision;
	}

	protected static bool ViewerFlashlight(IEntity viewer)
	{
		RefreshViewerGear(viewer);
		return s_ViewerFlashlight;
	}

	protected static bool TargetFlashlight(IEntity enemy)
	{
		float now = WorldTime();
		if (enemy == s_TargetGearBody && now == s_TargetGearAt)
			return s_TargetFlashlight;

		s_TargetGearBody = enemy;
		s_TargetGearAt = now;
		s_TargetFlashlight = false;
		if (!enemy)
			return false;

		SCR_GadgetManagerComponent manager = SCR_GadgetManagerComponent.Cast(
			enemy.FindComponent(SCR_GadgetManagerComponent)
		);
		if (manager && GearListActive(manager, EGadgetType.FLASHLIGHT))
			s_TargetFlashlight = true;

		if (!s_TargetFlashlight)
		{
			BaseWeaponComponent weapon = CurrentWeapon(enemy);
			if (weapon && WeaponLightOn(weapon.GetOwner(), 0))
				s_TargetFlashlight = true;
		}

		return s_TargetFlashlight;
	}

	protected static void RefreshViewerGear(IEntity viewer)
	{
		float now = WorldTime();
		if (viewer == s_ViewerGearBody && now == s_ViewerGearAt)
			return;

		s_ViewerGearBody = viewer;
		s_ViewerGearAt = now;
		s_ViewerNightVision = false;
		s_ViewerFlashlight = false;
		if (!viewer)
			return;

		SCR_GadgetManagerComponent manager = SCR_GadgetManagerComponent.Cast(
			viewer.FindComponent(SCR_GadgetManagerComponent)
		);
		if (manager)
		{
			s_ViewerNightVision = GearListActive(manager, EGadgetType.NIGHT_VISION);
			s_ViewerFlashlight = GearListActive(manager, EGadgetType.FLASHLIGHT);
		}

		if (s_ViewerFlashlight)
			return;

		BaseWeaponComponent weapon = CurrentWeapon(viewer);
		if (weapon && WeaponLightOn(weapon.GetOwner(), 0))
			s_ViewerFlashlight = true;
	}

	protected static bool GearListActive(
		notnull SCR_GadgetManagerComponent manager,
		EGadgetType type)
	{
		array<SCR_GadgetComponent> gadgets = manager.GetGadgetsByType(type);
		if (!gadgets)
			return false;

		foreach (SCR_GadgetComponent gadget : gadgets)
		{
			if (GearIsActive(gadget))
				return true;
		}

		return false;
	}

	// Worn or in the hand, and switched on. One in a pack does not light
	// anything, and night vision that is still down does not either.
	protected static bool GearIsActive(SCR_GadgetComponent gadget)
	{
		if (!gadget || !gadget.IsToggledOn())
			return false;

		EGadgetMode mode = gadget.GetMode();
		return mode == EGadgetMode.IN_HAND || mode == EGadgetMode.IN_SLOT;
	}

	// A light mounted on the gun is not in the gadget list. The weapon's
	// own pieces are.
	protected static bool WeaponLightOn(IEntity entity, int depth)
	{
		if (!entity || depth > 3)
			return false;

		SCR_GadgetComponent gadget = SCR_GadgetComponent.Cast(
			entity.FindComponent(SCR_GadgetComponent)
		);
		// A rail light is not in a vest slot. Switched on is enough.
		if (
			gadget &&
			gadget.IsToggledOn() &&
			gadget.GetType() == EGadgetType.FLASHLIGHT
		)
		{
			return true;
		}

		IEntity child = entity.GetChildren();
		while (child)
		{
			if (WeaponLightOn(child, depth + 1))
				return true;

			child = child.GetSibling();
		}

		return false;
	}

	// The handheld light points with him. A man beside him is not in it.
	// Past the beam, the light does not count.
	protected static bool FlashlightCone(IEntity viewer, IEntity enemy, float distance)
	{
		if (!viewer || !enemy || distance > 40)
			return false;

		vector transform[4];
		viewer.GetWorldTransform(transform);
		vector forward = transform[2];
		forward[1] = 0;
		if (forward.Length() < 0.01)
			return true;

		forward.Normalize();
		vector toEnemy = enemy.GetOrigin() - viewer.GetOrigin();
		toEnemy[1] = 0;
		if (toEnemy.Length() < 1)
			return true;

		toEnemy.Normalize();
		return vector.Dot(forward, toEnemy) >= 0.5;
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

		// Glass is already stepped through. A frame can still clip the
		// last bit of the ray. Judging that by a fraction of the whole
		// ray treated a wall a few metres in front of him as open air
		// once the shot was long.
		if (fraction >= 1)
			return true;

		float span = vector.Distance(s_SightTrace.Start, s_SightTrace.End);
		return span - (fraction * span) <= 0.4;
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
			if (fraction >= 1)
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

		return WindowCollider(s_SightTrace);
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

	// The collider comes from the ray that made the hit. A zone looked up
	// from some other ray names the wrong surface.
	protected static bool WindowCollider(TraceParam trace)
	{
		if (!trace)
			return false;

		IEntity current = trace.TraceEnt;
		int depth;
		while (current && depth < 4)
		{
			HitZoneContainerComponent container = HitZoneContainerComponent.Cast(
				current.FindComponent(HitZoneContainerComponent)
			);
			if (container && ZoneIsWindow(container, trace))
				return true;

			current = current.GetParent();
			depth++;
		}

		return false;
	}

	protected static bool ZoneIsWindow(notnull HitZoneContainerComponent container, notnull TraceParam trace)
	{
		HitZone zone = null;
		if (!trace.ColliderName.IsEmpty())
			zone = container.GetHitZone(trace.ColliderName);

		if (!zone && trace.ColliderIndex >= 0)
			zone = container.GetHitZoneByColliderID(trace.ColliderIndex);

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

	// Hold fire never shoots. Return fire shoots once the group already
	// counts itself under attack, or a round has reached this man, or the
	// man he is on is endangering him. Fire at will is unchanged.
	protected static bool DisciplineAllowsShot(IEntity body)
	{
		if (!body)
			return false;

		SCR_AIGroup group = GroupOf(body);
		if (!group)
			return true;

		SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(
			group.FindComponent(SCR_AIGroupUtilityComponent)
		);
		if (!utility)
			return true;

		EAIGroupCombatMode mode = utility.GetCombatModeExternal();
		if (mode == EAIGroupCombatMode.FIRE_AT_WILL)
			return true;

		if (mode == EAIGroupCombatMode.HOLD_FIRE)
			return false;

		if (utility.GetCombatModeActual() == EAIGroupCombatMode.FIRE_AT_WILL)
			return true;

		if (s_HitByAt.Contains(body) && WorldTime() - s_HitByAt.Get(body) <= HeardShotMs())
			return true;

		SCR_AIUtilityComponent soldier = UtilityOf(body);
		if (soldier && soldier.m_CombatComponent && TargetEndangers(soldier.m_CombatComponent.GetCurrentTarget()))
			return true;

		return false;
	}

	protected static bool TargetEndangers(BaseTarget target)
	{
		if (!target)
			return false;

		if (target.IsEndangering())
			return true;

		float since = target.GetTimeSinceEndangered();
		return since > 0 && since < THREAT_CONTACT_S;
	}

	protected static void SetFireWanted(IEntity body, bool wanted)
	{
		if (!body)
			return;

		if (wanted && !DisciplineAllowsShot(body))
			wanted = false;

		if (wanted)
		{
			s_TriggerHeld.Insert(body);
			// The burst already left. Keep him in the pump so the next one
			// starts, but do not put the button back down early.
			if (EngageBurstCooling(body))
				WriteFireWanted(body, false);
			else
				WriteFireWanted(body, true);

			EnsureTriggerPump();
			return;
		}

		s_TriggerHeld.RemoveItem(body);
		s_TriggerRounds.Remove(body);
		s_TriggerRoundAt.Remove(body);
		s_TriggerRearm.RemoveItem(body);
		s_RateAim.Remove(body);
		s_AutoBurstCap.Remove(body);
		s_AutoBurstFrom.Remove(body);
		s_AutoBurstGap.Remove(body);
		s_AutoBurstResume.Remove(body);
		WriteFireWanted(body, false);
	}

	// A new pull is in progress. The shot check must not press over it.
	protected static void PressFire(IEntity body)
	{
		if (!body || s_TriggerRearm.Contains(body))
			return;

		SetFireWanted(body, true);
	}

	// One cycle per trigger pull. Full auto is not one of these.
	protected static bool NeedsNewPull(IEntity body)
	{
		BaseWeaponComponent weapon = CurrentWeapon(body);
		if (!weapon)
			return false;

		EWeaponFiremodeType mode = weapon.GetCurrentFireModeType();
		return mode == EWeaponFiremodeType.Semiauto || mode == EWeaponFiremodeType.Burst;
	}

	// Single shots at long range wait longer between pulls. The same
	// steps apply once a clear or a garrison has already decided to fire.
	protected static float TriggerRepullMs(IEntity body)
	{
		if (!body || !s_RateAim.Contains(body))
			return TRIGGER_REPULL_MS;

		float distance = vector.Distance(body.GetOrigin(), s_RateAim.Get(body));
		BaseWeaponComponent weapon = CurrentWeapon(body);
		if (weapon && MachineGun(weapon))
			return 450;

		if (distance >= ENGAGE_BURST_M)
			return 800;

		if (distance >= ENGAGE_AUTO_M)
			return 500;

		return TRIGGER_REPULL_MS;
	}

	// Puts the selector on a sane mode for this distance, and tells the
	// pump how many automatic rounds to let out before it lifts the button.
	protected static void ApplyEngageDiscipline(IEntity body, vector point)
	{
		if (!body || point == vector.Zero)
			return;

		BaseWeaponComponent weapon = CurrentWeapon(body);
		if (!weapon || IsThrowableType(weapon.GetWeaponType()))
			return;

		BaseMuzzleComponent muzzle = weapon.GetCurrentMuzzle();
		if (!muzzle)
			return;

		float distance = vector.Distance(AimLineOrigin(body, point), point);
		s_RateAim.Set(body, point);
		EWeaponType type = weapon.GetWeaponType();
		bool belt = MachineGun(weapon);
		int want = PreferredEngageMode(muzzle, type, belt, distance);
		if (want >= 0)
			RequestFireMode(body, muzzle, want);

		EWeaponFiremodeType mode = weapon.GetCurrentFireModeType();
		NoteEngageBurst(
			body,
			EngageAutoCap(type, belt, mode, distance),
			EngageBurstGap(type, belt, distance)
		);
	}

	// Weapon Type on the weapon component. That field is how the prefab
	// tells the AI what the gun is. A rifle tagged as a rifle stays a rifle.
	protected static bool MachineGun(BaseWeaponComponent weapon)
	{
		if (!weapon)
			return false;

		EWeaponType type = weapon.GetWeaponType();
		return type == EWeaponType.WT_MACHINEGUN || type == EWeaponType.WT_AUTOCANNON;
	}

	protected static int PreferredEngageMode(
		BaseMuzzleComponent muzzle,
		EWeaponType type,
		bool belt,
		float distance)
	{
		if (type == EWeaponType.WT_ROCKETLAUNCHER || type == EWeaponType.WT_GRENADELAUNCHER)
			return -1;

		// Machine guns only have automatic on most guns. Leave them there
		// and shorten the trigger hold instead.
		if (belt || type == EWeaponType.WT_MACHINEGUN || type == EWeaponType.WT_AUTOCANNON)
		{
			return FirstMode(
				muzzle,
				EWeaponFiremodeType.Auto,
				EWeaponFiremodeType.Burst,
				EWeaponFiremodeType.Semiauto
			);
		}

		if (type == EWeaponType.WT_SNIPERRIFLE || distance >= ENGAGE_BURST_M)
		{
			return FirstMode(
				muzzle,
				EWeaponFiremodeType.Semiauto,
				EWeaponFiremodeType.Burst,
				EWeaponFiremodeType.Auto
			);
		}

		if (distance >= ENGAGE_AUTO_M)
		{
			return FirstMode(
				muzzle,
				EWeaponFiremodeType.Burst,
				EWeaponFiremodeType.Auto,
				EWeaponFiremodeType.Semiauto
			);
		}

		return FirstMode(
			muzzle,
			EWeaponFiremodeType.Auto,
			EWeaponFiremodeType.Burst,
			EWeaponFiremodeType.Semiauto
		);
	}

	protected static int FirstMode(
		BaseMuzzleComponent muzzle,
		EWeaponFiremodeType first,
		EWeaponFiremodeType second,
		EWeaponFiremodeType third)
	{
		int index = FireModeOf(muzzle, first);
		if (index >= 0)
			return index;

		index = FireModeOf(muzzle, second);
		if (index >= 0)
			return index;

		return FireModeOf(muzzle, third);
	}

	protected static int FireModeOf(BaseMuzzleComponent muzzle, EWeaponFiremodeType want)
	{
		if (!muzzle)
			return -1;

		int count = muzzle.GetFireModesCount();
		for (int i = 0; i < count; i++)
		{
			BaseFireMode mode = muzzle.GetFireMode(i);
			if (!mode)
				continue;

			if (mode.GetFiremodeType() == want)
				return i;
		}

		return -1;
	}

	// The selector animation restarts if this is sent again while it plays.
	protected static void RequestFireMode(IEntity body, BaseMuzzleComponent muzzle, int want)
	{
		if (!body || !muzzle || want < 0 || muzzle.GetFireModeIndex() == want)
			return;

		CharacterControllerComponent controller = Controller(body);
		if (!controller || controller.IsChangingFireMode())
			return;

		float now = WorldTime();
		if (s_FireModeTryAt.Contains(body) && now - s_FireModeTryAt.Get(body) < 700)
			return;

		s_FireModeTryAt.Set(body, now);
		controller.SetFireMode(want);
	}

	// Zero means the pump may hold automatic. A rifle inside close range
	// uses that. Farther, and every machine gun, gets a round limit.
	protected static int EngageAutoCap(
		EWeaponType type,
		bool belt,
		EWeaponFiremodeType mode,
		float distance)
	{
		if (mode != EWeaponFiremodeType.Auto)
			return 0;

		if (type == EWeaponType.WT_ROCKETLAUNCHER || type == EWeaponType.WT_GRENADELAUNCHER)
			return 1;

		if (type == EWeaponType.WT_SNIPERRIFLE)
			return 1;

		if (belt || type == EWeaponType.WT_MACHINEGUN || type == EWeaponType.WT_AUTOCANNON)
		{
			if (distance < MG_BURST_NEAR_M)
				return 8;

			if (distance < MG_BURST_FAR_M)
				return 5;

			return 3;
		}

		if (distance < ENGAGE_AUTO_M)
			return 0;

		if (distance < ENGAGE_BURST_M)
			return 3;

		return 2;
	}

	protected static float EngageBurstGap(EWeaponType type, bool belt, float distance)
	{
		bool mg = belt ||
			type == EWeaponType.WT_MACHINEGUN ||
			type == EWeaponType.WT_AUTOCANNON;
		if (mg)
		{
			if (distance < MG_BURST_NEAR_M)
				return 350;

			if (distance < MG_BURST_FAR_M)
				return 500;

			return 700;
		}

		if (distance >= ENGAGE_BURST_M)
			return 700;

		return 420;
	}

	protected static void NoteEngageBurst(IEntity body, int cap, float gap)
	{
		if (!body)
			return;

		// Close automatic is a held trigger. A pause left over from a
		// longer shot would keep cutting it.
		if (cap <= 0)
		{
			s_AutoBurstCap.Remove(body);
			s_AutoBurstFrom.Remove(body);
			s_AutoBurstGap.Remove(body);
			s_AutoBurstResume.Remove(body);
			return;
		}

		s_AutoBurstCap.Set(body, cap);
		s_AutoBurstGap.Set(body, gap);
		if (!s_AutoBurstFrom.Contains(body) && !s_AutoBurstResume.Contains(body))
			s_AutoBurstFrom.Set(body, LoadedRounds(CurrentWeapon(body)));
	}

	protected static bool EngageBurstCooling(IEntity body)
	{
		if (!body || !s_AutoBurstResume.Contains(body))
			return false;

		if (WorldTime() < s_AutoBurstResume.Get(body))
			return true;

		s_AutoBurstResume.Remove(body);
		s_AutoBurstFrom.Remove(body);
		return false;
	}

	protected static bool EngageBurstSpent(IEntity body)
	{
		if (!body || !s_AutoBurstCap.Contains(body))
			return false;

		int cap = s_AutoBurstCap.Get(body);
		if (cap <= 0)
			return false;

		int rounds = LoadedRounds(CurrentWeapon(body));
		if (!s_AutoBurstFrom.Contains(body))
		{
			s_AutoBurstFrom.Set(body, rounds);
			return false;
		}

		int from = s_AutoBurstFrom.Get(body);
		if (rounds > from)
		{
			s_AutoBurstFrom.Set(body, rounds);
			return false;
		}

		return from - rounds >= cap;
	}

	protected static void StartEngageBurstCool(IEntity body)
	{
		if (!body)
			return;

		float gap = 420;
		if (s_AutoBurstGap.Contains(body))
			gap = s_AutoBurstGap.Get(body);

		s_AutoBurstResume.Set(body, WorldTime() + gap);
		s_AutoBurstFrom.Remove(body);
	}

	// One ray down the shot, on the projectile layer. Brush that never
	// stops a round is not on that layer, so a man behind it is still
	// the hit. The target himself is not a block. A wall, a pane, or a
	// shell ends the ray. A man he can see is checked only for a friendly
	// in the first stretch. A man he cannot see is a guess: anything on
	// that ray short of the aim point holds the shot. A bullet from that
	// man that hit him, struck within the impact radius, or reaches him
	// on one of five rays from the muzzle still shoots.
	protected static bool FriendlyInEngageLane(IEntity body, IEntity enemy, vector aim)
	{
		if (!body || aim == vector.Zero)
			return false;

		vector from = AimLineOrigin(body, aim);
		vector span = aim - from;
		float length = span.Length();
		if (length < 0.4)
			return false;

		bool unseen = !EngageSees(body, enemy);
		vector to = aim;
		if (!unseen && length > LANE_NEAR_M)
			to = from + (span * (LANE_NEAR_M / length));

		LaneFraction(body, enemy, from, to);
		if (s_LaneHitFriendly)
			return true;

		if (!unseen)
			return false;

		if (!ProjectileBlocks(body, enemy, from, aim))
			return false;

		IEntity shooter = enemy;
		if (!shooter && s_HeardShooter.Contains(body))
			shooter = s_HeardShooter.Get(body);

		if (shooter && AttackEngageReturnHit() && ShooterHitSquad(shooter, body, THREAT_LANDED, true, false))
		{
			LogEngage(body, "return");
			return false;
		}

		if (shooter && AttackEngageHitMate() && ShooterHitSquad(shooter, body, THREAT_LANDED, false, true))
		{
			LogEngage(body, "return");
			return false;
		}

		if (shooter && AttackEngageShotImpact() && ShooterHitSquad(shooter, body, THREAT_IMPACT, true, true))
		{
			LogEngage(body, "return");
			return false;
		}

		if (shooter && AttackEngageHeardPath() && HeardPathOpen(body, shooter))
		{
			NoteBulletThreat(body, shooter, THREAT_PATH);
			LogEngage(body, "return");
			return false;
		}

		return true;
	}

	protected static string EngageLaneNote()
	{
		if (s_LaneHitFriendly)
			return "friendly";

		return "solid";
	}

	// One projectile ray. A friendly character on that line sets the lane
	// flag. The layer itself ignores brush. Glass and thin sheet stay
	// where the ray hits them.
	protected static float LaneFraction(IEntity body, IEntity ignore, vector from, vector aim)
	{
		s_LaneHitFriendly = false;
		s_LaneHitSolid = false;
		BaseWorld world = null;
		if (GetGame())
			world = GetGame().GetWorld();

		if (!world && body)
			world = body.GetWorld();

		if (!world)
			return 0;

		s_LaneViewer = body;
		s_LaneTarget = ignore;
		s_LaneTrace = new TraceParam();
		s_LaneTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		s_LaneTrace.LayerMask = EPhysicsLayerPresets.Projectile;
		s_LaneTrace.Exclude = body;
		s_LaneTrace.Start = from;
		s_LaneTrace.End = aim;
		float fraction = world.TraceMove(s_LaneTrace, FilterEngageLane);
		s_LaneViewer = null;
		s_LaneTarget = null;
		return fraction;
	}

	// Projectile layer only. A hit here is something a bullet can strike.
	// Reaching the man means the round gets there. A shell, a pane, or a
	// wall short of him holds the shot. The five rays from his muzzle are
	// what can still open it.
	protected static bool ProjectileBlocks(IEntity body, IEntity ignore, vector from, vector aim)
	{
		s_LaneHitSolid = false;
		BaseWorld world = null;
		if (GetGame())
			world = GetGame().GetWorld();

		if (!world && body)
			world = body.GetWorld();

		if (!world)
			return false;

		s_LaneViewer = body;
		s_LaneTarget = ignore;
		float fraction = TraceProjectile(world, from, aim);
		bool clear = fraction >= 1 || LaneHitIsTarget();
		s_LaneViewer = null;
		s_LaneTarget = null;
		if (clear)
			return false;

		s_LaneHitSolid = true;
		return true;
	}

	protected static float TraceProjectile(BaseWorld world, vector start, vector end)
	{
		s_LaneTrace = new TraceParam();
		s_LaneTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		s_LaneTrace.LayerMask = EPhysicsLayerPresets.Projectile;
		s_LaneTrace.Exclude = s_LaneViewer;
		s_LaneTrace.Start = start;
		s_LaneTrace.End = end;
		return world.TraceMove(s_LaneTrace, FilterProjectileBlock);
	}

	protected static bool LaneHitIsTarget()
	{
		if (!s_LaneTrace || !s_LaneTarget)
			return false;

		IEntity current = s_LaneTrace.TraceEnt;
		int depth;
		while (current && depth < 8)
		{
			if (SameCharacter(current, s_LaneTarget))
				return true;

			current = current.GetParent();
			depth++;
		}

		return false;
	}

	protected static bool FilterProjectileBlock(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		if (!entity || entity == s_LaneViewer)
			return entity == null;

		return true;
	}

	protected static bool FilterEngageLane(
		IEntity entity,
		vector start = "0 0 0",
		vector dir = "0 0 0")
	{
		if (entity == s_LaneViewer)
			return false;

		// The landscape has no entity. It still stops the ray.
		if (!entity)
			return true;

		IEntity current = entity;
		int depth;
		while (current && depth < 8)
		{
			if (current == s_LaneViewer)
				return false;

			// The man he is shooting ends the lane. Stopping here keeps a
			// friendly behind that man from cancelling the shot.
			if (SameCharacter(current, s_LaneTarget))
				return true;

			if (ChimeraCharacter.Cast(current))
			{
				if (!IsHostile(s_LaneViewer, current))
					s_LaneHitFriendly = true;

				return true;
			}

			current = current.GetParent();
			depth++;
		}

		return true;
	}

	protected static void WriteFireWanted(IEntity body, bool wanted)
	{
		CharacterControllerComponent controller = Controller(body);
		if (controller)
			controller.SetFireWeaponWanted(wanted);
	}

	// The weapon spaces its own rounds. This only puts the trigger back
	// after something else cleared it, until the next shot check lets go.
	protected static void EnsureTriggerPump()
	{
		if (s_bTriggerPumping || !GetGame())
			return;

		s_bTriggerPumping = true;
		GetGame().GetCallqueue().CallLater(TriggerPump, 0, false);
	}

	static void TriggerPump()
	{
		if (!KK_PerfLog.On())
		{
			TriggerPumpRun();
			return;
		}

		int started = System.GetTickCount();
		TriggerPumpRun();
		KK_PerfLog.End("CQB.TriggerPump", started);
	}

	protected static void TriggerPumpRun()
	{
		if (!GetGame() || s_TriggerHeld.Count() == 0)
		{
			s_bTriggerPumping = false;
			return;
		}

		s_TriggerBodies.Clear();
		foreach (IEntity held : s_TriggerHeld)
			s_TriggerBodies.Insert(held);

		foreach (IEntity body : s_TriggerBodies)
		{
			if (!body || !s_TriggerHeld.Contains(body))
			{
				s_TriggerHeld.RemoveItem(body);
				continue;
			}

			if (TriggerShouldDrop(body))
			{
				if (IsAttackSearch(body))
					LogFire(body, "drop");
				s_TriggerHeld.RemoveItem(body);
				s_RateAim.Remove(body);
				s_AutoBurstCap.Remove(body);
				s_AutoBurstFrom.Remove(body);
				s_AutoBurstGap.Remove(body);
				s_AutoBurstResume.Remove(body);
				WriteFireWanted(body, false);
				continue;
			}

			CharacterControllerComponent firing = Controller(body);
			if (firing && firing.IsChangingFireMode())
			{
				WriteFireWanted(body, false);
				continue;
			}

			// Attack engage will not press until the rifle can accept it.
			// A room shot already chose this moment. The pump only spaces
			// the rounds from here.
			if (IsAttackSearch(body))
			{
				MakeWeaponReady(body);
				if (!WeaponCanAcceptFire(body))
				{
					WriteFireWanted(body, false);
					continue;
				}
			}

			// Full auto keeps the button down. Attack engage caps that hold.
			// Semi and burst need a new pull once the last cycle has had
			// time to leave a round.
			if (!NeedsNewPull(body))
			{
				if (EngageBurstCooling(body))
				{
					WriteFireWanted(body, false);
					continue;
				}

				if (EngageBurstSpent(body))
				{
					StartEngageBurstCool(body);
					WriteFireWanted(body, false);
					continue;
				}

				WriteFireWanted(body, true);
				continue;
			}

			int rounds = LoadedRounds(CurrentWeapon(body));
			float now = WorldTime();
			if (s_TriggerRearm.Contains(body))
			{
				float released = 0;
				if (s_TriggerRoundAt.Contains(body))
					released = s_TriggerRoundAt.Get(body);
				if (now - released < TRIGGER_RELEASE_MS)
				{
					WriteFireWanted(body, false);
					continue;
				}

				s_TriggerRearm.RemoveItem(body);
				s_TriggerRounds.Set(body, rounds);
				s_TriggerRoundAt.Set(body, now);
				WriteFireWanted(body, true);
				continue;
			}

			if (!s_TriggerRounds.Contains(body) || s_TriggerRounds.Get(body) != rounds)
			{
				s_TriggerRounds.Set(body, rounds);
				s_TriggerRoundAt.Set(body, now);
				WriteFireWanted(body, true);
				continue;
			}

			if (now - s_TriggerRoundAt.Get(body) >= TriggerRepullMs(body))
			{
				s_TriggerRearm.Insert(body);
				s_TriggerRoundAt.Set(body, now);
				LogFire(body, "repull");
				WriteFireWanted(body, false);
				continue;
			}

			WriteFireWanted(body, true);
		}

		if (s_TriggerHeld.Count() == 0 || !GetGame())
		{
			s_bTriggerPumping = false;
			return;
		}

		GetGame().GetCallqueue().CallLater(TriggerPump, 0, false);
	}

	protected static bool TriggerShouldDrop(IEntity body)
	{
		CharacterControllerComponent controller = Controller(body);
		if (!controller || controller.IsDead() || IsDown(body))
			return true;

		// A finished engage reload still reports the magazine as an item
		// change. The rifle is already up. Dropping the trigger here is
		// the silent gun after the reload.
		if (controller.IsChangingItem() && !ReloadAimSettling(body))
			return true;

		return GunIsDry(body) ||
			IsEmptyReload(body) ||
			SprintBeforeReload(body) ||
			s_ReloadBash.Contains(body) ||
			HoldingThrowable(body);
	}

	protected static CharacterControllerComponent Controller(IEntity body)
	{
		if (!body)
			return null;

		if (
			body == s_ControllerBody &&
			s_ControllerCache &&
			s_ControllerCache.GetOwner() == body
		)
			return s_ControllerCache;

		s_ControllerBody = body;
		s_ControllerCache = CharacterControllerComponent.Cast(
			body.FindComponent(CharacterControllerComponent)
		);
		return s_ControllerCache;
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
		if (!KK_PerfLog.On())
			return KK_AttackEvaluate();

		int started = System.GetTickCount();
		float score = KK_AttackEvaluate();
		KK_PerfLog.End("CQB.AttackEval", started);
		return score;
	}

	protected float KK_AttackEvaluate()
	{
		IEntity character;
		IEntity agentEntity;
		if (m_Utility)
		{
			character = m_Utility.m_OwnerEntity;
			agentEntity = m_Utility.GetOwner();
		}

		// A man on the ground does not take the attack. CQB is paused,
		// and this behavior would aim and walk him anyway. Anyone else
		// who is down keeps the normal attack.
		if (
			(KK_GarrisonHold.IsDown(character) || KK_GarrisonHold.IsDown(agentEntity)) &&
			(KK_GarrisonHold.OnCqbOrder(character) || KK_GarrisonHold.OnCqbOrder(agentEntity))
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

		// Out of ammo on an attack order. Leaving the behavior lets the
		// magazine change this order just started finish.
		if (
			(
				KK_GarrisonHold.IsAttackSearch(character) &&
				(
					KK_GarrisonHold.EngageReloadRunning(character) ||
					KK_GarrisonHold.EngageSwapping(character)
				)
			) ||
			(
				KK_GarrisonHold.IsAttackSearch(agentEntity) &&
				(
					KK_GarrisonHold.EngageReloadRunning(agentEntity) ||
					KK_GarrisonHold.EngageSwapping(agentEntity)
				)
			)
		)
		{
			m_bUseCombatMove = false;
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

		// Attack-selected outranks the advance step for as long as the man
		// stays selected. That is the whole contact, not the plant. Between
		// plants the step is the move. The plant is the only window where
		// this behavior takes him.
		bool engageDriving =
			KK_GarrisonHold.AttackEngageOwnsLook(character) ||
			KK_GarrisonHold.AttackEngageOwnsLook(agentEntity);
		bool planted =
			KK_GarrisonHold.IsEngageStopped(character) ||
			KK_GarrisonHold.IsEngageStopped(agentEntity);
		if (engageDriving && !planted)
		{
			m_bUseCombatMove = false;
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
		bool engageStop =
			KK_GarrisonHold.IsEngageStopped(character) ||
			KK_GarrisonHold.IsEngageStopped(agentEntity);
		bool doorFiring =
			KK_GarrisonHold.IsDoorFiring(character) ||
			KK_GarrisonHold.IsDoorFiring(agentEntity);

		vector approachGoal;
		bool steering =
			KK_GarrisonHold.GetApproachGoal(character, approachGoal) ||
			KK_GarrisonHold.GetApproachGoal(agentEntity, approachGoal);

		if (clearingInside || pinned || engageStop || doorFiring || distantFight)
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
		// The attack shot does not take the vanilla aim wait. Sharp combat
		// does the same for a house order.
		if (
			!distantFight &&
			(
				attackSearch ||
				(
					KK_PerceptionBoost.UseSharpCombat() &&
					utility &&
					KK_PerceptionBoost.IsActiveSoldier(utility.m_OwnerEntity)
				)
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
		if (!KK_PerfLog.On())
			return KK_SimulateCombatMove(owner, dt);

		int started = System.GetTickCount();
		ENodeResult result = KK_SimulateCombatMove(owner, dt);
		KK_PerfLog.End("CQB.CombatMove", started);
		return result;
	}

	protected ENodeResult KK_SimulateCombatMove(AIAgent owner, float dt)
	{
		IEntity body;
		if (owner)
			body = owner.GetControlledEntity();

		if (KK_GarrisonHold.HealingNow(body) || KK_GarrisonHold.HealingNow(owner))
		{
			if (m_State && m_State.IsExecutingRequest())
				m_State.CancelRequest();

			IEntity healer = body;
			if (!healer)
				healer = owner;
			KK_GarrisonHold.HoldForHeal(healer);
			return ENodeResult.RUNNING;
		}

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

		if (KK_GarrisonHold.IsPinned(body) || KK_GarrisonHold.IsEngageStopped(body))
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

// Vanilla self heal, medic, and waiting on a medic. A clear that is
// still inside the room keeps them off. Once our treatment is running
// they stay off too, or the gesture and the item apply fight.
modded class SCR_AIHealBehavior
{
	override float CustomEvaluate()
	{
		if (KK_GarrisonHold.ClearHealSuppressed(m_Utility))
			return 0;

		float score = super.CustomEvaluate();
		if (KK_GarrisonHold.ClearHealBoost(m_Utility))
			return Math.Max(score, SCR_AIActionBase.PRIORITY_BEHAVIOR_HEAL_HIGH_PRIORITY);

		return score;
	}
}

modded class SCR_AIHealWaitBehavior
{
	override float CustomEvaluate()
	{
		if (KK_GarrisonHold.ClearHealSuppressed(m_Utility))
			return 0;

		float score = super.CustomEvaluate();
		if (KK_GarrisonHold.ClearHealBoost(m_Utility))
			return Math.Max(score, SCR_AIActionBase.PRIORITY_BEHAVIOR_HEAL_HIGH_PRIORITY);

		return score;
	}
}

modded class SCR_AIMedicHealBehavior
{
	override float CustomEvaluate()
	{
		if (KK_GarrisonHold.ClearHealSuppressed(m_Utility))
			return 0;

		float score = super.CustomEvaluate();
		if (KK_GarrisonHold.ClearHealBoost(m_Utility))
			return Math.Max(score, SCR_AIActionBase.PRIORITY_BEHAVIOR_HEAL_HIGH_PRIORITY);

		return score;
	}
}

// Idle observe pulls binoculars. That stays off except on a garrison post.
modded class SCR_AITakeGadgetInLeftHand
{
	protected IEntity m_KKSoldier;

	override void OnInit(AIAgent owner)
	{
		super.OnInit(owner);

		m_KKSoldier = null;
		if (owner)
			m_KKSoldier = owner.GetControlledEntity();
	}

	override IEntity FindItem()
	{
		if (
			m_eGadgetType == EGadgetType.BINOCULARS &&
			KK_GarrisonHold.OnCqbOrder(m_KKSoldier) &&
			!KK_GarrisonHold.AllowsBinoculars(m_KKSoldier)
		)
		{
			return null;
		}

		return super.FindItem();
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
		if (!KK_PerfLog.On())
		{
			KK_UpdateThreat(utility, timeSlice);
			return;
		}

		int started = System.GetTickCount();
		KK_UpdateThreat(utility, timeSlice);
		KK_PerfLog.End("CQB.Threat", started);
	}

	protected void KK_UpdateThreat(SCR_AIUtilityComponent utility, float timeSlice)
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

// A shot from a bush often never fills the detect meter: no aim point is
// exposed, so perception leaves him unknown. The danger event already
// has the shooter. Attack engage uses that instead of waiting for the
// category to change.
modded class SCR_AIDangerReaction_WeaponFired
{
	override bool PerformReaction(
		notnull SCR_AIUtilityComponent utility,
		notnull SCR_AIThreatSystem threatSystem,
		AIDangerEvent dangerEvent,
		int dangerEventCount)
	{
		// Note before the vanilla reaction. It returns without telling
		// anyone when the shooter has not been seen, so a bush hides the shot.
		AIDangerEventWeaponFire shot = AIDangerEventWeaponFire.Cast(dangerEvent);
		if (shot)
		{
			IEntity shooter = shot.GetInstigatorEntity();
			if (!shooter)
				shooter = shot.GetObject();

			KK_GarrisonHold.NoteAttackGunshot(utility.m_OwnerEntity, shooter, shot.GetPosition(), shot.GetDirection());
			KK_GarrisonHold.NoteAttackGunshot(utility.GetOwner(), shooter, shot.GetPosition(), shot.GetDirection());
		}

		return super.PerformReaction(
			utility,
			threatSystem,
			dangerEvent,
			dangerEventCount
		);
	}
}

// A strike within 10 m. The threat call only carries a count, so the
// position is read here, before that call.
modded class SCR_AIDangerReaction_ProjectileHit
{
	override bool PerformReaction(
		notnull SCR_AIUtilityComponent utility,
		notnull SCR_AIThreatSystem threatSystem,
		AIDangerEvent dangerEvent,
		int dangerEventCount)
	{
		KK_GarrisonHold.NoteNearbyImpact(utility.m_OwnerEntity, dangerEvent);
		KK_GarrisonHold.NoteNearbyImpact(utility.GetOwner(), dangerEvent);

		return super.PerformReaction(
			utility,
			threatSystem,
			dangerEvent,
			dangerEventCount
		);
	}
}

modded class SCR_AIUtilityComponent
{
	override SCR_AIBehaviorBase EvaluateBehavior(BaseTarget unknownTarget)
	{
		if (!KK_PerfLog.On())
			return KK_SelectHouseBehavior(unknownTarget);

		int started = System.GetTickCount();
		SCR_AIBehaviorBase result = KK_SelectHouseBehavior(unknownTarget);
		KK_PerfLog.End("CQB.Behavior", started);
		return result;
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
			(
				KK_GarrisonHold.IsDown(m_OwnerEntity) ||
				KK_GarrisonHold.IsDown(GetOwner())
			) &&
			(
				KK_GarrisonHold.OnCqbOrder(m_OwnerEntity) ||
				KK_GarrisonHold.OnCqbOrder(GetOwner())
			);

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

		IEntity healing = m_OwnerEntity;
		if (!healing)
			healing = GetOwner();
		if (KK_GarrisonHold.YieldsWeaponForHeal(healing))
		{
			KK_GarrisonHold.LeaveWeaponForHeal(healing);
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
		if (!KK_PerfLog.On())
			return KK_SimulateWeaponRaised(owner, dt);

		int started = System.GetTickCount();
		ENodeResult result = KK_SimulateWeaponRaised(owner, dt);
		KK_PerfLog.End("CQB.WeaponRaised", started);
		return result;
	}

	protected ENodeResult KK_SimulateWeaponRaised(AIAgent owner, float dt)
	{
		IEntity body;
		if (owner)
			body = owner.GetControlledEntity();

		// A raise or a lower during a climb restarts the pose and he
		// drops back on the side he left. A high wall stays up long
		// enough for this node to fire again.
		if (owner && KK_AgentMove.CrossingObstacle(owner))
			return ENodeResult.SUCCESS;

		// Attack engage owns the rifle. The rest of this node, and the
		// vanilla move behavior under it, lower a man who is between shots.
		if (
			KK_GarrisonHold.EngageHoldWeapon(body) ||
			KK_GarrisonHold.EngageHoldWeapon(owner)
		)
			return ENodeResult.SUCCESS;

		// A dry attack rifle stays down. The vanilla raise was putting it
		// back up and the magazine change never started.
		IEntity reloading = body;
		if (!reloading)
			reloading = owner;
		if (
			reloading &&
			KK_GarrisonHold.IsAttackSearch(reloading) &&
			KK_GarrisonHold.CannotShoot(reloading)
		)
		{
			CharacterControllerComponent reloader =
				CharacterControllerComponent.Cast(
					reloading.FindComponent(CharacterControllerComponent)
				);
			if (reloader && (reloader.IsWeaponRaised() || reloader.IsWeaponADS()))
			{
				reloader.SetWeaponADS(false);
				reloader.SetWeaponRaised(false);
			}

			return ENodeResult.SUCCESS;
		}

		if (
			(KK_GarrisonHold.IsDown(body) || KK_GarrisonHold.IsDown(owner)) &&
			(KK_GarrisonHold.OnCqbOrder(body) || KK_GarrisonHold.OnCqbOrder(owner))
		)
			return ENodeResult.SUCCESS;

		if (KK_GarrisonHold.YieldsWeaponForHeal(body) || KK_GarrisonHold.YieldsWeaponForHeal(owner))
		{
			IEntity healer = body;
			if (!healer)
				healer = owner;
			KK_GarrisonHold.LeaveWeaponForHeal(healer);
			return ENodeResult.SUCCESS;
		}

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

		// A bound sprint cannot keep the rifle up, unless engage is on a man.
		// The raise would cut the run. Sending the lower again restarts it.
		if (
			(KK_GarrisonHold.IsBoundSprint(body) || KK_GarrisonHold.IsBoundSprint(owner)) &&
			!KK_GarrisonHold.EngageOwnsTurn(body) &&
			!KK_GarrisonHold.EngageOwnsTurn(owner)
		)
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
		if (SnapClaimed() || SprintIgnoring() || SprintingToCover() || PathLookBlocks() || EngageLookBlocks())
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
		// The snap caller already rejected a man on the ground. Checking
		// that again here looks the controller up on every frame.
		if (pos == vector.Zero)
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
			(SprintIgnoring() || SprintingToCover() || PathLookBlocks() || EngageLookBlocks())
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

		if (!KK_GarrisonHold.IsPathLookWrite() && (PathLookBlocks() || EngageLookBlocks()))
			return;

		if (SprintIgnoring() || SprintingToCover() || BoundSprinting())
			return;

		super.LookAt(pos, priority, duration);
	}

	override void LookAt(IEntity ent, float priority, float duration = 0.8)
	{
		if (SnapClaimed() || PathLookBlocks() || EngageLookBlocks())
			return;

		if (SprintIgnoring() || SprintingToCover() || BoundSprinting())
			return;

		super.LookAt(ent, priority, duration);
	}

	override void Cancel()
	{
		if (PathLookBlocks() || EngageLookBlocks())
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
		if (!KK_PerfLog.On())
		{
			KK_MoveLookParameters(
				outCanLook,
				outLookPos,
				outLookDuration,
				outCancelLook,
				outRestartLook
			);
			return;
		}

		int started = System.GetTickCount();
		KK_MoveLookParameters(
			outCanLook,
			outLookPos,
			outLookDuration,
			outCancelLook,
			outRestartLook
		);
		KK_PerfLog.End("CQB.Look", started);
	}

	protected void KK_MoveLookParameters(
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
	// Attack engage is on a man. Another look, including the lane scan and
	// a threat glance, would restart the turn he already started.
	protected bool EngageLookBlocks()
	{
		if (!m_Utility)
			return false;

		return KK_GarrisonHold.PerceivedFireActive(m_Utility.m_OwnerEntity) ||
			KK_GarrisonHold.PerceivedFireActive(m_Utility.GetOwner());
	}

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
		if (!KK_PerfLog.On())
			return KK_EvaluateMove();

		int started = System.GetTickCount();
		float score = KK_EvaluateMove();
		KK_PerfLog.End("CQB.MoveEval", started);
		return score;
	}

	protected float KK_EvaluateMove()
	{
		if (PlantHoldsFeet())
			return 0;

		if (HealHoldsFeet())
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

	// The route move outranks a heal. While a bandage gesture is owed,
	// that move has to step aside or the run cuts the animation off.
	protected bool HealHoldsFeet()
	{
		if (!m_Utility)
			return false;

		IEntity body = m_Utility.m_OwnerEntity;
		bool healing =
			KK_GarrisonHold.HealingNow(body) ||
			KK_GarrisonHold.HealingNow(m_Utility.GetOwner());
		if (!healing)
			return false;

		if (!body)
		{
			AIAgent agent = AIAgent.Cast(m_Utility.GetOwner());
			if (agent)
				body = agent.GetControlledEntity();
		}

		KK_GarrisonHold.HoldForHeal(body);
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

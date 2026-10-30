[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBFloatEditorAttribute : SCR_BaseValueListEditorAttribute
{
	[Attribute("0", UIWidgets.EditBox)]
	protected int m_iSetting;

	override SCR_BaseEditorAttributeVar ReadVariable(
		Managed item,
		SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;

		SCR_BaseGameMode mode = GameMode();
		if (!mode)
			return null;

		return SCR_BaseEditorAttributeVar.CreateFloat(Read(mode));
	}

	override void WriteVariable(
		Managed item,
		SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager,
		int playerID)
	{
		if (!var)
			return;

		SCR_BaseGameMode mode = GameMode();
		if (!mode)
			return;

		Write(mode, var.GetFloat());
	}

	protected SCR_BaseGameMode GameMode()
	{
		return SCR_BaseGameMode.Get();
	}

	protected float Read(notnull SCR_BaseGameMode mode)
	{
		switch (m_iSetting)
		{
			case 1: return mode.KK_GetVerticalSpacing();
			case 2: return mode.KK_GetDeduplicateDistance();
			case 3: return mode.KK_GetClusterRadius();
			case 4: return mode.KK_GetClearSearchRadius();
			case 5: return mode.KK_GetClearArrivalRadius();
			case 6: return mode.KK_GetSightVisitRange();
			case 26: return mode.KK_GetSightAimHeight();
			case 20: return mode.KK_GetSightRetry();
			case 21: return mode.KK_GetDoorReach();
			case 22: return mode.KK_GetDoorSearchInterval();
			case 23: return mode.KK_GetDoorSearchDistance();
			case 7: return mode.KK_GetClearMovementTimeout();
			case 8: return mode.KK_GetClearStuckTimeout();
			case 9: return mode.KK_GetClearMaximumRetries();
			case 24: return mode.KK_GetClearDeferRetries();
			case 10: return mode.KK_GetGarrisonSearchRadius();
			case 11: return mode.KK_GetGarrisonArrivalRadius();
			case 12: return mode.KK_GetHoldRadius();
			case 28: return mode.KK_GetAuthoredHoldRadius();
			case 13: return mode.KK_GetReassignmentInterval();
			case 17: return mode.KK_GetRotateIntervalMin();
			case 18: return mode.KK_GetRotateIntervalMax();
			case 14: return mode.KK_GetGarrisonMovementTimeout();
			case 15: return mode.KK_GetGarrisonStuckTimeout();
			case 16: return mode.KK_GetGarrisonMaximumRetries();
			case 19: return mode.KK_GetPerceptionFactor();
			case 29: return mode.KK_GetShotDelay();
			case 42: return mode.KK_GetLostContactLook();
			case 30: return mode.KK_GetShotInterval();
			case 46: return mode.KK_GetShotMenPerTick();
			case 31: return mode.KK_GetReloadRemainder() * 100;
			case 32: return mode.KK_GetOutOfSight();
			case 33: return mode.KK_GetRearmReturn();
			case 27: return mode.KK_GetSampleAttempts();
			case 34: return mode.KK_GetAttackLaneOffset();
			case 35: return mode.KK_GetAttackStepLength();
			case 38: return mode.KK_GetAttackPause();
			case 40: return mode.KK_GetCoverHandoff();
			case 36: return mode.KK_GetAttackArrivalRadius();
			case 37: return mode.KK_GetTakeCoverReturn();
			case 47: return mode.KK_GetCoverRotate();
			case 48: return mode.KK_GetCoverRadius();
			case 50: return mode.KK_GetAttackEngageInitial();
			case 51: return mode.KK_GetAttackEngageInterval();
			case 52: return mode.KK_GetAttackEngageRepeat();
			case 53: return mode.KK_GetHeardShot();
			case 41: return mode.KK_GetRoomCombatRange();
			case 43: return mode.KK_GetBarrelStuck();
			case 44: return mode.KK_GetBarrelCool();
		}

		return mode.KK_GetHorizontalSpacing();
	}

	protected void Write(notnull SCR_BaseGameMode mode, float value)
	{
		switch (m_iSetting)
		{
			case 1: mode.KK_SetVerticalSpacing(value); break;
			case 2: mode.KK_SetDeduplicateDistance(value); break;
			case 3: mode.KK_SetClusterRadius(value); break;
			case 4: mode.KK_SetClearSearchRadius(value); break;
			case 5: mode.KK_SetClearArrivalRadius(value); break;
			case 6: mode.KK_SetSightVisitRange(value); break;
			case 26: mode.KK_SetSightAimHeight(value); break;
			case 20: mode.KK_SetSightRetry(value); break;
			case 21: mode.KK_SetDoorReach(value); break;
			case 22: mode.KK_SetDoorSearchInterval(value); break;
			case 23: mode.KK_SetDoorSearchDistance(value); break;
			case 7: mode.KK_SetClearMovementTimeout(value); break;
			case 8: mode.KK_SetClearStuckTimeout(value); break;
			case 9: mode.KK_SetClearMaximumRetries((int)Math.Round(value)); break;
			case 24: mode.KK_SetClearDeferRetries((int)Math.Round(value)); break;
			case 10: mode.KK_SetGarrisonSearchRadius(value); break;
			case 11: mode.KK_SetGarrisonArrivalRadius(value); break;
			case 12: mode.KK_SetHoldRadius(value); break;
			case 28: mode.KK_SetAuthoredHoldRadius(value); break;
			case 13: mode.KK_SetReassignmentInterval(value); break;
			case 17: mode.KK_SetRotateIntervalMin(value); break;
			case 18: mode.KK_SetRotateIntervalMax(value); break;
			case 14: mode.KK_SetGarrisonMovementTimeout(value); break;
			case 15: mode.KK_SetGarrisonStuckTimeout(value); break;
			case 16: mode.KK_SetGarrisonMaximumRetries((int)Math.Round(value)); break;
			case 19: mode.KK_SetPerceptionFactor(value); break;
			case 29: mode.KK_SetShotDelay(value); break;
			case 42: mode.KK_SetLostContactLook(value); break;
			case 30: mode.KK_SetShotInterval(value); break;
			case 46: mode.KK_SetShotMenPerTick(value); break;
			case 31: mode.KK_SetReloadRemainder(value); break;
			case 32: mode.KK_SetOutOfSight(value); break;
			case 33: mode.KK_SetRearmReturn(value); break;
			case 27: mode.KK_SetSampleAttempts((int)Math.Round(value)); break;
			case 34: mode.KK_SetAttackLaneOffset(value); break;
			case 35: mode.KK_SetAttackStepLength(value); break;
			case 38: mode.KK_SetAttackPause(value); break;
			case 40: mode.KK_SetCoverHandoff(value); break;
			case 36: mode.KK_SetAttackArrivalRadius(value); break;
			case 37: mode.KK_SetTakeCoverReturn(value); break;
			case 47: mode.KK_SetCoverRotate(value); break;
			case 48: mode.KK_SetCoverRadius(value); break;
			case 50: mode.KK_SetAttackEngageInitial(value); break;
			case 51: mode.KK_SetAttackEngageInterval(value); break;
			case 52: mode.KK_SetAttackEngageRepeat(value); break;
			case 53: mode.KK_SetHeardShot(value); break;
			case 41: mode.KK_SetRoomCombatRange(value); break;
			case 43: mode.KK_SetBarrelStuck(value); break;
			case 44: mode.KK_SetBarrelCool(value); break;
			default: mode.KK_SetHorizontalSpacing(value); break;
		}
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBBoolEditorAttribute : SCR_BaseEditorAttribute
{
	[Attribute("0", UIWidgets.EditBox)]
	protected int m_iSetting;

	override SCR_BaseEditorAttributeVar ReadVariable(
		Managed item,
		SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return null;

		return SCR_BaseEditorAttributeVar.CreateBool(Read(mode));
	}

	override void WriteVariable(
		Managed item,
		SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager,
		int playerID)
	{
		if (!var)
			return;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return;

		Write(mode, var.GetBool());
	}

	protected bool Read(notnull SCR_BaseGameMode mode)
	{
		switch (m_iSetting)
		{
			case 1: return mode.KK_GetClearFailCluster();
			case 2: return mode.KK_GetGarrisonFailCluster();
			case 3: return mode.KK_GetFilterUnreachableIslands();
			case 4: return mode.KK_GetDebugDraw();
			case 5: return mode.KK_GetOpenDoors();
			case 6: return mode.KK_GetSharpCombat();
			case 7: return mode.KK_GetStopWhenSeen();
			case 8: return mode.KK_GetFilterBuildingSurfaces();
			case 9: return mode.KK_GetClassifyOpenings();
			case 11: return mode.KK_GetIgnoreSquadCollision();
			case 12: return mode.KK_GetWaypointAuthoring();
			case 13: return mode.KK_GetRotateDuringCombat();
			case 14: return mode.KK_GetRoomCombat();
			case 15: return mode.KK_GetReloadCover();
			case 16: return mode.KK_GetReloadSprint();
			case 17: return mode.KK_GetDebugLog();
			case 18: return mode.KK_GetSidearmThenRelease();
			case 19: return mode.KK_GetStartFromOrder();
			case 22: return mode.KK_GetSnapAim();
			case 23: return mode.KK_GetSnapAimError();
			case 24: return mode.KK_GetShotDelaySkill();
			case 25: return mode.KK_GetSightConditions();
			case 45: return mode.KK_GetPerformanceLog();
			case 49: return mode.KK_GetAttackEngage();
			case 54: return mode.KK_GetAttackMedic();
			case 55: return mode.KK_GetAttackEngageHeardShot();
			case 56: return mode.KK_GetAttackEngageSuspected();
			case 57: return mode.KK_GetAttackEngageThreatening();
			case 58: return mode.KK_GetAttackEngageReturnHit();
			case 61: return mode.KK_GetAttackEngageHitMate();
			case 59: return mode.KK_GetAttackEngageShotImpact();
			case 60: return mode.KK_GetAttackEngageHeardPath();
		}

		return mode.KK_GetGarrisonAfterClear();
	}

	protected void Write(notnull SCR_BaseGameMode mode, bool value)
	{
		switch (m_iSetting)
		{
			case 1: mode.KK_SetClearFailCluster(value); break;
			case 2: mode.KK_SetGarrisonFailCluster(value); break;
			case 3: mode.KK_SetFilterUnreachableIslands(value); break;
			case 4: mode.KK_SetDebugDraw(value); break;
			case 5: mode.KK_SetOpenDoors(value); break;
			case 6: mode.KK_SetSharpCombat(value); break;
			case 7: mode.KK_SetStopWhenSeen(value); break;
			case 8: mode.KK_SetFilterBuildingSurfaces(value); break;
			case 9: mode.KK_SetClassifyOpenings(value); break;
			case 11: mode.KK_SetIgnoreSquadCollision(value); break;
			case 12: mode.KK_SetWaypointAuthoring(value); break;
			case 13: mode.KK_SetRotateDuringCombat(value); break;
			case 14: mode.KK_SetRoomCombat(value); break;
			case 15: mode.KK_SetReloadCover(value); break;
			case 16: mode.KK_SetReloadSprint(value); break;
			case 17: mode.KK_SetDebugLog(value); break;
			case 18: mode.KK_SetSidearmThenRelease(value); break;
			case 19: mode.KK_SetStartFromOrder(value); break;
			case 22: mode.KK_SetSnapAim(value); break;
			case 23: mode.KK_SetSnapAimError(value); break;
			case 24: mode.KK_SetShotDelaySkill(value); break;
			case 25: mode.KK_SetSightConditions(value); break;
			case 45: mode.KK_SetPerformanceLog(value); break;
			case 49: mode.KK_SetAttackEngage(value); break;
			case 54: mode.KK_SetAttackMedic(value); break;
			case 55: mode.KK_SetAttackEngageHeardShot(value); break;
			case 56: mode.KK_SetAttackEngageSuspected(value); break;
			case 57: mode.KK_SetAttackEngageThreatening(value); break;
			case 58: mode.KK_SetAttackEngageReturnHit(value); break;
			case 61: mode.KK_SetAttackEngageHitMate(value); break;
			case 59: mode.KK_SetAttackEngageShotImpact(value); break;
			case 60: mode.KK_SetAttackEngageHeardPath(value); break;
			default: mode.KK_SetGarrisonAfterClear(value); break;
		}
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrHorizontalSpacing : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrVerticalSpacing : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDeduplicateDistance : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClusterRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrFilterUnreachableIslands : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrFilterBuildingSurfaces : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonAfterClear : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrStartFromOrder : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClearSearchRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClearArrivalRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSightVisitRange : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSightAimHeight : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSightRetry : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrStopWhenSeen : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClearMovementTimeout : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClearStuckTimeout : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClearMaximumRetries : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClearDeferRetries : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClearFailCluster : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonSearchRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonArrivalRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrHoldRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAuthoredHoldRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrReassignmentInterval : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrRotateIntervalMin : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrRotateIntervalMax : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrRotateDuringCombat : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonMovementTimeout : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonStuckTimeout : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonMaximumRetries : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonFailCluster : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClassifyOpenings : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSharpCombat : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrRoomCombat : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrRoomCombatRange : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrShotDelay : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrShotDelaySkill : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSightConditions : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSnapAim : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSnapAimError : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAimMode : SCR_BaseEditorAttribute
{
	[Attribute()]
	protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

	override SCR_BaseEditorAttributeVar ReadVariable(
		Managed item,
		SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(mode.KK_GetAimMode());
	}

	override void WriteVariable(
		Managed item,
		SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager,
		int playerID)
	{
		Apply(var);
	}

	override void UpdateInterlinkedVariables(
		SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager,
		bool isInit = false)
	{
		super.UpdateInterlinkedVariables(var, manager, isInit);

		if (isInit)
			return;

		Apply(var);
	}

	protected void Apply(SCR_BaseEditorAttributeVar var)
	{
		if (!var)
			return;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return;

		int value = var.GetInt();
		float asFloat = var.GetFloat();
		if (asFloat > value)
			value = Math.Round(asFloat);

		mode.KK_SetAimMode(value);
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		int count = 5;
		if (m_aValues)
			count = m_aValues.Count();

		outEntries.Insert(new SCR_EditorAttributePresetEntry(count, false));
		outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
		return outEntries.Count();
	}
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrBarrelStuck : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrBarrelCool : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrLostContactLook : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrShotInterval : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrShotMenPerTick : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrReloadRemainder : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrOutOfSight : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrReloadCover : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrReloadSprint : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSidearmThenRelease : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrRearmReturn : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrPerceptionFactor : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrOpenDoors : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrNavImprovements : SCR_BaseEditorAttribute
{
	[Attribute()]
	protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

	override SCR_BaseEditorAttributeVar ReadVariable(
		Managed item,
		SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(mode.KK_GetNavMode());
	}

	override void WriteVariable(
		Managed item,
		SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager,
		int playerID)
	{
		Apply(var);
	}

	override void UpdateInterlinkedVariables(
		SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager,
		bool isInit = false)
	{
		super.UpdateInterlinkedVariables(var, manager, isInit);

		if (isInit)
			return;

		Apply(var);
	}

	protected void Apply(SCR_BaseEditorAttributeVar var)
	{
		if (!var)
			return;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return;

		int value = var.GetInt();
		float asFloat = var.GetFloat();
		if (asFloat > value)
			value = Math.Round(asFloat);

		mode.KK_SetNavMode(value);
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		int count = 3;
		if (m_aValues)
			count = m_aValues.Count();

		outEntries.Insert(new SCR_EditorAttributePresetEntry(count, false));
		outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
		return outEntries.Count();
	}
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrIgnoreSquadCollision : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDoorReach : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDoorSearchInterval : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDoorSearchDistance : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDebugDraw : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDebugLog : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrPerformanceLog : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrWaypointAuthoring : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSampleAttempts : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackLaneOffset : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackStepLength : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackPause : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrCoverHandoff : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrCoverRotate : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrCoverRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngage : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackMedic : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageInitial : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageInterval : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageRepeat : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageHeardShot : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageSuspected : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageThreatening : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageReturnHit : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageHitMate : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageShotImpact : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackEngageHeardPath : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrHeardShot : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrAttackArrivalRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrTakeCoverReturn : KK_CQBFloatEditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClearSpareMode : SCR_BaseEditorAttribute
{
	[Attribute()]
	protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

	override SCR_BaseEditorAttributeVar ReadVariable(
		Managed item,
		SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(mode.KK_GetClearSpareMode());
	}

	override void WriteVariable(
		Managed item,
		SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager,
		int playerID)
	{
		Apply(var);
	}

	override void UpdateInterlinkedVariables(
		SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager,
		bool isInit = false)
	{
		super.UpdateInterlinkedVariables(var, manager, isInit);

		if (isInit)
			return;

		Apply(var);
	}

	protected void Apply(SCR_BaseEditorAttributeVar var)
	{
		if (!var)
			return;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return;

		mode.KK_SetClearSpareMode(var.GetInt());
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		int count = 6;
		if (m_aValues)
			count = m_aValues.Count();

		outEntries.Insert(new SCR_EditorAttributePresetEntry(count, false));
		outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
		return outEntries.Count();
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBConfigFileAttribute : SCR_BaseEditorAttribute
{
	[Attribute()]
	protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

	override SCR_BaseEditorAttributeVar ReadVariable(
		Managed item,
		SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(-1);
	}

	override void UpdateInterlinkedVariables(
		SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager,
		bool isInit = false)
	{
		super.UpdateInterlinkedVariables(var, manager, isInit);

		if (isInit || !var)
			return;

		SCR_BaseGameMode mode = SCR_BaseGameMode.Get();
		if (!mode)
			return;

		if (var.GetInt() == 0)
		{
			ApplyOpenAttributes(manager);
			mode.KK_ExportConfig();
			return;
		}

		if (var.GetInt() == 1)
		{
			mode.KK_ImportConfig();
			RefreshOpenAttributes(manager);
			return;
		}

		if (var.GetInt() != 2)
			return;

		mode.KK_ResetConfig();
		RefreshOpenAttributes(manager);
	}

	protected void ApplyOpenAttributes(SCR_AttributesManagerEditorComponent manager)
	{
		if (!manager)
			return;

		array<Managed> items = {};
		if (manager.GetEditedItems(items) == 0)
			return;

		array<SCR_BaseEditorAttribute> attributes = {};
		manager.GetEditedAttributes(attributes);

		foreach (SCR_BaseEditorAttribute attribute : attributes)
		{
			if (!attribute || attribute == this)
				continue;

			SCR_BaseEditorAttributeVar value = attribute.GetVariable();
			if (!value)
				continue;

			attribute.WriteVariable(items[0], value, manager, 0);
		}
	}

	protected void RefreshOpenAttributes(SCR_AttributesManagerEditorComponent manager)
	{
		if (!manager)
			return;

		array<Managed> items = {};
		if (manager.GetEditedItems(items) == 0)
			return;

		array<SCR_BaseEditorAttribute> attributes = {};
		manager.GetEditedAttributes(attributes);

		foreach (SCR_BaseEditorAttribute attribute : attributes)
		{
			if (!attribute || attribute == this)
				continue;

			SCR_BaseEditorAttributeVar value = attribute.ReadVariable(items[0], manager);
			if (!value)
				continue;

			attribute.SetVariable(value);
			attribute.TelegraphChange(false);
		}
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		outEntries.Insert(new SCR_EditorAttributePresetEntry(3, false));
		outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
		return outEntries.Count();
	}
}

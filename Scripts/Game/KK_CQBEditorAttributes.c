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
			case 13: return mode.KK_GetReassignmentInterval();
			case 17: return mode.KK_GetRotateIntervalMin();
			case 18: return mode.KK_GetRotateIntervalMax();
			case 14: return mode.KK_GetGarrisonMovementTimeout();
			case 15: return mode.KK_GetGarrisonStuckTimeout();
			case 16: return mode.KK_GetGarrisonMaximumRetries();
			case 25: return mode.KK_GetGarrisonDeferRetries();
			case 19: return mode.KK_GetPerceptionFactor();
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
			case 13: mode.KK_SetReassignmentInterval(value); break;
			case 17: mode.KK_SetRotateIntervalMin(value); break;
			case 18: mode.KK_SetRotateIntervalMax(value); break;
			case 14: mode.KK_SetGarrisonMovementTimeout(value); break;
			case 15: mode.KK_SetGarrisonStuckTimeout(value); break;
			case 16: mode.KK_SetGarrisonMaximumRetries((int)Math.Round(value)); break;
			case 25: mode.KK_SetGarrisonDeferRetries((int)Math.Round(value)); break;
			case 19: mode.KK_SetPerceptionFactor(value); break;
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
class KK_CQBAttrClearSearchRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrClearArrivalRadius : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSightVisitRange : KK_CQBFloatEditorAttribute {}
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
class KK_CQBAttrReassignmentInterval : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrRotateIntervalMin : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrRotateIntervalMax : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonMovementTimeout : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonStuckTimeout : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonMaximumRetries : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonDeferRetries : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrGarrisonFailCluster : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrSharpCombat : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrPerceptionFactor : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrOpenDoors : KK_CQBBoolEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDoorReach : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDoorSearchInterval : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDoorSearchDistance : KK_CQBFloatEditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class KK_CQBAttrDebugDraw : KK_CQBBoolEditorAttribute {}

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

		if (var.GetInt() != 1)
			return;

		mode.KK_ImportConfig();
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
		outEntries.Insert(new SCR_EditorAttributePresetEntry(2, false));
		outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
		return outEntries.Count();
	}
}

[BaseContainerProps(), SCR_BaseContainerCustomTitleResourceName(
	"m_CommandPrefab",
	true
)]
class KK_GarrisonCommandAction : SCR_WaypointBaseCommandAction
{
	override void Perform(
		SCR_EditableEntityComponent hoveredEntity,
		notnull set<SCR_EditableEntityComponent> selectedEntities,
		vector cursorWorldPosition,
		int flags,
		int param = -1)
	{
		vector buildingPosition =
			KK_BuildingResolver.FindNearestBuildingPosition(
				cursorWorldPosition
			);

		super.Perform(
			hoveredEntity,
			selectedEntities,
			buildingPosition,
			flags,
			param
		);

		foreach (SCR_EditableEntityComponent entity : selectedEntities)
		{
			if (!entity)
				continue;

			KK_CQBOrders.ApplyLatestGarrison(
				entity.GetOwner(),
				buildingPosition
			);
		}
	}
}
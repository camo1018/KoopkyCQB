[BaseContainerProps(), SCR_BaseContainerCustomTitleResourceName(
	"m_CommandPrefab",
	true
)]
class KK_ClearCommandAction : SCR_WaypointBaseCommandAction
{
	override void Perform(
		SCR_EditableEntityComponent hoveredEntity,
		notnull set<SCR_EditableEntityComponent> selectedEntities,
		vector cursorWorldPosition,
		int flags,
		int param = -1)
	{
		IEntity hitEntity;
		if (hoveredEntity)
			hitEntity = hoveredEntity.GetOwner();

		BaseBuilding building;
		bool locked;
		vector buildingPosition =
			KK_BuildingResolver.ResolveOrderPosition(
				hitEntity,
				cursorWorldPosition,
				building,
				locked
			);

		foreach (SCR_EditableEntityComponent selected : selectedEntities)
		{
			if (!selected)
				continue;

			KK_CQBOrders.NoteOrderBuilding(
				selected.GetOwner(),
				building,
				locked,
				cursorWorldPosition,
				true
			);
		}

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

			KK_CQBOrders.ApplyLatestClear(
				entity.GetOwner(),
				buildingPosition,
				building,
				locked,
				cursorWorldPosition,
				true
			);
		}
	}
}
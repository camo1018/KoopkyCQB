[BaseContainerProps()]
class KK_ClearCommand : SCR_WaypointGroupCommand
{
	override bool Execute(
		IEntity cursorTarget,
		IEntity groupEnt,
		vector targetPosition,
		int playerID,
		bool isClient)
	{
		BaseBuilding building;
		bool locked;
		vector buildingPosition =
			KK_BuildingResolver.ResolveOrderPosition(
				cursorTarget,
				targetPosition,
				building,
				locked
			);

		KK_CQBOrders.NoteOrderBuilding(groupEnt, building, locked);

		bool created = super.Execute(
			cursorTarget,
			groupEnt,
			buildingPosition,
			playerID,
			isClient
		);

		if (!created)
		{
			KK_CQBOrders.ClearNotedOrder(groupEnt);
			return false;
		}

		KK_CQBOrders.ApplyLatestClear(
			groupEnt,
			buildingPosition,
			building,
			locked
		);

		return true;
	}
}
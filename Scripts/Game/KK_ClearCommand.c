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
		vector buildingPosition =
			KK_BuildingResolver.FindNearestBuildingPosition(
				targetPosition
			);

		bool created = super.Execute(
			cursorTarget,
			groupEnt,
			buildingPosition,
			playerID,
			isClient
		);

		if (created)
			KK_CQBOrders.ApplyLatestClear(groupEnt, buildingPosition);

		return created;
	}
}
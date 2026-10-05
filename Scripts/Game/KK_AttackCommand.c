[BaseContainerProps()]
class KK_AttackCommand : SCR_WaypointGroupCommand
{
	[Attribute("0", UIWidgets.ComboBox, "How the squad advances", "", ParamEnumArray.FromEnum(KK_EAttackPace))]
	protected KK_EAttackPace m_ePace;

	override bool Execute(
		IEntity cursorTarget,
		IEntity groupEnt,
		vector targetPosition,
		int playerID,
		bool isClient)
	{
		KK_EAttackPace pace = Pace();
		KK_CQBOrders.NoteAttackPace(groupEnt, pace);

		bool created = super.Execute(
			cursorTarget,
			groupEnt,
			targetPosition,
			playerID,
			isClient
		);

		if (!created)
		{
			KK_CQBOrders.ClearAttackPace(groupEnt);
			return false;
		}

		KK_CQBOrders.ApplyLatestAttack(
			groupEnt,
			targetPosition,
			pace
		);

		return true;
	}

	protected KK_EAttackPace Pace()
	{
		if (m_ePace == KK_EAttackPace.BOUND)
			return KK_EAttackPace.BOUND;

		if (m_ePace == KK_EAttackPace.TAKE_COVER)
			return KK_EAttackPace.TAKE_COVER;

		return KK_EAttackPace.ADVANCE;
	}
}

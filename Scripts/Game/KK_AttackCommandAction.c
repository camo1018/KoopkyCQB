[BaseContainerProps(), SCR_BaseContainerCustomTitleResourceName(
	"m_CommandPrefab",
	true
)]
class KK_AttackCommandAction : SCR_WaypointBaseCommandAction
{
	[Attribute("0", UIWidgets.ComboBox, "How the squad advances", "", ParamEnumArray.FromEnum(KK_EAttackPace))]
	protected KK_EAttackPace m_ePace;

	override void Perform(
		SCR_EditableEntityComponent hoveredEntity,
		notnull set<SCR_EditableEntityComponent> selectedEntities,
		vector cursorWorldPosition,
		int flags,
		int param = -1)
	{
		KK_EAttackPace pace = Pace();

		foreach (SCR_EditableEntityComponent selected : selectedEntities)
		{
			if (!selected)
				continue;

			KK_CQBOrders.NoteAttackPace(selected.GetOwner(), pace);
		}

		super.Perform(
			hoveredEntity,
			selectedEntities,
			cursorWorldPosition,
			flags,
			param
		);

		foreach (SCR_EditableEntityComponent entity : selectedEntities)
		{
			if (!entity)
				continue;

			KK_CQBOrders.ApplyLatestAttack(
				entity.GetOwner(),
				cursorWorldPosition,
				pace
			);
		}
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

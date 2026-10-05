class KK_CQBOrderButtonHandler : ScriptedWidgetComponent
{
	protected KK_CQBCommandBarComponent m_Bar;
	protected SCR_BaseEditorAction m_Action;

	void SetOrder(KK_CQBCommandBarComponent bar, SCR_BaseEditorAction action)
	{
		m_Bar = bar;
		m_Action = action;
	}

	override bool OnClick(Widget w, int x, int y, int button)
	{
		if (button != 0 || !m_Bar || !m_Action)
			return false;

		m_Bar.StartOrder(m_Action);
		return true;
	}

	override bool OnMouseEnter(Widget w, int x, int y)
	{
		if (m_Bar && m_Action)
			m_Bar.ShowOrderTooltip(m_Action);

		return false;
	}

	override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
	{
		if (m_Bar && m_Action)
			m_Bar.HideOrderTooltip(m_Action);

		return false;
	}
}

[ComponentEditorProps(
	category: "GameScripted/Editor",
	description: "Separate Game Master bar for Koopky orders."
)]
class KK_CQBCommandBarComponentClass : SCR_BaseActionsEditorComponentClass
{
}

// The vanilla command bar only shows nine orders. These stay on their own bar,
// but they are still editor actions so placing can find them.
class KK_CQBCommandBarComponent : SCR_BaseActionsEditorComponent
{
	protected static const ResourceName BAR_LAYOUT =
		"{6A7B0A1000000040}UI/layouts/Editor/KK_CQBCommandBar.layout";
	protected static const ResourceName TOOLTIP_LAYOUT =
		"{6A7B0A1000000041}UI/layouts/Editor/KK_CQBOrderTooltip.layout";

	protected static const string ACTION_GARRISON = "KK_CQBOrderGarrison";
	protected static const string ACTION_CLEAR = "KK_CQBOrderClear";
	protected static const string ACTION_ADVANCE = "KK_CQBOrderAdvance";
	protected static const string ACTION_BOUND = "KK_CQBOrderBound";
	protected static const string ACTION_TAKE_COVER = "KK_CQBOrderTakeCover";
	protected static const string ORDER_CONTEXT = "KK_CQBOrderContext";

	protected Widget m_wBar;
	protected Widget m_wTooltip;
	protected SCR_BaseEditableEntityFilter m_Selected;
	protected bool m_bHotkeysListening;
	protected bool m_bContextTicking;
	protected bool m_bVisibilityTicking;
	protected bool m_bGarrisonDown;
	protected bool m_bClearDown;
	protected bool m_bAdvanceDown;
	protected bool m_bBoundDown;
	protected bool m_bTakeCoverDown;
	protected float m_fHotkeyAt;

	override void EOnEditorActivate()
	{
		super.EOnEditorActivate();

		SCR_EntitiesManagerEditorComponent entities =
			SCR_EntitiesManagerEditorComponent.Cast(
				SCR_EntitiesManagerEditorComponent.GetInstance(
					SCR_EntitiesManagerEditorComponent,
					true
				)
			);
		if (entities)
			m_Selected = entities.GetFilter(EEditableEntityState.SELECTED);

		if (m_Selected)
			m_Selected.GetOnChanged().Insert(OnSelectionChanged);

		CreateBar();
		RefreshVisibility();
		ListenForHotkeys();
		StartContextTick();
		StartVisibilityTick();
	}

	override void EOnEditorDeactivate()
	{
		StopVisibilityTick();
		StopContextTick();
		StopHotkeys();

		if (m_Selected)
			m_Selected.GetOnChanged().Remove(OnSelectionChanged);

		m_Selected = null;

		if (m_wTooltip)
		{
			m_wTooltip.RemoveFromHierarchy();
			m_wTooltip = null;
		}

		if (m_wBar)
		{
			m_wBar.RemoveFromHierarchy();
			m_wBar = null;
		}

		super.EOnEditorDeactivate();
	}

	void StartOrder(SCR_BaseEditorAction action)
	{
		if (CoveredByAnotherMenu())
			return;

		SCR_BaseCommandAction command = SCR_BaseCommandAction.Cast(action);
		if (!command || !m_Selected)
			return;

		set<SCR_EditableEntityComponent> selected = new set<SCR_EditableEntityComponent>();
		m_Selected.GetEntities(selected);
		if (!command.StartPlacing(selected))
			return;

		SCR_CommandActionsEditorComponent commands =
			SCR_CommandActionsEditorComponent.Cast(
				SCR_CommandActionsEditorComponent.GetInstance(
					SCR_CommandActionsEditorComponent,
					true
				)
			);
		if (commands)
			commands.SetCurrentAction(action);
	}

	protected void ListenForHotkeys()
	{
		if (m_bHotkeysListening)
			return;

		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;

		input.AddActionListener(ACTION_GARRISON, EActionTrigger.DOWN, OnHotkeyGarrison);
		input.AddActionListener(ACTION_CLEAR, EActionTrigger.DOWN, OnHotkeyClear);
		input.AddActionListener(ACTION_ADVANCE, EActionTrigger.DOWN, OnHotkeyAdvance);
		input.AddActionListener(ACTION_BOUND, EActionTrigger.DOWN, OnHotkeyBound);
		input.AddActionListener(ACTION_TAKE_COVER, EActionTrigger.DOWN, OnHotkeyTakeCover);
		m_bHotkeysListening = true;
	}

	protected void StopHotkeys()
	{
		if (!m_bHotkeysListening)
			return;

		InputManager input = GetGame().GetInputManager();
		if (input)
		{
			input.RemoveActionListener(ACTION_GARRISON, EActionTrigger.DOWN, OnHotkeyGarrison);
			input.RemoveActionListener(ACTION_CLEAR, EActionTrigger.DOWN, OnHotkeyClear);
			input.RemoveActionListener(ACTION_ADVANCE, EActionTrigger.DOWN, OnHotkeyAdvance);
			input.RemoveActionListener(ACTION_BOUND, EActionTrigger.DOWN, OnHotkeyBound);
			input.RemoveActionListener(ACTION_TAKE_COVER, EActionTrigger.DOWN, OnHotkeyTakeCover);
		}

		m_bHotkeysListening = false;
	}

	// An action reports input only while one of its contexts is active.
	// The bar keeps its context up, and the duration covers the gap until
	// the next tick so a press is not sampled with the context already off.
	protected void StartContextTick()
	{
		if (m_bContextTicking)
			return;

		m_bContextTicking = true;
		GetGame().GetCallqueue().CallLater(ActivateOrderContext, 0, true);
	}

	protected void StopContextTick()
	{
		if (!m_bContextTicking)
			return;

		m_bContextTicking = false;
		GetGame().GetCallqueue().Remove(ActivateOrderContext);
	}

	protected void ActivateOrderContext()
	{
		if (!m_wBar || !m_wBar.IsVisible())
			return;

		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;

		input.ActivateContext(ORDER_CONTEXT, 1000);
		PollOrderKeys(input);
	}

	protected void PollOrderKeys(InputManager input)
	{
		bool garrison = input.GetActionValue(ACTION_GARRISON) > 0.5;
		if (garrison && !m_bGarrisonDown)
			OnHotkeyGarrison();
		m_bGarrisonDown = garrison;

		bool clear = input.GetActionValue(ACTION_CLEAR) > 0.5;
		if (clear && !m_bClearDown)
			OnHotkeyClear();
		m_bClearDown = clear;

		bool advance = input.GetActionValue(ACTION_ADVANCE) > 0.5;
		if (advance && !m_bAdvanceDown)
			OnHotkeyAdvance();
		m_bAdvanceDown = advance;

		bool bound = input.GetActionValue(ACTION_BOUND) > 0.5;
		if (bound && !m_bBoundDown)
			OnHotkeyBound();
		m_bBoundDown = bound;

		bool takeCover = input.GetActionValue(ACTION_TAKE_COVER) > 0.5;
		if (takeCover && !m_bTakeCoverDown)
			OnHotkeyTakeCover();
		m_bTakeCoverDown = takeCover;
	}

	protected void OnHotkeyGarrison()
	{
		StartNamedOrder("Garrison");
	}

	protected void OnHotkeyClear()
	{
		StartNamedOrder("Clear");
	}

	protected void OnHotkeyAdvance()
	{
		StartNamedOrder("Advance");
	}

	protected void OnHotkeyBound()
	{
		StartNamedOrder("Bound");
	}

	protected void OnHotkeyTakeCover()
	{
		StartNamedOrder("Take cover");
	}

	protected void StartNamedOrder(string orderName)
	{
		float now = 0;
		if (GetGame() && GetGame().GetWorld())
			now = GetGame().GetWorld().GetWorldTime();

		if (now - m_fHotkeyAt < 300)
			return;

		m_fHotkeyAt = now;

		if (!HasCommandedGroup())
			return;

		array<SCR_BaseEditorAction> actions = {};
		GetActions(actions);
		foreach (SCR_BaseEditorAction action : actions)
		{
			if (!action)
				continue;

			SCR_UIInfo info = action.GetInfo();
			if (!info || info.GetName() != orderName)
				continue;

			StartOrder(action);
			return;
		}
	}

	void ShowOrderTooltip(SCR_BaseEditorAction action)
	{
		if (!action || !m_wBar)
			return;

		SCR_UIInfo info = action.GetInfo();
		if (!info)
			return;

		EnsureTooltip();
		if (!m_wTooltip)
			return;

		TextWidget title = TextWidget.Cast(m_wTooltip.FindAnyWidget("Title"));
		TextWidget description = TextWidget.Cast(m_wTooltip.FindAnyWidget("Description"));
		TextWidget hotkeyText = TextWidget.Cast(m_wTooltip.FindAnyWidget("Hotkey"));
		if (title)
			title.SetText(info.GetName());
		if (description)
			description.SetText(info.GetDescription());
		if (hotkeyText)
			hotkeyText.SetText(HotkeyLine(info.GetName()));

		if (title)
			title.SetColor(Color(1, 1, 1, 1));
		if (description)
			description.SetColor(Color(0.75, 0.78, 0.8, 1));
		if (hotkeyText)
			hotkeyText.SetColor(Color(1, 0.86, 0.4, 1));

		m_wTooltip.SetVisible(true);
		m_wTooltip.SetOpacity(1);
		PlaceTooltip();
	}

	void HideOrderTooltip(SCR_BaseEditorAction action)
	{
		if (m_wTooltip)
			m_wTooltip.SetVisible(false);
	}

	protected string HotkeyLine(string orderName)
	{
		if (orderName == "Garrison")
			return "Numpad 1";

		if (orderName == "Clear")
			return "Numpad 2";

		if (orderName == "Advance")
			return "Numpad 3";

		if (orderName == "Bound")
			return "Numpad 4";

		if (orderName == "Take cover")
			return "Numpad 5";

		return string.Empty;
	}

	protected void CreateBar()
	{
		if (m_wBar)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return;

		Widget editor = EditorMenuRoot();
		if (!editor)
			return;

		Widget parent = editor;
		int zOrder = 0;
		Widget toolbar = FindCommandToolbar(editor);
		if (toolbar && toolbar.GetParent() && CoversScreen(toolbar.GetParent()))
		{
			parent = toolbar.GetParent();
			zOrder = toolbar.GetZOrder();
		}

		m_wBar = workspace.CreateWidgets(BAR_LAYOUT, parent);
		if (!m_wBar)
			return;

		FrameSlot.SetAnchorMin(m_wBar, 0.5, 1);
		FrameSlot.SetAnchorMax(m_wBar, 0.5, 1);
		FrameSlot.SetAlignment(m_wBar, 0.5, 1);
		FrameSlot.SetPos(m_wBar, 0, -252);
		FrameSlot.SetSize(m_wBar, 260, 48);
		m_wBar.SetZOrder(zOrder);
		EnsureTooltip();

		array<SCR_BaseEditorAction> actions = {};
		GetActions(actions);
		foreach (SCR_BaseEditorAction action : actions)
		{
			if (!action)
				continue;

			SCR_UIInfo info = action.GetInfo();
			if (!info)
				continue;

			Widget button = m_wBar.FindAnyWidget(info.GetName());
			if (!button)
				continue;

			button.SetColor(Color(0.145, 0.227, 0.247, 1));

			KK_CQBOrderButtonHandler handler = KK_CQBOrderButtonHandler.Cast(
				button.FindHandler(KK_CQBOrderButtonHandler)
			);
			if (handler)
				handler.SetOrder(this, action);

			ImageWidget icon = ImageWidget.Cast(button.FindAnyWidget("Icon"));
			if (icon)
			{
				info.SetIconTo(icon);
				icon.SetColor(Color(1, 1, 1, 1));
				PassCursorThrough(icon);
				PassCursorThrough(icon.GetParent());
			}
		}
	}

	// Sibling of the bar, in the same layer, so it stays on screen with the buttons.
	protected void EnsureTooltip()
	{
		if (m_wTooltip || !m_wBar)
			return;

		Widget parent = m_wBar.GetParent();
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!parent || !workspace)
			return;

		m_wTooltip = workspace.CreateWidgets(TOOLTIP_LAYOUT, parent);
		if (!m_wTooltip)
			return;

		Widget background = m_wTooltip.FindAnyWidget("Background");
		if (background)
			background.SetColor(Color(0.067, 0.086, 0.102, 0.96));

		m_wTooltip.SetVisible(false);
		m_wTooltip.SetOpacity(1);
		PassCursorThrough(m_wTooltip);
		PlaceTooltip();
	}

	// Same anchor as the bar, sitting just above it. Screen-space placement
	// was landing outside the parent, so the hint never drew.
	protected void PlaceTooltip()
	{
		if (!m_wTooltip || !m_wBar)
			return;

		FrameSlot.SetAnchorMin(m_wTooltip, 0.5, 1);
		FrameSlot.SetAnchorMax(m_wTooltip, 0.5, 1);
		FrameSlot.SetAlignment(m_wTooltip, 0.5, 1);
		FrameSlot.SetSize(m_wTooltip, 280, 150);
		FrameSlot.SetPos(m_wTooltip, 0, -312);
		m_wTooltip.SetZOrder(m_wBar.GetZOrder() + 10);
	}

	protected void StartVisibilityTick()
	{
		if (m_bVisibilityTicking)
			return;

		m_bVisibilityTicking = true;
		GetGame().GetCallqueue().CallLater(RefreshVisibility, 200, true);
	}

	protected void StopVisibilityTick()
	{
		if (!m_bVisibilityTicking)
			return;

		m_bVisibilityTicking = false;
		GetGame().GetCallqueue().Remove(RefreshVisibility);
	}

	protected Widget EditorMenuRoot()
	{
		SCR_MenuEditorComponent menuComponent = SCR_MenuEditorComponent.Cast(
			SCR_MenuEditorComponent.GetInstance(SCR_MenuEditorComponent, false)
		);
		if (!menuComponent)
			return null;

		MenuBase menu = menuComponent.GetMenu();
		if (!menu)
			return null;

		return menu.GetRootWidget();
	}

	protected Widget FindCommandToolbar(Widget widget)
	{
		if (!widget)
			return null;

		if (widget.FindHandler(SCR_CommandToolbarEditorUIComponent))
			return widget;

		Widget child = widget.GetChildren();
		while (child)
		{
			Widget found = FindCommandToolbar(child);
			if (found)
				return found;

			child = child.GetSibling();
		}

		return null;
	}

	protected bool CoversScreen(Widget widget)
	{
		if (!widget)
			return false;

		float width;
		float height;
		widget.GetScreenSize(width, height);

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return false;

		float screenW;
		float screenH;
		workspace.GetScreenSize(screenW, screenH);
		return height > screenH * 0.75;
	}

	protected bool CoveredByAnotherMenu()
	{
		if (!m_wBar)
			return false;

		MenuManager manager = GetGame().GetMenuManager();
		if (!manager)
			return false;

		MenuBase top = manager.GetTopMenu();
		if (!top)
			return false;

		Widget root = top.GetRootWidget();
		if (!root)
			return false;

		Widget widget = m_wBar;
		while (widget)
		{
			if (widget == root)
				return false;

			widget = widget.GetParent();
		}

		return true;
	}

	protected void PassCursorThrough(Widget widget)
	{
		if (!widget)
			return;

		widget.SetFlags(widget.GetFlags() | WidgetFlags.IGNORE_CURSOR);
	}

	protected void OnSelectionChanged(
		EEditableEntityState state,
		set<SCR_EditableEntityComponent> entitiesInsert,
		set<SCR_EditableEntityComponent> entitiesRemove
	)
	{
		RefreshVisibility();
	}

	protected void RefreshVisibility()
	{
		if (!m_wBar)
			CreateBar();

		if (!m_wBar)
			return;

		bool show = HasCommandedGroup() && !CoveredByAnotherMenu();
		m_wBar.SetVisible(show);
		if (!show)
			HideOrderTooltip(null);
	}

	protected bool HasCommandedGroup()
	{
		if (!m_Selected)
			return false;

		set<SCR_EditableEntityComponent> selected = new set<SCR_EditableEntityComponent>();
		m_Selected.GetEntities(selected);
		foreach (SCR_EditableEntityComponent entity : selected)
		{
			if (entity && entity.GetAIGroup())
				return true;
		}

		return false;
	}
}

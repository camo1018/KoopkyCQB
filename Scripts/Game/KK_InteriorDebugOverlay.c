class KK_InteriorDebugMark
{
	vector m_vFrom;
	vector m_vTo;
	float m_fRadius;
	int m_iColor;
	bool m_bLine;
}

// One screen overlay for interior points. The latest clear or garrison
// order owns it, so two squads do not draw on top of each other.
class KK_InteriorDebugOverlay
{
	protected static SCR_AIGroup s_Owner;
	protected static int s_iToken;
	protected static CanvasWidget s_wOverlay;
	protected static ref array<ref CanvasWidgetCommand> s_aCommands = {};
	protected static ref array<ref KK_InteriorDebugMark> s_aPending = {};
	protected static ref array<ref KK_InteriorDebugMark> s_aLive = {};
	protected static bool s_bTicking;

	static int Claim(SCR_AIGroup group)
	{
		s_iToken++;
		s_Owner = group;
		return s_iToken;
	}

	static bool Owns(int token)
	{
		return token > 0 && token == s_iToken;
	}

	static void Begin()
	{
		s_aPending = new array<ref KK_InteriorDebugMark>();
	}

	static void Disc(vector world, float radius, int color)
	{
		if (!s_aPending)
			return;

		KK_InteriorDebugMark mark = new KK_InteriorDebugMark();
		mark.m_vFrom = world;
		mark.m_fRadius = radius;
		mark.m_iColor = color;
		s_aPending.Insert(mark);
	}

	static void Line(vector from, vector to, int color)
	{
		if (!s_aPending)
			return;

		KK_InteriorDebugMark mark = new KK_InteriorDebugMark();
		mark.m_vFrom = from;
		mark.m_vTo = to;
		mark.m_iColor = color;
		mark.m_bLine = true;
		s_aPending.Insert(mark);
	}

	static void Commit()
	{
		s_aLive = s_aPending;
		if (!s_aLive || s_aLive.IsEmpty())
		{
			Hide();
			return;
		}

		Redraw();
		EnsureTick();
	}

	static void Hide()
	{
		s_aLive = {};
		s_bTicking = false;
		GetGame().GetCallqueue().Remove(Tick);

		if (!s_wOverlay)
			return;

		s_wOverlay.SetVisible(false);
		if (!s_aCommands)
			s_aCommands = new array<ref CanvasWidgetCommand>();
		s_aCommands.Clear();
		s_wOverlay.SetDrawCommands(s_aCommands);
	}

	protected static void EnsureTick()
	{
		if (s_bTicking)
			return;

		s_bTicking = true;
		GetGame().GetCallqueue().CallLater(Tick, 16, false);
	}

	protected static void Tick()
	{
		if (!s_aLive || s_aLive.IsEmpty())
		{
			s_bTicking = false;
			return;
		}

		Redraw();

		if (s_bTicking)
			GetGame().GetCallqueue().CallLater(Tick, 16, false);
	}

	protected static void Redraw()
	{
		if (!EnsureCanvas())
			return;

		s_aCommands = new array<ref CanvasWidgetCommand>();

		foreach (KK_InteriorDebugMark mark : s_aLive)
		{
			if (!mark)
				continue;

			if (mark.m_bLine)
				AppendLine(mark.m_vFrom, mark.m_vTo, mark.m_iColor);
			else
				AppendDisc(mark.m_vFrom, mark.m_fRadius, mark.m_iColor);
		}

		bool any = !s_aCommands.IsEmpty();
		s_wOverlay.SetVisible(any);
		s_wOverlay.SetDrawCommands(s_aCommands);
	}

	protected static bool EnsureCanvas()
	{
		if (s_wOverlay)
			return true;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return false;

		Widget created = workspace.CreateWidget(
			WidgetType.CanvasWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS | WidgetFlags.BLEND,
			Color.White,
			1,
			null
		);
		s_wOverlay = CanvasWidget.Cast(created);
		if (!s_wOverlay)
			return false;

		FrameSlot.SetAnchorMin(s_wOverlay, 0, 0);
		FrameSlot.SetAnchorMax(s_wOverlay, 1, 1);
		FrameSlot.SetOffsets(s_wOverlay, 0, 0, 0, 0);
		return true;
	}

	protected static int Faded(int color)
	{
		return (color & 0x00FFFFFF) | 0xD8000000;
	}

	protected static bool Project(vector world, out float x, out float y)
	{
		x = 0;
		y = 0;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		BaseWorld gameWorld = GetGame().GetWorld();
		if (!workspace || !gameWorld)
			return false;

		vector screen = workspace.ProjWorldToScreen(world, gameWorld);
		if (screen[2] <= 0.05)
			return false;

		x = workspace.DPIScale(screen[0]);
		y = workspace.DPIScale(screen[1]);
		return true;
	}

	protected static float DiscPixels(vector world, float radius)
	{
		CameraManager manager = GetGame().GetCameraManager();
		vector right = Vector(1, 0, 0);
		if (manager)
		{
			CameraBase camera = manager.CurrentCamera();
			if (camera)
			{
				vector transform[4];
				camera.GetWorldTransform(transform);
				right = transform[0];
			}
		}

		float x0;
		float y0;
		float x1;
		float y1;
		if (!Project(world, x0, y0))
			return 8;
		if (!Project(world + (right * radius), x1, y1))
			return 8;

		float dx = x1 - x0;
		float dy = y1 - y0;
		float size = Math.Sqrt((dx * dx) + (dy * dy)) * 2.0;
		if (size < 6)
			size = 6;
		if (size > 96)
			size = 96;
		return size;
	}

	protected static void AppendDisc(vector world, float radius, int color)
	{
		float x;
		float y;
		if (!Project(world, x, y))
			return;

		float pixelRadius = DiscPixels(world, radius) * 0.5;
		const int SEGMENTS = 16;
		TriMeshDrawCommand disc = new TriMeshDrawCommand();
		disc.m_Vertices = new array<float>();
		disc.m_Indices = new array<int>();
		disc.m_iColor = Faded(color);
		disc.m_Vertices.Insert(x);
		disc.m_Vertices.Insert(y);

		for (int i = 0; i <= SEGMENTS; i++)
		{
			float angle = (Math.PI2 * i) / SEGMENTS;
			disc.m_Vertices.Insert(x + (Math.Cos(angle) * pixelRadius));
			disc.m_Vertices.Insert(y + (Math.Sin(angle) * pixelRadius));
		}

		for (int i = 0; i < SEGMENTS; i++)
		{
			disc.m_Indices.Insert(0);
			disc.m_Indices.Insert(i + 1);
			disc.m_Indices.Insert(i + 2);
		}

		s_aCommands.Insert(disc);
	}

	protected static void AppendLine(vector from, vector to, int color)
	{
		float x1;
		float y1;
		float x2;
		float y2;
		if (!Project(from, x1, y1))
			return;
		if (!Project(to, x2, y2))
			return;

		float dx = x2 - x1;
		float dy = y2 - y1;
		float length = Math.Sqrt((dx * dx) + (dy * dy));
		if (length < 1 || length > 4000)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		float width = 3;
		if (workspace)
			width = workspace.DPIScale(width);

		LineDrawCommand line = new LineDrawCommand();
		line.m_Vertices = new array<float>();
		line.m_Vertices.Insert(x1);
		line.m_Vertices.Insert(y1);
		line.m_Vertices.Insert(x2);
		line.m_Vertices.Insert(y2);
		line.m_fWidth = width;
		line.m_iColor = Faded(color);
		line.m_bShouldEnclose = false;
		s_aCommands.Insert(line);
	}
}

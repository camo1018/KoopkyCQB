enum KK_EBuildingWaypointType
{
	WINDOW,
	DOOR,
	POST,
	ROUTE
}

class KK_AuthoredBuildingWaypoint
{
	int m_iId;
	KK_EBuildingWaypointType m_eType;
	vector m_vLocalPosition;
	vector m_vLocalFacing;
	ref array<int> m_aLinks = {};
}

class KK_CachedInteriorSample
{
	vector m_vLocalPosition;
	KK_EInteriorOpening m_eOpening;
	vector m_vLocalFacing;
	int m_iOpeningHeading;
}

class KK_PrefabWaypointSet
{
	string m_sPrefabName;
	float m_fScaleX;
	float m_fScaleY;
	float m_fScaleZ;
	float m_fHorizontalSpacing;
	float m_fVerticalSpacing;
	float m_fDeduplicateDistance;
	bool m_bHasSampleCache;
	int m_iNextWaypointId = 1;
	ref array<ref KK_AuthoredBuildingWaypoint> m_aWaypoints = {};
	ref array<ref KK_CachedInteriorSample> m_aSamples = {};
	ref array<float> m_aForbiddenFloorYs = {};
}

class KK_BuildingWaypointLibrary
{
	protected static const string PROFILE_DIR = "$profile:KoopkyCQB";
	protected static const string WAYPOINTS_DIR = "$profile:KoopkyCQB/Waypoints";
	protected static const string LEGACY_LIBRARY_PATH = "$profile:KoopkyCQB_waypoints.json";
	protected static const float FLOOR_BAND = 0.75;
	protected static const float SCALE_EPSILON = 0.01;

	protected static ref map<string, ref KK_PrefabWaypointSet> s_mSets;
	protected static bool s_bLoaded;

	static void EnsureLoaded()
	{
		if (s_bLoaded)
			return;

		s_mSets = new map<string, ref KK_PrefabWaypointSet>();
		s_bLoaded = true;
		LoadFromDisk();
	}

	static string ResolvePrefabName(notnull BaseBuilding building)
	{
		EntityPrefabData prefabData = building.GetPrefabData();
		if (!prefabData)
			return string.Empty;

		ResourceName prefabName = prefabData.GetPrefabName();
		if (!prefabName || prefabName.IsEmpty())
			return string.Empty;

		return string.Format("%1", prefabName);
	}

	static void ReadScale(notnull IEntity entity, out float scaleX, out float scaleY, out float scaleZ)
	{
		vector transform[4];
		entity.GetWorldTransform(transform);

		scaleX = transform[0].Length();
		scaleY = transform[1].Length();
		scaleZ = transform[2].Length();

		if (scaleX < 0.001)
			scaleX = 1.0;
		if (scaleY < 0.001)
			scaleY = 1.0;
		if (scaleZ < 0.001)
			scaleZ = 1.0;
	}

	static string CacheKey(
		string prefabName,
		float scaleX,
		float scaleY,
		float scaleZ,
		float horizontalSpacing,
		float verticalSpacing,
		float deduplicateDistance)
	{
		return string.Format(
			"%1|%2|%3|%4|%5|%6|%7",
			prefabName,
			scaleX.ToString(3),
			scaleY.ToString(3),
			scaleZ.ToString(3),
			horizontalSpacing.ToString(3),
			verticalSpacing.ToString(3),
			deduplicateDistance.ToString(3)
		);
	}

	static bool ScaleMatches(
		notnull KK_PrefabWaypointSet prefabSet,
		float scaleX,
		float scaleY,
		float scaleZ)
	{
		return Math.AbsFloat(prefabSet.m_fScaleX - scaleX) <= SCALE_EPSILON &&
			Math.AbsFloat(prefabSet.m_fScaleY - scaleY) <= SCALE_EPSILON &&
			Math.AbsFloat(prefabSet.m_fScaleZ - scaleZ) <= SCALE_EPSILON;
	}

	static bool CacheSettingsMatch(
		notnull KK_PrefabWaypointSet prefabSet,
		float scaleX,
		float scaleY,
		float scaleZ,
		float horizontalSpacing,
		float verticalSpacing,
		float deduplicateDistance)
	{
		if (!ScaleMatches(prefabSet, scaleX, scaleY, scaleZ))
			return false;

		return Math.AbsFloat(prefabSet.m_fHorizontalSpacing - horizontalSpacing) <= SCALE_EPSILON &&
			Math.AbsFloat(prefabSet.m_fVerticalSpacing - verticalSpacing) <= SCALE_EPSILON &&
			Math.AbsFloat(prefabSet.m_fDeduplicateDistance - deduplicateDistance) <= SCALE_EPSILON;
	}

	static KK_PrefabWaypointSet GetOrCreateSet(string prefabName)
	{
		EnsureLoaded();

		if (prefabName.IsEmpty())
			return null;

		KK_PrefabWaypointSet existing;
		if (s_mSets.Find(prefabName, existing) && existing)
			return existing;

		KK_PrefabWaypointSet created = new KK_PrefabWaypointSet();
		created.m_sPrefabName = prefabName;
		created.m_fScaleX = 1.0;
		created.m_fScaleY = 1.0;
		created.m_fScaleZ = 1.0;
		s_mSets.Set(prefabName, created);
		return created;
	}

	static KK_PrefabWaypointSet FindSet(string prefabName)
	{
		EnsureLoaded();

		if (prefabName.IsEmpty())
			return null;

		KK_PrefabWaypointSet existing;
		if (!s_mSets.Find(prefabName, existing))
			return null;

		return existing;
	}

	static vector WorldFacingToLocal(notnull IEntity building, vector worldFacing)
	{
		vector flat = worldFacing;
		flat[1] = 0;

		if (flat.Length() < 0.01)
		{
			vector transform[4];
			building.GetWorldTransform(transform);
			flat = transform[2];
			flat[1] = 0;
		}

		if (flat.Length() < 0.01)
			return Vector(0, 0, 1);

		flat.Normalize();

		vector origin = building.GetOrigin();
		vector localOrigin = building.CoordToLocal(origin);
		vector localTip = building.CoordToLocal(origin + flat);
		vector localFacing = localTip - localOrigin;
		localFacing[1] = 0;

		if (localFacing.Length() < 0.01)
			return Vector(0, 0, 1);

		localFacing.Normalize();
		return localFacing;
	}

	static vector LocalFacingToWorld(notnull IEntity building, vector localFacing)
	{
		vector flat = localFacing;
		flat[1] = 0;

		if (flat.Length() < 0.01)
			flat = Vector(0, 0, 1);

		flat.Normalize();

		vector localOrigin = Vector(0, 0, 0);
		vector worldOrigin = building.CoordToParent(localOrigin);
		vector worldTip = building.CoordToParent(flat);
		vector worldFacing = worldTip - worldOrigin;
		worldFacing[1] = 0;

		if (worldFacing.Length() < 0.01)
			return Vector(0, 0, 1);

		worldFacing.Normalize();
		return worldFacing;
	}

	static KK_AuthoredBuildingWaypoint PlaceWaypoint(
		notnull BaseBuilding building,
		KK_EBuildingWaypointType type,
		vector worldPosition,
		vector worldFacing,
		int linkFromId = -1)
	{
		string prefabName = ResolvePrefabName(building);
		KK_PrefabWaypointSet prefabSet = GetOrCreateSet(prefabName);
		if (!prefabSet)
			return null;

		float scaleX;
		float scaleY;
		float scaleZ;
		ReadScale(building, scaleX, scaleY, scaleZ);
		prefabSet.m_fScaleX = scaleX;
		prefabSet.m_fScaleY = scaleY;
		prefabSet.m_fScaleZ = scaleZ;

		KK_AuthoredBuildingWaypoint waypoint = new KK_AuthoredBuildingWaypoint();
		waypoint.m_iId = prefabSet.m_iNextWaypointId;
		prefabSet.m_iNextWaypointId++;
		waypoint.m_eType = type;
		waypoint.m_vLocalPosition = building.CoordToLocal(worldPosition);
		waypoint.m_vLocalFacing = WorldFacingToLocal(building, worldFacing);

		prefabSet.m_aWaypoints.Insert(waypoint);

		if (linkFromId >= 0)
			LinkWaypoints(prefabSet, linkFromId, waypoint.m_iId);

		SaveToDisk();
		return waypoint;
	}

	static bool LinkWaypoints(
		notnull KK_PrefabWaypointSet prefabSet,
		int fromId,
		int toId)
	{
		KK_AuthoredBuildingWaypoint from = FindWaypoint(prefabSet, fromId);
		KK_AuthoredBuildingWaypoint to = FindWaypoint(prefabSet, toId);
		if (!from || !to || fromId == toId)
			return false;

		if (from.m_aLinks.Find(toId) < 0)
			from.m_aLinks.Insert(toId);

		if (to.m_aLinks.Find(fromId) < 0)
			to.m_aLinks.Insert(fromId);

		return true;
	}

	static KK_AuthoredBuildingWaypoint FindWaypoint(
		notnull KK_PrefabWaypointSet prefabSet,
		int waypointId)
	{
		foreach (KK_AuthoredBuildingWaypoint waypoint : prefabSet.m_aWaypoints)
		{
			if (waypoint && waypoint.m_iId == waypointId)
				return waypoint;
		}

		return null;
	}

	static void StoreSampleCache(
		notnull BaseBuilding building,
		float horizontalSpacing,
		float verticalSpacing,
		float deduplicateDistance,
		notnull array<ref KK_InteriorTarget> targets)
	{
		string prefabName = ResolvePrefabName(building);
		KK_PrefabWaypointSet prefabSet = GetOrCreateSet(prefabName);
		if (!prefabSet)
			return;

		float scaleX;
		float scaleY;
		float scaleZ;
		ReadScale(building, scaleX, scaleY, scaleZ);

		prefabSet.m_fScaleX = scaleX;
		prefabSet.m_fScaleY = scaleY;
		prefabSet.m_fScaleZ = scaleZ;
		prefabSet.m_fHorizontalSpacing = horizontalSpacing;
		prefabSet.m_fVerticalSpacing = verticalSpacing;
		prefabSet.m_fDeduplicateDistance = deduplicateDistance;
		prefabSet.m_bHasSampleCache = true;
		prefabSet.m_aSamples.Clear();

		foreach (KK_InteriorTarget target : targets)
		{
			if (!target || target.m_iAuthoredId >= 0)
				continue;

			KK_CachedInteriorSample sample = new KK_CachedInteriorSample();
			sample.m_vLocalPosition = target.m_vLocalPosition;
			sample.m_eOpening = target.m_eOpening;
			sample.m_iOpeningHeading = target.m_iOpeningHeading;
			sample.m_vLocalFacing = WorldFacingToLocal(building, target.m_vFacing);
			prefabSet.m_aSamples.Insert(sample);
		}

		SaveToDisk();
	}

	static bool HasUsableSampleCache(
		notnull KK_PrefabWaypointSet prefabSet,
		float scaleX,
		float scaleY,
		float scaleZ,
		float horizontalSpacing,
		float verticalSpacing,
		float deduplicateDistance)
	{
		return prefabSet.m_bHasSampleCache &&
			CacheSettingsMatch(
				prefabSet,
				scaleX,
				scaleY,
				scaleZ,
				horizontalSpacing,
				verticalSpacing,
				deduplicateDistance
			);
	}

	static bool IsFloorForbidden(notnull KK_PrefabWaypointSet prefabSet, float localY)
	{
		foreach (float forbiddenY : prefabSet.m_aForbiddenFloorYs)
		{
			if (Math.AbsFloat(localY - forbiddenY) <= FLOOR_BAND)
				return true;
		}

		return false;
	}

	static void ToggleForbiddenFloor(notnull KK_PrefabWaypointSet prefabSet, float localY)
	{
		for (int i = prefabSet.m_aForbiddenFloorYs.Count() - 1; i >= 0; i--)
		{
			if (Math.AbsFloat(prefabSet.m_aForbiddenFloorYs[i] - localY) <= FLOOR_BAND)
			{
				prefabSet.m_aForbiddenFloorYs.Remove(i);
				SaveToDisk();
				return;
			}
		}

		prefabSet.m_aForbiddenFloorYs.Insert(localY);
		SaveToDisk();
	}

	static bool IsForbiddenFloorIndex(notnull KK_PrefabWaypointSet prefabSet, int floorIndex)
	{
		array<float> bands = {};
		CollectFloorBands(prefabSet, bands);

		if (floorIndex < 0 || floorIndex >= bands.Count())
			return false;

		return IsFloorForbidden(prefabSet, bands[floorIndex]);
	}

	static void ToggleForbiddenFloorIndex(notnull KK_PrefabWaypointSet prefabSet, int floorIndex)
	{
		array<float> bands = {};
		CollectFloorBands(prefabSet, bands);

		if (floorIndex < 0 || floorIndex >= bands.Count())
			return;

		ToggleForbiddenFloor(prefabSet, bands[floorIndex]);
	}

	static void CollectFloorBands(
		notnull KK_PrefabWaypointSet prefabSet,
		notnull array<float> outBands)
	{
		outBands.Clear();

		foreach (KK_CachedInteriorSample sample : prefabSet.m_aSamples)
		{
			if (!sample)
				continue;

			float elevation = sample.m_vLocalPosition[1];
			bool matched;

			foreach (float existing : outBands)
			{
				if (Math.AbsFloat(elevation - existing) <= FLOOR_BAND)
				{
					matched = true;
					break;
				}
			}

			if (!matched)
				outBands.Insert(elevation);
		}

		outBands.Sort();
	}

	static void SaveToDisk()
	{
		EnsureLoaded();
		EnsureDirectories();

		for (int i = 0; i < s_mSets.Count(); i++)
		{
			KK_PrefabWaypointSet prefabSet = s_mSets.GetElement(i);
			if (!prefabSet || prefabSet.m_sPrefabName.IsEmpty())
				continue;

			SaveSet(prefabSet);
		}
	}

	protected static void EnsureDirectories()
	{
		FileIO.MakeDirectory(PROFILE_DIR);
		FileIO.MakeDirectory(WAYPOINTS_DIR);
	}

	protected static void SaveSet(notnull KK_PrefabWaypointSet prefabSet)
	{
		EnsureDirectories();

		SCR_JsonSaveContext context = new SCR_JsonSaveContext();
		WriteSet(context, prefabSet);

		string path = PrefabFilePath(prefabSet.m_sPrefabName);
		if (!context.SaveToFile(path))
			Print("KK: Failed to write " + path, LogLevel.ERROR);
	}

	protected static string PrefabFilePath(string prefabName)
	{
		return WAYPOINTS_DIR + "/" + PrefabFileName(prefabName);
	}

	protected static string PrefabFileName(string prefabName)
	{
		string guid = GuidFromPrefab(prefabName);
		if (!guid.IsEmpty())
			return guid + ".json";

		string safe;
		int length = prefabName.Length();
		for (int i = 0; i < length; i++)
		{
			string character = prefabName[i];
			bool allowed =
				(character >= "a" && character <= "z") ||
				(character >= "A" && character <= "Z") ||
				(character >= "0" && character <= "9") ||
				character == "_" ||
				character == "-";

			if (allowed)
				safe = safe + character;
			else
				safe = safe + "_";
		}

		if (safe.IsEmpty())
			safe = "prefab";

		return safe + ".json";
	}

	protected static string GuidFromPrefab(string prefabName)
	{
		int length = prefabName.Length();
		int open = -1;

		for (int i = 0; i < length; i++)
		{
			if (prefabName[i] == "{")
			{
				open = i;
				break;
			}
		}

		if (open < 0)
			return string.Empty;

		string guid;
		for (int i = open + 1; i < length; i++)
		{
			if (prefabName[i] == "}")
				break;

			guid = guid + prefabName[i];
		}

		return guid;
	}

	protected static void WriteSet(
		notnull SCR_JsonSaveContext context,
		notnull KK_PrefabWaypointSet prefabSet)
	{
		context.StartObject();
		context.WriteValue("Prefab", prefabSet.m_sPrefabName);
		context.WriteValue("ScaleX", prefabSet.m_fScaleX);
		context.WriteValue("ScaleY", prefabSet.m_fScaleY);
		context.WriteValue("ScaleZ", prefabSet.m_fScaleZ);
		context.WriteValue("HorizontalSpacing", prefabSet.m_fHorizontalSpacing);
		context.WriteValue("VerticalSpacing", prefabSet.m_fVerticalSpacing);
		context.WriteValue("DeduplicateDistance", prefabSet.m_fDeduplicateDistance);
		context.WriteValue("HasSampleCache", prefabSet.m_bHasSampleCache);
		context.WriteValue("NextWaypointId", prefabSet.m_iNextWaypointId);

		int waypointCount = prefabSet.m_aWaypoints.Count();
		context.StartArray("Waypoints", waypointCount);

		foreach (KK_AuthoredBuildingWaypoint waypoint : prefabSet.m_aWaypoints)
		{
			context.StartObject();

			if (!waypoint)
			{
				context.EndObject();
				continue;
			}

			context.WriteValue("Id", waypoint.m_iId);
			context.WriteValue("Type", waypoint.m_eType);
			context.WriteValue("PosX", waypoint.m_vLocalPosition[0]);
			context.WriteValue("PosY", waypoint.m_vLocalPosition[1]);
			context.WriteValue("PosZ", waypoint.m_vLocalPosition[2]);
			context.WriteValue("FaceX", waypoint.m_vLocalFacing[0]);
			context.WriteValue("FaceY", waypoint.m_vLocalFacing[1]);
			context.WriteValue("FaceZ", waypoint.m_vLocalFacing[2]);

			int linkCount = waypoint.m_aLinks.Count();
			string linksCsv;
			for (int l = 0; l < linkCount; l++)
			{
				if (l > 0)
					linksCsv = linksCsv + ",";

				linksCsv = linksCsv + waypoint.m_aLinks[l].ToString();
			}

			context.WriteValue("Links", linksCsv);

			context.EndObject();
		}

		context.EndArray();

		int sampleCount = prefabSet.m_aSamples.Count();
		context.StartArray("Samples", sampleCount);

		foreach (KK_CachedInteriorSample sample : prefabSet.m_aSamples)
		{
			context.StartObject();

			if (!sample)
			{
				context.EndObject();
				continue;
			}

			context.WriteValue("PosX", sample.m_vLocalPosition[0]);
			context.WriteValue("PosY", sample.m_vLocalPosition[1]);
			context.WriteValue("PosZ", sample.m_vLocalPosition[2]);
			context.WriteValue("Opening", sample.m_eOpening);
			context.WriteValue("OpeningHeading", sample.m_iOpeningHeading);
			context.WriteValue("FaceX", sample.m_vLocalFacing[0]);
			context.WriteValue("FaceY", sample.m_vLocalFacing[1]);
			context.WriteValue("FaceZ", sample.m_vLocalFacing[2]);
			context.EndObject();
		}

		context.EndArray();

		int forbidCount = prefabSet.m_aForbiddenFloorYs.Count();
		string forbidCsv;
		for (int f = 0; f < forbidCount; f++)
		{
			if (f > 0)
				forbidCsv = forbidCsv + ",";

			forbidCsv = forbidCsv + prefabSet.m_aForbiddenFloorYs[f].ToString();
		}

		context.WriteValue("ForbiddenFloors", forbidCsv);

		context.EndObject();
	}

	protected static void LoadFromDisk()
	{
		EnsureDirectories();

		array<string> files = {};
		FileIO.FindFiles(files.Insert, WAYPOINTS_DIR, ".json");

		if (files.IsEmpty())
		{
			LoadLegacyLibrary();
			return;
		}

		foreach (string fileName : files)
		{
			string path = fileName;
			if (!path.Contains("/") && !path.Contains("\\"))
				path = WAYPOINTS_DIR + "/" + fileName;

			LoadSetFile(path);
		}

		PrintFormat("KK: Loaded waypoint library with %1 prefabs", s_mSets.Count());
	}

	protected static void LoadLegacyLibrary()
	{
		if (!FileIO.FileExists(LEGACY_LIBRARY_PATH))
			return;

		SCR_JsonLoadContext context = new SCR_JsonLoadContext();
		if (!context.LoadFromFile(LEGACY_LIBRARY_PATH))
		{
			Print("KK: Failed to load " + LEGACY_LIBRARY_PATH, LogLevel.ERROR);
			return;
		}

		int setCount;
		if (!context.StartArray("Prefabs", setCount))
			return;

		for (int i = 0; i < setCount; i++)
		{
			if (!context.StartObject())
				continue;

			KK_PrefabWaypointSet prefabSet = new KK_PrefabWaypointSet();
			ReadSet(context, prefabSet);
			context.EndObject();

			if (!prefabSet.m_sPrefabName.IsEmpty())
				s_mSets.Set(prefabSet.m_sPrefabName, prefabSet);
		}

		context.EndArray();
		PrintFormat(
			"KK: Moved waypoint library from %1 into %2 (%3 prefabs)",
			LEGACY_LIBRARY_PATH,
			WAYPOINTS_DIR,
			s_mSets.Count()
		);
		SaveToDisk();
	}

	protected static void LoadSetFile(string path)
	{
		SCR_JsonLoadContext context = new SCR_JsonLoadContext();
		if (!context.LoadFromFile(path))
		{
			Print("KK: Failed to load " + path, LogLevel.ERROR);
			return;
		}

		KK_PrefabWaypointSet prefabSet = new KK_PrefabWaypointSet();
		ReadSet(context, prefabSet);

		if (prefabSet.m_sPrefabName.IsEmpty())
			return;

		s_mSets.Set(prefabSet.m_sPrefabName, prefabSet);
	}

	protected static void ReadSet(
		notnull SCR_JsonLoadContext context,
		notnull KK_PrefabWaypointSet prefabSet)
	{
		context.ReadValue("Prefab", prefabSet.m_sPrefabName);
			context.ReadValue("ScaleX", prefabSet.m_fScaleX);
			context.ReadValue("ScaleY", prefabSet.m_fScaleY);
			context.ReadValue("ScaleZ", prefabSet.m_fScaleZ);
			context.ReadValue("HorizontalSpacing", prefabSet.m_fHorizontalSpacing);
			context.ReadValue("VerticalSpacing", prefabSet.m_fVerticalSpacing);
			context.ReadValue("DeduplicateDistance", prefabSet.m_fDeduplicateDistance);
			context.ReadValue("HasSampleCache", prefabSet.m_bHasSampleCache);
			context.ReadValue("NextWaypointId", prefabSet.m_iNextWaypointId);

			int waypointCount;
			if (context.StartArray("Waypoints", waypointCount))
			{
				for (int w = 0; w < waypointCount; w++)
				{
					if (!context.StartObject())
						continue;

					KK_AuthoredBuildingWaypoint waypoint =
						new KK_AuthoredBuildingWaypoint();

					context.ReadValue("Id", waypoint.m_iId);
					int typeValue;
					context.ReadValue("Type", typeValue);
					waypoint.m_eType = typeValue;

					float posX;
					float posY;
					float posZ;
					context.ReadValue("PosX", posX);
					context.ReadValue("PosY", posY);
					context.ReadValue("PosZ", posZ);
					waypoint.m_vLocalPosition = Vector(posX, posY, posZ);

					float faceX;
					float faceY;
					float faceZ;
					context.ReadValue("FaceX", faceX);
					context.ReadValue("FaceY", faceY);
					context.ReadValue("FaceZ", faceZ);
					waypoint.m_vLocalFacing = Vector(faceX, faceY, faceZ);

					string linksCsv;
					context.ReadValue("Links", linksCsv);
					ParseIntCsv(linksCsv, waypoint.m_aLinks);

					prefabSet.m_aWaypoints.Insert(waypoint);
					context.EndObject();
				}

				context.EndArray();
			}

			int sampleCount;
			if (context.StartArray("Samples", sampleCount))
			{
				for (int s = 0; s < sampleCount; s++)
				{
					if (!context.StartObject())
						continue;

					KK_CachedInteriorSample sample = new KK_CachedInteriorSample();

					float posX;
					float posY;
					float posZ;
					context.ReadValue("PosX", posX);
					context.ReadValue("PosY", posY);
					context.ReadValue("PosZ", posZ);
					sample.m_vLocalPosition = Vector(posX, posY, posZ);

					int opening;
					context.ReadValue("Opening", opening);
					sample.m_eOpening = opening;
					context.ReadValue("OpeningHeading", sample.m_iOpeningHeading);

					float faceX;
					float faceY;
					float faceZ;
					context.ReadValue("FaceX", faceX);
					context.ReadValue("FaceY", faceY);
					context.ReadValue("FaceZ", faceZ);
					sample.m_vLocalFacing = Vector(faceX, faceY, faceZ);

					prefabSet.m_aSamples.Insert(sample);
					context.EndObject();
				}

				context.EndArray();
			}

		string forbidCsv;
		context.ReadValue("ForbiddenFloors", forbidCsv);
		ParseFloatCsv(forbidCsv, prefabSet.m_aForbiddenFloorYs);
	}

	protected static void ParseIntCsv(string csv, notnull array<int> outValues)
	{
		outValues.Clear();
		if (!csv || csv.IsEmpty())
			return;

		array<string> parts = {};
		csv.Split(",", parts, true);

		foreach (string part : parts)
		{
			if (!part || part.IsEmpty())
				continue;

			outValues.Insert(part.ToInt());
		}
	}

	protected static void ParseFloatCsv(string csv, notnull array<float> outValues)
	{
		outValues.Clear();
		if (!csv || csv.IsEmpty())
			return;

		array<string> parts = {};
		csv.Split(",", parts, true);

		foreach (string part : parts)
		{
			if (!part || part.IsEmpty())
				continue;

			outValues.Insert(part.ToFloat());
		}
	}
}

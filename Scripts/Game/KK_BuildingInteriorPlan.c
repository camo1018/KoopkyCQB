enum KK_EInteriorTargetState
{
	PENDING,
	ACTIVE,
	VISITED,
	UNREACHABLE
}

class KK_InteriorTarget
{
	vector m_vPosition;
	vector m_vLocalPosition;

	int m_iFloor;
	int m_iCluster;
	int m_iRetries;

	KK_EInteriorTargetState m_eState;
	ref map<AIAgent, float> m_mSightMissAt;

	void KK_InteriorTarget(
		vector position,
		vector localPosition,
		int floorIndex)
	{
		m_vPosition = position;
		m_vLocalPosition = localPosition;
		m_iFloor = floorIndex;
		m_iCluster = -1;
		m_iRetries = 0;
		m_eState = KK_EInteriorTargetState.PENDING;
	}

	bool IsFinished()
	{
		return m_eState == KK_EInteriorTargetState.VISITED ||
			m_eState == KK_EInteriorTargetState.UNREACHABLE;
	}
}

class KK_InteriorCluster
{
	int m_iId;
	int m_iFloor;

	vector m_vCenter;

	ref array<ref KK_InteriorTarget> m_aTargets = {};

	void KK_InteriorCluster(int id, int floorIndex)
	{
		m_iId = id;
		m_iFloor = floorIndex;
	}

	void AddTarget(notnull KK_InteriorTarget target)
	{
		target.m_iCluster = m_iId;
		m_aTargets.Insert(target);
		RecalculateCenter();
	}

	protected void RecalculateCenter()
	{
		if (m_aTargets.IsEmpty())
		{
			m_vCenter = vector.Zero;
			return;
		}

		vector total = vector.Zero;

		foreach (KK_InteriorTarget target : m_aTargets)
			total += target.m_vPosition;

		m_vCenter = total / m_aTargets.Count();
	}

	KK_InteriorTarget GetPrimaryTarget()
	{
		if (m_aTargets.IsEmpty())
			return null;

		KK_InteriorTarget nearest;
		float nearestDistance = float.MAX;

		foreach (KK_InteriorTarget target : m_aTargets)
		{
			float distance = vector.Distance(
				target.m_vPosition,
				m_vCenter
			);

			if (distance < nearestDistance)
			{
				nearestDistance = distance;
				nearest = target;
			}
		}

		return nearest;
	}

	bool HasPendingTarget()
	{
		foreach (KK_InteriorTarget target : m_aTargets)
		{
			if (
				target.m_eState ==
				KK_EInteriorTargetState.PENDING
			)
			{
				return true;
			}
		}

		return false;
	}

	KK_InteriorTarget GetPendingTarget()
	{
		KK_InteriorTarget nearest;
		float nearestDistance = float.MAX;

		foreach (KK_InteriorTarget target : m_aTargets)
		{
			if (
				target.m_eState !=
				KK_EInteriorTargetState.PENDING
			)
			{
				continue;
			}

			float distance = vector.Distance(
				target.m_vPosition,
				m_vCenter
			);

			if (distance < nearestDistance)
			{
				nearestDistance = distance;
				nearest = target;
			}
		}

		return nearest;
	}
}

class KK_BuildingInteriorPlan
{
	protected BaseBuilding m_Building;

	protected ref array<ref KK_InteriorTarget> m_aTargets = {};
	protected ref array<ref KK_InteriorCluster> m_aClusters = {};

	BaseBuilding GetBuilding()
	{
		return m_Building;
	}

	array<ref KK_InteriorTarget> GetTargets()
	{
		return m_aTargets;
	}

	array<ref KK_InteriorCluster> GetClusters()
	{
		return m_aClusters;
	}

bool EnsureNavmeshLoaded(
	notnull AIPathfindingComponent pathfinding,
	notnull BaseBuilding building,
	float tileCheckSpacing = 10.0)
{
	NavmeshWorldComponent navmesh =
		pathfinding.GetNavmeshComponent();

	if (!navmesh)
		return false;

	vector mins;
	vector maxs;
	building.GetBounds(mins, maxs);

	float sampleY = (mins[1] + maxs[1]) * 0.5;
	bool allLoaded = true;

	float x = mins[0];

	while (x <= maxs[0])
	{
		float z = mins[2];

		while (z <= maxs[2])
		{
			vector localPosition = Vector(x, sampleY, z);
			vector worldPosition =
				building.CoordToParent(localPosition);

			if (!navmesh.IsTileLoaded(worldPosition))
			{
				navmesh.LoadTileIn(worldPosition);
				allLoaded = false;
			}

			z += tileCheckSpacing;
		}

		x += tileCheckSpacing;
	}

	vector finalCorner = building.CoordToParent(
		Vector(maxs[0], sampleY, maxs[2])
	);

	if (!navmesh.IsTileLoaded(finalCorner))
	{
		navmesh.LoadTileIn(finalCorner);
		allLoaded = false;
	}

	return allLoaded;
}

	bool Generate(
		notnull SCR_AIGroup group,
		notnull BaseBuilding building,
		float horizontalSpacing = 2.5,
		float verticalSpacing = 1.5,
		float deduplicateDistance = 1.25,
		float clusterRadius = 4.0,
		bool filterUnreachableIslands = false)
	{
		m_Building = building;
		m_aTargets.Clear();
		m_aClusters.Clear();
	
		AIPathfindingComponent pathfinding =
			AIPathfindingComponent.Cast(
				group.FindComponent(AIPathfindingComponent)
			);
	
		if (!pathfinding)
		{
			Print(
				"KK: Group has no AIPathfindingComponent",
				LogLevel.ERROR
			);
			return false;
		}
	
		// Prevent zero or excessively small increments.
		horizontalSpacing = Math.Max(
			horizontalSpacing,
			1.5
		);
	
		verticalSpacing = Math.Max(
			verticalSpacing,
			1.0
		);
	
		vector mins;
		vector maxs;
		ExpandBoundsToEntitySize(building, mins, maxs);

		PrintFormat(
			"KK: Generating interior plan bounds=%1 to %2 horizontalSpacing=%3 verticalSpacing=%4",
			mins,
			maxs,
			horizontalSpacing,
			verticalSpacing
		);
	
		const float HORIZONTAL_MARGIN = 0.6;
		const float VERTICAL_MARGIN = 0.25;
		const float BORDER_INSET = 1.0;
		const float MAX_HORIZONTAL_SNAP = 0.55;
	
		vector projectionExtents = Vector(
			0.55,
			1.0,
			0.55
		);
	
		NavmeshWorldComponent navmesh =
			pathfinding.GetNavmeshComponent();
	
		if (!navmesh)
		{
			Print(
				"KK: Group pathfinding has no navmesh component",
				LogLevel.ERROR
			);
			return false;
		}
	
		int testedCount;
		int projectedCount;
		int rejectedCount;
	
		BaseWorld world = GetGame().GetWorld();
	
		float localY =
			mins[1] + VERTICAL_MARGIN;
	
		while (localY <= maxs[1] - VERTICAL_MARGIN)
		{
			int layerTested;
			int layerAccepted;
	
			float localX =
				mins[0] + HORIZONTAL_MARGIN;
	
			while (localX <= maxs[0] - HORIZONTAL_MARGIN)
			{
				float localZ =
					mins[2] + HORIZONTAL_MARGIN;
	
				while (localZ <= maxs[2] - HORIZONTAL_MARGIN)
				{
					// Advance before validation so every rejection path
					// still progresses to the next sample.
					float sampleZ = localZ;
					localZ += horizontalSpacing;
	
					testedCount++;
					layerTested++;
	
					vector localSample = Vector(
						localX,
						localY,
						sampleZ
					);
	
					vector worldSample =
						building.CoordToParent(localSample);
	
					vector queryPosition =
						SnapToFloor(
							world,
							worldSample,
							0.8
						);
	
					vector correctedPosition;
	
					bool projected =
						pathfinding.GetClosestPositionOnNavmesh(
							queryPosition,
							projectionExtents,
							correctedPosition
						);
	
					if (!projected)
					{
						rejectedCount++;
						continue;
					}
	
					vector queryLocal =
						building.CoordToLocal(queryPosition);
	
					vector correctedLocal =
						building.CoordToLocal(
							correctedPosition
						);
	
					// Reject samples that snap vertically onto
					// a different floor.
					float verticalProjectionDifference =
						Math.AbsFloat(
							correctedLocal[1] -
							queryLocal[1]
						);
	
					if (
						verticalProjectionDifference >
						Math.Max(
							verticalSpacing * 0.75,
							0.8
						)
					)
					{
						rejectedCount++;
						continue;
					}

					// SnapToFloor / navmesh must stay on the storey
					// we sampled. Otherwise upper floors collapse
					// onto ground-level navmesh.
					if (
						Math.AbsFloat(
							correctedLocal[1] - localSample[1]
						) > 1.0
					)
					{
						rejectedCount++;
						continue;
					}
	
					float snapX =
						queryPosition[0] - correctedPosition[0];
					float snapZ =
						queryPosition[2] - correctedPosition[2];
					float horizontalSnap = Math.Sqrt(
						(snapX * snapX) + (snapZ * snapZ)
					);
	
					// A large horizontal snap usually means the
					// sample jumped through a wall onto exterior
					// sidewalk or eave navmesh.
					if (horizontalSnap > MAX_HORIZONTAL_SNAP)
					{
						rejectedCount++;
						continue;
					}
	
					vector reachablePoint;
					if (!navmesh.GetReachablePoint(
						correctedPosition,
						1.5,
						reachablePoint
					))
					{
						rejectedCount++;
						continue;
					}
	
					if (!IsLikelyInterior(
						world,
						building,
						correctedPosition,
						correctedLocal,
						mins,
						maxs,
						BORDER_INSET
					))
					{
						rejectedCount++;
						continue;
					}
	
					if (ContainsNearbyTarget(
						correctedPosition,
						deduplicateDistance
					))
					{
						rejectedCount++;
						continue;
					}
	
					KK_InteriorTarget target =
						new KK_InteriorTarget(
							correctedPosition,
							correctedLocal,
							0
						);
	
					m_aTargets.Insert(target);
					projectedCount++;
					layerAccepted++;
				}
	
				localX += horizontalSpacing;
			}
	
			PrintFormat(
				"KK: Sample layer localY=%1 tested=%2 accepted=%3",
				localY,
				layerTested,
				layerAccepted
			);
	
			localY += verticalSpacing;
		}
	
		if (m_aTargets.IsEmpty())
		{
			PrintFormat(
				"KK: No interior navmesh positions found in %1",
				building
			);
	
			return false;
		}

		if (filterUnreachableIslands && !KeepReachableIsland(group))
		{
			PrintFormat(
				"KK: No reachable interior island from squad in %1",
				building
			);

			return false;
		}
	
		AssignFloorIndices();
		BuildClusters(clusterRadius);
		OrderTargets(group.GetCenterOfMass());
	
		float minLocalY = 10000.0;
		float maxLocalY = -10000.0;
		int floor0Count;
		int floor1Count;
		int floor2PlusCount;
	
		foreach (KK_InteriorTarget summaryTarget : m_aTargets)
		{
			minLocalY = Math.Min(
				minLocalY,
				summaryTarget.m_vLocalPosition[1]
			);
	
			maxLocalY = Math.Max(
				maxLocalY,
				summaryTarget.m_vLocalPosition[1]
			);
	
			if (summaryTarget.m_iFloor <= 0)
				floor0Count++;
			else if (summaryTarget.m_iFloor == 1)
				floor1Count++;
			else
				floor2PlusCount++;
		}
	
		PrintFormat(
			"KK: Interior plan for %1 tested=%2 accepted=%3 rejected=%4 clusters=%5",
			building,
			testedCount,
			projectedCount,
			rejectedCount,
			m_aClusters.Count()
		);
	
		PrintFormat(
			"KK: Floor counts 0=%1 1=%2 2+=%3 localY=%4 to %5",
			floor0Count,
			floor1Count,
			floor2PlusCount,
			minLocalY,
			maxLocalY
		);
	
		return true;
	}	

	protected bool IsInsetFromBounds(
		vector point,
		vector mins,
		vector maxs,
		float inset)
	{
		return point[0] >= mins[0] + inset &&
			point[0] <= maxs[0] - inset &&
			point[1] >= mins[1] - 0.2 &&
			point[1] <= maxs[1] + 0.2 &&
			point[2] >= mins[2] + inset &&
			point[2] <= maxs[2] - inset;
	}

	protected void ExpandBoundsToEntitySize(
		notnull IEntity building,
		out vector mins,
		out vector maxs)
	{
		building.GetBounds(mins, maxs);

		vector size = SCR_EntityHelper.GetEntitySize(building);
		vector center =
			SCR_EntityHelper.GetEntityCenterWorld(building);

		vector topLocal = building.CoordToLocal(
			center + Vector(0, size[1] * 0.5, 0)
		);
		vector bottomLocal = building.CoordToLocal(
			center - Vector(0, size[1] * 0.5, 0)
		);
		vector cornerA = building.CoordToLocal(
			center + Vector(size[0] * 0.5, 0, size[2] * 0.5)
		);
		vector cornerB = building.CoordToLocal(
			center - Vector(size[0] * 0.5, 0, size[2] * 0.5)
		);

		mins[0] = Math.Min(mins[0], Math.Min(cornerA[0], cornerB[0]));
		mins[1] = Math.Min(mins[1], Math.Min(topLocal[1], bottomLocal[1]));
		mins[2] = Math.Min(mins[2], Math.Min(cornerA[2], cornerB[2]));
		maxs[0] = Math.Max(maxs[0], Math.Max(cornerA[0], cornerB[0]));
		maxs[1] = Math.Max(maxs[1], Math.Max(topLocal[1], bottomLocal[1]));
		maxs[2] = Math.Max(maxs[2], Math.Max(cornerA[2], cornerB[2]));
	}

	protected bool IsLikelyInterior(
		BaseWorld world,
		notnull BaseBuilding building,
		vector worldPosition,
		vector localPosition,
		vector mins,
		vector maxs,
		float borderInset)
	{
		bool enclosed = IsEnclosedByWalls(world, worldPosition);

		// Stairwells sit against outer walls. Keep a smaller inset
		// once the point is clearly enclosed.
		float inset = borderInset;
		if (enclosed)
			inset = 0.35;

		if (!IsInsetFromBounds(
			localPosition,
			mins,
			maxs,
			inset
		))
		{
			return false;
		}

		if (IsOutsideAgainstWall(
			world,
			building,
			worldPosition
		))
		{
			return false;
		}

		if (enclosed)
			return HasOverheadStructure(worldPosition, 8.0, false);

		return HasOverheadStructure(worldPosition, 3.6, true);
	}

	protected bool IsOutsideAgainstWall(
		BaseWorld world,
		notnull BaseBuilding building,
		vector worldPosition)
	{
		if (!world)
			return true;

		const float TOWARD_WALL = 1.0;
		const float OPEN_YARD = 3.0;

		vector start =
			worldPosition + Vector(0, 1.1, 0);

		vector center =
			SCR_EntityHelper.GetEntityCenterWorld(building);
		center[1] = start[1];

		vector toCenter = center - start;
		toCenter[1] = 0;

		if (toCenter.Length() < 0.2)
			return false;

		toCenter.Normalize();

		// Outside the facade: the exterior wall is immediately
		// toward the building, and the opposite direction is open
		// yard. Inside, the near wall is behind you.
		float towardDistance = HorizontalTraceDistance(
			world,
			start,
			toCenter,
			TOWARD_WALL + 0.15
		);

		if (towardDistance >= TOWARD_WALL)
			return false;

		float awayDistance = HorizontalTraceDistance(
			world,
			start,
			-toCenter,
			OPEN_YARD
		);

		return awayDistance >= OPEN_YARD;
	}

	protected float HorizontalTraceDistance(
		BaseWorld world,
		vector start,
		vector direction,
		float distance)
	{
		autoptr TraceParam trace = new TraceParam();
		trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		trace.Start = start;
		trace.End = start + (direction * distance);

		float result = world.TraceMove(trace, null);
		return result * distance;
	}

	protected bool IsEnclosedByWalls(
		BaseWorld world,
		vector worldPosition)
	{
		if (!world)
			return false;

		vector start =
			worldPosition + Vector(0, 1.15, 0);

		int hitCount;
		float angle;

		while (angle < 360.0)
		{
			float radians = angle * 0.0174533;
			vector direction = Vector(
				Math.Sin(radians),
				0,
				Math.Cos(radians)
			);

			autoptr TraceParam trace = new TraceParam();
			trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
			trace.Start = start;
			trace.End = start + (direction * 8.0);

			float result = world.TraceMove(trace, null);
			float hitDistance = result * 8.0;

			if (result < 1.0 && hitDistance >= 0.25)
				hitCount++;

			angle += 45.0;
		}

		return hitCount >= 2;
	}

	protected bool ContainsNearbyTarget(
		vector position,
		float minimumDistance)
	{
		foreach (KK_InteriorTarget existing : m_aTargets)
		{
			if (
				vector.Distance(
					existing.m_vPosition,
					position
				) < minimumDistance
			)
			{
				return true;
			}
		}

		return false;
	}
	
	protected vector SnapToFloor(
		BaseWorld world,
		vector worldSample,
		float verticalSpacing)
	{
		if (!world)
			return worldSample;
	
		autoptr TraceParam floorTrace = new TraceParam();
		floorTrace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		floorTrace.Start = worldSample + Vector(0, 0.35, 0);
		floorTrace.End =
			worldSample -
			Vector(0, Math.Max(verticalSpacing, 0.8), 0);
	
		float floorResult = world.TraceMove(floorTrace, null);
		if (floorResult >= 1.0)
			return worldSample;
	
		vector floorPosition =
			floorTrace.Start +
			((floorTrace.End - floorTrace.Start) * floorResult);
	
		floorPosition[1] = floorPosition[1] + 0.15;
		return floorPosition;
	}

	protected bool KeepReachableIsland(notnull SCR_AIGroup group)
	{
		int targetCount = m_aTargets.Count();
		if (targetCount == 0)
			return false;

		float minY = 10000.0;
		float maxY = -10000.0;
		foreach (KK_InteriorTarget spanTarget : m_aTargets)
		{
			minY = Math.Min(minY, spanTarget.m_vPosition[1]);
			maxY = Math.Max(maxY, spanTarget.m_vPosition[1]);
		}

		PrintFormat(
			"KK: Island filter considering %1 targets worldY=%2 to %3",
			targetCount,
			minY,
			maxY
		);

		vector seedPosition = group.GetCenterOfMass();
		array<AIAgent> agents = {};
		group.GetAgents(agents);

		int startIndex = -1;
		float nearestDistance = float.MAX;

		for (int i; i < targetCount; i++)
		{
			KK_InteriorTarget target = m_aTargets[i];
			float distance = vector.Distance(
				seedPosition,
				target.m_vPosition
			);

			foreach (AIAgent agent : agents)
			{
				if (!agent || !agent.GetControlledEntity())
					continue;

				float agentDistance = vector.Distance(
					agent.GetControlledEntity().GetOrigin(),
					target.m_vPosition
				);

				if (agentDistance < distance)
					distance = agentDistance;
			}

			if (distance < nearestDistance)
			{
				nearestDistance = distance;
				startIndex = i;
			}
		}

		if (startIndex < 0)
			return false;

		ref array<int> reached = {};
		for (int flagIndex; flagIndex < targetCount; flagIndex++)
		{
			reached.Insert(0);
		}

		ref array<int> queue = {};
		queue.Insert(startIndex);
		reached[startIndex] = 1;

		int queuePos;
		while (queuePos < queue.Count())
		{
			int currentIndex = queue[queuePos];
			queuePos++;

			KK_InteriorTarget currentTarget =
				m_aTargets[currentIndex];

			for (int j; j < targetCount; j++)
			{
				if (reached[j] != 0)
					continue;

				if (!AreWalkableNeighbors(
					currentTarget,
					m_aTargets[j]
				))
				{
					continue;
				}

				reached[j] = 1;
				queue.Insert(j);
			}
		}

		// Doorways are often unsampled, which would strand later
		// rooms. Once a storey is reached, keep every sample on it.
		const float SAME_STOREY_BAND = 1.25;
		int storeyExpanded;

		for (int keepIndex; keepIndex < targetCount; keepIndex++)
		{
			if (reached[keepIndex] != 0)
				continue;

			float candidateY =
				m_aTargets[keepIndex].m_vPosition[1];

			int reachedIndex;
			while (reachedIndex < targetCount)
			{
				if (reached[reachedIndex] != 0)
				{
					float reachedY =
						m_aTargets[reachedIndex].m_vPosition[1];

					if (
						Math.AbsFloat(candidateY - reachedY) <=
						SAME_STOREY_BAND
					)
					{
						reached[keepIndex] = 1;
						storeyExpanded++;
						break;
					}
				}

				reachedIndex++;
			}
		}

		int otherStoreyKept;
		for (int otherIndex; otherIndex < targetCount; otherIndex++)
		{
			if (reached[otherIndex] == 0)
				otherStoreyKept++;
		}

		ref array<ref KK_InteriorTarget> kept = {};
		for (int keptIndex; keptIndex < targetCount; keptIndex++)
		{
			if (reached[keptIndex] != 0)
				kept.Insert(m_aTargets[keptIndex]);
		}

		PrintFormat(
			"KK: Reachable island kept %1 of %2 targets from seed distance %3 storeyExpanded=%4 otherStoreyDropped=%5",
			kept.Count(),
			targetCount,
			nearestDistance,
			storeyExpanded,
			otherStoreyKept
		);

		if (kept.IsEmpty())
			return false;

		m_aTargets.Clear();
		foreach (KK_InteriorTarget keptTarget : kept)
		{
			m_aTargets.Insert(keptTarget);
		}

		return true;
	}

	protected bool AreWalkableNeighbors(
		notnull KK_InteriorTarget left,
		notnull KK_InteriorTarget right)
	{
		const float MAX_HORIZONTAL = 4.0;
		const float MAX_VERTICAL = 2.5;

		float heightDifference = Math.AbsFloat(
			left.m_vPosition[1] - right.m_vPosition[1]
		);

		if (heightDifference > MAX_VERTICAL)
			return false;

		float offsetX =
			left.m_vPosition[0] - right.m_vPosition[0];
		float offsetZ =
			left.m_vPosition[2] - right.m_vPosition[2];

		float horizontalDistance = Math.Sqrt(
			(offsetX * offsetX) + (offsetZ * offsetZ)
		);

		return horizontalDistance <= MAX_HORIZONTAL;
	}


	protected void AssignFloorIndices()
	{
		const float SAME_FLOOR_BAND = 0.75;
		const float STOREY_GAP = 2.0;
		const int MIN_FLOOR_POINTS = 2;
	
		ref array<float> denseElevations = {};
	
		foreach (KK_InteriorTarget candidate : m_aTargets)
		{
			int nearbyCount;
	
			foreach (KK_InteriorTarget other : m_aTargets)
			{
				if (
					Math.AbsFloat(
						candidate.m_vLocalPosition[1] -
						other.m_vLocalPosition[1]
					) <= SAME_FLOOR_BAND
				)
				{
					nearbyCount++;
				}
			}
	
			if (nearbyCount < MIN_FLOOR_POINTS)
				continue;
	
			float elevation = candidate.m_vLocalPosition[1];
			bool matched;
	
			foreach (float existingElevation : denseElevations)
			{
				if (
					Math.AbsFloat(
						elevation - existingElevation
					) <= SAME_FLOOR_BAND
				)
				{
					matched = true;
					break;
				}
			}
	
			if (!matched)
				denseElevations.Insert(elevation);
		}
	
		if (denseElevations.IsEmpty())
		{
			foreach (KK_InteriorTarget target : m_aTargets)
			{
				target.m_iFloor = 0;
			}
	
			Print("KK: No dense floor bands found, all targets floor 0");
			return;
		}
	
		denseElevations.Sort();
	
		ref array<float> floorElevations = {};
	
		foreach (float elevation : denseElevations)
		{
			if (floorElevations.IsEmpty())
			{
				floorElevations.Insert(elevation);
				continue;
			}
	
			float previous =
				floorElevations[floorElevations.Count() - 1];
	
			if (elevation - previous >= STOREY_GAP)
				floorElevations.Insert(elevation);
		}
	
		foreach (KK_InteriorTarget target : m_aTargets)
		{
			int nearestFloor;
			float nearestDifference = float.MAX;
	
			for (int i; i < floorElevations.Count(); i++)
			{
				float difference = Math.AbsFloat(
					target.m_vLocalPosition[1] -
					floorElevations[i]
				);
	
				if (difference < nearestDifference)
				{
					nearestDifference = difference;
					nearestFloor = i;
				}
			}
	
			target.m_iFloor = nearestFloor;
		}
	
		PrintFormat(
			"KK: Floor representatives=%1 from dense bands=%2",
			floorElevations.Count(),
			denseElevations.Count()
		);
	}	

	protected void BuildClusters(float clusterRadius)
	{
		m_aClusters.Clear();

		foreach (KK_InteriorTarget target : m_aTargets)
		{
			KK_InteriorCluster nearestCluster;
			float nearestDistance = float.MAX;

			foreach (
				KK_InteriorCluster cluster :
				m_aClusters
			)
			{
				if (cluster.m_iFloor != target.m_iFloor)
					continue;

				float distance = vector.Distance(
					cluster.m_vCenter,
					target.m_vPosition
				);

				if (
					distance <= clusterRadius &&
					distance < nearestDistance
				)
				{
					nearestDistance = distance;
					nearestCluster = cluster;
				}
			}

			if (!nearestCluster)
			{
				nearestCluster = new KK_InteriorCluster(
					m_aClusters.Count(),
					target.m_iFloor
				);

				m_aClusters.Insert(nearestCluster);
			}

			nearestCluster.AddTarget(target);
		}
	}

	protected void OrderTargets(vector startPosition)
	{
		ref array<ref KK_InteriorTarget> remaining = {};
		ref array<ref KK_InteriorTarget> ordered = {};
	
		foreach (KK_InteriorTarget sourceTarget : m_aTargets)
		{
			remaining.Insert(sourceTarget);
		}
	
		vector currentPosition = startPosition;
	
		while (!remaining.IsEmpty())
		{
			int floorToVisit = int.MAX;
	
			foreach (KK_InteriorTarget floorTarget : remaining)
			{
				if (floorTarget.m_iFloor < floorToVisit)
					floorToVisit = floorTarget.m_iFloor;
			}
	
			int nearestIndex = -1;
			float nearestDistance = float.MAX;
	
			for (int i; i < remaining.Count(); i++)
			{
				KK_InteriorTarget target = remaining[i];
	
				if (target.m_iFloor != floorToVisit)
					continue;
	
				float distance = vector.Distance(
					currentPosition,
					target.m_vPosition
				);
	
				if (distance < nearestDistance)
				{
					nearestDistance = distance;
					nearestIndex = i;
				}
			}
	
			if (nearestIndex < 0)
				break;
	
			KK_InteriorTarget selected =
				remaining[nearestIndex];
	
			ordered.Insert(selected);
			currentPosition = selected.m_vPosition;
			remaining.Remove(nearestIndex);
		}
	
		m_aTargets.Clear();
	
		foreach (KK_InteriorTarget orderedTarget : ordered)
		{
			m_aTargets.Insert(orderedTarget);
		}
	}
	
	protected bool HasOverheadStructure(
		vector position,
		float maximumHeight = 3.6,
		bool requireTypicalRoom = true)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;
	
		autoptr TraceParam trace = new TraceParam();
		trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		trace.Start = position + Vector(0, 0.4, 0);
		trace.End = position + Vector(0, maximumHeight, 0);
	
		float result = world.TraceMove(trace, null);
		if (result >= 1.0)
			return false;

		float ceilingHeight = result * maximumHeight;
		if (ceilingHeight < 1.6)
			return false;

		if (requireTypicalRoom)
			return ceilingHeight <= 3.5;

		return true;
	}

	KK_InteriorTarget GetNextPendingTarget()
	{
		foreach (KK_InteriorTarget target : m_aTargets)
		{
			if (
				target.m_eState ==
				KK_EInteriorTargetState.PENDING
			)
			{
				return target;
			}
		}

		return null;
	}

	bool IsFinished()
	{
		if (m_aTargets.IsEmpty())
			return false;

		foreach (KK_InteriorTarget target : m_aTargets)
		{
			if (!target.IsFinished())
				return false;
		}

		return true;
	}

	void ResetStates()
	{
		foreach (KK_InteriorTarget target : m_aTargets)
		{
			target.m_eState =
				KK_EInteriorTargetState.PENDING;

			target.m_iRetries = 0;
		}
	}
}
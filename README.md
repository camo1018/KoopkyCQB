# Koopky CQB

Orders for clearing and holding buildings. Works with the base game and with CRX Enfusion AI.

Give **Clear Building** or **Garrison** from the commanding menu, or place the matching waypoint in the editor. The order uses the nearest occupiable building within the search radius (75 m by default). A command-menu order copies the current Koopky CQB scenario properties onto that waypoint. A waypoint placed in the editor keeps the values set on that waypoint.

## Clear Building

Each soldier moves to his own point. The interior is sampled into points, then grouped into rooms on each floor. The squad works the lowest unfinished floor first, and soldiers run while they clear.

Two soldiers take a room: one on the far point, one on the near point. What the rest of the squad does is **Clear coordination behavior**:

- **Stack on the active room.** Extras wait on the near side of the room being cleared.
- **Hold the previous room.** Extras hold the last finished room.
- **Stage at the next room.** Extras wait at the following room.
- **Free pairs.** Every room on the current floor gets a pair at once. This is the default.
- **Spread across the floor.** Each soldier takes a separate point, as far from the others as possible.
- **Spread in pairs.** The same spread, two soldiers to a room.

A point counts as visited when a soldier stands on it, or when he is close enough and can see it. With **Stop once the point is seen** on, he leaves as soon as that sight check passes. A blocked view of the same point is checked again after a short wait.

If a soldier cannot reach a point, he retries. After the other points are done, held points are tried again. With **Clear fails the rest of the room** on, one unreachable point fails the rest of that room with it.

A finished clear garrisons that building when nothing else is queued and **Garrison after the last clear** is on. Chained orders continue to the next one.

## Garrison

Puts the squad on posts inside the building. They sprint toward the building and run once they are inside. On the way in, a bound runs and shoots. Inside, with **Custom CQB AI** on, an enemy in the building stops him until that enemy is gone, and only then does he run on. A closed door holds him, and he can shoot while he waits. At the post, he holds and fights from there.

A post only counts if the soldier is on that floor. A sampled interior post allows a wider drift (the garrison hold radius, 5 m). A placed window, door, or post uses the tighter placed-waypoint hold radius (1 m).

A soldier who leaves his post is pulled back on an interval. He rotates to another spot after a random wait between the minimum and maximum (20–60 s). **Rotate during combat** lets that wait count down while he is alerted or threatened, and he walks to the next post instead of chasing. On the way he stops to shoot, then runs to the next step, the same as the approach.

**Hold doors and windows** marks openings and sends soldiers there first. Off, garrison uses the normal interior posts.

## Doors and movement

**Open doors ahead** is on. A soldier opens a closed door in front of him on the way to a clear point or garrison post. The squad steps out of the door's swing first, so a door that opens toward them can move.

**AI navigation improvements** defaults to off.

- **Off** leaves collision and movement alone.
- **Make Way** is experimental. A soldier who has been stopped asks the person in front to step aside.
- **Pass-through** keeps collision until someone is blocking him. That soldier then ignores character collision until he is through. Walls, doors, and the ground stay solid.

**Pass through characters** is a separate option. While clearing or garrisoning, squad members pass through other characters for the whole order. A player in the squad keeps normal collision. Walls, doors, and the ground stay solid.

## Combat

While a soldier is clearing or garrisoning, **Recognition speed** replaces his normal speed, then returns when the order ends. 1 is normal. **Stay sharp under fire** keeps suppression from slowing recognition, and the first shot does not wait.

**Custom CQB AI** is on. It replaces the normal attack inside the building while soldiers are clearing or garrisoning. A soldier fires himself at the nearest enemy he can see. **Shot check** is how often he looks, in milliseconds, from 50 to 100. It defaults to 75. **Shot delay** is how long that clear sight has to last before the round, in seconds. 0 fires on the next check. If only a step to his left or right can see the enemy, he leans that way and shoots from the peek.

**Sidearm, then release** is on. When the primary has no rounds and no magazine, he switches to another loaded gun and stays on the clear or garrison. Grenades do not count. When no gun has ammo, he leaves that order and the normal AI can rearm him. **Return after rearm** is how many seconds he stays out once gun ammo is back, so a rearm can keep handing him magazines. It defaults to 10. 0 puts him back on the next check. He comes back only while that clear or garrison is still the squad's order. Off keeps him on the order with the rifle down.

## Interior

Horizontal and vertical spacing set how far apart interior points are. Samples closer than the deduplication distance are dropped. Points within the room size on the same floor count as one room.

**Drop points on open ground** is on. Grass, dirt, and other open terrain are removed. Floors, balconies, and roofs stay.

**Drop floors that are not walk-connected** is off. Turn it on to remove points the squad cannot walk to, including upper floors.

## Settings

Game Master scenario properties has a Koopky CQB tab. The server loads `KoopkyCQB/config.json` from the profile folder when the game mode starts. If that file is missing and an older `KoopkyCQB_config.json` is still in the profile root, those values are copied across once.

The **Config file** control on the tab writes the current values, reloads them, or restores the scenario defaults. Export again after a reset if the file should match.

**Draw interior points in Workbench** draws the plan while playing from Workbench.

## Waypoint authoring

**Waypoint radial** is off. Turn it on to add Waypoints and Nodes pages to the commanding menu.

**Lock Building** locks the building you are looking at. The layout is stored for that prefab, so later copies of the same building reuse it. **Release Building** drops the lock.

Place windows, doors, posts, and route points. Add or delete nodes, delete a cluster, link points, and undo. **Forbid above** and **Forbid below** limit which heights are kept. **Clear heights** removes those limits. **Roof allow** includes the roof.

Placed windows, doors, and posts become the tighter garrison holds. Linked points are the route a soldier follows to his post. The files live in `KoopkyCQB/Waypoints` in the profile folder.

**Toggle WP Debug** draws the authored marks. **Sample attempts** is how many times a lock waits for navmesh tiles. Each try is half a second.

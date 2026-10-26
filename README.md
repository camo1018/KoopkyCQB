# Koopky CQB

Squad combat for buildings and the ground outside them. Clear and garrison orders move soldiers through an interior and hold it. Attack orders push them outdoors by bounds and cover. While those orders run, custom CQB combat handles spotting, aiming, and shooting. You can also author the posts and routes a building uses, and recruit a whole squad from one man. Works with the base game and with CRX Enfusion AI.

Give **Clear Building**, **Garrison**, or an **Attack** pace from the commanding menu, or place the matching waypoint in the editor. Game Master also has a separate Koopky command bar: numpad 1 garrison, 2 clear, 3 advance, 4 bound, 5 take cover. A clear or garrison aimed at a building piece, or placed inside one, locks that building. Otherwise the order uses the nearest occupiable building within the search radius (75 m by default). A command-menu order copies the current Koopky CQB scenario properties onto that waypoint. A waypoint placed in the editor keeps the values set on that waypoint until the order starts, then scenario values are applied.

**Recruit Squad** recruits the whole squad of the soldier you are aiming at, not only that man.

## Clear Building

Each soldier moves to his own point. The interior is sampled into points, then grouped into rooms on each floor. The squad works the lowest unfinished floor first, and soldiers run while they clear. **Start from the order** is on by default. A player squad starts on the floor and side where the clear was placed. An AI squad still starts on the lowest floor nearest the squad.

Two soldiers take a room: one on the far point, one on the near point. What the rest of the squad does is **Clear coordination behavior**:

- **Stack on the active room.** Extras wait on the near side of the room being cleared.
- **Hold the previous room.** Extras hold the last finished room.
- **Stage at the next room.** Extras wait at the following room.
- **Free pairs.** Every room on the current floor gets a pair at once. This is the default.
- **Spread across the floor.** Each soldier takes a separate point, as far from the others as possible.
- **Spread in pairs.** The same spread, two soldiers to a room.

A point counts as visited when a soldier stands on it, or when he is close enough and can see it. **Stop once the point is seen** is on by default. He leaves as soon as that sight check passes. A blocked view of the same point is checked again after a short wait.

If a soldier cannot reach a point, he retries. After the other points are done, held points are tried again. **Clear fails the rest of the room** is on by default. One unreachable point fails the rest of that room with it.

Clear and garrison own movement and aim for the whole order. A new clear or garrison replaces the last one. A later move ends a clear. **Garrison after the last clear** is on by default. A finished clear garrisons that building when nothing else is queued. Chained orders continue to the next one. A player squad that finishes a clear goes back to follow.

## Garrison

Puts the squad on posts inside the building. They sprint toward the building and run once they are inside. On the way in, a bound runs and shoots. Inside, with **Custom CQB AI** on by default, an enemy in range stops him until that enemy is gone, and only then does he run on. A closed door holds him, and he can shoot while he waits. At the post, he holds and fights from there.

A post only counts if the soldier is on that floor. A sampled interior post allows a wider drift (the garrison hold radius, 5 m by default). A placed window, door, or post uses the tighter placed-waypoint hold radius (1 m by default).

A soldier who leaves his post is pulled back on an interval. He rotates to another spot after a random wait between the minimum and maximum (20–60 s by default). **Rotate during combat** is on by default. That wait counts down while he is alerted or threatened, and he walks to the next post instead of chasing. On the way he stops to shoot, then runs to the next step, the same as the approach.

**Hold doors and windows** is off by default. Off, garrison uses the normal interior posts. On, it marks openings and sends soldiers there first.

## Attack

Outdoor push orders under the Attack page of the commanding menu, or from the Game Master Koopky bar. With nothing else queued, every pace holds at the point. Another waypoint queued starts when the squad arrives.

**Attack medic** is on by default. A medic leaves the push to treat men who are down or still need treatment, behind the squad. Everyone else keeps moving. A live contact keeps them in the fight. With another waypoint queued, the squad waits at the point until those men catch up and the squad has treated what it can.

- **Advance.** Pairs step toward the point, pause, and scan.
- **Bound.** The same push, with pairs bounding by lane. A bound sprint does not take the engage shot.
- **Take cover.** Pairs bound into cover on the way. **Cover radius** is how far from the point a soldier looks for cover (25 m by default). He runs to that spot, including a man who gets there before the others. **Cover handoff** is how long after one fighter in the moving pair has stopped before the holding pair bounds, if the others have not arrived (2 s by default). Men who are down are not counted. 0 waits until every fighter who can still fight has arrived. At the point, **Cover rotate** moves him to the next cover after that many seconds (12 s by default). Later men leave a few seconds after the first. 0 stays in the first cover. A target hands the move to the normal attack. **Take cover return distance** is how far he may move from the point before he is walked back (40 m by default). With no target he stays on the cover spot.

**Attack engage** is on by default. A soldier on an attack aims and fires at a man perception has noticed, identified or not, which skips the usual wait to confirm him. The first sighting stops him for **Attack engage initial duration** (2 s by default). A new man he can see starts that stop again. While the contact stays open, **Attack engage interval** (6 s) and **Attack engage repeat duration** (2 s) stop him again. Between those stops the move continues, and he can still fire if the rifle is already on the man. **Heard shot memory** keeps him firing at a place he heard a shot and cannot see (8 s by default). Another shot starts the time over. 0 forgets the shot immediately. Engage keeps running until the order is cancelled or replaced.

**Attack lane spacing**, **Attack step length**, **Attack pause**, and **Attack finish distance** set how far apart the lanes sit, how far each step goes, how long a pair holds, and how close they must be to finish. Defaults are 12 m, 20 m, 2 s, and 8 m.

## Doors and movement

**Open doors ahead** is on by default. A soldier opens a closed door in front of him on the way to a clear point or garrison post. The squad steps out of the door's swing only when it opens toward them.

**AI navigation improvements** is off by default.

- **Off** leaves collision and movement alone.
- **Make Way** is experimental. A soldier who has been stopped asks the person in front to step aside.
- **Pass-through** keeps collision until someone is blocking him. That soldier then ignores character collision until he is through. Walls, doors, and the ground stay solid. The interior walk speed stays on during that step.

**Pass through characters** is off by default. While clearing or garrisoning, squad members pass through other characters for the whole order. A player in the squad keeps normal collision. Walls, doors, and the ground stay solid.

## Combat

While a soldier is clearing, garrisoning, or attacking, **Recognition speed** replaces his normal speed, then returns when the order ends. 1 is normal by default. **Stay sharp under fire** is on by default. Suppression does not slow recognition, and the first shot does not wait. On an attack he recognizes much faster than alerted for the whole order, and that rate is still scaled by **Recognition speed**.

**Custom CQB AI** is on by default and is strongly suggested. It replaces the normal attack while soldiers are clearing or garrisoning, including outside the building within **CQB range** (50 m by default). A soldier fires himself at the nearest enemy he can see in that range. A window or a glass pane does not hide that man. The sight trace goes through it. A wall, a frame, and another soldier still block it. He plants and shoots in place, then picks the route back up once that man is gone. On a garrison, a man past that range is left to the normal attack: it aims and sets the fire rate, and snap aim stays off. He does not leave the route to chase. A clear leaves anyone past that range alone. Between contacts he looks along the uncleared route. After the sight line breaks, **Lost contact look** keeps the rifle on the last place the man was seen before the route look returns (2 s by default).

**Sight conditions** is off by default. The ray is the same either way. Off, a clear ray is enough. On, a clear ray is only the first step. He then sees the man when that man is lit enough for how concealed he is. Concealment is not another wall. Crouching or lying down, holding still, and foliage around his body raise it. Running lowers it. A standing man in the open has none, so only very low light hides him. The distance of the ray does not change.

**Shot check** is how often he looks, in milliseconds, from 50 to 100. It is 75 by default. **Shot men per frame** is how many of those soldiers are checked in one frame. It is 2 by default. 0 checks everyone who is due. **Shot delay** is how long that clear sight has to last before the round, in milliseconds, from 0 to 2000. It is 150 by default. 0 fires on the next check. **Shot delay by skill** is on by default. A noob waits four times the delay, a rookie twice, a veteran half, an expert a quarter, and a cylon does not wait. Off uses the delay for every skill. If only a step to his left or right can see the enemy, he leans that way and shoots from the peek. The rifle stays up on a clear while the room gun is aimed.

**Aim point** is where that rifle goes. **Chest, then head** is the default: the upper chest, and the head when the chest is behind cover. **Head, then chest** does the reverse. **Chest** and **Head** stay on that point, with no fallback when it is behind cover. **Random** picks chest or head for each man, keeps that choice, and uses the other point when the first is behind cover.

**Snap aim** is experimental and off by default. While he is aiming at a man inside CQB range, it snaps heading and weapon aim onto that point instead of turning at the normal rate. A man past that range is not snapped. **Snap aim error** is on by default. It scatters that point by skill, distance, weapon, and light. A regular misses by about 1.25 degrees, and a lower skill opens that up. The same angle is a larger miss farther out.

**Barrel try** is how long he keeps shooting a man who has taken none of his damage (1 s by default). A round from someone else does not count. Then he leaves that spot and takes the route up again. **Barrel cool off** is how long that move lasts before he will plant and shoot from the next spot (2 s by default).

Under threat, **Run while reloading** is on by default. It starts the reload and runs to the cluster node farthest from the threat. Off reloads in place. **Sprint to cover** is experimental and off by default: with run while reloading on, an empty gun with the target still visible sprints to a hidden node, then reloads. **Reload below** tops off once the magazine is under that percent (30 by default) after the enemy has stayed out of sight for **Out of sight** (8 s by default). An empty gun still reloads immediately.

**Sidearm, then release** is on by default. When the primary has no rounds and no magazine, he switches to another loaded gun and stays on the clear or garrison. Grenades do not count. When no gun has ammo, he leaves that order and the normal AI can rearm him. **Return after rearm** is how many seconds he stays out once gun ammo is back, so a rearm can keep handing him magazines. It is 10 by default. 0 puts him back on the next check. He comes back only while that clear or garrison is still the squad's order. Off keeps him on the order with the rifle down.

## Interior

Horizontal and vertical spacing set how far apart interior points are. Samples closer than the deduplication distance are dropped. Points within the room size on the same floor count as one room.

**Drop points on open ground** is on by default. Grass, dirt, and other open terrain are removed. Floors, balconies, and roofs stay.

**Drop floors that are not walk-connected** is off by default. Turn it on to remove points the squad cannot walk to, including upper floors.

## Settings

Game Master scenario properties has a Koopky CQB tab, including Clear, Garrison, Attack, Combat, Interior, and Debug. The server loads `KoopkyCQB/config.json` from the profile folder when the game mode starts. If that file is missing and an older `KoopkyCQB_config.json` is still in the profile root, those values are copied across once.

The **Config file** control on the tab writes the current values, reloads them, or restores the scenario defaults. Export again after a reset if the file should match.

**Draw interior points** is off by default. It draws the plan for the latest clear or garrison order. **Debug log** is off by default. It writes KK log lines. **Performance log** is off by default. Once a second it logs how long the CQB fight and snap aim took.

## Waypoint authoring

**Waypoint radial** is off by default. Turn it on to add Waypoints and Nodes pages to the commanding menu.

**Lock Building** locks the building you are looking at. The layout is stored for that prefab, so later copies of the same building reuse it. **Release Building** drops the lock.

Place windows, doors, posts, and route points. Add or delete nodes, delete a cluster, link points, and undo. **Forbid above** and **Forbid below** limit which heights are kept. Confirm a forbid height with left click. **Clear heights** removes those limits. **Roof allow** includes the roof.

Placed windows, doors, and posts become the tighter garrison holds. Linked points are the route a soldier follows to his post. The files live in `KoopkyCQB/Waypoints` in the profile folder.

**Toggle WP Debug** draws the authored marks. In Workbench they are debug shapes. Outside Workbench they are circles and lines on the HUD. **Sample attempts** is how many times a lock waits for navmesh tiles. It is 20 by default. Each try is half a second.

# Koopky CQB

Orders for clearing and holding buildings. Works with the base game and with CRX Enfusion AI.

## Orders

**Clear Building** sends the squad through the interior. Each soldier moves to his own point instead of following the leader.

**Garrison** puts them on posts inside the building. On the way there they ignore ordinary fights and only break off for an emergent threat. Once they are on the post, they fight as usual. They sprint to the building and run once they are inside.

A finished clear garrisons that building only when nothing else is queued. Chained orders continue to the next one.

While garrisoned, a post only counts if the soldier is on that floor. Soldiers also rotate to other spots in the building.

## Settings

Game Master scenario properties has a Koopky CQB tab. Command-menu orders use those values. A waypoint placed in the editor keeps the values set on that waypoint.

The tab covers interior spacing, search distance, timeouts, retries, whether an unreachable point fails the rest of the room, and whether floors that are not walk-connected are dropped. Recognition speed applies while a soldier is clearing or garrisoning, then returns to normal when the order ends. Interior debug points can be drawn in Workbench.

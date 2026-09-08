# Historical Red Alert PlayerControl multiplayer authority audit

Base authority: `vanilla` at `a006664fa168116a628927824a7337a4f1fd2094` (tree `83329f374dc9efe8f0135576013681acbd70fb9b`).

Status: source-confirmed exploit; hardening design complete; engine patch not yet applied in this branch.

## Source-confirmed Red Alert behaviour

1. `HouseClass::Read_INI` creates every House and reads the House-section boolean `PlayerControl` directly into `HouseClass::IsPlayerControl`. The default is false.
2. Multiplayer house assignment occurs after scenario House parsing. The local `PlayerPtr` is marked `IsHuman=true` and `IsPlayerControl=true`, but the classic assignment path does not sanitize a foreign MultiX House that was already set player-controllable by the scenario.
3. Classic multiplayer assignment sorts participants by chosen colour and assigns the ordered participants to `Multi1`, `Multi2`, ... . Colour therefore determines which MultiX section names a participant; colour itself is not command authority.
4. RA input/control logic uses `House->IsPlayerControl` for selection and for ordinary player move/fire/action eligibility. Thus setting a foreign MultiX House player-controllable exposes that House's objects to normal local control paths without changing ownership.
5. `Queue_Mission` trusts its source target and creates a normal `MEGAMISSION` event without checking source-object ownership.
6. Event constructors identify the originator with the local player's House ID while retaining the foreign object as `Whom`.
7. Multiplayer packets are deterministic. The packet header carries the originating House ID; compressed commands inherit that ID. The receive path also knows the transport connection ID, but currently does not bind the packet/event origin ID to that transport identity.
8. `EventClass::Execute` applies `MEGAMISSION`, `IDLE`, `SCATTER`, `ARCHIVE`, `REPAIR`, and `PRIMARY` operations without a general sender-House-versus-source-object-owner authority check. `SELL` already contains an ownership check.
9. Therefore a source-supported minimal exploit is a foreign multiplayer House section (for example the opponent's `MultiN`) with `PlayerControl=yes`. The local UI can select/order the foreign House's objects and the resulting deterministic event is executed by all peers. Ownership remains foreign.
10. This explains the historical report that the exploit did not produce an out-of-sync condition: the unauthorized command itself is distributed through the normal deterministic event stream, so every peer mutates the same object in the same way.

`Allies=` is not required for this control path. Alliance state affects target/action semantics but is not the authority grant responsible for the exploit.

## Security invariant

In network multiplayer, a participant may originate gameplay commands only for Houses/objects explicitly assigned to that participant by the multiplayer session. Scenario `PlayerControl` / `IsPlayerControl` is a scenario-human-control semantic and must not grant network command authority.

## Required narrow hardening

The final patch should use defence in depth without deleting legitimate single-player `PlayerControl` semantics:

- network-only selection/input guard: foreign `House != PlayerPtr` objects cannot become player-commandable merely because `IsPlayerControl` is true;
- network-only local command-construction guard: reject a `Queue_Mission` whose source object's House is not `PlayerPtr`;
- transport binding: validate the received packet header origin against the connection sender identity, and ensure uncompressed sub-events cannot spoof a different event origin;
- deterministic event-application guard: for object-control events, reject when the event origin House does not own the source object; the decision uses event/simulation data shared by all peers and therefore is deterministic;
- preserve single-player/scenario `PlayerControl` behaviour and do not make `Allies=` imply shared command authority.

## Required RA regression names

- `RA_PLAYERCONTROL_FOREIGN_SELECTION_BLOCKED`
- `RA_PLAYERCONTROL_FOREIGN_COMMAND_BLOCKED`
- `RA_PLAYERCONTROL_NO_NETWORK_EVENT_FOR_FOREIGN_OBJECT`
- `RA_NORMAL_OWN_UNIT_COMMAND_PASS`
- `RA_SINGLEPLAYER_PLAYERCONTROL_PRESERVED`
- `RA_UNAUTHORISED_REMOTE_EVENT_REJECTED_DETERMINISTICALLY` (if receiving-side validation is implemented)

Also cover ordinary 1v1, AI opponent, allies, and multiple Houses where practical with synthetic/non-proprietary fixtures only.

## Tiberian Dawn comparison

TD does not expose the same RA `PlayerControl` House-INI path. Its ordinary move/control checks use exact local ownership (`PlayerPtr == House` / `Is_Owned_By_Player`), so the historical one-line RA exploit is not reproduced through TD scenario House configuration.

TD nevertheless has a related lockstep trust boundary worth a separate bounded hardening review: its event stream carries both House ID and `MPlayerID`, and ordinary `MEGAMISSION` application does not itself bind the command source object's owner to the network originator. This is architectural exposure to a deliberately modified event-producing client, not the same PlayerControl/configuration exploit and should not be conflated with it.

// ---- the events of the game (CG_EntityEvent in cg_event.c): what a client is told once and shows for a moment
//
// That somebody fired, that two blades met, where the beam of a disruptor went. The server keeps the events of
// every frame for the pages (WA_EventsFrame in wa_state.c) and a poll brings the ones the page does not have:
// each its number, how long ago in msec, the entity, the event (the names come once, /api/list?kind=events), its
// parameter, where, origin2, the angles, and of the entity that carried it otherEntityNum, otherEntityNum2,
// weapon, eFlags, time, time2, clientNum and owner.
// Stand-in: the events are taken and nothing is shown for them.

let evSeq = -1;			// the number of the last event the page has, -1 until the server said where it is
const EV = {};			// the number of an event by its name (EV.EV_FIRE_WEAPON), there once the list is read
let evNames = null;

// what a poll adds to its question, and what is done with its answer
const evQuery = () => evSeq >= 0 ? '&evs=' + evSeq : '';
function takeEvents(s) {
	if (s.evSeq === undefined) return;
	if (!evNames) {
		evNames = [];
		fetch('api/list?kind=events').then(r => r.json()).then(j => { evNames = j.names || []; evNames.forEach((n, i) => { if (n) EV[n] = i; }); }).catch(() => { evNames = null; });
	}
	for (const e of s.ev || []) {
		gameEvent({ seq: e[0], ago: e[1], ent: e[2], event: e[3], name: (evNames && evNames[e[3]]) || '', parm: e[4], org: [e[5], e[6], e[7]], org2: [e[8], e[9], e[10]], angles: [e[11], e[12], e[13]],
			other: e[14], other2: e[15], weapon: e[16], eFlags: e[17], time: e[18], time2: e[19], client: e[20], owner: e[21] });
	}
	evSeq = s.evSeq;
}

// one event of the game, as a client gets it in CG_EntityEvent
function gameEvent(e) {}

// at the end of the scene: what of the events is drawn by itself (the flare where two blades met)
function drawEvents(now) {}

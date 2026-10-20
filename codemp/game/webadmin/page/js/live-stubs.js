// ---- what the drawing asks of the parts of the panel the live view leaves out (live.js)
//
// The keys and the clicks on the map (js/input.js), a player moved from the page (js/control.js), the panel of a
// player (js/players.js), and what only the editor draws. The live view moves its camera by itself
// (js/live-boot.js) and shows no panel, so each of these is what it is while nobody uses it.

// the questions of the drawing (api/...) are below /live/ of the server: the website puts them where it likes
const liveBase = (window.VJK_LIVE && window.VJK_LIVE.base) || '/live/';
const fetch = (url, opts) => window.fetch(typeof url === 'string' && url.startsWith('api/') ? liveBase + url : url, opts);

const keys = new Set();
let marker = null;
function controlFrame() {}
const JP_RANKS = [];
function select() {}
function renderCommand() {}
function confirmed() { return false; }
// the editor's own lines, boxes and the route
const picked = new Set();
let surfShown = null;
function classColor() { return [1, 1, 1]; }
function drawEditorLines() {}
function drawFacing() {}
function drawDraftMark() {}
function drawSurfaceWire() {}
function drawRoute() {}
function drawBoxHandles() {}

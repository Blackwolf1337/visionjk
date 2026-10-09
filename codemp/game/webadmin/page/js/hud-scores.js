// The scoreboard and the centre prints of a client (cg_scoreboard.c; CG_CenterPrint and CG_DrawCenterString of
// cg_draw.c; the part of CG_Obituary in cg_event.c that names the killer and prints "You killed ...")
//
// A client that follows a player shows the scoreboard while he is dead and in the intermission. It is sent the
// scores when he dies ("scores", DeathmatchScoreboardMessage in g_cmds.c); the page has them with his player
// state while his client would show them ("scoreboard" of state.hud) and draws them as they are at that moment.
// Not here: the flag in front of one who carries it as a model that turns (cg_draw3DIcons: its picture is drawn,
// as with cg_draw3DIcons 0), the pictures of a power duel and of a siege class, the line of who won a siege, and
// that no scoreboard comes for a death during the warmup (the feed does not tell the warmup).

const HUD_SB = (() => {
	// SB_SCORELINE_X is 65 with the JA+ client plugin: wider than the 100 of the game, the score column also holds the deaths
	const X = 65, WIDTH = 640 - X * 2, HEADER = 86, TOP = HEADER + 32, STATUSBAR = 420, NORMAL_HEIGHT = 25, INTER_HEIGHT = 15;
	return { X, WIDTH, HEADER, TOP, NORMAL_HEIGHT, INTER_HEIGHT, BOTICON_X: 32, NAME_X: X, SCORE_X: X + 0.55 * WIDTH, PING_X: X + 0.75 * WIDTH, TIME_X: X + 0.85 * WIDTH,
		MAXCLIENTS_NORMAL: Math.trunc((STATUSBAR - TOP) / NORMAL_HEIGHT), MAXCLIENTS_INTER: Math.trunc((STATUSBAR - TOP) / INTER_HEIGHT) - 1 };
})();
const HUD_PM_DEAD = 5, HUD_RANK_TIED_FLAG = 0x4000, HUD_PW_REDFLAG = 4, HUD_PW_BLUEFLAG = 5, HUD_PW_NEUTRALFLAG = 6, HUD_CENTER_TIME = 3;	// cg_centerTime
// the icons of the three flag items (bg_itemlist)
const HUD_FLAG_ICONS = { [HUD_TEAM_RED]: 'gfx/hud/mpi_rflag', [HUD_TEAM_BLUE]: 'gfx/hud/mpi_bflag', [HUD_TEAM_FREE]: 'icons/iconf_neutral1' };

// cg.killerName, cg.centerPrint with its time, its height and its lines, and "localClient" of cg_scoreboard.c
const hudBoard = { killerName: '', localClient: false, centerPrint: '', centerPrintTime: 0, centerPrintY: 0, centerPrintLines: 0 };

// CG_PlaceString: "1st", "2nd", "Tied for 3rd"
function hudPlaceString(rank) {
	const tied = rank & HUD_RANK_TIED_FLAG ? hudString('MP_INGAME', 'TIED_FOR') + ' ' : '';
	rank &= ~HUD_RANK_TIED_FLAG;
	const st = hudString('MP_INGAME', 'NUMBER_ST'), nd = hudString('MP_INGAME', 'NUMBER_ND'), rd = hudString('MP_INGAME', 'NUMBER_RD'), th = hudString('MP_INGAME', 'NUMBER_TH');
	const end = rank === 11 || rank === 12 || rank === 13 ? th : rank % 10 === 1 ? st : rank % 10 === 2 ? nd : rank % 10 === 3 ? rd : th;
	return tied + rank + end;
}

// ---- the centre print

// CG_CenterPrint: a text for the middle of the screen, y its height. The lines are counted for the centring: a
// line break, or 50 characters.
function hudCenterPrint(str, y) {
	const B = hudBoard;
	B.centerPrint = str.slice(0, 1023);
	B.centerPrintTime = hudTime();
	B.centerPrintY = Math.trunc(y);
	B.centerPrintLines = 1;
	for (let n = 0, i = 0; n < B.centerPrint.length; n++) {
		if (++i >= 50) { i = 0; B.centerPrintLines++; }
		else if (B.centerPrint[n] === '\n') B.centerPrintLines++;
	}
}

// CG_DrawCenterString: there for three seconds (cg_centerTime), fading at the end, 50 characters to a line and
// cut at a blank where a word would be cut
function hudDrawCenterString(cg) {
	const B = hudBoard;
	if (!B.centerPrintTime) return;
	const alpha = hudFadeColor(cg, B.centerPrintTime, 1000 * HUD_CENTER_TIME);
	if (alpha === null) return;
	// BG_IsWhiteSpace, with the end of the text
	const white = c => c === undefined || c === ' ' || c === '\n', color = [1, 1, 1, alpha];
	let start = B.centerPrint, y = B.centerPrintY - Math.trunc(B.centerPrintLines * HUD_BIGCHAR_HEIGHT / 2);
	for (;;) {
		let l = 0;
		while (l < 50 && l < start.length && start[l] !== '\n') l++;
		let line = start.slice(0, l);
		if (!white(start[l]) && !white(line[l - 1])) {
			// a word may have been cut: back to the last blank, if there is one
			for (let counter = l - 2; counter >= 0; counter--) {
				if (white(start[counter])) { line = line.slice(0, counter); l = counter + 1; break; }
			}
		}
		const w = hudTextWidth(line, 1.0, HUD_FONT_MEDIUM), h = hudTextHeight(line, 1.0, HUD_FONT_MEDIUM);
		hudTextPaint(Math.trunc((640 - w) / 2), y + h, 1.0, color, line, 0, 0, HUD_TEXTSTYLE.SHADOWEDMORE, HUD_FONT_MEDIUM);
		y += h + 6;
		if (start[l] === '\n') l++;
		if (l >= start.length) break;
		start = start.slice(l);
	}
}

// CG_Obituary, for what it shows in the 2D of the one the page looks through the eyes of: "You killed ..." with
// his place when he was the attacker, and the name of the killer for the scoreboard when he was the target. Only
// a kill by another client does either. e: the event (other: the target, other2: the attacker).
// Not here: the texts it prints to the console, and in a Jedi Master game the line about the Jedi Master (whether
// the one who died was it the feed does not tell: the short text is taken there).
function hudObituary(e) {
	const target = e.other, attacker = e.other2;
	if (view.id < 0 || target < 0 || target >= 32 || attacker < 0 || attacker >= 32 || attacker === target) return;
	const t = hudClientInfo(target), a = hudClientInfo(attacker);
	if (!t || !a) return;
	if (attacker === view.id) {
		const gt = state.gametype || 0, h = state.hud && state.hud.client === view.id ? state.hud : null, killed = hudString('MP_INGAME', 'KILLED_MESSAGE'), targetName = t.name + '^7';
		let s = killed + ' ' + targetName;
		if (gt === HUD_GT.JEDIMASTER && !(h && h.jediMaster)) s += '\n';
		else if (gt < HUD_GT.TEAM && gt !== HUD_GT.DUEL && gt !== HUD_GT.POWERDUEL) {
			const rank = h ? h.pers[HUD_PERS.RANK] : 0, score = h ? h.pers[HUD_PERS.SCORE] : a.score;
			s += '.\n' + hudPlaceString(rank + 1) + ' ' + hudString('MP_INGAME', 'PLACE_WITH') + ' ' + score + '.';
		}
		hudCenterPrint(s, 480 * 0.30);
	}
	if (target === view.id) hudBoard.killerName = a.name + '^7';
}

// ---- the scoreboard

// CG_DrawFlagModel with cg_draw3DIcons 0: the picture of the flag item
function hudDrawFlagIcon(x, y, size, team) {
	hudPic(x, y, size, size, hudShader(HUD_FLAG_ICONS[team]));
}

// CG_DrawClientScore: one line. score: { client, score, ping, time, powerups, deaths, wins, losses }.
function hudDrawClientScore(cg, y, score, fade, largeFormat) {
	const ps = cg.ps, cgs = cg.cgs, ci = hudClientInfo(score.client), white = HUD_CT.WHITE, duel = cgs.gametype === HUD_GT.DUEL || cgs.gametype === HUD_GT.POWERDUEL;
	const scale = largeFormat ? 1.0 : 0.75, iconSize = largeFormat ? HUD_SB.NORMAL_HEIGHT : HUD_SB.INTER_HEIGHT;
	if (!ci) return;
	// the flag he carries, by the powerups the scores came with
	if (score.powerups & (1 << HUD_PW_NEUTRALFLAG)) hudDrawFlagIcon(HUD_SB.BOTICON_X, largeFormat ? y - (32 - HUD_BIGCHAR_HEIGHT) / 2 : y, iconSize, HUD_TEAM_FREE);
	else if (score.powerups & (1 << HUD_PW_REDFLAG)) hudDrawFlagIcon(HUD_SB.BOTICON_X, y, iconSize, HUD_TEAM_RED);
	else if (score.powerups & (1 << HUD_PW_BLUEFLAG)) hudDrawFlagIcon(HUD_SB.BOTICON_X, y, iconSize, HUD_TEAM_BLUE);

	// the line of the one who looks, in the colour of his place
	if (score.client === ps.clientNum) {
		hudBoard.localClient = true;
		const rank = ps.persistant[HUD_PERS.TEAM] === HUD_TEAM_SPECTATOR || cgs.gametype >= HUD_GT.TEAM ? -1 : ps.persistant[HUD_PERS.RANK] & ~HUD_RANK_TIED_FLAG;
		const c = rank === 0 ? [0, 0, 0.7] : rank === 1 ? [0.7, 0, 0] : rank === 2 ? [0.7, 0.7, 0] : [0.7, 0.7, 0.7];
		hudFillRect(HUD_SB.X - 5, y + 2, 640 - HUD_SB.X * 2 + 10, largeFormat ? HUD_SB.NORMAL_HEIGHT : HUD_SB.INTER_HEIGHT, [c[0], c[1], c[2], fade * 0.7]);
	}

	const text = (x, s) => hudTextPaint(x, y, 1.0 * scale, white, s, 0, 0, HUD_TEXTSTYLE.OUTLINED, HUD_FONT_SMALL);
	hudTextPaint(HUD_SB.NAME_X, y, 0.9 * scale, white, ci.name, 0, 0, HUD_TEXTSTYLE.OUTLINED, HUD_FONT_MEDIUM);
	if (score.ping !== -1) {
		// wins and losses in a duel, else the score with the deaths (what a JA+ server from 2.1 on sends a client with the plugin)
		if (hudTeamOf(ci) !== HUD_TEAM_SPECTATOR || duel) text(HUD_SB.SCORE_X, duel ? score.wins + '/' + score.losses : score.score + '/' + score.deaths);
		// cg_scoreboardBots: a bot has no ping
		text(HUD_SB.PING_X, ci.bot ? 'BOT' : String(score.ping));
		text(HUD_SB.TIME_X, String(score.time));
	} else {
		text(HUD_SB.SCORE_X, '-');
		text(HUD_SB.PING_X, '-');
		text(HUD_SB.TIME_X, '-');
	}
	// ready to leave the intermission
	if (ps.stats[HUD_STAT.CLIENTS_READY] & (1 << score.client)) hudTextPaint(HUD_SB.NAME_X - 64, y + 2, 0.7 * scale, white, hudString('MP_INGAME', 'READY'), 0, 0, HUD_TEXTSTYLE.OUTLINED, HUD_FONT_MEDIUM);
}

// CG_TeamScoreboard: the lines of one team, maxClients at most. Returns how many (countOnly: draws none).
function hudTeamScoreboard(cg, y, team, fade, maxClients, lineHeight, countOnly) {
	let count = 0;
	for (const score of cg.scores) {
		if (count >= maxClients) break;
		const ci = hudClientInfo(score.client);
		if (!ci || hudTeamOf(ci) !== team) continue;
		if (!countOnly) hudDrawClientScore(cg, y + lineHeight * count, score, fade, lineHeight === HUD_SB.NORMAL_HEIGHT);
		count++;
	}
	return count;
}

// CG_DrawOldScoreboard. Returns whether it is there (the centre print is not drawn then).
function hudDrawOldScoreboard(cg) {
	const ps = cg.ps, cgs = cg.cgs, B = hudBoard, h = state.hud && state.hud.client === cg.d.id ? state.hud : null;
	const intermission = ps.pm_type === HUD_PM_INTERMISSION;
	if (!(ps.pm_type === HUD_PM_DEAD || intermission) || !h || !h.scoreboard) {
		// the next time the scoreboard comes up it does not name the killer of the last time
		B.killerName = '';
		return false;
	}
	const fade = 1, white = HUD_CT.WHITE, duel = cgs.gametype === HUD_GT.DUEL || cgs.gametype === HUD_GT.POWERDUEL, name = id => (hudClientInfo(id) || { name: '' }).name;
	const centre = (s, y) => hudTextPaint(320 - Math.trunc(hudTextWidth(s, 1.0, HUD_FONT_MEDIUM) / 2), y, 1.0, white, s, 0, 0, HUD_TEXTSTYLE.OUTLINED, HUD_FONT_MEDIUM);
	cg.scores = h.scoreboard.map(r => ({ client: r[0], score: r[1], ping: r[2], time: r[3], powerups: r[4], deaths: r[5], wins: r[6], losses: r[7] }));
	const numScores = cg.scores.length;

	// who killed him, or in the intermission of a duel who won the round or who fought
	const duelists = String(state.duelists || '').split('|').map(v => (v === '' ? -1 : parseInt(v, 10) || 0));
	if (duel && cgs.duelWinner !== -1 && intermission) centre(name(cgs.duelWinner) + '^7 ' + hudString('MP_INGAME', 'DUEL_WINS'), 40);
	else if (duel && duelists.length > 1 && duelists[0] !== -1 && duelists[1] !== -1 && intermission) {
		const versus = name(duelists[0]) + '^7 ' + hudString('MP_INGAME', 'SPECHUD_VERSUS') + ' ' + name(duelists[1]);
		centre(cgs.gametype === HUD_GT.POWERDUEL && duelists.length > 2 && duelists[2] !== -1 ? versus + '^7 ' + hudString('MP_INGAME', 'AND') + ' ' + name(duelists[2]) : versus, 40);
	} else if (B.killerName) centre(hudString('MP_INGAME', 'KILLEDBY') + ' ' + B.killerName, 40);

	// his place, or which team leads
	if (cgs.gametype === HUD_GT.POWERDUEL) {
		// nothing
	} else if (cgs.gametype < HUD_GT.TEAM) {
		if (ps.persistant[HUD_PERS.TEAM] !== HUD_TEAM_SPECTATOR) {
			hudDrawProportionalString(320, 60, hudPlaceString(ps.persistant[HUD_PERS.RANK] + 1) + ' ' + hudString('MP_INGAME', 'PLACE') + ' (' + hudString('MP_INGAME', 'OF') + ' ' + numScores + ') ' +
				hudString('MP_INGAME', 'WITH') + ' ' + ps.persistant[HUD_PERS.SCORE], HUD_UI_CENTER | HUD_UI_DROPSHADOW, white);
		}
	} else if (cgs.gametype !== HUD_GT.SIEGE) {
		const red = state.scores.red, blue = state.scores.blue;
		centre(red === blue ? hudString('MP_INGAME', 'TIEDAT') + ' ' + red : red >= blue ? hudString('MP_INGAME', 'RED_LEADS') + ', ' + red + ' / ' + blue : hudString('MP_INGAME', 'BLUE_LEADS') + ', ' + blue + ' / ' + red, 60);
	}

	// the head of the table
	let y = HUD_SB.HEADER;
	hudPic(HUD_SB.X - 40, y - 5, HUD_SB.WIDTH + 80, 40, hudShader('gfx/menus/menu_buttonback'));
	const head = (x, s) => hudTextPaint(x, y, 1.0, white, s, 0, 0, HUD_TEXTSTYLE.OUTLINED, HUD_FONT_MEDIUM);
	head(HUD_SB.NAME_X, hudString('MP_INGAME', 'NAME'));
	head(HUD_SB.SCORE_X, hudString('MP_INGAME', duel ? 'W_L' : 'SCORE'));
	head(HUD_SB.PING_X, hudString('MP_INGAME', 'PING'));
	head(HUD_SB.TIME_X, hudString('MP_INGAME', 'TIME'));

	// more clients than there is room for at the usual height: lower lines
	y = HUD_SB.TOP;
	const narrow = numScores > HUD_SB.MAXCLIENTS_NORMAL, maxClients = narrow ? HUD_SB.MAXCLIENTS_INTER : HUD_SB.MAXCLIENTS_NORMAL;
	const lineHeight = narrow ? HUD_SB.INTER_HEIGHT : HUD_SB.NORMAL_HEIGHT, topBorderSize = 8, bottomBorderSize = narrow ? 16 : 8;
	B.localClient = false;
	if (cgs.gametype >= HUD_GT.TEAM) {
		// The team that leads first. With 9 or more on the other team it gets ten lines at least, the other team
		// what is left.
		y += Math.trunc(lineHeight / 2);
		const first = state.scores.red >= state.scores.blue ? HUD_TEAM_RED : HUD_TEAM_BLUE, second = first === HUD_TEAM_RED ? HUD_TEAM_BLUE : HUD_TEAM_RED;
		let team1MaxCl = hudTeamScoreboard(cg, y, first, fade, maxClients, lineHeight, true);
		const team2Count = hudTeamScoreboard(cg, y, second, fade, maxClients, lineHeight, true);
		if (team1MaxCl > 10 && team1MaxCl + team2Count > maxClients) team1MaxCl = Math.max(10, team1MaxCl - team2Count);
		const team2MaxCl = maxClients - team1MaxCl;
		for (const [team, most] of [[first, team1MaxCl], [second, team2MaxCl]]) {
			const n = hudTeamScoreboard(cg, y, team, fade, most, lineHeight, true);
			// CG_DrawTeamBackground
			hudFillRect(HUD_SB.X - 5, y - topBorderSize, 640 - HUD_SB.X * 2 + 10, n * lineHeight + bottomBorderSize, team === HUD_TEAM_RED ? [1, 0.2, 0.2, 0.33] : [0.2, 0.2, 1, 0.33]);
			hudTeamScoreboard(cg, y, team, fade, most, lineHeight, false);
			y += n * lineHeight + HUD_BIGCHAR_HEIGHT;
		}
		y += hudTeamScoreboard(cg, y, HUD_TEAM_SPECTATOR, fade, maxClients, lineHeight, false) * lineHeight + HUD_BIGCHAR_HEIGHT;
	} else {
		const n1 = hudTeamScoreboard(cg, y, HUD_TEAM_FREE, fade, maxClients, lineHeight, false);
		y += n1 * lineHeight + HUD_BIGCHAR_HEIGHT;
		y += hudTeamScoreboard(cg, y, HUD_TEAM_SPECTATOR, fade, maxClients - n1, lineHeight, false) * lineHeight + HUD_BIGCHAR_HEIGHT;
	}
	// the one who looks at the bottom, when there was no room for him
	if (!B.localClient) {
		const own = cg.scores.find(s => s.client === ps.clientNum);
		if (own) hudDrawClientScore(cg, y, own, fade, lineHeight === HUD_SB.NORMAL_HEIGHT);
	}
	return true;
}

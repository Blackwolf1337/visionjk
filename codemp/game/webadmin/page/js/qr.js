// ---- a QR code for a short text: the address an authenticator app scans to take the secret of the second
// step of the login (js/account.js). Bytes, error correction M, versions 1 to 10 (213 bytes), which is more than
// such an address has. ISO/IEC 18004: the data and its Reed-Solomon bytes in blocks, laid out in a zigzag from
// the lower right corner, under the mask that makes the calmest picture.

// [bytes of error correction per block, blocks, data bytes of each, more blocks, data bytes of those]
const QR_BLOCKS = [null, [10, 1, 16, 0, 0], [16, 1, 28, 0, 0], [26, 1, 44, 0, 0], [18, 2, 32, 0, 0], [24, 2, 43, 0, 0],
	[16, 4, 27, 0, 0], [18, 4, 31, 0, 0], [22, 2, 38, 2, 39], [22, 3, 36, 2, 37], [26, 4, 43, 1, 44]];
const QR_ALIGN = [null, [], [6, 18], [6, 22], [6, 26], [6, 30], [6, 34], [6, 22, 38], [6, 24, 42], [6, 26, 46], [6, 28, 50]];

// the field of 256 elements the Reed-Solomon code works in
const QR_EXP = new Uint8Array(512), QR_LOG = new Uint8Array(256);
for (let i = 0, x = 1; i < 255; i++) {
	QR_EXP[i] = QR_EXP[i + 255] = x;
	QR_LOG[x] = i;
	x <<= 1;
	if (x & 256) x ^= 0x11d;
}
function qrMul(a, b) { return a && b ? QR_EXP[QR_LOG[a] + QR_LOG[b]] : 0; }

// the bytes of error correction for the data of one block
function qrEcc(data, count) {
	const gen = new Uint8Array(count + 1);
	gen[0] = 1;
	for (let i = 0; i < count; i++) {
		for (let k = i + 1; k > 0; k--) gen[k] = gen[k - 1] ^ qrMul(gen[k], QR_EXP[i]);
		gen[0] = qrMul(gen[0], QR_EXP[i]);
	}
	// gen[] is lowest power first: the division takes it from its top
	const rest = new Uint8Array(count);
	for (const byte of data) {
		const factor = byte ^ rest[0];
		rest.copyWithin(0, 1);
		rest[count - 1] = 0;
		for (let k = 0; k < count; k++) rest[k] ^= qrMul(gen[count - 1 - k], factor);
	}
	return rest;
}

// the remainder of a division of bits, for the format and the version: value shifted left by the degree of poly
function qrBch(value, poly) {
	let degree = 0;
	for (let p = poly; p > 1; p >>= 1) degree++;
	let rest = value << degree;
	for (let bit = 31; bit >= degree; bit--) {
		if (rest & (1 << bit)) rest ^= poly << (bit - degree);
	}
	return (value << degree) | rest;
}

// The modules of the code for a text: an array of rows of true (dark) and false, or null if the text is too
// long for version 10.
function qrMatrix(text) {
	const bytes = new TextEncoder().encode(text);
	let version = 0, blocks = null, capacity = 0;
	for (let v = 1; v <= 10; v++) {
		const b = QR_BLOCKS[v];
		capacity = b[1] * b[2] + b[3] * b[4];
		// four bits for the mode, 8 or 16 for the length
		if (bytes.length + (v < 10 ? 2 : 3) <= capacity) { version = v; blocks = b; break; }
	}
	if (!version) return null;

	// the data: mode, length, the bytes, an end of up to four zero bits, then filled up
	const bits = [];
	const put = (value, count) => { for (let i = count - 1; i >= 0; i--) bits.push((value >> i) & 1); };
	put(4, 4);
	put(bytes.length, version < 10 ? 8 : 16);
	for (const b of bytes) put(b, 8);
	put(0, Math.min(4, capacity * 8 - bits.length));
	while (bits.length & 7) bits.push(0);
	const data = new Uint8Array(capacity);
	for (let i = 0; i < bits.length; i++) data[i >> 3] |= bits[i] << (7 - (i & 7));
	for (let i = bits.length >> 3, pad = 0; i < capacity; i++, pad ^= 1) data[i] = pad ? 0x11 : 0xec;

	// in blocks, each with its error correction; then byte by byte across the blocks
	const parts = [], eccs = [];
	for (let i = 0, at = 0; i < blocks[1] + blocks[3]; i++) {
		const size = i < blocks[1] ? blocks[2] : blocks[4];
		parts.push(data.subarray(at, at + size));
		eccs.push(qrEcc(parts[i], blocks[0]));
		at += size;
	}
	const stream = [];
	for (let i = 0; i < Math.max(blocks[2], blocks[4]); i++) for (const p of parts) if (i < p.length) stream.push(p[i]);
	for (let i = 0; i < blocks[0]; i++) for (const e of eccs) stream.push(e[i]);

	const size = 17 + version * 4;
	const dark = [], fixed = [];
	for (let y = 0; y < size; y++) { dark.push(new Array(size).fill(false)); fixed.push(new Array(size).fill(false)); }
	const set = (x, y, on) => { if (x >= 0 && y >= 0 && x < size && y < size) { dark[y][x] = on; fixed[y][x] = true; } };

	// the three corners with the white line around them
	for (const [cx, cy] of [[3, 3], [size - 4, 3], [3, size - 4]]) {
		for (let dy = -4; dy <= 4; dy++) for (let dx = -4; dx <= 4; dx++) {
			const d = Math.max(Math.abs(dx), Math.abs(dy));
			set(cx + dx, cy + dy, d !== 2 && d !== 4);
		}
	}
	// the small squares, but not on a corner
	const align = QR_ALIGN[version];
	for (const cy of align) for (const cx of align) {
		if ((cx === 6 && cy === 6) || (cx === 6 && cy === size - 7) || (cx === size - 7 && cy === 6)) continue;
		for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) set(cx + dx, cy + dy, Math.max(Math.abs(dx), Math.abs(dy)) !== 1);
	}
	// the two dotted lines
	for (let i = 8; i < size - 8; i++) {
		if (!fixed[6][i]) set(i, 6, i % 2 === 0);
		if (!fixed[i][6]) set(6, i, i % 2 === 0);
	}
	// where the format goes (written with the mask), the module that is always dark, the version
	for (let i = 0; i < 9; i++) { if (!fixed[8][i]) set(i, 8, false); if (!fixed[i][8]) set(8, i, false); }
	for (let i = 0; i < 8; i++) { set(size - 1 - i, 8, false); set(8, size - 1 - i, false); }
	set(8, size - 8, true);
	if (version >= 7) {
		const info = qrBch(version, 0x1f25);
		for (let i = 0; i < 18; i++) {
			const on = ((info >> i) & 1) === 1, a = Math.floor(i / 3), b = size - 11 + i % 3;
			set(a, b, on);
			set(b, a, on);
		}
	}

	// the stream: two columns wide, upwards and downwards in turn, from the right, past the dotted column
	let at = 0;
	for (let right = size - 1, up = true; right > 0; right -= 2, up = !up) {
		if (right === 6) right--;
		for (let i = 0; i < size; i++) {
			const y = up ? size - 1 - i : i;
			for (const x of [right, right - 1]) {
				if (fixed[y][x]) continue;
				dark[y][x] = at < stream.length * 8 && ((stream[at >> 3] >> (7 - (at & 7))) & 1) === 1;
				at++;
			}
		}
	}

	const MASKS = [(x, y) => (x + y) % 2 === 0, (x, y) => y % 2 === 0, (x, y) => x % 3 === 0, (x, y) => (x + y) % 3 === 0,
		(x, y) => (Math.floor(y / 2) + Math.floor(x / 3)) % 2 === 0, (x, y) => (x * y) % 2 + (x * y) % 3 === 0,
		(x, y) => ((x * y) % 2 + (x * y) % 3) % 2 === 0, (x, y) => ((x + y) % 2 + (x * y) % 3) % 2 === 0];
	const build = mask => {
		const m = dark.map((row, y) => row.map((on, x) => (fixed[y][x] ? on : on !== MASKS[mask](x, y))));
		// the format: error correction M is 00, then the mask
		const format = qrBch(mask, 0x537) ^ 0x5412;
		for (let i = 0; i < 15; i++) {
			const on = ((format >> i) & 1) === 1;
			// around the upper left corner
			if (i < 6) m[i][8] = on; else if (i < 8) m[i + 1][8] = on; else if (i === 8) m[8][7] = on; else m[8][14 - i] = on;
			// and split over the two others
			if (i < 8) m[8][size - 1 - i] = on; else m[size - 15 + i][8] = on;
		}
		return m;
	};
	// how restless a picture is: runs of one colour, blocks, what looks like a corner, the balance of the two
	const penalty = m => {
		let score = 0, darkCount = 0;
		for (let y = 0; y < size; y++) {
			for (const line of [m[y], m.map(row => row[y])]) {
				for (let x = 0, run = 1; x < size; x++) {
					if (x + 1 < size && line[x + 1] === line[x]) { run++; continue; }
					if (run >= 5) score += run - 2;
					run = 1;
				}
				for (let x = 0; x + 10 < size; x++) {
					const s = line.slice(x, x + 11).map(v => (v ? 1 : 0)).join('');
					if (s === '10111010000' || s === '00001011101') score += 40;
				}
			}
			for (let x = 0; x < size; x++) {
				if (m[y][x]) darkCount++;
				if (y + 1 < size && x + 1 < size && m[y][x] === m[y][x + 1] && m[y][x] === m[y + 1][x] && m[y][x] === m[y + 1][x + 1]) score += 3;
			}
		}
		return score + Math.floor(Math.abs(darkCount * 20 - size * size * 10) / (size * size)) * 10;
	};
	let best = null, bestScore = Infinity;
	for (let mask = 0; mask < 8; mask++) {
		const m = build(mask), score = penalty(m);
		if (score < bestScore) { best = m; bestScore = score; }
	}
	return best;
}

// draws the code of a text onto a canvas, with the white edge a reader needs. false: the text is too long.
function qrDraw(canvas, text, scale = 5) {
	const m = qrMatrix(text);
	if (!m) return false;
	const edge = 4, size = m.length + edge * 2;
	canvas.width = canvas.height = size * scale;
	const ctx = canvas.getContext('2d');
	ctx.fillStyle = '#fff';
	ctx.fillRect(0, 0, canvas.width, canvas.height);
	ctx.fillStyle = '#000';
	m.forEach((row, y) => row.forEach((on, x) => { if (on) ctx.fillRect((x + edge) * scale, (y + edge) * scale, scale, scale); }));
	return true;
}

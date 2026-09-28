#!/usr/bin/env node
/**
 * firebase_stress.js — simulates a scorekeeper driving a match on one RTDB
 * channel, so a FIREBASE-mode board picks up external changes for hours on
 * end (each one lands in ScoreActions::applyFromDatabase() → LED::update()
 * on Core 0). Used to reproduce the tournament "frozen LEDs" bug while the
 * board's serial output is being logged.
 *
 * Usage:
 *   node tools/firebase_stress.js --channel 99 [--interval 1500] [--force]
 *
 *   --channel   match-<channel> node to write (use a scratch channel!)
 *   --interval  ms between points (default 1500; board polls every 3s by
 *               default, lower its poll interval in the portal to hit it harder)
 *   --timeouts  0..1 chance per point of starting a timeout (default 0.03)
 *   --force     write even if the channel already holds a score
 *
 * Every write is logged with an ISO timestamp so it can be lined up with
 * the serial log (pio device monitor -f time -f log2file).
 */

const DB_URL = 'https://live-scoreboard-fc0e5-default-rtdb.europe-west1.firebasedatabase.app';

function arg(name, def) {
  const i = process.argv.indexOf(`--${name}`);
  if (i === -1) return def;
  const v = process.argv[i + 1];
  return v === undefined || v.startsWith('--') ? true : v;
}

const channel     = arg('channel');
const intervalMs  = Number(arg('interval', 1500));
const timeoutProb = Number(arg('timeouts', 0.03));
const force       = arg('force', false) === true;

if (!channel || channel === true) {
  console.error('Missing --channel <n> (pick a scratch channel no real board/match uses)');
  process.exit(1);
}

const base = `${DB_URL}/match-${channel}`;
const log  = (...a) => console.log(new Date().toISOString(), ...a);

async function rtdb(method, path, body) {
  const res = await fetch(`${base}${path}.json`, {
    method,
    headers: { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  if (!res.ok) throw new Error(`${method} ${path} → HTTP ${res.status}`);
  return res.json();
}

// ── Match simulation ─────────────────────────────────────────────────────────
const WIN = 21, HARDCAP = 25, SETS_TO_WIN = 2;
let sets = [];            // completed sets: [{a, b}]
let a = 0, b = 0;
let firstServer = 'a';    // team serving first in the current set
let matchNo = 1, writes = 0, errors = 0;

function setOver() {
  const hi = Math.max(a, b), lo = Math.min(a, b);
  return hi >= HARDCAP || (hi >= WIN && hi - lo >= 2);
}

function scoreNode() {
  const node = {};
  sets.forEach((s, i) => { node[`set_${i + 1}`] = { team_a_score: s.a, team_b_score: s.b }; });
  node[`set_${sets.length + 1}`] = {
    team_a_score: a,
    team_b_score: b,
    starting_server:   firstServer === 'a' ? 'a' : 'c',
    starting_receiver: firstServer === 'a' ? 'c' : 'a',
  };
  return node;
}

async function push(note) {
  await rtdb('PATCH', '', { active_set: sets.length + 1, score: scoreNode() });
  writes++;
  log(`#${writes} match ${matchNo} set ${sets.length + 1}  ${a}-${b}  ${note}`);
}

async function step() {
  // Occasional timeout: the board shows a countdown; the next point cancels it.
  if (Math.random() < timeoutProb) {
    await rtdb('PUT', '/timer', { type: 'timeout' });
    log('timer: timeout started');
    return;
  }

  if (Math.random() < 0.5) a++; else b++;

  if (setOver()) {
    sets.push({ a, b });
    const winsA = sets.filter(s => s.a > s.b).length;
    const winsB = sets.length - winsA;
    if (winsA >= SETS_TO_WIN || winsB >= SETS_TO_WIN) {
      sets.pop();  // show the final set as the active one, not as a phantom set_3
      await push(`→ match over (${winsA}-${winsB}), resetting in 10s`);
      await new Promise(r => setTimeout(r, 10000));
      sets = []; a = 0; b = 0; matchNo++;
      firstServer = Math.random() < 0.5 ? 'a' : 'b';
      await rtdb('PUT', '/timer', null);
      await push('new match');
      return;
    }
    a = 0; b = 0;
    firstServer = firstServer === 'a' ? 'b' : 'a';
    await push('new set');
    return;
  }
  await push('');
}

// ── Main ─────────────────────────────────────────────────────────────────────
(async () => {
  const existing = await rtdb('GET', '');
  if (existing && existing.score && !force) {
    console.error(`match-${channel} already has a score:\n${JSON.stringify(existing, null, 2)}\n` +
                  'Refusing to overwrite — pick an unused channel or pass --force.');
    process.exit(1);
  }

  log(`Driving match-${channel} every ${intervalMs}ms (Ctrl+C to stop)`);
  await rtdb('PUT', '/timer', null);
  await rtdb('PATCH', '/game_settings', { win_points: WIN, hardcap: HARDCAP, set_mode: '3' });
  await push('start');

  const started = Date.now();
  process.on('SIGINT', () => {
    const mins = ((Date.now() - started) / 60000).toFixed(1);
    log(`Stopped after ${mins} min, ${writes} writes, ${errors} errors`);
    process.exit(0);
  });

  for (;;) {
    await new Promise(r => setTimeout(r, intervalMs));
    try { await step(); }
    catch (e) { errors++; log('ERROR', e.message); }
  }
})();

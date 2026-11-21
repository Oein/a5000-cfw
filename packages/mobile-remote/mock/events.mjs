// v0+ event channel for the mock camera: a minimal WebSocket server (RFC 6455 text frames only,
// no dependencies) on /api/events. Behaves the way the CFW is meant to:
//  - hello on connect (model, wall clock, battery), ping every 15 s;
//  - after every shot: photo.added, then gps.request { reqId, shotId, capturedAt };
//  - shots taken with no phone connected are queued and asked for again on the next connect;
//  - gps.response is logged as the "EXIF write" (a real camera writes it into the file).
import { createHash, randomUUID } from 'node:crypto';

const clients = new Set();
const pendingGps = new Map(); // reqId -> request
const answered = new Map(); // shotId -> response
let battery = 100;

function frame(text) {
  const payload = Buffer.from(text);
  const len = payload.length;
  const head = len < 126 ? Buffer.from([0x81, len]) : len < 65536 ? Buffer.from([0x81, 126, len >> 8, len & 255]) : null;
  if (!head) throw new Error('frame too big');
  return Buffer.concat([head, payload]);
}

function send(socket, msg) {
  if (!socket.destroyed) socket.write(frame(JSON.stringify(msg)));
}

function broadcast(msg) {
  for (const s of clients) send(s, msg);
}

/** Parses client frames (always masked). Returns [messages, rest]. */
function parseFrames(buf, onClose) {
  const out = [];
  let o = 0;
  while (buf.length - o >= 2) {
    const op = buf[o] & 0x0f;
    let len = buf[o + 1] & 0x7f;
    let p = o + 2;
    if (len === 126) {
      if (buf.length - p < 2) break;
      len = buf.readUInt16BE(p);
      p += 2;
    } else if (len === 127) {
      onClose();
      return [out, Buffer.alloc(0)];
    }
    if (buf.length - p < 4 + len) break;
    const mask = buf.subarray(p, p + 4);
    const data = Buffer.from(buf.subarray(p + 4, p + 4 + len));
    for (let i = 0; i < data.length; i++) data[i] ^= mask[i % 4];
    o = p + 4 + len;
    if (op === 0x8) onClose();
    else if (op === 0x1) out.push(data.toString());
    // ping/pong/binary ignored
  }
  return [out, buf.subarray(o)];
}

export function attachEvents(server, { model, clock }) {
  server.on('upgrade', (req, socket) => {
    if (new URL(req.url, 'http://x').pathname !== '/api/events') return socket.destroy();
    const key = req.headers['sec-websocket-key'];
    const accept = createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    clients.add(socket);
    console.log(`events: phone connected (${clients.size})`);
    send(socket, { type: 'hello', v: 1, model, clock: clock(), battery });
    // Shots taken while no phone was there
    for (const r of pendingGps.values()) send(socket, r);
    let rest = Buffer.alloc(0);
    const close = () => {
      clients.delete(socket);
      socket.destroy();
    };
    socket.on('data', (chunk) => {
      const [messages, remaining] = parseFrames(Buffer.concat([rest, chunk]), close);
      rest = remaining;
      for (const text of messages) onMessage(socket, text);
    });
    socket.on('close', () => {
      clients.delete(socket);
      console.log(`events: phone disconnected (${clients.size})`);
    });
    socket.on('error', close);
  });
  setInterval(() => broadcast({ type: 'ping', t: Date.now() }), 15_000);
  setInterval(() => {
    battery = Math.max(5, battery - 1);
    broadcast({ type: 'status', battery });
  }, 60_000);
}

function onMessage(socket, text) {
  let m;
  try {
    m = JSON.parse(text);
  } catch {
    return;
  }
  if (m.type === 'hello') console.log(`events: phone hello app=${m.app} background=${m.background}`);
  else if (m.type === 'phone.state') console.log(`events: phone ${m.background ? 'in background' : 'in foreground'}`);
  else if (m.type === 'gps.response') {
    const req = pendingGps.get(m.reqId);
    if (!req) return;
    pendingGps.delete(m.reqId);
    answered.set(req.shotId, m);
    const f = m.fix;
    console.log(
      f
        ? `EXIF ← shot ${req.shotId}: ${f.lat.toFixed(6)}, ${f.lon.toFixed(6)} ±${f.acc ?? '?'}m (${m.source})`
        : `EXIF ← shot ${req.shotId}: no position (${m.reason})`,
    );
  }
}

/** Called by the shutter handler. */
export function shotTaken(item) {
  broadcast({ type: 'photo.added', item });
  const req = { type: 'gps.request', reqId: randomUUID(), shotId: item.id, capturedAt: item.date };
  pendingGps.set(req.reqId, req);
  broadcast(req);
  if (clients.size === 0) console.log(`events: shot ${item.id} queued for GPS (no phone)`);
}

export function gpsAnswerFor(shotId) {
  return answered.get(shotId) ?? null;
}

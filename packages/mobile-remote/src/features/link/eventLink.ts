import { backoffMs, LINK_SILENCE_MS, parseEvent, PROTOCOL_VERSION, type CameraEvent, type PhoneMessage } from './protocol';

export type LinkState = 'idle' | 'connecting' | 'open' | 'waiting';

export type LinkHandlers = {
  onEvent: (e: CameraEvent) => void;
  onState: (s: LinkState, detail?: string) => void;
};

/**
 * One WebSocket to the camera's event endpoint, kept up for as long as the session wants it:
 * reconnects with backoff, treats LINK_SILENCE_MS without any frame as dead (a socket can look
 * open for minutes after Wi-Fi drops), and answers pings. No React here — it runs the same with
 * the app in the background (kept alive there by the location background mode).
 */
export class EventLink {
  private ws: WebSocket | null = null;
  private url: string;
  private handlers: LinkHandlers;
  private wanted = false;
  private failures = 0;
  private retryTimer: ReturnType<typeof setTimeout> | null = null;
  private watchdog: ReturnType<typeof setInterval> | null = null;
  private lastFrame = 0;
  private background = false;
  state: LinkState = 'idle';

  constructor(url: string, handlers: LinkHandlers) {
    this.url = url;
    this.handlers = handlers;
  }

  start() {
    if (this.wanted) return;
    this.wanted = true;
    this.failures = 0;
    this.connect();
    this.watchdog = setInterval(() => {
      if (this.state === 'open' && Date.now() - this.lastFrame > LINK_SILENCE_MS) this.drop('silent');
    }, 5000);
  }

  stop() {
    this.wanted = false;
    if (this.retryTimer) clearTimeout(this.retryTimer);
    if (this.watchdog) clearInterval(this.watchdog);
    this.retryTimer = null;
    this.watchdog = null;
    const ws = this.ws;
    this.ws = null;
    ws?.close();
    this.setState('idle');
  }

  /** Reconnect now (e.g. the app came to the foreground, or the host changed). */
  kick() {
    if (!this.wanted || this.state === 'open' || this.state === 'connecting') return;
    if (this.retryTimer) clearTimeout(this.retryTimer);
    this.failures = 0;
    this.connect();
  }

  send(message: PhoneMessage) {
    if (this.ws && this.state === 'open') this.ws.send(JSON.stringify(message));
    return this.state === 'open';
  }

  setBackground(background: boolean) {
    this.background = background;
    this.send({ type: 'phone.state', background });
  }

  private setState(s: LinkState, detail?: string) {
    this.state = s;
    this.handlers.onState(s, detail);
  }

  private connect() {
    this.retryTimer = null;
    this.setState('connecting');
    let ws: WebSocket;
    try {
      ws = new WebSocket(this.url);
    } catch (e) {
      this.scheduleRetry(String(e));
      return;
    }
    this.ws = ws;
    ws.onopen = () => {
      if (this.ws !== ws) return;
      this.failures = 0;
      this.lastFrame = Date.now();
      this.setState('open');
      ws.send(JSON.stringify({ type: 'hello', v: PROTOCOL_VERSION, app: 'a5k-mobile-remote', background: this.background } satisfies PhoneMessage));
    };
    ws.onmessage = (m) => {
      if (this.ws !== ws) return;
      this.lastFrame = Date.now();
      const event = typeof m.data === 'string' ? parseEvent(m.data) : null;
      if (!event) return;
      if (event.type === 'ping') {
        this.send({ type: 'pong', t: event.t });
        return;
      }
      this.handlers.onEvent(event);
    };
    ws.onerror = () => {
      // onclose follows
    };
    ws.onclose = (e) => {
      if (this.ws !== ws) return;
      this.ws = null;
      this.scheduleRetry(e.reason || `closed (${e.code})`);
    };
  }

  private drop(reason: string) {
    const ws = this.ws;
    this.ws = null;
    ws?.close();
    this.scheduleRetry(reason);
  }

  private scheduleRetry(reason: string) {
    if (!this.wanted) return;
    const delay = backoffMs(this.failures++);
    this.setState('waiting', reason);
    this.retryTimer = setTimeout(() => this.connect(), delay);
  }
}

export function eventsUrl(host: string, port = 8080) {
  return `ws://${host}:${port}/api/events`;
}

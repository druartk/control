export const PROTOCOL_VERSION = 1;
export const MAX_FRAME = 8192;
const PREFIX = '@@CFG:';

export class LineFramer {
  constructor(onLine, onError = () => {}) { this.onLine = onLine; this.onError = onError; this.buffer = ''; this.discard = false; this.decoder = new TextDecoder(); }
  feed(chunk) {
    this.buffer += typeof chunk === 'string' ? chunk : this.decoder.decode(chunk, { stream: true });
    let newline;
    while ((newline = this.buffer.indexOf('\n')) >= 0) {
      const line = this.buffer.slice(0, newline).replace(/\r$/, '');
      this.buffer = this.buffer.slice(newline + 1);
      if (!this.discard && line.startsWith(PREFIX)) {
        const payload = line.slice(PREFIX.length);
        if (payload.length <= MAX_FRAME - PREFIX.length) this.onLine(payload);
        else this.onError('Trame trop longue');
      }
      this.discard = false;
    }
    if (new TextEncoder().encode(this.buffer).length > MAX_FRAME) { this.buffer = ''; this.discard = true; this.onError('Ligne série trop longue'); }
  }
}

export class ProtocolClient {
  constructor() { this.port = null; this.reader = null; this.writer = null; this.nextId = 1; this.pending = new Map(); this.helloWait = null; this.connecting = null; this.cleaning = false; this.framer = new LineFramer(line => this.receive(line), error => this.onEvent?.({ type: 'protocol_error', message: error })); }
  async connect() {
    if (this.connecting) return this.connecting;
    this.connecting = this.connectImpl().finally(() => { this.connecting = null; });
    return this.connecting;
  }
  async connectImpl() {
    if (!('serial' in navigator)) throw new Error('Web Serial est disponible dans Chrome ou Edge');
    if (this.port || this.reader || this.writer) await this.disconnect();
    this.onEvent?.({ type: 'serial_connect_requested' });
    const port = await navigator.serial.requestPort();
    this.port = port;
    try {
      await port.open({ baudRate: 115200, dataBits: 8, parity: 'none', stopBits: 1, flowControl: 'none' });
      this.onEvent?.({ type: 'port_opened' });
      this.reader = port.readable.getReader();
      this.readLoop();
      const hello = await new Promise(async (resolve, reject) => { const timer = setTimeout(() => { this.helloWait = null; reject(new Error('Délai hello dépassé')); }, 5000); this.helloWait = { resolve: value => { clearTimeout(timer); this.helloWait = null; resolve(value); }, reject }; try { await this.sendRaw({ cmd: 'hello', protocol: PROTOCOL_VERSION }, true); } catch (error) { clearTimeout(timer); this.helloWait = null; reject(error); } });
      if (hello.protocol !== PROTOCOL_VERSION) throw new Error(`Protocole incompatible: ESP32=${hello.protocol}, outil=${PROTOCOL_VERSION}`);
      return hello;
    } catch (error) {
      await this.disconnect();
      throw error;
    }
  }
  async readLoop() {
    let error = null;
    try { while (this.reader) { const { value, done } = await this.reader.read(); if (done) break; this.framer.feed(value); } }
    catch (caught) { error = caught; }
    finally {
      const lost = Boolean(this.port) && !this.cleaning;
      await this.cleanup();
      if (lost) this.onEvent?.({ type: 'connection_lost', error: error || new Error('Connexion série perdue') });
    }
  }
  async handleSerialDisconnect(port) { if (port !== this.port) return; await this.disconnect(); this.onEvent?.({ type: 'connection_lost', error: new Error('USB série déconnecté') }); }
  receive(payload) {
    let message;
    try { message = JSON.parse(payload); } catch { this.onEvent?.({ type: 'protocol_error', message: 'JSON ESP32 invalide' }); return; }
    console.info('CONTROL_UI:', 'rx', message.type || message.cmd || 'message', message.request_id ?? 0);
    if (message.request_id && this.pending.has(message.request_id)) {
      const entry = this.pending.get(message.request_id);
      if (message.type === 'command_queued') { this.onEvent?.(message); return; }
      clearTimeout(entry.timer); this.pending.delete(message.request_id);
      if (message.type === 'error') entry.reject(new Error(`${message.code}: ${message.message}`)); else entry.resolve(message);
    } else if (message.type === 'hello' && this.helloWait) this.helloWait.resolve(message);
    else this.onEvent?.(message);
  }
  async sendRaw(command, handshake = false) {
    const id = handshake ? 0 : (command.request_id ?? this.nextId++);
    if (!handshake) command.request_id = id;
    const line = `${PREFIX}${JSON.stringify(command)}\n`;
    if (new TextEncoder().encode(line).length > MAX_FRAME) throw new Error('Commande trop volumineuse');
    if (!this.writer) this.writer = this.port.writable.getWriter();
    try { await this.writer.write(new TextEncoder().encode(line)); }
    catch (error) { await this.disconnect(); this.onEvent?.({ type: 'connection_lost', error }); throw error; }
    console.info('CONTROL_UI:', `request=${command.cmd}`, `request_id=${id}`, 'timeout_ms=handshake');
    if (handshake) return;
  }
  request(cmd, params = {}, timeout = 10000) {
    const request = { cmd, ...params, request_id: this.nextId++ };
    console.info('CONTROL_UI:', `request=${cmd}`, `request_id=${request.request_id}`, `timeout_ms=${timeout}`);
    return new Promise(async (resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(request.request_id); console.warn('CONTROL_UI:', `timeout request=${cmd}`, `request_id=${request.request_id}`, `timeout_ms=${timeout}`); reject(new Error(`Délai dépassé: ${cmd}`)); }, timeout);
      this.pending.set(request.request_id, { resolve, reject, timer });
      try { await this.sendRaw(request); } catch (error) { clearTimeout(timer); this.pending.delete(request.request_id); reject(error); }
    });
  }
  async disconnect() { this.cleaning = true; try { this.rejectAll(new Error('Déconnecté')); await this.cleanup(); } finally { this.cleaning = false; } }
  async cleanup() { const reader = this.reader; const writer = this.writer; const port = this.port; this.reader = null; this.writer = null; this.port = null; this.helloWait?.reject(new Error('Connexion série perdue')); this.helloWait = null; if (reader) { await reader.cancel().catch(() => {}); try { reader.releaseLock(); } catch {} } if (writer) { try { writer.releaseLock(); } catch {} } if (port) await port.close().catch(() => {}); this.rejectAll(new Error('Connexion série perdue')); this.onEvent?.({ type: 'cleanup' }); }
  rejectAll(error) { for (const entry of this.pending.values()) { clearTimeout(entry.timer); entry.reject(error); } this.pending.clear(); }
  onEvent = null;
}

export function protocolSelfTest() {
  const messages = []; const errors = []; const framer = new LineFramer(x => messages.push(JSON.parse(x)), x => errors.push(x));
  framer.feed('log\n@@CFG:{"type":"one"}\n@@'); framer.feed('CFG:{"type":"two"}\n');
  if (messages.length !== 2 || errors.length !== 0) throw new Error('LineFramer self-test failed');
  return true;
}

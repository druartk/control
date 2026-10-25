import { MAX_FRAME, PROTOCOL_VERSION } from './protocol.js';

export class WifiProtocolClient {
  constructor() { this.nextId = 1; this.accessCode = ''; this.requests = new Set(); this.onEvent = null; }
  async connect(accessCode) {
    await this.disconnect();
    this.accessCode = accessCode.trim();
    if (!/^[0-9a-f]{16}$/.test(this.accessCode)) throw new Error('Saisissez le code Wi-Fi affiché dans Configuration sur l’ESP32.');
    const hello = await this.request('hello', { protocol: PROTOCOL_VERSION });
    if (hello.protocol !== PROTOCOL_VERSION) throw new Error(`Protocole incompatible : ESP32=${hello.protocol}`);
    return hello;
  }
  async request(cmd, params = {}, timeout = 10000) {
    const request = { cmd, ...params, request_id: this.nextId++ };
    const body = JSON.stringify(request);
    if (new TextEncoder().encode(body).length > MAX_FRAME - 7) throw new Error('Commande trop volumineuse');
    const controller = new AbortController();
    this.requests.add(controller);
    const timer = setTimeout(() => controller.abort(), timeout);
    try {
      const response = await fetch('/api/config', {
        method: 'POST', headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${this.accessCode}` },
        body, signal: controller.signal, cache: 'no-store',
      });
      if (response.status === 401) throw new Error('Code d’accès Wi-Fi incorrect.');
      if (!response.ok) throw new Error(`ESP32 indisponible (${response.status}). Réessayez la connexion.`);
      const message = await response.json();
      if (message.request_id !== request.request_id) throw new Error('Réponse ESP32 inattendue.');
      if (message.type === 'error') throw new Error(`${message.code}: ${message.message}`);
      return message;
    } catch (error) {
      if (error.name === 'AbortError') throw new Error(`Délai dépassé: ${cmd}`);
      if (error instanceof TypeError) this.onEvent?.({ type: 'connection_lost', error });
      throw error;
    } finally { clearTimeout(timer); this.requests.delete(controller); }
  }
  async disconnect() {
    for (const controller of this.requests) controller.abort();
    this.requests.clear(); this.accessCode = '';
  }
}

import assert from 'node:assert/strict';
import { WifiProtocolClient } from '../configurator/wifi.js';
import { runTests } from '../configurator/tests.js';

const originalFetch = globalThis.fetch;
const code = '0123456789abcdef';
const client = new WifiProtocolClient();
const requests = [];
globalThis.fetch = async (url, options) => {
  assert.equal(url, '/api/config');
  assert.equal(options.headers.Authorization, `Bearer ${code}`);
  assert.equal(options.headers['Content-Type'], 'application/json');
  const request = JSON.parse(options.body);
  requests.push(request);
  return { ok: true, status: 200, json: async () => ({
    type: request.cmd === 'hello' ? 'hello' : 'status', protocol: 1, request_id: request.request_id,
  }) };
};
try {
  await assert.rejects(client.connect(''), /code Wi-Fi/);
  assert.equal((await client.connect(code)).type, 'hello');
  const results = await Promise.all([client.request('get_devices'), client.request('get_configs')]);
  assert.notEqual(results[0].request_id, results[1].request_id);
  assert.equal(client.requests.size, 0);
  assert.equal(new Set(requests.map(request => request.request_id)).size, requests.length);
  await assert.rejects(client.request('save_config', { name: 'é'.repeat(8192) }), /volumineuse/);
  globalThis.fetch = async () => ({ ok: false, status: 401 });
  await assert.rejects(client.request('get_devices'), /Code d’accès Wi-Fi incorrect/);
  globalThis.fetch = async () => ({ ok: false, status: 503 });
  await assert.rejects(client.request('save_config'), /503/);
  globalThis.fetch = async () => ({ ok: true, json: async () => ({ request_id: -1 }) });
  await assert.rejects(client.request('get_status'), /inattendue/);
  globalThis.fetch = async (_, options) => ({ ok: true, json: async () => ({
    request_id: JSON.parse(options.body).request_id, type: 'error', code: 'INVALID_CONFIG', message: 'Pièce inconnue',
  }) });
  await assert.rejects(client.request('save_config'), /INVALID_CONFIG: Pièce inconnue/);
  let disconnected = false;
  client.onEvent = event => { disconnected ||= event.type === 'connection_lost'; };
  globalThis.fetch = async () => { throw new TypeError('Network lost'); };
  await assert.rejects(client.request('get_status'), /Network lost/);
  assert(disconnected);
  globalThis.fetch = (_, options) => new Promise((_, reject) => {
    options.signal.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')));
  });
  await assert.rejects(client.request('get_status', {}, 1), /Délai dépassé/);
  assert.equal(client.requests.size, 0);
  const pending = client.request('get_status');
  const rejected = assert.rejects(pending, /Délai dépassé/);
  await client.disconnect();
  await rejected;
  assert.equal(client.requests.size, 0);
  assert.equal(client.accessCode, '');
  assert.equal(runTests().passed, true);
  console.log('PASS: Wi-Fi JSON client, authentication errors, bounded requests, timeouts, disconnects and existing USB/model tests.');
} finally { globalThis.fetch = originalFetch; }

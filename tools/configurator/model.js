export const SCHEMA = 1;
export const MAX_ROOMS = 16;
export const MAX_BINDINGS = 32;
export const MAX_NAME = 47;

export function clone(value) { return JSON.parse(JSON.stringify(value)); }
export function emptyConfig(id = 1, name = 'Maison') { return { id, name, schema: SCHEMA, revision: 0, rooms: [], bindings: [] }; }
export function deviceIdentity(deviceOrNode, endpoint) {
  const node = typeof deviceOrNode === 'object' ? deviceOrNode?.node_id : deviceOrNode;
  const value = typeof deviceOrNode === 'object' ? deviceOrNode?.endpoint : endpoint;
  return `${node ?? ''}:${Number(value)}`;
}
export function bindingFor(config, nodeId, endpoint) { return config.bindings.find(binding => deviceIdentity(binding.node_id, binding.endpoint) === deviceIdentity(nodeId, endpoint)); }
export function deviceBinding(config, device) { return bindingFor(config, device?.node_id, device?.endpoint); }
export function displayName(device) { return device?.name?.trim() || `Endpoint ${device?.endpoint}`; }
export function reachability(device) { return device?.reachable === true ? 'reachable' : device?.reachable === false ? 'unreachable' : 'unknown'; }
export function outputState(device) { return device?.supports_onoff !== true ? 'unsupported' : device.onoff === true ? 'on' : device.onoff === false ? 'off' : 'unknown'; }
export function classification(device) {
  if (device?.supports_switch && !device?.supports_onoff) return 'Interrupteur';
  if (device?.supports_onoff && (device?.supports_color || device?.supports_level)) return 'Lumière dimmable';
  if (device?.supports_onoff) return device?.type_label?.includes('Prise') || device?.type_label?.includes('Plug') ? 'Prise / sortie' : 'On/Off';
  return 'Autre endpoint Matter';
}
export function commandState(result) { return !result?.success ? 'command_failed' : result?.state === true ? 'confirmed_on' : result?.state === false ? 'confirmed_off' : 'unknown'; }
export function roomName(config, id) { return config?.rooms.find(room => Number(room.id) === Number(id))?.name ?? 'Non assigné'; }
export function scanSummary(scan, knownCount, now = Date.now()) {
  const phase = ({ connecting: 'Connexion au pont', reading_root_descriptor: 'Lecture du nœud Matter', reading_endpoints: 'Lecture des endpoints' }[scan?.status?.phase] || 'Analyse Matter');
  const seconds = Math.max(0, Math.floor((now - (scan?.startedAt || now)) / 1000));
  return `${phase}… ${scan?.status?.endpoint_count || knownCount} appareils connus · Temps écoulé : ${Math.floor(seconds / 60)} min ${String(seconds % 60).padStart(2, '0')} s`;
}

export function deduplicateDevices(devices) {
  const seen = new Set();
  return devices.filter(device => {
    const key = deviceIdentity(device);
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

function normalizeSearch(value) { return String(value ?? '').trim().toLocaleLowerCase('fr'); }

export function filterDevices(devices, config, { search = '', status = 'all', roomId = 'all' } = {}) {
  const query = normalizeSearch(search);
  return devices.filter(device => {
    const binding = deviceBinding(config, device);
    const assigned = Boolean(binding?.configured && binding.room_id);
    const room = assigned ? roomName(config, binding.room_id) : 'Non assigné';
    const state = reachability(device);
    const searchable = `${displayName(device)} ${device.endpoint} ${room}`.toLocaleLowerCase('fr');
    if (query && !searchable.includes(query)) return false;
    if (status === 'assigned' && !assigned) return false;
    if (status === 'unassigned' && assigned) return false;
    if (status === 'reachable' && state !== 'reachable') return false;
    if (status === 'unreachable' && state !== 'unreachable') return false;
    if (status === 'unknown' && state !== 'unknown') return false;
    if (roomId !== 'all' && (roomId === 'unassigned' ? assigned : Number(binding?.room_id) !== Number(roomId))) return false;
    return true;
  });
}

export function sortDevices(devices, config, sort = 'name') {
  const collator = new Intl.Collator('fr', { sensitivity: 'base', numeric: true });
  return [...devices].sort((left, right) => {
    const leftBinding = deviceBinding(config, left);
    const rightBinding = deviceBinding(config, right);
    let result;
    if (sort === 'endpoint') result = Number(left.endpoint) - Number(right.endpoint);
    else if (sort === 'room') result = collator.compare(roomName(config, leftBinding?.configured ? leftBinding.room_id : 0), roomName(config, rightBinding?.configured ? rightBinding.room_id : 0));
    else if (sort === 'reachability') result = ({ reachable: 0, unknown: 1, unreachable: 2 }[reachability(left)] - { reachable: 0, unknown: 1, unreachable: 2 }[reachability(right)]);
    else if (sort === 'controllable') result = Number(right.supports_onoff === true) - Number(left.supports_onoff === true);
    else result = collator.compare(displayName(left), displayName(right));
    return result || Number(left.endpoint) - Number(right.endpoint) || collator.compare(String(left.node_id), String(right.node_id));
  });
}

export function assignDevices(config, devices, selectedIds, roomId) {
  const selected = new Set(selectedIds);
  const targets = devices.filter(device => selected.has(deviceIdentity(device)));
  const additions = Number(roomId) ? targets.filter(device => !deviceBinding(config, device)).length : 0;
  if (config.bindings.length + additions > MAX_BINDINGS) return { ok: false, updated: 0, message: `Limite atteinte : ${MAX_BINDINGS} appareils configurables maximum.` };
  let updated = 0;
  for (const device of targets) {
    let binding = deviceBinding(config, device);
    if (!Number(roomId)) {
      if (binding) {
        config.bindings.splice(config.bindings.indexOf(binding), 1);
        updated++;
      }
      continue;
    }
    if (!binding) {
      binding = { node_id: device.node_id, endpoint: Number(device.endpoint), room_id: 0, display_name: displayName(device), order: config.bindings.length, configured: false };
      config.bindings.push(binding);
    }
    binding.room_id = Number(roomId) || 0;
    binding.configured = binding.room_id !== 0;
    updated++;
  }
  return { ok: true, updated };
}

export function normalizeConfig(raw) {
  const config = clone(raw); config.rooms ??= []; config.bindings ??= [];
  config.rooms.sort((a, b) => (a.order ?? 0) - (b.order ?? 0)); config.bindings.sort((a, b) => (a.order ?? 0) - (b.order ?? 0)); return config;
}

export function mergeKnownDevices(current, known) {
  const result = deduplicateDevices(current);
  for (const device of known) {
    const index = result.findIndex(item => deviceIdentity(item) === deviceIdentity(device));
    if (index < 0) result.push({ ...device, reachable: false });
    else if (!result[index].name && device.name) result[index] = { ...device, ...result[index], name: device.name };
  }
  return deduplicateDevices(result);
}

export function validateConfig(config, devices = [], expectedNode = null) {
  const fail = message => ({ ok: false, message });
  if (!config || config.schema !== SCHEMA || !Number.isInteger(config.id) || config.id < 1) return fail('Schéma ou identifiant invalide');
  if (typeof config.name !== 'string' || !config.name.trim() || config.name.length > 31) return fail('Nom de configuration invalide');
  if (!Array.isArray(config.rooms) || config.rooms.length > MAX_ROOMS || !Array.isArray(config.bindings) || config.bindings.length > MAX_BINDINGS) return fail('Trop de pièces ou d’appareils');
  const rooms = new Set(); for (const room of config.rooms) { if (!Number.isInteger(room.id) || room.id <= 0 || rooms.has(room.id) || typeof room.name !== 'string' || room.name.length > 31) return fail('Pièce invalide'); rooms.add(room.id); }
  const endpoints = new Set();
  for (const binding of config.bindings) {
    const key = deviceIdentity(binding.node_id, binding.endpoint);
    if (!binding.node_id || !Number.isInteger(Number(binding.endpoint)) || Number(binding.endpoint) <= 0 || endpoints.has(key)) return fail('Endpoint dupliqué ou invalide');
    endpoints.add(key);
    if (typeof binding.display_name !== 'string' || binding.display_name.length > MAX_NAME) return fail('Nom d’appareil invalide');
    if (binding.configured && (!rooms.has(binding.room_id) || !binding.room_id)) return fail('Référence de pièce invalide');
    if (expectedNode && binding.node_id !== expectedNode) return fail('Le node ID ne correspond pas');
  }
  if (devices.length) for (const binding of config.bindings) if (binding.configured && !devices.some(device => deviceIdentity(device) === deviceIdentity(binding.node_id, binding.endpoint))) return fail(`Endpoint ${binding.endpoint} introuvable`);
  return { ok: true };
}

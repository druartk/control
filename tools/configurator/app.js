import { ProtocolClient } from './protocol.js';
import { WifiProtocolClient } from './wifi.js';
import { assignDevices, classification, commandState, deviceBinding, deviceIdentity, displayName, emptyConfig, filterDevices, mergeKnownDevices, normalizeConfig, outputState, roomName, scanSummary, sortDevices, validateConfig, reachability } from './model.js';

const $ = id => document.getElementById(id);
const wifiMode = !['localhost', '127.0.0.1', '[::1]'].includes(location.hostname);
const client = wifiMode ? new WifiProtocolClient() : new ProtocolClient();
const DEVICE_PAGE_SIZE = 16;
const state = {
  hello: null, devices: [], configs: [], activeId: 0, config: null, roomId: 'all', search: '', statusFilter: 'all', sort: 'name',
  selected: new Set(), dirty: false, lastSaved: '', loadingDevices: null, scan: null, view: 'normal', pending: new Map(),
};

client.onEvent = message => {
  console.info('CONTROL_UI:', message.type, message.error?.message || message.message || '');
  if (message.type === 'connection_lost') { state.hello = null; setConnection(false, 'ESP32 déconnecté — reconnectez-le.'); }
  else if (message.type === 'protocol_error') setStatus(message.message, true);
  else if (message.type === 'command_queued') setStatus(`Commande endpoint ${message.endpoint} en cours…`);
};

if (!wifiMode && 'serial' in navigator) navigator.serial.addEventListener('disconnect', event => client.handleSerialDisconnect(event.port));
$('wifi-credentials').hidden = !wifiMode;
$('connect').textContent = wifiMode ? 'Connecter en Wi-Fi' : 'Connecter en USB';
$('access-code').onkeydown = event => { if (event.key === 'Enter') connect(); };

function setConnection(connected, message = '') {
  $('connection').textContent = connected ? 'Connecté' : 'Déconnecté';
  $('connection').className = `badge ${connected ? 'on' : 'off'}`;
  if (message) setStatus(message, !connected);
}
function setStatus(message, error = false) { $('status').textContent = message; $('status').className = `status${error ? ' error' : ''}`; }
function requireConnection() { if (!state.hello) throw new Error('Connectez d’abord l’ESP32'); }
function markDirty(message = 'Modifications locales non enregistrées.') { state.dirty = true; render(); if (message) setStatus(message); }
function confirmDiscard() { return !state.dirty || confirm('Des modifications locales ne sont pas enregistrées. Continuer ?'); }
function currentConfig() { return state.config ?? emptyConfig(state.activeId || 1); }
function deviceNode(device) { return device.node_id ?? state.hello?.remote_node_id; }
function isControlDevice(device) { return device.supports_onoff === true; }
function outputLabel(device) { const stateName = outputState(device); if (stateName === 'unsupported') return 'Sortie · Non pilotable'; if (stateName === 'on') return 'Sortie · Allumée'; if (stateName === 'off') return 'Sortie · Éteinte'; return 'Sortie · État inconnu'; }

async function request(command, params = {}) {
  requireConnection();
  try {
    const timeout = ['get_configs', 'get_config', 'get_devices', 'save_config'].includes(command) ? 60000 : command === 'get_discovery_status' ? 15000 : 10000;
    return await client.request(command, params, timeout);
  } catch (error) { setStatus(error.message, true); throw error; }
}

async function loadDevices() {
  if (state.loadingDevices) return state.loadingDevices;
  const knownDevices = [...state.devices];
  state.loadingDevices = (async () => {
    try {
      const first = await request('get_devices', { offset: 0, limit: DEVICE_PAGE_SIZE });
      const total = Number(first.count ?? first.devices?.length ?? 0);
      const devices = (first.devices ?? []).map(device => ({ ...device, node_id: first.node_id }));
      for (let offset = devices.length; offset < total; offset += DEVICE_PAGE_SIZE) {
        const page = await request('get_devices', { offset, limit: DEVICE_PAGE_SIZE });
        devices.push(...(page.devices ?? []).map(device => ({ ...device, node_id: page.node_id ?? first.node_id })));
      }
      state.devices = mergeKnownDevices(devices, knownDevices);
      state.selected = new Set([...state.selected].filter(id => state.devices.some(device => deviceIdentity(device) === id)));
      render();
      setStatus(`${state.devices.length} appareil(s) Matter en cache.`);
    } catch (error) {
      if (!error.message.startsWith('DEVICES_UNAVAILABLE:')) throw error;
      state.devices = knownDevices; render(); setStatus('Aucun appareil Matter en cache; lancez un rescan.');
    }
  })().finally(() => { state.loadingDevices = null; render(); });
  return state.loadingDevices;
}

const wait = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
async function waitForScan(scanId) {
  const deadline = Date.now() + 300000;
  for (;;) {
    let result;
    try { result = await request('get_discovery_status'); }
    catch (error) { if (!error.message.startsWith('Délai dépassé') || Date.now() >= deadline) throw error; await wait(250); continue; }
    if (result.scan_id !== scanId) throw new Error('Résultat de rescan obsolète');
    state.scan.status = result; renderScan();
    if (!result.scanning) {
      if (result.phase === 'failed') throw new Error(`Rescan Matter échoué (${result.error})`);
      break;
    }
    setStatus(scanSummary(state.scan, state.devices.length));
    await wait(1000);
    if (Date.now() >= deadline) throw new Error('Délai dépassé: rescan Matter');
  }
  await loadDevices();
}

async function loadConfigs() {
  const result = await request('get_configs');
  state.configs = result.configs ?? [];
  state.activeId = result.active_config_id ?? state.configs[0]?.id ?? 1;
  if (!state.configs.length) { state.config = emptyConfig(1); state.configs = [{ id: 1, name: 'Maison', active: true }]; state.activeId = 1; }
  state.config = normalizeConfig(await request('get_config', { config_id: state.activeId }));
  state.dirty = false; state.roomId = 'all'; state.selected.clear(); render();
}

async function connect() {
  setStatus('Connexion à l’ESP32…');
  try {
    const hello = await client.connect($('access-code').value);
    state.hello = hello;
    setConnection(true, `ESP32 connecté en ${wifiMode ? 'Wi-Fi' : 'USB'} · node ${hello.remote_node_id}`);
    $('wifi-info').hidden = wifiMode || !hello.wifi_url || !hello.wifi_access_code;
    $('wifi-info').textContent = $('wifi-info').hidden ? '' : `Accès Wi-Fi : ${hello.wifi_url} · Code : ${hello.wifi_access_code}`;
    await wait(100); await loadConfigs(); await loadDevices();
  }
  catch (error) { state.hello = null; await client.disconnect().catch(() => {}); setConnection(false, error.message); }
}

function visibleDevices() { const devices = filterDevices(state.devices, currentConfig(), { search: state.search, status: state.statusFilter, roomId: state.roomId }); return sortDevices(state.view === 'all' ? devices : devices.filter(isControlDevice), currentConfig(), state.sort); }
function render() { renderConfigs(); renderRooms(); renderConfigSummary(); renderFilters(); renderSelectionBar(); renderDevices(); renderScan(); renderToolbar(); }
function renderToolbar() {
  $('refresh').disabled = !state.hello || Boolean(state.loadingDevices);
  $('rescan').disabled = !state.hello || Boolean(state.scan);
  $('reload').disabled = !state.hello;
  $('save').disabled = !state.hello || !state.config || !state.dirty;
}
function renderConfigSummary() {
  $('active-config').textContent = state.config ? `Configuration active · ${state.config.name}` : 'Aucune configuration active';
  $('device-count').textContent = `${state.devices.length} appareils connus`;
  $('result-count').textContent = `${visibleDevices().length} affichés`;
  $('dirty-indicator').hidden = !state.dirty;
}
function renderConfigs() {
  const root = $('configs'); root.replaceChildren();
  for (const config of state.configs) {
    const row = document.createElement('div'); row.className = 'item';
    const button = document.createElement('button'); button.className = config.id === state.activeId ? 'selected' : ''; button.textContent = `${config.id === state.activeId ? '● ' : ''}${config.name}`; button.onclick = () => selectConfig(config.id);
    const rename = document.createElement('button'); rename.textContent = 'Renommer'; rename.title = 'Renommer la configuration'; rename.onclick = () => renameConfig(config.id);
    const duplicate = document.createElement('button'); duplicate.textContent = 'Copier'; duplicate.title = 'Dupliquer la configuration'; duplicate.onclick = () => duplicateConfig(config.id);
    const remove = document.createElement('button'); remove.textContent = 'Suppr.'; remove.title = 'Supprimer la configuration'; remove.disabled = config.id === state.activeId; remove.onclick = () => deleteConfig(config.id);
    row.append(button, rename, duplicate, remove); root.append(row);
  }
}
function renderRooms() {
  const root = $('rooms'); root.replaceChildren(); const config = currentConfig();
  const unassigned = document.createElement('button'); unassigned.className = state.roomId === 'unassigned' ? 'selected' : ''; unassigned.textContent = `Non affectés (${state.devices.filter(device => !deviceBinding(config, device)?.configured).length})`; unassigned.onclick = () => { state.roomId = 'unassigned'; render(); }; root.append(unassigned);
  config.rooms.forEach((room, index) => {
    const row = document.createElement('div'); row.className = 'item'; const button = document.createElement('button'); button.className = Number(state.roomId) === Number(room.id) ? 'selected' : ''; button.textContent = `${room.name} (${state.devices.filter(device => deviceBinding(config, device)?.room_id === room.id).length})`; button.onclick = () => { state.roomId = room.id; render(); };
    const rename = document.createElement('button'); rename.textContent = 'Renommer'; rename.onclick = () => renameRoom(room);
    const remove = document.createElement('button'); remove.textContent = 'Suppr.'; remove.onclick = () => deleteRoom(room, index);
    row.append(button, rename, remove); root.append(row);
  });
}
function renderFilters() {
  $('search').value = state.search; $('status-filter').value = state.statusFilter; $('view-filter').value = state.view; $('sort').value = state.sort;
  const room = $('room-filter'); const previous = String(state.roomId); room.replaceChildren(new Option('Toutes les pièces', 'all'), new Option('Non affectés', 'unassigned'));
  for (const item of currentConfig().rooms) room.append(new Option(item.name, item.id));
  room.value = [...room.options].some(option => option.value === previous) ? previous : 'all';
  if (room.value !== previous) state.roomId = room.value;
}
function renderSelectionBar() {
  const root = $('selection-bar'); root.hidden = !state.selected.size; $('selection-count').textContent = `${state.selected.size} appareil${state.selected.size > 1 ? 's' : ''} sélectionné${state.selected.size > 1 ? 's' : ''}`;
  const target = $('bulk-room'); target.replaceChildren(new Option('Choisir une pièce…', ''));
  for (const room of currentConfig().rooms) target.append(new Option(room.name, room.id));
  $('select-visible').textContent = visibleDevices().length && visibleDevices().every(device => state.selected.has(deviceIdentity(device))) ? 'Désélectionner les visibles' : 'Sélectionner les visibles';
}
function renderDevices() {
  const root = $('devices'); root.replaceChildren(); const devices = visibleDevices();
  if (!devices.length) { const empty = document.createElement('p'); empty.className = 'empty'; empty.textContent = state.devices.length ? 'Aucun appareil ne correspond à ces critères.' : 'Aucun appareil Matter en cache. Actualisez le cache ou lancez un rescan.'; root.append(empty); return; }
  const fragment = document.createDocumentFragment(); for (const device of devices) fragment.append(deviceCard(device)); root.append(fragment);
}
function makeDetail(label, value) { const dt = document.createElement('dt'); dt.textContent = label; const dd = document.createElement('dd'); dd.textContent = value; return [dt, dd]; }
function deviceCard(device) {
  const config = currentConfig(); const binding = deviceBinding(config, device); const identity = deviceIdentity(device); const stateName = reachability(device); const card = document.createElement('article'); card.className = `card ${stateName}`; card.dataset.identity = identity;
  const head = document.createElement('div'); head.className = 'card-head'; const checkbox = document.createElement('input'); checkbox.type = 'checkbox'; checkbox.checked = state.selected.has(identity); checkbox.setAttribute('aria-label', `Sélectionner ${displayName(device)}`); checkbox.onchange = () => { checkbox.checked ? state.selected.add(identity) : state.selected.delete(identity); renderSelectionBar(); renderDevices(); }; const title = document.createElement('div'); title.className = 'title'; const name = document.createElement('h3'); name.textContent = displayName(device); const meta = document.createElement('p'); meta.className = 'meta'; meta.textContent = `Endpoint ${device.endpoint} · ${binding?.configured ? roomName(config, binding.room_id) : 'Non affecté'}`; title.append(name, meta); const badge = document.createElement('span'); badge.className = `reachability ${stateName}`; badge.textContent = stateName === 'reachable' ? 'Disponible' : stateName === 'unreachable' ? 'Indisponible' : 'Inconnue'; head.append(checkbox, title, badge); card.append(head);
  const capability = ['On/Off', device.supports_level && 'Luminosité', device.supports_color && 'Couleur'].filter(Boolean).join(' · ') || 'Lecture seule'; const summary = document.createElement('p'); summary.className = 'capabilities'; summary.textContent = `${classification(device)} · ${capability}`; card.append(summary); const output = document.createElement('p'); output.className = `output ${outputState(device)}`; output.textContent = outputLabel(device); card.append(output);
  if (isControlDevice(device)) {
    const actions = document.createElement('div'); actions.className = 'actions'; const pending = state.pending.has(identity); for (const [label, commandName, params] of [['Allumer', 'device_on'], ['Éteindre', 'device_off'], ['Identifier', 'device_identify', { duration_ms: 3000 }]]) { const button = document.createElement('button'); button.dataset.label = label; button.textContent = pending && commandName !== 'device_identify' ? 'En cours…' : label; button.disabled = stateName === 'unreachable' || Boolean(state.scan) || (pending && commandName !== 'device_identify'); button.onclick = () => command(device, commandName, params); actions.append(button); } card.append(actions);
  } else { const note = document.createElement('p'); note.className = 'muted'; note.textContent = 'Endpoint détecté mais non pilotable : cluster On/Off absent.'; card.append(note); }
  const controls = document.createElement('div'); controls.className = 'controls'; const label = document.createElement('label'); label.textContent = 'Nom local'; const input = document.createElement('input'); input.type = 'text'; input.maxLength = 47; input.value = binding?.display_name || ''; input.placeholder = displayName(device); input.onchange = () => { const value = input.value.trim(); if (!value && !binding) return; ensureBinding(device).display_name = value || displayName(device); markDirty(); }; label.append(input); controls.append(label);
  const room = document.createElement('select'); room.disabled = !isControlDevice(device); room.title = room.disabled ? 'Seuls les endpoints On/Off peuvent être enregistrés dans une pièce.' : ''; room.append(new Option('Non affecté', '0')); for (const item of config.rooms) room.append(new Option(item.name, item.id)); room.value = String(binding?.configured ? binding.room_id : 0); room.onchange = () => updateDeviceRoom(device, room.value); controls.append(room);
  if (device.supports_level) { const brightness = document.createElement('input'); brightness.type = 'range'; brightness.min = 0; brightness.max = 254; brightness.value = device.brightness_known ? device.brightness : 0; brightness.disabled = stateName === 'unreachable' || Boolean(state.scan); const value = document.createElement('span'); value.textContent = device.brightness_known ? `${device.brightness}` : '—'; brightness.oninput = () => value.textContent = brightness.value; brightness.onchange = () => command(device, 'set_brightness', { brightness: Number(brightness.value) }); controls.append(document.createTextNode('Luminosité'), brightness, value); }
  card.append(controls);
  const details = document.createElement('details'); const summaryDetails = document.createElement('summary'); summaryDetails.textContent = 'Détails techniques'; const list = document.createElement('dl'); for (const [labelText, value] of [['Node ID', device.node_id || '—'], ['Endpoint', device.endpoint], ['Nom Matter', displayName(device)], ['Source du nom', device.name_source || 'inconnue'], ['Type Matter', device.type_label || 'inconnu'], ['Type ID', `0x${Number(device.device_type || 0).toString(16).padStart(4, '0')}`], ['Classification', classification(device)], ['Capacités', capability], ['État du cache', stateName === 'unknown' ? 'Joignabilité inconnue' : stateName === 'reachable' ? 'Joignable' : 'Hors ligne']]) list.append(...makeDetail(labelText, value)); details.append(summaryDetails, list); card.append(details); return card;
}
function ensureBinding(device) { let binding = deviceBinding(state.config, device); if (!binding) { binding = { node_id: device.node_id, endpoint: Number(device.endpoint), room_id: 0, display_name: displayName(device), order: state.config.bindings.length, configured: false }; state.config.bindings.push(binding); } return binding; }
function updateDeviceRoom(device, roomId) { if (!Number(roomId) && !deviceBinding(state.config, device)) return; const result = assignDevices(state.config, [device], new Set([deviceIdentity(device)]), roomId); if (!result.ok) { setStatus(result.message, true); return; } markDirty(); }
function updateBulkRoom() { const roomId = $('bulk-room').value; if (!roomId) { setStatus('Choisissez une pièce pour les appareils sélectionnés.', true); return; } const result = assignDevices(state.config, state.devices, state.selected, roomId); if (!result.ok) { setStatus(result.message, true); return; } markDirty(`${result.updated} appareil(s) affecté(s) à ${roomName(state.config, roomId)}.`); }
function clearBulkRooms() { const result = assignDevices(state.config, state.devices, state.selected, 0); if (!result.ok) { setStatus(result.message, true); return; } markDirty(`${result.updated} affectation(s) supprimée(s).`); }
function updateDeviceCard(device) { const card = [...document.querySelectorAll('#devices [data-identity]')].find(item => item.dataset.identity === deviceIdentity(device)); if (!card) return; const output = card.querySelector('.output'); output.className = `output ${outputState(device)}`; output.textContent = outputLabel(device); const pending = state.pending.has(deviceIdentity(device)); card.querySelectorAll('.actions button').forEach(button => { button.disabled = reachability(device) === 'unreachable' || Boolean(state.scan) || (pending && button.dataset.label !== 'Identifier'); button.textContent = pending && button.dataset.label !== 'Identifier' ? 'En cours…' : button.dataset.label; }); }
async function command(device, cmd, params = {}) { const identity = deviceIdentity(device); if (state.pending.has(identity)) return; const requestId = Symbol(identity); if (cmd === 'device_on' || cmd === 'device_off' || cmd === 'set_brightness') { state.pending.set(identity, requestId); updateDeviceCard(device); console.info('CONTROL_UI:', `onoff_before=${device.onoff}`, `onoff_requested=${cmd === 'device_on' ? 1 : cmd === 'device_off' ? 0 : 'brightness'}`); } try { const result = await request(cmd, { node_id: deviceNode(device), endpoint: Number(device.endpoint), ...params }); if (result.type === 'command_result' && !result.success) throw new Error(result.error || 'Commande échouée'); if (result.type === 'command_result' && (cmd === 'device_on' || cmd === 'device_off')) { device.onoff = commandState(result).startsWith('confirmed_') ? result.state : null; console.info('CONTROL_UI:', `onoff_confirmed=${device.onoff}`); } if (cmd === 'set_brightness' && result.success) { device.brightness = params.brightness; device.brightness_known = true; } setStatus(`Endpoint ${device.endpoint}: commande réussie.`); } catch { /* status is already shown */ } finally { if (state.pending.get(identity) === requestId) { state.pending.delete(identity); updateDeviceCard(device); } } }
function renameRoom(room) { const name = prompt('Nom de la pièce', room.name)?.trim(); if (name) { room.name = name.slice(0, 31); markDirty(); } }
function deleteRoom(room, index) { if (!confirm(`Supprimer ${room.name} ? Les appareils resteront non affectés.`)) return; state.config.bindings.filter(binding => binding.room_id === room.id).forEach(binding => { binding.room_id = 0; binding.configured = false; }); state.config.rooms.splice(index, 1); if (Number(state.roomId) === Number(room.id)) state.roomId = 'all'; markDirty(); }
function selectVisible() { const devices = visibleDevices(); const allSelected = devices.length && devices.every(device => state.selected.has(deviceIdentity(device))); for (const device of devices) allSelected ? state.selected.delete(deviceIdentity(device)) : state.selected.add(deviceIdentity(device)); renderSelectionBar(); renderDevices(); }
async function selectConfig(id) { if (id === state.activeId || !confirmDiscard()) return; try { await request('set_active_config', { config_id: id }); state.activeId = id; state.config = normalizeConfig(await request('get_config', { config_id: id })); state.roomId = 'all'; state.selected.clear(); state.dirty = true; render(); setStatus('Configuration active modifiée localement; enregistrez pour persister.'); } catch {} }
async function renameConfig(id) { const item = state.configs.find(config => config.id === id); const name = prompt('Nouveau nom', item?.name)?.trim(); if (!name) return; try { await request('rename_config', { config_id: id, name }); item.name = name; if (state.config?.id === id) state.config.name = name; markDirty('Nom de configuration modifié localement; enregistrez pour persister.'); } catch {} }
async function duplicateConfig(id) { const source = state.configs.find(config => config.id === id); const name = prompt('Nom de la copie', `${source?.name ?? 'Configuration'} copie`)?.trim(); if (!name) return; try { const result = normalizeConfig(await request('duplicate_config', { config_id: id, name })); await request('set_active_config', { config_id: result.id }); result.active = true; state.configs.push({ id: result.id, name: result.name }); state.activeId = result.id; state.config = result; state.roomId = 'all'; state.selected.clear(); state.dirty = true; render(); } catch {} }
async function deleteConfig(id) { if (id === state.activeId || !confirm('Supprimer cette configuration ?')) return; try { await request('delete_config', { config_id: id }); state.configs = state.configs.filter(config => config.id !== id); state.dirty = true; render(); setStatus('Configuration supprimée localement; enregistrez pour persister.'); } catch {} }
async function newConfig() { const name = prompt('Nom de la configuration', 'Nouvelle configuration')?.trim(); if (!name) return; try { const config = normalizeConfig(await request('create_config', { name })); await request('set_active_config', { config_id: config.id }); config.active = true; state.configs.push({ id: config.id, name: config.name }); state.activeId = config.id; state.config = config; state.roomId = 'all'; state.selected.clear(); state.dirty = true; render(); } catch {} }
async function newRoom() { if (!state.config) return; if (state.config.rooms.length >= 16) { setStatus('Limite de 16 pièces atteinte.', true); return; } const name = prompt('Nom de la pièce', `Pièce ${state.config.rooms.length + 1}`)?.trim(); if (!name) return; const id = Math.max(0, ...state.config.rooms.map(room => room.id)) + 1; state.config.rooms.push({ id, name: name.slice(0, 31), order: state.config.rooms.length }); state.roomId = id; markDirty(); }
async function save() { try { const check = validateConfig(state.config, state.devices, state.hello.remote_node_id); if (!check.ok) throw new Error(check.message); const expected = normalizeConfig(state.config); await request('save_config', { config: expected }); const saved = normalizeConfig(await request('get_config', { config_id: expected.id })); const comparable = config => { const { type, revision, active, request_id, ...rest } = normalizeConfig(config); return rest; }; if (saved.id !== expected.id || saved.active !== true || JSON.stringify(comparable(saved)) !== JSON.stringify(comparable(expected))) throw new Error('La relecture ne correspond pas aux modifications'); state.config = saved; state.dirty = false; state.lastSaved = new Date().toLocaleTimeString(); setStatus(`Configuration vérifiée à ${state.lastSaved}.`); render(); } catch (error) { setStatus(error.message, true); } }
async function reload() { if (!confirmDiscard()) return; try { await loadConfigs(); await loadDevices(); setStatus('Configuration et cache relus depuis l’ESP32.'); } catch {} }
function exportJson() { if (!state.config) return; const payload = { tool_format: 1, schema: 1, node_id: state.hello?.remote_node_id, configuration: state.config }; const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' }); const link = document.createElement('a'); link.href = URL.createObjectURL(blob); link.download = `${state.config.name.replace(/[^a-z0-9_-]+/gi, '_')}.json`; link.click(); URL.revokeObjectURL(link.href); }
function importJson() { $('file').click(); }
async function readImport(event) { const file = event.target.files[0]; if (!file || !confirmDiscard()) return; try { const payload = JSON.parse(await file.text()); const config = normalizeConfig(payload.configuration ?? payload); if (payload.node_id && payload.node_id !== state.hello?.remote_node_id && !confirm('Le node ID du fichier diffère de celui de l’ESP32. Importer quand même ?')) return; const check = validateConfig(config, [], null); if (!check.ok) throw new Error(check.message); const result = await request('import_config', { config }); state.config = normalizeConfig(result); await request('set_active_config', { config_id: state.config.id }); state.config.active = true; state.activeId = state.config.id; state.dirty = true; render(); setStatus('Import chargé localement; confirmez avec Enregistrer.'); } catch (error) { setStatus(`Import refusé: ${error.message}`, true); } event.target.value = ''; }
async function reset() { if (!confirm('Effacer uniquement la configuration UI ? Matter, Wi-Fi et commissioning seront conservés.')) return; try { await request('reset_ui_config', { confirm: 'RESET_UI_ONLY' }); await loadConfigs(); render(); setStatus('Configuration UI effacée; Matter et Wi-Fi préservés.'); } catch {} }
function renderScan() { const root = $('scan-state'); const scan = state.scan; root.hidden = !scan; if (!scan) return; const result = scan.status || {}; root.textContent = scanSummary(scan, state.devices.length); root.className = `scan-state ${result.scanning === false ? 'complete' : ''}`; }

$('connect').onclick = connect; $('refresh').onclick = () => { setStatus('Actualisation du cache Matter…'); loadDevices().catch(() => {}); }; $('rescan').onclick = async () => { if (state.scan) return; try { const result = await request('rescan_devices'); if (!result.accepted) throw new Error(`Rescan Matter refusé: ${result.error || 'ESP32 occupé'}`); state.scan = { id: result.scan_id, startedAt: Date.now(), status: result }; render(); setStatus('Analyse Matter en cours… la liste en cache reste disponible.'); await waitForScan(result.scan_id); setStatus(`Rescan Matter terminé · ${state.devices.length} appareils.`); } catch (error) { setStatus(error.message, true); } finally { state.scan = null; render(); } }; $('reload').onclick = reload; $('save').onclick = save; $('new-config').onclick = newConfig; $('new-room').onclick = newRoom; $('export').onclick = exportJson; $('import').onclick = importJson; $('file').onchange = readImport; $('reset').onclick = reset; $('search').oninput = event => { state.search = event.target.value; renderConfigSummary(); renderSelectionBar(); renderDevices(); }; $('status-filter').onchange = event => { state.statusFilter = event.target.value; render(); }; $('room-filter').onchange = event => { state.roomId = event.target.value; render(); }; $('view-filter').onchange = event => { state.view = event.target.value; render(); }; $('sort').onchange = event => { state.sort = event.target.value; render(); }; $('select-visible').onclick = selectVisible; $('assign-selected').onclick = updateBulkRoom; $('clear-selected').onclick = clearBulkRooms;
window.addEventListener('beforeunload', event => { if (state.dirty) { event.preventDefault(); event.returnValue = ''; } });
render();

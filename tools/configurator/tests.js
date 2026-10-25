import { LineFramer, protocolSelfTest } from './protocol.js';
import { assignDevices, classification, commandState, deviceIdentity, filterDevices, validateConfig, emptyConfig, displayName, normalizeConfig, mergeKnownDevices, deduplicateDevices, outputState, reachability, scanSummary, sortDevices } from './model.js';

export function runTests() {
  const check = (condition, message) => { if (!condition) throw new Error(message); };
  const seen = []; const errors = []; const parser = new LineFramer(x => { try { seen.push(JSON.parse(x)); } catch { errors.push('malformed JSON'); } }, e => errors.push(e));
  parser.feed('INFO log\n@@CFG:{"request_id":1'); parser.feed(',"type":"ok"}\n@@CFG:{bad}\n');
  check(seen.length === 1 && seen[0].request_id === 1, 'fragmentation/log filtering');
  check(errors.length === 1 && errors[0] === 'malformed JSON', 'malformed JSON rejection');
  const utf8 = []; const utfParser = new LineFramer(x => utf8.push(JSON.parse(x)));
  const utfFrame = new TextEncoder().encode(`@@CFG:${JSON.stringify({ type: 'devices', name: 'Lumière entrée "Bureau"' })}\n`);
  utfParser.feed(utfFrame.slice(0, 9)); utfParser.feed(utfFrame.slice(9, 17)); utfParser.feed(utfFrame.slice(17));
  check(utf8[0].name === 'Lumière entrée "Bureau"', 'split UTF-8 and escaped name');
  const large = []; const largeParser = new LineFramer(x => large.push(JSON.parse(x)), () => { throw new Error('large frame rejected'); });
  largeParser.feed(`@@CFG:${JSON.stringify({ type: 'devices', devices: [{ name: 'x'.repeat(7900) }] })}\n`);
  check(large[0].devices[0].name.length === 7900, 'large response frame');
  check(displayName({ name: 'Lampe salon', endpoint: 12 }) === 'Lampe salon', 'real name is primary');
  check(displayName({ endpoint: 12 }) === 'Endpoint 12', 'unnamed endpoint fallback');
  check(outputState({ supports_onoff: true, onoff: true }) === 'on' && outputState({ supports_onoff: true, onoff: null }) === 'unknown' && outputState({ supports_onoff: false }) === 'unsupported', 'endpoint output state');
  check(commandState({ success: true, state: true }) === 'confirmed_on' && commandState({ success: true, state: false }) === 'confirmed_off' && commandState({ success: true }) === 'unknown' && commandState({ success: false }) === 'command_failed', 'command state machine');
  check(classification({ supports_onoff: true, supports_level: true }) === 'Lumière dimmable' && classification({ supports_onoff: true, type_label: 'Prise' }) === 'Prise / sortie', 'bridged classification');
  const preserved = mergeKnownDevices([], [{ node_id: '0x1', endpoint: 12, name: 'Lampe salon', reachable: true }]);
  check(preserved.length === 1 && preserved[0].name === 'Lampe salon' && preserved[0].reachable === false, 'unreachable cached device');
  const refreshed = mergeKnownDevices([{ node_id: '0x1', endpoint: 12, name: '' }], [{ node_id: '0x1', endpoint: 12, name: 'Lampe salon' }]);
  check(refreshed[0].name === 'Lampe salon', 'cache refresh preserves resolved name');
  check(deduplicateDevices([{ node_id: '0x1', endpoint: 12 }, { node_id: '0x1', endpoint: 12 }, { node_id: '0x1', endpoint: 13 }]).length === 2, 'duplicate device identity');
  check(mergeKnownDevices([{ node_id: '0x1', endpoint: 12 }, { node_id: '0x1', endpoint: 12 }], []).length === 1, 'duplicate paginated device');
  const config = emptyConfig(); config.rooms = [{ id: 1, name: 'Salon', order: 0 }]; config.bindings = [{ node_id: '0x1', endpoint: 2, display_name: 'Plafonnier "Bureau" — Lumière', configured: true, room_id: 1, order: 0 }];
  check(validateConfig(config, [{ node_id: '0x1', endpoint: 2 }], '0x1').ok, 'valid config');
  check(!validateConfig({ ...config, bindings: [config.bindings[0], config.bindings[0]] }).ok, 'duplicate endpoint');
  check(!validateConfig({ ...config, bindings: [{ ...config.bindings[0], room_id: 9 }] }).ok, 'invalid room');
  check(!validateConfig({ ...config, schema: 99 }).ok, 'schema validation');
  check(!validateConfig({ ...config, bindings: Array.from({ length: 33 }, (_, i) => ({ ...config.bindings[0], endpoint: i + 1 })) }).ok, 'binding limit');
  const reloaded = normalizeConfig(JSON.parse(JSON.stringify(config)));
  check(reloaded.bindings[0].node_id === '0x1' && reloaded.bindings[0].endpoint === 2 && reloaded.bindings[0].display_name.includes('Bureau'), 'save/reload node endpoint name mapping');
  for (const count of [48, 49, 64, 96, 128]) {
    const expected = Array.from({ length: count }, (_, endpoint) => ({
      node_id: '0x1', endpoint: endpoint + 1,
      name: endpoint === 127 ? 'Lumière entrée "Bureau"' : `Lampe ${endpoint + 1}`,
      reachable: endpoint % 3 !== 0,
    }));
    const pages = [];
    for (let offset = 0; offset < count; offset += 16) {
      const frame = `@@CFG:${JSON.stringify({ type: 'devices', count, offset, devices: expected.slice(offset, offset + 16) })}\n`;
      const pageParser = new LineFramer(x => pages.push(JSON.parse(x)));
      for (let i = 0; i < frame.length; i += 7) pageParser.feed(frame.slice(i, i + 7));
    }
    const flattened = pages.flatMap(page => page.devices);
    check(flattened.length === count, `pagination count ${count}`);
    check(new Set(flattened.map(device => `${device.node_id}:${device.endpoint}`)).size === count, `pagination identity ${count}`);
    if (count > 127) check(flattened[127].name === 'Lumière entrée "Bureau"', `pagination UTF-8 ${count}`);
  }
  const highEndpoint = { ...config, bindings: [{ ...config.bindings[0], endpoint: 128 }] };
  check(validateConfig(highEndpoint, [{ node_id: '0x1', endpoint: 128 }], '0x1').ok, 'device above legacy cache index');
  check(normalizeConfig(JSON.parse(JSON.stringify(highEndpoint))).bindings[0].endpoint === 128, 'high endpoint save/reload');
  const largeConfig = emptyConfig(1, 'Maison'); largeConfig.rooms = [{ id: 1, name: 'Entrée', order: 0 }, { id: 2, name: 'Bureau', order: 1 }];
  const largeDevices = Array.from({ length: 256 }, (_, index) => ({ node_id: '0x1', endpoint: index + 1, name: index % 2 ? `Plafond ${index + 1}` : `Prise ${index + 1}`, reachable: index % 5 === 0 ? null : index % 3 !== 0, supports_onoff: true }));
  check(largeDevices.slice(0, 167).length === 167, 'synthetic 167 devices');
  check(largeDevices.length === 256, 'synthetic 256 devices');
  check(filterDevices(largeDevices, largeConfig, { search: 'prise 167' }).length === 1, 'search by real name');
  check(filterDevices(largeDevices, largeConfig, { search: '74' }).some(device => device.endpoint === 74), 'search by endpoint');
  check(filterDevices(largeDevices, largeConfig, { status: 'reachable' }).every(device => reachability(device) === 'reachable'), 'available filter');
  check(filterDevices(largeDevices, largeConfig, { status: 'unknown' }).every(device => reachability(device) === 'unknown'), 'unknown reachability filter');
  check(filterDevices(largeDevices, largeConfig, { status: 'unassigned' }).length === 256, 'unassigned filter');
  largeConfig.bindings = [{ node_id: '0x1', endpoint: 167, display_name: 'Plafond 167', configured: true, room_id: 2, order: 0 }];
  check(filterDevices(largeDevices, largeConfig, { search: 'bureau', status: 'assigned' }).length === 1, 'combined search and room filter');
  check(sortDevices(largeDevices.slice(0, 3), largeConfig, 'endpoint').map(device => device.endpoint).join(',') === '1,2,3', 'endpoint sort');
  check(sortDevices([{ ...largeDevices[1], name: 'Zèbre' }, { ...largeDevices[0], name: 'À côté' }], largeConfig, 'name')[0].name === 'À côté', 'name sort');
  check(sortDevices([{ ...largeDevices[0], supports_onoff: false }, largeDevices[1]], largeConfig, 'controllable')[0].supports_onoff === true, 'controllable sort');
  const selected = new Set([deviceIdentity(largeDevices[3]), deviceIdentity(largeDevices[74]), deviceIdentity(largeDevices[167])]);
  check(assignDevices(largeConfig, largeDevices, selected, 1).updated === 3, 'bulk room assignment');
  check([...selected].every(id => largeConfig.bindings.find(binding => deviceIdentity(binding) === id)?.room_id === 1), 'selection identity survives assignment');
  check(assignDevices(largeConfig, largeDevices, selected, 0).updated === 3 && [...selected].every(id => !largeConfig.bindings.some(binding => deviceIdentity(binding) === id)), 'clear assignments');
  const scan = { startedAt: 1000, status: { phase: 'reading_endpoints', endpoint_count: 167, scanning: true } };
  check(scanSummary(scan, 167, 19000).includes('Temps écoulé : 0 min 18 s'), 'scan elapsed state');
  const beforeRescan = cloneForTest(largeConfig);
  mergeKnownDevices(largeDevices.slice(0, 167), largeDevices);
  check(JSON.stringify(beforeRescan) === JSON.stringify(largeConfig), 'rescan preserves unsaved config edits');
  check(deviceIdentity({ node_id: '0x1', endpoint: 74 }) === '0x1:74', 'canonical node endpoint identity');
  check(protocolSelfTest(), 'protocol self-test');
  return { passed: true, frames: seen.length, errors: errors.length };
}

function cloneForTest(value) { return JSON.parse(JSON.stringify(value)); }

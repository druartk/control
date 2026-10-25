#!/usr/bin/env python3
"""Verify the real board over USB and Wi-Fi; optionally save the current config unchanged."""
import argparse
import ipaddress
import json
import re
import time
import urllib.error
import urllib.parse
import urllib.request
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='COM4')
parser.add_argument('--save-current', action='store_true', help='Persist the active configuration unchanged and compare readback')
args = parser.parse_args()
port = serial.Serial(baudrate=115200, timeout=0.2)
port.dtr = False
port.rts = False
port.port = args.port
port.open()

def usb(cmd, **params):
    request = dict(cmd=cmd, protocol=1, request_id=1, **params)
    port.write(('@@CFG:' + json.dumps(request) + '\n').encode())
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        line = port.readline().decode('utf-8', errors='replace').strip()
        if not line.startswith('@@CFG:'):
            continue
        message = json.loads(line[6:])
        if message.get('request_id') == 1:
            return message
    raise AssertionError('No USB response')

with port:
    hello = usb('hello')
    assert hello.get('controller_ready'), 'Matter controller not ready'
    url = hello.get('wifi_url', '')
    assert urllib.parse.urlparse(url).scheme == 'http'
    assert ipaddress.ip_address(urllib.parse.urlparse(url).hostname).is_private
    code = hello.get('wifi_access_code', '')
    assert re.fullmatch('[0-9a-f]{16}', code), 'Missing access code'
    usb_devices = usb('get_devices', offset=0, limit=16)
print('PASS: USB preserved; Wi-Fi URL ' + url, flush=True)

opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
def post(payload, *, token=code, content_type='application/json', expected=200):
    data = payload if isinstance(payload, bytes) else json.dumps(payload, ensure_ascii=False).encode()
    headers = {'Content-Type': content_type}
    if token is not None:
        headers['Authorization'] = 'Bearer ' + token
    request = urllib.request.Request(url + '/api/config', data, headers)
    try:
        with opener.open(request, timeout=65) as response:
            assert response.status == expected
            return json.loads(response.read())
    except urllib.error.HTTPError as error:
        assert error.code == expected, (error.code, expected)
        error.read()
        return None

request_id = 0
def rpc(cmd, **params):
    global request_id
    request_id += 1
    response = post(dict(cmd=cmd, request_id=request_id, **params))
    assert response['request_id'] == request_id
    assert response['type'] != 'error', (response.get('code'), response.get('message'))
    return response

for path, marker, mime in [('/', 'Configurateur Matter', 'text/html'),
                           ('/app.js', 'WifiProtocolClient', 'text/javascript'),
                           ('/wifi.js', 'class WifiProtocolClient', 'text/javascript'),
                           ('/protocol.js', 'class ProtocolClient', 'text/javascript'),
                           ('/model.js', 'normalizeConfig', 'text/javascript'),
                           ('/style.css', '.topbar', 'text/css')]:
    with opener.open(url + path, timeout=10) as response:
        assert response.headers['Content-Type'].startswith(mime)
        body = response.read().decode()
        assert marker in body and '\x00' not in body
post({'cmd': 'hello', 'request_id': 1}, token=None, expected=401)
post({'cmd': 'hello', 'request_id': 1}, token='0' * 16, expected=401)
post(b'{}', content_type='text/plain', expected=400)
post(b'{bad', expected=400)
post(b'x' * 8192, expected=400)
assert rpc('hello', protocol=1)['type'] == 'hello'
assert 'wifi_access_code' not in rpc('hello', protocol=1)
print('PASS: embedded interface, authentication, malformed and oversized requests rejected', flush=True)

first = rpc('get_devices', offset=0, limit=16)
# Live On/Off and reachability can change between the two reads.
assert first['node_id'] == usb_devices['node_id'], 'USB and Wi-Fi bridge identities differ'
assert [device['endpoint'] for device in first['devices']] == [device['endpoint'] for device in usb_devices['devices']], 'USB and Wi-Fi endpoint lists differ'
devices = list(first['devices'])
for offset in range(len(devices), first['count'], 16):
    page = rpc('get_devices', offset=offset, limit=16)
    assert page['count'] == first['count']
    devices.extend(page['devices'])
assert len(devices) == first['count']
assert len({device['endpoint'] for device in devices}) == len(devices)
print('PASS: Wi-Fi pagination: ' + str(len(devices)) + ' Matter endpoints', flush=True)
configs = rpc('get_configs')
config_id = configs['active_config_id']
config = rpc('get_config', config_id=config_id)
assert config['id'] == config_id
assert rpc('get_rooms')['type'] == 'rooms'
assert rpc('get_discovery_status')['type'] == 'discovery_status'
if args.save_current:
    saved = rpc('save_config', config=config)
    assert saved['type'] == 'config_saved' and saved['verified']
    reloaded = rpc('get_config', config_id=config_id)
    def comparable(value):
        return {key: item for key, item in value.items() if key not in {'type', 'request_id', 'revision', 'modified_at'}}
    assert comparable(config) == comparable(reloaded), 'Saved configuration readback differs'
    print('PASS: current configuration saved over Wi-Fi and read back unchanged', flush=True)
else:
    print('PASS: configuration read; save skipped (use --save-current)', flush=True)

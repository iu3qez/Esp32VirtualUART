// REST API client
const BASE = '';

export async function fetchPorts() {
  const res = await fetch(`${BASE}/api/ports`);
  return res.json();
}

export async function updatePortConfig(portId, config) {
  const res = await fetch(`${BASE}/api/ports/${portId}/config`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(config),
  });
  return res.json();
}

export async function fetchRoutes() {
  const res = await fetch(`${BASE}/api/routes`);
  return res.json();
}

export async function createRoute(route) {
  const res = await fetch(`${BASE}/api/routes`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(route),
  });
  return res.json();
}

export async function deleteRoute(routeId) {
  const res = await fetch(`${BASE}/api/routes/${routeId}`, {
    method: 'DELETE',
  });
  return res.json();
}

export async function fetchConfig() {
  const res = await fetch(`${BASE}/api/config`);
  return res.json();
}

export async function updateConfig(config) {
  const res = await fetch(`${BASE}/api/config`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(config),
  });
  return res.json();
}

export async function resetConfig() {
  const res = await fetch(`${BASE}/api/config/reset`, { method: 'POST' });
  return res.json();
}

export async function fetchSystem() {
  const res = await fetch(`${BASE}/api/system`);
  return res.json();
}

// WebSocket connections
export function connectSignals(onMessage) {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const ws = new WebSocket(`${proto}//${location.host}/ws/signals`);
  ws.onmessage = (e) => onMessage(JSON.parse(e.data));
  ws.onclose = () => setTimeout(() => connectSignals(onMessage), 3000);
  return ws;
}

export function connectMonitor(onMessage) {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const ws = new WebSocket(`${proto}//${location.host}/ws/monitor`);
  ws.onmessage = (e) => onMessage(JSON.parse(e.data));
  ws.onclose = () => setTimeout(() => connectMonitor(onMessage), 3000);
  return ws;
}

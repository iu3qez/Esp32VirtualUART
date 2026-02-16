<script>
  import { onMount } from 'svelte';
  import { PORT_TYPES, SIGNAL_NAMES } from '../stores/ports.js';
  import { ROUTE_TYPES } from '../stores/routes.js';
  import { updatePortConfig, createRoute, deleteRoute, fetchConfig, updateConfig } from './api.js';
  import { refreshPorts } from '../stores/ports.js';
  import { refreshRoutes } from '../stores/routes.js';

  export let selectedPort = null;
  export let ports = [];
  export let routes = [];
  export let onClose = () => {};

  let baudRate = 115200;
  let dataBits = 8;
  let stopBits = 0;
  let parity = 0;

  // WiFi settings
  let wifiConfig = { wifi: { ssid: '', mode: 'ap', ip: '', connected: false }, tcpConfigs: [] };
  let wifiSsid = '';
  let wifiPassword = '';
  let wifiSaving = false;
  let wifiStatus = '';

  // New route form
  let newRouteType = 0;
  let newRouteSrc = 0;
  let newRouteDst = [1];

  $: if (selectedPort) {
    baudRate = selectedPort.lineCoding?.baudRate || 115200;
    dataBits = selectedPort.lineCoding?.dataBits || 8;
    stopBits = selectedPort.lineCoding?.stopBits || 0;
    parity = selectedPort.lineCoding?.parity || 0;
  }

  async function loadWifiConfig() {
    try {
      wifiConfig = await fetchConfig();
      wifiSsid = wifiConfig.wifi?.ssid || '';
    } catch (e) {
      // ignore
    }
  }

  async function saveWifi() {
    if (!wifiSsid.trim()) return;
    wifiSaving = true;
    wifiStatus = '';
    try {
      const result = await updateConfig({ wifi: { ssid: wifiSsid.trim(), password: wifiPassword } });
      wifiPassword = '';
      if (result?.wifiChanging) {
        wifiStatus = 'Saved! Device is switching to "' + wifiSsid.trim() + '". You may lose connection if the device leaves AP mode.';
      } else {
        wifiStatus = 'Saved.';
      }
      setTimeout(loadWifiConfig, 8000);
    } catch (e) {
      wifiStatus = 'Saved (connection lost — device may be switching networks).';
    }
    wifiSaving = false;
  }

  async function saveLineCoding() {
    if (!selectedPort) return;
    await updatePortConfig(selectedPort.id, {
      lineCoding: { baudRate, dataBits, stopBits, parity }
    });
    await refreshPorts();
  }

  async function addRoute() {
    await createRoute({
      type: newRouteType,
      srcPortId: newRouteSrc,
      dstPortIds: newRouteDst,
    });
    await refreshRoutes();
  }

  async function removeRoute(id) {
    await deleteRoute(id);
    await refreshRoutes();
  }

  onMount(() => {
    loadWifiConfig();
    const interval = setInterval(loadWifiConfig, 10000);
    return () => clearInterval(interval);
  });
</script>

<div class="panel">
  <div class="panel-header">
    <h3>{selectedPort ? selectedPort.name : 'Configuration'}</h3>
    <button class="close-btn" on:click={onClose}>&times;</button>
  </div>

  <div class="section">
    <h4>WiFi Settings</h4>
    <div class="wifi-status">
      <span class="wifi-mode" class:sta={wifiConfig.wifi?.mode === 'sta'}
            class:ap={wifiConfig.wifi?.mode === 'ap'}>
        {wifiConfig.wifi?.mode === 'sta' ? 'STA' : 'AP'}
      </span>
      <span>{wifiConfig.wifi?.connected ? wifiConfig.wifi?.ip : 'Not connected'}</span>
    </div>
    {#if wifiConfig.wifi?.ssid}
      <div class="wifi-current">Network: {wifiConfig.wifi.ssid}</div>
    {/if}
    <label>
      SSID
      <input type="text" bind:value={wifiSsid} placeholder="WiFi network name" />
    </label>
    <label>
      Password
      <input type="password" bind:value={wifiPassword} placeholder="WiFi password" />
    </label>
    <button on:click={saveWifi} disabled={wifiSaving || !wifiSsid.trim()}>
      {wifiSaving ? 'Saving...' : 'Connect to WiFi'}
    </button>
    {#if wifiStatus}
      <div class="wifi-msg">{wifiStatus}</div>
    {/if}
  </div>

  {#if selectedPort}
    <div class="section">
      <h4>Line Coding</h4>
      <label>
        Baud Rate
        <select bind:value={baudRate}>
          {#each [300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600] as br}
            <option value={br}>{br}</option>
          {/each}
        </select>
      </label>
      <label>
        Data Bits
        <select bind:value={dataBits}>
          {#each [5, 6, 7, 8] as db}
            <option value={db}>{db}</option>
          {/each}
        </select>
      </label>
      <label>
        Stop Bits
        <select bind:value={stopBits}>
          <option value={0}>1</option>
          <option value={1}>1.5</option>
          <option value={2}>2</option>
        </select>
      </label>
      <label>
        Parity
        <select bind:value={parity}>
          <option value={0}>None</option>
          <option value={1}>Odd</option>
          <option value={2}>Even</option>
        </select>
      </label>
      <button on:click={saveLineCoding}>Apply</button>
    </div>

    <div class="section">
      <h4>Signals</h4>
      <div class="signal-grid">
        {#each SIGNAL_NAMES as sig}
          <div class="signal-item">
            <span class="signal-led"
                  class:on={selectedPort.signals?.[sig.toLowerCase()]}>
            </span>
            {sig}
          </div>
        {/each}
      </div>
    </div>
  {/if}

  <div class="section">
    <h4>Routes</h4>
    {#each routes as route}
      <div class="route-item">
        <span class="route-type">{ROUTE_TYPES[route.type]}</span>
        <span>
          {ports.find(p => p.id === route.srcPortId)?.name || '?'}
          &rarr;
          {route.dstPortIds.map(id => ports.find(p => p.id === id)?.name || '?').join(', ')}
        </span>
        <button class="delete-btn" on:click={() => removeRoute(route.id)}>&times;</button>
      </div>
    {/each}

    <div class="new-route">
      <h5>Add Route</h5>
      <label>
        Type
        <select bind:value={newRouteType}>
          {#each ROUTE_TYPES as t, i}
            <option value={i}>{t}</option>
          {/each}
        </select>
      </label>
      <label>
        Source
        <select bind:value={newRouteSrc}>
          {#each ports as p}
            <option value={p.id}>{p.name}</option>
          {/each}
        </select>
      </label>
      <label>
        Destination
        <select bind:value={newRouteDst[0]}>
          {#each ports as p}
            <option value={p.id}>{p.name}</option>
          {/each}
        </select>
      </label>
      <button on:click={addRoute}>Create Route</button>
    </div>
  </div>
</div>

<style>
  .panel {
    position: fixed;
    right: 0;
    top: 0;
    width: 280px;
    height: 100vh;
    background: #1a1a2e;
    border-left: 1px solid #333;
    overflow-y: auto;
    padding: 12px;
    color: #ddd;
    font-size: 13px;
    z-index: 100;
  }
  .panel-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 12px;
  }
  .panel-header h3 { margin: 0; color: #fff; }
  .close-btn {
    background: none;
    border: none;
    color: #888;
    font-size: 20px;
    cursor: pointer;
  }
  .section {
    margin-bottom: 16px;
    padding-bottom: 12px;
    border-bottom: 1px solid #333;
  }
  h4 { margin: 0 0 8px; color: #aaa; font-size: 12px; text-transform: uppercase; }
  h5 { margin: 8px 0 4px; color: #888; }
  label {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 6px;
  }
  select, input {
    background: #2a2a3e;
    color: #ddd;
    border: 1px solid #444;
    padding: 4px 8px;
    border-radius: 4px;
    width: 120px;
  }
  button {
    background: #4a9eff;
    color: white;
    border: none;
    padding: 6px 14px;
    border-radius: 4px;
    cursor: pointer;
    margin-top: 6px;
    width: 100%;
  }
  button:hover { background: #3a8eef; }
  .signal-grid {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 6px;
  }
  .signal-item {
    display: flex;
    align-items: center;
    gap: 4px;
    font-size: 11px;
  }
  .signal-led {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #333;
    border: 1px solid #555;
  }
  .signal-led.on { background: #0f0; }
  .route-item {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 4px 0;
    font-size: 12px;
  }
  .route-type {
    background: #333;
    padding: 2px 6px;
    border-radius: 3px;
    font-size: 10px;
  }
  .delete-btn {
    background: #c33;
    width: auto;
    padding: 2px 8px;
    margin: 0;
    font-size: 14px;
  }
  .new-route {
    margin-top: 8px;
    padding-top: 8px;
    border-top: 1px dashed #444;
  }
  .wifi-status {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 8px;
  }
  .wifi-mode {
    padding: 2px 6px;
    border-radius: 3px;
    font-size: 10px;
    font-weight: bold;
    background: #c93;
    color: #000;
  }
  .wifi-mode.sta { background: #3c9; }
  .wifi-current {
    font-size: 11px;
    color: #aaa;
    margin-bottom: 6px;
  }
  .wifi-msg {
    font-size: 11px;
    color: #4a9eff;
    margin-top: 4px;
  }
  button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
</style>

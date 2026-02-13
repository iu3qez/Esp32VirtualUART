<script>
  import { fetchSystem } from './api.js';
  import { onMount } from 'svelte';

  let system = {};

  onMount(() => {
    async function poll() {
      try {
        system = await fetchSystem();
      } catch (e) {
        system = { firmware: 'Disconnected', version: '-' };
      }
    }
    poll();
    const interval = setInterval(poll, 5000);
    return () => clearInterval(interval);
  });
</script>

<div class="status-bar">
  <span class="brand">Virtual UART</span>
  <span class="info">{system.firmware || ''} v{system.version || '?'}</span>
  <span class="sep">|</span>
  <span class="info">Ports: {system.portCount || 0}</span>
  <span class="sep">|</span>
  <span class="info">Routes: {system.activeRoutes || 0}</span>
  <span class="sep">|</span>
  <span class="info">Heap: {system.freeHeap ? Math.round(system.freeHeap / 1024) + 'KB' : '?'}</span>
  <span class="sep">|</span>
  <span class="info">Up: {system.uptime ? Math.round(system.uptime / 60) + 'm' : '?'}</span>
</div>

<style>
  .status-bar {
    position: fixed;
    bottom: 0;
    left: 0;
    right: 0;
    height: 28px;
    background: #1a1a2e;
    border-top: 1px solid #333;
    display: flex;
    align-items: center;
    padding: 0 12px;
    gap: 8px;
    font-size: 12px;
    color: #888;
    z-index: 100;
  }
  .brand {
    color: #4a9eff;
    font-weight: bold;
  }
  .sep { color: #444; }
</style>

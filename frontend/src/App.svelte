<script>
  import { onMount } from 'svelte';
  import NodeEditor from './lib/NodeEditor.svelte';
  import ConfigPanel from './lib/ConfigPanel.svelte';
  import StatusBar from './lib/StatusBar.svelte';
  import { ports, refreshPorts } from './stores/ports.js';
  import { routes, refreshRoutes } from './stores/routes.js';
  import { updateSignal, updateDataFlow } from './stores/signals.js';
  import { connectSignals, connectMonitor, createRoute } from './lib/api.js';

  let selectedPortId = null;
  let showPanel = false;

  function onSelectPort(id) {
    selectedPortId = id;
    showPanel = id !== null;
  }

  function openSettings() {
    selectedPortId = null;
    showPanel = true;
  }

  async function onCreateRoute(srcId, dstId) {
    try {
      await createRoute({
        type: 0, // Bridge by default
        srcPortId: srcId,
        dstPortIds: [dstId],
      });
    } catch (e) {
      console.error('createRoute failed:', e);
    }
    await refreshRoutes();
  }

  onMount(async () => {
    await refreshPorts();
    await refreshRoutes();

    // Live WebSocket updates
    connectSignals(updateSignal);
    connectMonitor(updateDataFlow);

    // Periodic refresh
    const interval = setInterval(async () => {
      await refreshPorts();
      await refreshRoutes();
    }, 5000);

    return () => clearInterval(interval);
  });

  $: selectedPort = $ports.find(p => p.id === selectedPortId) || null;
</script>

<main>
  <NodeEditor ports={$ports}
              routes={$routes}
              {selectedPortId}
              {onSelectPort}
              {onCreateRoute} />

  {#if showPanel}
    <ConfigPanel {selectedPort}
                 ports={$ports}
                 routes={$routes}
                 onClose={() => { showPanel = false; selectedPortId = null; }} />
  {/if}

  <button class="settings-btn" on:click={openSettings} title="WiFi &amp; Settings">&#9881;</button>

  <StatusBar />
</main>

<style>
  :global(body) {
    margin: 0;
    padding: 0;
    overflow: hidden;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    background: #111;
  }
  main {
    width: 100vw;
    height: 100vh;
    position: relative;
  }
  .settings-btn {
    position: fixed;
    top: 10px;
    right: 10px;
    z-index: 99;
    background: #1a1a2e;
    color: #aaa;
    border: 1px solid #444;
    border-radius: 6px;
    font-size: 20px;
    width: 36px;
    height: 36px;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .settings-btn:hover { color: #4a9eff; border-color: #4a9eff; }
</style>

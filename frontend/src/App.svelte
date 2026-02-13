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

  async function onCreateRoute(srcId, dstId) {
    await createRoute({
      type: 0, // Bridge by default
      srcPortId: srcId,
      dstPortIds: [dstId],
    });
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
</style>

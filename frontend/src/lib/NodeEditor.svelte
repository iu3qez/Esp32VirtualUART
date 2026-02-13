<script>
  import PortNode from './PortNode.svelte';
  import Wire from './Wire.svelte';
  import { PORT_COLORS } from '../stores/ports.js';
  import { dataFlow } from '../stores/signals.js';

  export let ports = [];
  export let routes = [];
  export let selectedPortId = null;
  export let onSelectPort = () => {};
  export let onCreateRoute = () => {};

  // Node positions (port ID -> {x, y})
  let positions = {};

  // Dragging a wire from a connector
  let wireStart = null; // { portId, x, y }
  let wireEnd = { x: 0, y: 0 };
  let drawingWire = false;

  // SVG pan/zoom
  let viewBox = { x: 0, y: 0, w: 1200, h: 700 };
  let panning = false;
  let panStart = { x: 0, y: 0 };

  // Auto-layout initial positions
  $: {
    ports.forEach((p, i) => {
      if (!positions[p.id]) {
        const col = p.type; // group by type
        const row = Object.values(positions).filter(
          (pos, idx) => ports[idx]?.type === p.type
        ).length;
        positions[p.id] = {
          x: 80 + col * 250,
          y: 60 + row * 150,
        };
      }
    });
  }

  function getConnectorPos(portId, side) {
    const pos = positions[portId] || { x: 0, y: 0 };
    return {
      x: pos.x + (side === 'output' ? 160 : 0),
      y: pos.y + 60,
    };
  }

  function onConnectorMouseDown(portId, side, e) {
    const pos = getConnectorPos(portId, side);
    wireStart = { portId, ...pos };
    wireEnd = { ...pos };
    drawingWire = true;
    e.preventDefault();
  }

  function onConnectorMouseUp(portId, side, e) {
    if (drawingWire && wireStart && wireStart.portId !== portId) {
      onCreateRoute(wireStart.portId, portId);
    }
    drawingWire = false;
    wireStart = null;
  }

  function onSvgMouseMove(e) {
    if (drawingWire) {
      const svg = e.currentTarget;
      const pt = svg.createSVGPoint();
      pt.x = e.clientX;
      pt.y = e.clientY;
      const svgPt = pt.matrixTransform(svg.getScreenCTM().inverse());
      wireEnd = { x: svgPt.x, y: svgPt.y };
    }
    if (panning) {
      viewBox = {
        ...viewBox,
        x: viewBox.x - (e.clientX - panStart.x),
        y: viewBox.y - (e.clientY - panStart.y),
      };
      panStart = { x: e.clientX, y: e.clientY };
    }
  }

  function onSvgMouseDown(e) {
    if (e.target.tagName === 'svg' || e.target.classList.contains('bg')) {
      panning = true;
      panStart = { x: e.clientX, y: e.clientY };
      onSelectPort(null);
    }
  }

  function onSvgMouseUp() {
    drawingWire = false;
    wireStart = null;
    panning = false;
  }

  function onWheel(e) {
    const scale = e.deltaY > 0 ? 1.1 : 0.9;
    const cx = viewBox.x + viewBox.w / 2;
    const cy = viewBox.y + viewBox.h / 2;
    viewBox = {
      x: cx - (viewBox.w * scale) / 2,
      y: cy - (viewBox.h * scale) / 2,
      w: viewBox.w * scale,
      h: viewBox.h * scale,
    };
    e.preventDefault();
  }
</script>

<!-- svelte-ignore a11y_no_static_element_interactions -->
<svg class="node-editor"
     viewBox="{viewBox.x} {viewBox.y} {viewBox.w} {viewBox.h}"
     on:mousemove={onSvgMouseMove}
     on:mousedown={onSvgMouseDown}
     on:mouseup={onSvgMouseUp}
     on:wheel={onWheel}>

  <!-- Background grid -->
  <defs>
    <pattern id="grid" width="40" height="40" patternUnits="userSpaceOnUse">
      <path d="M 40 0 L 0 0 0 40" fill="none" stroke="#222" stroke-width="0.5" />
    </pattern>
  </defs>
  <rect class="bg" x="{viewBox.x}" y="{viewBox.y}"
        width="{viewBox.w}" height="{viewBox.h}" fill="url(#grid)" />

  <!-- Route wires -->
  {#each routes as route}
    {@const srcPos = getConnectorPos(route.srcPortId, 'output')}
    {#each route.dstPortIds as dstId}
      {@const dstPos = getConnectorPos(dstId, 'input')}
      {@const flow = $dataFlow[route.id]}
      <Wire x1={srcPos.x} y1={srcPos.y}
            x2={dstPos.x} y2={dstPos.y}
            active={route.active}
            color={PORT_COLORS[ports.find(p => p.id === route.srcPortId)?.type] || '#666'} />
    {/each}
  {/each}

  <!-- Drawing wire -->
  {#if drawingWire && wireStart}
    <Wire x1={wireStart.x} y1={wireStart.y}
          x2={wireEnd.x} y2={wireEnd.y}
          color="#fff" />
  {/if}

  <!-- Port nodes -->
  {#each ports as port (port.id)}
    <PortNode {port}
              bind:x={positions[port.id].x}
              bind:y={positions[port.id].y}
              selected={selectedPortId === port.id}
              onSelect={onSelectPort}
              {onConnectorMouseDown}
              {onConnectorMouseUp} />
  {/each}
</svg>

<style>
  .node-editor {
    width: 100%;
    height: 100vh;
    background: #111;
    display: block;
  }
</style>

<script>
  import { PORT_TYPES, PORT_COLORS, SIGNAL_NAMES } from '../stores/ports.js';
  import { liveSignals } from '../stores/signals.js';

  export let port;
  export let x = 0;
  export let y = 0;
  export let selected = false;
  export let onSelect = () => {};
  export let onDragStart = () => {};
  export let onConnectorMouseDown = () => {};
  export let onConnectorMouseUp = () => {};

  $: signals = $liveSignals[port.id] || port.signals || {};
  $: color = PORT_COLORS[port.type] || '#888';
  $: typeName = PORT_TYPES[port.type] || '?';

  function onMouseDown(e) {
    if (e.target.closest('.connector-group')) return;
    onSelect(port.id);
    onDragStart(port.id, e);
    e.preventDefault();
  }
</script>

<!-- svelte-ignore a11y_no_static_element_interactions -->
<g transform="translate({x},{y})"
   on:mousedown={onMouseDown}
   class="port-node"
   class:selected>

  <!-- Background -->
  <rect width="160" height="120" rx="8" ry="8"
        fill="#1e1e2e" stroke={color} stroke-width={selected ? 2.5 : 1.5} />

  <!-- Header -->
  <rect width="160" height="28" rx="8" ry="8" fill={color} opacity="0.9" />
  <rect y="20" width="160" height="8" fill={color} opacity="0.9" />
  <text x="80" y="19" text-anchor="middle" fill="white" font-size="13" font-weight="bold">
    {port.name}
  </text>
  <text x="148" y="18" text-anchor="end" fill="rgba(255,255,255,0.7)" font-size="10">
    {typeName}
  </text>

  <!-- Baud rate -->
  <text x="80" y="46" text-anchor="middle" fill="#ccc" font-size="11">
    {port.lineCoding?.baudRate || '?'} baud
  </text>

  <!-- Signal LEDs -->
  {#each SIGNAL_NAMES as sig, i}
    <g transform="translate({12 + i * 25}, 56)">
      <circle r="5" cx="8" cy="0"
              fill={signals[sig.toLowerCase()] ? '#0f0' : '#333'}
              stroke="#555" stroke-width="0.5" />
      <text x="8" y="14" text-anchor="middle" fill="#999" font-size="8">{sig}</text>
    </g>
  {/each}

  <!-- State indicator -->
  <text x="80" y="100" text-anchor="middle" fill="#888" font-size="10">
    {['Disabled', 'Ready', 'Active', 'Error'][port.state] || '?'}
  </text>

  <!-- Input connector (left) -->
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <g class="connector-group input"
     on:mouseup={(e) => onConnectorMouseUp(port.id, 'input', e)}>
    <circle class="connector input" cx="0" cy="60" r="11"
            fill="#333" stroke={color} stroke-width="2" />
    <text x="14" y="64" fill={color} font-size="10" font-weight="bold">In</text>
  </g>

  <!-- Output connector (right) -->
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <g class="connector-group output"
     on:mousedown={(e) => { e.stopPropagation(); onConnectorMouseDown(port.id, 'output', e); }}>
    <circle class="connector output" cx="160" cy="60" r="11"
            fill="#333" stroke={color} stroke-width="2" />
    <text x="146" y="64" text-anchor="end" fill={color} font-size="10" font-weight="bold">Out</text>
  </g>
</g>

<style>
  .port-node { cursor: grab; }
  .port-node:active { cursor: grabbing; }
  .connector-group { cursor: crosshair; }
  .connector-group:hover circle { fill: #555; }
  .selected rect:first-child {
    filter: drop-shadow(0 0 6px rgba(255,255,255,0.3));
  }
</style>

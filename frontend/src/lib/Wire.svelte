<script>
  export let x1 = 0;
  export let y1 = 0;
  export let x2 = 0;
  export let y2 = 0;
  export let active = false;
  export let color = '#666';

  // Bezier control points for smooth curves
  $: dx = Math.abs(x2 - x1) * 0.5;
  $: path = `M ${x1} ${y1} C ${x1 + dx} ${y1}, ${x2 - dx} ${y2}, ${x2} ${y2}`;
</script>

<g class="wire" class:active>
  <!-- Shadow/glow for active wires -->
  {#if active}
    <path d={path} fill="none" stroke={color} stroke-width="4" opacity="0.3" />
  {/if}
  <path d={path} fill="none" stroke={color} stroke-width="2"
        stroke-dasharray={active ? 'none' : '6,3'} />
</g>

<style>
  .active path {
    animation: flow 1s linear infinite;
  }
  @keyframes flow {
    to { stroke-dashoffset: -12; }
  }
</style>

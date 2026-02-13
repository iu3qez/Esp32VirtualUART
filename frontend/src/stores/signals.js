import { writable } from 'svelte/store';

// Live signal state per port: { portId: { dtr, rts, cts, dsr, dcd, ri } }
export const liveSignals = writable({});

// Data flow per route: { routeId: { bytesSrcToDst, bytesDstToSrc } }
export const dataFlow = writable({});

export function updateSignal(msg) {
  if (msg.type === 'signal') {
    liveSignals.update(s => ({ ...s, [msg.portId]: msg.signals }));
  }
}

export function updateDataFlow(msg) {
  if (msg.type === 'dataFlow') {
    dataFlow.update(d => ({ ...d, [msg.routeId]: {
      bytesSrcToDst: msg.bytesSrcToDst,
      bytesDstToSrc: msg.bytesDstToSrc,
    }}));
  }
}

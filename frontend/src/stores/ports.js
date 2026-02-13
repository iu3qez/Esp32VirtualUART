import { writable } from 'svelte/store';
import { fetchPorts } from '../lib/api.js';

export const ports = writable([]);

export async function refreshPorts() {
  const data = await fetchPorts();
  ports.set(data);
}

// Port type labels
export const PORT_TYPES = ['CDC', 'UART', 'TCP'];

// Port type colors
export const PORT_COLORS = {
  0: '#4a9eff', // CDC - blue
  1: '#4caf50', // UART - green
  2: '#ff9800', // TCP - orange
};

// Signal names
export const SIGNAL_NAMES = ['DTR', 'RTS', 'CTS', 'DSR', 'DCD', 'RI'];

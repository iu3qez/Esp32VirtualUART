import { writable } from 'svelte/store';
import { fetchRoutes } from '../lib/api.js';

export const routes = writable([]);

export async function refreshRoutes() {
  const data = await fetchRoutes();
  routes.set(data);
}

export const ROUTE_TYPES = ['Bridge', 'Clone', 'Merge'];

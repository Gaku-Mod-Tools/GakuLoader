import { isWebView2Available, WebView2BridgeClient } from './client'
import { MockBridgeClient } from './mock'
import type { BridgeClient } from './protocol'

let client: BridgeClient | null = null

// Inside WebView2 the native bridge is used; in a plain browser (Vite dev) or in
// tests a stateful mock keeps the UI fully interactive.
export function getBridge(): BridgeClient {
  if (!client) client = isWebView2Available() ? new WebView2BridgeClient() : new MockBridgeClient()
  return client
}

export function setBridge(next: BridgeClient | null): void {
  client = next
}

export { MockBridgeClient } from './mock'
export { WebView2BridgeClient, isWebView2Available } from './client'
export * from './protocol'

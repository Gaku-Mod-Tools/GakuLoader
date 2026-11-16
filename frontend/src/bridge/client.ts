import { WebView2BridgeCore, isWebView2Available } from './core'
import type { BridgeClient, BridgeEvents, BridgeMethods } from './protocol'

export { isWebView2Available }

// Loader Manager prototype client. The transport lives in ./core so the Motion
// panel bridge (src/motion/bridge.ts) shares the exact same wire behavior.
export class WebView2BridgeClient
  extends WebView2BridgeCore<BridgeMethods, BridgeEvents>
  implements BridgeClient
{
  readonly kind = 'webview2' as const
}

import { WebView2BridgeCore, isWebView2Available } from './core'
import type { BridgeClient, BridgeEvents, BridgeMethods } from './protocol'

export { isWebView2Available }

// Loader Manager prototype client over the generic WebView2BridgeCore transport.
export class WebView2BridgeClient
  extends WebView2BridgeCore<BridgeMethods, BridgeEvents>
  implements BridgeClient
{
  readonly kind = 'webview2' as const
}

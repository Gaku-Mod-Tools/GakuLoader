import MotionApp from '@/motion/components/MotionApp'
import LoaderPrototypeApp from '@/prototype/LoaderPrototypeApp'

// Production entry is the GakuMotion panel (rendered inside the Loader plugin
// content-area WebView2). The previous Loader Manager web prototype is kept
// isolated and reachable only via ?prototype=1 for comparison/dev use; the
// real Loader Manager stays native ImGui.
export default function App() {
  const params = new URLSearchParams(window.location.search)
  if (params.get('prototype') === '1') return <LoaderPrototypeApp />
  return <MotionApp />
}

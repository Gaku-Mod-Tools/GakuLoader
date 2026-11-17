import LoaderPrototypeApp from '@/prototype/LoaderPrototypeApp'

// Isolated Loader Manager Web prototype for dev/test reference only.
// Production GakuLoader Manager stays native ImGui, and plugin Web UIs
// (such as GakuMotion) live in and are served from their own repositories.
export default function App() {
  return <LoaderPrototypeApp />
}

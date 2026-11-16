import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { WebView2BridgeClient } from '@/bridge/client'
import { BridgeError } from '@/bridge/protocol'

type Listener = (event: { data: unknown }) => void

class FakeWebview {
  posted: Record<string, unknown>[] = []
  throwOnPost = false
  private listeners = new Set<Listener>()
  postMessage(message: unknown) {
    if (this.throwOnPost) throw new Error('transport gone')
    this.posted.push(message as Record<string, unknown>)
  }
  addEventListener(_type: 'message', listener: Listener) {
    this.listeners.add(listener)
  }
  removeEventListener(_type: 'message', listener: Listener) {
    this.listeners.delete(listener)
  }
  emit(data: unknown) {
    for (const listener of this.listeners) listener({ data })
  }
}

let fake: FakeWebview

beforeEach(() => {
  fake = new FakeWebview()
  ;(window as unknown as { chrome: { webview: FakeWebview } }).chrome = { webview: fake }
})

afterEach(() => {
  delete (window as unknown as { chrome?: unknown }).chrome
  vi.useRealTimers()
})

function respond(id: number, body: Record<string, unknown>) {
  fake.emit({ v: 1, id, type: 'response', ...body })
}

describe('WebView2BridgeClient', () => {
  it('correlates requests and responses by id', async () => {
    const client = new WebView2BridgeClient()
    const promise = client.request('bridge.ping')
    expect(fake.posted).toHaveLength(1)
    const id = fake.posted[0].id as number
    expect(fake.posted[0].method).toBe('bridge.ping')
    respond(id, { ok: true, result: { pong: true } })
    await expect(promise).resolves.toEqual({ pong: true })
    client.close()
  })

  it('rejects with a structured BridgeError on failure', async () => {
    const client = new WebView2BridgeClient()
    const promise = client.request('appearance.setTheme', { theme: 9 })
    const id = fake.posted[0].id as number
    respond(id, { ok: false, error: { code: 'invalid_theme', message: 'Theme must be 0..4.' } })
    await expect(promise).rejects.toMatchObject({ code: 'invalid_theme', message: 'Theme must be 0..4.' })
    client.close()
  })

  it('ignores responses with unknown ids', async () => {
    const client = new WebView2BridgeClient()
    const promise = client.request('bridge.ping')
    const id = fake.posted[0].id as number
    respond(id + 999, { ok: true, result: { pong: true } })
    respond(id, { ok: true, result: { pong: true } })
    await expect(promise).resolves.toEqual({ pong: true })
    client.close()
  })

  it('cancels a pending request via AbortSignal', async () => {
    const client = new WebView2BridgeClient()
    const controller = new AbortController()
    const promise = client.request('host.snapshot', undefined, { signal: controller.signal })
    controller.abort()
    await expect(promise).rejects.toMatchObject({ code: 'cancelled' })
    // A late response for the cancelled id must not resolve anything.
    respond(fake.posted[0].id as number, { ok: true, result: {} })
    client.close()
  })

  it('times out a request that never receives a response', async () => {
    vi.useFakeTimers()
    const client = new WebView2BridgeClient()
    const promise = client.request('host.snapshot', undefined, { timeoutMs: 50 })
    const assertion = expect(promise).rejects.toMatchObject({ code: 'timeout' })
    await vi.advanceTimersByTimeAsync(60)
    await assertion
    client.close()
  })

  it('dispatches native events to subscribers', () => {
    const client = new WebView2BridgeClient()
    const handler = vi.fn()
    const off = client.on('shell.saveFailed', handler)
    fake.emit({ v: 1, type: 'event', name: 'shell.saveFailed', payload: { failed: true }, seq: 1, sid: 1 })
    expect(handler).toHaveBeenCalledWith({ failed: true })
    off()
    fake.emit({ v: 1, type: 'event', name: 'shell.saveFailed', payload: { failed: false }, seq: 2, sid: 1 })
    expect(handler).toHaveBeenCalledTimes(1)
    client.close()
  })

  it('adopts the session from hello and drops stale-session events', async () => {
    const client = new WebView2BridgeClient()
    const handler = vi.fn()
    client.on('host.snapshotChanged', handler)
    const hello = client.request('bridge.hello')
    respond(fake.posted[0].id as number, { ok: true, result: { protocol: 1, sessionId: 5 } })
    await hello
    fake.emit({ v: 1, type: 'event', name: 'host.snapshotChanged', payload: { available: true }, seq: 1, sid: 4 })
    expect(handler).not.toHaveBeenCalled()
    fake.emit({ v: 1, type: 'event', name: 'host.snapshotChanged', payload: { available: true }, seq: 2, sid: 5 })
    expect(handler).toHaveBeenCalledTimes(1)
    client.close()
  })

  it('invalidates pending requests on a new native session', async () => {
    const client = new WebView2BridgeClient()
    const hello = client.request('bridge.hello')
    respond(fake.posted[0].id as number, { ok: true, result: { protocol: 1, sessionId: 1 } })
    await hello
    const pending = client.request('host.snapshot')
    fake.emit({ v: 1, type: 'event', name: 'shell.ready', payload: {}, seq: 1, sid: 2 })
    await expect(pending).rejects.toMatchObject({ code: 'reset' })
    client.close()
  })

  it('rejects pending and future work when the native bridge closes', async () => {
    const client = new WebView2BridgeClient()
    const pending = client.request('host.snapshot')
    fake.emit({ v: 1, type: 'event', name: 'bridge.closed', payload: { reason: 'host_shutdown' }, seq: 1, sid: 1 })
    await expect(pending).rejects.toBeInstanceOf(BridgeError)
    await expect(client.request('bridge.ping')).rejects.toMatchObject({ code: 'closed' })
    client.close()
  })

  it('rejects with a structured error when postMessage throws', async () => {
    fake.throwOnPost = true
    const client = new WebView2BridgeClient()
    await expect(client.request('bridge.ping')).rejects.toMatchObject({ code: 'transport_error' })
    expect(fake.posted).toHaveLength(0)
    client.close()
  })

  it('ignores messages from an unsupported protocol version', async () => {
    const client = new WebView2BridgeClient()
    const handler = vi.fn()
    client.on('shell.saveFailed', handler)
    const promise = client.request('bridge.ping')
    const id = fake.posted[0].id as number
    fake.emit({ v: 2, id, type: 'response', ok: true, result: { pong: true } })
    fake.emit({ v: 2, type: 'event', name: 'shell.saveFailed', payload: { failed: true }, seq: 1, sid: 1 })
    expect(handler).not.toHaveBeenCalled()
    respond(id, { ok: true, result: { pong: true } })
    await expect(promise).resolves.toEqual({ pong: true })
    client.close()
  })
})

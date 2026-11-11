// Real UnityQuit phase transitions with a controlled resolver and game call.
#include "../src/UnityQuit.cpp"
#include <cassert>
#include <thread>
#include <cstdio>
#include <cstring>
namespace {
bool failResolve = false;
std::atomic_bool inHostShutdown{false}, allowHostShutdown{false};
int calls = 0;
void FakeQuit(int code) {
    assert(code == 0 && GakuLoader::UnityQuitIssued());
    assert(GakuLoader::GetUnityQuitPhase() == GakuLoader::UnityQuitPhase::Calling);
    ++calls;
}
}
namespace GakuLoader {
void LogInfo(const char*, ...) {}
void LogWarn(const char*, ...) {}
void LogError(const char*, ...) {}
void LogDebug(const char*, ...) {}
void* ResolveIl2cppClass(const char*, const char*, const char*) {
    assert(GetUnityQuitPhase() == UnityQuitPhase::Resolving);
    return failResolve ? nullptr : reinterpret_cast<void*>(1);
}
void* ResolveIl2cppMethod(void*, const char*, int) { return reinterpret_cast<void*>(&FakeQuit); }
void DisableAllHooksForExit() {
    assert(GetUnityQuitPhase() == UnityQuitPhase::HostShutdown && !UnityQuitIssued());
    inHostShutdown.store(true);
    while (!allowHostShutdown.load()) Sleep(1);
}
}
int main(int argc, char** argv) {
    failResolve = argc > 1 && std::strcmp(argv[1], "fail") == 0;
    if (failResolve) {
        GakuLoader::UnityQuitImpl::IssueQuitOnMainThread();
        assert(GakuLoader::GetUnityQuitPhase() == GakuLoader::UnityQuitPhase::ResolveFailed);
        assert(!GakuLoader::UnityQuitIssued() && calls == 0);
    } else {
        std::thread observer([] {
            while (!inHostShutdown.load()) Sleep(1);
            // A resolved method plus a blocked plugin callback must report
            // pending host teardown, never "method could not be resolved".
            assert(GakuLoader::GetUnityQuitPhase() == GakuLoader::UnityQuitPhase::HostShutdown);
            assert(!GakuLoader::UnityQuitIssued());
            allowHostShutdown.store(true);
        });
        GakuLoader::UnityQuitImpl::IssueQuitOnMainThread();
        observer.join();
        assert(GakuLoader::GetUnityQuitPhase() == GakuLoader::UnityQuitPhase::Returned);
        assert(GakuLoader::UnityQuitIssued() && calls == 1);
    }
    puts("PASS: atomic quit phase distinguishes resolve failure, pending host teardown and actual game call");
}

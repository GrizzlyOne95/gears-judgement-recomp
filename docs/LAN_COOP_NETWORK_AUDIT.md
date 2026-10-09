# Judgment LAN campaign co-op: compatibility investigation

Status: **source-level feasibility audit, not a working multiplayer implementation**.  
Baseline: OverkillLabs3 `v0.1.0`, commit `c20e7b3db6b56e3529e6df241a9d4e4ab7f0b2ee`.  
ReXGlue SDK baseline required by [BUILDING.md](../BUILDING.md): `0.10.0.9-dev`, commit `923c1a5`.

## Objective and boundaries

Target **two native Windows PCs running the same compatible retail Judgment XEX** in the same LAN campaign session, with a separate local player identity on each PC. Do not start by emulating Xbox Live matchmaking or Microsoft authentication. Explicit host IP is an acceptable diagnostic join path, **if** the game exposes/accepts the required session workflow.

This repo currently documents **single-player only**; campaign co-op and system-link availability in the recompiled build have **not been demonstrated**. First establish which multiplayer code paths are reachable from the game menu and whether they require Live services. Never treat successful Xbox API return codes as proof of a functional network session.

## Measured from source: SDK gaps

See the **specified SDK commit** (not an arbitrary newer SDK release):

1. [`src/kernel/xam/apps/xgi_app.cpp`](https://github.com/rexglue/rexglue-sdk/blob/923c1a5/src/kernel/xam/apps/xgi_app.cpp)
   - XGI `0x000B0010` session create logs arguments then returns success without persisting session state.
   - `0x000B0012` join local/remote similarly logs and returns success.
   - `0x000B0016` search and `0x000B001C` search extended return success without populating session results.
   - `0x000B0014` start, `0x000B0015` end, `0x000B001D` get-details, and related calls need consistent shared session state.
2. [`src/kernel/xam/xam_user.cpp`](https://github.com/rexglue/rexglue-sdk/blob/923c1a5/src/kernel/xam/xam_user.cpp)
   - `XamSessionCreateHandle` returns fixed `0xCAFEDEAD`; `XamSessionRefObjByHandle` returns `0xDEADF00D`. These are placeholder handles, not a per-session object lifetime.
   - `XamUserGetXUID`, sign-in state and profile data exist for local player index 0, but **separate-PC identity uniqueness must be checked**.
3. [`src/kernel/xam/xam_net.cpp`](https://github.com/rexglue/rexglue-sdk/blob/923c1a5/src/kernel/xam/xam_net.cpp)
   - `XNetGetTitleXnAddr` advertises loopback, not a reachable LAN address.
   - `XNetXnAddrToInAddr` / reverse conversion fail, the system-link port handler returns failure, `XNetInAddrToString` returns a placeholder string, and `XNetQosListen` returns failure.
   - Basic socket APIs (`socket`, `bind`, `sendto`, `recvfrom`) exist. This **does not establish working Xbox multiplayer transport**.
4. [`src/system/xsocket.cpp`](https://github.com/rexglue/rexglue-sdk/blob/923c1a5/src/system/xsocket.cpp)
   - Native send/receive is present, but the secure-XNet packet handling is commented out.

These are SDK-wide deficiencies. Decide whether fixes belong in a **small, version-pinned ReXGlue fork/patch set** or narrow Judgment-specific wrappers. Avoid duplicating broad networking code in a proprietary-game-specific patch when it can be isolated in reusable runtime components.

## First milestone: capture a reproducible multiplayer API trace

This is a **diagnostics-only** milestone; no fake success codes, injected host sessions or gameplay changes.

1. Acquire the *supported retail executable* (`4D530A26`, media ID `3528321A`) lawfully and generate the project's C++ using the pinned ReXGlue SDK. The private/debug v845 XEX used in the separate PC-native conversion project is **not interchangeable**.
2. Inspect `generated/default/gowj_funcs.h` and the generated import call sites. Enumerate actually referenced `__imp__XamSession*`, `__imp__NetDll_*`, `__imp__XamUser*` and relevant `XGI` message dispatches. **Do not assume every SDK export is used by Judgment.**
3. Make a diagnostic-only, opt-in logger, e.g. `GOWJ_NET_TRACE=1`, with no changes to guest registers, return values, or guest buffers. Cover: XGI app ID `0xFB` session messages, Xam user/session calls, XNet address calls, socket startup/bind/connect/send/receive and HRESULT/WSA errors. Log relative timestamps, player/session IDs (redacted where appropriate), argument lengths, buffer capacities, and return values, with throttling for packet volume.
4. Follow the project's existing **export-thunk interception precedent**, [`src/gowj_content_probe.cpp`](../src/gowj_content_probe.cpp), if an imported function must be instrumented. That code documents that `PPCFuncMappings` and taking `&__imp__...` did **not** hook generated direct imports; it instead patches a validated exported 16-byte thunk in `rexruntime.dll` and calls through to its saved body. Use guards and check each target's actual signature/import presence. For XGI dispatch, prefer SDK-level instrumentation if no equivalent callable exported thunk exists.
5. Capture comparable logs from: boot -> profile -> campaign lobby/menu -> attempt host; then boot -> attempt search/join on a second PC. Record whether menus are reachable and where the state machine stops. A title that requires Live before entering co-op may need a separate profile/service shim before LAN work is possible.

**Exit criteria:** a checked-in matrix of *observed guest call sites*, SDK handlers, input/output contracts, actual failure point(s) and redacted logs, all for the pinned XEX/SDK. No proprietary generated C++ or captured retail data should enter Git history.

## Second milestone: minimal LAN-compatible session service

Only once the trace establishes Judgment's required path:

- Give each PC a persistent, distinct offline profile/XUID; verify signed-in state and guest checks rather than hardcoding global identity.
- Maintain real session records: ID, nonce, host endpoint, owner XUID, slot counts, members, status, and expiration. Session create/modify/search/join/start/end/get-details must return *the actual guest ABI's expected result structures*, sized and endian-correct.
- Implement a bounded UDP host advertisement or explicit IP query for LAN. Support bind addresses on multihomed Windows hosts: avoid selecting Hyper-V, VPN or WSL adapters for the game's listen endpoint. Include protocol and game/build version in discovery responses.
- Map Xbox XNADDR/session keys to actual peer IPv4:port and handle network-byte-order conversion. Use a consistent system-link port mapping, bounded retries, timeouts, QoS behavior if observed, and real error codes.
- Test native datagram round trips using a **non-game synthetic harness first**, then a two-PC lobby. Security note: do not accept untrusted network packet lengths/pointers without validation; bind to LAN/test interfaces rather than exposing a public listener by default.

**Exit criteria:** PC B discovers or directly joins PC A, and both processes report the same real session membership/host endpoint, with packet captures proving bidirectional traffic.

## Third milestone: campaign replication

- Verify that host and guest load the **same campaign chapter and map**; system-link lobby success by itself does not prove co-op.
- Inspect replication handshake, player controller/pawn spawning, AI authority, weapons, collision, cutscene triggers, checkpoints and next-map travel.
- Run repeated two-PC joins/disconnects and chapter transitions. Preserve separate saves. Test the failure path when a peer drops during loading.

**Exit criteria:** a repeatable playable chapter and subsequent transition with both PCs, plus logs and pass/fail checklist.

## Recommended implementation boundaries

- Keep `main` as the original functional single-player baseline until diagnostic changes are verified.
- Use `feature/lan-coop-observability` for instrumentation, `feature/lan-session-service` for the session runtime, then `feature/lan-campaign-coop` for gameplay integration; merge after review/tests and remove stale branches.
- If editing ReXGlue internals is necessary, maintain a separately pinned SDK fork/patch with an explicit dependency. The source-generated game code and game files must stay ignored.
- **Do not reuse the Gears 3 Steamworks co-op patch as-is.** That patch targets a different native Windows UE3 runtime and APIs; concepts (reservations, adapter selection, party state) are useful research, not drop-in binary compatibility.

## First practical blocker

The compatible retail `default.xex` and whole `GearGame` directory are not in this public repo (by design), so none of the generated import call-site inventory, menu-reachability tests, or two-PC behavior can be established from source alone. That is the first local validation to perform.

# Direct GXM boundary v1

The `gxm` feature owns direct Vita GXM devices, mapped allocations, offline GXP programs, indexed draws, fragment/transfer notifications and display scanout. It depends on the pinned `vitasdk-sys = 0.3.3` import stubs and `std`. GXM-only builds skip the private PVR/EGL stub archives. The maintained `graphics`, `platform` and `video` interfaces retain their existing behavior.

This implementation changes `src/gxm*`, the independent Cargo feature, the build-link selection and a shared process device guard. Authoring, Aside and platform handles remain outside this boundary.

## Programs and coordinates

`Context::new(vsync, clear_vertex, clear_fragment)` takes the same offline production clear GXP programs that the caller builds with its other shaders. The vertex clear input is `float2 a_position`; the fragment clear uniform is `float4 u_clear_color`. The boundary owns aligned copies of both GXP binaries and all patched variants.

Every fragment program retains `uniform float4 reel_clip_rect` and a `float4 : WPOS` input, and discards when either component is below the minimum or at/above the maximum. This is a top-left, half-open pixel rectangle. GXM region clip additionally rejects whole tiles. Top-left viewport and source texture coordinates are shared with the caller's logical coordinate model.

`create_program` selects native2d's 12-float interleaved vertex layout: position2, uv2, color4 and window4 at offsets 0, 2, 4 and 8. `create_program_with_layout` accepts another explicit f32 layout. Vertex programs are prepared for both index widths, and fragment programs for replacement and premultiplied source-over blending. Program creation checks the GXP header, declared length, stage and required clip parameter, then uses the SDK validation and patcher. Program input is offline compiled executable content and should come from the application's trusted build pipeline.

Numeric uniforms use reflected default-buffer parameters; `[0]` suffixes normalize to the GXP array name. Integers become float values. Sampler integers validate the compiled TEXUNIT assignment. Reflection and patching occur during preparation, while draws consume borrowed values synchronously into the GXM uniform rings.

## Allocation contracts

The pure `texture_layout` function describes ordinary linear sampled storage. Width rounds up to eight texels for implicit pitch, preserving repeat addressing. Luma8 uses `(R,R,R,1)` and LumaAlpha8 `(R,R,R,G)`, so existing NV12 chroma `.ra` consumers read U and V.

`target_layout` and `create_target_texture` share the explicit RGBA8 strided layout: width rounds up to two pixels, as required by color surfaces. Strided target views use clamp addressing. `target_footprint` queries firmware render-target backing with exactly the configured scene capacity and reports its separately allocated uncached kernel capacity. RT driver blocks are never application GPU-mapped.

BC1/BC3 inputs contain portable row-major blocks over physical power-of-two dimensions, at least 4 by 4. Creation writes Morton block order directly into the final mapped allocation; it consumes no full-payload temporary staging. Replacement waits real readers when necessary, then writes the final allocation. Block-internal bytes remain unchanged. Allocation bytes equal the pure layout result; suballocator gaps belong to native reserved capacity.

CDRAM textures and uncached buffers share coalescing suballocation arenas. A 256-KiB CDRAM kernel granule serves many small resources. Empty blocks retire when their final allocation retires. The default caller pixel arena cap is 96 MiB, the buffer arena cap 16 MiB, and simultaneously owned transfer staging is capped at 4 MiB within that buffer cap. These three capacities are configurable u64 byte counts.

The device separately owns three 960 by 544 scanout buffers, capped at 8 MiB of native CDRAM blocks. Defaults also reserve a 16-MiB CDRAM parameter buffer, 128-KiB VDM ring, 2-MiB vertex ring, 512-KiB fragment ring, 16-KiB fragment USSE ring, and shader-patcher pools of 1 MiB, 256 KiB and 512 KiB. `memory_usage` reports kernel reservation, live payload, RT backing, patcher host storage and display capacity separately. Upload staging is included in the buffer arena and must not be added twice.

## Submission, updates and retirement

A target/viewport switch ends the current scene and begins the next. Every ended scene retains its original target, sampled allocations, geometry and patched programs until the fragment notification completes. The queue owns these leases even when a caller drops its fence. There are 256 reusable generation-tagged notification slots. Actual slot exhaustion waits the oldest real notification before reusing a slot.

Cold CPU uploads copy visible rows directly into unused mapped storage. An in-flight sampled allocation is updated through owned upload staging and `sceGxmTransferCopy` with fragment synchronization. All scenes use matching fragment/transfer synchronization. The final transfer chunk publishes its own notification, retaining both staging and destination. Uploads split into bounded row groups when they exceed the staging cap. Admission polls and waits an existing transfer only at the explicit cap. Candidate cancellation leaves already submitted native owners with their notifications.

Vertex/index range updates retain the shared interface's vertex/element units and require all GPU readers to have completed. They poll completed native leases first and update the original allocation within capacity. Sequential draws reuse a bounded identity-index allocation with at most 1,048,576 elements.

`fence` ends the current scene and returns the most recent real fragment/transfer completion token. `flush` submits via EndScene. `finish` and `reset` explicitly drain context work, transfers and queued display callbacks. Target switches perform no `sceGxmFinish` barrier.

## Display and close

Each scanout buffer has one stable synchronization object. Admission occurs before rendering to a display slot; the queue holds at most two pending entries. The callback only changes the display, waits for a requested VBLANK handoff and publishes bounded atomic completion/error data. Application callbacks stay on the owner thread. Fragment completion retires general inputs, while display sync separately gates scanout reuse.

`close` requires all caller resources and fences to be released and returns the context on rejection or native failure. It drains GPU work and callbacks, calls `sceDisplaySetFrameBuf(NULL, NEXTVSYNC)` and then `sceDisplayWaitSetFrameBuf` before releasing scanout. Native retirement/submission failure records the first terminal fault and preserves unresolved backing and callback/program storage through process termination.

## Verification

CPU checks exercise actual layout functions, rectangular Morton block addressing, odd YUV pitches and the arena free-interval implementation:

```sh
cargo test --lib --features gxm
cargo check --lib --features graphics
```

Target compilation and strict linting use VitaSDK with the application's normal `panic_unwind` build:

```sh
VITASDK=/usr/local/vitasdk CARGO_INCREMENTAL=0 RUSTC_BOOTSTRAP=1 \
cargo clippy --lib --features gxm --target armv7-sony-vita-newlibeabihf \
  -Z build-std=std,panic_unwind -- -D warnings
```

These checks establish CPU contracts and target API compatibility. Device rendering, scanout timing, shader execution and performance remain hardware validation milestones. Production `read_rgba` provides explicit drained top-left capture; the checks above do not perform screenshot/readback tests.

## Primary references

The local Sony SDK declarations and guides define the native behavior used here: `gxm/context.h` (scene, viewport and no-depth contracts), `gxm/surface.h` (two-pixel color pitch), `gxm/texture.h` and GPU User's Guide pp41-45 (linear/strided/BC layouts), `gxm/transfer.h` and libgxm Overview pp39-40 (ordered transfers), `gxm/init.h` and `gxm/display_queue.h` (callback and notification ownership), Memory Management Tutorial pp4-7 (memory domains), and Display Reference pp9-10,17 (NULL black output and VBLANK handoff). No SDK document bodies are redistributed.

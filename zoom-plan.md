# Fusion battlefield zoom

## Summary

Implement zoom for the validated Fusion build first, with Wine as the initial validation environment. Preserve the existing resolution and native battlefield rendering size; zoom by cropping and enlarging the battlefield before composing the UI.

Feasibility is positive in principle. Complete scene rendering, special command modes, and performance remain release gates.

## Controls and presentation

- Start each mission at **1×**, the widest permitted view. Wheel up/down selects **1×, 1.25×, 1.5×, 1.75×, or 2×** around the native battlefield viewport’s center.
- Animate toward the selected level over **150 ms**, restarting from the current displayed scale when another wheel event arrives. Middle click returns to 1×.
- Accept zoom controls only over the battlefield when no modal UI owns input. Accumulate partial wheel deltas. Preserve existing keyboard controls.
- Pause transitions and ignore zoom commands during left/right button drags; resume the pending transition after release.
- Draw five small vertical dots at the left screen edge while zoom input is active, filling upward from 1× to 2× and hiding shortly after input stops. Keep them unscaled and non-interactive; menus obscure them normally. Allow `ZoomIndicatorAnchor=left|right|hidden` in `[Game]`.
- Add `[Game] ZoomMode=off|steps|smooth` to the existing selected INI. Development defaults to `off`; switch the validated Fusion default to `smooth` after acceptance. Unsupported builds remain disabled.

## Implementation

**Compatibility and integration**

- Gate the new hooks on detected Fusion identity plus instruction signatures at every new patch site. Forced profile selection must not bypass this check.
- Reuse the existing patch lifecycle with reversible call-site and vtable patches that retain original callbacks. Avoid a new hooking dependency.
- Keep exported CAD interfaces and shared module layouts unchanged. Store zoom state internally: target scale, animation state, viewport, and the transform associated with the last successfully presented frame.
- Restore hooks and release buffers on shutdown. Allocation or signature failures disable zoom while retaining existing resolution functionality.

**Battlefield composition**

- Establish separate world-rendering and presentation phases around the verified native world callback and existing decoration hook.
- Maintain a complete, clean battlefield source before decorative UI draws. Initialize it through the native full-invalidation sequence, including companion camera/world state updates.
- Keep decoration writes out of the clean source: compose into a separate presentation buffer, temporarily routing the required renderer pointers and restoring them afterward. Separate presentation coverage and fog bookkeeping from source validity.
- Refresh the battlefield cache from completed world redraw regions, applying fog exactly once. Regions hidden by HUD elements must remain valid source pixels because zooming can expose them elsewhere.
- Crop and scale the battlefield, including units, health frames, selection outlines, and world markers. Then compose decorative UI and panels at their original coordinates and size, with the physical cursor last.
- Use a 16-bit nearest-neighbor scaler with reusable coordinate lookup tables and buffers. Preserve pitch, clipping, and circular-buffer addressing. Do not change `Screen` dimensions to implement zoom.
- Reuse cached battlefield pixels on presentation ticks without a world update. Animate without advancing simulation or forcing a full world redraw per animation frame.
- At 1×, bypass scaling. Rebuild source validity after scene changes, surface restoration, and transitions between native and zoom composition.

**Input routing**

- Perform UI ownership and hit-testing using physical coordinates first.
- Wrap Fusion’s separate FILD click/drag, cursor, and tooltip callbacks. Transform their coordinates using the exact crop and sampling convention of the displayed frame.
- Cover keyboard-triggered commands and gameplay routines that read mouse globals through narrowly scoped logical-coordinate overrides. Restore physical coordinates before UI processing, cursor drawing, and edge scrolling; guard nested callbacks against double transformation.
- Preserve native camera scrolling and minimap navigation. Reset zoom on mission exit and clear pending mouse state on focus loss.

## Validation and release gates

Implement fixed-scale composition and input first, then animation and the indicator.

- **Automated checks:** coordinate mapping at every level and viewport edge; clipping and padded pitch; circular-buffer wrapping; partial wheel deltas; nested input overrides; signature rejection and patch rollback. Extend the existing native callback probes.
- **Scene correctness:** compare cached output against a diagnostic complete redraw. Exercise stationary scenes, camera movement, fog changes, combat, HUD removal, chat, pause overlays, menus, surface loss, and ticks without world rendering. Require no decorative contamination, stale regions, fog errors, or scaled HUD elements.
- **Gameplay correctness:** verify selection, dragging, movement, attacks, targeting modes, tooltips, keyboard commands, minimap interaction, and edge scrolling throughout zoom transitions.
- **Performance:** benchmark the same scenes with zoom off, fixed zoom, and animation at 1024×768, 1920×1080, and 3840×2160 under Wine. Record median/p95 frame-processing time and additional memory. Use an initial acceptance budget of **no more than 10% p95 processing-time regression**; 1× should remain within measurement noise.
- If smooth mode exceeds the budget, validate stepped mode independently. If fixed zoom fails correctness or performance, keep zoom disabled and reassess; animation fallback cannot resolve those failures.
- Build through the existing project tooling and run DLL load checks. Document Wine results and leave native Windows validation explicitly outstanding.

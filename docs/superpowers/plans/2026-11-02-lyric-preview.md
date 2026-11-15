# Expanded player lyric preview

Requested layout: put full-text/search actions beside transport controls;
use the old footer space for a subdued upcoming lyric and upward promotion.

Implemented in player.luau with a dedicated lyric-scroll.luau model:
- Transport row372px fits inside384px metadata column.
- Previous/current/next preview uses a fixed132px viewport and constant18px text.
  The attached player height is376px so the additional row has reserved space.
- Sequential lyrics follow a continuous fractional index with critically
  damped velocity; the preview rises and brightens without changing font size.
- Seeks settle immediately; missing LRC does not invent an upcoming line.
- Native labels ignore the generic height property. Each label now lives in a
  fixed44px flex row, inside a clipped132px scroll viewport. Negative vertical
  strip padding moves rows without resizing glyphs. The critically damped
  movement settles in700ms, with smooth edge fades.
- Song metadata, artwork and lyrics fade out for160ms, swap while invisible,
  then fade in for320ms. Late lyrics also fade in. Rapid song selection during
  fade-in preserves the visible song and its opacity until it fades out again;
  this policy is optional so rapid word-level updates retain their behavior.
- The final settled frame is painted even when animation becomes inactive.

Tests: verify-lyric-scroll.lua covers preview, geometry, fast retarget continuity,
seeking and track changes. verify-player.lua covers colocated actions, real
upcoming text and final-frame painting, plus existing spectrum/capture behavior.
Review identified a jump when replacing row identities during rapid promotion;
an observable position/opacity test reproduced it before the continuous-position
fix. Tests pass with the revised model. Live compositor smoothness is not
established by these model/UI-tree checks.

Full-text footer repair: bound root to the declared640×540 panel size so the
growing scroll area receives a finite height. Footer transport/mode row is312px;
clock recovery and offset controls each occupy their own conditional row.
Reserve80px when an extra row is visible, otherwise36px. Tests cover optional
Firefox recovery, offsets and100 long lyric rows in complete-text mode.
Compact capsule width is380px as requested.

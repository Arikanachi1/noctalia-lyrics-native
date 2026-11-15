# Clock around the music island

User initially specified HH: song :MM, then delegated a cleaner visual design.
Keep uninterrupted time and a1.5–3s opening; use whole HH:MM at the trailing edge
of music, on a subtle rounded background. Closed clock is centered and unadorned.
Use the existing native capsule and Luau animation; choose2200ms for opening
and closing, retaining short lyric-only fades. No hover tooltip is introduced.

1. Verify expansion remains active beyond560ms, finishes at2200ms and does not
   restart for lyric updates. Preserve continuity when expansion reverses.
2. Keep HH:MM as the same shaped label in both states, without separating its
   digits. A plain Flex column with flexGrow=1 allocates remaining space to
   music with a bounded maxWidth and moves the whole clock to the trailing edge.
   Background alpha/padding and clock opacity follow expansion continuously.
   Include clock in render deduplication so minutes stay fresh.
3. Check clock is visible throughout opening, music playback, hover and closing;
   retain click behavior, pause timeout and idle redraw suppression.
4. Build, lint, run tests and apply with the existing backup installer.

Review focus: readable clock opacity, time updates during playback, rapid lyrics
must not restart expansion, no tooltip/help text on hover.

Native regression observed in1.7.5: fixed24px clock halves inserted a visible
gap, while nested ScrollView failed to occupy its requested animated width.
Scroll applyProps sets min/max but its doLayout uses current width or default260;
horizontal Flex initially measures non-growing children without main-axis width.
Avoid this path for the compact one-line music label. Regression checks require
one collapsed label, natural expanded clock widths and a growing plain music column.
The column passes its allocated width to label cross-axis measurement, so larger
fonts cannot overlap minutes.
These checks cover the layout contract; live screen verification remains needed.

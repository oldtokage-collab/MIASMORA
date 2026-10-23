// MIASMORA — compile the per-sample DSP code for speed (v0.32)
//
// The Arduino builder puts -Os after the project's build flags, so the -O2 in
// platformio.ini never took effect and the whole firmware was built for size.
// Every source file that synthesises or processes audio includes this header
// FIRST (before any other include), so that everything after it in that file,
// the inline helpers of the headers included, too, is optimised for speed.
// Since v0.32b platformio.ini removes -Os for the whole firmware (measured
// lighter); this header stays so that the DSP code keeps -O2 even if that
// setting is ever lost.
//
// No include guard needed: it only sets a pragma for the rest of the file
// that includes it.
#if defined(ESP_PLATFORM)
#pragma GCC optimize("O2")
#endif

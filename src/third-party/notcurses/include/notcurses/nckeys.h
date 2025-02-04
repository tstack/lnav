#ifndef NOTCURSES_NCKEYS
#define NOTCURSES_NCKEYS

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NOTCURSES_FFI
#define static API
#endif

#ifndef __MINGW32__
#define API __attribute__((visibility("default")))
#else
#define API __declspec(dllexport)
#endif
#define ALLOC __attribute__((malloc)) __attribute__((warn_unused_result))

// Synthesized input events, i.e. any input event we can report that isn't
// representative of some Unicode. This covers both keyboard and mouse events,
// as well as signals and even window events.

// Rather than using one of the Private Use Areas of Unicode, we use the area
// beyond the 17 65536-entry Planes (1114112). We round up to 5000 so that it's
// trivial to identify synthesized characters based on their numeric definition
// here. This is safe, since we needn't convert these synthesized characters
// into UTF8 (they would otherwise require more than four bytes).
#define PRETERUNICODEBASE 1115000
#define preterunicode(w) ((w) + PRETERUNICODEBASE)

// Special composed key definitions. These values are added to 0x100000.
#define NCKEY_INVALID   preterunicode(0)
#define NCKEY_RESIZE    preterunicode(1) // we received SIGWINCH
#define NCKEY_UP        preterunicode(2)
#define NCKEY_RIGHT     preterunicode(3)
#define NCKEY_DOWN      preterunicode(4)
#define NCKEY_LEFT      preterunicode(5)
#define NCKEY_INS       preterunicode(6)
#define NCKEY_DEL       preterunicode(7)
#define NCKEY_BACKSPACE preterunicode(8) // backspace (sometimes)
#define NCKEY_PGDOWN    preterunicode(9)
#define NCKEY_PGUP      preterunicode(10)
#define NCKEY_HOME      preterunicode(11)
#define NCKEY_END       preterunicode(12)
#define NCKEY_F00       preterunicode(20)
#define NCKEY_F01       preterunicode(21)
#define NCKEY_F02       preterunicode(22)
#define NCKEY_F03       preterunicode(23)
#define NCKEY_F04       preterunicode(24)
#define NCKEY_F05       preterunicode(25)
#define NCKEY_F06       preterunicode(26)
#define NCKEY_F07       preterunicode(27)
#define NCKEY_F08       preterunicode(28)
#define NCKEY_F09       preterunicode(29)
#define NCKEY_F10       preterunicode(30)
#define NCKEY_F11       preterunicode(31)
#define NCKEY_F12       preterunicode(32)
#define NCKEY_F13       preterunicode(33)
#define NCKEY_F14       preterunicode(34)
#define NCKEY_F15       preterunicode(35)
#define NCKEY_F16       preterunicode(36)
#define NCKEY_F17       preterunicode(37)
#define NCKEY_F18       preterunicode(38)
#define NCKEY_F19       preterunicode(39)
#define NCKEY_F20       preterunicode(40)
#define NCKEY_F21       preterunicode(41)
#define NCKEY_F22       preterunicode(42)
#define NCKEY_F23       preterunicode(43)
#define NCKEY_F24       preterunicode(44)
#define NCKEY_F25       preterunicode(45)
#define NCKEY_F26       preterunicode(46)
#define NCKEY_F27       preterunicode(47)
#define NCKEY_F28       preterunicode(48)
#define NCKEY_F29       preterunicode(49)
#define NCKEY_F30       preterunicode(50)
#define NCKEY_F31       preterunicode(51)
#define NCKEY_F32       preterunicode(52)
#define NCKEY_F33       preterunicode(53)
#define NCKEY_F34       preterunicode(54)
#define NCKEY_F35       preterunicode(55)
#define NCKEY_F36       preterunicode(56)
#define NCKEY_F37       preterunicode(57)
#define NCKEY_F38       preterunicode(58)
#define NCKEY_F39       preterunicode(59)
#define NCKEY_F40       preterunicode(60)
#define NCKEY_F41       preterunicode(61)
#define NCKEY_F42       preterunicode(62)
#define NCKEY_F43       preterunicode(63)
#define NCKEY_F44       preterunicode(64)
#define NCKEY_F45       preterunicode(65)
#define NCKEY_F46       preterunicode(66)
#define NCKEY_F47       preterunicode(67)
#define NCKEY_F48       preterunicode(68)
#define NCKEY_F49       preterunicode(69)
#define NCKEY_F50       preterunicode(70)
#define NCKEY_F51       preterunicode(71)
#define NCKEY_F52       preterunicode(72)
#define NCKEY_F53       preterunicode(73)
#define NCKEY_F54       preterunicode(74)
#define NCKEY_F55       preterunicode(75)
#define NCKEY_F56       preterunicode(76)
#define NCKEY_F57       preterunicode(77)
#define NCKEY_F58       preterunicode(78)
#define NCKEY_F59       preterunicode(79)
#define NCKEY_F60       preterunicode(80)
// ... leave room for up to 100 function keys, egads
#define NCKEY_ENTER     preterunicode(121)
#define NCKEY_CLS       preterunicode(122) // "clear-screen or erase"
#define NCKEY_DLEFT     preterunicode(123) // down + left on keypad
#define NCKEY_DRIGHT    preterunicode(124)
#define NCKEY_ULEFT     preterunicode(125) // up + left on keypad
#define NCKEY_URIGHT    preterunicode(126)
#define NCKEY_CENTER    preterunicode(127) // the most truly neutral of keypresses
#define NCKEY_BEGIN     preterunicode(128)
#define NCKEY_CANCEL    preterunicode(129)
#define NCKEY_CLOSE     preterunicode(130)
#define NCKEY_COMMAND   preterunicode(131)
#define NCKEY_COPY      preterunicode(132)
#define NCKEY_EXIT      preterunicode(133)
#define NCKEY_PRINT     preterunicode(134)
#define NCKEY_REFRESH   preterunicode(135)
#define NCKEY_SEPARATOR preterunicode(136)
// these keys aren't generally available outside of the kitty protocol
#define NCKEY_CAPS_LOCK    preterunicode(150)
#define NCKEY_SCROLL_LOCK  preterunicode(151)
#define NCKEY_NUM_LOCK     preterunicode(152)
#define NCKEY_PRINT_SCREEN preterunicode(153)
#define NCKEY_PAUSE        preterunicode(154)
#define NCKEY_MENU         preterunicode(155)
// media keys, similarly only available through kitty's protocol
#define NCKEY_MEDIA_PLAY   preterunicode(158)
#define NCKEY_MEDIA_PAUSE  preterunicode(159)
#define NCKEY_MEDIA_PPAUSE preterunicode(160)
#define NCKEY_MEDIA_REV    preterunicode(161)
#define NCKEY_MEDIA_STOP   preterunicode(162)
#define NCKEY_MEDIA_FF     preterunicode(163)
#define NCKEY_MEDIA_REWIND preterunicode(164)
#define NCKEY_MEDIA_NEXT   preterunicode(165)
#define NCKEY_MEDIA_PREV   preterunicode(166)
#define NCKEY_MEDIA_RECORD preterunicode(167)
#define NCKEY_MEDIA_LVOL   preterunicode(168)
#define NCKEY_MEDIA_RVOL   preterunicode(169)
#define NCKEY_MEDIA_MUTE   preterunicode(170)
// modifiers when pressed by themselves. this ordering comes from the Kitty
// keyboard protocol, and mustn't be changed without updating handlers.
#define NCKEY_LSHIFT       preterunicode(171)
#define NCKEY_LCTRL        preterunicode(172)
#define NCKEY_LALT         preterunicode(173)
#define NCKEY_LSUPER       preterunicode(174)
#define NCKEY_LHYPER       preterunicode(175)
#define NCKEY_LMETA        preterunicode(176)
#define NCKEY_RSHIFT       preterunicode(177)
#define NCKEY_RCTRL        preterunicode(178)
#define NCKEY_RALT         preterunicode(179)
#define NCKEY_RSUPER       preterunicode(180)
#define NCKEY_RHYPER       preterunicode(181)
#define NCKEY_RMETA        preterunicode(182)
#define NCKEY_L3SHIFT      preterunicode(183)
#define NCKEY_L5SHIFT      preterunicode(184)
// mouse events. We encode which button was pressed into the char32_t,
// but position information is embedded in the larger ncinput event.
#define NCKEY_MOTION    preterunicode(200) // no buttons pressed
#define NCKEY_BUTTON1   preterunicode(201)
#define NCKEY_BUTTON2   preterunicode(202)
#define NCKEY_BUTTON3   preterunicode(203)
#define NCKEY_BUTTON4   preterunicode(204) // scrollwheel up
#define NCKEY_BUTTON5   preterunicode(205) // scrollwheel down
#define NCKEY_BUTTON6   preterunicode(206)
#define NCKEY_BUTTON7   preterunicode(207)
#define NCKEY_BUTTON8   preterunicode(208)
#define NCKEY_BUTTON9   preterunicode(209)
#define NCKEY_BUTTON10  preterunicode(210)
#define NCKEY_BUTTON11  preterunicode(211)

#define NCKEY_PASTE     preterunicode(300)

// we received SIGCONT
#define NCKEY_SIGNAL    preterunicode(400)

// indicates that we have reached the end of input. any further calls
// will continute to return this immediately.
#define NCKEY_EOF       preterunicode(500)

// Is this uint32_t a synthesized event?
static inline bool
nckey_synthesized_p(uint32_t w){
  return w >= PRETERUNICODEBASE && w <= NCKEY_EOF;
}

// Synonyms and aliases (so far as we're concerned)
#define NCKEY_SCROLL_UP   NCKEY_BUTTON4
#define NCKEY_SCROLL_DOWN NCKEY_BUTTON5
#define NCKEY_RETURN      NCKEY_ENTER
#define NCKEY_TAB         0x09
#define NCKEY_DC2         0x12
#define NCKEY_ESC         0x1b
#define NCKEY_GS          0x1d
#define NCKEY_SPACE       0x20

// Is this uint32_t from the Private Use Area in the BMP (Plane 0)?
static inline bool
nckey_pua_p(uint32_t w){
  return w >= 0xe000 && w <= 0xf8ff; // 6,400 codepoints
}

// Is this uint32_t a Supplementary Private Use Area-A codepoint?
static inline bool
nckey_supppuaa_p(uint32_t w){
  return w >= 0xf0000 && w <= 0xffffd; // 65,534 codepoints
}

// Is this uint32_t a Supplementary Private Use Area-B codepoint?
static inline bool
nckey_supppuab_p(uint32_t w){
  return w >= 0x100000 && w <= 0x10fffd; // 65,534 codepoints
}

// used with the modifiers bitmask. definitions come straight from the kitty
// keyboard protocol.
#define NCKEY_MOD_SHIFT      1
#define NCKEY_MOD_ALT        2
#define NCKEY_MOD_CTRL       4
#define NCKEY_MOD_SUPER      8
#define NCKEY_MOD_HYPER     16
#define NCKEY_MOD_META      32
#define NCKEY_MOD_CAPSLOCK  64
#define NCKEY_MOD_NUMLOCK  128
#define NCKEY_MOD_MOTION   256

#ifdef __cplusplus
} // extern "C"
#endif

#endif

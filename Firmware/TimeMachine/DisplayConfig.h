#pragma once

// Set to 0 for linear PWM diagnostics; default is gamma 2.2 correction.
// Override here or with a build-wide -D, not only in the .ino file.
#ifndef DISPLAY_GAMMA_ENABLED
#define DISPLAY_GAMMA_ENABLED 1
#endif

#if DISPLAY_GAMMA_ENABLED != 0 && DISPLAY_GAMMA_ENABLED != 1
#error "DISPLAY_GAMMA_ENABLED must be 0 or 1"
#endif

// Development PWM profile; overall brightness is limited by GCC.
#ifndef DISPLAY_DEV_MODE
#define DISPLAY_DEV_MODE 1
#endif

#ifndef DISPLAY_DEV_MAX_PWM
#define DISPLAY_DEV_MAX_PWM 0xFF
#endif

#if DISPLAY_DEV_MODE != 0 && DISPLAY_DEV_MODE != 1
#error "DISPLAY_DEV_MODE must be 0 or 1"
#endif

#if DISPLAY_DEV_MAX_PWM < 0 || DISPLAY_DEV_MAX_PWM > 255
#error "DISPLAY_DEV_MAX_PWM must be between 0 and 255"
#endif

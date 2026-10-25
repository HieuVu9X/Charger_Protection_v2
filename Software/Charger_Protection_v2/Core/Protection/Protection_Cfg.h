/* Protection input setup. Independent of the CubeIDE build configuration. */
#ifndef PROTECTION_CFG_H_
#define PROTECTION_CFG_H_

#define PROTECTION_MODE_RELEASE  0U
#define PROTECTION_MODE_DEBUG    1U

/* Change only this setting, then rebuild and flash the firmware.
 * RELEASE: all protection checks use the real MeasureResult.
 * DEBUG:   all protection checks use debugger-editable MeasureResult_test.
 *          The real ADC/DMA acquisition and zero-cross callbacks still run.
 *          Test data starts at zero; fill it before setting startup_complete=1.
 * A compiler -D override is supported for automated tests. */
#ifndef PROTECTION_MODE
#define PROTECTION_MODE PROTECTION_MODE_RELEASE
#endif

#if (PROTECTION_MODE != PROTECTION_MODE_RELEASE) && \
    (PROTECTION_MODE != PROTECTION_MODE_DEBUG)
#error "PROTECTION_MODE must be PROTECTION_MODE_RELEASE or PROTECTION_MODE_DEBUG"
#endif

#endif /* PROTECTION_CFG_H_ */

#ifndef __CONFIG_H__
#define __CONFIG_H__

/* hugin_min: minimal configuration for headless calibration-only build */

/* Define if you have log1p function */
#define HAVE_LOG1P 1

#define INSTALL_LOCALE_DIR "/usr/share/locale"
#define INSTALL_DATA_DIR "/usr/share/hugin/"
#define INSTALL_XRC_DIR "/usr/share/hugin/"
#define INSTALL_OSX_BUNDLE_DIR "/"

/* FFTW / epoxy / EGL / OpenMP not enabled in minimal build */
/* #undef HAVE_FFTW */
/* #undef HAVE_EPOXY */
/* #undef HAVE_EGL */
/* #undef HAVE_OPENMP */
/* #undef UNIX_SELF_CONTAINED_BUNDLE */

#endif

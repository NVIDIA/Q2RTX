#ifndef Z_FRAMES_H
#define Z_FRAMES_H

#if defined(_DEBUG) && defined(_Z_TESTMODE)

/*=========================================================================
   Sequence resolver for classnames.
  =========================================================================*/
mmove_t **z_frame_get_sequence(char *classname);

#endif

#endif

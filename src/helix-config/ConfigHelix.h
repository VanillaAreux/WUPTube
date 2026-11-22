/* Helix decoder buffer configuration for WUPTube.
 * The upstream library expects this project-specific header, but its sparse
 * source checkout does not include one.
 */
#ifndef WUPTUBE_CONFIG_HELIX_H
#define WUPTUBE_CONFIG_HELIX_H

#define MP3_MAX_OUTPUT_SIZE (1024 * 5)
#define MP3_MAX_FRAME_SIZE  (1024 * 2)
#define AAC_MAX_OUTPUT_SIZE (1024 * 8)
#define AAC_MAX_FRAME_SIZE  2100

#endif

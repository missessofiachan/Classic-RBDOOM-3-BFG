/* zlib.h — forwarding shim for Fedora 44 / zlib-ng-compat build
 *
 * The bundled minizip uses #include "../zlib.h" (relative path), so we
 * forward to the system zlib-ng-compat header to avoid duplicate struct
 * definitions and Z_EXTERN/ZEXTERN macro conflicts on Fedora 44+.
 *
 * Original file backed up as zlib.h.orig
 */
#include_next <zlib.h>

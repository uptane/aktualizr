#ifndef GARAGE_COMMON_H_
#define GARAGE_COMMON_H_

#include "ostree-core.h"

/** \file */

/** Execution mode to run garage tools in. */
enum class RunMode {
  /** Default operation. Upload objects to server if necessary and relevant. */
  kDefault = 0,
  /** Dry run. Do not upload any objects. */
  kDryRun,
  /** Walk the entire tree (without uploading). Do not assume that if an object
   * exists, its parents must also exist. */
  kWalkTree,
  /** Walk the entire tree and upload any missing objects. Do not assume that if
   * an object exists, its parents must also exist. */
  kPushTree,
};

/* sota_tools was originally designed to not depend on OSTree. This was because
 * libostree wasn't widely available in package managers so we depended on just
 * glib. This header file used to contain a copy of the definition of
 * OstreeObjectType from libostree/ostree-core.h, with an added entry for
 * OSTREE_OBJECT_TYPE_UNKNOWN at position 0 (which wasn't defined at all in
 * ostree-core.h).
 *
 * We now have a dependency on libostree, so this duplication both isn't needed
 * any more, and breaks compilation (because of duplicate definitions). We
 * still need OSTREE_OBJECT_TYPE_UNKNOWN in a few places, so define it here.
 */
const OstreeObjectType OSTREE_OBJECT_TYPE_UNKNOWN = static_cast<OstreeObjectType>(0);

#endif  // GARAGE_COMMON_H_

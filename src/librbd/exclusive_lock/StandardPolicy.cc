// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "librbd/exclusive_lock/StandardPolicy.h"
#include "librbd/ImageCtx.h"
#include "librbd/ExclusiveLock.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::ExclusiveLock::StandardPolicy "

namespace librbd {
namespace exclusive_lock {

int StandardPolicy::lock_requested(bool force) {
  ceph_assert(m_image_ctx->owner_lock.is_locked());
  ceph_assert(m_image_ctx->exclusive_lock != nullptr);

  ldout(m_image_ctx->cct, 20) << this << " " << __func__ << ": force=" << force
			      << dendl;

  return -EROFS;
}

} // namespace exclusive_lock
} // namespace librbd


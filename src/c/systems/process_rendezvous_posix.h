#pragma once

/// @file process_rendezvous_posix.h
/// @brief Internal POSIX adapter declarations for process_rendezvous in ISO
/// C23.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "systems/process_rendezvous.h"

#ifdef __cplusplus
extern "C" {
#endif

[[nodiscard]] enum process_rendezvous_error process_rendezvous_posix_create(
    struct process_rendezvous* rdv, const char* name, size_t bytes);

[[nodiscard]] enum process_rendezvous_error process_rendezvous_posix_open(
    struct process_rendezvous* rdv, const char* name);

[[nodiscard]] enum process_rendezvous_error process_rendezvous_posix_unlink(
    const char* name);

void process_rendezvous_posix_cleanup(struct process_rendezvous* rdv);

void process_rendezvous_posix_notify(const struct process_rendezvous* rdv);

void process_rendezvous_posix_wait_slice(const struct process_rendezvous* rdv,
                                         int64_t slice_ns);

void process_rendezvous_posix_sleep_us(uint32_t us);

#ifdef __cplusplus
}
#endif

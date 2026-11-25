#pragma once

// Door unit core logic. Implements the rules in
// docs/firmware/lock-command-v0.md and docs/firmware/qr-access-v0.md without
// any hardware dependency.

#include "base64url.h"
#include "body_reader.h"
#include "camera_scan.h"
#include "command_processor.h"
#include "duplicate_list.h"
#include "ed25519.h"
#include "event.h"
#include "event_outbox.h"
#include "interfaces.h"
#include "lock_controller.h"
#include "poll_parser.h"
#include "poll_scheduler.h"
#include "poll_service.h"
#include "qr_access.h"
#include "qr_event.h"
#include "qr_self_test.h"
#include "qr_test_vector.h"
#include "qr_token.h"
#include "serial_scan.h"
#include "server_clock.h"
#include "timestamp.h"
#include "timing.h"

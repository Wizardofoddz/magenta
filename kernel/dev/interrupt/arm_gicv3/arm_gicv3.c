// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2017, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <dev/interrupt/arm_gicv3_regs.h>
#include <err.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 1

void arm_gicv3_init(void) {
    printf("PIDR2=0x%x\n", GICREG(GICD_PIDR0));
}

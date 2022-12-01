/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"fmt"
	"runtime"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var cpu = Cpu{"cpu", "warning"}

type Cpu struct {
	name         string
	warningLevel string
}

func (c Cpu) Name() string {
	return c.name
}

func (c Cpu) WarningLevel() string {
	return c.warningLevel
}

func (c Cpu) Execute() {

	if runtime.NumCPU() < defaultMinCPUs {
		log.Fatal(fmt.Sprintf("System currently has %v CPU but requires %v CPUs.",
			runtime.NumCPU(), defaultMinCPUs))
	} else {
		log.Info(fmt.Sprintf("System meets the requirement of %v Virtual CPUs!", defaultMinCPUs))
	}
}

func init() {
	RegisterPreflightCheck(cpu)
}

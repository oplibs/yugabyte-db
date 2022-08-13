/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
     "fmt"
     "strconv"
     "os"
 )

 func main() {

    commandLineArgs := os.Args[1:]

    type functionPointer func()
    steps := make(map[string][]functionPointer)
    var order []string

    var versionToInstall = "2.8.1.0-b37"

    var versionToUpgrade = "2.15.0.1-b4"

    var corsOrigin = GenerateCORSOrigin()

    // Default http, but now configurable.
    var httpMode = "http"

    var postgres = Postgres{"postgres",
    "/usr/lib/systemd/system/postgresql-11.service",
    []string{"/var/lib/pgsql/11/data/pg_hba.conf",
    "/var/lib/pgsql/11/data/postgresql.conf"},
    "11", versionToInstall, httpMode}

    var prometheus = Prometheus{"prometheus",
            "/etc/systemd/system/prometheus.service",
            "/etc/prometheus/prometheus.yml",
            "2.27.1", false, versionToInstall,
            httpMode}

    var nginx = Nginx{"nginx",
                    "/etc/nginx/nginx.conf",
                    httpMode, "_", "", "",
                    versionToInstall}

    var platformInstall = Platform{"platform",
                "/etc/systemd/system/yb-platform.service",
                "/opt/yugabyte/platform.conf",
                versionToInstall, corsOrigin, false,
                httpMode}

    var platformUpgrade = Platform{"platform",
        "/etc/systemd/system/yb-platform.service",
        "/opt/yugabyte/platform.conf",
        versionToUpgrade, corsOrigin, false, httpMode}

    var commonInstall = Common{"common", versionToInstall, httpMode}

    var commonUpgrade = Common{"common", versionToUpgrade, httpMode}

    TestSudoPermission()

    if commandLineArgs[0] == "clean" {

        common := Common{"common", "", ""}

        steps[common.Name] = []functionPointer{
            common.Uninstall}

        order = []string{common.Name}

    } else if commandLineArgs[0] == "preflight" {

        Preflight("yba-installer-input.yml")

    } else if commandLineArgs[0] == "license" {

        License()

    } else if commandLineArgs[0] == "version" {

        Version("version_metadata.json")

    } else if commandLineArgs[0] == "params" {

        key := commandLineArgs[1]
        value := commandLineArgs[2]

        Params(key, value)

    } else if commandLineArgs[0] == "createBackup" {

        outputPath := commandLineArgs[1]

        dataDir := "/opt/yugabyte"
        excludePrometheus := false
        skipRestart := false
        verbose := false

        createBackupArgs := commandLineArgs[2:]

        if len(createBackupArgs) == 4 {
            dataDir = createBackupArgs[0]
            excludePrometheus, _ = strconv.ParseBool(createBackupArgs[1])
            skipRestart, _ = strconv.ParseBool(createBackupArgs[2])
            verbose, _ = strconv.ParseBool(createBackupArgs[3])

        } else if len(createBackupArgs) == 3 {
            dataDir = createBackupArgs[0]
            excludePrometheus, _ = strconv.ParseBool(createBackupArgs[1])
            skipRestart, _ = strconv.ParseBool(createBackupArgs[2])

        } else if len(createBackupArgs) == 2 {
            dataDir = createBackupArgs[0]
            excludePrometheus, _ = strconv.ParseBool(createBackupArgs[1])

        } else if len(createBackupArgs) == 1 {
            dataDir = createBackupArgs[0]

        }

        CreateBackupScript(outputPath, dataDir, excludePrometheus,
            skipRestart, verbose)

    }  else if commandLineArgs[0] == "restoreBackup" {

        inputPath := commandLineArgs[1]

        destination := "/opt/yugabyte"
        skipRestart := false
        verbose := false

        restoreBackupArgs := commandLineArgs[2:]

        if len(restoreBackupArgs) == 3 {
            destination = restoreBackupArgs[0]
            skipRestart, _ = strconv.ParseBool(restoreBackupArgs[1])
            verbose, _ = strconv.ParseBool(restoreBackupArgs[2])

        } else if len(restoreBackupArgs) == 2 {
            destination = restoreBackupArgs[0]
            skipRestart, _ = strconv.ParseBool(restoreBackupArgs[1])

        } else if len(restoreBackupArgs) == 1 {
            destination = restoreBackupArgs[0]
        }

        RestoreBackupScript(inputPath, destination,
            skipRestart, verbose)

    } else if commandLineArgs[0] == "install" {

        steps[commonInstall.Name] = []functionPointer{commonInstall.SetUpPrereqs,
            commonInstall.Uninstall, commonInstall.Install}

        steps[prometheus.Name] = []functionPointer{prometheus.SetUpPrereqs,
            prometheus.Install, prometheus.Start}

        steps[postgres.Name] = []functionPointer{postgres.SetUpPrereqs,
            postgres.Install, postgres.Restart}

        steps[platformInstall.Name] = []functionPointer{platformInstall.Install,
            platformInstall.Start}

        steps[nginx.Name] = []functionPointer{nginx.SetUpPrereqs,
            nginx.Install, nginx.Start}

        order = []string{commonInstall.Name, prometheus.Name,
            postgres.Name, platformInstall.Name, nginx.Name}

    } else if commandLineArgs[0] == "upgrade" {

        steps[commonUpgrade.Name] = []functionPointer{commonUpgrade.SetUpPrereqs,
            commonUpgrade.Upgrade}

        steps[prometheus.Name] = []functionPointer{
            prometheus.Install, prometheus.Start}

        steps[platformUpgrade.Name] = []functionPointer{
         platformUpgrade.Stop, platformUpgrade.Install, platformUpgrade.Start}

        steps[nginx.Name] = []functionPointer{nginx.SetUpPrereqs,
            nginx.Install, nginx.Start}

        order = []string{commonUpgrade.Name, prometheus.Name, platformUpgrade.Name,
                nginx.Name}

        } else if commandLineArgs[0] == "configure" {

            steps[postgres.Name] = []functionPointer{postgres.Stop, postgres.Start}

            steps[prometheus.Name] = []functionPointer{prometheus.Stop, prometheus.Start}

            steps[platformInstall.Name] = []functionPointer{platformInstall.Stop,
                platformInstall.Start}

            steps[nginx.Name] = []functionPointer{nginx.Stop, nginx.Start}

            order = []string{postgres.Name, prometheus.Name, platformInstall.Name,
              nginx.Name}

            }

    for index := range order {
        service := order[index]
        serviceSteps := steps[service]
        fmt.Println("Executing steps for action " + commandLineArgs[0] + " for service " +
        service + "!")
        for index, _ := range serviceSteps {
            serviceSteps[index]()
        }
    }
}

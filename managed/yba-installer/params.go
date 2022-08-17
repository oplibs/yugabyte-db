/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
  "gopkg.in/yaml.v3"
  "io/ioutil"
  "log"
  "strings"
 )

 func Params(key string, value string) {

    configurableParameters := make(map[string][]string)

    configurableParameters["preflight"] = []string{"overrideWarning"}

    configurableParameters["prometheusOldConfig"] = []string{"fileName",
    "scrapeInterval", "evaluationInterval", "scrapeTimeout", "privateIp",
    "externalPort"}

   configurableParameters["prometheusOldConfig"] = []string{"fileName",
   "scrapeInterval", "evaluationInterval", "scrapeTimeout", "privateIp",
   "externalPort", "appExternalPort"}

    configurableParameters["prometheusOldService"] = []string{"storagePath"}

    configurableParameters["prometheusNewService"] = []string{"storagePath",
    "maxConcurrency", "maxSamples", "timeout"}

    configurableParameters["platformOldConfig"] = []string{"fileName",
    "platformDbUser", "platformdDbPassword", "devopsHome",
    "swamperTargetPath", "metricsUrl", "useOauth", "ybSecurityType",
    "ybOidcClientId", "ybOldcSecret", "ybOidcDiscoveryUri",
    "ywUrl", "ybOidcScope", "ybOidcEmailAttr", "privateIp",
    "externalPort"}

    configurableParameters["platformNewConfig"] = []string{"fileName",
    "platformDbUser", "platformdDbPassword", "devopsHome",
    "swamperTargetPath", "metricsUrl", "swapmerRulesPath",
    "metricsManagementUrl","useOauth", "ybSecurityType", "ybOidcClientId",
    "ybOldcSecret", "ybOidcDiscoveryUri", "ywUrl", "ybOidcScope",
    "ybOidcEmailAttr", "privateIp", "externalPort"}

    configurableParameters["platformOldService"] = []string{"useOauth",
    "ybSecurityType", "ybOidcClientId", "ybOldcSecret",
    "ybOidcDiscoveryUri", "ywUrl", "ybOidcScope", "ybOidcEmailAttr"}

    configurableParameters["platformNewService"] = []string{"useOauth",
    "ybSecurityType", "ybOidcClientId", "ybOldcSecret",
    "ybOidcDiscoveryUri", "ywUrl", "ybOidcScope", "ybOidcEmailAttr"}

    configurableParameters["nginxHttp"] = []string{"serverName",
    "privateIp", "containerExposedPort", "uploadSize"}

    configurableParameters["nginxHttps"] = []string{"serverName",
    "privateIp", "containerExposedPort", "uploadSize"}

    splitKey := strings.Split(key, ".")

    if len(splitKey) < 2 {
        log.Fatal("Invalid configuration string passed in!")
    }

    service := strings.Split(key, ".")[0]
    param := strings.Split(key, ".")[1]

    serviceConfigFields, exists := configurableParameters[service]

    if !exists {
        log.Fatal("The service " + service + " does not exist!")
    }

    if !Contains(serviceConfigFields, param) {
        log.Fatal("Parameter " + param + " for " + service + " is not " +
         "configurable!")
    }

    fileName := "yba-installer-input.yml"

    data, _ := ioutil.ReadFile(fileName)
    var inputYml map[string]interface{}

    err := yaml.Unmarshal([]byte(data), &inputYml)
    if err != nil {
        log.Fatalf("error: %v", err)
    }

    inputYml[service].(map[string]interface{})[param] = value

    updatedBytes, _ := yaml.Marshal(&inputYml)
    WriteBytes(updatedBytes, []byte(fileName))

 }

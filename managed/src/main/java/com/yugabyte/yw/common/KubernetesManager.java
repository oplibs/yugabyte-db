// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.models.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Singleton;
import java.util.List;
import java.util.Map;
import java.util.UUID;

@Singleton
public class KubernetesManager {
  public static final Logger LOG = LoggerFactory.getLogger(KubernetesManager.class);

  @Inject
  ShellProcessHandler shellProcessHandler;

  @Inject
  play.Configuration appConfig;

  public ShellProcessHandler.ShellResponse helmInit(UUID providerUUID) {
    Provider provider = Provider.get(providerUUID);
    Map<String, String> config = provider.getConfig();
    if (!config.containsKey("KUBECONFIG_SERVICE_ACCOUNT")) {
      throw new RuntimeException("Service Account is required.");
    }
    List<String> commandList = ImmutableList.of("helm",  "init",
        "--service-account",  config.get("KUBECONFIG_SERVICE_ACCOUNT"), "--upgrade", "--wait");
    return execCommand(providerUUID, commandList);
  }

  public ShellProcessHandler.ShellResponse helmInstall(UUID providerUUID, String universePrefix, String overridesFile) {
    String helmPackagePath = appConfig.getString("yb.helm.package");
    if (helmPackagePath == null || helmPackagePath.isEmpty()) {
      throw new RuntimeException("Helm Package path not provided.");
    }
    List<String> commandList = ImmutableList.of("helm",  "install",
        helmPackagePath, "--namespace", universePrefix, "--name", universePrefix, "-f", overridesFile, "--wait");
    return execCommand(providerUUID, commandList);
  }

  public ShellProcessHandler.ShellResponse getPodInfos(UUID providerUUID, String universePrefix) {
    List<String> commandList = ImmutableList.of("kubectl",  "get", "pods", "--namespace", universePrefix,
        "-o", "json", "-l", "release=" + universePrefix);
    return execCommand(providerUUID, commandList);
  }

  public ShellProcessHandler.ShellResponse helmDelete(UUID providerUUID, String universePrefix) {
    List<String> commandList = ImmutableList.of("helm",  "delete", universePrefix, "--purge");
    return execCommand(providerUUID, commandList);
  }

  public void deleteStorage(UUID providerUUID, String universePrefix) {
    // Delete Master Volumes
    List<String> masterCommandList = ImmutableList.of("kubectl",  "delete", "pvc",
        "--namespace", universePrefix, "-l", "app=yb-master");
    execCommand(providerUUID, masterCommandList);
    // Delete TServer Volumes
    List<String> tserverCommandList = ImmutableList.of("kubectl",  "delete", "pvc",
        "--namespace", universePrefix, "-l", "app=yb-tserver");
    execCommand(providerUUID, tserverCommandList);
    // TODO: check the execCommand outputs.
  }

  private ShellProcessHandler.ShellResponse execCommand(UUID providerUUID, List<String> command) {
    Provider provider = Provider.get(providerUUID);
    Map<String, String> config = provider.getConfig();
    return shellProcessHandler.run(command, config);
  }
}

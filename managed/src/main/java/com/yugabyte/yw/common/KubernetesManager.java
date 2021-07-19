// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class KubernetesManager {

  @Inject ReleaseManager releaseManager;

  @Inject ShellProcessHandler shellProcessHandler;

  @Inject play.Configuration appConfig;

  public static final Logger LOG = LoggerFactory.getLogger(KubernetesManager.class);

  private static final long DEFAULT_TIMEOUT_SECS = 300;

  private static final String SERVICE_INFO_JSONPATH =
      "{.spec.clusterIP}|" + "{.status.*.ingress[0].ip}|{.status.*.ingress[0].hostname}";

  private static final String LEGACY_HELM_CHART_FILENAME = "yugabyte-2.7-helm-legacy.tar.gz";

  public ShellResponse createNamespace(Map<String, String> config, String universePrefix) {
    List<String> commandList = ImmutableList.of("kubectl", "create", "namespace", universePrefix);
    return execCommand(config, commandList);
  }

  // TODO(bhavin192): modify the pullSecret on the fly while applying
  // it? Add nodePrefix to the name, add labels which make it easy to
  // find the secret by label selector, and even delete it if
  // required. Something like, -lprovider=gke1 or
  // -luniverse=uni1. Tracked here:
  // https://github.com/yugabyte/yugabyte-db/issues/7012
  public ShellResponse applySecret(
      Map<String, String> config, String namespace, String pullSecret) {
    List<String> commandList =
        ImmutableList.of("kubectl", "apply", "-f", pullSecret, "--namespace", namespace);
    return execCommand(config, commandList);
  }

  public String getTimeout() {
    Long timeout = appConfig.getLong("yb.helm.timeout_secs");
    if (timeout == null || timeout == 0) {
      timeout = DEFAULT_TIMEOUT_SECS;
    }
    return String.valueOf(timeout) + "s";
  }

  public ShellResponse helmInstall(
      String ybSoftwareVersion,
      Map<String, String> config,
      UUID providerUUID,
      String universePrefix,
      String namespace,
      String overridesFile) {

    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);

    List<String> commandList =
        ImmutableList.of(
            "helm",
            "install",
            universePrefix,
            helmPackagePath,
            "--namespace",
            namespace,
            "-f",
            overridesFile,
            "--timeout",
            getTimeout(),
            "--wait");
    LOG.info(String.join(" ", commandList));
    return execCommand(config, commandList);
  }

  public ShellResponse getPodInfos(
      Map<String, String> config, String universePrefix, String namespace) {
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "get",
            "pods",
            "--namespace",
            namespace,
            "-o",
            "json",
            "-l",
            "release=" + universePrefix);
    return execCommand(config, commandList);
  }

  public ShellResponse getServices(
      Map<String, String> config, String universePrefix, String namespace) {
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "get",
            "services",
            "--namespace",
            namespace,
            "-o",
            "json",
            "-l",
            "release=" + universePrefix);
    System.out.println(commandList);
    return execCommand(config, commandList);
  }

  public ShellResponse getPodStatus(Map<String, String> config, String namespace, String podName) {
    List<String> commandList =
        ImmutableList.of("kubectl", "get", "pod", "--namespace", namespace, "-o", "json", podName);
    return execCommand(config, commandList);
  }

  public ShellResponse getServiceIPs(
      Map<String, String> config, String namespace, boolean isMaster) {
    String serviceName = isMaster ? "yb-master-service" : "yb-tserver-service";
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "get",
            "svc",
            serviceName,
            "--namespace",
            namespace,
            "-o",
            "jsonpath=" + SERVICE_INFO_JSONPATH);
    return execCommand(config, commandList);
  }

  public JsonNode getNodeInfos(Map<String, String> config) {
    ShellResponse response = runGetNodeInfos(config);
    if (response.code != 0) {
      String msg = "Unable to get node information";
      if (!response.message.isEmpty()) {
        msg = String.format("%s: %s", msg, response.message);
      }
      throw new RuntimeException(msg);
    }
    return Util.convertStringToJson(response.message);
  }

  public ShellResponse runGetNodeInfos(Map<String, String> config) {
    List<String> commandList = ImmutableList.of("kubectl", "get", "nodes", "-o", "json");
    return execCommand(config, commandList);
  }

  public JsonNode getSecret(Map<String, String> config, String secretName, String namespace) {
    ShellResponse response = runGetSecret(config, secretName, namespace);
    if (response.code != 0) {
      String msg = "Unable to get secret";
      if (!response.message.isEmpty()) {
        msg = String.format("%s: %s", msg, response.message);
      }
      throw new RuntimeException(msg);
    }
    return Util.convertStringToJson(response.message);
  }

  // TODO: disable the logging of stdout of this command if possibile,
  // as it just leaks the secret content in the logs at DEBUG level.
  public ShellResponse runGetSecret(
      Map<String, String> config, String secretName, String namespace) {
    List<String> commandList = new ArrayList<String>();
    commandList.addAll(ImmutableList.of("kubectl", "get", "secret", secretName, "-o", "json"));
    if (namespace != null) {
      commandList.add("--namespace");
      commandList.add(namespace);
    }
    return execCommand(config, commandList);
  }

  public ShellResponse helmUpgrade(
      String ybSoftwareVersion,
      Map<String, String> config,
      String universePrefix,
      String namespace,
      String overridesFile) {

    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);

    List<String> commandList =
        ImmutableList.of(
            "helm",
            "upgrade",
            universePrefix,
            helmPackagePath,
            "-f",
            overridesFile,
            "--namespace",
            namespace,
            "--timeout",
            getTimeout(),
            "--wait");
    LOG.info(String.join(" ", commandList));
    return execCommand(config, commandList);
  }

  public ShellResponse updateNumNodes(Map<String, String> config, String namespace, int numNodes) {
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "--namespace",
            namespace,
            "scale",
            "statefulset",
            "yb-tserver",
            "--replicas=" + numNodes);
    return execCommand(config, commandList);
  }

  public ShellResponse helmDelete(
      Map<String, String> config, String universePrefix, String namespace) {
    List<String> commandList = ImmutableList.of("helm", "delete", universePrefix, "-n", namespace);
    return execCommand(config, commandList);
  }

  public void deleteStorage(Map<String, String> config, String universePrefix, String namespace) {
    // Delete Master Volumes
    List<String> masterCommandList =
        ImmutableList.of(
            "kubectl",
            "delete",
            "pvc",
            "--namespace",
            namespace,
            "-l",
            "app=yb-master,release=" + universePrefix);
    execCommand(config, masterCommandList);
    // Delete TServer Volumes
    List<String> tserverCommandList =
        ImmutableList.of(
            "kubectl",
            "delete",
            "pvc",
            "--namespace",
            namespace,
            "-l",
            "app=yb-tserver,release=" + universePrefix);
    execCommand(config, tserverCommandList);
    // TODO: check the execCommand outputs.
  }

  public void deleteNamespace(Map<String, String> config, String namespace) {
    // Delete Namespace
    List<String> masterCommandList = ImmutableList.of("kubectl", "delete", "namespace", namespace);
    execCommand(config, masterCommandList);
  }

  private ShellResponse execCommand(Map<String, String> config, List<String> command) {
    String description = String.join(" ", command);
    return shellProcessHandler.run(command, config, description);
  }

  public String getHelmPackagePath(String ybSoftwareVersion) {
    String helmPackagePath = null;

    // Get helm package filename from release metadata.
    ReleaseManager.ReleaseMetadata releaseMetadata =
        releaseManager.getReleaseByVersion(ybSoftwareVersion);
    if (releaseMetadata != null) {
      helmPackagePath = releaseMetadata.chartPath;
    }

    if (helmPackagePath == null || helmPackagePath.isEmpty()) {
      // TODO: The "legacy" helm chart is included in the yugaware container build to ensure that
      // universes deployed using previous versions of the platform (that did not use versioned
      // helm charts) will still be usable after upgrading to newer versions of the platform (that
      // use versioned helm charts). We can (and should) remove this special case once all customers
      // that use the k8s provider have upgraded their platforms and universes to versions > 2.7.
      if (Util.compareYbVersions(ybSoftwareVersion, "2.8.0.0") < 0) {
        helmPackagePath =
            new File(appConfig.getString("yb.helm.packagePath"), LEGACY_HELM_CHART_FILENAME)
                .toString();
      } else {
        throw new RuntimeException("Helm Package path not found for release: " + ybSoftwareVersion);
      }
    }

    // Ensure helm package file actually exists.
    File helmPackage = new File(helmPackagePath);
    if (!helmPackage.exists()) {
      throw new RuntimeException("Helm Package file not found: " + helmPackagePath);
    }

    return helmPackagePath;
  }
}
